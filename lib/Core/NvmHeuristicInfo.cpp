#include "NvmFunctionInfo.h"

using namespace llvm;
using namespace std;
using namespace klee;

NvmGlobalVariableDesc::NvmGlobalVariableDesc() {}

static shared_ptr<NvmGlobalVariableDesc> NvmGlobalVariableDesc::create(Module* m) {

}

/**
 * NvmHeuristicInfo
 */

uint64_t computePriority()

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
