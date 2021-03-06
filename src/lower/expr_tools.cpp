#include "expr_tools.h"

#include <stack>
#include <set>

#include "taco/expr/expr.h"
#include "taco/expr/expr_nodes.h"
#include "taco/expr/expr_visitor.h"
#include "taco/util/collections.h"

using namespace std;

namespace taco {
namespace lower {

/// Retrieves the available sub-expression at the index variable
vector<IndexExpr> getAvailableExpressions(const IndexExpr& expr,
                                          const vector<IndexVar>& vars) {

  // Available expressions are the maximal sub-expressions that only contain
  // operands whose index variables have all been visited.
  struct ExtractAvailableExpressions : public ExprVisitor {
    IndexVar var;
    set<IndexVar> visitedVars;

    /// A vector of all the available expressions
    vector<IndexExpr> availableExpressions;

    /// A stack of active expressions and a bool saying whether they are
    /// available. Expressions are moved from the stack to availableExpressions
    /// when an inactive sub-expression is found.
    stack<pair<IndexExpr,bool>> activeExpressions;

    vector<IndexExpr> get(const IndexExpr& expr, const vector<IndexVar>& vars) {
      this->visitedVars = set<IndexVar>(vars.begin(), vars.end());
      this->var = var;

      expr.accept(this);

      taco_iassert(activeExpressions.size() == 1);
      if (activeExpressions.top().second) {
        availableExpressions.push_back(activeExpressions.top().first);
      }

      // Take out available expressions that are just immediates or a scalars.
      // No point in storing these to a temporary.
      // TODO ...

      return availableExpressions;
    }

    using ExprVisitor::visit;

    void visit(const AccessNode* op) {
      bool available = true;
      for (auto& var : op->indexVars) {
        if (!util::contains(visitedVars, var)) {
          available = false;
          break;
        }
      }
      activeExpressions.push({op, available});
    }

    void visit(const UnaryExprNode* op) {
      op->a.accept(this);
      taco_iassert(activeExpressions.size() >= 1);

      pair<IndexExpr,bool> a = activeExpressions.top();
      activeExpressions.pop();

      activeExpressions.push({op, a.second});
    }

    void visit(const BinaryExprNode* op) {
      op->a.accept(this);
      op->b.accept(this);
      taco_iassert(activeExpressions.size() >= 2);

      pair<IndexExpr,bool> a = activeExpressions.top();
      activeExpressions.pop();
      pair<IndexExpr,bool> b = activeExpressions.top();
      activeExpressions.pop();

      if (a.second && b.second) {
        activeExpressions.push({op, true});
      }
      else {
        if (a.second) {
          availableExpressions.push_back(a.first);
        }
        if (b.second) {
          availableExpressions.push_back(b.first);
        }
        activeExpressions.push({op, false});
      }
    }

    // Immediates are always available (can compute them anywhere)
    void visit(const ImmExprNode* op) {
      activeExpressions.push({op,true});
    }
  };

  return ExtractAvailableExpressions().get(expr, vars);
}

IndexExpr getSubExprOld(IndexExpr expr, const vector<IndexVar>& vars) {
  class SubExprVisitor : public ExprVisitor {
  public:
    SubExprVisitor(const vector<IndexVar>& vars) {
      this->vars.insert(vars.begin(), vars.end());
    }

    IndexExpr getSubExpression(const IndexExpr& expr) {
      visit(expr);
      IndexExpr e = subExpr;
      subExpr = IndexExpr();
      return e;
    }

  private:
    set<IndexVar> vars;
    IndexExpr     subExpr;

    using ExprVisitorStrict::visit;

    void visit(const AccessNode* op) {
      // If any variable is in the set of index variables, then the expression
      // has not been emitted at a previous level, so we keep it.
      for (auto& indexVar : op->indexVars) {
        if (util::contains(vars, indexVar)) {
          subExpr = op;
          return;
        }
      }
      subExpr = IndexExpr();
    }

    void visit(const UnaryExprNode* op) {
      IndexExpr a = getSubExpression(op->a);
      if (a.defined()) {
        subExpr = op;
      }
      else {
        subExpr = IndexExpr();
      }
    }

    void visit(const BinaryExprNode* op) {
      IndexExpr a = getSubExpression(op->a);
      IndexExpr b = getSubExpression(op->b);
      if (a.defined() && b.defined()) {
        subExpr = op;
      }
      else if (a.defined()) {
        subExpr = a;
      }
      else if (b.defined()) {
        subExpr = b;
      }
      else {
        subExpr = IndexExpr();
      }
    }

    void visit(const ImmExprNode* op) {
      subExpr = IndexExpr();
    }

  };
  return SubExprVisitor(vars).getSubExpression(expr);
}

class SubExprVisitor : public ExprVisitorStrict {
public:
  SubExprVisitor(const vector<IndexVar>& vars) {
    this->vars.insert(vars.begin(), vars.end());
  }

  IndexExpr getSubExpression(const IndexExpr& expr) {
    visit(expr);
    IndexExpr e = subExpr;
    subExpr = IndexExpr();
    return e;
  }

private:
  set<IndexVar> vars;
  IndexExpr     subExpr;

  using ExprVisitorStrict::visit;

  void visit(const AccessNode* op) {
    // If any variable is in the set of index variables, then the expression
    // has not been emitted at a previous level, so we keep it.
    for (auto& indexVar : op->indexVars) {
      if (util::contains(vars, indexVar)) {
        subExpr = op;
        return;
      }
    }
    subExpr = IndexExpr();
  }

  template <class T>
  IndexExpr unarySubExpr(const T* op) {
    IndexExpr a = getSubExpression(op->a);
    return a.defined() ? op : IndexExpr();
  }

  void visit(const NegNode* op) {
    subExpr = unarySubExpr(op);
  }

  void visit(const SqrtNode* op) {
    subExpr = unarySubExpr(op);
  }

  template <class T>
  IndexExpr binarySubExpr(const T* op) {
    IndexExpr a = getSubExpression(op->a);
    IndexExpr b = getSubExpression(op->b);
    if (a.defined() && b.defined()) {
      return new T(a, b);
    }
    else if (a.defined()) {
      return a;
    }
    else if (b.defined()) {
      return b;
    }

    return IndexExpr();
  }

  void visit(const AddNode* op) {
    subExpr = binarySubExpr(op);
  }

  void visit(const SubNode* op) {
    subExpr = binarySubExpr(op);
  }

  void visit(const MulNode* op) {
    subExpr = binarySubExpr(op);
  }

  void visit(const DivNode* op) {
    subExpr = binarySubExpr(op);
  }

  void visit(const IntImmNode* op) {
    subExpr = IndexExpr();
  }

  void visit(const FloatImmNode* op) {
    subExpr = IndexExpr();
  }

  void visit(const ComplexImmNode* op) {
    subExpr = IndexExpr();
  }

  void visit(const UIntImmNode* op) {
    subExpr = IndexExpr();
  }
};

IndexExpr getSubExpr(IndexExpr expr, const vector<IndexVar>& vars) {
  return SubExprVisitor(vars).getSubExpression(expr);
}

}}
