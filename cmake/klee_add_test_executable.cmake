#===-- klee_add_test_executable.cmake --------------------------*- CMake -*-===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
# Author: Ian Glen Neal
#
# Purpose: Add a new target that is compiled into a native executable and 
#          has it's bitcode automatically extracted and copied into the 
#          bin/ folder. This is useful for compiling full benchmarks which
#          can be immediately run in KLEE without having to manually invoke
#          'extract-bc' after recompilation. A convenience facility.
#          NOTE: These executables cannot contain calls to klee_* functions,
#          as they do not run on native executables. All symbolics must be 
#          set through the POSIX environment emulation interface.
#
# Usage: See nvmbugs/002_simple_symbolic/CMakeLists.txt.
#
#   klee_add_test_executable(TARGET 001_BranchMod SOURCES branch_nvm_mod.c)
#
#===------------------------------------------------------------------------===#

function(klee_add_test_executable)
  set(options)
  set(oneValueArgs TARGET)
  set(multiValueArgs SOURCES EXTRA_LIBS)
  cmake_parse_arguments(KE_ARGS "${options}" "${oneValueArgs}" 
                        "${multiValueArgs}" ${ARGN} )

  set(KLEE_EXE_FLAGS "-g;-O0;-Xclang;-disable-llvm-passes")
  add_executable(${KE_ARGS_TARGET} ${KE_ARGS_SOURCES})
  
  target_compile_options(${KE_ARGS_TARGET} PUBLIC ${KLEE_EXE_FLAGS})

  target_include_directories(${KE_ARGS_TARGET} 
                             PUBLIC ${KLEE_COMPONENT_EXTRA_INCLUDE_DIRS})
  target_link_libraries(${KE_ARGS_TARGET} 
                        PUBLIC 
                        ${KLEE_COMPONENT_EXTRA_LIBRARIES} ${KE_ARGS_EXTRA_LIBS})

  # Now we auto extract
  add_custom_command(TARGET ${KE_ARGS_TARGET} 
                     POST_BUILD
                     COMMAND extract-bc $<TARGET_FILE:${KE_ARGS_TARGET}>)
endfunction()
