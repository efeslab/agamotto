cmake_minimum_required(VERSION 3.1) 
set(PM_INVADERS_SRC
  pminvaders_nogui.c
)

include_directories(${PMDK_INCLUDE_DIR})
link_directories(${PMDK_LIB_DIR})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1 -Xclang -disable-llvm-passes -D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__")

klee_add_test_executable(TARGET 008_PM_Invaders1 SOURCES ${PM_INVADERS_SRC}
  EXTRA_LIBS pmemobj pmem ncurses)
