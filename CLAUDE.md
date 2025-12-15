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
println("Type of node:", type(node))  // Always check actual type string first
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

### For Loops

**C-style:**
```lx
for let i = 0; i < 10; i = i + 1 {
  println(i)
}
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

