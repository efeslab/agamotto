# Testing Example

Here, I'll try to describe how set up a new system to test with Agamotto.

## Target Selection

The [`Level_Hashing`](./Level_Hashing/) directory is based on the Level Hashing benchmark from WITCHER's artifact ([`witcher/benchmark/Level_Hashing`](https://github.com/cosmoss-jigu/witcher/tree/sosp21-ae/benchmark/Level_Hashing)). Their setup actually refers to a third party code directory ([`witcher/third_party/Level_Hashing/persistent_level_hashing`](https://github.com/cosmoss-jigu/witcher/tree/sosp21-ae/third_party/Level_Hashing/persistent_level_hashing)), so for convenience I have consolidated these two directories together into the single `./Level_hashing` directory in this repo.

WITCHER's artifact has a number of other benchmarks not originally tested in our paper; they could also make good targets for porting to test on Agamotto.

## Compilation Setup

See [`CMakeLists.txt`](./CMakeLists.txt) for how to set up compilation.

I also had to make the following modifications to the source code so it would compile and run on KLEE:

- I removed all calls to WITCHER's API (i.e., everything that derives from ` #include "witcherAnnotation.h"`).
- I removed certain inline assembly instructions (example: [`pflush.c:76`](Level_Hashing/lib/pflush.c#L77)). If you encounter unsupported inline assembly instructions, KLEE will give the following error:

```
KLEE: ERROR: nvmbugs/testing_example/Level_Hashing/lib/pflush.c:76: inline assembly is unsupported
```

A lot of this process is also reading/playing with the executable to see how to
run the application. For example, I read the main function of [`main.c`](Level_Hashing/main/main.c#L180) to understand how to run the application (e.g., what arguments to provide and what they mean).

I also have to read the original Makefiles/build system to find what libraries the
application may depend on when I add a new application to Agamotto's build system (this is how I derive the commands in the CMakeLists.txt file).

When adding something new to the build system, you need to refresh the CMake files under the build directory:

```sh
cd build
cmake ..
```

Note that since CMake caches previous values of options set (see `build/CMakeCache.txt`), you can just run `cmake ..` if you have previously run `cmake` and you will preserve the same configuration.

## Running Level Hashing

Normally, you'd run this test using a command like this:

```sh
$ cd build
# ./bin/ExLevelHashing <path to PM file> <size of PM file> <name of layout> <path to operations file> <start operation> <operation to skip> <output file path>
$ ./bin/ExLevelHashing lhl.pmem 32 lhl ../nvmbugs/testing_example/Level_Hashing/random/op_file_small.txt 0 -1 output.txt

Creating a new level hash.
Level hashing: ASSOC_NUM 4, KEY_LEN 16, VALUE_LEN 15 
The number of top-level buckets: 4
The number of all buckets: 6
The number of all entries: 24
The level hash table initialization succeeds!
```

However, we want to run this under KLEE.

This command runs a single concrete execution of the Level Hashing driver program in KLEE.
```sh
cd build

# Environment variables defined in $REPO_DIR/build.env
./bin/klee \
  --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --write-errors-only=true \
  --max-time=$((60 * 60)) --max-memory=10000 \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_PREBUILT_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_PREBUILT_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_PREBUILT_DIR/pmdk.env \
  ./bin/ExLevelHashing.bc \
    --sym-pmem-delay PMEM 33554432 PMEM \
    32 \
    level_hashing_layout \
    $REPO_DIR/nvmbugs/testing_example/Level_Hashing/random/op_file_small.txt \
    0 \
    -1 \
    output.txt
```

This will output the results under `build/klee-last`, which symlinks to the last
KLEE results directory.

Here's a breakdown of what the arguments mean:

- `--search=nvm --nvm-heuristic-type=static` - Use Agamotto's search heuristic.
- `--custom-checkers=false` - Disable the custom checkers (described in our OSDI paper).
- `--write-errors-only=true`
- `--max-time=$((60 * 60)) --max-memory=10000` - Run for 3600 seconds (1 hour), using 10,000 MB of RAM at most.
- `--disable-verify=true --libc=uclibc` -
- `--link-llvm-lib=$PMDK_PREBUILT_DIR/libpmemobj.so.bc --link-llvm-lib=$PMDK_PREBUILT_DIR/libpmem.so.bc` - KLEE will dynamically link with
the PMDK bitcode libraries, which is required for symbolic execution of applications that use PMDK.
- `--env-file=$PMDK_PREBUILT_DIR/pmdk.env` - Sets the environment variables in the symbolic environment (as described in the file).
- `--posix-runtime` - Use the POSIX runtime in KLEE (https://klee.github.io/tutorials/using-symbolic/).
Needed for some PMDK-specific configurations (e.g., `PMEM_IS_PMEM_FORCE=1` makes PMDK assume all PM files are PM without explicitly checking).
- `./bin/ExLevelHashing.bc` - argv[0]; the path to the executable (in bitcode form). **When using POSIX runtime, all arguments after this are passed into the POSIX runtime instead of KLEE directly.**
- `--sym-pmem-delay PMEM 33554432` - [POSIX Runtime argument] This informs KLEE that the application will create a PM file (named `PMEM`) with a size of 32MiB (33554432 bytes).
- `PMEM` - argv[1]; PMEM is the symbolic PM file
- `32` - argv[2]; the size of the PM file (needs to match the number of MiB from the `--sym-pmem-delay`)
- `level_hashing_layout` - argv[3]; the name of the layout of the PM pool (this can be any name, really).
- `$REPO_DIR/nvmbugs/testing_example/Level_Hashing/random/op_file_small.txt` - argv[4]; a file containing a list of commands to run in Level Hashing (i.e., add key-value pair, remove key-value pair, etc.)
- `0` - argv[5]; what operation in the file to start at (0 means start from beginning)
- `-1` - argv[6]; what operation in the file to skip (-1 means skip no operations)
- `output.txt` - argv[7]; where this program writes it's output

### Running Multiple Paths

However, we might want to explore more than a single concrete path. We can therefore try to provide symbolic arguments in place of some of the `argv` arguments listed above to explore symbolic executions.

`$REPO_DIR/nvmbugs/testing_example/Level_Hashing/random/op_file.txt` contains 2000
random operations for testing. We can try symbolically executing different combinations of these operations with the following:

```sh
./bin/klee \
  --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --write-errors-only=true \
  --max-time=$((60 * 60)) --max-memory=10000 \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_PREBUILT_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_PREBUILT_DIR/libpmem.so.bc \
  --posix-runtime --env-file=$PMDK_PREBUILT_DIR/pmdk.env \
  ./bin/ExLevelHashing.bc \
    --sym-pmem-delay PMEM 33554432 PMEM \
    32 \
    level_hashing_layout \
    $REPO_DIR/nvmbugs/testing_example/Level_Hashing/random/op_file.txt \
    -sym-arg 4 \
    -sym-arg 4 \
    output.txt
```

We could also test symbolic operation parsing (likely to be very slow, not recommended) using the following (see [using symbolic files in KLEE](https://klee.github.io/tutorials/using-symbolic/#-sym-files-usage) for more details):

```sh
./bin/klee \
  --search=nvm --nvm-heuristic-type=static \
  --custom-checkers=false --write-errors-only=true \
  --max-time=$((60 * 60)) --max-memory=10000 \
  --disable-verify=true --libc=uclibc \
  --link-llvm-lib=$PMDK_PREBUILT_DIR/libpmemobj.so.bc \
  --link-llvm-lib=$PMDK_PREBUILT_DIR/libpmem.so.bc \
  --env-file=$PMDK_PREBUILT_DIR/pmdk.env \
  --posix-runtime \
  ./bin/ExLevelHashing.bc \
    --sym-files 1 1000 \
    --sym-pmem-delay PMEM 33554432 PMEM \
    32 \
    level_hashing_layout \
    A \
    0 \
    -1 \
    output.txt
```

This version provides a symbolic file, `A`, which is 1000 bytes long. The file
is purely symbolic, so when the application reads the file and starts parsing
the contents, it will fork the state conditional on file contents, as every byte
in the file is effectively "any value."
