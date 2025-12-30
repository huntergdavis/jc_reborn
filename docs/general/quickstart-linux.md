# Quick Start - PS1 Development on Linux

## Clone and Setup (First Time)

```bash
# Clone repository
git clone https://github.com/huntergdavis/jc_reborn.git
cd jc_reborn

# Checkout PS1 branch
git checkout ps1

# Build Docker container (takes ~10 minutes first time)
docker build --platform linux/amd64 -f Dockerfile.ps1 -t jc-reborn-ps1-dev:amd64 .
```

## Quick Build & Test Commands

### Build Minimal Test (Known Working)
```bash
# Single command to build and create CD
docker run --rm --platform linux/amd64 -v $(pwd):/project jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && rm -rf build-minimal && mkdir build-minimal && \
  cp CMakeLists.minimal.txt build-minimal/CMakeLists.txt && \
  cp ps1_minimal_main.c build-minimal/ && cd build-minimal && \
  cmake . && make && cd /project && mkpsxiso cd_layout_minimal.xml"

# Test in DuckStation
duckstation-qt ps1_minimal.cue
```

### Build Full Game (Visual Debug)
```bash
# Build
docker run --rm --platform linux/amd64 -v $(pwd):/project jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project/build-ps1 && make clean && make"

# Create CD image
docker run --rm --platform linux/amd64 -v $(pwd):/project jc-reborn-ps1-dev:amd64 \
  bash -c "cd /project && mkpsxiso cd_layout.xml"

# Test in DuckStation - WATCH FOR COLORED SCREENS
duckstation-qt jcreborn.cue
```

## Visual Debug Color Code

When testing jcreborn.cue, note which colors appear:

- **RED** (2 sec) = main() reached! ✅
- **GREEN** (2 sec) = CD-ROM initialized ✅
- **BLUE** (2 sec) = Resources parsed ✅
- **PURPLE** (2 sec) = Starting graphics init ✅
- **YELLOW** = ERROR: CD-ROM failed ❌
- **NO COLORS** = Crash before main() ❌

## Get DuckStation (Linux)

```bash
# Download
wget https://github.com/stenzek/duckstation/releases/latest/download/duckstation-qt-x64.AppImage
chmod +x duckstation-qt-x64.AppImage
sudo mv duckstation-qt-x64.AppImage /usr/local/bin/duckstation-qt

# Run
duckstation-qt
```

## Where to Start

1. **Read**: `PS1_DEVELOPMENT_GUIDE.md` - Comprehensive setup and troubleshooting
2. **Review**: `PS1_TESTING_SESSION_5.md` - Latest testing notes
3. **Build**: Minimal test first to verify environment
4. **Test**: Full game and report color sequence
5. **Debug**: Based on colors, follow guide's "Next Steps" section

## Current Issue

**Full game hangs before main()** - Visual debug will tell us where!

## Key Files

- `jc_reborn.c` - Main entry, visual debug markers
- `graphics_ps1.c` - GPU interface
- `cdrom_ps1.c` - CD file access
- `Dockerfile.ps1` - Build environment
- `cd_layout.xml` - CD image layout

## Get Help

- Session logs: `PS1_TESTING_SESSION_*.md`
- Full guide: `PS1_DEVELOPMENT_GUIDE.md`
- Git log: `git log --oneline`
- Last commit: `git show HEAD`
