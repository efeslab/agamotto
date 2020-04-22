//===-- Constraints.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CONSTRAINTS_H
#define KLEE_CONSTRAINTS_H

#include "klee/Expr/Expr.h"
#include "klee/Internal/Support/IndependentElementSet.h"
#include <unordered_set>

namespace klee {

class ExprVisitor;
// Constraints_ty is an abstract type representing a collection of constraints.
// (ref<Expr> or Expr or etc.)
// Currently it is a vector and other data structures are used to guarantee
// uniqueness.
// TODO: In the future I hope it can be changed to std::unordered_set
typedef std::vector<ref<Expr>> Constraints_ty;
class ConstraintManager {
public:
  using constraints_ty = std::vector<ref<Expr>>;
  using iterator = constraints_ty::iterator;
  using const_iterator = constraints_ty::const_iterator;

  ConstraintManager() = default;
  //ConstraintManager &operator=(const ConstraintManager &cs) = default;
  //ConstraintManager(ConstraintManager &&cs) = default;
  //ConstraintManager &operator=(ConstraintManager &&cs) = default;

  // create from constraints with no optimization
  explicit
  ConstraintManager(const std::vector< ref<Expr> > &_constraints);
  ConstraintManager(const ConstraintManager &cs);

  // Destructor
  ~ConstraintManager();

  typedef Constraints_ty::const_iterator constraint_iterator;
  typedef std::unordered_set<klee::IndependentElementSet*>::const_iterator factor_iterator;

  // given a constraint which is known to be valid, attempt to
  // simplify the existing constraint set
  void simplifyForValidConstraint(ref<Expr> e);

  ref<Expr> simplifyExpr(ref<Expr> e) const;

  // \param[out] if constraint added successfully (if it is valid)
  bool addConstraint(ref<Expr> e);

  bool empty() const noexcept { return constraints.empty(); }
  ref<Expr> back() const { return constraints.back(); }
  const_iterator begin() const { return constraints.cbegin(); }
  const_iterator end() const { return constraints.cend(); }
  std::size_t size() const noexcept { return constraints.size(); }
  factor_iterator factor_begin() const { return factors.begin(); }
  factor_iterator factor_end() const { return factors.end(); }

  bool operator==(const ConstraintManager &other) const {
    return constraints == other.constraints;
  }

  bool operator!=(const ConstraintManager &other) const {
    return constraints != other.constraints;
  }

  const Constraints_ty& getAllConstraints() const { return constraints; }

private:
  Constraints_ty constraints;
  std::vector<ref<Expr>> old;
  ExprHashMap<klee::IndependentElementSet*> representative;
  std::vector<ref<Expr>> deleteConstraints;
  std::vector<ref<Expr>> addedConstraints;
  std::unordered_set<klee::IndependentElementSet*> factors;
  // equalities consists of EqExpr in current constraints.
  // For each item <key,value> in this map, ExprReplaceVisitor2 can find 
  // occurrences of "key" in an expression and replace it with "value"
  std::map<ref<Expr>, ref<Expr>> equalities;

  // returns true iff the constraints were modified
  bool rewriteConstraints(ExprVisitor &visitor);

  bool addConstraintInternal(ref<Expr> e);

  void updateIndependentSet();
  // maintain the equalities used to simplify(replace) expression
  void updateEqualities();

  void checkConstraintChange();

  void updateDelete();
};

} // namespace klee

#endif /* KLEE_CONSTRAINTS_H */

