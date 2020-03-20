#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

function(klee_add_test_object)
  set(options)
  set(oneValueArgs TARGET)
  set(multiValueArgs SOURCES EXTRA_OPTIONS)
  cmake_parse_arguments(KO "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )

  set(KLEE_EXE_FLAGS "-g;-O0;-Xclang;-disable-llvm-passes")

  message("KO_TARGET ${KO_TARGET}")
  message("KO_SOURCES ${KO_SOURCES}")
  message("KO_EXTRA_OPTIONS ${KO_EXTRA_OPTIONS}")

  foreach(opt IN LISTS KO_EXTRA_OPTIONS)
    string(APPEND KLEE_EXE_FLAGS ";${opt}")
  endforeach()

  add_library(${KO_TARGET} SHARED ${KO_SOURCES})
  
  target_compile_options(${KO_TARGET} PUBLIC ${KLEE_EXE_FLAGS})
  
  target_include_directories(${KO_TARGET} PUBLIC ${KLEE_COMPONENT_EXTRA_INCLUDE_DIRS})

  set(BITCODE_LOC ${RUNTIME_OUTPUT_DIRECTORY}/${KO_TARGET}.bc)
  # # Now we auto extract
  add_custom_command(TARGET ${KO_TARGET} 
                     POST_BUILD
                     COMMAND extract-bc $<TARGET_FILE:${KO_TARGET}> -o $<TARGET_FILE_DIR:klee>/${KO_TARGET}.bc)
endfunction()
