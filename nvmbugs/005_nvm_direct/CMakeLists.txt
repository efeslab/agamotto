cmake_minimum_required(VERSION 3.1)
 

file(GLOB ORIGINAL_SOURCES "original_lib/*.c")
file(GLOB ORIGINAL_HEADERS "original_lib/*.h")
set(ORIGINAL_SRC ${ORIGINAL_SOURCES} ${ORIGINAL_HEADERS})

file(GLOB LIB_SOURCES "lib/*.c")
file(GLOB LIB_HEADERS "lib/*.h")
set(LIB_SRC ${LIB_SOURCES} ${LIB_HEADERS})

set(TINY_SRC ${LIB_SRC} tiny.c)
set(TINY_ORIGINAL ${ORIGINAL_SRC} tiny.c)

set(SMALL_SRC ${LIB_SRC} small.c)
set(SMALL_ORIGINAL ${ORIGINAL_SRC} small.c)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1 -pthread -Xclang -disable-llvm-passes -D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__")

klee_add_test_executable(TARGET 005_Tiny SOURCES ${TINY_SRC})
klee_add_test_executable(TARGET 005_TinyOriginal SOURCES ${TINY_ORIGINAL})

klee_add_test_executable(TARGET 005_Small SOURCES ${SMALL_SRC})
klee_add_test_executable(TARGET 005_SmallOriginal SOURCES ${SMALL_ORIGINAL})
