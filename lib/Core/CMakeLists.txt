#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
# iangneal: support for Andersen's whole program pointer analysis
set(ANDERSEN_SRC ${PROJECT_SOURCE_DIR}/deps/andersen-pa)
set(ANDERSEN_BIN ${PROJECT_BINARY_DIR}/deps/andersen-pa)
set(BUILD_TESTS OFF)
add_subdirectory(${ANDERSEN_SRC} ${ANDERSEN_BIN})
include_directories(${ANDERSEN_SRC}/include)
list(APPEND KLEE_COMPONENT_EXTRA_INCLUDE_DIRS ${ANDERSEN_SRC}/include)

klee_add_component(kleeCore
  AddressSpace.cpp
  MergeHandler.cpp
  CallPathManager.cpp
  Context.cpp
  CoreStats.cpp
  ExecutionState.cpp
  Executor.cpp
  ExecutorUtil.cpp
  ExternalDispatcher.cpp
  ImpliedValue.cpp
  Memory.cpp
  MemoryManager.cpp
  NvmAnalysisUtils.cpp
  NvmFunctionInfo.cpp
  NvmHeuristicInfo.cpp
  PTree.cpp
  Searcher.cpp
  SeedInfo.cpp
  SpecialFunctionHandler.cpp
  StatsTracker.cpp
  TimingSolver.cpp
  UserSearcher.cpp
)

# TODO: Work out what the correct LLVM components are for
# kleeCore.
set(LLVM_COMPONENTS
  core
  executionengine
  mcjit
  native
  support
)

klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})
target_link_libraries(kleeCore PUBLIC ${LLVM_LIBS} ${SQLITE3_LIBRARIES})
target_link_libraries(kleeCore PRIVATE
  kleeBasic
  kleeModule
  kleaverSolver
  kleaverExpr
  kleeSupport
  Andersen
)
