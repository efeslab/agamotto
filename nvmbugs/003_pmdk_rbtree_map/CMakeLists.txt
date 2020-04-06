cmake_minimum_required(VERSION 3.1) 
set(BUGGY_SRC_RBTREE
  driver.c
  rbtree_map_buggy.c
  tree_map.h
  rbtree_map.h
)

set(CLEAN_SRC_RBTREE
  driver.c
  rbtree_map_clean.c
  tree_map.h
  rbtree_map.h
)


include_directories(${PMDK_INCLUDE_DIR})
link_directories(${PMDK_LIB_DIR})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1 -Xclang -disable-llvm-passes -D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__")

klee_add_test_executable(TARGET 003_Clean SOURCES ${CLEAN_SRC_RBTREE} EXTRA_LIBS pmemobj pmem)

set(CLEAN_EXEC_RBTREE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/003_Clean)
add_custom_target(EXTRACT_CLEAN_RBTREE COMMAND extract-bc ${CLEAN_EXEC_RBTREE} -o ${CLEAN_EXEC_RBTREE}.bc)
add_dependencies(EXTRACT_CLEAN_RBTREE 003_Clean)
add_custom_target(LINK_CLEAN_RBTREE COMMAND llvm-link-8 -o ${CLEAN_EXEC_RBTREE}.linked.bc ${CLEAN_EXEC_RBTREE}.bc ${PMDK_LIB_DIR}/libpmemobj.so.bc)
add_dependencies(LINK_CLEAN_RBTREE EXTRACT_CLEAN_RBTREE)

klee_add_test_executable(TARGET 003_Buggy SOURCES ${BUGGY_SRC_RBTREE} EXTRA_LIBS pmemobj pmem)
