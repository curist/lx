# Phase 1 Typechecker — Test Plan

## 1. Purpose

This document defines the **test contract for Phase 1** of the Lx typechecker.

Phase 1 is a **minimal, monomorphic, eager-unification** typechecker.
Its purpose is *not* to be expressive or clever, but to be:

* Deterministic
* Predictable
* Stable under refactoring
* Easy to reason about

Tests in this plan **must not assume any Phase 2+ features**.

---

## 2. Phase 1 Invariants (What Tests Must Enforce)

These invariants are **non-negotiable**.
If a future change breaks one of these tests, it is a regression.

### 2.1 No Flow Sensitivity

* No branch-local refinement
* No narrowing
* No join logic
* No Option wrapping

```lx
let x = 1
if cond { x = "s" }
x
```

✅ Phase 1 result: **type error or Any**
❌ Phase 2-style narrowing is forbidden

---

### 2.2 No Polymorphism

* Every binding has exactly **one monomorphic type**
* No generalization
* No instantiation

```lx
fn id(x) { x }
let a = id(1)
let b = id("s")
```

✅ Phase 1: error or `Any`
❌ Phase 2+/HM behavior is forbidden

---

### 2.3 Usage-Driven Inference Only

Definitions alone do **not** force concrete types.

```lx
fn f(x) { x }
f
```

✅ Result: `Unknown` / `Any`
❌ Inferring `(Any) -> Any` “just because”

---

### 2.4 Eager Unification

* Constraints apply immediately
* No deferred solving
* No second pass

If unification fails → error **now**, not later.

---

## 3. Test Structure

Each test should include:

1. **Source code**
2. **Expected type per node (or key bindings)**
3. **Expected errors (if any)**

Golden tests are preferred.

Example format:

```text
=== source ===
fn add(a, b) { a + b }
let x = add(1, 2)
x

=== expected ===
x : Number
add : (Number, Number) -> Number
errors: []
```

---

## 4. Core Test Categories

### 4.1 Literals

#### Test: numeric literal

```lx
1
```

Expected:

* type: `Number`
* no errors

#### Test: string literal

```lx
"hello"
```

Expected:

* type: `String`

---

### 4.2 Binary Operators

#### Test: valid arithmetic

```lx
1 + 2
```

Expected:

* type: `Number`
* no errors

#### Test: invalid arithmetic

```lx
1 + "x"
```

Expected:

* error: “Expected number”
* result type: `Any` (or equivalent fallback)

---

### 4.3 Identifiers & Let Bindings

#### Test: simple let

```lx
let x = 1
x
```

Expected:

* `x : Number`

#### Test: let without initializer

```lx
let x
x
```

Expected:

* `x : Unknown` / `Any`
* no crash

---

### 4.4 Functions — Definition Only

#### Test: unconstrained function

```lx
fn f(x) { x }
f
```

Expected:

* `f : Unknown`
* no errors

This test is **critical**: it locks in usage-driven inference.

---

### 4.5 Functions — Called

#### Test: constrained by call

```lx
fn add(a, b) { a + b }
add(1, 2)
```

Expected:

* `add : (Number, Number) -> Number`
* call result: `Number`

---

### 4.6 Function Argument Errors

#### Test: wrong argument type

```lx
fn add(a, b) { a + b }
add(1, "x")
```

Expected:

* error on argument
* result type: `Any` (or error-only)

---

### 4.7 If Expressions (Phase 1 Semantics)

#### Test: if with else, compatible

```lx
if cond { 1 } else { 2 }
```

Expected:

* type: `Number`

#### Test: if with else, incompatible

```lx
if cond { 1 } else { "x" }
```

Expected:

* error: branch mismatch

#### Test: if without else

```lx
if cond { 1 }
```

Expected:

* type: `Nil`
* **not Option(Number)**

This test must fail if Phase-2 semantics leak in.

---

### 4.8 No Narrowing / No Guards

#### Test: nil check does nothing

```lx
let x = 1
if x == nil { }
x
```

Expected:

* `x : Number` or `Any`
* no narrowing

---

### 4.9 Records (Closed Shape)

#### Test: record literal

```lx
let r = .{ a: 1, b: 2 }
r.a
```

Expected:

* `r : { a: Number, b: Number }`
* `r.a : Number`

#### Test: missing field

```lx
let r = .{ a: 1 }
r.b
```

Expected:

* error: field not found

---

### 4.10 Failure Safety Tests

These tests ensure the checker **never crashes**.

#### Test: recursive function

```lx
fn f(x) { f(x) }
```

Expected:

* no infinite loop
* either `Unknown` or error

#### Test: self-referential unification

```lx
fn f(x) { x(x) }
```

Expected:

* occurs-check error
* no stack overflow

---

## 5. Explicit Non-Tests (Forbidden in Phase 1)

The following **must not** appear in Phase-1 tests:

* Option / nullable behavior
* Nil-joining
* Branch refinement
* Tagged unions
* Map indexing
* Polymorphic reuse
* Import type replay

If a test *needs* these, it belongs to Phase 2+.

---

## 6. Test Completion Criteria

Phase 1 is considered **complete and frozen** when:

* All tests above pass
* No test relies on undocumented behavior
* Running Phase-2 code against Phase-1 tests still passes

After this point, **Phase-1 code should be treated as immutable**.

---

## 7. Guiding Principle

> Phase-1 tests protect correctness, not convenience.

A result of `Unknown` is **acceptable**.
A crash, hang, or silent mis-unification is **not**.
