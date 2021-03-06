#include "taco/expr/expr.h"

#include "error/error_checks.h"
#include "error/error_messages.h"
#include "taco/type.h"
#include "taco/format.h"
#include "taco/expr/schedule.h"
#include "taco/expr/expr_nodes.h"
#include "taco/expr/expr_rewriter.h"
#include "taco/util/name_generator.h"

using namespace std;

namespace taco {

// class ExprNode
ExprNode::ExprNode() : operatorSplits(new vector<OperatorSplit>) {
  }

void ExprNode::splitOperator(IndexVar old, IndexVar left, IndexVar right) {
  operatorSplits->push_back(OperatorSplit(this, old, left, right));
}
  
ExprNode::ExprNode(DataType type) : operatorSplits(new vector<OperatorSplit>), dataType(type) {
}

DataType ExprNode::getDataType() const {
  return dataType;
}

const std::vector<OperatorSplit>& ExprNode::getOperatorSplits() const {
  return *operatorSplits;
}


// class IndexExpr
IndexExpr::IndexExpr(long long val) : IndexExpr(new IntImmNode(val)) {
}

IndexExpr::IndexExpr(std::complex<double> val) : IndexExpr(new ComplexImmNode(val)) {
}

IndexExpr::IndexExpr(unsigned long long val) : IndexExpr(new UIntImmNode(val)) {
}

IndexExpr::IndexExpr(double val) : IndexExpr(new FloatImmNode(val)) {
}

IndexExpr IndexExpr::operator-() {
  return new NegNode(this->ptr);
}

void IndexExpr::splitOperator(IndexVar old, IndexVar left, IndexVar right) {
  const_cast<ExprNode*>(this->ptr)->splitOperator(old, left, right);
}
  
DataType IndexExpr::getDataType() const {
  return const_cast<ExprNode*>(this->ptr)->getDataType();
}

void IndexExpr::accept(ExprVisitorStrict *v) const {
  ptr->accept(v);
}

std::ostream& operator<<(std::ostream& os, const IndexExpr& expr) {
  if (!expr.defined()) return os << "Expr()";
  expr.ptr->print(os);
  return os;
}

struct Equals : public ExprVisitorStrict {
  bool eq = false;
  IndexExpr b;

  bool check(IndexExpr a, IndexExpr b) {
    this->b = b;
    a.accept(this);
    return eq;
  }

  using ExprVisitorStrict::visit;

  void visit(const AccessNode* anode) {
    if (!isa<AccessNode>(b)) {
      eq = false;
      return;
    }

    auto bnode = to<AccessNode>(b);
    if (anode->tensorVar != bnode->tensorVar) {
      eq = false;
      return;
    }
    if (anode->indexVars.size() != anode->indexVars.size()) {
      eq = false;
      return;
    }
    for (size_t i = 0; i < anode->indexVars.size(); i++) {
      if (anode->indexVars[i] != bnode->indexVars[i]) {
        eq = false;
        return;
      }
    }

    eq = true;
  }

  template <class T>
  bool unaryEquals(const T* anode, IndexExpr b) {
    if (!isa<T>(b)) {
      return false;
    }
    auto bnode = to<T>(b);
    if (!equals(anode->a, bnode->a)) {
      return false;
    }
    return true;
  }

  void visit(const NegNode* anode) {
    eq = unaryEquals(anode, b);
  }

  void visit(const SqrtNode* anode) {
    eq = unaryEquals(anode, b);
  }

  template <class T>
  bool binaryEquals(const T* anode, IndexExpr b) {
    if (!isa<T>(b)) {
      return false;
    }
    auto bnode = to<T>(b);
    if (!equals(anode->a, bnode->a) || !equals(anode->b, bnode->b)) {
      return false;
    }
    return true;
  }

  void visit(const AddNode* anode) {
    eq = binaryEquals(anode, b);
  }

  void visit(const SubNode* anode) {
    eq = binaryEquals(anode, b);
  }

  void visit(const MulNode* anode) {
    eq = binaryEquals(anode, b);
  }

  void visit(const DivNode* anode) {
    eq = binaryEquals(anode, b);
  }

  template <class T>
  bool immediateEquals(const T* anode, IndexExpr b) {
    if (!isa<T>(b)) {
      return false;
    }
    auto bnode = to<T>(b);
    if (anode->val != bnode->val) {
      return false;
    }
    return true;
  }

  void visit(const IntImmNode* anode) {
    eq = immediateEquals(anode, b);

  }

  void visit(const FloatImmNode* anode) {
    eq = immediateEquals(anode, b);
  }

  void visit(const ComplexImmNode* anode) {
    eq = immediateEquals(anode, b);
  }

  void visit(const UIntImmNode* anode) {
    eq = immediateEquals(anode, b);
  }
};

bool equals(IndexExpr a, IndexExpr b) {
  if (!a.defined() && !b.defined()) {
    return true;
  }
  if ((a.defined() && !b.defined()) || (!a.defined() && b.defined())) {
    return false;
  }
  return Equals().check(a,b);
}


// class Access
Access::Access(const Node* n) : IndexExpr(n) {
}

Access::Access(const TensorVar& tensor, const std::vector<IndexVar>& indices)
    : Access(new Node(tensor, indices)) {
}

const Access::Node* Access::getPtr() const {
  return static_cast<const Node*>(ptr);
}

const TensorVar& Access::getTensorVar() const {
  return getPtr()->tensorVar;
}

const std::vector<IndexVar>& Access::getIndexVars() const {
  return getPtr()->indexVars;
}

void Access::operator=(const IndexExpr& expr) {
  TensorVar result = getTensorVar();
  taco_uassert(!result.getIndexExpr().defined()) << "Cannot reassign " <<result;
  result.setIndexExpression(getIndexVars(), expr);
}

void Access::operator=(const Access& expr) {
  operator=(static_cast<IndexExpr>(expr));
}

void Access::operator+=(const IndexExpr& expr) {
  TensorVar result = getTensorVar();
  taco_uassert(!result.getIndexExpr().defined()) << "Cannot reassign " <<result;
  // TODO: check that result format is dense. For now only support accumulation
  /// into dense. If it's not dense, then we can insert an operator split.
  result.setIndexExpression(getIndexVars(), expr, true);
}

void Access::operator+=(const Access& expr) {
  operator+=(static_cast<IndexExpr>(expr));
}

// Operators
IndexExpr operator+(const IndexExpr& lhs, const IndexExpr& rhs) {
  return new AddNode(lhs, rhs);
}

IndexExpr operator-(const IndexExpr& lhs, const IndexExpr& rhs) {
  return new SubNode(lhs, rhs);
}

IndexExpr operator*(const IndexExpr& lhs, const IndexExpr& rhs) {
  return new MulNode(lhs, rhs);
}

IndexExpr operator/(const IndexExpr& lhs, const IndexExpr& rhs) {
  return new DivNode(lhs, rhs);
}


// class IndexVar
struct IndexVar::Content {
  string name;
};

IndexVar::IndexVar() : IndexVar(util::uniqueName('i')) {}

IndexVar::IndexVar(const std::string& name) : content(new Content) {
  content->name = name;
}

std::string IndexVar::getName() const {
  return content->name;
}

bool operator==(const IndexVar& a, const IndexVar& b) {
  return a.content == b.content;
}

bool operator<(const IndexVar& a, const IndexVar& b) {
  return a.content < b.content;
}

std::ostream& operator<<(std::ostream& os, const IndexVar& var) {
  return os << var.getName();
}


// class TensorVar
struct TensorVar::Content {
  string name;
  Type type;
  Format format;

  vector<IndexVar> freeVars;
  IndexExpr indexExpr;
  bool accumulate;

  Schedule schedule;
};

TensorVar::TensorVar() : TensorVar(Type()) {
}

TensorVar::TensorVar(const Type& type) : TensorVar(type, Dense) {
}

TensorVar::TensorVar(const std::string& name, const Type& type)
    : TensorVar(name, type, Dense) {
}

TensorVar::TensorVar(const Type& type, const Format& format)
    : TensorVar(util::uniqueName('A'), type, format) {
}

TensorVar::TensorVar(const string& name, const Type& type, const Format& format)
    : content(new Content) {
  content->name = name;
  content->type = type;
  content->format = format;
}

std::string TensorVar::getName() const {
  return content->name;
}

int TensorVar::getOrder() const {
  return content->type.getShape().getOrder();
}

const Type& TensorVar::getType() const {
  return content->type;
}

const Format& TensorVar::getFormat() const {
  return content->format;
}

const std::vector<IndexVar>& TensorVar::getFreeVars() const {
  return content->freeVars;
}

const IndexExpr& TensorVar::getIndexExpr() const {
  return content->indexExpr;
}

bool TensorVar::isAccumulating() const {
  return content->accumulate;
}

const Schedule& TensorVar::getSchedule() const {
  struct GetSchedule : public ExprVisitor {
    using ExprVisitor::visit;
    Schedule schedule;
    void visit(const BinaryExprNode* expr) {
      for (auto& operatorSplit : expr->getOperatorSplits()) {
        schedule.addOperatorSplit(operatorSplit);
      }
    }
  };
  GetSchedule getSchedule;
  content->schedule.clearOperatorSplits();
  getSchedule.schedule = content->schedule;
  getIndexExpr().accept(&getSchedule);
  return content->schedule;
}

void TensorVar::setName(std::string name) {
  content->name = name;
}

void TensorVar::setIndexExpression(vector<IndexVar> freeVars,
                                   IndexExpr indexExpr, bool accumulate) {
  auto shape = getType().getShape();
  taco_uassert(error::dimensionsTypecheck(freeVars, indexExpr, shape))
      << error::expr_dimension_mismatch << " "
      << error::dimensionTypecheckErrors(freeVars, indexExpr, shape);

  // The following are index expressions the implementation doesn't currently
  // support, but that are planned for the future.
  taco_uassert(!error::containsTranspose(this->getFormat(), freeVars, indexExpr))
      << error::expr_transposition;
  taco_uassert(!error::containsDistribution(freeVars, indexExpr))
      << error::expr_distribution;

  content->freeVars = freeVars;
  content->indexExpr = indexExpr;
  content->accumulate = accumulate;
}

const Access TensorVar::operator()(const std::vector<IndexVar>& indices) const {
  taco_uassert((int)indices.size() == getOrder()) <<
      "A tensor of order " << getOrder() << " must be indexed with " <<
      getOrder() << " variables, but is indexed with:  " << util::join(indices);
  return Access(new AccessNode(*this, indices));
}

Access TensorVar::operator()(const std::vector<IndexVar>& indices) {
  taco_uassert((int)indices.size() == getOrder()) <<
      "A tensor of order " << getOrder() << " must be indexed with " <<
      getOrder() << " variables, but is indexed with:  " << util::join(indices);
  return Access(new AccessNode(*this, indices));
}

bool operator==(const TensorVar& a, const TensorVar& b) {
  return a.content == b.content;
}

bool operator<(const TensorVar& a, const TensorVar& b) {
  return a.content < b.content;
}

std::ostream& operator<<(std::ostream& os, const TensorVar& var) {
  return os << var.getName() << " : " << var.getType();
}


set<IndexVar> getIndexVars(const TensorVar& tensor) {
  set<IndexVar> indexVars(tensor.getFreeVars().begin(), tensor.getFreeVars().end());
  match(tensor.getIndexExpr(),
    function<void(const AccessNode*)>([&indexVars](const AccessNode* op) {
      indexVars.insert(op->indexVars.begin(), op->indexVars.end());
    })
  );
  return indexVars;
}

map<IndexVar,Dimension> getIndexVarRanges(const TensorVar& tensor) {
  map<IndexVar, Dimension> indexVarRanges;

  auto& freeVars = tensor.getFreeVars();
  auto& type = tensor.getType();
  for (size_t i = 0; i < freeVars.size(); i++) {
    indexVarRanges.insert({freeVars[i], type.getShape().getDimension(i)});
  }

  match(tensor.getIndexExpr(),
    function<void(const AccessNode*)>([&indexVarRanges](const AccessNode* op) {
      auto& tensor = op->tensorVar;
      auto& vars = op->indexVars;
      auto& type = tensor.getType();
      for (size_t i = 0; i < vars.size(); i++) {
        indexVarRanges.insert({vars[i], type.getShape().getDimension(i)});
      }
    })
  );
  
  return indexVarRanges;
}


// functions
struct Simplify : public ExprRewriterStrict {
public:
  Simplify(const set<Access>& exhausted) : exhausted(exhausted) {}

private:
  using ExprRewriterStrict::visit;

  set<Access> exhausted;
  void visit(const AccessNode* op) {
    if (util::contains(exhausted, op)) {
      expr = IndexExpr();
    }
    else {
      expr = op;
    }
  }

  template <class T>
  IndexExpr visitUnaryOp(const T *op) {
    IndexExpr a = rewrite(op->a);
    if (!a.defined()) {
      return IndexExpr();
    }
    else if (a == op->a) {
      return op;
    }
    else {
      return new T(a);
    }
  }

  void visit(const NegNode* op) {
    expr = visitUnaryOp(op);
  }

  void visit(const SqrtNode* op) {
    expr = visitUnaryOp(op);
  }

  template <class T>
  IndexExpr visitDisjunctionOp(const T *op) {
    IndexExpr a = rewrite(op->a);
    IndexExpr b = rewrite(op->b);
    if (!a.defined() && !b.defined()) {
      return IndexExpr();
    }
    else if (!a.defined()) {
      return b;
    }
    else if (!b.defined()) {
      return a;
    }
    else if (a == op->a && b == op->b) {
      return op;
    }
    else {
      return new T(a, b);
    }
  }

  template <class T>
  IndexExpr visitConjunctionOp(const T *op) {
    IndexExpr a = rewrite(op->a);
    IndexExpr b = rewrite(op->b);
    if (!a.defined() || !b.defined()) {
      return IndexExpr();
    }
    else if (a == op->a && b == op->b) {
      return op;
    }
    else {
      return new T(a, b);
    }
  }

  void visit(const AddNode* op) {
    expr = visitDisjunctionOp(op);
  }

  void visit(const SubNode* op) {
    expr = visitDisjunctionOp(op);
  }

  void visit(const MulNode* op) {
    expr = visitConjunctionOp(op);
  }

  void visit(const DivNode* op) {
    expr = visitConjunctionOp(op);
  }

  void visit(const IntImmNode* op) {
    expr = op;
  }

  void visit(const FloatImmNode* op) {
    expr = op;
  }

  void visit(const UIntImmNode* op) {
    expr = op;
  }

  void visit(const ComplexImmNode* op) {
    expr = op;
  }
};

IndexExpr simplify(const IndexExpr& expr, const set<Access>& exhausted) {
  return Simplify(exhausted).rewrite(expr);
}


}
