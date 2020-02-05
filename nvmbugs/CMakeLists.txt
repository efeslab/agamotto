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
set(PMDK_INCLUDE_DIR ${PMDK_INSTALL_DIR}/include)
set(PMDK_LIB_DIR ${PMDK_INSTALL_DIR}/lib)

if(NOT EXISTS "${PMDK_ROOT_DIR}")
    message("no pmdk at ${PMDK_ROOT_DIR}")
    execute_process(COMMAND git clone https://github.com/pmem/pmdk.git ${PMDK_ROOT_DIR})
    execute_process(COMMAND git -C ${PMDK_ROOT_DIR} checkout tags/1.7)
endif()

include(ProcessorCount)
ProcessorCount(N)

execute_process(COMMAND make -C ${PMDK_ROOT_DIR} -j${N})
execute_process(COMMAND make -C ${PMDK_ROOT_DIR} install prefix=${PMDK_INSTALL_DIR})

if((NOT EXISTS ${PMDK_INCLUDE_DIR}) OR (NOT EXISTS ${PMDK_LIB_DIR}))
    message(FATAL_ERROR "PMDK build and installation failed!")
endif()

add_subdirectory(000_pmdk_btree_map)