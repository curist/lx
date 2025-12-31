# lx Numeric Semantics & VM Representation

This document defines:

1. Language-level numeric semantics (`Number`)
2. VM numeric representations (Flonum + Fixnum via NaN-boxing)
3. The “integer context” rules for `%`, bitwise ops, and indexing
4. How these rules support future FastCheck/VM specialization

## 1. Surface Type: `Number`

lx exposes a single surface numeric type:

- `Number`

All numeric literals and numeric operations produce `Number`.

## 2. Integer Contexts (Option B)

Some operations require an *integer-valued* `Number`. These operations accept values like `5` and `5.0`, but reject `5.5`.

An “integer-valued Number” must be:

- A number (`Number`)
- Finite (`isfinite(x)`; not `NaN`/`±Inf`)
- Integral-valued (`trunc(x) == x`)
- Exactly representable as an integer in the required range (no implicit rounding/truncation)

On violation, the VM raises a runtime error (current behavior: prints an error + unwinds and aborts execution).

## 3. Operator Semantics

### 3.1 `+ - *`

- Operands must be `Number`
- Result is a `Number`
- Implemented as IEEE-754 `double` arithmetic by default
- The compiler/VM may use Fixnum fast paths for proven integer-valued operands; on overflow it falls back to Flonum

### 3.2 `/` (Division)

- Operands must be `Number`
- Result is a `Number`
- Current behavior follows IEEE-754 `double` division (so division by `0.0` can produce `±Inf`/`NaN`)

If a later policy disallows non-finite values, `/` will be updated to runtime-error on divisor `0` and/or non-finite results.

### 3.3 `%` (Remainder)

`a % b` is defined only for integer-valued numbers:

- Both operands must satisfy the integer-context rule above
- Division by zero is a runtime error
- Semantics are **truncating remainder** (C-style): the quotient is truncated toward zero, and the remainder has the same sign as `a`

### 3.4 Bitwise Ops: `& | ^ << >>`

Bitwise operations are integer contexts:

- Operands must satisfy the integer-context rule
- Operands must fit in **signed 32-bit** range (`INT32_MIN..INT32_MAX`)
- Shifts require a shift count in `0..31`
- Results are returned as `Number` (typically a Fixnum when representable)

## 4. Indexing Semantics

### 4.1 Arrays

`arr[idx]` and `arr[idx] = value` require `idx` to satisfy the integer-context rule.

- If `idx` is out of bounds, `arr[idx]` evaluates to `nil`
- If `idx` is out of bounds, `arr[idx] = value` evaluates to `nil` (and does not grow the array)

### 4.2 Strings

`str[idx]` requires `idx` to satisfy the integer-context rule.

- If `idx` is out of bounds, the result is `nil`

## 5. VM Representations (NaN-boxing + Fixnum)

Internally, a `Number` may be represented as:

- **Flonum**: IEEE-754 `double` (inexact)
- **Fixnum**: immediate signed integer stored in the NaN-box payload (exact integer fast path)

### 5.1 Tag Layout (Refinement)

The existing NaN-boxing uses 2 tag bits for immediates:

- `01` = `nil`
- `10` = `false`
- `11` = `true`
- `00` = **fixnum**

Objects are encoded as `SIGN_BIT | QNAN | pointer`.

### 5.2 Fixnum Range

With a 48-bit NaN payload and 2 tag bits, fixnums have **46 signed bits**.

The authoritative constants/macros live in `include/value.h`.

## 6. Canonicalization (Keeping One `Number`)

To keep `5` and `5.0` semantically identical while still enabling fast paths:

- The object loader may canonicalize numeric constants: if a serialized `double` is finite, integral-valued, and within fixnum range, it may be stored as a Fixnum at runtime.
- `OP_CONST_BYTE` pushes a Fixnum for small integer constants.

## 7. Equality and Hashing

- When both operands are numeric, equality is by numeric value (Fixnum and Flonum compare equal if their numeric values are equal).
- `-0.0` and `+0.0` are treated as equal and hash the same for map keys.
- If `NaN` values exist (current `/` behavior can create them), IEEE rules apply: `NaN != NaN`, which makes `NaN` an impractical hashmap key.

## 8. FastCheck (Future Optimization)

FastCheck (when implemented) can treat integer-context operations (`%`, bitwise ops, indexing) as strong signals:

- They require integer-valued inputs on the non-error path
- They produce integer-valued results (for `%` and bitwise ops)

This enables specializing hot numeric code to unboxed Fixnum operations while preserving the surface `Number` semantics.
