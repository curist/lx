# Import Test Fixtures

## Adding New Fixtures

The test driver automatically loads any `.lx` file from this directory.
No code changes needed - just create your fixture file and reference it in a test.

Mock sources (in-memory only) are used for:
- Circular import tests (circular-a.lx, circular-b.lx)
- Simple inline examples (simple.lx, math.lx)

## Diamond Import Pattern

Demonstrates shared dependency handling and import caching:

```
         app.lx
        /      \
   config.lx  logger.lx
        \      /
      constants.lx
```

### Modules

- **constants.lx** - Shared constants (VERSION, MAX_SIZE, DEBUG)
- **config.lx** - Configuration using constants
- **logger.lx** - Logging utilities using constants
- **app.lx** - Main application importing both config and logger

### Key Behavior

When `app.lx` is compiled:
1. Imports `config.lx` → which imports `constants.lx`
2. Imports `logger.lx` → which tries to import `constants.lx` again
3. Cache detects `constants.lx` is already compiled (status: "done")
4. Returns cached result instead of recompiling

Result: `constants.lx` compiled exactly once despite multiple import paths.

### Test Coverage

The diamond pattern test verifies:
- Shared dependency compiled only once
- All modules successfully cached with "done" status
- No circular import errors
- Correct resolution through multiple import levels
