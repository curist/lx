# Fiber-Based Compiler API - Hand-off Notes

This document captures the current state and next steps for the fiber-based compiler API (Stage 1 of `fiber-patterns.md`).

**Status: Stage 1 Complete** ✅

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

## Stage 1 Completion Summary

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

### 3. ~~Wire to Driver for Module-Level Compilation~~ ✅ DONE

`Compiler.startModule(driver, path, opts)` now handles full module compilation with imports:

```lx
let Driver = import "src/driver.lx"
let Compiler = import "src/compiler-fiber.lx"

let driver = Driver.make(.{
  loadSource: fn(path) { ... }
})

let handle = Compiler.startModule(driver, "main.lx", .{})
let result = Compiler.poll(handle.fiber, .{ maxEvents: 200 })
// result.result.result.function contains bytecode
// result.result.result.compiledModules lists all compiled modules
```

**Features:**
- Uses driver's `buildModule` for full compilation with import resolution
- Handles import caching (via driver's cache)
- Handles circular import detection (via driver)
- Returns ERROR event for missing entry file
- Returns DONE with success: false for compilation failures

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

### 5. ~~Add DEPS Events for Import Tracking~~ ✅ DONE

`startModule` now yields `DEPS` events for all compiled modules:

```lx
// After polling, look for DEPS events in result.events
for evt in result.events {
  if evt.kind == Events.EventKind.DEPS {
    println(evt.file, "depends on", evt.deps)
  }
}
```

**Behavior:**
- Emits a DEPS event for each compiled module (entry + all imports)
- Dependencies are extracted from resolve pass's `importInfoByNodeId`
- Useful for LSP dependency tracking and incremental recompilation

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

**Test coverage:**
- Tests 1-18: Single-source compilation (`Compiler.start`)
- Tests 19-24: Driver integration (`Compiler.startModule`)
  - Basic API shape and bytecode generation
  - Import handling and DEPS events
  - Error handling for missing files

## Architecture Decisions

1. **Single fiber per compilation** - Keeps state management simple
2. **Budget-based polling** - Allows driver to control responsiveness
3. **Events as return values** - The final `Events.done(result)` is returned, not yielded
4. **Pass-level granularity** - Progress events at pass boundaries, not per-node (yet)
5. **Driver delegation** - `startModule` delegates to driver's `buildModule` for import handling

## LSP Integration

The LSP (`lx/services/lsp.lx`) now uses `CompilerFiber.startModule` for compilation:

**Changes:**
- `flushCompileNow` uses fiber-based compilation with cancellation support
- Active compile is tracked in `state.activeCompile`
- Previous compile is cancelled when a new compile starts
- DEPS events update the dependency graph via `updateDepsFromEvent`

**Benefits:**
- Cancellation: If user edits quickly, in-progress compiles are cancelled
- Cleaner dep tracking: DEPS events provide explicit dependency information

**Limitations (until Stage 2):**
- Compilation still blocks (polls to completion synchronously)
- Diagnostics still come from driver cache, not streamed events

## Future Stages (from fiber-patterns.md)

- **Stage 2:** Node-based budgets and cancellation (streaming diagnostics)
- **Stage 3:** Fiber-based module resolution handshake (`needModule` events)
- **Stage 4:** Shared traversal utilities

## Related Files

- `lx/docs/fiber-patterns.md` - Original design document
- `lx/src/driver.lx` - Driver used by `startModule` for compilation
- `lx/src/passes/pipeline.lx` - Pass execution utilities
- `lx/services/lsp.lx` - LSP server using fiber-based compilation
- `lx/examples/fiber-events-demo.lx` - Demo of the pattern with mock data
