#! /usr/bin/env python3

from argparse import ArgumentParser
from IPython import embed
from pathlib import Path

import pandas as pd

pd.set_option('display.max_rows', 500)

CURDIR = Path(__file__).absolute().parent

CORRECTNESS = 'write (unpersisted)'

FALSE_POS = {
    'benign bug 1 [libpmemobj] (flags rebuilt for unlinked items)': [
        'do_slabs_free at slabs.c:540',
    ],

    'test case (we introduced this bug)': [
        '__klee_posix_wrapped_main at nvmbugs/000_pmdk_btree_map/driver.c:74'
    ],

    'pwrite issues': [
        'pwrite at runtime/POSIX/./fd.c:426'
    ]
}

# Bug name => list of locations
OLD_DIAGNOSED_NVMDIRECT = {

    'universal correctness 1 (unflushed state)': [
        'nvm_txend at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:874',
    ],

    'universal correctness 2 (incomplete flush)': [
        'nvm_freelist_link at nvmbugs/005_nvm_direct/no_fc_lib/nvm_heap.c:2939'
    ],

    'universal correctness 3 (unflushed link for undo)': [
        'nvm_recover at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:5384',
    ],

    'universal performance 2 (extra flush in TX)': [
        'nvm_undo at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:1764',
    ],

    'universal performance 3 (extra flush/fence at txend)': [
        'nvm_txend at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:877'
    ],

    'universal performance 4 (unnecessary flush at commit)': [
        'nvm_commit at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:2755',
    ],

    'universal performance 5 (unnecessary region flush)': [
        'nvm_create_region at nvmbugs/005_nvm_direct/no_fc_lib/nvm_region.c:616'
    ],

    'universal performance 6 (unnecessary heap flush)': [
        # Seems they thought this would clear it from the CPU, but in pmdk could be hinted to stay
        'nvm_create_baseheap at nvmbugs/005_nvm_direct/no_fc_lib/nvm_heap.c:367'
    ],

    'universal performance 7 (unnecessary full block flush on txend)': [
        'nvm_free_callback at nvmbugs/005_nvm_direct/no_fc_lib/nvm_heap.c:1967'
    ],

    'transient use 1 (unflushed link)': [
        'nvm_txend at nvm_transaction.c:872',
        'nvm_txend at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:884',
        'nvm_txend at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:889'
    ],

    'transient use 2 (dead list)': [
        'nvm_recover at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:5351',
        'nvm_recover at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:5333',
    ],

    'transient use 3 (Transaction transients)': [
        'nvm_recover at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:5325',
        'nvm_recover at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:5301',
        'nvm_commit at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:2836'
    ],

}



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
        'do_item_unlinktail_q at items.c:1831',
        'do_item_crawl_q at items.c:1851',
        'do_item_unlinktail_q at items.c:1832',
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
        'create_buckets at nvmbugs/hashmap_atomic/hashmap_atomic.c:120',
        # 'create_buckets at nvmbugs/hashmap_atomic/hashmap_atomic.c:118',
    ],

    # Examples: rbtree
    'universal correctness 1 [rbtree] (dst not backed up)': [
        'tree_map_insert_bst at nvmbugs/003_pmdk_rbtree_map/rbtree_map_buggy.c:174'
    ],
}

COUNT_DOUBLE = [
    'create_buckets at nvmbugs/hashmap_atomic/hashmap_atomic.c:120'
]

DIAGNOSED_RECIPE = {
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

DIAGNOSED_REDIS = {
    'universal correctness 1 [redis] (non-fence MOVNT)': [
        'aofguard_init at src/aofguard.c:185',
        'flushAppendOnlyFile at aof.c:335',
    ],

    'universal correctness 2 [redis] (unflushed string)': [
        'sdsdupnvm at sds.c:212'
    ],

    'transient usage 1 (memkind allocator)': [
        'jemk_mallocx_check at src/memkind_arena.c:473'
    ],
}

DIAGNOSED_NVMDIRECT = {

    'universal correctness 1 (unpersisted heap data)': [
        'nvm_create_baseheap at nvmbugs/005_nvm_direct/original_lib/nvm_heap.c:360',
        'nvm_create_baseheap at nvmbugs/005_nvm_direct/original_lib/nvm_heap.c:350',
        'nvm_create_baseheap at nvmbugs/005_nvm_direct/original_lib/nvm_heap.c:359',
        'nvm_freelist_link at nvmbugs/005_nvm_direct/original_lib/nvm_heap.c:2936',
    ],

    'universal correctness 2 (unpersisted transaction data)': [
        'nvm_recover at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:5374',
        'nvm_txend at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:872',
    ],

    'universal correctness 3 (branch set)': [
        'branch_set at nvmbugs/005_nvm_direct/types.h:34',
    ] ,

    'universal performance 1 (unnecessary fence)': [
        'nvm_txend at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:873',
    ],

    'universal performance 2 (unnecessary flush)': [
        'nvm_add_oper at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:1430',
        'nvm_commit at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:2751',
        'nvm_txbegin at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:545',
        'nvm_add_oper at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:1429',
        'nvm_txbegin at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:545',
        'nvm_create_baseheap at nvmbugs/005_nvm_direct/original_lib/nvm_heap.c:367',
    ],

    'perf?': [
        'nvm_create_trans_table at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:4649',
        'nvm_create_heap at nvmbugs/005_nvm_direct/original_lib/nvm_heap.c:741',
        'nvm_oncommit at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:2243'
    ],

    'transient use 1 (unflushed link)': [
        'nvm_txend at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:884',
        'nvm_txend at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:885',
        'nvm_txend at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:880',
    ],

    'transient?': [
        'nvm_recover at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:5323',
        'nvm_recover at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:5341',
        'nvm_recover at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:5294',
        'nvm_recover at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:5315',        
        'nvm_commit at nvmbugs/005_nvm_direct/original_lib/nvm_transaction.c:2832',

        'nvm_commit at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:2836',
        'nvm_recover at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:5325',
        'nvm_recover at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:5351',
        'nvm_recover at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:5301',
        'nvm_recover at nvmbugs/005_nvm_direct/no_fc_lib/nvm_transaction.c:5333',

    ]
}

def remove_diagnosed(df, diagnosed): 
    dgn = []
    for _, bug_list in diagnosed.items():
        dgn += bug_list

    to_remove = []
    to_keep = []
    for i, row in df.iterrows():
        for x in row:
            if x in dgn:
                to_remove += [i]
            else:
                to_keep += [i]

    return df.drop(index=to_remove), df.drop(index=to_keep)

def get_diagnosed(series, diagnosed):
    for bug, loc_list in diagnosed.items():
        for x in series:
            if x in loc_list:
                return x
    
    raise Exception('Not yet diagnosed!')

def uniquify(df, diagnosed):
    unique_bugs = {}
    for k, v in diagnosed.items():
        diagnosis = 'performance'
        if 'correctness' in k:
            diagnosis = 'correctness'
        if 'transient' in k:
            diagnosis = 'transient'

        for x in v:
            unique_bugs[x] = diagnosis

    sdf = df.sort_values('Timestamp').reset_index().drop('index', axis=1)
    uniqueNum = 0
    data = []
    bugs = []
    bug_types = []
    last_diagnosis = None
    for i in range(0, len(sdf)):
        bug = get_diagnosed(sdf.loc[i], diagnosed)
        if bug in unique_bugs:
            uniqueNum += 1
            last_diagnosis = unique_bugs[bug]
            unique_bugs.pop(bug)
        data += [uniqueNum]
        bugs += [bug]
        bug_types += [last_diagnosis]
    sdf['UniqueBugsAtTime'] = data
    sdf['BugDiagnosis'] = bugs
    sdf['BugType'] = bug_types
    # embed()

    return sdf


SYSTEMS = ['pmdk', 'recipe', 'memcached', 'nvm-direct', 'redis']
FILTER_PMDK = ['recipe']
SEARCHES = ['bfs', 'covnew', 'default', 'dfs', 'static']
DIAGNOSED_MAPPING = {
    'memcached': DIAGNOSED_MEMCACHED,
    'pmdk': DIAGNOSED_PMDK,
    'recipe': DIAGNOSED_RECIPE,
    'nvm-direct': DIAGNOSED_NVMDIRECT,
    'redis': DIAGNOSED_REDIS
}

def convert_all(root_dir, out_dir):
    from collections import defaultdict
    import glob
    source_name = 'all_pmem_errs.csv'

    klee_out_paths = str(root_dir / 'klee-*')

    if not out_dir.exists():
        out_dir.mkdir()

    # system => search => [data points]
    df_dict = defaultdict(lambda: defaultdict(list))

    for exp_dir_str in glob.glob(klee_out_paths):
        exp_dir = Path(exp_dir_str)
        assert(exp_dir.is_dir())
        csv_file = exp_dir / source_name
        if not csv_file.exists():
            continue

        parts = exp_dir.name.split('-')

        system = parts[1]
        if system == 'nvm':
            system = f'{system}-{parts[2]}'
            print(exp_dir_str, system)
        search = parts[-1]
        if 'repro' in search or 'debug' in search:
            continue

        if system not in SYSTEMS:
            raise Exception(f'{system} not in SYSTEMS!')
        if search not in SEARCHES:
            raise Exception(f'{search} not in SEARCHES!')

        df = pd.read_csv(csv_file)
        df, _ = remove_diagnosed(df, FALSE_POS)

        if system in FILTER_PMDK:
            df, xdf = remove_diagnosed(df, DIAGNOSED_PMDK)
            df_dict['pmdk'][search] += [xdf]
            # xdf = df - fdf
            # embed()
        
        df_dict[system][search] += [df]
    
    # For the summary stuff (system => [dfs])
    summary_dict = defaultdict(list)

    for system, search_dict in df_dict.items():
        for search, df_list in search_dict.items():
            combined_df = pd.concat(df_list).reset_index(drop=True)
            if system == 'nvm-direct':
                df, _ = remove_diagnosed(combined_df, DIAGNOSED_MAPPING[system])
                # embed()
            df = uniquify(combined_df, DIAGNOSED_MAPPING[system])
            out_file = out_dir / f'{system}_{search}.csv'
            df.to_csv(out_file)
            assert(out_file.exists())

            summary_dict[system] += [df]
    
    # Do the summarizing
    summary_info = defaultdict(dict)
    total = 0
    correctness = 0
    performance = 0
    transient = 0
    summary_dfs = []
    for system, df_list in summary_dict.items():
        df = pd.concat(df_list)
        df = uniquify(df, DIAGNOSED_MAPPING[system])

        summary_info[system]['Total'] = df['UniqueBugsAtTime'].max()
        gdf = df.drop_duplicates(subset='BugDiagnosis', keep='first')
        gdf['System'] = [system] * len(gdf)
        summary_info[system]['Correctness'] = len(gdf[gdf['BugType'] == 'correctness'])
        summary_info[system]['Performance'] = len(gdf[gdf['BugType'] != 'correctness'])
        # embed()
        assert(summary_info[system]['Total'] == \
               summary_info[system]['Correctness'] + summary_info[system]['Performance'])
        total += summary_info[system]['Total']
        correctness += summary_info[system]['Correctness']
        performance += len(gdf[gdf['BugType'] == 'performance'])
        transient += len(gdf[gdf['BugType'] == 'transient'])
        # embed()
        summary_dfs += [gdf[['System', 'BugType', 'BugDiagnosis']]]
    
    sdf = pd.concat(summary_dfs).sort_values(by=['System', 'BugType'])
    sdf.to_csv('summary.csv', index=False)

    from pprint import pprint
    pprint(dict(summary_info), indent=4)
    print(f'\nOverall:\n\tTotal: {total}\n\tCorrectness: {correctness}\n\tPerformance: {performance}')
    print(f'\tTransient: {transient}')
    

def main():
    parser = ArgumentParser('Parse all of the bug reports in a given directory.')
    parser.add_argument('--experiment_output_dir', type=Path, 
                        help='The location of the experiment outputs.',
                        default=CURDIR)
    parser.add_argument('--outdir', type=Path, help='Where to output parsed results',
                        default=(CURDIR / 'parsed'))
    args = parser.parse_args()
    convert_all(args.experiment_output_dir, args.outdir)

if __name__ == '__main__':
    main()
