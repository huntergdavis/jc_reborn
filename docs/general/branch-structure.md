# Johnny Reborn - Branch Structure and Platform Ports

This document describes the branch organization and various platform ports of Johnny Reborn.

## Main Branches

### `main`
- Primary development branch
- SDL2-based for desktop platforms (Linux, macOS, Windows)
- Full feature set
- Latest stable code

### `ps1`
- **PlayStation 1 port** (active development)
- Based on `4mb2025` memory-optimized branch
- Built with PSn00bSDK 0.24 and mipsel-none-elf-gcc
- Dockerized build environment for Linux/macOS
- Visual debugging system (colored screens) since printf() doesn't work
- Memory: 350KB peak usage fits easily in PS1's 2MB RAM
- Resolution: Native 640x480 interlaced (matches engine's design)
- Testing: DuckStation emulator recommended
- See `docs/ps1/` for complete PS1 workflow

### `4mb2025`
- **Memory-optimized version**
- 350KB usage (down from 20MB+)
- LRU caching with lazy loading
- Pinning system for active resources
- Perfect base for embedded ports
- Target for all low-memory platforms

## Platform Ports

### Embedded Systems

**`dreamcast`** - Sega Dreamcast
- KallistiOS-based
- 16MB RAM, PowerVR GPU
- Native 640x480 support

**`lowmem` / `lowmemdc`** - Low-memory embedded systems
- <1MB RAM variants
- Stripped-down feature set
- Essential animations only

**`SDL1.2` / `SDL_1.2_backport`** - RetroFW devices
- GCW Zero, RS-97, RG350
- SDL 1.2 for compatibility
- 32-64MB RAM typical
- Hardware-scaled output

### Specialized Hardware

**`inkplate`** - InkPlate e-paper displays
- 20K pre-rendered frames in `rawframes/`
- E-ink optimized rendering
- Ultra-low power consumption
- Greyscale display

**`bash`** - Text-only version
- Pure ASCII art rendering
- No graphics dependencies
- Runs in terminal
- Educational/debugging tool

### Accessibility

**`closed_captions`** - Accessibility features
- Scene descriptions for visually impaired
- Closed captioning support
- Audio descriptions
- Enhanced contrast modes

### Web

**`emscripten`** - Web/browser port (incomplete)
- Emscripten/WebAssembly compilation
- Canvas rendering
- Web Audio API
- Work in progress

## Branch Strategy

### Creating a New Platform Port

1. **Start from appropriate base**:
   - Memory-constrained (<4MB RAM): Branch from `4mb2025`
   - Standard hardware (>4MB RAM): Branch from `main`

2. **Platform-specific files**:
   - `graphics_<platform>.c/h` - Graphics layer
   - `sound_<platform>.c/h` - Audio layer
   - `events_<platform>.c/h` - Input layer
   - `Makefile.<platform>` or `CMakeLists.<platform>.txt`

3. **Keep core unchanged**:
   - Never modify core engine files
   - All platform code in separate files
   - Use conditional compilation if needed

4. **Document thoroughly**:
   - Create `<PLATFORM>_README.md`
   - Build instructions
   - Testing procedure
   - Known issues

### Code Reuse Statistics

From PS1 port analysis:
- **Platform-independent code**: ~4000 lines (63%)
- **Platform-specific code**: ~900 lines (37%)
- **Files requiring porting**: 3 (graphics, sound, events)
- **Files unchanged**: 24 (core engine)

This high reuse ratio demonstrates the value of clean architecture.

## Platform Comparison

| Platform | RAM | Resolution | Status | Branch |
|----------|-----|------------|--------|--------|
| Desktop (SDL2) | 512MB+ | 640x480 | ✅ Complete | `main` |
| PlayStation 1 | 2MB | 640x480 | 🔨 Active | `ps1` |
| Dreamcast | 16MB | 640x480 | ✅ Complete | `dreamcast` |
| RetroFW | 32-64MB | 320x240 | ✅ Complete | `SDL1.2` |
| InkPlate | 512KB | 800x600 | ✅ Complete | `inkplate` |
| Low-memory | <1MB | Variable | ✅ Complete | `lowmem` |
| Bash/Terminal | N/A | Terminal | ✅ Complete | `bash` |
| Web/Emscripten | N/A | Canvas | ⚠️ WIP | `emscripten` |

## Platform-Specific Notes

### PS1
**Critical rules**:
- Do NOT call `CdInit()` when booting from CD-ROM (causes crash)
- printf() does not output to DuckStation TTY - use visual debugging
- BSS size must be <50KB - use malloc for large buffers
- Test frequently in DuckStation emulator

**Build**:
```bash
docker run --rm --platform linux/amd64 \
  -v $(pwd):/project \
  jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ps1 && make"
```

### Dreamcast
**Features**:
- Native 640x480 with PowerVR GPU
- CD-ROM based distribution
- KallistiOS toolchain
- Controller support via maple bus

### RetroFW
**Considerations**:
- Hardware scaling to native resolution
- SDL 1.2 compatibility
- Limited button set
- Battery-powered devices

### InkPlate
**Unique aspects**:
- Pre-rendered frame approach
- E-ink refresh optimization
- Ultra-low power
- Suitable for art installations

## Merging Strategy

- **Upstream**: `main` receives all general improvements
- **Downstream**: Platform branches periodically merge from `main`
- **Platform-specific**: Changes stay in platform branches
- **Memory optimizations**: Start in `4mb2025`, may merge to `main`

## Development Guidelines

1. **Test on multiple platforms** before merging to `main`
2. **Keep platform branches up to date** with `main`
3. **Document platform-specific quirks** in respective READMEs
4. **Share optimizations** that benefit all platforms
5. **Maintain backwards compatibility** when possible

## Contributing a New Port

Interested in porting to a new platform?

1. Open an issue describing the target platform
2. Create a branch from `4mb2025` or `main`
3. Implement platform-specific layers
4. Test thoroughly
5. Document build process and quirks
6. Submit pull request with platform documentation

## See Also

- [Architecture](architecture.md) - Core system design
- [Memory Management](memory-management.md) - Memory optimization strategies
- PS1 documentation in `docs/ps1/` for detailed platform port example
