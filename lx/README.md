# lx

A programming language derived from [Lox](https://github.com/munificent/craftinginterpreters/wiki/Lox-implementations).

This directory contains the self-hosted compiler/runtime code written in `lx` itself (`lx/src`, `lx/commands`, `lx/test`, etc).

## Running

From the repo root (recommended):

```sh
./out/lx run lx/test/anf.test.lx
./out/lx check main.lx

./out/lx anf lx/test/stub/ch17.lx
```

## Module & Path Resolution

Module loading is handled by `lx/src/module_resolution.lx`.

Project root selection:

1. Walk up from the current working directory until a `.lxroot` file is found; that directory is the project root.
2. Otherwise, if `Lx.env["LX_ROOT"]` is set, use it (relative values are resolved against the current working directory).
3. Otherwise, fall back to the current working directory.

Module base selection:

- If `<projectRoot>/lx` exists, it becomes the module base. This allows imports like `"src/..."` and `"test/..."` to work even when invoked from the repo root (without `cd lx`).
- If the import path starts with `lx/`, it is resolved relative to `<projectRoot>` (useful for running files like `lx/test/...` from the repo root).
- Absolute paths (`/…`) are used as-is.

## Builtin Native Functions

Native builtins are documented in `../API.md` (this is the source of truth for editor hover docs / LSP).

### Prelude builtins (`lx/globals.lx`)

- `_1(cb: fn) -> fn` Wrap a callback as a unary function (ignores extra args).
- `_2(cb: fn) -> fn` Wrap a callback as a binary function (ignores extra args).
- `first(coll: array | string) -> any` First element/char.
- `last(coll: array | string) -> any` Last element/char.
- `each(arr: array, cb: fn(x, i, arr, abort)) -> nil` Iterate; call `abort()` to stop early.
- `fold(arr: array, acc, cb: fn(acc, x, i, abort) -> acc) -> any` Left fold.
- `foldr(arr: array, acc, cb: fn(acc, x, i, abort) -> acc) -> any` Right fold.
- `map(arr: array, cb: fn(x, i, arr) -> y) -> array` Map to a new array.
- `filter(arr: array, cb: fn(x, i, arr) -> bool) -> array` Filter into a new array.
- `contains(haystack: array | string, needle) -> bool` Membership test for arrays/strings.
- `take(arr: array, n: number) -> array` Prefix.
- `drop(arr: array, n: number) -> array` Suffix.
- `startsWith(s: string, prefix: string) -> bool` String prefix test.
- `endsWith(s: string, suffix: string) -> bool` String suffix test.
- `max(a, b) -> any` Maximum of two values.
- `min(a, b) -> any` Minimum of two values.
- `sort(arr: array, less: fn(a, b) -> bool) -> array` Stable-ish merge sort returning a new array.
