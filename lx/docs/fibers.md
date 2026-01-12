# Fibers Design (VM + Scheduler)

This document outlines a minimal, explicit fiber model for the lx VM. The goal
is to enable deterministic, cooperative scheduling now and clean event loop
integration later.

## Goals

- Cooperative fibers with explicit `yield`.
- Deterministic ordering (no preemption).
- VM state saved/restored per fiber.
- Clean integration points for future event loop work.
- GC safety for fiber-owned stacks/frames/upvalues.

## User-Facing API (Initial)

This design ships as a library API first (no new syntax/keywords):

- `Fiber.create(fn)` -> fiber
- `Fiber.resume(fiber, ...args)` -> yielded value(s) or final return
- `Fiber.yield(value)` -> yield from within a fiber
- `Fiber.status(fiber)` -> `"new" | "running" | "suspended" | "done" | "error"`

Syntax sugar (like `yield` as a keyword) can be added later.

## Fiber Model

A fiber is a snapshot of VM execution state. The VM is a "view" over the
currently running fiber.

### Fiber States

- `NEW`: created, not yet started
- `RUNNING`: currently executing
- `SUSPENDED`: yielded, resumable
- `DONE`: completed normally
- `ERROR`: completed with an error

### Fiber Data (conceptual)

```
struct ObjFiber {
  Obj obj;
  FiberState state;

  Value* stack;
  Value* stackTop;
  int stackCapacity;

  CallFrame* frames;
  int frameCount;
  int frameCapacity;

  ObjUpvalue* openUpvalues;

  Value lastError;   // structured error value
  ErrorHandler* errorHandler; // per-fiber error boundary stack

  ObjFiber* caller;  // fiber to return control to on yield/return
  bool cancelled;
};
```

Caller invariant:

- When a fiber is `RUNNING`, `fiber->caller` is either the resuming fiber or
  `NULL` if entered from a top-level scheduler.
- On `yield` or `return`, control transfers to `caller`.
- `caller` is cleared when the fiber reaches `DONE` or `ERROR`.

## VM Integration

The VM keeps fast "register" pointers for stack/frames/upvalues, and
`switchToFiber` is the only place that swaps those registers.

```
switchToFiber(vm, fiber)  // loads fiber fields into VM registers
syncFromVM(vm)            // writes VM registers back to fiber
```

The VM must call `syncFromVM` at yield/return/error boundaries.

## Yield/Resume Semantics

Lua-like semantics:

- `yield(y)` yields `y` to the resumer.
- When resumed, `yield(...)` evaluates to the resume arguments.

Resume arguments are pushed onto the fiber stack as the result values of the
last suspended `yield` expression.

Rules:

- `fiberResume(f, ...args)` allowed in `NEW` or `SUSPENDED`.
- `NEW -> RUNNING`, `SUSPENDED -> RUNNING`.
- On `DONE` or `ERROR`, `fiberResume` returns an error.
- Yield/resume support single or multiple values (defined explicitly).

## Yield Safety

A fiber may yield only from bytecode execution, not from non-yieldable native
calls.

Mechanism:

- VM tracks `nonYieldableDepth`.
- Entering a native function increments; leaving decrements.
- `OP_YIELD` checks and errors if `nonYieldableDepth > 0`.

Yieldable native functions can be supported later by returning a special status
that requests suspension rather than longjmp from deep C stacks.

## Error Handling

Errors are intra-fiber:

- If caught, execution continues inside the fiber.
- If uncaught, fiber transitions to `ERROR` and returns an error value to its
  resumer/scheduler.

Resume behavior:

- `DONE` -> error "cannot resume completed fiber"
- `ERROR` -> error "fiber failed" plus `lastError`

Per-fiber error handling uses an explicit handler stack:

```
struct ErrorHandler { jmp_buf buf; ErrorHandler* prev; }
```

This avoids pointer lifetime issues and supports nested boundaries.

## Scheduler Queue

A minimal FIFO runnable queue is enough for MVP.

- Scheduler enqueues/resumes fibers.
- `fiberResume` does not decide scheduling; it only runs a fiber until a
  boundary.
- Queue storage must be visible to the GC (e.g. VM-owned array or explicit
  marking in `markRoots`).

## GC Integration

`markFiber` must trace:

- stack values from `stack` to `stackTop`
- call frame closures/functions and their referenced constants
- open upvalues chain
- `lastError`

Scheduler structures must be rooted or explicitly scanned in `markRoots`.

## Event Loop Integration (Future)

Event loop completions should:

- enqueue the waiting fiber
- supply resume values
- honor cancellation/backpressure rules

This keeps IO completion and scheduling independent of the VM core.

## Invariants

- Only one fiber is `RUNNING` at a time per VM.
- VM registers always reflect `currentFiber`.
- A fiber may not be resumed while `RUNNING`.
- `yield` is illegal when `nonYieldableDepth > 0`.

## OP_YIELD Stack Effect (Conceptual)

```
OP_YIELD n
  pops n values as yield results
  suspends current fiber
  on resume, pushes m resume values
```

## MVP Implementation Steps

1. Add `ObjFiber` type and allocation helpers.
2. Add `switchToFiber` / `syncFromVM` plumbing in `vm.c`.
3. Add `OP_YIELD` and `fiberResume` (NEW/SUSPENDED only).
4. Implement GC tracing for fibers and scheduler queue roots.
5. Add a minimal FIFO scheduler helper (library-level, not VM core).
