# [BlockFIFO](/relaxed_concurrent_fifo/block_based_queue.h) & [MultiFIFO](/relaxed_concurrent_fifo/contenders/multififo/)

## Building

This project uses CMake.

```
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To build for ARM, specify `BBQ_IS_ARM`.

## Requirements

C++23

Windows requires MSVC or GCC.
Unix based systems require GCC.

### Master Script

To automatically generate all data used in the paper the [master script](/scripts/master.py) can be used.

It additionally requires:
- Python
- Ninja
- numpy
- pdflatex

#### Arguments

The script can be invoked as following:
```
python scripts/master.py <thread_count> <experiments> <contenders...>
```

- `experiments` is a comma-separated list of experiments to run, possible experiments include `tuning`, `performance`, `quality`, `prodcon`, `bfs`.
- `contenders` is a list of concrete FIFO-queue implementation to use in the experiments, supporting regex. They are all arguments following `experiments`.

An example invocation could look like `python scripts/master.py 128 performance,quality .*bbq.* .*multififo.*` to run the performance and quality experiments on all BlockFIFO and MultiFIFO instances.

By default all experiments and instances are used.

#### Graphs

Place all graph instances you wish to benchmark in the respective folder in the scripts directory.

Weak scaling expects graphs to be named in the following format: `{graph_name}_t_{x}.gr`, so e.g. `my_graph_t_1.gr`, `my_graph_t_2.gr`, `my_graph_t_4.gr`, ...
All power of two thread counts are tested up to and including the maximum available thread count of the machine (even if it itself is not a power of two).
