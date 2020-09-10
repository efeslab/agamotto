<span style="font-variant:small-caps;">Agamotto</span> (OSDI '20)
================================================================

This repository contains the code and experiments for the OSDI '20 paper 
["<span style="font-variant:small-caps;">Agamotto</span>: How Persistent is your Persistent Memory Application?"](https://www.usenix.org/conference/osdi20/presentation/neal) <span style="font-variant:small-caps;">Agamotto</span> is based on <span style="font-variant:small-caps;">Klee</span> (described below).

## Core Additions and Layout

`artifact/`: This directory contains all of the information needed to build <span style="font-variant:small-caps;">Agamotto</span> and reproduce the key results from the paper.

`lib/Core/NvmHeuristics.{cpp,h}`: This contains the core logic behind <span style="font-variant:small-caps;">Agamotto</span>'s search strategy.

`lib/Core/RootCause.{cpp,h}`: This contains the bug tracking and reporting mechanisms used in <span style="font-variant:small-caps;">Agamotto</span>.

`lib/Core/CustomCheckerHandler.{cpp,h}`: Contains the APIs for the custom semantic bug oracles.

`runtime/POSIX/`: Contains PM-specific modifications to the environment model (modeling persistent files), as well as ports of [Cloud9's](https://github.com/dslab-epfl/cloud9) extended environment model. Also contains symbolic socket handlers used in the evaluation of Redis and memcached.

---
### KLEE Symbolic Virtual Machine

<!-- [![Build Status](https://travis-ci.org/klee/klee.svg?branch=master)](https://travis-ci.org/klee/klee)
[![Coverage](https://codecov.io/gh/klee/klee/branch/master/graph/badge.svg)](https://codecov.io/gh/klee/klee) -->

`KLEE` is a symbolic virtual machine built on top of the LLVM compiler
infrastructure. Currently, there are two primary components:

  1. The core symbolic virtual machine engine; this is responsible for
     executing LLVM bitcode modules with support for symbolic
     values. This is comprised of the code in lib/.

  2. A POSIX/Linux emulation layer oriented towards supporting uClibc,
     with additional support for making parts of the operating system
     environment symbolic.

Additionally, there is a simple library for replaying computed inputs
on native code (for closed programs). There is also a more complicated
infrastructure for replaying the inputs generated for the POSIX/Linux
emulation layer, which handles running native programs in an
environment that matches a computed test input, including setting up
files, pipes, environment variables, and passing command line
arguments.

For further information, see the [webpage](http://klee.github.io/).
