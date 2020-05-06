cmake_minimum_required(VERSION 3.1)

set(BUGGY_SRC
    driver.c
    btree_map_buggy.c
    btree_map.h
)

set(CLEAN_SRC
    driver.c
    btree_map_clean.c
    btree_map.h
)

set(PERF_SRC
    driver.c
    btree_map_performance_fixed.c
    btree_map.h
)

include_directories(${PMDK_INCLUDE_DIR})
link_directories(${PMDK_LIB_DIR})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1 -Xclang -disable-llvm-passes -D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__")

klee_add_test_executable(TARGET 000_Clean SOURCES ${CLEAN_SRC} EXTRA_LIBS pmemobj pmem)

# target_link_libraries(000_Clean pmemobj pmem)
set(CLEAN_EXEC ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/000_Clean)
add_custom_target(EXTRACT_CLEAN COMMAND extract-bc ${CLEAN_EXEC} -o ${CLEAN_EXEC}.bc)
add_dependencies(EXTRACT_CLEAN 000_Clean)
add_custom_target(LINK_CLEAN COMMAND llvm-link-8 -o ${CLEAN_EXEC}.linked.bc ${CLEAN_EXEC}.bc ${PMDK_LIB_DIR}/libpmemobj.so.bc)
add_dependencies(LINK_CLEAN EXTRACT_CLEAN)

klee_add_test_executable(TARGET 000_Buggy SOURCES ${BUGGY_SRC} EXTRA_LIBS pmemobj pmem)
klee_add_test_executable(TARGET 000_Performance_Fixed SOURCES ${PERF_SRC} EXTRA_LIBS pmemobj pmem)
klee_add_test_executable(TARGET 000_Tiny SOURCES tiny_driver.c btree_map_clean.c btree_map.h EXTRA_LIBS pmemobj pmem)
# target_link_libraries(000_Buggy pmemobj pmem)

set(TEST_SRC
    test.c
)

add_executable(000_Test ${TEST_SRC})

set(PRINTF_TEST_SRC
    printf_test.c
)

add_executable(000_Printf ${PRINTF_TEST_SRC})

set(ONCE_SRC
    once.c
)

add_executable(000_Once ${ONCE_SRC})
target_link_libraries(000_Once pthread)

set(ENV_TEST_SRC
    env_test.c
)
add_executable(000_Env ${ENV_TEST_SRC})

set(CTOR_SRC
    constructor_test.c
)
add_executable(000_Ctor ${CTOR_SRC})

add_executable(000_Mmap mmap_heuristic_test.c)
