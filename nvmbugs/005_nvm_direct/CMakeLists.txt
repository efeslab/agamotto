cmake_minimum_required(VERSION 3.1)
 

file(GLOB LIB_SOURCES "lib/*.c")
file(GLOB LIB_HEADERS "lib/*.h")
set(LIB_SRC ${LIB_SOURCES} ${LIB_HEADERS})

set(DRIVER_SRC
  ${LIB_SRC}
  test_nvmd_main.c
)

set(SIMPLE_SRC
  ${LIB_SRC}
  simpler_driver.c
)

set(TEST_SRC
  ${LIB_SRC}
  simple_no_nvm_ext.c
)

set(REPRODUCE_SRC
  ${LIB_SRC}
  reproduce_simple.c
)

set(TINY_SRC ${LIB_SRC} tiny.c)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1 -pthread -Xclang -disable-llvm-passes -D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__")

klee_add_test_executable(TARGET 005_Driver SOURCES ${DRIVER_SRC})
klee_add_test_executable(TARGET 005_Simple SOURCES ${SIMPLE_SRC})
klee_add_test_executable(TARGET 005_Test SOURCES ${TEST_SRC})
klee_add_test_executable(TARGET 005_Reproduce SOURCES ${REPRODUCE_SRC})
klee_add_test_executable(TARGET 005_Tiny SOURCES ${TINY_SRC})
