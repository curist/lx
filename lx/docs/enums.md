# Enums in Lx

Lx enums provide a compact way to define **named integer constants** with:
- auto-incrementing values (like the old `iota` pattern),
- compile-time validation (duplicate names/values, non-integers),
- fast reverse lookup via `nameOf()`,
- runtime immutability.

They are designed to stay compatible with Lx’s direction toward **closed records** (no hidden fields and no “tagged map” hacks).

## Syntax

Enums are expressions:

```lx
let Color = enum { Red, Green, Blue }         // 0, 1, 2
let Status = enum(100) { Pending, Done }      // 100, 101
let E = enum { A, B = 5, C }                  // 0, 5, 6
let F = enum { A = 10, B, C, D = 0, E, F }    // allowed if values remain unique
```

## Semantics

### Values

- Member values are numbers, but are **validated as integers** at parse time.
- Values **auto-increment** from `0` or from `enum(start)` if provided.
- Explicit assignments reset the next implicit value (`B = 5` makes the next member `6`).
- Member names must be unique.
- Member values must be unique.

### Runtime representation

At runtime, `enum { ... }` evaluates to a first-class value of type `"enum"`:

```lx
let E = enum { A, B }
type(E) // "enum"
```

Internally, an enum is a dedicated VM object with two tables:
- forward: `name -> value`
- reverse: `value -> name`

### Member access

Enums support the same read syntax as maps/records:

```lx
E.A       // member value (or nil if missing)
E["A"]    // same
```

### Immutability

Enums are immutable from user code:

```lx
let E = enum { A, B }
E.A = 999        // runtime error: Enum is immutable.
E["A"] = 999     // runtime error: Enum is immutable.
```

This is enforced in the VM (not just by convention).

### Reverse lookup: `nameOf`

Use `nameOf(enumValue, value)` to look up the member name:

```lx
let Color = enum { Red, Green = 3, Blue }
nameOf(Color, Color.Blue)  // "Blue"
nameOf(Color, 999)         // nil
```

`nameOf` **expects an enum**. It does not accept generic maps/records.

### Equality / identity

Enums compare by **object identity** (reference equality). Two separately-evaluated enums with the same members are not equal unless they are literally the same object reference.

### Iteration / keys

`keys(enum)` and `range(enum)` return the enum member names in **declaration order**.

## Rationale

Historically, Lx used an `iota`-style pattern (and sometimes reverse-name tables) to represent “enums”. That approach had drawbacks:
- extra variables / scope blocks to avoid leaking `iota` state,
- reverse lookups were either missing or required separate hand-maintained maps,
- it pushed more “convention” into user code.

The current design makes enums:
- **ergonomic** (single expression),
- **safe** (parse-time validation),
- **fast** (`nameOf` is O(1) expected),
- **future-compatible** with closed records (no hidden keys/fields added to user data).

## Potential future work

The current enum feature is a foundation; likely future directions include:

- **Exhaustiveness checking / switch lowering**
  - Use `enumInfo` from the compiler pipeline to drive static checking and/or jump-table style optimizations.
  - For enum-keyed record literals, the typechecker also records key origins in a side table:
    - `recordEnumKeyOrigins` (returned by `lx/src/passes/frontend/typecheck.lx`) maps `recordLiteralNodeId -> key -> { enumDeclId, enumName, member }`.
    - This is keyed by `enumDeclId` (resolver identity), so same-named enums across modules don’t collide.
    - This is intended as the hook for future “enum-keyed map/record” exhaustiveness checks, even though the record type itself uses numeric keys (what runtime sees).

- **Stable enum IDs (debug/tooling)**
  - Add a monotonic runtime id to `ObjEnum` (or to all objects) if stable identity is useful for tooling/serialization.

- **More efficient runtime storage**
  - If needed, enums can move beyond `Table` (e.g. dense reverse array for small integer ranges, or sorted arrays for deterministic value-order queries).

- **More enum-aware tooling**
  - Better diagnostics, richer query integration, and editor features (go-to member, find references, etc.).
