#include "NvmFunctionInfo.h"

using namespace llvm;
using namespace std;
using namespace klee;

/* Begin NvmFunctionCallDesc */

NvmFunctionCallDesc::NvmFunctionCallDesc() : fn_(nullptr), nvm_args_() { }

NvmFunctionCallDesc::NvmFunctionCallDesc(const Function *fn) : fn_(fn), nvm_args_() { }

NvmFunctionCallDesc::NvmFunctionCallDesc(const Function *fn, const
    unordered_set<unsigned> &nvm_args) : fn_(fn), nvm_args_(nvm_args) { }

size_t NvmFunctionCallDesc::HashFn::operator()(const NvmFunctionCallDesc& x) const
{
  size_t hash_val = hash<const Function*>{}(x.Fn());
  for (unsigned i : x.NvmArgs()) {
    hash_val ^= i;
  }

  return hash_val;
}

void NvmFunctionCallDesc::dumpInfo() const {
  if (fn_) {
    errs() << "Function " << fn_ << "->" << fn_->getName() << " with arguments <";
  } else {
    errs() << "Function (nullptr) with arguments <";
  }
  for (unsigned i : nvm_args_) {
    errs() << i << ", ";
  }
  errs() << ">\n";
}

/* End NvmFunctionCallDesc */

/* Begin NvmFunctionCallInfo */

NvmFunctionCallInfo::NvmFunctionCallInfo(
        NvmFunctionInfo *parent,
        const NvmFunctionCallDesc &desc,
        const unordered_set<const Function*> &blacklist) :
    parent_(parent), fn_(desc.Fn()), nvm_args_(desc.NvmArgs()), blacklist_(blacklist)
{
  blacklist_.insert(fn_);
  init();
}

NvmFunctionCallInfo::NvmFunctionCallInfo(
        NvmFunctionInfo *parent,
        const NvmFunctionCallDesc &desc) :
    parent_(parent), fn_(desc.Fn()), nvm_args_(desc.NvmArgs()), blacklist_({desc.Fn()})
{
  init();
}

void NvmFunctionCallInfo::init() {
  getNvmInfo();
  computeFactors();
}

void NvmFunctionCallInfo::getNvmInfo() {
  nvm_ptr_locs_ = utils::getNvmPtrLocs(*fn_);

  for (const Value *loc : nvm_ptr_locs_) {
    auto ptrs = utils::getPtrsFromLoc(loc);
    nvm_ptrs_.insert(ptrs.begin(), ptrs.end());
  }

  // (iangneal): LLVM 10 supports a getArg function, but LLVM 8 does not.
  for (unsigned a : nvm_args_) {
    const Argument *arg = fn_->arg_begin() + a;
    nvm_ptrs_.insert(arg);
  }

  // Now that we have all the pointers, we can find all the derivatives.
  utils::getDerivativePtrs(nvm_ptrs_);

  // Now we find all the instructions which modify NVM.
  for (const Value *ptr : nvm_ptrs_) {
    utils::getModifiers(ptr, nvm_mods_);
  }

  for (const BasicBlock &bb : *fn_) {
    for (const Instruction &i : bb) {
      // Get all the fences and treat them like NVM modifying instructions.
      if (utils::isFence(i)) nvm_mods_.insert(&i);
      // If this is a nested function call, remember it for later when we have
      // to compute the recursive factors.
      const CallInst *ci = utils::getNestedFunctionCallInst(&i);
      if (ci) nested_calls_.insert(ci);
    }
  }
}

void NvmFunctionCallInfo::computeSuccessorFactor(
    const BasicBlock *bb, const unordered_set<const BasicBlock*> &be) {

  if (succ_factor_.find(bb) != succ_factor_.end()) return;

  size_t max_imp = 0;

  Function *mfn = const_cast<Function*>(fn_);
  DominatorTree dom(*mfn);
  PostDominatorTree pdom(*mfn);

  for (const BasicBlock *succ : successors(bb)) {
      bool is_succ_loop_body = false;
      for (const BasicBlock *lbb : be) {
          is_succ_loop_body |= pdom.dominates(lbb, succ);
      }
      if (is_succ_loop_body) continue;

      unordered_set<const BasicBlock*> beSucc(be.begin(), be.end());
      if (dom.dominates(succ, bb)) {
          beSucc.insert(bb);
      }

      computeSuccessorFactor(succ, beSucc);
      max_imp = max_imp > succ_factor_[succ] ? max_imp : succ_factor_[succ];
  }

  succ_factor_[bb] = imp_nested_[bb] + max_imp;
}

void NvmFunctionCallInfo::computeFactors() {
  // Importance factor.
  // -- Since we already have all the instructions, we can just get their
  // parent basic blocks instead of iterating over all of the blocks.
  for (const Value *v : nvm_mods_) {
    const Instruction *i = dyn_cast<Instruction>(v);
    if (!i) continue;

    imp_factor_[i->getParent()]++;
  }

  // Nested Factor.
  // -- Initial value is that of the importance factor.
  imp_nested_.insert(imp_factor_.begin(), imp_factor_.end());
  // -- Add up the nested calls.
  for (const CallInst *ci : nested_calls_) {
    // Create the description of this nested call.
    const Function *cfn = ci->getCalledFunction();
    unordered_set<unsigned> args;
    for (unsigned i = 0; ci->arg_begin() + i != ci->arg_end(); ++i) {
      const Use *use = ci->arg_begin() + i;
      // If this argument is an NVM pointer, add the argno to the list.
      if (nvm_ptrs_.find(use->get()) != nvm_ptrs_.end()) args.insert(i);
    }

    NvmFunctionCallDesc nested_desc(cfn, args);
    const NvmFunctionCallInfo *nci = parent_->get(nested_desc, blacklist_);
    if (nci) {
      // If we were able to resolve the nested call, increment the block that
      // contains this instruction.
      const BasicBlock *bb = ci->getParent();
      imp_nested_[bb] += nci->getMagnitude();
    }
  }

  // Successor factor.
  // -- Now, we just start at the beginning and recurse our way down.
  const BasicBlock &entry = fn_->getEntryBlock();
  unordered_set<const llvm::BasicBlock*> empty_backedges;
  computeSuccessorFactor(&entry, empty_backedges);

  // Magnitude.
  // -- This is simply the successor factor at the entry block!
  magnitude_ = succ_factor_[&entry];
}

void NvmFunctionCallInfo::dumpInfo() const {
  errs() << "Function " << fn_->getName() << " with arguments <";
  for (unsigned i : nvm_args_) {
    errs() << i << ", ";
  }
  errs() << "> as NVM pointers and " << nvm_ptrs_.size()
    << " overall NVM pointer" << (nvm_ptrs_.size() > 1 ? "s" : "") <<
    "has a magnitude of " << magnitude_ << "\n";

  for (const auto &p : imp_factor_) {
    errs() << "\t (IMP FACTOR) BB " << p.first << " => " << p.second << "\n";
  }
  for (const auto &p : imp_nested_) {
    errs() << "\t (IMP NESTED) BB " << p.first << " => " << p.second << "\n";
  }
  for (const auto &p : succ_factor_) {
    errs() << "\t (SUC FACTOR) BB " << p.first << " => " << p.second << "\n";
  }
}

size_t NvmFunctionCallInfo::getSuccessorFactor(const BasicBlock *bb) const {
  return succ_factor_.at(bb);
}

size_t NvmFunctionCallInfo::getImportanceFactor(const llvm::BasicBlock *bb) const {
  return imp_nested_.at(bb);
}

unordered_set<unsigned> NvmFunctionCallInfo::queryNvmArgs(const CallInst *ci) const {
  // Create the description of this nested call.
  unordered_set<unsigned> args;
  for (unsigned i = 0; ci->arg_begin() + i != ci->arg_end(); ++i) {
    const Use *use = ci->arg_begin() + i;
    // If this argument is an NVM pointer, add the argno to the list.
    if (nvm_ptrs_.find(use->get()) != nvm_ptrs_.end()) args.insert(i);
  }

  return args;
}


/* End NvmFunctionCallInfo */

/* Begin NvmFunctionInfo */
//NvmFunctionInfo::NvmFunctionInfo(ModulePass *mp): mp_(mp) {};

const NvmFunctionCallInfo* NvmFunctionInfo::findInfo(const NvmFunctionCallDesc& d) {
  return fn_info_.at(d).get();
}

const NvmFunctionCallInfo* NvmFunctionInfo::get(const NvmFunctionCallDesc &d) {
  unordered_set<const Function*> bl;
  return get(d, bl);
}

const NvmFunctionCallInfo* NvmFunctionInfo::get(const NvmFunctionCallDesc &d,
    const unordered_set<const Function*> &bl) {

  if (fn_info_.find(d) != fn_info_.end()) return fn_info_[d].get();

  // If we can't construct the new instance, abort. Likely recursion.
  if (fn_info_.find(d) == fn_info_.end()
      && bl.find(d.Fn()) != bl.end()) return nullptr;

  return (fn_info_[d] = make_shared<NvmFunctionCallInfo>(this, d, bl)).get();
}

void NvmFunctionInfo::dumpAllInfo() const {
  for (const auto &t : fn_info_) {
    t.second->dumpInfo();
  }
}

unordered_set<unsigned> NvmFunctionInfo::getNvmArgs(const NvmFunctionCallDesc& caller,
    const CallInst* call) const {
  std::shared_ptr<NvmFunctionCallInfo> info = fn_info_.at(caller);
  return info->queryNvmArgs(call);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
