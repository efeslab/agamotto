cmake_minimum_required(VERSION 3.1)
find_package(PkgConfig REQUIRED)
# https://stackoverflow.com/questions/54864924/cmakelist-file-to-generate-llvm-bitcode-file-from-c-source-file
# set(CMAKE_C_COMPILER "clang-8")
# set(CMAKE_LINKER "ldd-8")

include(ProcessorCount)
ProcessorCount(N)

set(KLEE_FLAGS "-g -O1 -Xclang -disable-llvm-passes -D__NO_STRING_INLINES -DVALGRIND_ENABLED=0")

################################################################################
# Here, we download and build PMDK

set(PMDK_ROOT_DIR ${CMAKE_CURRENT_BINARY_DIR}/pmdk)
set(PMDK_INSTALL_DIR ${PMDK_ROOT_DIR}/install)
# set(PMDK_INCLUDE_DIR ${PMDK_INSTALL_DIR}/include)
# set(PMDK_LIB_DIR ${PMDK_INSTALL_DIR}/lib)
set(PMDK_INCLUDE_DIR ${PMDK_INSTALL_DIR}/include)
set(PMDK_LIB_DIR ${PMDK_INSTALL_DIR}/lib/pmdk_debug)

if(NOT EXISTS "${PMDK_ROOT_DIR}")
    message("no pmdk at ${PMDK_ROOT_DIR}")
    execute_process(COMMAND git clone https://github.com/pmem/pmdk.git ${PMDK_ROOT_DIR})
    # execute_process(COMMAND git -C ${PMDK_ROOT_DIR} checkout tags/1.7)
    execute_process(COMMAND git -C ${PMDK_ROOT_DIR} checkout master)
endif()

set(ENV{LLVM_COMPILER} clang)
set(ENV{LLVM_COMPILER_PATH} /usr/lib/llvm-8/bin)

set(CMAKE_C_COMPILER wllvm)
set(CMAKE_CXX_COMPILER wllvm++)
find_program(WLLVM wllvm)
if(WLLVM STREQUAL "WLLVM-NOTFOUND")
    message(FATAL_ERROR "Could not find wllvm/wllvm++, required for building PMDK!")
endif()

# Doing CC.... make doesn't work. Need to have the command first, then the variables

# set(KLEE_FLAGS "-g -O0 -Xclang -disable-O0-optnone -D__NO_STRING_INLINES")
execute_process(COMMAND make 
                        DEBUG=1
                        CC=wllvm 
                        CXX=wllvm++
                        EXTRA_CFLAGS=${KLEE_FLAGS}
                        EXTRA_CXXFLAGS=${KLEE_FLAGS}
                        NDCTL_ENABLE=n
                        AVX512F_AVAILABLE=n
                        -C ${PMDK_ROOT_DIR} -j${N})
execute_process(COMMAND make 
                        DEBUG=1
                        CC=wllvm 
                        CXX=wllvm++
                        EXTRA_CFLAGS=${KLEE_FLAGS}
                        EXTRA_CXXFLAGS=${KLEE_FLAGS}
                        NDCTL_ENABLE=n
                        AVX512F_AVAILABLE=n
                        -C ${PMDK_ROOT_DIR} -j${N} install prefix=${PMDK_INSTALL_DIR})

# Now we need to do the extraction
file(GLOB pmdk_libs ${PMDK_LIB_DIR}/*.so )
foreach(shared IN LISTS pmdk_libs)
    execute_process(COMMAND extract-bc ${shared} -o ${shared}.bc)
    if (EXISTS "${shared}.bc")
        file(COPY "${shared}.bc" DESTINATION "${KLEE_RUNTIME_DIRECTORY}")
    endif()
endforeach(shared)

if((NOT EXISTS ${PMDK_INCLUDE_DIR}) OR (NOT EXISTS ${PMDK_LIB_DIR}))
    message(FATAL_ERROR "PMDK build and installation failed!")
endif()


################################################################################
# We also need libevent for memcached

set(LIBEVENT_ROOT_DIR ${CMAKE_CURRENT_BINARY_DIR}/libevent)
set(LIBEVENT_INSTALL_DIR ${LIBEVENT_ROOT_DIR}/install)
set(LIBEVENT_INCLUDE_DIR ${LIBEVENT_INSTALL_DIR}/include)
set(LIBEVENT_LIB_DIR ${LIBEVENT_INSTALL_DIR}/lib/)

if(NOT EXISTS "${LIBEVENT_ROOT_DIR}")
    message("no libevent at ${LIBEVENT_ROOT_DIR}")
    execute_process(COMMAND git clone https://github.com/libevent/libevent.git ${LIBEVENT_ROOT_DIR})
    execute_process(COMMAND git -C ${LIBEVENT_ROOT_DIR} checkout master)

    execute_process(COMMAND ${LIBEVENT_ROOT_DIR}/autogen.sh
                WORKING_DIRECTORY ${LIBEVENT_ROOT_DIR})

    execute_process(COMMAND env CC=wllvm CXX=wllvm++ CFLAGS=${KLEE_FLAGS} 
                            ${LIBEVENT_ROOT_DIR}/configure --disable-openssl
                    WORKING_DIRECTORY ${LIBEVENT_ROOT_DIR})
endif()

execute_process(COMMAND make -j${N} WORKING_DIRECTORY ${LIBEVENT_ROOT_DIR})
execute_process(COMMAND make install prefix=${LIBEVENT_INSTALL_DIR}
                WORKING_DIRECTORY ${LIBEVENT_ROOT_DIR})

file(GLOB libevent_libs ${LIBEVENT_LIB_DIR}/*.so )
foreach(shared IN LISTS libevent_libs)
    execute_process(COMMAND extract-bc ${shared} -o ${shared}.bc)
    if (EXISTS "${shared}.bc")
        file(COPY "${shared}.bc" DESTINATION "${KLEE_RUNTIME_DIRECTORY}")
    endif()
endforeach(shared)

################################################################################

################################################################################
# Lenovo's Memcached

set(MEMCACHED_ROOT_DIR ${CMAKE_CURRENT_BINARY_DIR}/memcached-pmem)

if(NOT EXISTS "${MEMCACHED_ROOT_DIR}")
    message("no memcached at ${memcached_ROOT_DIR}")
    execute_process(COMMAND git clone https://github.com/lenovo/memcached-pmem ${MEMCACHED_ROOT_DIR})
    execute_process(COMMAND git -C ${MEMCACHED_ROOT_DIR} checkout master)

    set(MEMCACHED_LIBS_FLAGS "-levent -lpmem")
    set(MEMCACHED_LDFLAGS_FLAGS "-L${LIBEVENT_LIB_DIR} -L${PMDK_LIB_DIR}")
    set(MEMCACHED_CFLAGS_FLAGS "-I${LIBEVENT_INCLUDE_DIR} -I${PMDK_INCLUDE_DIR} ${KLEE_FLAGS}")
    set(MEMCACHED_LD_PATH "${LIBEVENT_LIB_DIR}:${PMDK_LIB_DIR}")
    
    execute_process(COMMAND env 
                            LD_LIBRARY_PATH=${MEMCACHED_LD_PATH}
                            LIBS=${MEMCACHED_LIBS_FLAGS}
                            LDFLAGS=${MEMCACHED_LDFLAGS_FLAGS}
                            CFLAGS=${MEMCACHED_CFLAGS_FLAGS}
                            CC=wllvm 
                            CXX=wllvm++ 
                            ${MEMCACHED_ROOT_DIR}/configure --enable-pslab
                    WORKING_DIRECTORY ${MEMCACHED_ROOT_DIR})
endif()

execute_process(COMMAND make -j${N} -C ${MEMCACHED_ROOT_DIR})
# message("${CMAKE_BINARY_DIR}/bin/memcached.bc")
execute_process(COMMAND extract-bc "memcached" -o "${CMAKE_BINARY_DIR}/bin/memcached.bc"
                WORKING_DIRECTORY ${MEMCACHED_ROOT_DIR})      
# add_custom_command(COMMAND extract-bc $<TARGET_FILE:${MEMCACHED_ROOT_DIR}/memcached> 
#                            -o $<TARGET_FILE_DIR:klee>/memcached.bc)         

################################################################################

# Shared
add_custom_target(set_wllvm_env COMMAND export LLVM_COMPILER=clang; export LLVM_CC_NAME=clang-8)

# Subdirectories can access header files in this directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(000_pmdk_btree_map)
add_subdirectory(001_simple)  
add_subdirectory(002_simple_symbolic)
add_subdirectory(003_pmdk_rbtree_map)
add_subdirectory(003_heuristic_functional)
add_subdirectory(004_heuristic_correctness)
