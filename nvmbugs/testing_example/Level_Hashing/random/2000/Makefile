NAME = main

PM_FILE_PATH = pm.img
PM_SIZE = 32
PM_LAYOUT = layout
OP_FILE_PATH = op_file.txt
OP_INDEX = 0
SKIP_INDEX = -1
OUTPUT_PATH = output
MEM_LAYOUT_PATH = memory_layout
INPUT = $(PM_FILE_PATH) $(PM_SIZE) $(PM_LAYOUT) $(OP_FILE_PATH) $(OP_INDEX) $(SKIP_INDEX) $(OUTPUT_PATH) $(MEM_LAYOUT_PATH)

PM_ADDR = $(PMEM_MMAP_HINT)

REPLAY_OUT_PATH = replay-output
OP_PATH = op_file.txt

TRACE_EXE = ../../main/$(NAME).trace.exe
EXE = ../../main/$(NAME).exe
COV_EXE = ../../main/$(NAME).cov.exe
ALL_BC = ../../main/$(NAME).all.bc

CHECK_EXE = ../../check/check.exe

include ../../../Makefile.test.parallel.common
