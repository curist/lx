# Typecheck Phase (`src/passes/frontend/typecheck.lx`)

## Responsibility

Perform **best-effort** static typing for tooling and early diagnostics.

This phase:
- infers types for expressions and bindings using a constraint solver
- reports type/shape mismatches, but **continues** (containment to `Any`)
- is **monomorphic** (no HM/let-polymorphism), with a few practical “dynamic language” rules

This phase does **not**:
- change runtime behavior
- transform the AST
- guarantee full soundness

## Position in Pipeline

```
Source
  → src/passes/frontend/parser.lx
  → src/passes/frontend/lower.lx
  → src/passes/frontend/resolve.lx
  → src/passes/frontend/typecheck.lx
  → (optional) src/passes/backend/codegen.lx
```

`typecheck` depends on `resolve` and runs on the **lowered** AST.

## API

```lx
typecheck(ast, resolveResult) -> .{
  success: bool,
  types: { nodeId: Type },
  typeVarBindings: { typeVarId: Type },
  errors: [.{ nodeId, message, severity }],
}
```

`types` and `typeVarBindings` are returned **dereferenced/canonicalized** (see `derefAll` in `src/passes/frontend/typecheck.lx`).

## Type Forms

User-facing (structural) types:

```
Any | Nil | Number | Bool | String

TypeVar(id)

Function(params: [Type], return: Type)
Array(elem: Type)
Map(key: Type, elem: Type)         // key restricted to Number|String (best-effort)
Record(fields: { key: Type })      // closed shape
Option(value: Type)
```

Internal-only:

```
Indexable(elem: Type, key: Type)   // temporary “unknown container” for x[i] / x[i]=v inference
```

## High-level Strategy

The typechecker is structured as:

1. A traversal (`synthExpr`) that assigns types to nodes and emits constraints.
2. A worklist solver (`src/passes/frontend/typecheck-helper.lx`) that repeatedly tries to solve constraints until no **binding progress** is made.
3. A second pass over function bodies (`refineFunctions`) to refine closure-captured types.
4. Finalization:
   - resolve remaining `Indexable` bindings (default to `Map` with a diagnostic)
   - contain remaining unsolved constraints to `Any` so tooling does not crash

## Constraint Solver

Constraints are stored in `checker.constraints` and solved by `trySolveConstraint`.

### Constraint Kinds

- `Eq(t1, t2)`: unification
- `HasField(base, fieldName, fieldType, mode)`
  - `mode: "read" | "write"`
- `Call(callee, args[], out)`
  - extra args allowed; too-few args is an error
- `Index(base, index, out, literalKey?)`
- `IndexSet(base, index, value, literalKey?)`
- `KeyLike(t)`: enforces “hashmap key type must be number or string” (best-effort)

### Progress and Convergence

The worklist is run until **no typevar binding changes** occur (`checker.changed` set by `bindTypeVar`).

If the solver hits `maxRounds`, it emits a diagnostic and continues with containment.

## Core Semantics (Current)

### Records vs Maps (`.{ ... }`)

- `.{}` (empty) is a `Map[K,V]` (hashmap literal).
- Any **non-empty** `.{ ... }` is a **closed `Record{...}`**, even when written with computed keys.
- For non-empty literals, computed keys must be **string/number literals** (e.g. `.{ ["a"]: 1 }`, `.{ [1]: "x" }`).
  - Non-literal computed keys (e.g. `.{ [k]: v }`) are a type error (“closed shape”).

Closed record behavior:
- `x.a` requires field `a` to exist (no field growth).
- `x["a"]` is allowed for records only when the index key is a **literal** string/number (so the field is known).
- `x["newKey"] = ...` is rejected for records (closed shape), but is allowed for `Map`.

### Indexing (`x[i]`) and Index Assignment (`x[i] = v`)

Concrete bases:
- `Array[T][Number] -> T`
- `Map[K,V][K] -> V` (with `KeyLike(K)`)
- `String[Number] -> String`

Unknown base:
- Using `x[i]` or `x[i] = v` on an unbound typevar introduces `Indexable(elem,key)`.
- When there is enough evidence, `Indexable` can commit to a concrete container.
- If it remains ambiguous, finalization defaults it to `Map[key, elem]` with a diagnostic.

### Option

Conditionals:
- `if cond { expr }` returns `Option[T(expr)]`, except `if cond { nil }` is normalized to `Nil` (no `Option[Nil]`).

Nil unification (best-effort):
- `Nil` can promote a typevar binding to `Option[T]` (without double-wrapping).

Field access on Option:
- Reads: `opt.field` typechecks as `Option[fieldType]` (implicit lifting).
- Writes: `opt.field = v` is rejected (“Cannot assign to field on optional value”).

### Calls

Calls are solved by a `Call` constraint (not by baking arity into unification):
- Too few arguments is an error.
- Extra arguments are allowed and ignored (no constraints for extras).

### Operators

- Arithmetic `+ - * /` expects numbers (except `+` supports `String + String -> String`).
- Comparisons `< <= > >=` expect numbers and return `Bool`.
- Equality `== !=` returns `Bool` (no operand constraints).

### Logical `and/or`

Lua-like semantics:
- `and/or` return operands (not necessarily `Bool`).
- The typechecker uses constant-truthiness shortcuts for obvious literals (`true`, `false`, `nil`, literals/containers).
- Otherwise it uses a conservative `joinTypes`:
  - `Nil` + `T` becomes `Option[T]`
  - mixed unrelated types fall back to `Any` (no unions yet)

## Builtins

Resolver marks unknown/global names as `kind: "builtin"`.

Typechecking assigns concrete types to many builtins via `builtinTypeByName`:
- Native builtins from `include/native_fn.h` (e.g. `Math.floor`, `tonumber`, `len`, `keys`, `push/pop/concat`, `Date`, `Lx`, …)
- Prelude helpers from `globals.lx` (e.g. `_1/_2/_3`, `map`, `fold`, `sort`, …)

Anything not recognized still defaults to `Any`.
