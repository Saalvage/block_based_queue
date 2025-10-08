# [BlockFIFO](/relaxed_concurrent_fifo/block_based_queue.h) & [MultiFIFO](/relaxed_concurrent_fifo/contenders/multififo/): Scalable Relaxed Queues
Stefan Koch, Peter Sanders, and Marvin Williams

## Quick Start
The easiest way to run the benchmarks is to use Docker.
By default, all experiments are run with all competitors with all available threads.

By mounting a local directory `results` to the container, the produced artifacts can be accessed outside the container.
```bash
mkdir results
docker build -t evaluation .
docker run --rm -v $(pwd)/results:/block_multi_fifo/results evaluation
```

For running the benchmark script interactively, use:
```bash
docker run -it --rm -v $(pwd)/results:/block_multi_fifo/results evaluation /bin/bash
```
From there, you can run the [main script](/scripts/run_all.py) manually as described below.

If you want to adapt the code or scripts, you can also mount the whole directory:
```bash
docker run -it --rm -v $(pwd):/block_multi_fifo evaluation /bin/bash
```

## Manual Building

This project uses CMake.

```
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To build for ARM, add `-DFIFO_IS_ARM` to the first command.

## Requirements

C++23

Windows requires MSVC or GCC.
Unix based systems require GCC.

### Main Script

To automatically generate all data used in the paper the [main script](/scripts/run_all.py) can be used.

It additionally requires:
- Python
- Ninja
- numpy
- pdflatex

#### Arguments

The script can be invoked as following:
```
python3 scripts/run_all.py <thread_count> <experiments> <contenders...>
```

- `experiments` is a comma-separated list of experiments to run, possible experiments include `tuning`, `performance`, `quality`, `prodcon`, `bfs`.
- `contenders` is a list of concrete FIFO-queue implementation to use in the experiments, supporting regex. They are all arguments following `experiments`.

An example invocation could look like `python3 scripts/run_all.py 128 performance,quality .*blockfifo.* .*multififo.*` to run the performance and quality experiments on all BlockFIFO and MultiFIFO instances.

If no arguments are given, all experiments and instances are used with the maximum available thread count.

#### Graphs

Since the graphs used in the paper are multiple TB in size, they are not provided here.
You can obtain the graphs yourself as described below.
Place all graph instances you wish to benchmark in the respective subfolder in the `graphs` directory (`graphs/ss` for strong scaling, `graphs/ws` for weak scaling).
Weak scaling graphs must be named in the following format, where {x} is the thread number: `{graph_name}_t_{x}.gr`, e.g., `my_graph_t_1.gr`, `my_graph_t_2.gr`, `my_graph_t_4.gr`, ...
All power of two thread counts are tested up to and including the maximum available thread count of the machine (even if it itself is not a power of two).

The strong scaling graphs used in the paper are available [here](https://i11www.iti.kit.edu/resources/roadgraphs.php) and [here](https://law.di.unimi.it/datasets.php).
The weak scaling graphs can be generated with [KaGen](https://github.com/KarlsruheGraphGeneration/KaGen).
Please refer to the KaGen documentation for installation instructions.
The commands to generate the graphs are as follows, where `t` is the number of threads you want to scale to (e.g. for 64 threads use `t=64`).
```bash
KaGen rgg2d -n $((2**12*t)) -m $((2**12*t*16)) --output graphs/ss/rgg2d_t_${t}.gr
KaGen rhg -g 2.7 -n $((2**12*t)) -m $((2**12*t*16)) --output graphs/ss/rhg_t_${t}.gr
KaGen gnm-undirected -n $((2**12*t)) -m $((2**12*t*16)) --output graphs/ss/gnm_t_${t}.gr
```
Please contact the authors if you need help generating the graphs.
