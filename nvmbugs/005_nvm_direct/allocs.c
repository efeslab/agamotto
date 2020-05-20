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

/* the number of pointers in the application managed extent of pointers */
#define ptrs 16
#include "types.h"

/* The virtual address to attach the main region at */
//#define attch_int (1024 * 1024 * 1024 * 128LL) //fixed mapping
#define attch_int (0) // Let OS choose
#define attch ((uint8_t*)attch_int)

// void *ext = nvm_add_extent(base + xoff, ptrspace);
/* The virtual address space consumed by the main region */
const size_t vspace = 8 * 1024 * 1024;

/* the physical size of the base extent */
const size_t pspace = 2 * 1024 * 1024;

/* the physical size of the application managed extent of pointers */
const size_t ptrspace = 128 * 1024;

/* the offset into the region for the application managed extent of pointers */
const size_t xoff = 4 * 1024 * 1024;

// offset for the heap managed
const size_t xoff2 = 5 * 1024 * 1024;

/* last stat from the main region */
nvm_region_stat tstat;
static void report(nvm_desc desc, const char *title);
static void report_heap(nvm_heap *heap);
void nvm_heap_test(nvm_desc desc, nvm_heap *heap);
/*
 * This is the main driver of the test
 */
int
main(int argc, char** argv)
{
    const char *fname = "/tmp/nvmd";
    if (argc != 3) {
      printf("usage: %s <fname> <allocs>\n", argv[0]);
      exit(1);
    }
    fname = argv[1];
    int N = atoi(argv[2]);
    if (N < 0 || N > 10000) {
      printf("allocs must be in range [0, 10000]\n");
      exit(1);
    }

    // printf("NVM Device: \'%s\', allocs: %d\n", fname, N);
    /* We get here if we are the child process or there are no children.
     * The first thing is to initialize the NVM library for the main thread */
    nvm_thread_init();
    nvm_usid_register_types(test_usid_types);

    /* If attaching the region does not work then create a new one. This will
     * run recovery if needed. */
    printf("Driver: creating region\n");
    /* nvm_desc desc = nvm_attach_region(0, fname, attch); */
    nvm_desc desc = nvm_create_region(0, fname, "TestRegion needs 32 characters at least for 2 cachelines",
                    attch, vspace, pspace, 0777);

    /* Get the root heap and base extent */
    int success = nvm_query_region(desc, &tstat);
    nvm_heap *rh = tstat.rootheap;
    nvm_heap_test(desc, rh);
    uint8_t *base = tstat.base;

    /* Allocate a root struct in a transaction */
    nvm_txbegin(desc);
    root *rs = nvm_alloc(rh, shapeof(root), 1);
    int m;
    for (m = 0; m < ptrs; m++)// initialize the mutexes
      nvm_mutex_init(&rs->mtx[m], 10);
    nvm_commit();
    nvm_txend();

    nvm_set_root_object(desc, rs);
    branch_srp *ptr;
    printf("Set new root object and everything\n");

    printf("Attempting to add an application managed extent\n");
    nvm_extent_stat xstat[4];
    nvm_query_extents(desc, 1, 4, xstat);
    if (xstat[0].psize == 0)
    {
      nvm_txbegin(desc);
      void *ext = nvm_add_extent(base + xoff, ptrspace);
      if (ext == NULL)
      {
        perror("Add extent failed");
        exit(1);
      }

      /* The extent will hold the array of pointers to allocated space.
       * They need to be initialized to be null pointers */
      nvm_undo(&rs->ptr, sizeof(rs->ptr));
      branchArray_set(&rs->ptr, ext);
      int i;
      ptr = ext;
      for (i = 0; i < ptrs; i++)
          branch_set(&ptr[i], NULL);
      nvm_flush(ptr, sizeof(ptr[0]) * ptrs);
      nvm_commit();
      nvm_txend();
      printf("Added an app managed extent\n");
    }

    printf("Attempting to add a heap managed extent\n");
    if (xstat[1].psize == 0)
    {
      nvm_txbegin(desc);
      nvm_heap *heap = nvm_create_heap(desc, base + xoff2, ptrspace, "heap extent");
      nvm_heap_txset(&rs->heap, heap);
      if (heap == NULL)
      {
        perror("Add heap failed");
        exit(1);
      }
      nvm_heap_setroot(heap, heap);
      nvm_txend();
      printf("Added a heap extent\n");
    }
    /* report txconfig */
    nvm_txconfig cfg;
    nvm_get_txconfig(desc, &cfg);
    printf("maxtx:%d undo:%d limit:%d\n", cfg.txn_slots, cfg.undo_blocks,
            cfg.undo_limit);
    cfg.txn_slots = 80;
    cfg.undo_blocks = 700;
    cfg.undo_limit = 16;
    nvm_set_txconfig(desc, &cfg);
    nvm_get_txconfig(desc, &cfg);
    printf("maxtx:%d undo:%d limit:%d\n", cfg.txn_slots, cfg.undo_blocks,
            cfg.undo_limit);
    cfg.txn_slots = 31;
    cfg.undo_blocks = 250;
    cfg.undo_limit = 8;
    nvm_set_txconfig(desc, &cfg);
    nvm_get_txconfig(desc, &cfg);
    printf("maxtx:%d undo:%d limit:%d\n", cfg.txn_slots, cfg.undo_blocks,
            cfg.undo_limit);

    nvm_heap_test(desc, nvm_heap_get(&rs->heap));
    report(desc, "Immediately after creation");

    /* grow the pointer extent just for fun */
    printf("Growing pointer extent\n");
    nvm_txbegin(desc);
    nvm_resize_extent(ptr, ptrspace * 2);
    nvm_commit();
    nvm_txend();

    printf("Growing heap extent\n");

    /* Grow the heap just for fun */
    nvm_heap *heap = nvm_heap_get(&rs->heap);
    if (!nvm_resize_heap(heap, ptrspace * 2))
    {
      perror("Grow of heap failed");
      exit(1);
    }
    nvm_heap_test(desc, heap);

    printf("Done growing extents\n");

    /* determine how much is allocated before we start the test */
    int i;
    size_t init_consumed = heap->consumed;
    int init_cnt = 0;
    for (i = 0; i < ptrs; i++)
    {
        branch *p = branch_get(&ptr[i]);
        if (p)
        {
            nvm_blk *nvb = ((nvm_blk*)p) - 1;
            nvm_verify(nvb, shapeof(nvm_blk));
            init_consumed -=
                    sizeof(nvm_blk) *
                    (nvm_blk_get(&nvb->neighbors.fwrd) - nvb);
            init_cnt++;
        }
    }

    struct counts cnts;
    memset(&cnts, 0, sizeof(cnts));


    /* shrink the pointer extent back just for fun */
    nvm_txbegin(desc);
    nvm_resize_extent(ptr, ptrspace);
    nvm_commit();
    nvm_txend();

    /* Shrink the extent back to its original size. First we free the highest
     * allocation so the we do not get caught with too small a last block. */
    branch *hp = NULL;
    int hi = 0;
    for (i = 0; i < ptrs; i++)
    {
        branch *p = branch_get(&ptr[i]);
        if (p > hp)
        {
            hp = p;
            hi = i;
        }
    }
    if (hp)
    {
        nvm_txbegin(desc);
        nvm_free(hp);
        nvm_undo(&ptr[hi], sizeof(ptr[0]));
        branch_set(&ptr[hi], 0);
        nvm_flush1(&ptr[hi]);
        nvm_commit();
        nvm_txend();

    }
    if (!nvm_resize_heap(heap, ptrspace))
    {
        perror("Shrink of heap failed");
        exit(1);
    }

    report(desc, "Begin Run");
  srand(69); 
    for (int i = 0; i < N; i++) {
      int slot = rand() % ptrs;
      size_t size = 1 + rand() % 16;
      nvm_txbegin(desc);
      nvm_xlock(&rs->mtx[slot]);

      branch_srp *ptr = branchArray_get(&rs->ptr);
      branch *oldp = branch_get(&ptr[slot]);
      nvm_verify(oldp, shapeof(branch));
      if (oldp)
      {
        if (oldp->version != 3)
          nvms_assert_fail("bad version - not upgraded");
        nvm_free(oldp);
        branch_txset(&ptr[slot], 0);
      }
      const nvm_type *shape = shapeof(branch);
      int v = 3;
      switch (size % 16)
      {
        case 1:
          shape = shapeof(branch_v1);
          printf("\tdouble upgrade will be needed\n");
          v=1;
          break;
        case 2:
          shape = shapeof(branch_v2);
          printf("\tsingle upgrade will be needed\n");
          v=2;
          break;
        default:
          break;
      }
      branch *leaf = nvm_alloc(heap, shape, size);
      if (leaf)
      {
        size_t newsz = size * sizeof(uint64_t) + sizeof(branch);
        if (v==3)
          NVM_NTSTORE(leaf->version, v);
        NVM_NTSTORE(leaf->data[0], newsz);
        branch_txset(&ptr[slot], leaf);
      }
      nvm_txend();

    }

    printf("Cleaning up by deleting heap and app extent\n");

    //delete heap and app extent before return
    nvm_query_extents(desc, 0, 4, xstat);
    nvm_txbegin(desc);
    nvm_delete_heap(heap);
    nvm_heap_txset(&rs->heap, 0);
    nvm_remove_extent(xstat[1].extent);
    nvm_commit();
    nvm_txend();

    printf("Driver: detaching region\n");
    nvm_detach_region(desc);
    printf("Driver: finishing thread\n");
    nvm_thread_fini();
    printf("Driver: exiting now, success\n");
}

/*
 * Report a region, all the extents and any heaps they contain
 */
void report(nvm_desc desc, const char *title)
{
    nvm_region_stat rstat;
    nvm_extent_stat xstat[64];

    if (title)
        printf("\n%s\n", title);

    /* Region report */
    nvm_query_region(desc, &rstat);
    printf("Region[%d]:\"%s\" addr:%p, vsize:0x%lx, extents:%d\n", desc,
            rstat.name, rstat.base, rstat.vsize, rstat.extent_cnt);

    /* extents report */
    nvm_query_extents(desc, 0, 64, xstat);
    int x;
    for (x = 0; x <= rstat.extent_cnt; x++)
    {
        printf("  Extent[%d] addr:%p, psize:0x%lx, heap:%p\n", x,
                xstat[x].extent, xstat[x].psize, xstat[x].heap);
        if (xstat[x].heap)
            report_heap(xstat[x].heap);
    }
    printf("\n");
}
/*
 * Report the query results for a heap
 */
void report_heap(nvm_heap *heap)
{
    nvm_heap_stat hstat;
    nvm_query_heap(heap, &hstat);

    /* Report statistics */
    printf("Heap:\"%s\" Consumed:0x%lx Free:0x%lx inuse:%d\n", hstat.name,
            hstat.consumed, hstat.free, hstat.inuse);
}
