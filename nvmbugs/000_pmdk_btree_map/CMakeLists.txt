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

include_directories(${PMDK_INCLUDE_DIR})
link_directories(${PMDK_LIB_DIR})

add_executable(000_Clean ${CLEAN_SRC})
target_link_libraries(000_Clean pmemobj pmem)

add_executable(000_Buggy ${BUGGY_SRC})
target_link_libraries(000_Buggy pmemobj pmem)