DIR = ../../../third_party/Level_Hashing/persistent_level_hashing
FILES = $(DIR)/hash.h $(DIR)/log.h $(DIR)/pflush.h $(DIR)/level_hashing.h $(DIR)/pmdk.h

.PHONY: all copy clean

all: copy

copy:
	@ cp $(FILES) .

clean:
	@ rm -f hash.h log.h pflush.h level_hashing.h pmdk.h
