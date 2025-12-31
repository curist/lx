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

This section is the source of truth for editor hover docs (LSP).

The runtime provides:

- Native builtins (implemented in C, see include/native_fn.h)
- Prelude helpers (implemented in lx, see lx/globals.lx)

### Global builtins (native)

- `print(...values) -> nil` Print values to stdout separated by spaces.
- `println(...values) -> nil` Print values to stdout separated by spaces and a trailing newline.
- `groan(...values) -> nil` Print values to stderr separated by spaces.
- `groanln(...values) -> nil` Print values to stderr separated by spaces and a trailing newline.
- `putc(...codes: number) -> nil` Write bytes/chars to stdout.
- `str(value) -> string` Convert a value to a string (uses runtime formatting).
- `join(values: array, sep: string) -> string` Join stringified values with a separator.
- `split(s: string, sep: string) -> array[string]` Split by substring; when `sep == ""`, splits into chars.
- `tolower(s: string) -> string` Lowercase ASCII.
- `toupper(s: string) -> string` Uppercase ASCII.
- `tonumber(s: string) -> number` Parse a string to a number (float).
- `int(n: number) -> number` Truncate a number toward zero.
- `sqrt(n: number) -> number` Square root.
- `random() -> number` Random float in `[0, 1)`.
- `chr(code: number) -> string` Convert a byte (0–255) to a 1-byte string.
- `ord(ch: string) -> number` Convert a 1-character string to its byte value.
- `len(x: string | array) -> number` Length of a string/array.
- `type(x) -> string` One of `nil|boolean|number|fn|string|enum|map|array`.
- `range(x: number | string | array | map | enum) -> array` For number: `[0..n-1]`; for string: chars; for map/enum: keys.
- `keys(x: map | enum) -> array[string]` Keys/names.
- `nameOf(enum: enum, value: number | string) -> string | nil` Reverse lookup (value → name).
- `push(arr: array, value) -> array` Mutates `arr` by appending `value`.
- `pop(arr: array) -> value | nil` Mutates `arr` by removing and returning the last element.
- `concat(a: array, b: array) -> array` New array with elements of `a` followed by `b`.
- `lines(s: string) -> array[string]` Split by `\n` (newline not included in lines).
- `getline() -> string | nil` Read one line from stdin (without trailing `\n`), or `nil` on EOF.
- `read(n: number) -> string | nil` Read up to `n` bytes from stdin, or `nil` on EOF.
- `slurp(path: string) -> string` Read a file.
- `spit(path: string, content: string) -> true` Write a file.
- `exec(cmd: string) -> {code,out}` Run a shell command and capture stdout.
- `system(cmd: string) -> number` Run a shell command (inherits stdio), returning exit code.

Notes:

- `slurp`/`spit` keep paths simple: no tilde expansion, no shell expansion. Relative paths resolve against process CWD.

### `Date` namespace (native)

- `Date` - runtime date/time namespace.
- `Date.RFC3339: string` Default RFC3339 format string.
- `Date.time() -> number` Current unix timestamp (milliseconds).
- `Date.nanotime() -> number` Current unix timestamp (nanoseconds).
- `Date.format(unix: number, format?: string) -> string` Format unix timestamp (milliseconds).
- `Date.parse(s: string, format: string) -> number` Parse a date string, returns unix timestamp (milliseconds).

### `Lx` namespace (native)

- `Lx` - runtime namespace.
- `Lx.args: array[string]` CLI args.
- `Lx.env: map[string]string` environment variables.
- `Lx.version: string` runtime version string.
- `Lx.fs` - filesystem helpers namespace.
- `Lx.fs.cwd() -> string` Current working directory.
- `Lx.fs.exists(path: string) -> bool` Whether a path exists.
- `Lx.fs.stat(path: string) -> map | nil` File metadata (e.g. `.{type,size,mtime,mode}`).
- `Lx.fs.realpath(path: string) -> string | nil` Resolve symlinks, or `nil` on failure.
- `Lx.path` - path helpers namespace.
- `Lx.path.join(...segments: string) -> string` Join path segments.
- `Lx.path.dirname(path: string) -> string` Parent directory.
- `Lx.stdin` - stdin helpers namespace.
- `Lx.stdin.readAll() -> string` Read all stdin.
- `Lx.stdin.readLine(prompt?: string) -> string | nil` Read one line from stdin.
- `Lx.stdin.readBytes(n: number) -> string | nil` Read up to `n` bytes from stdin.
- `Lx.stdout` - stdout helpers namespace.
- `Lx.stdout.flush() -> nil` Flush stdout.
- `Lx.globals() -> array[string | number]` List current global keys.
- `Lx.doubleToUint8Array(x: number) -> array[number]` Convert a float64 to 8 bytes (little-endian).
- `Lx.isLxObj(bytes: string | array[number]) -> bool` checks whether a byte buffer looks like an lxobj.
- `Lx.loadObj(bytes: string | array[number], printCode?: bool) -> fn` loads an lxobj and returns a callable closure.
- `Lx.pcall(fn: fn, ...args) -> {ok,value,error}` calls a function and captures runtime errors instead of aborting.
- `Lx.error(message: string) -> never` raises a runtime error (caught by `Lx.pcall`).
- `Lx.exit(code?: number) -> never` Exit the process.

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
