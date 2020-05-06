#! /usr/bin/env python3

from argparse import ArgumentParser
from IPython import embed
from pathlib import Path

import pandas as pd

pd.set_option('display.max_rows', 500)

KNOWN_VM_USAGES = {
    'StackFrame0_Function': [
        'do_slabs_free', 'assoc_insert', 'do_item_link_q', 'do_slabs_alloc'],
    'StackFrame0': ['do_item_link at items.c:541'],
}

CORRECTNESS = 'write (unpersisted)'

DIAGNOSED = [
    'do_slabs_newslab_from_pmem at slabs.c:343',
    'do_item_alloc at items.c:374',
    'do_item_link at items.c:516',
    'do_item_link at items.c:520',
    'do_item_link at items.c:522',
    'drive_machine at memcached.c:5594',
]

def remove_known_volatile_usages(df):
    for stack_fn, fn_list in KNOWN_VM_USAGES.items():
        for fn_name in fn_list:
            df = df[df[stack_fn] != fn_name]
    return df

def remove_diagnosed(df): 
    to_remove = []
    for i, row in df.iterrows():
        for x in row:
            if x in DIAGNOSED:
                to_remove += [i]
    return df.drop(index=to_remove)

def main():
    parser = ArgumentParser()
    parser.add_argument('file_path', type=Path, help='CSV file to open')

    args = parser.parse_args()
    assert(args.file_path.exists())

    df = pd.read_csv(args.file_path)
    df = remove_known_volatile_usages(df)

    cdf = df[df['Type'] == CORRECTNESS]
    pdf = df[df['Type'] != CORRECTNESS]

    embed() 

if __name__ == '__main__':
    main()
