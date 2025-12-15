# Typecheck Phase (typecheck.lx)

## Responsibility

Perform **best-effort static analysis** on resolved Lx programs.

This phase:

* infers stable types for variables, functions, records, and arrays
* detects incompatible usages (shape mismatches, conflicting assignments)
* supports closures, imports, and higher-order functions
* is **mostly monomorphic**, with **limited, explicit polymorphism**
* is **flow-insensitive**, **local**, and **fast**

It does **not**:

* affect runtime semantics
* mutate the AST
* participate in code generation
* attempt full soundness or full Hindley–Milner inference

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
  builtinTypes: BuiltinTypeTable,     // len, push, str, etc.
  importSignatures: SignatureLoader?, // optional future extension
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
  Nil
  Number
  Bool
  String
  TypeVar(id)

  Function(params: [Type], return: Type)
  Array(elem: Type)
  Record(fields: map<string, Type>)
  Tagged(tagField: string, cases: map<tagValue, Record>)
```

### Type Schemes (Limited Polymorphism)

```
Scheme =
  Forall(typeVars: [TypeVar], type: Type)
```

### Semantics

* **Unknown**: initial state; can be refined by constraints
* **Any**: escape hatch; unifies with everything, suppresses cascades
* All **ordinary variables are monomorphic**
* **Only function literals may be generalized**
* Records and arrays are **frozen once shaped**

This is **not** full Hindley–Milner.

---

## Environments and Identity

### Type Bindings

Each variable binding corresponds to exactly one entry:

```
TypeBinding =
  Mono(TypeVar)   // assignable, monomorphic
  Poly(Scheme)    // immutable, instantiable
```

### Type Environment

```lx
TypeEnv = {
  parent: TypeEnv?,
  vars: map<declNodeId, TypeBinding>,
}
```

**Key invariants:**

* Variable identity is determined solely by `resolveResult`
* Names are irrelevant during typechecking
* Captured variables reuse the **same binding** (Mono or Poly)

---

## Constraint Model

Typechecking is a single traversal that:

1. creates fresh `TypeVar`s
2. emits constraints
3. unifies eagerly
4. reports conflicts

There is **no generalization except at `let`-bound function literals**.

---

## Polymorphism Model (Intentional and Restricted)

Lx supports **limited parametric polymorphism** via:

* `Poly(Scheme)` bindings
* per-use instantiation
* a strict value restriction

### Design Intent

This exists to support:

* prelude helpers (`map`, `each`, `fold`, etc.)
* higher-order callbacks
* adapter functions (`_1`, `_2`, `_3`, …)

It does **not** turn Lx into a fully polymorphic language.

---

## Free Variables, Generalization, Instantiation

### Free Type Variables

* `ftv(type)` → free type variables in a `Type`
* `ftv(binding)` → free vars in Mono or Poly
* `ftv(env)` → union of all reachable bindings

### Generalization

```
generalize(env, type) =
  let qs = ftv(type) - ftv(env)
  Poly(Forall(qs, type))
```

Only performed for **function literals**.

### Instantiation

```
instantiate(Forall([t1..tn], T)) =
  let [u1..un] = fresh TypeVars
  substitute(T, { t1 → u1, ... })
```

Performed **at every identifier use** of a polymorphic binding.

---

## Core Rules

### Let Binding

```lx
let x = expr
```

Procedure:

1. Analyze `expr`, producing `T(expr)`.

2. If `expr` is a **function literal** (`fn (...) { ... }`):

   * Bind `x → generalize(env, T(expr))`.

3. Otherwise:

   * Create fresh `TypeVar tv`.
   * Bind `x → Mono(tv)`.
   * Emit constraint: `tv ≈ T(expr)`.

### Top-Level Function Declarations

```lx
fn f(a, b) { body }
```

Treated equivalently to:

```lx
let f = fn(a, b) { body }
```

For purposes of **generalization only**.

---

### Reassignment

```lx
x = expr
```

Rules:

* If `x` resolves to `Mono(tv)`:

  ```
  tv ≈ T(expr)
  ```

* If `x` resolves to `Poly(_)`:

  * Report error: *“Cannot assign to polymorphic binding”*
  * Widen assignment result to `Any` to continue analysis

This prevents unsound polymorphic mutation.

---

### Identifier Use

```lx
x
```

Rules:

* If `x → Mono(tv)`:

  ```
  T(expr) = tv
  ```

* If `x → Poly(scheme)`:

  ```
  T(expr) = instantiate(scheme)
  ```

Undefined variables must already be caught by `resolve`.

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
fn(a, b) { body }
```

Type:

```
Function([A, B], R)
```

* `A`, `B`, `R` are fresh `TypeVar`s
* Parameters are **always monomorphic**
* Parameters are bound as `Mono(TypeVar)`

---

### Function Call

```lx
f(x1, x2, ..., xm)
```

Rules:

If:

```
T(f) ≈ Function([P1..Pk], R)
```

Then:

* For `i = 1 .. min(k, m)`:

  ```
  T(xi) ≈ Pi
  ```

* If `m > k`: extra arguments are **allowed and ignored**

* If `m < k`: missing arguments may be reported as an error (implementation choice)

* Result type:

  ```
  T(call) = R
  ```

This matches Lua / JS calling semantics and supports callback adapters.

---

### Returns

```lx
return expr
```

Constraint:

```
T(expr) ≈ R
```

All returns unify to the same return type.

---

### Closures

Captured variables:

* reuse the **same TypeBinding**
* propagate constraints across scopes naturally

No special handling beyond `resolve`’s capture graph.

---

## Conditionals (Flow-Insensitive)

```lx
if cond { A } else { B }
```

Rules:

* Optionally constrain `T(cond) ≈ Bool`
* Analyze both branches
* Unify all variable bindings touched by either branch

This enforces **branch consistency** without control-flow tracking.

---

## Tagged Records (Optional Precision)

Two supported strategies:

### MVP (Default)

* Detect tag fields (`kind`, `type`, etc.)
* Variant-specific fields widen to `Any`

### Precise (Future)

```
Tagged(tagField, cases)
```

* Enforce per-tag record shapes
* Safe field access only when tag is known

No syntax changes required.

---

## Error Handling

On any type error:

* Emit diagnostic
* Replace conflicting type with `Any`
* Continue analysis

Severity levels:

* `error` (default)
* `warning`
* `info`

---

## Guarantees

If `success == true`:

* No incompatible constraints were found
* Inferred types are stable and usable for tooling

If `success == false`:

* Partial types remain available
* Diagnostics are complete and non-cascading

---

## Non-Goals

This phase intentionally does **not**:

* implement full Hindley–Milner
* infer polymorphic parameters
* perform effect or lifetime analysis
* reason about reachability or exhaustiveness
* influence runtime or code generation

---

## Summary

The typecheck phase provides:

* fast, predictable static analysis
* honest support for higher-order prelude functions
* limited, well-scoped polymorphism
* minimal implementation complexity
* zero coupling to backend decisions

It is a **frontend value multiplier**, not a soundness gatekeeper.

