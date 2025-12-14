# Test Specification

This document defines required tests to validate compiler correctness.

## Order Preservation Tests

**Critical invariant**: Function constant order must match `objbuilder.lx` traversal order.

`objbuilder` walks the constant graph starting from main:
1. Scans `main.chunk.constants` in order
2. Enqueues each `OBJ_FUNCTION` found
3. Recursively processes each function's `chunk.constants` (DFS/preorder-like)

**Expected order**: The order obtained by this traversal, NOT just source order.

### Sequential Functions

**Purpose**: Verify function constants appear in traversal order

```lx
{
  fn a() { 1 }
  fn b() { 2 }
  fn c() { 3 }
}
```

**Expected**:
- Codegen emits `OP.CLOSURE` in source order: a, b, c
- Main's `chunk.constants` contains `OBJ_FUNCTION(a)`, `OBJ_FUNCTION(b)`, `OBJ_FUNCTION(c)` in that order
- `objbuilder` traversal encounters them in order: a, b, c
- Chunk order matches: a, b, c
- Loader patches correctly

### Nested Functions

**Purpose**: Verify traversal order for nested functions

```lx
{
  fn outer() {
    fn inner1() { 1 }
    fn inner2() { 2 }
    inner2()
  }
  outer()
}
```

**Expected**:
- Codegen compiles `outer`, which:
  1. Compiles `outer`'s body
  2. Encounters `inner1` → emits `OP.CLOSURE` → adds `OBJ_FUNCTION(inner1)` to `outer.chunk.constants[0]`
  3. Encounters `inner2` → emits `OP.CLOSURE` → adds `OBJ_FUNCTION(inner2)` to `outer.chunk.constants[1]`
- Main's constants: `[OBJ_FUNCTION(outer)]`
- `objbuilder` traversal:
  1. Process main.constants[0] = `outer` → enqueue `outer`
  2. Process outer.constants[0] = `inner1` → enqueue `inner1`
  3. Process outer.constants[1] = `inner2` → enqueue `inner2`
- Final order: outer, inner1, inner2
- Loader patches correctly

### Multiple Nesting Levels

```lx
{
  fn a() {
    fn b() {
      fn c() { 1 }
      c()
    }
    b()
  }
  a()
}
```

**Expected**: Order a, b, c

## Hoisting Tests

### Legal: Mutual Recursion

```lx
{
  fn a() { b() }
  fn b() { a() }
  a()
}
```

**Expected**:
- Resolves successfully
- No semantic errors
- Function constants order: a, b
- Runtime: mutual recursion works

### Legal: Call After All Declarations

```lx
{
  fn a() { b() }
  fn b() { c() }
  fn c() { 1 }
  a()
}
```

**Expected**:
- Resolves successfully
- Call at index 3 >= lastHoistedFunctionIndex (2)

### Illegal: Block-Level Call Before Declaration

```lx
{
  a()
  fn a() { 1 }
}
```

**Expected**:
- Semantic error
- Message: "Cannot use function 'a' before its declaration"
- Codegen not run

### Illegal: Call Before Mutual Recursion Group Complete

```lx
{
  fn a() { b() }
  a()
  fn b() { a() }
}
```

**Expected**:
- Semantic error
- Message: "Cannot use hoisted function 'a' before all hoisted functions are declared"
- Reason: exprIndex (1) < lastHoistedFunctionIndex (2)

### Legal: Deep Mutual Recursion

```lx
{
  fn a() { b() }
  fn b() { c() }
  fn c() { a() }
  a()
}
```

**Expected**: Resolves successfully

### Illegal: Let Doesn't Hoist

```lx
{
  a()
  let a = fn() { 1 }
}
```

**Expected**:
- Semantic error
- Message: "Undefined variable 'a'"

### Edge: Function Inside Function

```lx
{
  fn outer() {
    fn inner() { outer() }
    inner()
  }
  outer()
}
```

**Expected**:
- Resolves successfully
- Inner can reference outer (enclosing scope)

## Module Chunk Classification Tests

### Module Root Has Empty Name

```lx
// In compile(src, "foo.lx")
```

**Expected**:
- Top-level function has `name == ""`
- `arity == 0`
- `chunk.filename == "foo.lx"` (canonical)

### User Functions Have Non-Empty Names

```lx
fn foo() { 1 }
```

**Expected**:
- Function has `name == "foo"`
- NOT empty string

### Anonymous Functions Have Name "fn"

```lx
{
  let f = fn() { 1 }
  f()
}
```

**Expected**:
- Anonymous function has `name == "fn"` (non-empty!)
- NOT empty string
- NOT treated as module chunk
- NO module dedup/REF behavior

### Multiple Anonymous Functions Don't Dedup as Modules

```lx
{
  let a = fn() { 1 }
  let b = fn() { 2 }
}
```

**Expected**:
- Two distinct `OBJ_FUNCTION` constants
- Two distinct chunks
- Both have `name == "fn"`
- NO module REF logic triggered (only empty string triggers that)

### Path Canonicalization

**Test**: Import same module with different paths

```lx
// In a.lx
import "./b.lx"
import "./subdir/../b.lx"  // Same file, different path
```

**Expected**:
- Both resolve to same canonical path
- `builtModuleCache` dedup works
- REF chunk emitted for second import
- NOT duplicate ACTUAL chunks

## Import Tests

### Simple Import

```lx
// a.lx
import "b.lx"

// b.lx
1 + 1
```

**Expected**:
- b.lx compiled first
- Module function cached
- Import expression evaluates to module result

### Circular Import Detection

```lx
// a.lx
import "b.lx"

// b.lx
import "a.lx"
```

**Expected**:
- Semantic error
- Message: "Circular import: a.lx (status: resolving)"

### Import Deduplication

```lx
// a.lx
import "c.lx"
import "c.lx"

// c.lx
42
```

**Expected**:
- c.lx compiled once
- Same function object reused
- REF chunk for second import

### Import Before Use

```lx
// a.lx
let math = import "math.lx"
math.add(1, 2)
```

**Expected**: Works correctly

## Arrow Lowering Tests

### Simple Arrow

```lx
x->f()
```

**Expected**: Lowers to `f(x)`

### Arrow with Arguments

```lx
x->f(a, b)
```

**Expected**: Lowers to `f(x, a, b)`

### Chained Arrows

```lx
x->a()->b()->c()
```

**Expected**:
1. `a(x)->b()->c()`
2. `b(a(x))->c()`
3. `c(b(a(x)))`

### Invalid: Arrow Without Call

```lx
x->42
```

**Expected**:
- Lowering error
- Message: "Arrow operator requires function call on right side"
- Error recovery: returns `x`

### Arrow with Complex Left Side

```lx
f(x)->g()
```

**Expected**: Lowers to `g(f(x))`

## Semantic Validation Tests

### Undefined Variable

```lx
x
```

**Expected**:
- Semantic error
- Message: "Undefined variable 'x'"

### Duplicate Declaration

```lx
{
  let x = 1
  let x = 2
}
```

**Expected**:
- Semantic error
- Message: "Variable 'x' already declared in this scope"

### Read Before Initialization

```lx
let x = x
```

**Expected**:
- Semantic error
- Message: "Can't read local variable in its own initializer"

### Return at Block Level

```lx
{
  return 1
  42
}
```

**Expected**:
- Semantic error
- Message: "Can only return at end of block"

### Return Not at End

```lx
fn f() {
  return 1
  42  // Dead code after return
}
```

**Expected**:
- Semantic error
- Message: "Can only return at end of block"

### Break Outside Loop

```lx
break
```

**Expected**:
- Semantic error
- Message: "Can only break inside a loop"

### Continue Outside Loop

```lx
continue
```

**Expected**:
- Semantic error
- Message: "Can only continue inside a loop"

## Constant Pool Tests

### No Function Deduplication

```lx
{
  let f1 = fn() { 1 }
  let f2 = fn() { 1 }  // Identical function
}
```

**Expected**:
- Two separate `OBJ_FUNCTION` constants
- Two separate chunks
- NO deduplication

### String Interning OK (Future)

```lx
{
  let s1 = "hello"
  let s2 = "hello"
}
```

**Expected** (when string interning added):
- Same string constant reused
- Only one "hello" in constant pool

## Regression Tests

All existing compiler.lx tests must pass with new pipeline.

**Success criteria**:
- Same output (not same bytecode)
- Same runtime behavior
- Same error messages (or better)

**Test categories**:
- Arithmetic and operators
- Variables and scoping
- Functions and closures
- Control flow (if, for, while)
- Imports and modules
- Error cases

## Performance Benchmarks

### Compilation Speed

**Test**: Compile large file (1000+ lines)

**Acceptance**: < 10% slower than compiler.lx

### Runtime Performance

**Test**: Execute compiled programs

**Acceptance**: No runtime performance difference (bytecode semantics preserved)

## Unit Test Structure

### Parser Tests

- ✅ Node IDs are unique within module
- ✅ Node IDs are monotonic
- ✅ All node types get IDs
- ✅ Arrow nodes preserved
- ✅ Only syntax errors reported
- ❌ No semantic validation

### Lower Tests

- ✅ Arrow → Call transformation correct
- ✅ Position spans copied
- ✅ Provenance map maintained
- ✅ Error recovery for invalid arrows
- ✅ Node IDs continue from parse
- ✅ Golden AST tests (before/after)

### Resolve Tests

- ✅ Scope management (begin/end, nesting)
- ✅ Local resolution
- ✅ Upvalue resolution
- ✅ Global fallback
- ✅ Function hoisting (mutual recursion)
- ✅ All semantic checks (undefined, duplicates, etc.)
- ✅ Import handling
- ✅ Circular import detection
- ✅ Error collection with node IDs
- ✅ Side tables correct

### Codegen Tests

- ✅ Output equivalence with compiler.lx
- ✅ Mechanical opcode emission
- ✅ Closure handling
- ✅ Import emission
- ✅ Scope cleanup (reverse order)
- ✅ POP_LOCAL vs CLOSE_UPVALUE
- ✅ Module root has `name == ""`
- ✅ User functions have non-empty names

### Integration Tests

- ✅ Full pipeline: source → bytecode → run
- ✅ Error propagation
- ✅ Import caching
- ✅ Multi-file programs
- ✅ All language features
