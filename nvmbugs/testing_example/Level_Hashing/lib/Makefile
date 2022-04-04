CC = $(LLVM9_BIN)/clang
INC_DIR = ../include
CFLAGS = -g -O0 -c -emit-llvm -I$(INC_DIR) -Wno-deprecated

DIR = ../../../third_party/Level_Hashing/persistent_level_hashing
FILES = $(DIR)/hash.c $(DIR)/log.c $(DIR)/pflush.c $(DIR)/level_hashing.c $(DIR)/pmdk.c

.PHONY: all bc obj hash.bc log.bc pflush.bc level_hashing.bc copy clean

all: bc obj cov

bc : hash.bc log.bc pflush.bc level_hashing.bc pmdk.bc

hash.bc : copy
	$(CC) $(CFLAGS) hash.c -o hash.bc
log.bc : copy
	$(CC) $(CFLAGS) log.c -o log.bc
pflush.bc : copy
	$(CC) $(CFLAGS) pflush.c -o pflush.bc
level_hashing.bc : copy
	$(CC) $(CFLAGS) level_hashing.c -o level_hashing.bc
pmdk.bc : copy
	$(CC) $(CFLAGS) pmdk.c -o pmdk.bc

cov: hash.cov.o log.cov.o pflush.cov.o level_hashing.cov.o pmdk.cov.o

hash.cov.o: copy
	$(CC) -g -c -fprofile-instr-generate -fcoverage-mapping -I$(INC_DIR) hash.c -o hash.cov.o
log.cov.o: copy
	$(CC) -g -c -fprofile-instr-generate -fcoverage-mapping -I$(INC_DIR) log.c -o log.cov.o
pflush.cov.o: copy
	$(CC) -g -c -fprofile-instr-generate -fcoverage-mapping -I$(INC_DIR) pflush.c -o pflush.cov.o
level_hashing.cov.o: copy
	$(CC) -g -c -fprofile-instr-generate -fcoverage-mapping -I$(INC_DIR) level_hashing.c -o level_hashing.cov.o
pmdk.cov.o: copy
	$(CC) -g -c -fprofile-instr-generate -fcoverage-mapping -I$(INC_DIR) pmdk.c -o pmdk.cov.o

obj : copy
	$(CC) -g -c -I$(INC_DIR) *.c

copy:
	@ cp $(FILES) .

clean:
	@ rm -f *.c *.bc *.o
