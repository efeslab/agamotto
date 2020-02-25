
default: mmap_test

mmap_test:
	clang -I ../../include -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone mmap_test.c
	clang -I ../../include -g -O0 -Xclang -disable-O0-optnone mmap_test.c -o mmap_test

clean:
	rm -vf *.bc *.o a.out mmap_test

