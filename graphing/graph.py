#! /usr/bin/env python3

from argparse import ArgumentParser
from IPython import embed
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt

def graph(dfs, xlabel, ylabel):
  ax = plt.gca()
  for df in dfs:
    try:
      df = df.drop_duplicates(subset='x')
      pivoted = df.pivot(index='x', columns='label', values='y')
      pivoted.plot.line(ax=ax)
    except:
      embed()
      raise
  plt.xlabel(xlabel)
  plt.ylabel(ylabel)
  plt.legend(loc='best')

def output(output_file):
  fig = plt.gcf()
  fig.set_size_inches(3.5, 3.0)
  fig.tight_layout()

  plt.savefig(output_file, dpi=300, bbox_inches='tight', pad_inches=0.02)
  plt.close()


def main():
  parser = ArgumentParser()
  parser.add_argument('--labels', type=str, nargs='+', 
                      help='Series labels which correspond to the files')
  parser.add_argument('--files', type=Path, nargs='+',
                      help='Files. Match labels')
  parser.add_argument('--x-axis', '-x', type=str, default='UserTime',
                      help='What to use as X-axis for graph. Default is user time.')
  parser.add_argument('--y-axis', '-y', type=str, default='NvmBugsTotalUniq',
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
    df_list += [xy]
    embed()
  
  ylabel = args.y_axis + " (MB)" if args.y_axis == 'MallocUsage' else args.y_axis
  graph(df_list, args.x_axis + " (Minutes)", ylabel)
  output(args.output)


if __name__ == '__main__':
  main()