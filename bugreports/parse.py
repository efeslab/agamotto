#! /usr/bin/env python3

from argparse import ArgumentParser
from IPython import embed
from pathlib import Path

import pandas as pd

pd.set_option('display.max_rows', 500)

CORRECTNESS = 'write (unpersisted)'

FALSE_POS = {
    'false positive 1 [libpmemobj] (flags rebuilt for unlinked items)': [
        'do_slabs_free at slabs.c:540',
    ]
}

# Bug name => list of locations
DIAGNOSED_MEMCACHED = {
    'transient use 1 (item free list)': [
        'do_item_crawl_q at items.c:1880',
        'do_item_crawl_q at items.c:1883',
        'do_item_linktail_q at items.c:1808',
        'do_item_link_q at items.c:421',
        'do_item_link_q at items.c:423',
        'do_slabs_free at slabs.c:550',
        'do_item_crawl_q at items.c:1875',
        'do_slabs_free at slabs.c:548',
        'do_slabs_alloc at slabs.c:413',
        'do_item_crawl_q at items.c:1871',
        'do_item_crawl_q at items.c:1872',
        'do_item_unlink_q at items.c:470',
    ],
    'transient use 2 (refcount)': [
        'do_item_remove at items.c:591',
        'item_crawler_thread at crawler.c:404',
        'lru_pull_tail at items.c:1176',
        'do_item_link at items.c:541',
    ],
    'transient use 3 (assoc hash in item)': [
        'assoc_insert at assoc.c:167',
    ],
    'transient use 4 (slabs_clsid)': [
        'lru_pull_tail at items.c:1300',
    ],
    'universal performance 1 (item double flush)' : [
        'do_item_link at items.c:516',
    ],
    'universal correctness 1 (cas id)': [
        'do_item_link at items.c:538',
    ],
}

DIAGNOSED_PMDK = {
    # libpmemobj
    # -- found in data structure examples
    'universal performance 1 [libpmemobj] (page sized flushes in init)': [
        'pmemobj_create at obj.c:1423',
    ],
    'universal performance 2 [libpmemobj] (unnecessary drain palloc)': [
        'palloc_exec_actions at palloc.c:559',
    ],
    'universal performance 3 [libpmemobj] (ravl delete unnecessary flushes)': [
        'tx_pre_commit at tx.c:432',
    ],
    'universal performance 4 [libpmemobj] (flush on stats)': [
        'stats_delete at stats.c:138',
    ],
    # -- found in recipe
    'universal performance 5 [libpmemobj] (unnecessary fence)': [
        'pmemobj_tx_commit at tx.c:1001'
    ],
    'universal performance 6 [libpmemobj] (unnecessary flush)': [
        'memblock_run_init at memblock.c:1388'
    ],

    'transient use 1 [libpmemobj] (mutex in pmem)': [
        'obj_pool_cleanup at obj.c:1919',
        'pmemobj_mutex_lock at sync.c:201',
        'obj_runtime_init at obj.c:1201', # mutex head
        'obj_runtime_init at obj.c:1206',
    ],
    'transient use 2 [libpmemobj] (operation lanes)': [
        'lane_hold at lane.c:520',
        'lane_cleanup at lane.c:335',
        'lane_boot at lane.c:258'
    ],
    'transient use 3 [libpmemobj] (heap)': [
        'heap_cleanup at heap.c:1625',
        'heap_split_block at heap.c:964',
        'heap_split_block at heap.c:961',
        'heap_zone_init at heap.c:427',
        'heap_boot at heap.c:1500',
        'heap_boot at heap.c:1499',
        'memblock_huge_init at memblock.c:1317',
        'heap_boot at heap.c:1496'
    ],
    # -- found by RECIPE
    'transient use 4 [libpmemobj] (replicas)': [
        'obj_replica_init at obj.c:1133',
        'obj_replicas_init at obj.c:1628',
        'obj_replica_init_local at obj.c:993',
        'obj_replicas_init at obj.c:1626',
        'obj_replicas_init at obj.c:1627',
        'obj_replica_init_local at obj.c:996',
        'obj_replicas_init at obj.c:1624',
    ],
    'transient use 5 [libpmemobj] (pop meta)': [
        'obj_ctl_init_and_load at obj.c:157',
        'obj_runtime_init at obj.c:1189',
        'obj_runtime_init at obj.c:1181',
        'obj_runtime_init at obj.c:1185',
        'obj_runtime_init at obj.c:1179',
        'obj_open_common at obj.c:1737',
        'obj_runtime_init at obj.c:1236',
        'obj_runtime_init at obj.c:1193',
        'obj_runtime_init at obj.c:1244'
    ],
    'transient use 6 [libpmemobj] (fn pointers)': [
        'obj_replica_init at obj.c:1111',
        'obj_replica_init at obj.c:1110',
        'obj_replica_init_local at obj.c:1007'
    ],

    # Examples: hashmap
    'universal performance 1 [hashmap_atomic] (2 cache lines flushed when 1 would do)': [
        'create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:150',
    ],
    'universal performance 2 [hashmap_atomic] (nbuckets flushed after memset persist persists the same cacheline)': [
        'create_buckets at nvmbugs/hashmap_atomic/hashmap_atomic.c:120',
    ],
    # This bug means that the hashmap atomic isn't really, because it wants
    # to persist nbuckets after 
    # consequence of universal perf #2
    'semantic correctness 1 [hashmap_atomic] (nbuckets flushed with buckets)': [
        'create_buckets at nvmbugs/hashmap_atomic/hashmap_atomic.c:118',
    ],

    # Examples: rbtree
    'universal correctness 1 [rbtree] (dst not backed up)': [
        'tree_map_insert_bst at nvmbugs/003_pmdk_rbtree_map/rbtree_map_buggy.c:174'
    ],

    # Sys on PMDK: recipe
    'universal performance 1 [recipe] (unnecessary mfence)': [
        'mfence at src/clht_lb_res.c:139',
    ],
    'universal performance 2 [recipe] (unnecessary flush in tx)': [
        'ht_resize_pes at src/clht_lb_res.c:895'
    ],
    'universal performance 3 [recipe] (unnecessary flush in tx)': [
        'clht_create at src/clht_lb_res.c:321'
    ],
    'universal performance 4 [recipe] (unnecessary flush in tx)': [
        'clht_create at src/clht_lb_res.c:320'
    ],
    'universal performance 5 [recipe] (unnecessary flush put)': [
        'clht_put at src/clht_lb_res.c:556'
    ],
    'universal performance 6 [recipe] (unnecessary flush in tx)': [
        'clht_create at src/clht_lb_res.c:319'
    ],

    'universal correctness 1 [recipe] (lack of flush due to API confusion)': [
        'clht_put at src/clht_lb_res.c:583'
    ],

    'transient use 1 [recipe] (tmp table for resizing)': [
        'ht_resize_pes at src/clht_lb_res.c:843',
        'ht_resize_pes at src/clht_lb_res.c:973',
    ],
    'transient use 2 [recipe] (next off)': [
        'clht_put at src/clht_lb_res.c:557'
    ],
    'transient use 3 [recipe] (locks)': [
        'lock_acq_resize at include/clht_lb_res.h:352',
        'clht_put at src/clht_lb_res.c:592',
        'ht_resize_pes at src/clht_lb_res.c:974',
        'ht_status at src/clht_lb_res.c:1042' # status lock
    ], 
    'transient use 4 [recipe] (stats)': [
        'clht_bucket_create_stats at src/clht_lb_res.c:245'
    ],
    'transient use 5 [recipe] (versioning)': [
        'clht_gc_thread_init at src/clht_gc.c:58'
    ]
}

def remove_diagnosed(df, diagnosed): 
    dgn = []
    for _, bug_list in diagnosed.items():
        dgn += bug_list

    to_remove = []
    for i, row in df.iterrows():
        for x in row:
            if x in dgn:
                to_remove += [i]
    return df.drop(index=to_remove)

def main():
    parser = ArgumentParser()
    parser.add_argument('system', type=str, help='which system',
                        choices=['memcached', 'pmdk', 'recipe'])
    parser.add_argument('file_path', type=Path, help='CSV file to open')

    args = parser.parse_args()
    assert(args.file_path.exists())

    df = pd.read_csv(args.file_path)
    # df = remove_known_volatile_usages(df)
    if (args.system == 'memcached'):
        df = remove_diagnosed(df, DIAGNOSED_MEMCACHED)
    elif (args.system == 'pmdk' or args.system == 'recipe'):
        df = remove_diagnosed(df, DIAGNOSED_PMDK)

    cdf = df[df['Type'] == CORRECTNESS]
    pdf = df[df['Type'] != CORRECTNESS]

    embed() 

    print(f'Total diagnosed: {len(DIAGNOSED_MEMCACHED) + len(DIAGNOSED_PMDK)}')

if __name__ == '__main__':
    main()
