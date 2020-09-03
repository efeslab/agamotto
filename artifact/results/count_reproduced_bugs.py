#! /usr/bin/env python3

from argparse import ArgumentParser
from IPython import embed
from pathlib import Path

import pandas as pd

pd.set_option('display.max_rows', 500)
pd.set_option('display.max_colwidth', 0)

CURDIR = Path(__file__).absolute().parent

DIAGNOSED = {
    'klee-pmdk-btree-pmtest-repro': [],
    'klee-pmdk-hashmap-atomic-pmtest-repro': {
        'create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:132': 0,
        'create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:135': 0,
        'create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:137': 0,
        'create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:138': 0,
    },
    'klee-redis-xfd-repro': []
}

def count(csv_file, bugs):
    ''' Returns (# diagnosed, # undiagnosed) '''
    df = pd.read_csv(csv_file)
    bug_types = ['semantic correctness!', 'semantic performance!']
    bdf = df[df['Type'].isin(bug_types)]
    diag_rows = []
    undiag_rows = 0
    for _, row in bdf.iterrows():
        diagnosed = -1
        for idx, val in row.iteritems():
            if 'StackFrame' not in idx or '_' in idx:
                continue
            if val in bugs:
                diagnosed = True
                break

        if diagnosed >= 0:
            diag_rows += [diagnosed]
        else:
            undiag_rows += 1

    embed()
    return len(set(diag_rows)), undiag_rows

def count_reproduced(exp_root):
    for exp, bugs in DIAGNOSED.items():
        exp_dir = exp_root / exp
        assert exp_dir, f'Could not find {str(exp_dir)}!'
        csv_file = exp_dir / 'all_pmem_errs.csv'
        assert csv_file.exists(), 'Could not find bug report file!'
        ndiag, nundiag = count(csv_file, bugs)

def main():
    parser = ArgumentParser('Compute the number of reproduced bugs.')
    parser.add_argument('--experiment-out-dir', default=CURDIR,
                        type=Path, help='Location of the experiment results')
    args = parser.parse_args()

    assert args.experiment_out_dir.exists(), f'Could not find {str(args.experiment_out_dir)}!'

    count_reproduced(args.experiment_out_dir)

if __name__ == '__main__':
    main()
