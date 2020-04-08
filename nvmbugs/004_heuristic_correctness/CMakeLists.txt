cmake_minimum_required(VERSION 3.1)

klee_add_test_object(TARGET 004_ReadOrWrite SOURCES read_or_write.c)
klee_add_test_object(TARGET 004_NvmOrVol SOURCES nvm_or_vol.c)