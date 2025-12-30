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

The runtime provides some global builtins plus a `Lx` namespace.

Global builtins:

- `slurp(path: string) -> string` reads a file.
- `spit(path: string, content: string) -> true` writes a file.
- Paths are kept simple: no tilde expansion, no shell expansion. Relative paths are resolved against the process current working directory.

`Lx` namespace:

- `Lx.args: array[string]` CLI args.
- `Lx.env: map[string]string` environment variables.
- `Lx.version: string` runtime version string.
- `Lx.fs`: filesystem helpers:
  - `Lx.fs.cwd() -> string`
  - `Lx.fs.exists(path: string) -> bool`
  - `Lx.fs.stat(path: string) -> map | nil` (e.g. `.{type,size,mtime,mode}`)
  - `Lx.fs.realpath(path: string) -> string | nil`
- `Lx.path`: path helpers:
  - `Lx.path.join(...segments: string) -> string`
  - `Lx.path.dirname(path: string) -> string`
- `Lx.stdin`: stdin helpers (native builds):
  - `Lx.stdin.readAll() -> string`
  - `Lx.stdin.readLine(prompt?: string) -> string | nil`
- `Lx.isLxObj(bytes: string | array[number]) -> bool` checks whether a byte buffer looks like an lxobj.
- `Lx.loadObj(bytes: string | array[number], printCode?: bool) -> fn` loads an lxobj and returns a callable closure.
- `Lx.pcall(fn: fn, ...args) -> {ok,value,error}` calls a function and captures runtime errors instead of aborting.
- `Lx.error(message: string) -> never` raises a runtime error (caught by `Lx.pcall`).
