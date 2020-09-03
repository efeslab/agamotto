#! /usr/bin/env python3

from argparse import ArgumentParser
from IPython import embed
from pathlib import Path

import pandas as pd

pd.set_option('display.max_rows', 500)
pd.set_option('display.max_colwidth', 0)

CURDIR = Path(__file__).absolute().parent

DIAGNOSED = {
    'klee-pmdk-btree-pmtest-repro': {
        'btree_map_insert_item at nvmbugs/000_pmdk_btree_map/btree_map_buggy.c:267': 0,
        'btree_map_insert_empty at nvmbugs/000_pmdk_btree_map/btree_map_buggy.c:150': 1
    },
    'klee-pmdk-hashmap-atomic-pmtest-repro': {
        'create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:132': 0,
        'create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:135': 0,
        'create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:137': 0,
        'create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:138': 0,
        'hm_atomic_insert at nvmbugs/hashmap_atomic/hashmap_atomic.c:263': 1,
    },
    'klee-redis-xfd-repro': {
        'initPersistentMemory at server.c:4029': 0
    }
}

def count(csv_file, bugs):
    ''' Returns (# diagnosed, # undiagnosed) '''
    df = pd.read_csv(csv_file)
    # bug_types = ['semantic correctness!', 'semantic performance!']
    # bdf = df[df['Type'].isin(bug_types)]
    diag_rows = []
    for _, row in df.iterrows():
        diagnosed = -1
        for idx, val in row.iteritems():
            if 'StackFrame' not in idx or '_' in idx:
                continue
            if val in bugs:
                print(f'\t{val}')
                diagnosed = bugs[val]
                break

        if diagnosed >= 0:
            diag_rows += [diagnosed]

    return len(set(diag_rows))

def count_reproduced(exp_root):
    ndiag = 0
    for exp, bugs in DIAGNOSED.items():
        exp_dir = exp_root / exp
        assert exp_dir, f'Could not find {str(exp_dir)}!'
        csv_file = exp_dir / 'all_pmem_errs.csv'
        assert csv_file.exists(), 'Could not find bug report file!'
        ndiag += count(csv_file, bugs)
    
    print(f'Number of unique reproduced bugs: {ndiag}')

def main():
    parser = ArgumentParser('Compute the number of reproduced bugs.')
    parser.add_argument('--experiment-out-dir', default=CURDIR,
                        type=Path, help='Location of the experiment results')
    args = parser.parse_args()

    assert args.experiment_out_dir.exists(), f'Could not find {str(args.experiment_out_dir)}!'

    count_reproduced(args.experiment_out_dir)

if __name__ == '__main__':
    main()
