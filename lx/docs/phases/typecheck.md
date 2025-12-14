# Typecheck Phase (typecheck.lx)

## Responsibility

Perform **best-effort, monomorphic static analysis** on resolved Lx programs.

This phase:

* infers stable types for variables, functions, records, and arrays
* detects incompatible usages (shape mismatches, conflicting assignments)
* supports closures and imports
* is **flow-insensitive**, **local**, and **fast**

It does **not**:

* affect runtime semantics
* mutate the AST
* participate in code generation
* attempt full soundness or polymorphism

This phase exists primarily to support:

* IDE / LSP diagnostics
* early error detection
* developer feedback loops

---

## Position in Pipeline

```
Source
  → parser.lx
  → lower.lx
  → resolve.lx
  → typecheck.lx
  → (optional) codegen.lx
```

**Important:** `typecheck` depends on `resolve` and must run *after* it.

---

## Inputs

```lx
typecheck(ast, resolveResult, opts)
```

### AST

* Lowered AST (canonical syntax, no Arrow nodes)
* Node IDs are stable and unique per module

### resolveResult (required)

Produced by `resolve.lx`:

* `nodes: { nodeId → AST node }`
* `resolvedNames: { nodeId → BindingInfo }`
* `scopeInfo: { nodeId → ScopeInfo }`

These are **authoritative** for:

* variable identity
* closure capture
* function boundaries
* import resolution

### opts (optional)

```lx
.{
  builtinTypes: BuiltinTypeTable,  // len, str, type, etc.
  importSignatures: SignatureLoader?,  // optional future extension
}
```

---

## Outputs

```lx
{
  success: bool,
  types: { nodeId → Type },
  errors: [{ nodeId, message, severity }],
}
```

### `types`

* Maps **node IDs** (not AST objects) to inferred types
* Includes:

  * identifiers
  * expressions
  * function nodes
  * return expressions

### `errors`

* Non-fatal unless configured otherwise
* Analysis continues after errors
* On error, offending type variables are widened to `Any`

---

## Type System

### Type Forms

```
Type =
  Unknown
  Any
  Number
  Bool
  String
  Nil
  Function(params: [Type], return: Type)
  Array(elem: Type)
  Record(fields: map<string, Type>)
  Tagged(tagField: string, cases: map<tagValue, Record>)
```

### Semantics

* **Unknown**: initial state; can be refined by constraints
* **Any**: escape hatch; always unifies, suppresses cascading errors
* All types are **monomorphic**
* Records and arrays are **frozen once shaped**

---

## Environments and Identity

### Type Variables

Each variable binding corresponds to **exactly one TypeVar**.

Source of truth:

* Variable identity is determined by `resolveResult`
* Captured variables reuse the **same TypeVar** as their outer binding

```lx
TypeEnv = {
  parent: TypeEnv?,
  vars: map<declNodeId, TypeVar>,
}
```

**Key invariant**: names are irrelevant here — only declaration identity matters.

---

## Constraint Model

Typechecking is a single traversal that:

1. creates fresh `TypeVar`s
2. emits constraints
3. unifies types eagerly
4. reports conflicts

This is **not** Hindley–Milner and does not generalize.

---

## Core Rules (Selected)

### Variable Declaration

```lx
let x = expr
```

Constraint:

```
T(x) ≈ T(expr)
```

---

### Assignment

```lx
x = expr
```

Constraint:

```
T(x) ≈ T(expr)
```

Mismatch → error → widen to `Any`

---

### Identifier Use

```lx
x
```

Constraint:

```
T(expr) = T(x)
```

Undefined variables should already be caught by `resolve`.

---

### Record Literals

```lx
.{ a: v1, b: v2 }
```

Type:

```
Record{ a: T(v1), b: T(v2) }
```

---

### Record Field Access

```lx
x.a
```

Rules:

* If `T(x) = Unknown` → becomes `Record{ a: Unknown }`
* If `T(x) = Record` and field missing → error
* Record shapes are **frozen once known**

---

### Arrays

```lx
[]
```

Creates:

```
Array(elem = Unknown)
```

```lx
push(xs, v)
```

Constraint:

```
T(xs) ≈ Array(elem = T(v))
```

---

## Functions and Closures

### Function Definition

```lx
fn f(a, b) { body }
```

Type:

```
Function([A, B], R)
```

* `A`, `B`, `R` are fresh TypeVars
* Parameters are bound in function TypeEnv

---

### Function Call

```lx
f(x, y)
```

Constraint:

```
T(f) ≈ Function([A, B], R)
T(x) ≈ A
T(y) ≈ B
T(call) = R
```

**Monomorphic rule**:

* First call constrains parameters
* Later calls must match

---

### Returns

```lx
return expr
```

Constraint:

```
T(expr) ≈ R
```

All returns unify to the same `R`.

---

### Closures

Captured variables:

* reuse the **same TypeVar**
* constraints propagate naturally across closures

No special handling required beyond resolve’s capture graph.

---

## Conditionals (Flow-Insensitive)

```lx
if cond { A } else { B }
```

* `cond` should be `Bool` (optional check)
* Analyze both branches
* Unify resulting variable types

This enforces **branch consistency** without control-flow tracking.

---

## Tagged Records (Optional Precision)

Two supported strategies:

### MVP (Heuristic)

* Detect tag fields (`kind`, `type`, etc.)
* Allow variant fields to widen to `Any`

### Precise (Future)

* Use `Tagged(tagField, cases)`
* Enforce per-tag record shapes
* Safe field access only when tag is known

**No syntax changes required**.

---

## Error Handling

On any type error:

* Report diagnostic
* Replace conflicting TypeVar with `Any`
* Continue analysis

Severity is configurable:

* `error` (default)
* `warning`
* `info`

---

## Guarantees

If `success == true`:

* No incompatible type constraints were found
* Inferred types are stable and usable for tooling

If `success == false`:

* Partial types are still available
* Diagnostics are complete and non-cascading

---

## Non-Goals

This phase intentionally does **not**:

* infer polymorphic types
* perform effect analysis
* reason about reachability
* affect runtime or bytecode

---

## Future Extensions

* Narrowing via pattern tests
* Better tagged-union precision
* IDE-only hints (dead code, unused vars)

---

## Summary

The typecheck phase provides:

* fast, predictable static analysis
* strong IDE support
* minimal implementation complexity
* zero coupling to backend decisions

It is a **frontend value multiplier**, not a gatekeeper.

