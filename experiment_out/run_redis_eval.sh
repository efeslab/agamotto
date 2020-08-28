#! /bin/bash
set +x

OUTDIR=$(realpath .)

cp -r redis2 ../build/bin
rm -rf klee-redis-static
cd ../build/bin/redis2
ulimit -s unlimited

# 1. Agamotto
./run.sh build/redis-server.bc "--search=nvm --nvm-heuristic-type=static" "$OUTDIR/klee-redis-static"

# 2. Default (random-path + covnew)
# ./run.sh build/redis-server.bc "" "$OUTDIR/klee-redis-default" &

# 3. BFS
# ./run.sh build/redis-server.bc "--search=bfs" "$OUTDIR/klee-redis-bfs" &

# 4. DFS
# ./run.sh build/redis-server.bc "--search=dfs" "$OUTDIR/klee-redis-dfs" &

# 5. Coverage new 
# ./run.sh build/redis-server.bc "--search=nurs:covnew" "$OUTDIR/klee-redis-covnew" &
