# Source Layout

This document describes the intended source layout for `lx/` as the compiler grows.
It is a target direction (some pieces may not exist yet).

## Goals

- Make it obvious what is a public entrypoint vs internal library code.
- Keep “passes” discoverable and composable (pipeline-driven), without turning `lx/src/` into a flat bucket.
- Establish a clean boundary where the frontend hands an IR to the backend.

## Directory Map

```
lx/
  cmd/                 # User-facing entrypoints (CLI-ish programs)
  docs/                # Design docs for the lx compiler (this folder)
  examples/            # Small example programs / demos
  scripts/             # Dev/bootstrap drivers (repo-internal maintenance)
  services/            # Tooling services built on compiler artifacts (e.g. IDE queries)
  src/                 # Compiler libraries (passes, IRs, utilities)
  test/                # Test suite for lx compiler and runtime contracts
```

### `lx/cmd/` (entrypoints)

User-facing programs you run directly, e.g.:
- `lx/cmd/mlx.lx`: the compiler command surface
- `lx/cmd/anf_debug.lx`: debugging / introspection commands
- `lx/cmd/lxcheck.lx`: developer-facing CLI to typecheck an entry module (run as `./cmd/lxcheck.lx typecheck <entry>` from `lx/`)

Guideline: `cmd/` should be thin wrappers around `src/` libraries.

### `lx/scripts/` (bootstrap + maintenance)

Repo-internal drivers that exist to unblock bootstrapping and maintenance tasks.
These may write to temp paths and may assume a repo checkout.

Keep separate from `cmd/` to avoid mixing “product surface” with “maintenance tools”.

Note: repo root `scripts/` is shell wrappers; `lx/scripts/` is lx code.

### `lx/services/` (tooling)

Read-only services that consume compiler artifacts, e.g. `lx/services/query.lx`.

Guideline: services should not own compiler semantics; they should only interpret
artifact tables produced by passes.

## `lx/src/` structure

Recommended shape:

```
lx/src/
  passes/
    pipeline.lx        # Generic pass runner + dependency validation
    frontend/          # Source -> resolved AST (+ tables)
    typed/             # Resolved AST -> Typed IR (planned)
    backend/           # IR -> bytecode/object (optional grouping)
  ir/
    typed/             # Typed IR data structures + invariants (planned)
  runtime/             # Runtime-facing ABI helpers (optional; may start empty)
  ...                  # Shared utilities (errors, scanner, types, etc.)
```

### What belongs where

- **Pass implementations** (parse/lower/anf/resolve/typecheck/...) live under `lx/src/passes/**`.
- **IR definitions** live under `lx/src/ir/**` (not in pass files), so multiple passes/backends can share them.
- **Utilities** (token/opcode enums, error formatting, scanners, origin tracking helpers) stay at `lx/src/*` as shared modules.

## Typed Backend Plan (not implemented yet)

If typed IR will drive codegen, treat it as the frontend/backend boundary.

Planned pipeline shape:

```
parser -> lower -> anf -> resolve -> build-tir -> codegen-tir -> verify-bytecode -> objbuilder
```

Proposed components:

- `lx/src/ir/typed/*`: typed IR nodes + constructors + invariants
- `lx/src/passes/typed/build-tir.lx`: converts resolved AST + tables into typed IR
  - produces `tir` as the primary artifact
  - produces type diagnostics as a normal pass result (best-effort vs fatal is a pipeline choice)
- `lx/src/codegen-tir.lx` (or `lx/src/passes/backend/codegen-tir.lx`): bytecode emission from typed IR
- Driver profiles:
  - `default`: keep current AST-based backend
  - `typed-backend`: switch backend to typed IR once it exists

Pragmatic migration rule: keep the existing AST codegen working until `typed-backend`
is stable; avoid breaking `default` while the typed IR is under construction.
