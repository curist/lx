# Claude Code Assistant Guide for lx

This document contains important information for AI assistants working with the lx programming language to avoid common mistakes and understand lx-specific behaviors.

## Quick Reference: Common Gotchas

1. **`type()` returns `"map"` not `"hashmap"`** ⚠️ MOST COMMON MISTAKE
2. **Imports are relative to project root** - determined by `.lxroot` marker file
3. **No `this` keyword** - explicitly reference objects
4. **Functions are hoisted** - mutual recursion is allowed, but you can't call a hoisted function before the block's hoisted function group is complete
5. **Closures capture locals via upvalues** - loop-variable captures can share the same slot; create a fresh `let` per iteration when you need per-iteration capture
6. **Implicit return + expressions** - functions/blocks return the last expression; most constructs (blocks/if/enum/etc.) are expressions
7. **No string interpolation** - use concatenation
8. **No `++`/`--` operators** - use `i = i + 1`
9. **Hashmaps use `.{ }` syntax** - dot prefix required
10. **Opcode/object changes may require bootstrapping** - see Bootstrapping section below
11. **`nameOf()` expects an `enum`** - it no longer accepts generic maps/records

## Communication Guidelines

**Focus on substance, not metrics:**
- ❌ Don't count: line numbers, test counts, assertion counts, file sizes, etc.
- ✅ Do explain: what changed, why it matters, how it works
- In documentation and commit messages, focus on **what/why/how**, not quantitative details
- Example: Instead of "Added 7 tests with 16 assertions", write "Added tests covering nested scopes, closures, and variable shadowing"

## Lx command line args

```sh
❯ ./out/lx
Usage:

  ./out/lx <command> [arguments]

Commands:
  run      Run source or lxobj
  eval     Evaluate expression (arg or stdin)
  repl     Start REPL (use :q to quit)
  compile  Compile source to lxobj (-o/--output <output>)
  disasm   Disassemble lxobj or lx source
  ast      Dump AST with metadata at different compilation stages (debug)
  check    Typecheck an entry module (dev)
  lsp      Start LSP server on stdio
  version  Print version
  help     Print this helpful page
```

## Type System

### The `type()` Function

**CRITICAL:** The `type()` function returns different strings than you might expect:

```lx
type(42)           // "number"
type("hello")      // "string"
type(true)         // "boolean"
type(nil)          // "nil"
type([1, 2, 3])    // "array"
type(.{ a: 1 })    // "map"       ⚠️ NOT "hashmap" or "object"!
type(fn() {})      // "fn"
type(enum { A })   // "enum"
```

**Debugging tip:**
```lx
Lx.stderr.println("Type of node:", type(node))  // Always check actual type string first
// Lx.stderr.println prints to stderr
```

### Hashmap Literals

Hashmaps in lx use `.{ }` syntax:
```lx
let obj = .{
  name: "foo",
  value: 42
}
```

Note: The dot prefix `.{` is required to distinguish from block expressions `{ }`.

## Scoping and Closures

### The `this` Keyword

There is no implicit `this` in lx. Methods must explicitly reference their parent object:

```lx
// ❌ WRONG
let obj = .{
  value: 42,
  getValue: fn() { this.value }  // 'this' is undefined
}

// ✅ CORRECT - capture the object reference
let obj = .{}
obj.value = 42
obj.getValue = fn() { obj.value }  // Explicitly reference 'obj'
```

## Language Features

### Keywords

Current keywords (see `lx/src/types.lx`):
- `and`, `or`
- `if`, `else`
- `fn`, `for`
- `nil`, `return`, `true`, `false`
- `let`, `break`, `continue`
- `import`, `in`
- `collect`
- `enum`

Keywords can be used as hashmap keys:
```lx
let x = .{ if: 1, and: 2 }  // Valid!
```

### Implicit Return

**Functions and blocks automatically return their last expression:**

```lx
fn add(a, b) {
  a + b  // Implicitly returned
}

fn max(a, b) {
  if a > b {
    a  // Implicitly returned from if branch
  } else {
    b  // Implicitly returned from else branch
  }
}

// Blocks are expressions
let result = {
  let x = 10
  let y = 20
  x + y  // Block evaluates to 30
}
```

**Explicit `return` for early exit:**
```lx
fn find(arr, target) {
  for let i = 0; i < len(arr); i = i + 1 {
    if arr[i] == target {
      return i  // Early return
    }
  }
  nil  // Implicitly returned if not found
}
```

**Return values:**
- `let x = value` returns `value` (the RHS)
- Assignments `x = value` return `value`
- `for` loops always return `nil`
- `break` returns `nil` (for loops are statements, not expressions)

```lx
fn example1() {
  let x = 5  // Returns 5 (the RHS)
}

fn example2() {
  for let i = 0; i < 3; i = i + 1 {
    i * 10  // Loop body executes, but loop returns nil
  }
  // Returns nil
}

fn example3() {
  let result = for let i = 0; i < 10; i = i + 1 {
    if i == 5 {
      break  // Exit loop early
    }
  }
  // result is nil
}
```

### Truthiness

**Only `nil` and `false` are falsy. Everything else is truthy:**

```lx
if nil { }        // falsy
if false { }      // falsy

if 0 { }          // truthy (!)
if "" { }         // truthy (!)
if [] { }         // truthy (!)
if .{} { }        // truthy (!)
```

**Idiomatic nil checking:**
```lx
let value = findSomething()

// Check if value exists
if value {
  use(value)
}

// Check if value is nil
if !value {
  handleMissing()
}
```

### For Loops

**C-style:**
```lx
for let i = 0; i < 10; i = i + 1 {
  println(i)
}
```

**While-style:**
```lx
for true {
  println("endless")
}
```

**For-in iteration:**
```lx
// Iterate over array elements
for item in [1, 2, 3] {
  println(item)
}

// With index
for item, i in ["a", "b", "c"] {
  println(i, item)  // 0 a, 1 b, 2 c
}
```

### Collect Expressions

Collect expressions build arrays by evaluating a body expression for each iteration:

**C-style collect:**
```lx
let squares = collect let i = 0; i < 5; i = i + 1 {
  i * i
}
// Result: [0, 1, 4, 9, 16]
```

**Collect-in (for-in style):**
```lx
let doubled = collect x in [1, 2, 3] {
  x * 2
}
// Result: [2, 4, 6]

// With index
let indexed = collect val, i in ["a", "b"] {
  .{ index: i, value: val }
}
// Result: [{index: 0, value: "a"}, {index: 1, value: "b"}]
```

**Type inference:**
- Collect expressions infer `Array[T]` where `T` is unified across all iterations
- All iterations must produce compatible types
```lx
collect i in range(3) { str(i) }  // Array[String]
collect i in range(3) { i * 2 }    // Array[Number]
```

**Early termination with `break`:**
- Using `break` in a collect expression exits the loop early
- The array contains all elements collected before the break
```lx
collect i in [1, 2, 3, 4, 5] {
  if i == 3 { break }
  i * 2
}
// Result: [2, 4] (collected before break at i=3)
```

**Skipping emission with `continue`:**
- Using `continue` in a collect expression skips emitting a value for that iteration
```lx
collect i in [1, 2, 3, 4] {
  if i == 2 { continue }
  i
}
// Result: [1, 3, 4]
```

### Operators

**Arrow operator `->`** for method chaining:
```lx
println(Color->nameOf(Color.Green))
```

### String Building

No string interpolation - use concatenation:
```lx
// ❌ WRONG
let msg = `Hello ${name}`

// ✅ CORRECT
let msg = "Hello " + name
```

## Standard Library

### Global Functions

Common globals (from `globals.lx`):
- `println(...)`, `print(...)`
- `len(x)` - array/string length
- `type(val)` - returns type string
- `str(val)` - convert to string
- `tonumber(str)` - convert string to number (e.g., `tonumber("42")` → `42`)
- `range(n)` or `range(string)` - create array [0..n-1] or UTF-8 character array
- `fold(arr, init, fn)` - reduce/fold
- `push(arr, val)` - append to array
- `keys(map)` - get hashmap keys
- `join(arr, sep)` - join strings
- `Math.floor(n)` - floor to integer
- `Lx.stderr.print(...)`, `Lx.stderr.println(...)`

**Number conversion:**
```lx
// String to number: use tonumber()
let n = tonumber("42")  // 42

// Double to int: use Math.floor()
let i = Math.floor(3.14)  // 3

// ❌ WRONG - Math.floor() doesn't parse strings
let x = Math.floor("42")  // Error!
```

## Import System

**Module Resolution:**

Imports are resolved relative to the **project root**, which is discovered by:
1. Starting from the **entry file's directory** (not cwd)
2. Walking up parent directories to find a `.lxroot` marker file
3. Using that directory as the project root
4. If no `.lxroot` found, falling back to `LX_ROOT` env var or entry file's directory

```lx
// These paths are resolved relative to project root
let parser = import "src/parser.lx"     // Resolves to {projectRoot}/src/parser.lx
let suite = import "test/helpers.lx"    // Resolves to {projectRoot}/test/helpers.lx
```

**Example:**
- Entry file: `lx/scripts/build.lx`
- Entry file directory: `lx/scripts/`
- Search for `.lxroot`: walks up to find `lx/.lxroot`
- Project root: `lx/`
- Import `"src/driver.lx"` → resolves to `lx/src/driver.lx`

**Module exports:**
- If a file exports just a function: `parse` (last line), import it directly:
  ```lx
  let parse = import "src/parser.lx"
  parse(source, "test.lx")  // Call directly
  ```
- If a file exports a hashmap: `.{ foo: fn() {}, bar: fn() {} }`, access properties:
  ```lx
  let module = import "src/module.lx"
  module.foo()  // Access as properties
  ```

## Testing and Development Workflow

### Test Framework

Tests use a custom test framework:
```lx
let suite = (import "test/makeTestSuite.lx")()
let test = suite.defineTest

test("test name", fn(assert) {
  assert.equal(actual, expected, "optional message")
  assert.truthy(value, "optional message")
})

suite.run()
```

**Available assertions:**
- `assert.equal(actual, expected)` - Compare values
- `assert.truthy(value)` - Check if value is truthy
- No `assert.falsy()` - use `assert.truthy(!value)` instead

### Running Tests with Makefile

Run from repo root:

```bash
# Run with the in-repo compiler (./out/lx)
make test

# Or run an individual test
./out/lx run lx/test/parser.test.lx
```

**Common workflow:**
```bash
# Build + test from repo root
make
make test
```

## Debugging Compiler Passes with `ast` Command

The `ast` command is a powerful tool for debugging and understanding compilation passes. It provides multiple modes for inspecting AST transformations.

### Quick Overview

```bash
# Dump AST with metadata (default: runs through anf-inline)
./out/lx ast file.lx

# Dump AST after a specific pass
./out/lx ast --pass parse file.lx
./out/lx ast --pass anf file.lx

# Show pipeline summary (node counts, stats)
./out/lx ast --summary file.lx

# Track a specific node through all passes
./out/lx ast --track-node 42 file.lx

# Compare two passes (diff mode)
./out/lx ast --diff parse anf file.lx

# View metadata documentation
./out/lx ast --help-metadata
```

### Metadata Annotations

The ast command enriches nodes with metadata from compilation passes:

**Origin tracking** (lower, anf, lower-loops):
```
Binary #80 [origin: #43]  # Node #80 came from node #43 in previous pass
```

**Name bindings** (resolve):
```
Identifier #50 [local slot=1 decl=#49 depth=2]     # Local variable
Identifier #50 [upvalue idx=0 decl=#37 depth=0]    # Captured variable (closure)
Identifier #51 [builtin name=println]              # Built-in function
```

**Scope information** (resolve):
```
Function #45 [scope function depth=1 locals=1 upvalues=1]
Block #47 [scope block depth=2 locals=1]
```

### Use Cases

**1. Understanding ANF Transformations**

When ANF splits complex expressions into temporaries:
```bash
# See what ANF does to your code
./out/lx ast --diff parse anf file.lx
```

Output shows which nodes split and how:
```
Transformed nodes: 9
  ~ Binary (operator) #9
    → split into 5 nodes (Let, Identifier, Block, Identifier, Binary)
```

**2. Tracking Node Transformations**

When debugging why a specific node changed:
```bash
# First, find the node ID in parse output
./out/lx ast --pass parse file.lx | grep -A2 "Binary"

# Then track it through all passes
./out/lx ast --track-node 9 file.lx
```

Shows the complete evolution:
```
[parse]
  Binary (operator) #9
  Line: 1

[lower]
  Binary (operator) #41
  Line: 1

[anf]
  Split into 5 nodes:
    - Let #95
    - Identifier '$anf.2' #96
    - Block [synthetic] #93
```

**3. Debugging Scope/Binding Issues**

When variables aren't resolving correctly:
```bash
# Check what identifiers resolve to
./out/lx ast --pass resolve file.lx | grep "Identifier.*name: x"
```

Output shows binding information:
```
Identifier #50 [upvalue idx=0 decl=#37 depth=0]
  name: x
```

This tells you:
- It's a captured variable (upvalue)
- At upvalue index 0
- Declared at node #37
- Accessed from scope depth 0

**4. Quick Pipeline Overview**

To see what each pass does without verbose output:
```bash
./out/lx ast --summary file.lx
```

Shows concise statistics:
```
parse:
  Nodes: 37 (+37)

lower:
  Nodes: 37

anf:
  Nodes: 61 (+24)
  Synthetic temporaries: 3

resolve:
  Nodes: 61
  Bindings: 8 locals, 1 upvalues, 3 builtins
  Max scope depth: 4

anf-inline:
  Nodes: 61
```

**5. Debugging Pass Bugs**

When a pass produces unexpected results:

```bash
# Compare before and after the buggy pass
./out/lx ast --diff lower anf file.lx

# Track a specific problematic node
./out/lx ast --track-node 15 file.lx

# See the full AST at the buggy pass
./out/lx ast --pass anf file.lx
```

### Common Debugging Workflows

**ANF not transforming correctly:**
1. Use `--diff parse anf` to see what changed
2. Use `--track-node N` to follow the problematic expression
3. Look for `[synthetic]` nodes and verify they're correct

**Variable binding issues:**
1. Use `--pass resolve` to see binding metadata
2. Look for `[local]`, `[upvalue]`, or `[builtin]` annotations
3. Check scope depth and declaration node IDs

**Pass adding unexpected nodes:**
1. Use `--diff fromPass toPass` to see additions
2. Look for "Added nodes" section
3. Check if they're marked `[synthetic]`

**Node disappearing:**
1. Use `--track-node N` to see where it goes
2. Look for "Node not found" messages
3. Check if it was removed or merged

## Bootstrapping

The lx compiler is **self-hosted** - it's written in lx and compiles itself. The embedded bytecode headers (`include/lx/lxlx.h` and `include/lx/lxglobals.h`) contain the compiled compiler and must be regenerated when core compiler code changes.

### When to Bootstrap

Regenerate headers when you modify:
- Compiler source files (`lx/src/`, `lx/main.lx`, `lx/globals.lx`)
- Module resolution logic
- Bytecode format or opcodes
- Object file format

If you made **non-backward-compatible opcode/bytecode changes**, the normal `make` flow can break because the embedded compiler bytecode may no longer run on the updated VM. In that case, follow the bootstrap flow in `lx/scripts/README.md` (use `lx/scripts/bootstrap-codegen.lx` to produce fresh bytecode first, then build the C VM with those headers).

### How to Bootstrap

**Normal build** (uses system `lx` compiler):
```bash
make          # Builds ./out/lx using embedded headers
make test     # Runs tests
```

**Bootstrap build** (uses newly built compiler to rebuild itself):
```bash
# Step 1: Build with system compiler
make -B prepare build

# Step 2: Rebuild using the newly built compiler, do it twice
LX=./out/lx make -B prepare build
LX=./out/lx make -B prepare build

# Step 3: Verify the build is stable
make test
```

**What happens during bootstrapping:**
1. `scripts/build-lxlx.sh` (and `scripts/build-globals.sh`) refresh bundled builtin docs (`scripts/gen-builtin-docs.lx`)
2. They compile `lx/main.lx` / `lx/globals.lx`:
   - Fast path: `./out/lx compile ... > out/{lxlx,lxglobals}-new.lxobj`
   - Fallback path (non-repo `lx`): `lx run lx/scripts/bootstrap-codegen.lx ...` → `/tmp/{main,globals}.lxobj` (then copied into `out/`)
3. They convert the `.lxobj` into a C header via `xxd -i` (`include/lx/lxlx.h`, `include/lx/lxglobals.h`)
4. The C build links those headers into the `./out/lx` binary

**Debugging bootstrap issues:**
```bash
# Test build script directly
lx run lx/scripts/bootstrap-codegen.lx lx/main.lx

# Use new compiler to compile a simple file
./out/lx run /tmp/test.lx

# Verify module resolution
./out/lx compile lx/main.lx
```

**Common issues:**
- **Path doubling** (`lx/lx/main.lx`) - Check entry file paths in build scripts use `lx/` prefix
- **Wrong project root** - Ensure `.lxroot` exists in `lx/` directory
- **Infinite loops** - Check for circular dependencies in module resolution
- **Build hangs** - Set timeout when testing: `timeout 30 make prepare build`

### Module Resolution in Build Scripts

Build scripts in `lx/scripts/` must use `forEntry()` with correct paths:

```lx
// ✅ CORRECT (from repo root)
let resolver = ModuleResolution.forEntry("lx/main.lx")

// ✅ CORRECT (from within `lx/`)
let resolver = ModuleResolution.forEntry("main.lx")

// ❌ WRONG - Wrong prefix (path doubling)
let resolver = ModuleResolution.forEntry("lx/lx/main.lx")
```

The resolver finds `.lxroot` by walking up from the entry file's directory.
