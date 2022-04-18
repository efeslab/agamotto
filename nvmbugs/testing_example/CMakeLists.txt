cmake_minimum_required(VERSION 3.1)

################################################################################
# Level Hashing (from WITCHER)
################################################################################

# All the source files needed to build the executable.
set(LEVEL_HASHING_SOURCES
    Level_Hashing/main/main.c
    Level_Hashing/lib/hash.c
    Level_Hashing/lib/level_hashing.c
    Level_Hashing/lib/log.c
    Level_Hashing/lib/pflush.c
    Level_Hashing/lib/pmdk.c
)

# These flags are used to that compile LLVM bitcode that KLEE can use.
# See (https://klee.github.io/tutorials/testing-function/) for details.
set(CMAKE_C_FLAGS
  "${CMAKE_C_FLAGS} -g -O1 -Xclang -disable-llvm-passes -D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__")

include_directories(${PMDK_INCLUDE_DIR} "./Level_Hashing/include")
link_directories(${PMDK_LIB_DIR})

# This command is defined in $REPO_DIR/cmake/klee_add_test_executable.cmake
# It simplifies some of the repeated compilation tasks.
# This will also automatically create a ExLevelHashing.bc file, which is what
# KLEE actually uses to run the program symbolically.
# This executable will be put into build/bin.
klee_add_test_executable(TARGET ExLevelHashing
                         SOURCES ${LEVEL_HASHING_SOURCES}
                         EXTRA_LIBS pmemobj pmem m)
