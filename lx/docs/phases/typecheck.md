# Typecheck Phase (`typecheck.lx`)

## Responsibility

Perform **best-effort static analysis** on resolved Lx programs.

This phase:

* infers stable types for variables, functions, records, and arrays
* detects incompatible usages (shape mismatches, conflicting assignments)
* supports closures, imports, and higher-order functions
* is **mostly monomorphic**, with **limited polymorphism** for function values
* is **flow-insensitive** globally, but supports **local narrowing** in `if` branches
* is **local**, **fast**, and designed for tooling feedback loops

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
  builtinTypes: BuiltinTypeTable,     // len, push, str, type, etc.
  importSignatures: SignatureLoader?, // optional future extension
  tagFieldNames: ["kind", "tag", "type"]?, // optional; default ["kind"]
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

  Tagged(
    tagField: string,
    cases: map<tagValue, Record>
  )
```

### Semantics

* **Unknown**: initial state; can be refined by constraints
* **Any**: escape hatch; unifies with everything, suppresses cascades
* Variables are monomorphic by default; **limited polymorphism exists only for function values**
* Records and arrays are **structural** (shape-based)
* Discriminated unions are represented using `Tagged(...)` where detectable

---

## Environments and Identity

### Type Bindings

Each resolved declaration identity (`declNodeId`) maps to exactly one binding:

```
TypeBinding =
  Mono(TypeVar)   // assignable, monomorphic slot
  Poly(Scheme)    // immutable, instantiable scheme
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
* Captured variables reuse the **same TypeBinding** (Mono or Poly)

---

## Constraint Model

Typechecking is a single traversal that:

1. creates fresh `TypeVar`s
2. emits constraints
3. unifies eagerly
4. reports conflicts

This is not full HM. The only generalization is a restricted let-polymorphism rule for function literals.

---

## Limited Polymorphism Model

Prelude/user code often wants polymorphic higher-order helpers (`map`, `each`, `fold`, `_1/_2/_3` adapters). A strictly monomorphic system would either reject common usage or collapse to `Any`.

Lx supports **limited parametric polymorphism**:

* `Poly(Scheme)` bindings
* per-use instantiation
* strict value restriction: **only function literals are generalized**
* polymorphic bindings are **not assignable**

This remains lightweight and aligns with typical “safe subset” let-polymorphism.

---

## Schemes, Free Variables, Generalization, Instantiation

### Scheme Form

```
Scheme =
  Forall(typeVars: [TypeVar], type: Type)
```

### Free Type Variables

* `ftv(type)` → free type variables in a `Type`
* `ftv(binding)` → free vars in Mono/Poly
* `ftv(env)` → union of all reachable bindings’ free vars

### Generalization

```
generalize(env, type) =
  let qs = ftv(type) - ftv(env)
  Poly(Forall(qs, type))
```

Only performed for **function literals** (and top-level `fn` declarations, treated as function literals).

### Instantiation

```
instantiate(Forall([t1..tn], T)) =
  let [u1..un] = fresh TypeVars
  substitute(T, { t1 → u1, ... })
```

Performed on every identifier use of a polymorphic binding.

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

for purposes of generalization only.

---

### Reassignment

```lx
x = expr
```

Rules:

* If `x → Mono(tv)`:

  ```
  tv ≈ T(expr)
  ```

* If `x → Poly(_)`:

  * report error: “Cannot assign to polymorphic binding”
  * widen assignment result to `Any` to continue analysis

---

### Identifier Use

```lx
x
```

Rules:

* If `x → Mono(tv)`:

  ```
  T(x_expr) = tv
  ```

* If `x → Poly(scheme)`:

  ```
  T(x_expr) = instantiate(scheme)
  ```

Undefined variables should already be caught by `resolve`.

---

## Records

### Record Literals

```lx
.{ a: v1, b: v2 }
```

Type (base case):

```
Record{ a: T(v1), b: T(v2) }
```

If the record literal qualifies as a tagged record (see “Tagged Records”), it may instead produce a `Tagged(...)`.

### Record Field Access

```lx
x.a
```

Rules:

* If `T(x) = Unknown`:

  * refine to `Record{ a: Unknown }` (best-effort shape introduction)
* If `T(x) = Record` and `a` missing:

  * error (or optionally extend, if you later adopt grow-only shapes)
* If `T(x) = Tagged(...)`:

  * use the Tagged field-access rules (below)
* Shapes are considered stable once established; conflicting constraints produce errors.

---

## Arrays

### Array Literals

```lx
[]
```

Creates:

```
Array(elem = Unknown)
```

### Element Access

```lx
arr[i]
```

Rules (best-effort):

* constrain `T(i) ≈ Number` (optional)
* if `T(arr) = Unknown` ⇒ refine to `Array(Unknown)`
* if `T(arr) = Array(E)` ⇒ `T(arr[i]) = E`
* otherwise error/widen to `Any`

### push

```lx
push(xs, v)
```

Constraint:

```
T(xs) ≈ Array(elem = T(v))
```

---

## Functions and Closures

### Function Literals

```lx
fn(a, b) { body }
```

Type:

```
Function([A, B], R)
```

* `A`, `B`, `R` are fresh `TypeVar`s
* parameters are always bound as `Mono(TypeVar)`
* returns unify into `R`

### Calls

```lx
f(x1, x2, ..., xm)
```

First, constrain callee shape:

```
T(f) ≈ Function([P1..Pk], R)
```

Then apply argument constraints:

* for `i = 1 .. min(k, m)`:

  ```
  T(xi) ≈ Pi
  ```

* if `m > k`: extra args are **allowed and ignored** (no constraints)

* if `m < k`: missing args may be an error or warning (implementation choice)

* result:

  ```
  T(call) = R
  ```

This matches Lx runtime semantics (Lua/JS-style extra args ignored).

---

## Function Arity Compatibility

Because extra call arguments are ignored, function values are compatible under **arity widening**.

### Compatibility Rule

A function value of type:

```
Function([P1..Pk], R)
```

is compatible with an expected function type:

```
Function([Q1..Qm], S)
```

when:

* `k <= m`
* for all `i = 1..k`: `Pi ≈ Qi`
* and `R ≈ S`

Intuition: a “short” callback can be passed where a “long” callback is expected, because it will ignore extra arguments at runtime.

### Where Applied

Apply this rule when:

* constraining an argument against an expected callback type
* constraining a value against an annotated expected function type
* checking builtin signatures that accept callbacks

This is a targeted rule; no full variance/subtyping system is required.

---

## Returns

```lx
return expr
```

Constraint:

```
T(expr) ≈ R
```

All returns unify to the same return type.

---

## Closures

Captured variables:

* reuse the **same TypeBinding**
* constraints propagate across closures naturally

No special handling beyond `resolve` capture identity.

---

## Conditionals and Local Narrowing

The analysis is globally flow-insensitive, but supports **local narrowing** inside `if` branches to reduce false positives for common guard patterns.

### Branch Analysis

For:

```lx
if cond { A } else { B }
```

Procedure:

1. Analyze `cond` normally.
2. Compute two derived environments:

   * `envThen = narrow(env, cond, truthy=true)`
   * `envElse = narrow(env, cond, truthy=false)`
3. Analyze `A` under `envThen`, `B` under `envElse`.
4. Merge environments after the `if` by unifying corresponding bindings.

Narrowing is best-effort and limited to simple syntactic patterns; it never changes runtime semantics.

### Supported Narrowing Patterns

#### Nil checks

```
x != nil
```

* truthy branch: treat `x` as non-nil for guarded operations (e.g., field/index access)
* falsy branch: treat `x` as `Nil`

```
x == nil
```

* truthy branch: treat `x` as `Nil`
* falsy branch: treat `x` as non-nil

(Implementation detail: you may represent “non-nil” as a guard fact rather than a new type form; the requirement is that guarded `x.a` should not fail solely because `x` might be nil.)

#### Type tests

```
type(x) == "string" | "number" | "bool" | "nil"
```

* truthy branch: refine `x` to the corresponding primitive type
* falsy branch: no refinement required

Only applied when `x` is a resolved identifier.

#### Tag checks

```
x.kind == "Foo"
```

If `T(x) = Tagged("kind", cases)` and `"Foo"` is a known case:

* truthy branch: treat `x` as the `"Foo"` case for field access
* falsy branch: no refinement required (future extension could track exclusion)

---

## Tagged Records (Discriminated Unions)

### Problem

Dynamic code often encodes tagged unions using records like:

```lx
.{ kind: "Int",  value: 123 }
.{ kind: "Str",  value: "hi" }
.{ kind: "None" }
```

A naïve frozen-record rule would either:

* flag differing `value` shapes as errors, or
* collapse everything to `Any`

### Goal

Provide syntax-free discriminated unions via:

```
Tagged(tagField, cases)
```

This enables:

* per-tag field typing
* safe field access when tag is known (directly or via narrowing)
* fewer false positives for common union patterns

---

### Tagged Record Detection

A record literal is a tagged-record candidate if:

* it contains a tag field name in `opts.tagFieldNames` (default `["kind"]`), and
* the tag value is a **literal string** (or literal number, if enabled)

Example:

```lx
.{ kind: "Int", value: 123 }
```

yields:

```
Tagged("kind", {
  "Int": Record{ value: Number }
})
```

If the tag value is not a literal, the literal does not produce `Tagged` (it remains a plain `Record`) unless you later adopt a heuristic mode.

---

### Unification Rules

#### Tagged with Tagged

```
Tagged(tf, cases1)  ≈  Tagged(tf, cases2)
```

Rules:

* if tagField differs: error → widen to `Any`
* merge case maps by tag value
* for each shared tag value `t`:

  * unify the corresponding case records (`Record1(t) ≈ Record2(t)`)
  * field conflicts within the same tag → error → widen that field to `Any` (or widen the entire case)

#### Tagged with Unknown

If `T(x) = Unknown` and unified with `Tagged`, set `T(x)` to that Tagged.

#### Tagged with Record

Recommended (strict) behavior:

* error: cannot unify `Tagged` with plain `Record` unless the Record is recognized as a Tagged candidate (literal tag)
* widen to `Any` to continue analysis

(You may optionally implement a permissive mode that converts a plain record with a literal tag into a single-case Tagged on the fly.)

---

### Field Access on Tagged Values

For:

```lx
x.field
```

If `T(x) = Tagged(tagField, cases)`:

#### Accessing the tag field

* `x.tagField` is a tag-literal set internally, but exposed as `String` (or `Any`) in `types` if unions are not representable.

#### Accessing a non-tag field

* If the current environment proves `x.tagField == t` for some literal `t`:

  * lookup `cases[t]`
  * `T(x.field)` = the field type from that case record (error if missing)
* If the tag is not known:

  * allow access only if `field` exists in **all cases** with the **same type**
  * otherwise: error (or widen to `Any`, depending on your error strategy)

This makes “safe field access requires known tag” explicit and tool-friendly.

---

## Error Handling

On any type error:

* report diagnostic
* replace conflicting type constraints with `Any`
* continue analysis

Severity levels:

* `error` (default)
* `warning`
* `info`

---

## Guarantees

If `success == true`:

* no incompatible constraints were found
* inferred types are stable and usable for tooling

If `success == false`:

* partial types remain available
* diagnostics are complete and non-cascading

---

## Non-Goals

This phase intentionally does **not**:

* implement full Hindley–Milner
* infer polymorphic parameters (rank-1 only at let-bound function values)
* perform effect/reachability/exhaustiveness analysis
* enforce runtime semantics or optimize codegen

---

## Summary

The typecheck phase provides:

* fast, predictable static analysis
* honest support for polymorphic prelude utilities via restricted schemes
* callback typing consistent with “extra args ignored”
* practical narrowing for nil/type/tag guards
* precise discriminated unions using `Tagged(...)` without syntax changes

It is a frontend value multiplier for diagnostics and IDE support, not a soundness gatekeeper.

