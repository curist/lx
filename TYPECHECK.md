# **Lx Static Analysis Specification (Monomorphic, Best-Effort, Flow-Insensitive)**

## **1. Overview**

This document defines a *static analysis* pass for the Lx language.
It runs **after parsing (AST)** and **before codegen**.

Goals:

* Detect *shape inconsistencies* (wrong field names, wrong array element types).
* Infer stable types for variables, function parameters, return values.
* Support closures (captured variables share types with their outer variables).
* Allow dynamic code, but identify *incompatible* patterns.
* Keep the analysis **simple**, **fast**, and **local** (not whole-program HM).

Non-goals:

* No polymorphism.
* No overloads.
* No inferred generic types.
* No subtyping.
* No perfect soundness.

Principle:

> **Every variable and function has exactly one monomorphic type.
> The type is initially Unknown and refined by constraints.
> Any two incompatible constraints create a type error.**

---

# **2. Type System**

## **2.1 Type Forms**

```
Type =
  Unknown
  Number
  Bool
  String
  Nil
  Function(params: array<Type>, return: Type)
  Array(elem: Type)
  Record(fields: map<string, Type>)
  Map(key: Type, value: Type)   // Optional; only if needed later
  Any                           // Escape hatch (not assigned by default)
  // Optional extension for discriminated unions (tagged records), see §2.2
  Tagged(tagField: string, cases: map<tagValue, Record(fields: map<string, Type>)>)
```

Notes:

* `Unknown` = type is not yet inferred; any constraint may refine it.
* `Any` = analysis explicitly gave up; always allowed but loses benefit.
* Records and Arrays are **monomorphic** and **frozen** once fields/elem types are known.

---

## **2.2 Tagged Records (Discriminated Unions)**

Problem: patterns like `Value { kind: ValueType, value: <per-kind type> }` encode a tagged union inside a record; the monomorphic frozen-record rule would otherwise flag differing `value` shapes as errors.

Two implementation options (no syntax changes):

1) **Precise**: use `Tagged(tagField, cases)` in the type lattice.
   * Detect a record literal with a tag field (e.g., `kind`/`tag`/`type`) and a literal tag value; create/extend a `Tagged` with a case per tag value, each with its own frozen record shape (excluding the tag field).
   * Unify merges case maps; conflicting shapes for the same tag → error. Access without a known tag only allows fields common to all cases with the same type.
   * Best accuracy; reusable for all discriminated unions.

2) **Heuristic**: treat records with a tag field specially but widen variant fields.
   * Detect tag field names; allow companion fields to vary per tag by widening them to `Any` (or a loose per-tag map) unless the tag is a known literal.
   * Minimal code; less precise and may mask shape errors; limited to chosen tag field names.

Migration plan: start with (2) if we need speed; keep the tag detection logic isolated. Later replace the widening with the `Tagged` type constructor/unifier from (1). User code and syntax stay unchanged; the checker just becomes stricter/more informative when upgraded.

---

# **3. Environments**

Each function (including top-level script) gets a **TypeEnv**:

```
TypeEnv = {
  parent: TypeEnv | nil,
  vars: map<string, TypeVar>,   // monomorphic type variables
}
```

Captured variables reuse the **same TypeVar** as the outer environment.

Functions have a **FunctionTypeVar** with param + return typevars.

---

# **4. Constraint Rules**

Static analysis is:

> A traversal of the AST that emits **constraints** between TypeVars,
> then resolves them by **unification**.

---

## **4.1 Variable assignment**

```
let x = expr
```

Constraint:

```
T(x) ≈ T(expr)
```

Where:

* If `T(x) = Unknown`, assign it to `T(expr)`.
* If both are non-Unknown, unify or error.

---

## **4.2 Reassignment**

```
x = expr
```

Same rule:

```
T(x) ≈ T(expr)
```

If they differ → type error.

---

## **4.3 Identifier usage**

```
x
```

Produces constraint:

```
T(expr) = T(x)
```

If `x` is missing → error (undefined variable).

---

## **4.4 Record literals**

```
.{ a: v1, b: v2 }
```

Creates fresh record type:

```
Record{ a: T(v1), b: T(v2) }
```

---

## **4.5 Record field access**

```
x.a
```

Constraint:

```
T(x) ≈ Record{ a: T_field }
T(expr) = T_field
```

If `x` was Unknown → becomes `Record{ a: Unknown }`.

If `x` was already Record but lacks field `a` → *extend? or error?*

### **Lx Rule: Frozen record shapes**

Once a record’s field set is known, it cannot grow unless the type was still Unknown.

Formal:

* If `T(x) = Unknown` → unify with `Record{ a: Unknown }`.
* If `T(x) = Record(fields)` and `a ∉ fields`:

  * Error: “Record has no field ‘a’”.

---

## **4.6 Record field assignment**

```
x.a = expr
```

Constraint:

```
T(x) ≈ Record{ a: T(expr) }
```

Same freezing rule applies.

---

## **4.7 Array literals**

```
[]
```

Creates fresh:

```
Array(elem = Unknown)
```

---

## **4.8 Array append / push**

```
push(x, expr)
```

Constraint:

```
T(x) ≈ Array(elem = T(expr))
```

If elem type conflicting → error.

---

## **4.9 Array indexing**

```
x[i]
```

Constraint:

```
T(x) ≈ Array( elem = E )
T(expr) = E
```

`i` must be Number (optional check).

---

# **5. Functions and Closures**

## **5.1 Function definition**

```
fn f(a, b) { body }
```

Creates function type:

```
T(f) = Function( params = [A, B], return = R )
```

Where `A`, `B`, `R` are fresh Unknown typevars.

Parameters inserted in the local env:

```
T(a) = A
T(b) = B
```

---

## **5.2 Function call**

```
f(x, y)
```

Constraint:

```
T(f) ≈ Function([A, B], R)
T(x) ≈ A
T(y) ≈ B
T(callResult) = R
```

### **Key rule (your design):**

> The first call usually sets the param types; later calls must match.

This comes naturally from monomorphic unification.

---

## **5.3 Return statements**

```
return expr
```

Constraint:

```
T(expr) ≈ R
```

If multiple return statements appear → unify all return types.
If inconsistent → error.

---

## **5.4 Closures**

Captured variable = same TypeVar as outer scope.

```
fn outer() {
  let x = 0
  fn inner() { print(x) }
}
```

`inner` sees the same `T(x)` as outer.

Constraints propagate through closure boundaries automatically.

---

# **6. Conditionals**

```
if cond { ... } else { ... }
```

Analysis is **flow-insensitive** but constraints from both branches must agree.

Procedure:

1. Analyze `cond`, require it to be Bool (optional).
2. Analyze then-branch, producing env1.
3. Analyze else-branch, producing env2.
4. Join:

```
For each variable v:
  unify( env1[v], env2[v] )
```

If they disagree → error.

This enforces:

> A variable has the same type no matter which branch executed.

---

# **7. Loops**

```
for ...
while ...
```

Flow-insensitive approach:

* Analyze the body once.
* All variable constraints apply unconditionally.
* Assignments inside loops must be consistent with assignments outside loops.

This is simple but effective given your restrained style.

---

# **8. Unification Algorithm**

Unification between two types:

* `Unknown` ≈ T → T
* T ≈ `Unknown` → T
* Primitive types must match exactly.
* Arrays unify if their element types unify.
* Records unify if:

  * They have exactly the same field set.
  * Each field type unifies.
* Functions unify if:

  * Same arity.
  * Param types unify pointwise.
  * Return types unify.

If any mismatch → type error.

---

# **9. Module Boundaries (Imports)**

Two modes:

### **9.1 Simple mode (default)**

Imported symbols have type:

```
Any
```

Unless an explicit signature is provided.

### **9.2 Signature mode (optional)**

Each module may export a summarized type signature (`.lxsig`).
Static analyzer loads signatures before analyzing callers.

Not required for MVP.

---

# **10. Error Behavior**

Static errors include:

* Record field mismatch.
* Array element type mismatch.
* Variable assigned incompatible shapes/types.
* Function called with incompatible argument types.
* Function returns incompatible values across branches.
* Undefined variable.
* Record shape grown after freezing.

On errors:

* Continue analysis (don’t abort).
* Mark offending TypeVar as `Any` to avoid cascading errors.
* Collect error list for IDE / CLI.

---

# **11. Polymorphic Builtins (Special-Cased)**

The language is monomorphic for user code, but a small set of builtins are intrinsically polymorphic and are handled explicitly by the analyzer:

* `len(x)` — accepts `Array`, `Record`, or `String`; returns `Number`.
* `str(x)` — accepts `Any`; returns `String`.
* `type(x)` — accepts `Any`; returns a tag/`String` describing the runtime type.

These do **not** imply user-definable polymorphism. The checker models them via a builtin overload table; all other functions remain monomorphic.

---

# **12. Interaction with Runtime Semantics**

Static analysis does **not** change runtime behavior.

* Runtime remains dynamic and monomorphic.
* Static errors are advisory unless you choose to enforce them.
* Unknowns and Anys produce no warnings unless explicitly requested.

---

# **13. Example: Record Inference**

```
let p = .{ x: 1 }
p.y = 2    // ERROR: cannot add field y; record shape frozen
```

Constraints:

* T(p) = Record{x: Number}
* p.y = 2 → requires T(p) = Record{y: Number}
* Mismatch → error.

---

# **14. Example: Function Inference**

```
fn add(a, b) { a + b }
let r1 = add(10, 20)
let r2 = add(5.5, 1)    // ERROR if Number is not unified with Float
```

Constraints:

* First call: a,b → Number.
* Second call: a,b → Float.
* Unification fails → error.

---

# **15. Example: Arrays**

```
let xs = []
push(xs, 1)
push(xs, "hello")   // ERROR: Number vs String
```

Constraints:

* xs: Array(elem = Unknown)
* After first push: Array(Number)
* Second push conflicts → error.

---

# **16. Complexity**

* Linear over AST size.
* Uses union-find for typevars.
* No exponential HM behavior, no backtracking, no polymorphism.

---

# **17. Summary**

This spec gives Lx:

* **Predictable monomorphic types**
* **Frozen record shapes**
* **Frozen array element types**
* **Monomorphic functions with call-site inference**
* **Best-effort but meaningful error detection**
* **Very low implementation complexity**
