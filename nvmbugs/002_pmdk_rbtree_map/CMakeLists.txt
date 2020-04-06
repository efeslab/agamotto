cmake_minimum_required(VERSION 3.1) 
#set(BUGGY_SRC2
#simple_driver.c
#rbtree_map_buggy.c
#rbtree_map.h
#)

#set(CLEAN_SRC2
#simple_driver.c
#rbtree_map_clean.c
#rbtree_map.h
#)

set(DEFAULT_SRC2
  driver.c
  rbtree_map.c
  rbtree_map.h
)

include_directories(${PMDK_INCLUDE_DIR})
link_directories(${PMDK_LIB_DIR})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1 -Xclang -disable-llvm-passes -D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__")

klee_add_test_executable(TARGET 002_Clean SOURCES ${DEFAULT_SRC2} EXTRA_LIBS pmemobj pmem)

# target_link_libraries(002_Clean pmemobj pmem)
set(CLEAN_EXEC2 ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/002_Clean)
add_custom_target(EXTRACT_CLEAN2 COMMAND extract-bc ${CLEAN_EXEC2} -o ${CLEAN_EXEC2}.bc)
add_dependencies(EXTRACT_CLEAN2 002_Clean)
add_custom_target(LINK_CLEAN2 COMMAND llvm-link-8 -o ${CLEAN_EXEC2}.linked.bc
  ${CLEAN_EXEC2}.bc ${PMDK_LIB_DIR}/libpmemobj.so.bc)
add_dependencies(LINK_CLEAN2 EXTRACT_CLEAN2)

#klee_add_test_executable(TARGET 002_Buggy SOURCES ${BUGGY_SRC2} EXTRA_LIBS pmemobj pmem)
# target_link_libraries(002_Buggy pmemobj pmem)
