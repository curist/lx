# API

## Builtin Native Functions

This document is the source of truth for editor hover docs (LSP) for native builtins implemented in C (`include/native_fn.h`).

### Global builtins (native)

- `print(...values) -> nil` Print values to stdout separated by spaces.
- `println(...values) -> nil` Print values to stdout separated by spaces and a trailing newline.
- `str(value) -> string` Convert a value to a string (uses runtime formatting).
- `join(values: array, sep: string) -> string` Join stringified values with a separator.
- `split(s: string, sep: string) -> array[string]` Split by substring; when `sep == ""`, splits into chars.
- `substr(s: string, start: number, end?: number) -> string` Substring from start to end (exclusive). Supports negative indices (count from end). If end omitted, goes to end of string.
- `startsWith(s: string, prefix: string) -> bool` String prefix test.
- `endsWith(s: string, suffix: string) -> bool` String suffix test.
- `contains(haystack: array | string, needle) -> bool` Membership test for arrays/strings.
- `tolower(s: string) -> string` Lowercase ASCII.
- `toupper(s: string) -> string` Uppercase ASCII.
- `tonumber(s: string) -> number` Parse a string to a number (float).
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
- `reverse(arr: array) -> array` Returns a new array with elements in reverse order (non-mutating).
- `slice(arr: array, start: number, end?: number) -> array` Array slice from start to end (exclusive). Supports negative indices (count from end). If end omitted, goes to end of array.
- `lines(s: string) -> array[string]` Split by `\n` (newline not included in lines).

### `Date` namespace (native)

- `Date` - runtime date/time namespace.
- `Date.RFC3339: string` Default RFC3339 format string.
- `Date.now() -> number` Current unix timestamp (milliseconds).
- `Date.nanotime() -> number` Current unix timestamp (nanoseconds).
- `Date.format(unix: number, format?: string) -> string` Format unix timestamp (milliseconds).
- `Date.parse(s: string, format: string) -> number` Parse a date string, returns unix timestamp (milliseconds).

### `Math` namespace (native)

- `Math` - runtime math namespace.
- `Math.floor(n: number) -> number` Floor (round down) to an integer.
- `Math.sqrt(n: number) -> number` Square root.
- `Math.random() -> number` Random float in `[0, 1)`.

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
- `Lx.fs.readFile(path: string) -> string` Read a file.
- `Lx.fs.writeFile(path: string, content: string) -> true` Write a file.
- `Lx.path` - path helpers namespace.
- `Lx.path.join(...segments: string) -> string` Join path segments.
- `Lx.path.dirname(path: string) -> string` Parent directory.
- `Lx.path.basename(path: string) -> string` Filename component (last segment after final `/`).
- `Lx.stdin` - stdin helpers namespace.
- `Lx.stdin.readAll() -> string` Read all stdin.
- `Lx.stdin.readLine(prompt?: string) -> string | nil` Read one line from stdin (without trailing `\n`), or `nil` on EOF.
- `Lx.stdin.readBytes(n: number) -> string | nil` Read up to `n` bytes from stdin.
- `Lx.stdin.readFd(n: number) -> string | nil` Read up to `n` bytes from stdin using fd I/O (works with `poll`).
- `Lx.stdin.poll(timeoutMs: number) -> bool` Whether stdin is readable within `timeoutMs` (ms). Use `0` to poll and `-1` to wait forever.
- `Lx.stdin.unbuffered() -> nil` Disable stdin stdio buffering (recommended if mixing `poll` with `readLine`/`readBytes`).
- `Lx.stdout` - stdout helpers namespace.
- `Lx.stdout.flush() -> nil` Flush stdout.
- `Lx.stdout.putc(...codes: number) -> nil` Write bytes/chars to stdout.
- `Lx.stderr` - stderr helpers namespace.
- `Lx.stderr.print(...values) -> nil` Print values to stderr separated by spaces.
- `Lx.stderr.println(...values) -> nil` Print values to stderr separated by spaces and a trailing newline.
- `Lx.stderr.flush() -> nil` Flush stderr.
- `Lx.proc` - process helpers namespace.
- `Lx.proc.exec(cmd: string) -> {code,out}` Run a shell command and capture stdout.
- `Lx.proc.system(cmd: string) -> number` Run a shell command (inherits stdio), returning exit code.
- `Lx.globals() -> array[string | number]` List current global keys.
- `Lx.doubleToUint8Array(x: number) -> array[number]` Convert a float64 to 8 bytes (little-endian).
- `Lx.isLxObj(bytes: string | array[number]) -> bool` checks whether a byte buffer looks like an lxobj.
- `Lx.loadObj(bytes: string | array[number], printCode?: bool) -> fn` loads an lxobj and returns a callable closure.
- `Lx.pcall(fn: fn, ...args) -> {ok,value,error}` calls a function and captures runtime errors instead of aborting.
- `Lx.error(message: string) -> never` raises a runtime error (caught by `Lx.pcall`).
- `Lx.exit(code?: number) -> never` Exit the process.
