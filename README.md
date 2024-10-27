# BBQ – Block-Based Queue

The result of my Bachelor's thesis.

Outside of [the queue itself](/relaxed_concurrent_fifo/relaxed_fifo.h) and the [atomic bitset](/relaxed_concurrent_fifo/atomic_bitset.h) everything is kind of a mess.

Due to an extreme amount of template instnations build times are extreme, realistically building is only possible when commenting out large chunks of code.
