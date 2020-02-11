cmake_minimum_required(VERSION 3.1)
find_package(PkgConfig REQUIRED)
# https://stackoverflow.com/questions/54864924/cmakelist-file-to-generate-llvm-bitcode-file-from-c-source-file
# set(CMAKE_C_COMPILER "clang-8")
# set(CMAKE_LINKER "ldd-8")

# Here, we download and build PMDK

pkg_check_modules(PMDK_REQUIREMENTS
    REQUIRED
    libndctl>=60
    libdaxctl>=60
)

set(PMDK_ROOT_DIR ${CMAKE_CURRENT_BINARY_DIR}/pmdk)
set(PMDK_INSTALL_DIR ${PMDK_ROOT_DIR}/install)
# set(PMDK_INCLUDE_DIR ${PMDK_INSTALL_DIR}/include)
# set(PMDK_LIB_DIR ${PMDK_INSTALL_DIR}/lib)
set(PMDK_INCLUDE_DIR ${PMDK_INSTALL_DIR}/include)
set(PMDK_LIB_DIR ${PMDK_INSTALL_DIR}/lib/pmdk_debug)

if(NOT EXISTS "${PMDK_ROOT_DIR}")
    message("no pmdk at ${PMDK_ROOT_DIR}")
    execute_process(COMMAND git clone https://github.com/pmem/pmdk.git ${PMDK_ROOT_DIR})
    execute_process(COMMAND git -C ${PMDK_ROOT_DIR} checkout tags/1.7)
endif()

include(ProcessorCount)
ProcessorCount(N)

set(ENV{LLVM_COMPILER} clang)
set(ENV{LLVM_COMPILER_PATH} /usr/lib/llvm-8/bin)
# set`(ENV{WLLVM_BC_STORE} ${PMDK_INSTALL_DIR}/bitcode)
# execute_process(COMMAND mkdir -p ${PMDK_INSTALL_DIR}/bitcode)

set(CMAKE_C_COMPILER wllvm)
set(CMAKE_CXX_COMPILER wllvm++)
find_program(WLLVM wllvm)
if(WLLVM STREQUAL "WLLVM-NOTFOUND")
    message(FATAL_ERROR "Could not find wllvm/wllvm++, required for building PMDK!")
endif()

# Doing CC.... make doesn't work. Need to have the command first, then the variables
# set(KLEE_FLAGS "-g -O1 -Xclang -disable-llvm-passes -D__NO_STRING_INLINES")
set(KLEE_FLAGS "-g -O0 -Xclang -disable-O0-optnone -D__NO_STRING_INLINES")
execute_process(COMMAND make 
                        DEBUG=1
                        CC=wllvm 
                        CXX=wllvm++
                        EXTRA_CFLAGS=${KLEE_FLAGS}
                        EXTRA_CXXFLAGS=${KLEE_FLAGS}
                        -C ${PMDK_ROOT_DIR} -j${N})
execute_process(COMMAND make 
                        DEBUG=1
                        CC=wllvm 
                        CXX=wllvm++
                        EXTRA_CFLAGS=${KLEE_FLAGS}
                        EXTRA_CXXFLAGS=${KLEE_FLAGS}
                        -C ${PMDK_ROOT_DIR} -j${N} install prefix=${PMDK_INSTALL_DIR})

# Now we need to do the extraction
file(GLOB pmdk_libs ${PMDK_LIB_DIR}/*.so )
foreach(shared IN LISTS pmdk_libs)
    execute_process(COMMAND extract-bc ${shared} -o ${shared}.bc)
    list(APPEND PMDK_BC "${shared}.bc")
endforeach(shared)

if((NOT EXISTS ${PMDK_INCLUDE_DIR}) OR (NOT EXISTS ${PMDK_LIB_DIR}))
    message(FATAL_ERROR "PMDK build and installation failed!")
endif()

# Shared
add_custom_target(set_wllvm_env COMMAND export LLVM_COMPILER=clang; export LLVM_CC_NAME=clang-8)

add_subdirectory(000_pmdk_btree_map)