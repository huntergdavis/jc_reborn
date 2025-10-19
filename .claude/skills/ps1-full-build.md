# PS1 Full Build Skill

Perform a complete CMake build of the PS1 port to generate executable and CD image.

## Usage

Run this skill to build the complete PS1 project using CMake and PSn00bSDK.

## Task

1. Verify Docker image exists (jc-reborn-ps1-dev:amd64)
2. Copy CMakeLists.ps1.txt to CMakeLists.txt (if needed)
3. Create/clean build-ps1 directory
4. Run CMake configuration
5. Run make build
6. Report build status:
   - Show compilation progress
   - Report any errors/warnings
   - Show final executable size
   - Verify CD image generation (if mkpsxiso configured)

## Build Commands

```bash
# In Docker container
cd /project
cp CMakeLists.ps1.txt CMakeLists.txt
rm -rf build-ps1
mkdir -p build-ps1
cd build-ps1
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Success Criteria

- CMake configures without errors
- All source files compile successfully
- Executable links successfully (jcreborn.elf or jcreborn.exe)
- Total build time reported
- Final binary size reported

## Common Issues

- Missing PSN00BSDK environment variable → Check Dockerfile.ps1
- FILE type errors → Add `typedef struct _FILE FILE;` before includes
- SDL headers included → Use `#ifdef PS1_BUILD` conditionals
- Missing -ffreestanding flag → Check CMakeLists.ps1.txt compiler flags
