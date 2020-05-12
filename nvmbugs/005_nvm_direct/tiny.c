#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <setjmp.h>
#include <string.h>
#include <pthread.h>
#include "nvm.h"
#include "nvm_region0.h"
#include "nvm_heap0.h"

/* The virtual address to attach the main region at */
//#define attch_int (1024 * 1024 * 1024 * 128LL) //fixed mapping
#define attch_int (0) // Let OS choose
#define attch ((uint8_t*)attch_int)

// void *ext = nvm_add_extent(base + xoff, ptrspace);
/* The virtual address space consumed by the main region */
const size_t vspace = 8 * 1024 * 1024;

/* the physical size of the base extent */
const size_t pspace = 2 * 1024 * 1024;

/*
 * This is the main driver of the test
 */
int
main(int argc, char** argv)
{
    const char *fname = "/tmp/nvmd";
    if (argc == 2)
        fname = argv[1];

    printf("NVM Device: \'%s\'\n", fname);
    /* We get here if we are the child process or there are no children.
     * The first thing is to initialize the NVM library for the main thread */
    nvm_thread_init();

    /* If attaching the region does not work then create a new one. This will
     * run recovery if needed. */
    printf("Driver: creating region\n");
    /* nvm_desc desc = nvm_attach_region(0, fname, attch); */
    nvm_desc desc = nvm_create_region(0, fname, "TestRegion needs 32 characters at least for 2 cachelines",
                    attch, vspace, pspace, 0777);
    printf("Driver: detaching region\n");
    nvm_detach_region(desc);
    printf("Driver: finishing thread\n");
    nvm_thread_fini();
    printf("Driver: exiting now, success\n");
}
