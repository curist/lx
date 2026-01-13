# Fiber-Driven Compiler + LSP Patterns (Design Notes)

This document sketches a small, maintainable way to leverage fibers in the
compiler pipeline and LSP server without turning the codebase into a callback
maze. The focus is on incremental progress, responsive cancellation, and a
stable event contract.

## Goals

- Stream diagnostics and progress events without threading callbacks through
  every pass.
- Keep the LSP responsive under load by yielding at predictable points.
- Make cancellation semantics explicit and easy to reason about.
- Avoid VM-level step counting or invasive instrumentation.

## Non-Goals

- Full async IO integration.
- A shared, multi-consumer AST event stream.
- Changing core pass semantics or ordering.

## Patterns Fibers Enable (Compiler + LSP)

### Streaming diagnostics/progress (lowest risk)

Passes can yield events as they work, and the LSP can stream them immediately.
This avoids threading callbacks through every pass and keeps diagnostics
consistent with pass attribution.

### Time-sliced compilation (cooperative yield points)

Passes yield after processing N nodes or finishing a phase. The driver resumes
until a budget is consumed. This keeps the server responsive without VM-level
step counting.

### Lazy module resolution handshake (future stage)

Resolver yields a structured `needModule` event; the driver resumes with cached
exports or an error. This requires explicit cache and cycle policies.

### Request fibers + cancellation

Each LSP request runs in its own fiber. Cancellation is handled at yield
points by simply not resuming canceled fibers and emitting a final event.

### Shared traversal utilities (not shared streams)

Share traversal code per pass; do not share a single event stream across
passes. Different analyses want different traversal order and early exits.

## Core Idea: One Compiler Fiber + Event Stream

Use a single compiler fiber that yields standardized events. The LSP driver
resumes the fiber until a budget is consumed or the compilation finishes.

### Event Schema

Events are maps with a stable `kind` and pass attribution:

```
.{ kind: "progress", pass: "resolve", phase: "walk", done, total? }
.{ kind: "diag", pass: "typecheck", file, range, severity, message, code? }
.{ kind: "deps", file, deps }
.{ kind: "done", result }
.{ kind: "error", error }
```

Keep the schema small and consistent; avoid ad-hoc fields.

### Driver Contract

```
Compiler.start(entry, opts) -> fiber
Compiler.poll(fiber, budget) -> .{ events, done?, result?, error? }
```

`budget` can start as `{ maxEvents }` and later expand to include node-based
limits. The driver is responsible for batching events and deciding when to
resume.

## Yield Model

Yield at natural iteration points inside passes (e.g., after processing N nodes
or completing a phase) rather than VM instruction counts. This keeps overhead
low and makes yield placement easy to audit.

## Cancellation

Cancellation is handled at yield points. The driver simply stops resuming a
canceled fiber and may emit a final `error` or `done` event with a cancellation
flag. This keeps cancellation logic localized to yield boundaries.

## Lazy Module Resolution (Future Stage)

If/when module resolution becomes fiber-aware:

```
.{ kind: "needModule", key, spec }
```

The driver resumes with either cached module exports or a compilation error.
This requires explicit cache and cycle policies; do not attempt until those are
well-defined.

## Shared Traversal (Future Stage)

Prefer shared traversal utilities over a shared traversal stream. Different
passes have different traversal needs; a single shared stream risks coupling
and buffering complexity.

## Suggested Staging

- Stage 1: single compiler fiber with standardized events.
- Stage 2: node-based budgets and cancellation.
- Stage 3: fiber-based module resolution handshake.
- Stage 4: shared traversal utilities if duplication is painful.
