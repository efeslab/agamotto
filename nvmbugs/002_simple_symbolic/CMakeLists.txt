cmake_minimum_required(VERSION 3.1)

klee_add_test_object(
  TARGET 002_SymbolicOffsetTest
  SOURCES symbolic_offset_test.c
  EXTRA_OPTIONS -mclwb
) 