cmake_minimum_required(VERSION 3.1)

klee_add_test_object(
  TARGET 002_SymbolicOffsetTest
  SOURCES symbolic_offset_test.c
  EXTRA_OPTIONS -mclwb
) 

klee_add_test_object(
  TARGET 002_SimpleSymbolic
  SOURCES simple_symbolic.c
  EXTRA_OPTIONS -mclwb
) 

klee_add_test_object(
  TARGET 002_TwoObjectTest
  SOURCES symbolic_two_object.c
  EXTRA_OPTIONS -mclwb
) 
