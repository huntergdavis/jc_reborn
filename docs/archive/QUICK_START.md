# PS1 Port - Quick Start Guide

## Initial Setup (One-Time)

```bash
# 1. Install Docker
./setup-docker.sh

# 2. IMPORTANT: Log out and log back in
#    This activates the docker group membership
#    Without this, you'll need to use 'sudo' (which creates root-owned files)

# 3. After logging back in, build the Docker image
./build-docker-image.sh
```

## Regular Development Workflow

```bash
# Check status
./check-ps1-status.sh

# Build and test (all-in-one)
./rebuild-and-test.sh

# Or step-by-step:
./build-ps1.sh           # Compile
./analyze-build.sh       # Check sizes
./make-cd-image.sh       # Create ISO
./test-ps1.sh            # Launch DuckStation
```

## Current Status

**BSS Size**: ✅ 22 KB (target < 50 KB)
**Total Size**: ✅ 104 KB (target < 256 KB)
**Build**: Ready to test

## Common Issues

### "Permission denied" when running docker commands
**Solution**: Log out and log back in (docker group membership needs refresh)

### CD image files owned by root
**Cause**: Ran scripts with `sudo` before logging out/in
**Fix**:
```bash
sudo chown $USER:$USER jcreborn.bin jcreborn.cue
```

### DuckStation can't find .cue file
**Cause**: File ownership or flatpak sandbox permissions
**Fix**: Ensure files are owned by your user (not root)

## File Loading & Graphics Status

### ✅ File Loading - COMPLETE
- CD-ROM I/O layer implemented
- PS1File* wrapper working
- Sector-based reading functional

### ✅ Graphics - 95% COMPLETE
- GPU initialization ✅
- Sprite rendering ✅
- Layer compositing ✅
- Background loading ✅
- Double buffering ✅

## Next Steps
1. Fix any remaining boot issues
2. Test resource loading from CD
3. Verify graphics rendering
