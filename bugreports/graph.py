#! /usr/bin/env python3

from argparse import ArgumentParser
from IPython import embed
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams['font.family']     = 'serif'
plt.rcParams['font.size']       = 8
plt.rcParams['axes.labelsize']  = 7
# plt.rcParams['text.usetex']     = True

def graph(dfs, searches, xlabel, ylabel, xlim):
    COLORS = {'static': 'green', 'covnew': 'blue', 'default': 'blue'}
    STYLES = {'static': '-', 'covnew': ':', 'default': ':'}

    ax = plt.gca()
    max_bugs = 0
    for df, search in zip(dfs, searches):
        # embed()
        try:
            df = df.drop_duplicates(subset='x')
            max_bugs = max(max_bugs, df['y'].max())
            pivoted = df.pivot(index='x', columns='label', values='y')
            pivoted.plot.line(ax=ax, linestyle=STYLES[search], color=COLORS[search])
        except:
            embed()
            raise
    step = 1
    if (max_bugs > 10):
        step = 2
    plt.yticks([0, max_bugs // 2, max_bugs])
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.xlim(0, xlim)
    plt.legend(loc='right')

def output(output_file):
    fig = plt.gcf()
    fig.set_size_inches(3.5, 1.75)
    fig.tight_layout()

    plt.savefig(output_file, dpi=300, bbox_inches='tight', pad_inches=0.02)
    plt.close()


def old_main():
    parser = ArgumentParser()
    parser.add_argument('--labels', type=str, nargs='+', 
                        help='Series labels which correspond to the files')
    parser.add_argument('--files', type=Path, nargs='+',
                        help='Files. Match labels')
    parser.add_argument('--x-axis', '-x', type=str, default='Timestamp',
                        help='What to use as X-axis for graph. Default is user time.')
    parser.add_argument('--x-limit', type=int, default=-1, help='max X value')
    parser.add_argument('--y-axis', '-y', type=str, default='UniqueBugsAtTime',
                        help='What to use as X-axis for graph. Default is bugs.')
    parser.add_argument('--output', '-o', type=str, default='out.pdf',
                        help='Output graph file name.')

    args = parser.parse_args()
    if len(args.labels) != len(args.files):
        raise Exception('Argument length mismatch!')

    print(args)

    df_list = []
    for l, f in zip(args.labels, args.files):
        assert(f.exists())
        df = pd.read_csv(f)
        xy = df[[args.x_axis, args.y_axis]]
        xy[args.x_axis] /= (1_000_000 * 60)# convert from microseconds to minutes
        if (args.y_axis == 'MallocUsage'):
            xy[args.y_axis] /= (1024 ** 2)

        xy['label'] = [l] * len(xy)
        xy = xy.rename(columns={args.x_axis: 'x', args.y_axis: 'y'})
        # insert a 0 point
        xy = xy.append({'x': 0, 'y': 0, 'label': l})
        df_list += [xy]
        # embed()
    
    ylabel = args.y_axis + " (MB)" if args.y_axis == 'MallocUsage' else args.y_axis
    graph(df_list, args.x_axis + " (Minutes)", ylabel, args.x_limit)
    output(args.output)

# SEARCHES = {'bfs': 'BFS', 
#             'covnew': 'KLEE Default', 
#             'default': 'KLEE Default', 
#             'dfs': 'DFS', 
#             'static': 'Agamotto'}



def get_dfs(system, xmax, root_dir=Path('./parsed')):
    import glob
    assert(root_dir.exists())
    df_list = []
    search_list = []

    SEARCHES = {'covnew': 'KLEE Default', 
            'default': 'KLEE Default', 
            'static': 'Aɢᴀᴍᴏᴛᴛᴏ'}
    
    # Ensure we either get default or covnew
    use_covnew = True
    # Puts 'static' first
    csv_files = sorted(glob.glob(str(root_dir / f'{system}_*.csv')), reverse=True)
    for csv_file in csv_files:
        if 'default' in csv_file:
            use_covnew = False
    
    if not use_covnew:
        SEARCHES.pop('covnew')

    for csv_file in csv_files:
        csv_path = Path(csv_file)
        csv_path.exists()
        search = csv_path.name.split('_')[-1].split('.')[0]
        if search not in SEARCHES:
            continue
        df = pd.read_csv(csv_path)
        xy = df[['Timestamp', 'UniqueBugsAtTime']]
        xy['Timestamp'] /= (1_000_000 * 60)# convert from microseconds to minutes
        xy['label'] = [SEARCHES[search]] * len(xy)
        xy = xy.rename(columns={'Timestamp': 'x', 'UniqueBugsAtTime': 'y'})
        # insert a 0 point
        xy = xy.append({'x': 0, 'y': 0, 'label': SEARCHES[search]}, ignore_index=True)
        # insert a max
        xy = xy.append({'x': xmax, 
                        'y': xy['y'][xy['x'] <= xmax].max(), 
                        'label': SEARCHES[search]}, ignore_index=True)
        df_list += [xy]
        search_list += [search]
    return df_list, search_list

def main():
    parser = ArgumentParser()
    parser.add_argument('system', type=str, help='What system to graph', 
                        choices=['pmdk', 'recipe', 'memcached', 'nvm-direct', 'redis'])
    parser.add_argument('--x-limit', '-x', type=int, help='Number of minutes',
                        default=60)
    args = parser.parse_args()
    dfs, searches = get_dfs(args.system, args.x_limit)
    assert(len(searches) == 2)

    graph(dfs, searches, 'Time (minutes)', 'Number of Unique Bugs', args.x_limit)
    output(f'{args.system}.pdf')

if __name__ == '__main__':
    main()