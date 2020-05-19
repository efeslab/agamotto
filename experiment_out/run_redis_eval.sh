#! /bin/bash
set +x

OUTDIR=$(realpath .)

cp -r redis2 ../build/bin
cd ../build/bin/redis2
ulimit -s unlimited

# 1. Agamotto
./run.sh build/redis-server.bc "--search=nvm --nvm-heuristic-type=static" "$OUTPUT/klee-redis-static" &

# 2. Default (random-path + covnew)
./run.sh build/redis-server.bc "" "$OUTDIR/klee-redis-default" &

# 3. BFS
./run.sh build/redis-server.bc "--search=bfs" "$OUTPUT/klee-redis-bfs" &

# 4. DFS
./run.sh build/redis-server.bc "--search=dfs" "$OUTPUT/klee-redis-dfs" &

# 5. Coverage new 
./run.sh build/redis-server.bc "--search=nurs:covnew" "$OUTPUT/klee-redis-covnew" &