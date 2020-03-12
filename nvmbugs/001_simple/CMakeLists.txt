cmake_minimum_required(VERSION 3.1)


include_directories(${PMDK_INCLUDE_DIR})
# link_directories("include/")

# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -Xclang -disable-llvm-passes")


# add_executable(001_BranchMod branch_nvm_mod.c)
# add_executable(001_SymMmap sym_mmap.c)
klee_add_test_executable(001_BranchMod
  branch_nvm_mod.c
)

klee_add_test_object(001_SimpleMod
  simple_nvm_mod.c
)

klee_add_test_object(001_SimpleBranchMod
  branch_paths.c
) 