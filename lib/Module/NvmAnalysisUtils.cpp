#include "NvmAnalysisUtils.h"

using namespace klee;
using namespace llvm;
using namespace std;

bool utils::checkInlineAsmEq(const Instruction *i...) {
    va_list args;
    va_start(args, i);

    const InlineAsm *ia = nullptr;

    if (i->getOpcode() == Instruction::Call) {
        const CallInst *ci = static_cast<const CallInst*>(i);
        if (ci->isInlineAsm()) {
            ia = static_cast<InlineAsm*>(ci->getCalledValue());
        }
    }

    char *asmStr;
    while(nullptr != ia &&
          nullptr != (asmStr = va_arg(args, char*))) {
        if (ia->getAsmString() == asmStr) {
            va_end(args);
            return true;
        }
    }

    va_end(args);
    return false;
}

bool utils::checkInstrinicInst(const Instruction *i...) {
    va_list args;
    va_start(args, i);

    const Function *fn = nullptr;

    if (i->getOpcode() == Instruction::Call) {
        const CallInst *ci = static_cast<const CallInst*>(i);
        if (ci->getIntrinsicID() != Intrinsic::not_intrinsic) {
            const IntrinsicInst *ii = static_cast<const IntrinsicInst*>(i);
            fn = ii->getCalledFunction();
        }
    }

    char *fnName;
    while(nullptr != fn &&
          nullptr != (fnName = va_arg(args, char*))) {
        if (fn->getName().contains(fnName)) {
            va_end(args);
            return true;
        }
    }

    va_end(args);
    return false;
}

bool utils::isFlush(const Instruction &i) {
    return checkInstrinicInst(&i,
                              "clflush",
                              "clwb",
                              nullptr) ||
           utils::checkInlineAsmEq(&i,
                   ".byte 0x66; clflush $0",
                   ".byte 0x66; xsaveopt $0", nullptr);
}

bool utils::isFence(const Instruction &i) {
    return checkInstrinicInst(&i, "sfence", nullptr);
}

Value* utils::getPtrLoc(Value *v) {
    if (isa<User>(v)) {
        User *u = dyn_cast<User>(v);
        for (auto &op : u->operands()) {
            if (isa<AllocaInst>(op)) {
                return op;
            }
        }
    }

    return v;
}

unordered_set<const Value*> utils::getPtrsFromStoredLocs(const Value *ptr) {
    unordered_set<const Value*> ptrs;

    for (const auto *u : ptr->users()) {
        const StoreInst *si = dyn_cast<StoreInst>(u);
        if (nullptr != si
            && ptr == si->getValueOperand()) {
            //errs() << "\t\t(store): " << *si << "\n";
            const Value *store_loc = si->getPointerOperand();

            for (const auto *su : store_loc->users()) {
                //errs() << "\t\t\t(store user): " << *su << "\n";
                const LoadInst *li = dyn_cast<LoadInst>(su);
                if (nullptr != li
                    && store_loc == li->getPointerOperand()) {
                    //errs() << "\t\t\t(load): " << *li << "\n";

                    ptrs.insert(li);
                }
            }
        }
    }

    return ptrs;
}

void utils::getDerivativePtrs(unordered_set<const Value*> &s)
{
    if (s.empty()) return;
    unordered_set<const Value*> der;

    for (auto *v : s) {
        // We're not interested with dereferenced values from NVM.
        if (!v->getType()->isPointerTy()) continue;

        for (auto *u : v->users()) {
            if (u->getType()->isPointerTy()) {
                der.insert(u);
            }
        }

        auto fromStores = getPtrsFromStoredLocs(v);
        der.insert(fromStores.begin(), fromStores.end());
    }

    getDerivativePtrs(der);

    s.insert(der.begin(), der.end());
}

void utils::getModifiers(const Value* ptr, unordered_set<const Value*> &s) {
    for (auto *u : ptr->users()) {
        if (const StoreInst *si = dyn_cast<StoreInst>(u)) {
            if (si->getPointerOperand() == ptr) {
                s.insert(u);
            }
        }

        if (isFlush(*dyn_cast<Instruction>(u))) {
            s.insert(u);
        }
    }
}

const CallInst* utils::getNestedFunctionCallInst(const Instruction* i) {
  const CallInst *ci = dyn_cast<CallInst>(i);
  if (ci && !ci->isInlineAsm()) {
    const Function *cfn = ci->getCalledFunction();
    if (cfn && !cfn->isIntrinsic()) {
      return ci;
    }
  }

  return nullptr;
}

list<const Function*> utils::getNestedFunctionCalls(const BasicBlock *bb) {
    list<const Function*> fns;

    for (const Instruction &i : *bb) {
      const CallInst *ci = utils::getNestedFunctionCallInst(&i);
      if (ci) fns.push_back(ci->getCalledFunction());
    }

    return fns;
}

Value* utils::getNvmPtrLocFromAnno(const Instruction &i) {
  if (!utils::checkInstrinicInst(&i, "annotation", nullptr)) {
      return nullptr;
  }

  Value* annotation = i.getOperand(1);
  Value *ann = annotation->stripPointerCasts();
  auto *ex = ValueAsMetadata::getConstant(ann);

  auto *g = dyn_cast<GlobalVariable>(ex->getValue());
  if (g) {
      auto *r = dyn_cast<ConstantDataSequential>(g->getInitializer());
      if (r) {
          auto name = r->getAsCString();
          if ("nvmptr" == name) {
              return utils::getPtrLoc(i.getOperand(0));
          }
      }
  }

  return nullptr;
}

unordered_set<const Value*> utils::getNvmPtrLocs(const Function &f) {
  unordered_set<const Value*> s;
  for (auto &b : f) {
    for (auto &i : b) {
      Value *v = utils::getNvmPtrLocFromAnno(i);
      if (v) s.insert(v);
    }
  }
  return s;
}

unordered_set<const Value*> utils::getPtrsFromLoc(const Value *ptr_loc) {
  unordered_set<const Value*> s;

  for (auto *u : ptr_loc->users()) {
    // if is assignment
    if (isa<LoadInst>(u)) {
      s.insert(u);
    }
  }

  return s;
}
/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
