# Bootstrap Driver Scripts

This directory contains scripts for bootstrapping the lx compiler when making non-backward compatible opcode changes.

## When You Need These Scripts

Use these scripts when:
- Adding, removing, or renumbering opcodes in the VM
- Changing opcode semantics in a way that breaks compatibility with existing bytecode
- The embedded compiler bytecode (lxlx.h/lxglobals.h) needs to be rebuilt with new codegen

**Why?** The lx compiler embeds its own bytecode (compiled with an older version). When opcodes change incompatibly, the old VM can't run the new bytecode, creating a bootstrapping problem.

## The Scripts

### `build-lxlx-driver.lx`
Compiles `cmd/mlx.lx` (the lx compiler) using the NEW codegen from `src/codegen.lx`.

- Uses `profile: "default"` (all passes including ANF inline optimization)
- Recursively compiles all imported modules
- Runs bytecode verification on all functions
- Outputs to `/tmp/lxlx-new.lxobj`
- Codegen uses the most optimized AST available: anf-inline → anf → lower

### `build-globals-driver.lx`
Compiles `globals.lx` (standard library) using the NEW codegen from `src/codegen.lx`.

- Uses `profile: "default"` (all passes including ANF inline optimization)
- Codegen uses the most optimized AST available: anf-inline → anf → lower
- Outputs to `/tmp/lxglobals-new.lxobj`

## Bootstrap Process

When you've made non-backward compatible opcode changes:

### 1. Update the opcode definitions

Edit both:
- `lx/src/types.lx` - lx language opcode enum
- `include/chunk.h` - C language opcode enum

**Important:** Keep the enums synchronized!

### 2. Update the VM implementation

Edit `src/vm.c`:
- Update dispatch table initialization
- Add/remove/modify opcode handlers

### 3. Update supporting code

- `src/debug.c` - Add disassembler cases for new opcodes
- `lx/src/verify-bytecode.lx` - Update STACK_MIN, STACK_EFFECTS, OPERAND_SIZES tables
- Update any tests that reference specific opcodes

### 4. Rebuild embedded bytecode using drivers

Run the drivers with your **system lx** (the old version):

```bash
# From lx/ directory
lx run scripts/build-lxlx-driver.lx
lx run scripts/build-globals-driver.lx
```

**Why this works:** The old VM can still execute the driver scripts (they're just normal lx code). The drivers use the NEW codegen to produce bytecode with the new opcodes.

### 5. Install the new bytecode

Convert the .lxobj files to C headers and install them:

```bash
# From lx/ directory (parent of lx/)
xxd -i lxlx-new.lxobj | sed 's/unsigned char/static const unsigned char/; s/unsigned int/static const unsigned int/' > include/lx/lxlx.h

xxd -i lxglobals-new.lxobj | sed 's/unsigned char/static const unsigned char/; s/unsigned int/static const unsigned int/' > include/lx/lxglobals.h
```

**Note:** Adjust paths if running from different directory.

### 6. Build the compiler without prepare step

The normal build uses `make prepare` which runs the old system lx to compile lx code. Skip this since the old lx can't understand the new opcodes:

```bash
# From lx/ directory
cc src/*.c -o out/lx -Iinclude -O2 -std=c11
```

This builds the VM with:
- New C code (with new opcodes)
- New embedded bytecode (compiled with new codegen)

### 7. Test the new compiler

```bash
# From lx/ directory
../out/lx run test/some-test.lx

# Or run the full test suite
make test
```

### 8. Commit everything together

Commit all changes atomically:
- Opcode enum changes (types.lx, chunk.h)
- VM implementation (vm.c)
- Supporting code (debug.c, verify-bytecode.lx, etc.)
- Rebuilt embedded bytecode (lxlx.h, lxglobals.h)
- Bootstrap drivers (if they needed updates)

## Example: Removing NEW_LOCAL and POP_LOCAL

This process was used in commit `1c82b07` to remove legacy dual-stack opcodes:

1. Removed `NEW_LOCAL` and `POP_LOCAL` from `types.lx` and `chunk.h`
2. Removed handlers from `vm.c` dispatch table
3. Updated `debug.c`, `verify-bytecode.lx`, tests
4. Ran `lx run scripts/build-lxlx-driver.lx` (with system lx)
5. Ran `lx run scripts/build-globals-driver.lx` (with system lx)
6. Installed new bytecode to `include/lx/lxlx.h` and `include/lx/lxglobals.h`
7. Built compiler with `cc src/*.c -o out/lx -Iinclude -O2 -std=c11`
8. All 364 tests passed

## Troubleshooting

**Error: "Unknown opcode X"**
- The new VM doesn't recognize an old opcode in the embedded bytecode
- Solution: Make sure you rebuilt embedded bytecode with the drivers

**Error: Hashmap key type errors in driver**
- You're referencing a removed opcode (e.g., `OP.REMOVED_OPCODE`)
- Solution: Update all lx code to remove references to old opcodes

**Tests fail after rebuild**
- Check that opcode enums in `types.lx` and `chunk.h` match exactly
- Verify all three tables in `verify-bytecode.lx` are updated
- Check that test files don't reference removed opcodes

## Notes

- These scripts should rarely be needed (hopefully never again!)
- They're preserved for historical reference and emergencies
- The normal build process uses `make prepare` which works fine for backward-compatible changes
