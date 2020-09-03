#! /usr/bin/env python3

from collections import defaultdict
from pathlib import Path
from pprint import pprint
from IPython import embed
from io import StringIO

import pandas as pd
import shlex
import subprocess
from subprocess import PIPE

CURDIR = Path(__file__).absolute().parent

def get_stat_df(klee_stats_bin, experiment_dir, category):
    assert isinstance(experiment_dir, Path), f'Error: wanted Path, got {type(experiment_dir)}!'
    assert isinstance(category, str), f'Error: wanted str, got {type(category)}!'

    args = shlex.split(f'{str(klee_stats_bin)} {str(experiment_dir)} --to-csv')

    proc = subprocess.run(args, stdout=PIPE, stderr=PIPE)
    assert proc.returncode == 0, f'klee stat failed!'

    csv_raw = proc.stdout.decode()

    df = pd.read_csv(StringIO(csv_raw))

    return df



def process(klee_stats_bin, experiment_root):
    # Map experiment -> category
    experiments = { 'klee-memcached-static': 'memcached-pm',
                    'klee-redis-static': 'redis-pmem',
                    'klee-nvm-direct-static': 'nvm-direct',
                    'klee-recipe-nothread-static': 'recipe',
                    'klee-pmdk-btree-static': 'pmdk',
                    'klee-pmdk-rbtree-static': 'pmdk',
                    'klee-pmdk-hashmap-atomic-static': 'pmdk' }
    
    res = defaultdict(float)

    for exp, cat in experiments.items():
        dirent = experiment_root / exp
        assert dirent.exists(), f'Cannot find experiment {str(dirent)}!'

        df = get_stat_df(klee_stats_bin, dirent, cat)

        res[cat] = max(res[cat], df['NvmOfflineTime'].max())

    s = pd.Series(res).rename('Offline Overhead (minutes)')
    # to minutes
    return s / (60.0 * 1e6)

def main():
    from argparse import ArgumentParser

    parser = ArgumentParser(description='Display the offline overhead of Aɢᴀᴍᴏᴛᴛᴏ in a nice tabular format')
    parser.add_argument('--bin-dir', default=(CURDIR.parent.parent / 'build' / 'bin'),
                        type=Path, help='Location of the bin/ directory of Aɢᴀᴍᴏᴛᴛᴏ\'s build directory.')
    parser.add_argument('--experiment-out-dir', default=CURDIR,
                        type=Path, help='Location of the experiment results')

    args = parser.parse_args()

    assert args.bin_dir.exists(), f'Bin dir {str(args.bin_dir)} does not exist!'

    klee_stats_bin = args.bin_dir / 'klee-stats'
    assert klee_stats_bin.exists(), f'Cannot find {str(klee_stats_bin)}!'
    assert args.experiment_out_dir.exists(), f'Cannot find {str(args.experiment_out_dir)}'

    res = process(klee_stats_bin, args.experiment_out_dir)

    print()
    pprint(res)
    print()

if __name__ == "__main__":
    main()