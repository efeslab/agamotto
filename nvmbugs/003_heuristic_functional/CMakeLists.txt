cmake_minimum_required(VERSION 3.1)

klee_add_test_object(TARGET 003_VarArgTest SOURCES vararg_test.c)
klee_add_test_object(TARGET 003_RecursionTest SOURCES recursion.c)
klee_add_test_object(TARGET 003_FnPtrTest SOURCES function_pointer_test.c)