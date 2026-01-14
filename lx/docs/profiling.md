# Profiling (Flame Graphs)

This guide shows how to capture LX-level flame graphs from the VM using the
stack sampler and render them with FlameGraph.

## Build a profiling binary

The Makefile has a `profile` mode that enables the sampler:

```sh
make MODE=profile
```

This builds `./out/lx` with `PROFILE_STACKS` enabled.

## Run a workload and collect samples

Set `LX_STACK_SAMPLE` to write folded stacks to a file, and optionally tune the
sampling rate.

```sh
LX_STACK_SAMPLE=out/lx.folded LX_STACK_SAMPLE_RATE=10000 \
  ./out/lx compile lx/main.lx
```

Notes:
- `LX_STACK_SAMPLE` can be a path or `-` (stderr).
- `LX_STACK_SAMPLE_RATE` is "every N opcodes". Lower values capture more detail
  but increase overhead.

## Render the flame graph

`LX_STACK_SAMPLE` already produces folded stacks, so you can render directly:

```sh
flamegraph.pl out/lx.folded > ~/Downloads/fg.svg
```

If you prefer, saving as `.html` also works since the output is SVG markup:

```sh
flamegraph.pl out/lx.folded > ~/Downloads/fg.html
```

## Reading the flame graph

- Width indicates time (samples). Wider boxes are hotter.
- The bottom is caller context; higher boxes are deeper callees.
- Color is just a palette. It is not a heat indicator unless you specifically
  use a heatmap palette.
