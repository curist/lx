# Benchmark Results Archive

This directory contains historical benchmark results for the lx interpreter.

## Files

- **2025-12-26.txt** - Post-optimization results including ANF temp elimination and superinstructions (ADD_LOCAL_IMM, STORE_LOCAL, STORE_BY_IDX)
  - Commit: 2286bed
  - Key improvements: 19.6% average speedup, up to 31% on array_fill

## Format

Each results file contains:
- Test date and commit hash
- Hardware/OS information
- Raw benchmark timings for all test cases
- Comparison with other languages (Python, Lua, LuaJIT, Chez Scheme)
- Performance comparison with baseline when applicable

## Running Benchmarks

To generate new results:

```bash
cd benchmarks
REPEAT=3 ./run.sh 2>&1 | tee results/$(date +%Y-%m-%d).txt
```

For detailed analysis, see `optimization-report.md` in the parent directory.
