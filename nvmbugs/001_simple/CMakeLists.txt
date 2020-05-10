cmake_minimum_required(VERSION 3.1)


include_directories(${PMDK_INCLUDE_DIR})
link_directories(${PMDK_LIB_DIR})
# link_directories("include/")

# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -Xclang -disable-llvm-passes")


# add_executable(001_BranchMod branch_nvm_mod.c)
# add_executable(001_SymMmap sym_mmap.c)
klee_add_test_executable(TARGET 001_BranchMod SOURCES
  branch_nvm_mod.c
)

klee_add_test_object(TARGET 001_InlineAsm SOURCES
  inline_asm.c
)

klee_add_test_object(TARGET 001_SimpleMod SOURCES
  simple_nvm_mod.c
)

klee_add_test_object(TARGET 001_FlushAndFence SOURCES
  flush_and_fence.c
)

klee_add_test_object(TARGET 001_FlushNoFence SOURCES
  flush_no_fence.c
)

klee_add_test_object(TARGET 001_SimpleBranchMod SOURCES
  branch_paths.c
) 

klee_add_test_object(TARGET 001_DoubleFlushErr SOURCES
  double_flush_err.c
)

klee_add_test_object(TARGET 001_DoubleFlushNoErr SOURCES
  double_flush_no_err.c
  EXTRA_OPTIONS -mclwb
)

klee_add_test_object(TARGET 001_CleanFlushErr SOURCES
  clean_flush_err.c
)

klee_add_test_object(TARGET 001_MayFlushErr SOURCES
  may_flush_err.c
)

klee_add_test_object(TARGET 001_NontemporalErr SOURCES
  nontemporal_err.c
)

klee_add_test_object(TARGET 001_NontemporalNoErr SOURCES
  nontemporal_noerr.c
)
klee_add_test_object(TARGET 001_SimpleLoop SOURCES
  simple_loop.c
) 

klee_add_test_object(TARGET 001_DoublePmemPersist 
                     SOURCES double_pmem_persist.c)

klee_add_test_object(TARGET 001_NoFlushNoErr
                     SOURCES no_flush_no_err.c)