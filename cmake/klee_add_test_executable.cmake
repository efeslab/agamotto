#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

function(klee_add_test_executable)
  set(options)
  set(oneValueArgs TARGET)
  set(multiValueArgs SOURCES EXTRA_LIBS)
  cmake_parse_arguments(KE_ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )

  set(KLEE_EXE_FLAGS "-g;-O0;-Xclang;-disable-llvm-passes")
  # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Xclang -disable-llvm-passes")
  add_executable(${KE_ARGS_TARGET} ${KE_ARGS_SOURCES})
  
  target_compile_options(${KE_ARGS_TARGET} PUBLIC ${KLEE_EXE_FLAGS})

  target_include_directories(${KE_ARGS_TARGET} PUBLIC ${KLEE_COMPONENT_EXTRA_INCLUDE_DIRS})
  # target_compile_definitions(${target_name} PUBLIC ${KLEE_COMPONENT_CXX_DEFINES})
  target_link_libraries(${KE_ARGS_TARGET} PUBLIC ${KLEE_COMPONENT_EXTRA_LIBRARIES} ${KE_ARGS_EXTRA_LIBS})

  # Now we auto extract
  add_custom_command(TARGET ${KE_ARGS_TARGET} 
                     POST_BUILD
                     COMMAND extract-bc $<TARGET_FILE:${KE_ARGS_TARGET}>)
endfunction()
