# Agent Guide (lx-lang)

This repo contains the `lx` language runtime (C) and its self-hosted compiler/tooling (lx sources).

## Build & Test

- Build the binary: `make` (produces `out/lx`)
- Run tests: `make test` (runs `cd lx && make runall && make test`)

## Bootstrapping (embedded bytecode headers)

The compiler is embedded as bytecode headers:
- `include/lx/lxlx.h` (compiled `lx/main.lx`)
- `include/lx/lxglobals.h` (compiled `lx/globals.lx`)

Regenerate headers when you modify core compiler sources (`lx/`), module resolution, bytecode/opcodes, or object format:
- `make clean && make`
- `LX=./out/lx make -B prepare build`

Related scripts:
- `scripts/build-lxlx.sh`, `scripts/build-globals.sh`
- `lx/scripts/bootstrap-codegen.lx` (fallback codegen path; writes `/tmp/{main,globals}.lxobj`)

Non-backward-compatible opcode/bytecode changes:
- Normal `make`/`make prepare` may fail if the embedded bytecode can’t run on the updated VM.
- Use `lx/scripts/bootstrap-codegen.lx` (with an older/system `lx`) to generate fresh `.lxobj`, then rebuild the embedded headers and build the C VM; see `lx/scripts/README.md`.

Path gotcha:
- From repo root: `lx run lx/scripts/bootstrap-codegen.lx lx/main.lx`
- From `lx/`: `lx run scripts/bootstrap-codegen.lx main.lx`

## Module Resolution / Project Root

Imports resolve relative to a **project root** discovered by walking up from the *entry file’s directory* to find a `.lxroot` marker.
- In this repo, compiler sources use `lx/.lxroot` as their project root marker.
- If no `.lxroot` is found, `LX_ROOT` can provide a fallback; otherwise the entry file’s directory is used.

## Language Gotchas (high-signal)

- `type(.{ ... })` is `"map"` (not `"hashmap"`).
- Hashmap literals require `.{ ... }` (to distinguish from block expressions `{ ... }`).
- Most constructs are expressions; functions/blocks implicitly return the last expression (use `return` for early exit).
- No `this` keyword; reference objects explicitly.
- No string interpolation; no `++`/`--`.
- Functions are hoisted: mutual recursion is allowed, but you can’t call a hoisted function before the block’s hoisted function group is complete.
- Closures capture locals via upvalues; loop-variable captures can share the same slot—create a fresh `let` per iteration when you need per-iteration capture.
- `nameOf()` requires an `enum`.

## Style / Idioms (lx)

- Prefer `collect` for array building/transforms: `collect x in xs { f(x) }` (and `collect let i = ... { ... }`); `continue` skips emitting and `break` returns the collection built so far; avoid `map`/`filter` (likely to be removed from the prelude).
- Use hashmap shorthand fields when the key matches the variable: `.{ x, y }` ≈ `.{ x: x, y: y }`.
- Use let-destructuring to pull fields out of maps/modules: `let .{ OP, NODE: node } = types`.
- Prefer chaining with `->` for pipelines: `Color->nameOf(Color.Green)`, `Lx.globals()->each(_1(print))`.

## Communication / Reviews

- Prefer describing **what/why/how** over counting metrics (line counts, assertion counts, etc.).
