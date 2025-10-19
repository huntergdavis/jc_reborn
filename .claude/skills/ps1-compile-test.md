# PS1 Compile Test Skill

Test compilation of PS1 platform files using Docker and PSn00bSDK.

## Usage

Run this skill to quickly test if PS1 source files compile without errors.

## Task

1. Verify Docker image exists (jc-reborn-ps1-dev:amd64)
2. Compile all three PS1 platform files in parallel:
   - graphics_ps1.c
   - events_ps1.c
   - sound_ps1.c
3. Report compilation status:
   - Show any errors/warnings
   - Show object file sizes on success
   - Summarize results

## Docker Command Template

```bash
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mipsel-none-elf-gcc -ffreestanding \
  -I/opt/psn00bsdk/PSn00bSDK-0.24-Linux/include/libpsn00b \
  -c <FILE>.c -o /tmp/<FILE>.o 2>&1"
```

## Success Criteria

- All three files compile with zero warnings
- Object files generated successfully
- File sizes reported (graphics_ps1.o ~21KB, events_ps1.o ~3KB, sound_ps1.o ~3KB)
