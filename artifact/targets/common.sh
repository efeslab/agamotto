# Common defines
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
BUILD=$(realpath $DIR/../../build)
OUTDIR=$(realpath $DIR/../results)
KLEE=$BUILD/bin/klee

mkdir -p $OUTDIR

PMDK_DIR=$DIR/pmdk-prebuilt

CONSTRAINTS="--max-time=$((60 * 60)) --max-memory=10000"

# Common setup commands
ulimit -s unlimited
