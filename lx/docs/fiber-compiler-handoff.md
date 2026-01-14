# Fiber-Based Compiler API - Hand-off Notes

This document captures the current state and next steps for the fiber-based compiler API (Stage 1 of `fiber-patterns.md`).

## What's Been Built

### Files Created

- `lx/src/compiler-fiber.lx` - Core fiber-based compiler API
- `lx/test/compiler-fiber.test.lx` - TDD test suite
- `lx/src/events.lx` - Event schema (created earlier)
- `lx/test/events.test.lx` - Event schema tests (created earlier)

### API Contract

```lx
let Compiler = import "src/compiler-fiber.lx"

// Start a compilation fiber
// - source: source code string
// - opts: .{ filename: "foo.lx" }
// Returns: .{ fiber, cancel } where cancel() requests cancellation
let handle = Compiler.start(source, opts)

// Poll for events with budget
// - budget: .{ maxEvents: N }
// Returns: .{ events: [...], done?: bool, result?: Event, error?: string }
let result = Compiler.poll(handle.fiber, .{ maxEvents: 10 })

// Cancel at any time (optional)
handle.cancel()
// Next poll returns ERROR event with message "Cancelled"
```

### Current Pipeline

```
parse → lower → anf → resolve → anf-inline → lower-intrinsics → codegen
```

Each pass yields:
- `PROGRESS` event at start (`phase: "start"`)
- `DIAG` events for any errors
- `PROGRESS` event at end (`phase: "done"`)

The full pipeline produces a bytecode function that can be executed.

### Event Types (from `events.lx`)

```lx
EventKind.PROGRESS  // .{ kind, pass, phase, done, total }
EventKind.DIAG      // .{ kind, pass, file, range, severity, message, code }
EventKind.DEPS      // .{ kind, file, deps } - not yet used
EventKind.DONE      // .{ kind, result }
EventKind.ERROR     // .{ kind, error }
```

## Next Steps to Complete Stage 1

### 1. ~~Add Remaining Passes~~ ✅ DONE

All passes are now implemented:
```
parse → lower → anf → resolve → anf-inline → lower-intrinsics → codegen
```

### 2. ~~Extract Proper Range Info for DIAG Events~~ ✅ DONE

DIAG events now include proper source locations:

- **Parse errors**: Extracted from error message format `[file:line:col] context: message`
- **Resolve errors**: Looked up from `resolveResult.nodes[nodeId]` using `line`, `col`, `endLine`, `endCol`
- **Codegen errors**: Same approach using `resolveResult.nodes[nodeId]`

### 3. Wire to Driver for Module-Level Compilation

The current API compiles a single source string. For real usage, it needs to integrate with the driver's module resolution and caching.

**Task:** Create a module-aware variant or extend the API:

```lx
// Option A: Add a path-based start function
Compiler.startModule(path, opts) -> fiber

// Option B: Accept a driver instance
Compiler.startWithDriver(driver, path, opts) -> fiber
```

**Considerations:**
- Module resolution needs access to the filesystem
- Import caching should be preserved across compilations
- Circular import detection

### 4. ~~Add Cancellation Support~~ ✅ DONE

`Compiler.start(source, opts)` now returns `{ fiber, cancel }` with built-in cancellation support:

```lx
let handle = Compiler.start(source, .{ filename: "test.lx" })

// Poll for events
let result = Compiler.poll(handle.fiber, .{ maxEvents: 10 })

// Cancel at any time (optional)
handle.cancel()

// Next poll returns ERROR event with message "Cancelled"
let result2 = Compiler.poll(handle.fiber, .{ maxEvents: 100 })
// result2.result.kind == EventKind.ERROR
// result2.result.error == "Cancelled"
```

Implementation uses a shared `cancelled` flag checked at every yield point. If you don't need cancellation, simply ignore `handle.cancel`.

### 5. Add DEPS Events for Import Tracking

**Task:** Yield `DEPS` events when imports are resolved.

This is useful for:
- LSP to track file dependencies
- Incremental recompilation
- Build system integration

Currently not implemented because the single-source API doesn't do imports.

## Testing Notes

Run tests with:
```bash
./out/lx run lx/test/compiler-fiber.test.lx
```

TDD pattern used:
1. Write failing test (RED)
2. Implement minimal code to pass (GREEN)
3. Refactor if needed

Debug with `lx eval`:
```bash
LX_ROOT=lx ./out/lx eval '
let Compiler = import "src/compiler-fiber.lx"
let handle = Compiler.start("let x = 42", .{ filename: "test.lx" })
let result = Compiler.poll(handle.fiber, .{ maxEvents: 100 })
println(result)
'
```

## Architecture Decisions

1. **Single fiber per compilation** - Keeps state management simple
2. **Budget-based polling** - Allows driver to control responsiveness
3. **Events as return values** - The final `Events.done(result)` is returned, not yielded
4. **Pass-level granularity** - Progress events at pass boundaries, not per-node (yet)

## Future Stages (from fiber-patterns.md)

- **Stage 2:** Node-based budgets and cancellation
- **Stage 3:** Fiber-based module resolution handshake (`needModule` events)
- **Stage 4:** Shared traversal utilities

## Related Files

- `lx/docs/fiber-patterns.md` - Original design document
- `lx/src/driver.lx` - Current non-fiber driver (reference for pass ordering)
- `lx/src/passes/pipeline.lx` - Pass execution utilities
- `lx/examples/fiber-events-demo.lx` - Demo of the pattern with mock data
