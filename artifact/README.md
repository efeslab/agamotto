# Artifact Evaluation Instructions

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
```

#### Using the prebuilt VM

```
wget <VM URL> artifact/vm_scripts
cd artifact/vm_scripts
./run-vm.sh
ssh reviewer@localhost -p 5000

```

#### Compile <span style="font-variant:small-caps;">Agamotto</span>

## Results Reproduced

There are three main results from <span style="font-variant:small-caps;">Agamotto</span>:

1. The number of new bugs found.
2. The performance of <span style="font-variant:small-caps;">Agamotto</span>'s 
search strategy compared to <span style="font-variant:small-caps;">Klee</span>'s default search strategy.
3. The overhead of <span style="font-variant:small-caps;">Agamotto</span>'s static analysis.

[//]: # (Links below)

[klee-build]: http://klee.github.io/releases/docs/v1.3.0/build-llvm34/
