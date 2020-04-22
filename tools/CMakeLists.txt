#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
add_subdirectory(gen-bout)
add_subdirectory(gen-random-bout)
add_subdirectory(kleaver)
add_subdirectory(klee)
# klee-replay is disabled because there are too many hacks coupled with POSIX
# runtime (e.g. fd_init.c). I do not want to add more hacks after I porting
# cloud9's POSIX runtime
#add_subdirectory(klee-replay)
add_subdirectory(klee-stats)
add_subdirectory(ktest-tool)
