# <span style="font-variant:small-caps;">Agamotto</span> OSDI '20 Artifact

This document describes the artifact for our OSDI '20 paper on <span style="font-variant:small-caps;">Agamotto</span>, a symbolic-execution based approach for systematically finding bugs in persistent memory applications and libraries. The remainder of this document describes how to run <span style="font-variant:small-caps;">Agamotto</span> and reproduce the key results from our paper.

### Downloads


[VM Image](https://drive.google.com/file/d/1UYg1D5vsL58lJ7HwDoprs5bfPeqUaypW/view?usp=sharing): Already has all the dependencies installed. Runs on KVM QEMU.

- Username: `reviewer`
- Password: See submission site

## Artifact Overview


`targets/`: This directory contains the scripts required to run all the tests needed to reproduce the main results from the paper.
After running experiments, the results will be placed into the `results/` directory

`results/`: This directory contains the scripts required to parse the results generated from the main experiments.

`vm_scripts/`: This directory contains scripts for building and running the evaluation VM.


## Artifacts Available Criteria

Agamotto is open-source and is available at https://github.com/efeslab/agamotto.git.

## Artifacts Functional Criteria

We now provide an overview of how to build and run <span style="font-variant:small-caps;">Agamotto</span>. For a guide on how to compile applications to run on <span style="font-variant:small-caps;">Agamotto</span>, see [<span style="font-variant:small-caps;">Klee</span>'s tutorial on building coreutils](https://klee.github.io/tutorials/testing-coreutils/).

### Building <span style="font-variant:small-caps;">Agamotto</span>

The procedure for building <span style="font-variant:small-caps;">Agamotto</span> is substantially similar to building KLEE
(the instructions for building KLEE can be found [here][klee-build].) The notable difference is that <span style="font-variant:small-caps;">Agamotto</span> requires LLVM 8 rather than LLVM 9.


#### Creating the VM (optional)

```
./vm_scripts/install-vm.sh
# configure 

mkdir -p mnt; mkdir -p boot
sudo guestmount -a agamotto.qcow2 -i -o allow_other mnt
cp mnt/vmlinuz boot
cp mnt/initrd.img boot
sudo guestunmount mnt

# run and login
./run-vm.sh
# --- from inside the VM
cd ~/
git clone https://github.com/efeslab/klee-nvm.git agamotto 
git submodule init
git submodule update
mkdir -p build
source build.env
cd build
# Proceed to the instructions on "Compiling" below
```

#### Using the prebuilt VM (recommended)

```
# Download agamotto.qcow2 from the Google drive link
mv agamotto.qcow2 artifact/vm_scripts

# This is necessary to extract the vmlinuz and initrd.img for the run-vm.sh script
mkdir -p mnt; mkdir -p boot
sudo guestmount -a agamotto.qcow2 -i -o allow_other mnt
cp mnt/vmlinuz boot
cp mnt/initrd.img boot
sudo guestunmount mnt

./run-vm.sh
# The repo is in ~/agamotto
```

All the dependencies should be installed. You can then update the version of <span style="font-variant:small-caps;">Agamotto</span> and rebuild it by simply running:

```
cd ~/agamotto
source build.env
git checkout <version>
cd build/
make -j$(nproc)
```

#### Using the VM

`run-vm.sh` will run the QEMU VM (use `^a-X` to kill QEMU). You can login directly
at the login prompt, but the rendering of the terminal may behave strangely.
To login using ssh (which is recommend), run the following:

```
ssh reviewer@localhost -p 5000 # this port can be configured in run-vm.sh
```

#### Compiling <span style="font-variant:small-caps;">Agamotto</span>

Heavily adapted from: http://klee.github.io/build-llvm9/

```
git clone https://github.com/efeslab/klee-nvm.git agamotto
mkdir -p agamotto/build
cd agamotto

sudo apt install -y python3-pip build-essential curl libcap-dev git cmake libncurses5-dev python-minimal python-pip unzip libtcmalloc-minimal4 libgoogle-perftools-dev libsqlite3-dev doxygen python3-pip libselinux1-dev clang-8 llvm-8 llvm-8-dev llvm-8-tools pandoc

sudo -H pip3 install wllvm tabulate lit

# Install STP https://github.com/stp/stp
sudo apt-get install -y cmake bison flex libboost-all-dev python perl minisat
git clone https://github.com/stp/stp
cd stp
git submodule init && git submodule update
mkdir build
cd build
cmake ..
cmake --build .
sudo cmake --install .

source build.env

# uCLibc

cd klee-uclibc
./configure --make-llvm-lib --with-llvm-config=$(which llvm-config-8)
make -j$(nproc)
cd ..

# LibCXX

LLVM_VERSION=8 SANITIZER_BUILD= BASE=$(realpath ./build/) REQUIRES_RTTI=1 DISABLE_ASSERTIONS=1 ENABLE_DEBUG=0 ENABLE_OPTIMIZED=1 ./scripts/build/build.sh libcxx

# Finally build Agamotto

cd build

cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_CRC32_SUPPORT=ON \
  -DENABLE_SOLVER_STP=ON \
  -DENABLE_POSIX_RUNTIME=ON \
  -DENABLE_KLEE_UCLIBC=ON \
  -DKLEE_UCLIBC_PATH=$(realpath ../klee-uclibc) \
  -DENABLE_UNIT_TESTS=OFF \
  -DLLVM_CONFIG_BINARY=$(which llvm-config-8) \
  -DLLVMCC=$(which clang-8) \
  -DLLVMCXX=$(which clang++-8) \
  -DCMAKE_C_COMPILER=$(which wllvm) \
  -DCMAKE_CXX_COMPILER=$(which wllvm++) \
  -DENABLE_KLEE_LIBCXX=ON \
  -DKLEE_LIBCXX_DIR=$(realpath .)/libc++-install-8/ \
  -DKLEE_LIBCXX_INCLUDE_DIR=$(realpath .)/libc++-install-8/include/c++/v1/ \
  ..

make -j$(nproc)

```

#### Running <span style="font-variant:small-caps;">Agamotto</span>

Running <span style="font-variant:small-caps;">Agamotto</span> is very similar
to running <span style="font-variant:small-caps;">Klee</span> (see [this tutorial](https://klee.github.io/tutorials/testing-coreutils/) for how to run general programs on <span style="font-variant:small-caps;">Klee</span>). We provide the following extensions via command line arguments below (these arguments are
visible via `klee --help`)

General arguments:
- `--search=nvm --nvm-heuristic-type=static`: This causes <span style="font-variant:small-caps;">Klee</span> to use <span style="font-variant:small-caps;">Agamotto</span>'s PM-aware heuristic search. 
- `--custom-checkers=true`: This enables the use of semantic bug oracles.

POSIX runtime arguments (part of the [symbolic environment model](https://klee.github.io/tutorials/using-symbolic/)):
- `-sym-pmem <FILE> <NBYTES>`: Provide a symbolic persistent memory file of the specified size. The application-under-test can then call `mmap(FILE)` to access a region of persistment memory. The file contents
are symbolic on initialization and can represent any value.
- `-sym-pmem-zeroed <FILE> <NBYTES>`: Same as `-sym-pmem`, but the initial values are all 0.
- `-sym-pmem-delay <FILE> <NBYTES>`: Same as the above, but tricks the application into thinking that it created `FILE`.
- `-sym-pmem-init-from <INIT_FILE_PATH>`: Initialize the symbolic persistent memory file (named the same as `INIT_FILE_PATH`) to concrete values based on values from the real file at the specified path.


## Results Reproduced

There are four main results from <span style="font-variant:small-caps;">Agamotto</span>:

1. The number of new bugs found.
2. The performance of <span style="font-variant:small-caps;">Agamotto</span>'s 
search strategy compared to <span style="font-variant:small-caps;">Klee</span>'s default search strategy.
3. The overhead of <span style="font-variant:small-caps;">Agamotto</span>'s static analysis.
4. The number of bugs reproduced from prior work.

### 1. Reproduce the newly found bugs.

First, we run all of the test cases that we used to symbolically explore our test applications, as we did for the evaluation in our paper.

The `run_*_eval.sh` scripts are wrappers which execute <span style="font-variant:small-caps;">Agamotto</span>
with the appropriate command line parameters required.

```
cd artifact/targets
./run_pmdk_eval.sh
./run_nvm_direct_eval.sh
./run_memcached_eval.sh
./run_recipe_eval.sh
./run_redis_eval.sh
```

This creates <span style="font-variant:small-caps;">Klee</span> output files under `artifact/results`. These are the same as running standard <span style="font-variant:small-caps;">Klee</span> tests, but additionally outputs `all_pmem_err.csv` and `all.pmem.err`, which provide tabular and textual descriptions of all 
encountered PM bugs, respectively. In the `artifact/results` directory, we provide a number of scripts
to parse <span style="font-variant:small-caps;">Agamotto</span>'s output automatically.

For identifying new bugs, we run this script:

```
cd artifact/results
sudo -H pip3 install -r requirements.txt
./parse_bugs_and_perf.py
```

This should provide the following output:

```
{   'memcached': {'Correctness': 1, 'Performance': 20, 'Total': 21},
    'nvm-direct': {'Correctness': 7, 'Performance': 16, 'Total': 23},
    'pmdk': {'Correctness': 2, 'Performance': 11, 'Total': 13},
    'recipe': {'Correctness': 1, 'Performance': 13, 'Total': 14},
    'redis': {'Correctness': 3, 'Performance': 1, 'Total': 4}}

Overall:
        Total: 75
        Correctness: 14
        Performance: 21
        Transient: 40
```


### 2. Measure the performance of <span style="font-variant:small-caps;">Agamotto</span>'s search strategy

Run the experiments as performed for finding new bugs. If already run, there is no need to re-run them.

Then, run:
```
cd artifact/results
sudo -H pip3 install -r requirements.txt
./graph_performance.py {pmdk,recipe,memcached,nvm-direct,redis}
```

This generates `{pmdk,recipe,memcached,nvm-direct,redis}.pdf` in the same directory. These
graphs represent the performance of <span style="font-variant:small-caps;">Agamotto</span>'s search strategy
versus <span style="font-variant:small-caps;">Klee</span>'s default search strategy, and should be 
similar to the results shown in the evaluation in our paper.

Note that this experiment is dependent on the underlying CPU for timing and 
may vary.


### 3. Calculate the offline overhead of <span style="font-variant:small-caps;">Agamotto</span>

Run the experiments as performed for finding new bugs. If already run, there is no need to re-run them.

Then, run:
```
cd artifact/results
sudo -H pip3 install -r requirements.txt
./get_offline_overhead.py
```

This should provide output similar to the following:

```
memcached-pm      2.198278
redis-pmem       19.661336
nvm-direct        0.021961
recipe            0.549605
pmdk              0.596972
Name: Offline Overhead (minutes), dtype: float64
```

Note that this experiment is dependent on the underlying CPU for timing and 
may vary. In addition, we have modified our alias analysis algorithm from our original submission,
so the precise numbers may vary. 

### 4. Reproducing bugs from prior work

We run a different set of test cases to reproduce the bugs found by prior work (specifically
PMTest and XFDetector).

```
cd artifact/targets
./run_prior_art_repro.sh
```

Then, run the script to count the bugs that were reproduced.

```
cd artifacts/results
sudo -H pip3 install -r requirements.txt
./count_reproduced_bugs.py
```

This should provide output similar to the following:

```
        btree_map_insert_empty at nvmbugs/000_pmdk_btree_map/btree_map_buggy.c:150
        btree_map_insert_empty at nvmbugs/000_pmdk_btree_map/btree_map_buggy.c:150
        btree_map_insert_item at nvmbugs/000_pmdk_btree_map/btree_map_buggy.c:267
        create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:135
        create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:132
        hm_atomic_insert at nvmbugs/hashmap_atomic/hashmap_atomic.c:263
        create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:137
        create_hashmap at nvmbugs/hashmap_atomic/hashmap_atomic.c:138
        initPersistentMemory at server.c:4029
        initPersistentMemory at server.c:4029
Number of unique reproduced bugs: 5
```


[//]: # (Links below)

[klee-build]: http://klee.github.io/releases/docs/v1.3.0/build-llvm34/
