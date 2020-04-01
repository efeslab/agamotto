#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>

#define BUF_LEN 4096

int mod_function(char *addr, bool mod) {
    char a = 2;
    if (mod) {
        addr[0] = a;
    } else {
        a = addr[0];
    }

    return a;
}

int loop_function(char *addr, int count) {
    for (int i = 0; i < count; ++i) {
        addr[i] = (char)i;
    }

    return 0;
}

int loop_extra(char *addr, int count) {
    for (int i = 0; i < count; ++i) {
        addr[i] = (char)i;
    }

    addr[0] = 'S';

    return 0;
}

#define USE_MMAP 1
int main(int argc, char *argv[]) {
    #if USE_MMAP
    if (argc < 3) {
        fprintf(stderr, "usage: %s PMEM_FILE_NAME <0/1 for DoMod>\n", argv[0]);
        return -1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("file open");
        return -1;
    }

	char *pmemaddr = (char*)mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, 
                                 MAP_PRIVATE, fd, 0);
    if (pmemaddr == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    #else
    char pmemaddr[BUF_LEN];
    #endif

    bool doMod = !!atoi(argv[2]);

    mod_function(pmemaddr, doMod);
    // mod_function(pmemaddr, false);

    loop_function(pmemaddr, 10);
    loop_extra(pmemaddr, 10);

    #if USE_MMAP
    int ret = munmap(pmemaddr, getpagesize());
    if (ret) {
        perror("unmap!");
        return -1;
    }
    close(fd);
    #endif

	return 0;
}
