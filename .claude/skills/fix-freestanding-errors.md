# Fix Freestanding Compilation Errors Skill

Automatically fix common compilation errors when porting to embedded/freestanding environments like PS1.

## Usage

Run this skill when you encounter FILE type errors, missing stdio.h, or standard library issues during PS1 compilation.

## Common Error Patterns

### 1. FILE Type Not Defined
**Error**: `unknown type name 'FILE'`
**Fix**: Add before includes that use FILE:
```c
/* Forward declare FILE for -ffreestanding */
typedef struct _FILE FILE;
```

### 2. Custom Integer Types Not Defined
**Error**: `unknown type name 'uint8'`
**Fix**: Include mytypes.h before other headers:
```c
#include "mytypes.h"  /* Defines uint8, uint16, uint32, etc. */
```

### 3. Standard Library Headers Missing
**Error**: `stdio.h: No such file or directory`
**Fix**: Remove or conditionally compile stdio.h includes:
```c
#ifdef PS1_BUILD
/* PS1 doesn't have standard library */
#else
#include <stdio.h>
#endif
```

### 4. printf/fprintf Not Available
**Fix**: Use PSn00bSDK's printf or remove debug prints:
```c
#ifdef PS1_BUILD
/* PSn00bSDK provides minimal printf via utils.h */
#else
printf("Debug message\n");
#endif
```

## Task Steps

1. Read the compilation error output
2. Identify error pattern (FILE type, stdint types, stdio.h, etc.)
3. Apply appropriate fix from patterns above
4. Re-compile to verify fix
5. Report results

## Compilation Test Command

```bash
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mipsel-none-elf-gcc -ffreestanding \
  -I/opt/psn00bsdk/PSn00bSDK-0.24-Linux/include/libpsn00b \
  -c <FILE>.c -o /tmp/<FILE>.o 2>&1"
```

## Success Criteria

- Compilation succeeds with zero errors
- Warnings minimized (ideally zero)
- Object file generated successfully
