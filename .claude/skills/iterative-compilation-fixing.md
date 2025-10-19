# Iterative Compilation Fixing Skill

Systematically fix compilation errors when porting to embedded/freestanding environments.

## Usage

Run this skill when you need to work through compilation errors file-by-file in a large codebase port.

## Methodology

### 1. Build and Capture First Error

```bash
# Build and show only key info
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  sdk-dev:amd64 bash -c "cd /project/build && \
  make 2>&1 | grep -E '^\[|Building|error:|Error'"
```

**What to look for:**
- `[ X%] Building C object` - Shows progress
- `error:` - Actual compilation errors
- Note which file is currently failing

### 2. Common Error Patterns and Fixes

#### Pattern 1: Missing Standard Headers
**Error**: `stdio.h: No such file or directory`
**Fix**: Add conditional includes
```c
#ifndef PS1_BUILD
#include <stdio.h>
#include <stdlib.h>
#else
/* Provide minimal declarations */
extern int printf(const char *format, ...);
extern void *malloc(size_t size);
#endif
```

#### Pattern 2: FILE Type Not Defined
**Error**: `unknown type name 'FILE'`
**Fix**: Forward declare with include guard
```c
#ifndef _FILE_DEFINED
#define _FILE_DEFINED
typedef struct _FILE FILE;
#endif
```

#### Pattern 3: Typedef Redefinition
**Error**: `redefinition of typedef 'uint8'`
**Fix**: Add header guards
```c
#ifndef MYTYPES_H
#define MYTYPES_H
typedef uint8_t uint8;
#endif
```

#### Pattern 4: Missing size_t
**Error**: `unknown type name 'size_t'`
**Fix**: Use stddef.h (available in freestanding)
```c
#include <stddef.h>  /* Always available in freestanding */
```

#### Pattern 5: Platform-Specific Functions
**Error**: `gettimeofday` undefined
**Fix**: Provide platform-specific implementations
```c
int getCurrentTime() {
#ifdef PS1_BUILD
    return 0;  /* No RTC on PS1 */
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
#endif
}
```

### 3. Iterative Workflow

**Step 1**: Build and identify first failing file
```bash
make 2>&1 | grep "Building.*\.obj" | tail -1
# Shows: [ 23%] Building C object resource.c.obj
```

**Step 2**: Check what errors that file has
```bash
make 2>&1 | grep -A 10 "resource.c:"
```

**Step 3**: Fix errors in that file
- Apply appropriate fix pattern from above
- Use conditional compilation (#ifdef PS1_BUILD)
- Provide minimal stubs for unavailable functions

**Step 4**: Rebuild
```bash
make 2>&1 | grep -E '^\[|error:' | head -20
```

**Step 5**: Repeat for next file
- Each iteration should progress to the next file
- Track progress: "X% compiled" from make output

### 4. File-Level Fix Strategy

For each failing file, apply in order:

1. **Add conditional includes** (stdio.h, stdlib.h, time.h)
2. **Forward declare types** (FILE, size_t from stddef.h)
3. **Add function declarations** (printf, malloc, etc.)
4. **Stub unavailable functions** (gettimeofday → return 0)
5. **Test compile** (should move to next file)

### 5. Track Progress

Keep notes on which files are done:
```markdown
## Compilation Progress

✅ jc_reborn.c (5%)
✅ utils.c (11%)
✅ uncompress.c (17%)
⏳ resource.c (23%) - needs CD-ROM I/O stubs
⬜ dump.c
⬜ story.c
... (14 files remaining)
```

### 6. Common Freestanding Patterns

**Always available in -ffreestanding:**
- `<stddef.h>` - size_t, NULL, offsetof
- `<stdint.h>` - uint8_t, int32_t, etc.
- `<stdalign.h>` - alignment macros
- `<stdarg.h>` - va_list for variadic functions
- `<stdnoreturn.h>` - noreturn functions

**Never available:**
- `<stdio.h>` - FILE, printf, fopen
- `<stdlib.h>` - malloc, exit, atoi
- `<time.h>` - time_t, gettimeofday
- `<string.h>` - Usually available but check

**Platform provides:**
- printf() - Usually available (PSn00bSDK has it)
- malloc() - Platform memory allocator
- memcpy/memset - Usually provided

### 7. Create Reusable Header

For complex ports, create a platform compatibility header:

```c
// platform_compat.h
#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef PS1_BUILD
/* PS1-specific declarations */
#ifndef _FILE_DEFINED
#define _FILE_DEFINED
typedef struct _FILE FILE;
#endif

extern int printf(const char *fmt, ...);
extern int vprintf(const char *fmt, va_list args);
extern void *malloc(size_t size);
extern void exit(int status) __attribute__((noreturn));

#else
/* Desktop platforms */
#include <stdio.h>
#include <stdlib.h>
#endif

#endif /* PLATFORM_COMPAT_H */
```

Then just:
```c
#include "platform_compat.h"
```

## Success Metrics

- **Progress**: Each iteration advances to next file
- **No regressions**: Previously compiled files still compile
- **Clean warnings**: Minimize -Wpedantic warnings
- **Percentage**: Track % compiled (from make output)

## Efficiency Tips

1. **Parallel builds**: Use `make -j2` to compile multiple files
2. **Filter output**: `grep` for errors only, ignore warnings initially
3. **Batch similar files**: If multiple files need same fix, do them together
4. **Commit frequently**: After each successful file or logical group

## Example Session

```bash
# Iteration 1: jc_reborn.c fails
make | grep error  # Find: FILE undefined
# Fix: Add FILE typedef
make | grep "^\["  # [ 5%] Building jc_reborn.c

# Iteration 2: utils.c fails
make | grep error  # Find: time.h missing
# Fix: Conditional include + stub functions
make | grep "^\["  # [ 11%] Building utils.c

# Iteration 3: uncompress.c compiles!
make | grep "^\["  # [ 17%] Building uncompress.c (no errors)

# Iteration 4: resource.c fails
make | grep error  # Find: fopen/fread undefined
# Fix: CD-ROM I/O adaptation needed
```

## Success Criteria

- All files compile (100%)
- Zero errors, minimal warnings
- Platform-specific code properly isolated with #ifdef
- No loss of functionality on other platforms
- Commits pushed to git

## Time Estimate

- Simple file (no platform dependencies): 2-5 minutes
- Complex file (I/O, time, graphics): 10-30 minutes
- Entire codebase (15-20 files): 2-4 hours

Expect 15-20 iterations for a typical embedded port.
