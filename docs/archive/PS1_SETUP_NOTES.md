# PS1 Development Environment Setup Notes

## Status

- ✅ DuckStation emulator downloaded to /tmp/DuckStation.app
- ✅ CMake installed (version 3.24.1)
- ⏳ PSn00bSDK toolchain installation (next step)

## PSn00bSDK Installation Options

### Option 1: Precompiled Toolchain (Recommended for macOS)
1. Download precompiled MIPS GCC toolchain from PSn00bSDK releases
2. Extract to `/usr/local/mipsel-none-elf` or custom location
3. Clone and build PSn00bSDK
4. Install PSn00bSDK

### Option 2: Build Toolchain from Source (Linux Required)
- Requires Linux environment (even for Windows/macOS targets)
- GCC cannot be built natively on macOS for MIPS target
- Would need to use Docker/VM or cross-compile
- Takes ~30 minutes to compile

### Recommended Approach for This Project

Since we're on macOS and building the full GCC toolchain from source requires Linux, we have a few options:

1. **Use precompiled toolchain** (if available for macOS)
2. **Use Docker with Linux** to build toolchain
3. **Cross-compile approach** - build Linux toolchain first, then cross-compile for macOS
4. **Alternative SDK** - Check if there are macOS-native PS1 development tools

## DuckStation Emulator

- Location: `/tmp/DuckStation.app`
- Can move to `/Applications` for permanent installation
- Supports both Intel and Apple Silicon Macs
- Requires PS1 BIOS files (must be obtained separately)

## Next Steps

1. Research precompiled PSn00bSDK toolchain for macOS
2. If not available, set up Docker environment for building
3. Clone PSn00bSDK repository
4. Build and install SDK
5. Test with hello world program

## Alternative: Start Port Without Full Toolchain

We can begin porting work now by:
1. Creating PS1-specific source files (graphics_ps1.c, sound_ps1.c, etc.)
2. Using PSn00bSDK header files as reference
3. Stubbing out PS1 API calls with proper signatures
4. Testing will wait until toolchain is ready

This allows us to make progress on the port architecture while toolchain setup continues.

## Resources

- PSn00bSDK: https://github.com/Lameguy64/PSn00bSDK
- Installation docs: https://github.com/Lameguy64/PSn00bSDK/blob/master/doc/installation.md
- Toolchain build: https://github.com/Lameguy64/PSn00bSDK/blob/master/doc/toolchain.md
- Library reference: https://psx.arthus.net/sdk/PSn00SDK/Docs/libn00bref.pdf
