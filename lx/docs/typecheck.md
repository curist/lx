# Typecheck Overview

This document describes how the current lx typecheck works, with an emphasis on
the facts + rules refactor and the Simple-Sub constraint solver. The typecheck
is implemented in `lx/src/passes/analysis/typecheck.lx` and operates on the
lowered, pre-ANF AST with name resolution (`analysis.resolve`) attached.

## Goals and Scope

- Provide fast, deterministic inference without search.
- Support monomorphic `let` bindings, functions, records, arrays, maps, enums,
  and optionals (`Option` for `nil`).
- Keep policy decisions in small rules and facts instead of ad hoc state.

## Inputs and Outputs

Inputs:
- Lowered AST (pre-ANF) and resolve data (`resolvedNames`, `scopeInfo`).
- Optional `enumInfo` from parse, remapped by lower.

Outputs:
- `types`: nodeId -> inferred type.
- `errors`: list of diagnostics.
- `recordEnumKeyOrigins`: enum-member origins for record keys.

## Type Representation

Types are small tagged maps created by helper constructors in
`lx/src/passes/analysis/typecheck/helpers.lx`:

- Primitives: `Any`, `Nil`, `Number`, `Bool`, `String`.
- `Array(elem)`, `Map(key, elem)`, `Record(fields)`, `Enum(fields)`.
- `Function(params, return)` with optional `minArity` and `paramNames`.
- `Option(value)` for values that can be `nil`.
- `TypeVar(id)` with `lowerBounds` and `upperBounds`.
- `Indexable(elem, key)` for deferred index heuristics.

`Option` is treated as an optional wrapper; `Nil` is a subtype of `Option[T]`,
and `T` is a subtype of `Option[T]`.

## Phases

### 1) Facts pass (`facts.lx`)

`computeFacts` walks the AST once and records small, stable facts:

- `stmtPosition`: whether a node's result is discarded.
- `alwaysReturns`: whether a branch always returns.
- `nilGuardReturn`: identifies `if !x { return }` guards and the guarded decl.
- `literalKeyValue`: literal keys for index expressions (string or number).

These facts remove the need to track mutable state during synthesis and allow
rules to stay simple.

### 2) Type synthesis (`typecheck.lx`)

`synthExpr` traverses the AST, assigns a type to each node, and emits
constraints. The checker carries:

- `env`: declId -> Type (monomorphic bindings).
- `types`: nodeId -> Type.
- `constraintCache`: to avoid re-checking constraint pairs.
- `builtinConstraints`: deferred constraints for `range` and `keys`.
- `facts`: the precomputed facts table.

The synthesis logic is mostly mechanical and delegates special behavior to
rules in `rules.lx`.

### 3) Rule application (`rules.lx`)

Rules are small handlers that match on a node kind and a few facts:

- Dot access rules:
  - `Option[T].field` yields `Option[fieldType]` and unwraps nested options.
  - `Enum.member` yields `Number`.
  - `Record.field` yields the field type.
  - `TypeVar.field` emits a record constraint.
  - `Any.field` yields `Any`.

- Builtin rules:
  - `range` and `keys` synthesize `Array[elem]` and defer element refinement.
  - `len` validates array or string.
  - `nameOf` validates enum inputs and value type.

Rules are applied in ordered sequences so specific cases win before fallback
behavior.

### 4) Constraint solving (`helpers.lx`)

`constrain(lhs, rhs)` enforces `lhs <: rhs` using a Simple-Sub solver:

- Type variables collect lower and upper bounds.
- Constraints propagate through bounds.
- Occurs checks avoid infinite types.
- Option rules preserve optionality (no dropping `Option`).
- Function types are contravariant in params and covariant in return.
- Records check overlapping fields for conflicts.

`solveBuiltinConstraints` runs after synthesis to resolve deferred `range/keys`
element types once enough evidence exists.

## Flow Sensitivity and Narrowing

Nil-guard narrowing is driven by facts instead of ad hoc detection:

- The facts pass detects `if !x { return }` guards in blocks.
- `synthBlock` applies a refinement overlay for subsequent expressions.
- Refinements are scoped to the block and do not leak outward.
- Closures are synthesized with the original (unrefined) environment so that
  closure captures remain sound.

This replaces earlier save/restore gymnastics and keeps narrowing predictable.

## Statement Position

Statement position is a precomputed fact. It is used to avoid emitting
branch-mismatch errors for `if` expressions whose results are ignored.

## Indexing and Assignment Heuristics

Index access (`obj[idx]`) and assignment (`obj[idx] = value`) follow a small set
of heuristics:

- Arrays require numeric indices.
- Strings require numeric indices and return `String`.
- Maps constrain key and element types.
- Records allow literal keys; dynamic keys yield `Option` of a unified field
  type.
- Empty records can be promoted to `Map` or `Array` when assignment evidence
  indicates that usage.

These heuristics rely on literal-key facts and constraints, with additional
enum-member resolution when needed.

## Builtin Special Cases

Most builtins are treated as functions from the builtin catalog. A small set
get additional rules:

- `range` and `keys` record deferred constraints so later evidence can refine
  the element type.
- `len` and `nameOf` validate argument types.

## Error Policy

Diagnostics are emitted when constraints fail or when a rule detects a misuse
(missing field access, invalid index type, invalid builtin call). Error policy
is intentionally local and deterministic; type inference continues where
possible with `Any` to avoid cascading failures.

## Notes and Limitations

- Inference is monomorphic and single-pass; no generalization of `let` bindings.
- Flow sensitivity is limited to nil-guard narrowing.
- Records and maps are distinguished by usage heuristics rather than explicit
  annotations.
- The checker is designed for predictable performance and tooling friendliness.
