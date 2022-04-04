include ../../Makefile.main.common

NAME = main

LIB_DIR = ../lib
SRC_FILES = $(NAME).c $(LIB_DIR)/hash.c $(LIB_DIR)/log.c $(LIB_DIR)/pflush.c $(LIB_DIR)/level_hashing.c $(LIB_DIR)/pmdk.c
IR_FILES += $(SRC_FILES:%.c=%.bc)
OBJ_FILES = $(SRC_FILES:%.c=%.o)
COV_FILES = $(SRC_FILES:%.c=%.cov.o)

INC_DIR0 = ../include
INC_DIR1 = $(GIRI_DIR)/include/Witcher
CFLAGS += -I$(INC_DIR0) -I$(INC_DIR1) -Wno-deprecated

# Generate $(NAME).bc for $(NAME).c
$(NAME).bc : $(NAME).c
	$(CC) $(CFLAGS) $+ -o $@

# Generate $(NAME).o for $(NAME).c
$(NAME).o : $(NAME).c
	$(CC) -g -c -I$(INC_DIR0) -I$(INC_DIR1) $+

$(NAME).cov.o : $(NAME).c
	$(CC) -g -c -fprofile-instr-generate -fcoverage-mapping -I$(INC_DIR0) -I$(INC_DIR1) $+ -o $@
