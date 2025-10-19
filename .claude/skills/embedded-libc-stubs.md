# Embedded LibC Stubs Skill

When porting to embedded/freestanding environments, you'll often reach 100% compilation but fail at the linking stage due to missing standard library functions. This skill covers creating minimal stubs for missing libc functions.

## When to Use

- **Symptom**: All files compile (100%) but linking fails with "undefined reference to X"
- **Functions commonly missing**: fprintf, fopen, fclose, fread, fwrite, fseek, ftell, exit, atoi, getenv, etc.
- **Target**: Embedded systems, PS1, freestanding environments

## Understanding the Problem

Freestanding environments (`-ffreestanding`) don't provide a hosted C standard library. While you can declare functions with `extern`, if they're not provided by the SDK, linking will fail:

```
undefined reference to `fprintf'
undefined reference to `fopen'
undefined reference to `exit'
```

## Solution Strategies

### Strategy 1: Use SDK-Provided Alternatives

**Check what your SDK provides**:
```bash
# Find SDK libraries
find /path/to/sdk -name '*.a' | grep libc

# Check what symbols a library provides
nm /path/to/sdk/libc.a | grep printf
```

**Common SDK-provided functions**:
- `printf()` - Usually available (debug output)
- `malloc()/free()` - Platform memory allocator
- `memcpy()/memset()` - Usually in libc or compiler builtins
- `sprintf()/snprintf()` - Sometimes available

**PSn00bSDK example**:
- Has `printf()` for debug output to TTY
- Has basic memory functions
- Does NOT have FILE I/O (fopen/fread/etc)
- Does NOT have `exit()` or `atoi()`

### Strategy 2: Create Minimal Stubs File

Create `ps1_stubs.c` with implementations or no-ops for missing functions:

```c
/* ps1_stubs.c - Minimal implementations for missing libc functions */

#include <stddef.h>
#include <stdarg.h>

/* FILE type stub */
typedef struct _FILE FILE;
#define stderr ((FILE*)2)

/* Stub FILE I/O - PS1 will use CD-ROM or memory */
FILE *fopen(const char *pathname, const char *mode) {
    /* Return null or implement CD-ROM loader */
    return NULL;
}

int fclose(FILE *stream) {
    return 0;  /* No-op */
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    /* Stub: return 0 bytes read */
    return 0;
}

int fseek(FILE *stream, long offset, int whence) {
    return 0;  /* No-op */
}

long ftell(FILE *stream) {
    return 0;  /* No position */
}

int fgetc(FILE *f) {
    return -1;  /* EOF */
}

int fflush(FILE *stream) {
    return 0;  /* No-op */
}

int fputs(const char *s, FILE *stream) {
    /* Could redirect to printf if stream is stderr */
    if (stream == stderr) {
        printf("%s", s);
        return 0;
    }
    return -1;
}

/* fprintf and vfprintf - redirect to printf for stderr */
int fprintf(FILE *stream, const char *format, ...) {
    if (stream == stderr) {
        va_list args;
        va_start(args, format);
        int result = vprintf(format, args);
        va_end(args);
        return result;
    }
    return 0;
}

int vfprintf(FILE *stream, const char *format, va_list args) {
    if (stream == stderr) {
        return vprintf(format, args);
    }
    return 0;
}

/* exit() - halt or loop forever */
void exit(int status) {
    /* On PS1, could return to BIOS or loop forever */
    while(1) {
        /* Infinite loop */
    }
}

/* atoi() - simple integer parsing */
int atoi(const char *str) {
    int result = 0;
    int sign = 1;

    /* Skip whitespace */
    while (*str == ' ' || *str == '\t') str++;

    /* Handle sign */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    /* Convert digits */
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result * sign;
}

/* getenv() - no environment on embedded */
char *getenv(const char *name) {
    return NULL;  /* No environment variables */
}

/* sprintf() - if not provided by SDK */
extern int vsprintf(char *str, const char *format, va_list args);

int sprintf(char *str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsprintf(str, format, args);
    va_end(args);
    return result;
}

/* mkdir/stat - stubs for dump.c (debug feature not needed on PS1) */
struct stat { int st_mode; };

int stat(const char *pathname, struct stat *statbuf) {
    return -1;  /* Always fail - no filesystem */
}

int mkdir(const char *pathname, int mode) {
    return -1;  /* Always fail - no filesystem */
}
```

### Strategy 3: Conditional Compilation

For functions only used in debug/desktop modes, wrap them:

```c
#ifndef PS1_BUILD
void dumpAllResources() {
    /* Desktop: write to filesystem */
    createDumpDirs();
    for (int i = 0; i < numResources; i++) {
        dumpResource(i);
    }
}
#else
void dumpAllResources() {
    /* PS1: no-op or print to debug console */
    printf("Dump not available on PS1\n");
}
#endif
```

### Strategy 4: Link Order Matters

When linking, order matters. Your stubs must come BEFORE standard libraries:

```cmake
# CMakeLists.txt
psn00bsdk_add_executable(jcreborn GPREL
    ${SOURCES}
    ps1_stubs.c  # Add stubs BEFORE linking libraries
)

target_link_libraries(jcreborn PRIVATE
    psxgpu
    psxgte
    psxspu
    psxcd
    c  # Standard library last
)
```

## Common Stub Patterns

### Pattern 1: No-Op Stub
For functions where failure is acceptable:
```c
int fflush(FILE *stream) {
    return 0;  /* Success - did nothing */
}
```

### Pattern 2: Redirect to Available Function
For functions where you have an alternative:
```c
int fprintf(FILE *stream, const char *format, ...) {
    if (stream == stderr) {
        va_list args;
        va_start(args, format);
        int result = vprintf(format, args);  /* SDK provides vprintf */
        va_end(args);
        return result;
    }
    return 0;
}
```

### Pattern 3: Minimal Implementation
For critical functions that need to work:
```c
int atoi(const char *str) {
    int result = 0;
    int sign = 1;

    if (*str == '-') { sign = -1; str++; }
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return result * sign;
}
```

### Pattern 4: Fatal Error Stub
For functions that shouldn't be called:
```c
FILE *fopen(const char *pathname, const char *mode) {
    printf("ERROR: fopen() not supported on PS1\n");
    while(1);  /* Halt */
}
```

## Implementation Workflow

1. **Compile all source files** (should reach 100%)

2. **Attempt to link** - note all undefined references:
```bash
make 2>&1 | grep "undefined reference"
```

3. **Categorize missing functions**:
   - **Critical**: atoi, sprintf (need implementation)
   - **Debug/Optional**: fprintf, fopen (can stub/redirect)
   - **Never called**: exit (can loop forever)

4. **Create stubs file**: `platform_stubs.c`

5. **Add to build**:
```cmake
set(SOURCES
    # ... existing sources ...
    ps1_stubs.c
)
```

6. **Rebuild and verify linking**

7. **Test runtime behavior** - ensure stubs don't break execution

## Testing Stubs

After linking succeeds, test that your stubs work correctly:

```c
/* Test atoi() */
int val = atoi("42");
printf("atoi test: %d\n", val);  // Should print 42

/* Test fprintf to stderr */
fprintf(stderr, "Error test\n");  // Should appear on console

/* Test fopen failure handling */
FILE *f = fopen("test.txt", "r");
if (f == NULL) {
    printf("fopen correctly returned NULL\n");
}
```

## Success Criteria

- ✅ Linking completes with no undefined reference errors
- ✅ Executable boots in emulator/hardware
- ✅ Printf/debug output works
- ✅ No crashes from stub functions
- ✅ Critical functions (atoi, sprintf) behave correctly

## Anti-Patterns to Avoid

❌ **Don't implement full libc** - You don't need complete implementations
❌ **Don't link desktop libc** - It won't work on embedded targets
❌ **Don't ignore undefined references** - Linking must succeed cleanly
❌ **Don't stub without testing** - Verify stubs don't break runtime

## Next Steps After Linking

Once you have a linked executable:
1. Test in emulator (DuckStation for PS1)
2. Implement platform-specific functionality (graphics, input, etc.)
3. Replace stubs with real implementations as needed
4. Profile memory usage and optimize

## Example Session

```bash
# Initial link failure
$ make
[100%] Linking C executable jcreborn.elf
undefined reference to `fprintf'
undefined reference to `fopen'
undefined reference to `exit'
undefined reference to `atoi'

# Create stubs
$ cat > ps1_stubs.c << EOF
int fprintf(FILE *s, const char *f, ...) { return 0; }
FILE *fopen(const char *p, const char *m) { return NULL; }
void exit(int s) { while(1); }
int atoi(const char *s) { /* minimal impl */ }
EOF

# Add to CMakeLists.txt
set(SOURCES ... ps1_stubs.c)

# Rebuild
$ make clean && make
[100%] Linking C executable jcreborn.elf
[100%] Built target jcreborn

# Success!
```

## Platform-Specific Notes

### PS1 (PSn00bSDK)
- Has: printf, vprintf, malloc, free, memcpy, memset
- Missing: All FILE I/O, exit, atoi, getenv, snprintf (sometimes)
- CD-ROM: Use `CdRead()` instead of fopen/fread
- Debug: fprintf(stderr) → vprintf

### Dreamcast
- Has: Basic libc via KallistiOS
- Missing: Some POSIX functions
- Use KOS filesystem for FILE I/O

### Bare Metal ARM
- Has: Nothing (compiler builtins only)
- Missing: Everything
- Link with newlib-nano or implement all functions

## Time Estimate

- Creating basic stubs: 30-60 minutes
- Testing and fixing: 1-2 hours
- Implementing proper I/O: 4-8 hours (for CD-ROM/filesystem)

Total: 2-4 hours for functional stubs, longer for full implementations.
