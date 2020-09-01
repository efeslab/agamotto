# <span style="font-variant:small-caps;">Agamotto</span> OSDI '20 Artifact

This document describes the artifact for our OSDI '20 paper on <span style="font-variant:small-caps;">Agamotto</span>, a symbolic-execution based approach for systematically finding bugs in persistent memory applications and libraries. The remainder of this document describes how to run <span style="font-variant:small-caps;">Agamotto</span> and reproduce the key results from our paper.

### Downloads
---



## Artifact Overview
---

`targets/`: This directory contains the scripts required to run all the tests needed to reproduce the main results from the paper.
After running experiments, the results will be placed into the `results/` directory

`results/`: This directory contains the scripts required to parse the results generated from the main experiments.

`vm_scripts/`: This directory contains scripts for building and running the evaluation VM.


## Artifacts Available Criteria

Agamotto is open-source and is available at https://github.com/efeslab/klee-nvm.git.

## Artifacts Functional Criteria



### Building <span style="font-variant:small-caps;">Agamotto</span>

The procedure for building <span style="font-variant:small-caps;">Agamotto</span> is substantially similar to building KLEE
(the instructions for building KLEE can be found [here][klee-build].) We outline
the main differences at a high-level below:
1. <span style="font-variant:small-caps;">Agamotto</span> requires LLVM 8.


#### Creating the VM

```
./vm_scripts/install-vm.sh
# configure 

mkdir -p mnt
sudo guestmount -a agamotto.qcow2 -i -o allow_other mnt
cp mnt/vmlinuz boot
cp mnt/initrd.img boot
sudo guestunmount mnt

# login
<!-- sudo apt install  -->

cd ~/
git clone https://github.com/efeslab/klee-nvm.git agamotto 
git submodule init
```

#### Using the prebuilt VM

```
wget <VM URL> artifact/vm_scripts
cd artifact/vm_scripts
./run-vm.sh
ssh reviewer@localhost -p 5000

```

#### Compile <span style="font-variant:small-caps;">Agamotto</span>

Heavily adapted from: http://klee.github.io/build-llvm9/

```
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

cd agamotto/build

cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_SOLVER_STP=ON \
  -DENABLE_POSIX_RUNTIME=ON \
  -DENABLE_KLEE_UCLIBC=ON \
  -DKLEE_UCLIBC_PATH=$(realpath ../klee-uclibc) \
  -DENABLE_UNIT_TESTS=OFF \
  -DLLVM_CONFIG_BINARY=$(which llvm-config-8) \
  -DLLVMCC=$(which clang-8) \
  -DLLVMCXX=$(which clang++-8) \
  -DCMAKE_C_COMPILER=$(which clang-8) \
  -DCMAKE_CXX_COMPILER=$(which clang++-8) \
  -DENABLE_KLEE_LIBCXX=ON \
  -DKLEE_LIBCXX_DIR=$(realpath .)/libc++-install-8/ \
  -DKLEE_LIBCXX_INCLUDE_DIR=$(realpath .)/libc++-install-8/include/c++/v1/ \
  ..

make -j$(nproc)

```

## Results Reproduced

There are three main results from <span style="font-variant:small-caps;">Agamotto</span>:

1. The number of new bugs found.
2. The performance of <span style="font-variant:small-caps;">Agamotto</span>'s 
search strategy compared to <span style="font-variant:small-caps;">Klee</span>'s default search strategy.
3. The overhead of <span style="font-variant:small-caps;">Agamotto</span>'s static analysis.

[//]: # (Links below)

[klee-build]: http://klee.github.io/releases/docs/v1.3.0/build-llvm34/
