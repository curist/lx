Microbenchmarks for Lx vs Python vs Lua.

Each benchmark returns a checksum to avoid I/O noise. Keep implementations structurally similar so we measure dispatch/arith/table overheads, not library calls.

## Benchmarks
- `sum_loop`: sum 1..N (tight numeric loop).
- `fizzbuzz`: classic modulo/branch; accumulates numeric tags instead of printing.
- `fib_iter`: iterative Fibonacci up to N (mod 1,000,000,007 to avoid bignum skew).
- `array_fill`: allocate array of length N, fill with i, then increment each element and sum.
- `map_hit_miss`: insert N entries (int→int), then probe alternating hits/misses.

## Running
Use the helper script; it defaults to larger sizes for better timing resolution. Adjust `N_*` vars or `REPEAT` inside `run.sh` for heavier/lighter runs.

```sh
cd benchmarks
./run.sh              # runs all langs that are available
LX=../lx/lx ./run.sh  # point to custom lx binary if not on PATH
```

The script skips a language if its interpreter is missing. Results include each checksum (to confirm correctness) followed by wall-clock seconds from `/usr/bin/env time` (uses `gtime -f "%e"` when available). For better stability, bump `REPEAT` and take the best or median.
