//===-- KModule.cpp -------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "KModule"

#include "Passes.h"

#include "klee/Config/Version.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/Debug.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Interpreter.h"
#include "klee/OptionCategories.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(4, 0)
#include "llvm/Bitcode/BitcodeWriter.h"
#else
#include "llvm/Bitcode/ReaderWriter.h"
#endif
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/Scalar.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
#include "llvm/Transforms/Scalar/Scalarizer.h"
#endif
#include "llvm/Transforms/Utils/Cloning.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(7, 0)
#include "llvm/Transforms/Utils.h"
#endif

#include <sstream>

using namespace llvm;
using namespace klee;

namespace klee {
cl::OptionCategory
    ModuleCat("Module-related options",
              "These options affect the compile-time processing of the code.");
}

namespace {
  enum SwitchImplType {
    eSwitchTypeSimple,
    eSwitchTypeLLVM,
    eSwitchTypeInternal
  };

  cl::opt<bool>
  OutputSource("output-source",
               cl::desc("Write the assembly for the final transformed source (default=true)"),
               cl::init(true),
	       cl::cat(ModuleCat));

  cl::opt<bool>
  OutputModule("output-module",
               cl::desc("Write the bitcode for the final transformed module"),
               cl::init(false),
	       cl::cat(ModuleCat));

  cl::opt<SwitchImplType>
  SwitchType("switch-type", cl::desc("Select the implementation of switch (default=internal)"),
             cl::values(clEnumValN(eSwitchTypeSimple, "simple", 
                                   "lower to ordered branches"),
                        clEnumValN(eSwitchTypeLLVM, "llvm", 
                                   "lower using LLVM"),
                        clEnumValN(eSwitchTypeInternal, "internal", 
                                   "execute switch internally")
                        KLEE_LLVM_CL_VAL_END),
             cl::init(eSwitchTypeInternal),
	     cl::cat(ModuleCat));
  
  cl::opt<bool>
  DebugPrintEscapingFunctions("debug-print-escaping-functions", 
                              cl::desc("Print functions whose address is taken (default=false)"),
			      cl::cat(ModuleCat));

  // Don't run VerifierPass when checking module
  cl::opt<bool>
  DontVerify("disable-verify",
             cl::desc("Do not verify the module integrity (default=false)"),
             cl::init(false), cl::cat(klee::ModuleCat));

  cl::opt<bool>
  OptimiseKLEECall("klee-call-optimisation",
                             cl::desc("Allow optimization of functions that "
                                      "contain KLEE calls (default=true)"),
                             cl::init(true), cl::cat(ModuleCat));
}

/***/

namespace llvm {
extern void Optimize(Module *, llvm::ArrayRef<const char *> preservedFunctions);
}

// what a hack
static Function *getStubFunctionForCtorList(Module *m,
                                            GlobalVariable *gv, 
                                            std::string name) {
  assert(!gv->isDeclaration() && !gv->hasInternalLinkage() &&
         "do not support old LLVM style constructor/destructor lists");

  std::vector<Type *> nullary;

  Function *fn = Function::Create(FunctionType::get(Type::getVoidTy(m->getContext()),
						    nullary, false),
				  GlobalVariable::InternalLinkage, 
				  name,
                              m);
  BasicBlock *bb = BasicBlock::Create(m->getContext(), "entry", fn);
  llvm::IRBuilder<> Builder(bb);

  // From lli:
  // Should be an array of '{ int, void ()* }' structs.  The first value is
  // the init priority, which we ignore.
  auto arr = dyn_cast<ConstantArray>(gv->getInitializer());
  if (arr) {
    for (unsigned i=0; i<arr->getNumOperands(); i++) {
      auto cs = cast<ConstantStruct>(arr->getOperand(i));
      // There is a third element in global_ctor elements (``i8 @data``).
#if LLVM_VERSION_CODE >= LLVM_VERSION(9, 0)
      assert(cs->getNumOperands() == 3 &&
             "unexpected element in ctor initializer list");
#else
      // before LLVM 9.0, the third operand was optional
      assert((cs->getNumOperands() == 2 || cs->getNumOperands() == 3) &&
             "unexpected element in ctor initializer list");
#endif
      auto fp = cs->getOperand(1);
      if (!fp->isNullValue()) {
        if (auto ce = dyn_cast<llvm::ConstantExpr>(fp))
          fp = ce->getOperand(0);

        if (auto f = dyn_cast<Function>(fp)) {
          Builder.CreateCall(f);
        } else {
          assert(0 && "unable to get function pointer from ctor initializer list");
        }
      }
    }
  }

  Builder.CreateRetVoid();

  return fn;
}

static void
injectStaticConstructorsAndDestructors(Module *m,
                                       llvm::StringRef entryFunction) {
  GlobalVariable *ctors = m->getNamedGlobal("llvm.global_ctors");
  GlobalVariable *dtors = m->getNamedGlobal("llvm.global_dtors");

  if (!ctors && !dtors)
    return;

  Function *mainFn = m->getFunction(entryFunction);
  if (!mainFn)
    klee_error("Entry function '%s' not found in module.",
               entryFunction.str().c_str());

  if (ctors) {
    llvm::IRBuilder<> Builder(&*mainFn->begin()->begin());
    Builder.CreateCall(getStubFunctionForCtorList(m, ctors, "klee.ctor_stub"));
  }

  if (dtors) {
    Function *dtorStub = getStubFunctionForCtorList(m, dtors, "klee.dtor_stub");
    for (Function::iterator it = mainFn->begin(), ie = mainFn->end(); it != ie;
         ++it) {
      if (isa<ReturnInst>(it->getTerminator())) {
        llvm::IRBuilder<> Builder(it->getTerminator());
        Builder.CreateCall(dtorStub);
      }
    }
  }
}

/**
 * UCLIBC's main (and libc mains in general) take an app_init and app_fini
 * argument to do all of the static construction/destruction. So, rather
 * than shoving the calls to the constructors and the beginning of main, 
 * we can just update the arguments to the __uclibc_main call to point to the 
 * ctor/dtor stubs.
 */
static void
fillUclibcMainInitAndFiniArgs(Module *m, llvm::StringRef entryFunction, 
                              llvm::StringRef libcMainFunction) {
  GlobalVariable *ctors = m->getNamedGlobal("llvm.global_ctors");
  GlobalVariable *dtors = m->getNamedGlobal("llvm.global_dtors");

  if (!ctors && !dtors)
    return;

  Function *entry = m->getFunction(entryFunction);
  Function *libcMain = m->getFunction(libcMainFunction);
  if (!entry) {
    klee_error("Entry function '%s' not found in module.",
               entryFunction.str().c_str());
  }
  if (!libcMain) {
    klee_error("Libc main function '%s' not found in module.",
               libcMainFunction.str().c_str());
  }

  // Now we find the call instruction associated with the libc main.
  CallInst *libcCall = nullptr;
  for (BasicBlock &bb : *entry) {
    for (Instruction &ii : bb) {
      if (nullptr != (libcCall = dyn_cast<CallInst>(&ii))
          && libcCall->getCalledFunction() == libcMain) break;
    }
  }

  if (!libcCall) {
    klee_error("Could not find call to %s in entry function %s.",
               libcMainFunction.str().c_str(),
               entryFunction.str().c_str());
  }

  if (ctors) {
    libcCall->setArgOperand(3, 
          getStubFunctionForCtorList(m, ctors, "klee.ctor_stub")); // app_init
  }

  if (dtors) {
    libcCall->setArgOperand(4, 
          getStubFunctionForCtorList(m, dtors, "klee.dtor_stub")); // app_fini
  }
}

void KModule::addInternalFunction(const char* functionName){
  Function* internalFunction = module->getFunction(functionName);
  if (!internalFunction) {
    KLEE_DEBUG(klee_warning(
        "Failed to add internal function %s. Not found.", functionName));
    return ;
  }
  KLEE_DEBUG(klee_message("Added function %s.",functionName));
  internalFunctions.insert(internalFunction);
}

bool KModule::link(std::vector<std::unique_ptr<llvm::Module>> &modules,
                   const std::string &entryPoint) {
  auto numRemainingModules = modules.size();
  // Add the currently active module to the list of linkables
  modules.push_back(std::move(module));
  std::string error;
  module = std::unique_ptr<llvm::Module>(
      klee::linkModules(modules, entryPoint, error));
  if (!module)
    klee_error("Could not link KLEE files %s", error.c_str());

  targetData = std::unique_ptr<llvm::DataLayout>(new DataLayout(module.get()));

  // Check if we linked anything
  return modules.size() != numRemainingModules;
}

void KModule::instrument(const Interpreter::ModuleOptions &opts) {
  // Inject checks prior to optimization... we also perform the
  // invariant transformations that we will end up doing later so that
  // optimize is seeing what is as close as possible to the final
  // module.
  legacy::PassManager pm;

  // XXX: (iangneal) We can eventually change the pass so it accepts KLEE's
  // raised ASM for uniformity, but since it was built on raw LLVM IR, it's best
  // to do everything beforehand.
  if (opts.EnableNvmInfo) {
    // (iangneal) The "add" function will destroy the pass for us.
    // klee_message("Running NvmAnalysisPass.");
    // pm.add(new NvmAnalysisPass(&nvmInfo));
    klee_warning_once(0, "We are no longer using the static NvmAnalysisPass.");
  }

  pm.add(new RaiseAsmPass());

  // This pass will scalarize as much code as possible so that the Executor
  // does not need to handle operands of vector type for most instructions
  // other than InsertElementInst and ExtractElementInst.
  //
  // NOTE: Must come before division/overshift checks because those passes
  // don't know how to handle vector instructions.
  pm.add(createScalarizerPass());

  // This pass will replace atomic instructions with non-atomic operations
  pm.add(createLowerAtomicPass());
  if (opts.CheckDivZero) pm.add(new DivCheckPass());
  if (opts.CheckOvershift) pm.add(new OvershiftCheckPass());

  pm.add(new IntrinsicCleanerPass(*targetData));
  pm.run(*module);
}

void KModule::optimiseAndPrepare(
    const Interpreter::ModuleOptions &opts,
    llvm::ArrayRef<const char *> preservedFunctions) {
  // Preserve all functions containing klee-related function calls from being
  // optimised around
  if (!OptimiseKLEECall) {
    legacy::PassManager pm;
    pm.add(new OptNonePass());
    pm.run(*module);
  }

  if (opts.Optimize)
    Optimize(module.get(), preservedFunctions);

  // Add internal functions which are not used to check if instructions
  // have been already visited
  if (opts.CheckDivZero)
    addInternalFunction("klee_div_zero_check");
  if (opts.CheckOvershift)
    addInternalFunction("klee_overshift_check");

  // Needs to happen after linking (since ctors/dtors can be modified)
  // and optimization (since global optimization can rewrite lists).
  if (opts.LibcMainFunction.empty()) {
    injectStaticConstructorsAndDestructors(module.get(), opts.EntryPoint);
  } else {
    fillUclibcMainInitAndFiniArgs(module.get(), opts.EntryPoint, opts.LibcMainFunction);
  }
  

  // Finally, run the passes that maintain invariants we expect during
  // interpretation. We run the intrinsic cleaner just in case we
  // linked in something with intrinsics but any external calls are
  // going to be unresolved. We really need to handle the intrinsics
  // directly I think?
  legacy::PassManager pm3;
  pm3.add(createCFGSimplificationPass());
  switch(SwitchType) {
  case eSwitchTypeInternal: break;
  case eSwitchTypeSimple: pm3.add(new LowerSwitchPass()); break;
  case eSwitchTypeLLVM:  pm3.add(createLowerSwitchPass()); break;
  default: klee_error("invalid --switch-type");
  }
  pm3.add(new IntrinsicCleanerPass(*targetData));
  pm3.add(new PhiCleanerPass());
  pm3.add(new FunctionAliasPass());

  // if (opts.EnableNvmInfo) {
  //   klee_warning("NVM info requires isolating call instructions to separate "
  //                "basic blocks. Adding block-splitting pass...");
  //   pm3.add(new IsolateCallInstsPass());
  // }

  pm3.run(*module);
}

void KModule::manifest(InterpreterHandler *ih, bool forceSourceOutput) {
  if (OutputSource || forceSourceOutput) {
    std::unique_ptr<llvm::raw_fd_ostream> os(ih->openOutputFile("assembly.ll"));
    assert(os && !os->has_error() && "unable to open source output");
    *os << *module;
  }

  if (OutputModule) {
    std::unique_ptr<llvm::raw_fd_ostream> f(ih->openOutputFile("final.bc"));
#if LLVM_VERSION_CODE >= LLVM_VERSION(7, 0)
    WriteBitcodeToFile(*module, *f);
#else
    WriteBitcodeToFile(module.get(), *f);
#endif
  }

  /* Build shadow structures */

  infos = std::unique_ptr<InstructionInfoTable>(
      new InstructionInfoTable(*module.get()));

  std::vector<Function *> declarations;

  for (auto &Function : *module) {
    if (Function.isDeclaration()) {
      declarations.push_back(&Function);
      continue;
    }

    auto kf = std::unique_ptr<KFunction>(new KFunction(&Function, this));

    for (unsigned i=0; i<kf->numInstructions; ++i) {
      KInstruction *ki = kf->instructions[i];
      ki->info = &infos->getInfo(*ki->inst);
    }

    functionMap.insert(std::make_pair(&Function, kf.get()));
    functions.push_back(std::move(kf));
  }

  /* Compute various interesting properties */

  for (auto &kf : functions) {
    if (functionEscapes(kf->function))
      escapingFunctions.insert(kf->function);
  }

  for (auto &declaration : declarations) {
    if (functionEscapes(declaration))
      escapingFunctions.insert(declaration);
  }

  if (DebugPrintEscapingFunctions && !escapingFunctions.empty()) {
    llvm::errs() << "KLEE: escaping functions: [";
    std::string delimiter = "";
    for (auto &Function : escapingFunctions) {
      llvm::errs() << delimiter << Function->getName();
      delimiter = ", ";
    }
    llvm::errs() << "]\n";
  }
}

void KModule::checkModule() {
  InstructionOperandTypeCheckPass *operandTypeCheckPass =
      new InstructionOperandTypeCheckPass();

  legacy::PassManager pm;
  if (!DontVerify) {
    klee_message("Creating verifier pass because DontVerify=False");
    pm.add(createVerifierPass());
  } else {
    klee_warning("Skipping verification!");
  }
  pm.add(operandTypeCheckPass);
  pm.run(*module);

  // Enforce the operand type invariants that the Executor expects.  This
  // implicitly depends on the "Scalarizer" pass to be run in order to succeed
  // in the presence of vector instructions.
  if (!operandTypeCheckPass->checkPassed()) {
    klee_error("Unexpected instruction operand types detected");
  }
}

KConstant* KModule::getKConstant(const Constant *c) {
  auto it = constantMap.find(c);
  if (it != constantMap.end())
    return it->second.get();
  return NULL;
}

KInstruction *KModule::getKInstruction(llvm::Instruction *inst) {
  llvm::Function *f = inst->getFunction();
  KFunction *kf = functionMap[f];
  return kf->getKInstruction(inst);
}

unsigned KModule::getConstantID(Constant *c, KInstruction* ki) {
  if (KConstant *kc = getKConstant(c))
    return kc->id;  

  unsigned id = constants.size();
  auto kc = std::unique_ptr<KConstant>(new KConstant(c, id, ki));
  constantMap.insert(std::make_pair(c, std::move(kc)));
  constants.push_back(c);
  return id;
}

NvmFunctionInfo *KModule::getNvmFunctionInfo() {
  if (nvmInfo) return &nvmInfo;
  return nullptr;
}

/***/

KConstant::KConstant(llvm::Constant* _ct, unsigned _id, KInstruction* _ki) {
  ct = _ct;
  id = _id;
  ki = _ki;
}

/***/

static int getOperandNum(Value *v,
                         std::map<Instruction*, unsigned> &registerMap,
                         KModule *km,
                         KInstruction *ki) {
  if (Instruction *inst = dyn_cast<Instruction>(v)) {
    return registerMap[inst];
  } else if (Argument *a = dyn_cast<Argument>(v)) {
    return a->getArgNo();
  } else if (isa<BasicBlock>(v) || isa<InlineAsm>(v) ||
             isa<MetadataAsValue>(v)) {
    return -1;
  } else {
    assert(isa<Constant>(v));
    Constant *c = cast<Constant>(v);
    return -(km->getConstantID(c, ki) + 2);
  }
}

KFunction::KFunction(llvm::Function *_function,
                     KModule *km) 
  : function(_function),
    numArgs(function->arg_size()),
    numInstructions(0),
    trackCoverage(true) {
  // Assign unique instruction IDs to each basic block
  for (auto &BasicBlock : *function) {
    basicBlockEntry[&BasicBlock] = numInstructions;
    numInstructions += BasicBlock.size();
  }

  instructions = new KInstruction*[numInstructions];

  std::map<Instruction*, unsigned> registerMap;

  // The first arg_size() registers are reserved for formals.
  unsigned rnum = numArgs;
  for (llvm::Function::iterator bbit = function->begin(), 
         bbie = function->end(); bbit != bbie; ++bbit) {
    for (llvm::BasicBlock::iterator it = bbit->begin(), ie = bbit->end();
         it != ie; ++it)
      registerMap[&*it] = rnum++;
  }
  numRegisters = rnum;
  
  unsigned i = 0;
  for (llvm::Function::iterator bbit = function->begin(), 
         bbie = function->end(); bbit != bbie; ++bbit) {
    for (llvm::BasicBlock::iterator it = bbit->begin(), ie = bbit->end();
         it != ie; ++it) {
      KInstruction *ki;

      switch(it->getOpcode()) {
      case Instruction::GetElementPtr:
      case Instruction::InsertValue:
      case Instruction::ExtractValue:
        ki = new KGEPInstruction(); break;
      default:
        ki = new KInstruction(); break;
      }

      Instruction *inst = &*it;
      ki->inst = inst;
      ki->dest = registerMap[inst];

      if (isa<CallInst>(it) || isa<InvokeInst>(it)) {
        CallSite cs(inst);
        unsigned numArgs = cs.arg_size();
        ki->operands = new int[numArgs+1];
        ki->operands[0] = getOperandNum(cs.getCalledValue(), registerMap, km,
                                        ki);
        for (unsigned j=0; j<numArgs; j++) {
          Value *v = cs.getArgument(j);
          ki->operands[j+1] = getOperandNum(v, registerMap, km, ki);
        }
      } else {
        unsigned numOperands = it->getNumOperands();
        ki->operands = new int[numOperands];
        for (unsigned j=0; j<numOperands; j++) {
          Value *v = it->getOperand(j);
          ki->operands[j] = getOperandNum(v, registerMap, km, ki);
        }
      }

      instructions[i++] = ki;
    }
  }
}

KInstruction *KFunction::getKInstruction(llvm::Instruction *inst) {
  // Find the instruction's index within its basic block.
  // XXX: This is hacky but there's no other way we know of.
  BasicBlock *bb = inst->getParent();
  size_t indexInBB = 0;
  for (auto it = bb->begin(); it != bb->end(); ++it) {
    if (&*it == inst)
      break;
    ++indexInBB;
  }

  // Add that index to the index where the BB begins in the instruction list
  auto bbEntryIndex = basicBlockEntry[bb];

  return instructions[bbEntryIndex + indexInBB];
}

KFunction::~KFunction() {
  for (unsigned i=0; i<numInstructions; ++i)
    delete instructions[i];
  delete[] instructions;
}
