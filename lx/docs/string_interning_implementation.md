# String Interning Implementation Notes

## Summary

Implemented bounded string interning policy: only strings ≤ 64 bytes are interned.

## Changes

### src/object.c
- Added `INTERN_MAX_LEN` constant (64 bytes)
- Modified `takeString()` to bypass interning for large strings
- Modified `copyString()` to bypass interning for large strings

### src/value.c
- Fixed `valuesEqual()` to perform value-based string comparison
- Strings now compared by length, hash, and content (memcmp)

## Addressing Common Pitfalls

### 1. VM Stack Initialization
✅ **Safe**: Large strings bypass `allocateString()` entirely, avoiding `push()/pop()` requirement.
- Small strings (≤ 64): use `allocateString()` which requires initialized stack
- Large strings (> 64): create directly without stack operations

### 2. Hash Computation for Table Lookups
✅ **Safe**: Hash is computed before any table lookup.
```c
uint64_t hash = hashString(chars, (size_t)length);
if (length <= INTERN_MAX_LEN) {
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  // ...
}
```

### 3. Testing Both Paths
✅ **Tested**: Comprehensive test suite covers both `takeString` and `copyString`:
- `copyString`: String literals (e.g., `"hello"`)
- `takeString`: String concatenation (e.g., `"a" + "b"`)
- Test file: `lx/test/string_interning.test.lx`

### 4. Accidental Interning
✅ **Safe**: No other code paths bypass the threshold check.
- Only `takeString()` and `copyString()` can intern
- Both check `length <= INTERN_MAX_LEN` before calling `allocateString()`
- Verified via grep: no direct calls to `allocateString()` elsewhere

## GC Safety Analysis

### Small Strings (≤ 64 bytes)
Use existing `allocateString()` which protects the string during table insertion:
```c
push(OBJ_VAL(string));
tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
pop();
```

### Large Strings (> 64 bytes)
Direct allocation without GC protection is **safe** because:

1. `heapChars` is raw C memory (not GC-managed)
2. GC only collects objects in `vm.objects` linked list
3. Even if `ALLOCATE_OBJ()` triggers GC, `heapChars` won't be freed
4. Once assigned to `string->chars`, it's protected by the string object

## Semantic Guarantees

- ✅ String equality is **value-based** (not pointer-based)
- ✅ Pointer equality is **never observable** to lx code
- ✅ Hash lookups work for both interned and non-interned strings
- ✅ No behavioral changes to existing code

## Test Coverage

### lx/test/string_interning.test.lx
- Small string interning (≤ 64 bytes)
- Large string value equality (> 64 bytes)
- Boundary case (exactly 64 bytes)
- String concatenation (takeString path)
- Hashmap keys with large strings
- Arrays with large strings
- Both paths (takeString vs copyString)
- GC safety stress test (100 large strings)

All tests pass ✅
