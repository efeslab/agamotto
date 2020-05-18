#! /usr/bin/env python3

from argparse import ArgumentParser
from IPython import embed
from pathlib import Path

import pandas as pd

pd.set_option('display.max_rows', 500)

CORRECTNESS = 'write (unpersisted)'

FALSE_POS = {
    'false positive 1 (flags rebuilt for unlinked items)': [
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
    'universal performance 1 (page sized flushes in init)': [
        'pmemobj_create at obj.c:1423',
    ],
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
                        choices=['memcached', 'pmdk'])
    parser.add_argument('file_path', type=Path, help='CSV file to open')

    args = parser.parse_args()
    assert(args.file_path.exists())

    df = pd.read_csv(args.file_path)
    # df = remove_known_volatile_usages(df)
    if (args.system == 'memcached'):
        df = remove_diagnosed(df, DIAGNOSED_MEMCACHED)
    elif (args.system == 'pmdk'):
        df = remove_diagnosed(df, DIAGNOSED_PMDK)

    # cdf = df[df['Type'] == CORRECTNESS]
    # pdf = df[df['Type'] != CORRECTNESS]

    embed() 

if __name__ == '__main__':
    main()
