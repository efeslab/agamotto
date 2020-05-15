#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <setjmp.h>
#include <string.h>
#include <pthread.h>
#include "lib/nvm.h"
#include "lib/nvm_region0.h"
#include "lib/nvm_heap0.h"

/* the number of log records in a log region */
#define logsz  1024

/* the number of pointers in the application managed extent of pointers */
#define ptrs 2

/*
 * The test repeatedly allocates and deallocates random sized instances
 * of the persistent struct branch. They are allocated from the heap extent.
 */
extern const nvm_type nvm_type_branch;
extern const nvm_type nvm_type_branch_v1;
extern const nvm_type nvm_type_branch_v2;
struct branch
{
    nvm_usid type_usid;
    uint64_t version; // this is the version number changed by upgrade, it is 3
    uint64_t data[0];
};
typedef struct branch branch;
NVM_SRP2(struct branch, branch)
NVM_SRP2(branch_srp, branchArray)

/* These are pretend upgradeable versions of branch used for exercising the
 * upgrade code. */
struct branch_v1
{
    nvm_usid type_usid;
    uint64_t version1; // contains 1
    uint64_t data[0];
};
typedef struct branch_v1 branch_v1;
struct branch_v2
{
    nvm_usid type_usid;
    uint64_t version2; //contains 2
    uint64_t data[0];
};
typedef struct branch_v2 branch_v2;

/*
 * Upgrade a branch_v1 to branch_v2
 */
int upgrade_br_v1(branch_v1 *b1)
{
    nvm_verify(b1, shapeof(branch_v1));
    if (b1->version1 && b1->version1 != 1)
        return 0; // fail due to corruption
    branch_v2 *b2 = (branch_v2*)b1;
    nvm_verify(b2, shapeof(branch_v2));
    NVM_TXSTORE(b2->version2, 2);
    return 1; // success
}

/*
 * Upgrade a branch_v2 to branch
 */
int upgrade_br_v2(branch_v2 *b2)
{
    nvm_verify(b2, shapeof(branch_v2));
    if (b2->version2 && b2->version2 != 2)
        return 0; // fail due to corruption
    branch *b = (branch*)b2;
    nvm_verify(b, shapeof(branch));
    NVM_TXSTORE(b->version, 3);
    return 1; // success
}

/* The type descriptions for all 3 versions of branch. */
const nvm_type nvm_type_branch = {
    {0xa0636cdd76e78b42, 0x0c32eeabd43c829c}, // type_usid
    "branch", // name
    "Test branch stuct", // tag
    sizeof(branch), //size
    8, // xsize
    0, // version
    16, // align
    0, // upgrade
    0, // uptype
    3, // field_cnt
    { // field[]]
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_unsigned, 0, 1, (void*)64, "version"},
        {nvm_field_unsigned, 0, 0, (void*)64, "data"},
        {0, 0, 0, 0, 0}
    }
};
const nvm_type nvm_type_branch_v2 = {
    {0xf8e75535672d1d12, 0xf534478681e21a8c}, // type_usid
    "branch_v2", // name
    "Test branch_v2 stuct", // tag
    sizeof(branch), //size
    8, // xsize
    0, // version
    16, // align
    &upgrade_br_v2, // upgrade
    &nvm_type_branch, // uptype
    3, // field_cnt
    { // field[]]
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_unsigned, 0, 1, (void*)64, "version2"},
        {nvm_field_unsigned, 0, 0, (void*)64, "data"},
        {0, 0, 0, 0, 0}
    }
};
const nvm_type nvm_type_branch_v1 = {
    {0xb0a229e0449d54cb, 0x8bdd5c52356ef016}, // type_usid
    "branch_v1", // name
    "Test branch_v1 stuct", // tag
    sizeof(branch), //size
    8, // xsize
    0, // version
    16, // align
    &upgrade_br_v1, // upgrade
    &nvm_type_branch_v2, // uptype
    3, // field_cnt
    { // field[]]
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_unsigned, 0, 1, (void*)64, "version1"},
        {nvm_field_unsigned, 0, 0, (void*)64, "data"},
        {0, 0, 0, 0, 0}
    }
};

/*
 * The persistent struct root is the root struct in the main region
 * It holds the mutexes that protect the pointers.
 */
struct root
{
    nvm_usid type_usid;
    nvm_mutex mtx[ptrs]; // a bunch of mutexes
    nvm_heap_srp heap; // heap in heap extent
    branchArray_srp ptr; // struct branch ^ptr
};
typedef  struct root root;

const nvm_type nvm_type_root = {
    {0x2c042fa4902080f9, 0xb90bc3a43502958f}, // type_usid
    "root", // name
    "Test root stuct", // tag
    sizeof(root), //size
    0, // xsize
    0, // version
    16, // align
    0, // upgrade
    0, // uptype
    4, // field_cnt
    { // field[]]
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_unsigned, 0, ptrs, (void*)64, "mtx"},
        {nvm_field_pointer, 0, 1, NULL, "heap"},
        {nvm_field_pointer, 0, 1, NULL, "ptr"},
        {0, 0, 0, 0, 0}
    }
};

/*
 * This is one pretend log record
 */
struct logrec
{
    uint64_t rec;  // log record number
    uint32_t slot; // slot operated on
    uint32_t oldsz; // zero if slot was empty
    uint32_t newsz; // zero if allocation failed.
};

/*
 * Note that logrec gets an nvm_type struct description even though it is not
 * declared persistent. This is because it is embedded in the persistent
 * struct log.
 */
const nvm_type nvm_type_logrec = {
    {0, 0}, // type_usid
    "logrec", // name
    "Test log record stuct", // tag
    sizeof(struct logrec), //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    5, // field_cnt
    { // field[]]
        {nvm_field_unsigned, 0, 1, (void*)64, "rec"},
        {nvm_field_unsigned, 0, 1, (void*)32, "slot"},
        {nvm_field_unsigned, 0, 1, (void*)32, "oldsz"},
        {nvm_field_unsigned, 0, 1, (void*)32, "newsz"},
        {nvm_field_pad, 0, 1, (void*)32, "_pad1"},
        {0, 0, 0, 0, 0}
    }
};

/*
 * This is a pretend write ahead log for testing off region nested transactions.
 * It is the root struct of a log region
 */
struct log
{
    nvm_usid type_usid;
    uint64_t count; // the number of records written to the log
    struct logrec recs[logsz];
};
typedef struct log log;

const nvm_type nvm_type_log = {
    {0x4f10c888407ca083, 0x18f575d6190b5674}, // type_usid
    "log", // name
    "Test log stuct", // tag
    sizeof(log), //size
    0, // xsize
    0, // version
    16, // align
    0, // upgrade
    0, // uptype
    3, // field_cnt
    { // field[]]
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_unsigned, 0, 1, (void*)64, "count"},
        {nvm_field_struct, 0, logsz, &nvm_type_logrec, "recs"},
        {0, 0, 0, 0, 0}
    }
};

/*
 * The following array of pointers to nvm_type structs is used to initialize
 * The USID map. Every nvm_type that contains a USID should be present in this
 * array.
 */
const nvm_type *test_usid_types[] = {
    &nvm_type_root,
    &nvm_type_branch,
    &nvm_type_branch_v1,
    &nvm_type_branch_v2,
    &nvm_type_log,
    NULL // NULL terminator
};

/* This is the context for each thread doing allocations */
struct counts
{
    int thread;       // owning thread number starting at 1
    nvm_region_stat logstat; // region stat for the log used by this thread
    size_t alloc;     // total bytes successfully allocated
    int alloc_cnt;    // number of successful allocations
    size_t fail;      // total bytes that failed allocation
    int fail_cnt;     // number of failed allocations
    size_t free;      // total bytes freed
    int free_cnt;     // number of calls to nvm_free
    jmp_buf env;      // jump buf to exit early
};

#endif
