# Claude Code Assistant Guide for lx

This document contains important information for AI assistants working with the lx programming language to avoid common mistakes and understand lx-specific behaviors.

## Quick Reference: Common Gotchas

1. **`type()` returns `"map"` not `"hashmap"`** ⚠️ MOST COMMON MISTAKE
2. **Imports are relative to cwd**, not the importing file
3. **No `this` keyword** - explicitly reference objects
4. **Forward declarations required** for mutual recursion (until Phase 10+)
5. **Closure capture by value** for primitives - use objects for mutable state
6. **No string interpolation** - use concatenation
7. **No `++`/`--` operators** - use `i = i + 1`
8. **Hashmaps use `.{ }` syntax** - dot prefix required

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

The commands are:
  run          Run source or lxobj
  eval         Evaluate expression
  repl         Start REPL
  compile      Compile source to lxobj (-o/--output <output>)
  disasm       Disassemble lxobj or lx source
  version      Print version
  help         Print this helpful page
```

## Type System

### The `type()` Function

**CRITICAL:** The `type()` function returns different strings than you might expect:

```lx
type(42)           // "number"
type("hello")      // "string"
type(true)         // "bool"
type(nil)          // "nil"
type([1, 2, 3])    // "array"
type(.{ a: 1 })    // "map"       ⚠️ NOT "hashmap" or "object"!
type(fn() {})      // "function"
```

**Debugging tip:**
```lx
groanln("Type of node:", type(node))  // Always check actual type string first
// groanln print to stderr
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
- `import`

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

### Operators

**Arrow operator `->`** for method chaining:
```lx
result = value->map(fn(x) { x + 1 })->filter(fn(x) { x > 5 })
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
- `groanln(...)`, `groan(...)`
- `len(arr)` - array/string/hashmap length
- `type(val)` - returns type string
- `str(val)` - convert to string
- `int(val)` - convert to integer
- `range(n)` or `range(string)` - create array [0..n-1] or UTF-8 character array
- `map(arr, fn)` - map function
- `fold(arr, init, fn)` - reduce/fold
- `each(arr, fn)` - forEach
- `push(arr, val)` - append to array
- `keys(map)` - get hashmap keys
- `join(arr, sep)` - join strings

## Import System

**CRITICAL:** Imports are resolved relative to **current working directory (cwd)**, NOT relative to the importing file.

```lx
// These paths are resolved from cwd
let parser = import "src/parser.lx"     // Resolves to {cwd}/src/parser.lx
let suite = import "test/helpers.lx"    // Resolves to {cwd}/test/helpers.lx
```

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

The `lx/lx` directory contains a Makefile for running tests. The default compiler is `../out/lx`, but you can override it using the `LX` variable:

```bash
# Run with default compiler (../out/lx)
make test

# Run with specific compiler version
LX=../out/lx make test

# Other useful targets
LX=../out/lx make runall      # Run all stub tests
```

**Common workflow:**
```bash
# Build a specific compiler version and run tests
cd /path/to/lx/lx
# ... make changes to compiler ...
make  # builds ../out/lx

cd lx
LX=../out/lx make test  # Run tests with the newly built compiler
```
