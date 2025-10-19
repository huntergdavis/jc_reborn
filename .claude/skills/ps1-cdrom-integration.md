# PS1 CD-ROM File Access Integration

## Overview

PlayStation 1 uses CD-ROM for data storage instead of traditional file systems. This skill documents the pattern for integrating CD-ROM access into PS1 ports using PSn00bSDK.

## Key Concepts

### CD-ROM Location Structure (CdlLOC)

PS1 CD-ROM uses BCD-encoded minute/second/sector addressing:

```c
typedef struct _CdlLOC {
    uint8_t minute;  // BCD encoded
    uint8_t second;  // BCD encoded
    uint8_t sector;  // BCD encoded (frame)
    uint8_t track;   // Track number
} CdlLOC;
```

### CD-ROM File Structure (CdlFILE)

```c
typedef struct _CdlFILE {
    CdlLOC pos;      // CD-ROM position of file start
    int size;        // File size in bytes
    char name[16];   // File name
} CdlFILE;
```

### Sector-Based I/O

- CD-ROM reads in 2048-byte sectors (Mode 1)
- Must convert byte offsets to sector positions
- Use `CdPosToInt()` and `CdIntToPos()` for conversions

## Implementation Pattern

### 1. File Handle Abstraction Layer

Create a wrapper that maps traditional FILE I/O to CD-ROM:

```c
#define MAX_CD_FILES 8
static CdlFILE cdFiles[MAX_CD_FILES];
static uint8 cdFileInUse[MAX_CD_FILES];
static uint32 cdFilePos[MAX_CD_FILES];  // Track position in bytes
```

**Key insight**: Store position in **bytes**, not sectors, for compatibility with fseek/ftell.

### 2. Initialization

```c
int cdromInit() {
    CdInit();
    while (CdSync(1, NULL) > 0);  // Wait for ready

    uint8 mode = CdlModeSpeed;
    CdControl(CdlSetmode, &mode, NULL);

    for (int i = 0; i < MAX_CD_FILES; i++) {
        cdFileInUse[i] = 0;
        cdFilePos[i] = 0;
    }
    return 0;
}
```

### 3. Opening Files

**Critical**: CD-ROM filenames must be uppercase and use DOS-style paths:

```c
int cdromOpen(const char *filename) {
    char upperName[256];
    // Convert to uppercase
    for (int i = 0; filename[i]; i++) {
        if (filename[i] >= 'a' && filename[i] <= 'z') {
            upperName[i] = filename[i] - 32;
        } else if (filename[i] == '/') {
            upperName[i] = '\\';  // Unix to DOS path
        } else {
            upperName[i] = filename[i];
        }
    }

    // Add leading backslash if missing
    char cdPath[256];
    if (upperName[0] != '\\') {
        snprintf(cdPath, sizeof(cdPath), "\\%s", upperName);
    }

    CdlFILE *file = &cdFiles[slot];
    if (!CdSearchFile(file, cdPath)) {
        return -1;  // File not found
    }

    cdFileInUse[slot] = 1;
    cdFilePos[slot] = 0;
    return slot;
}
```

### 4. Reading Data

Must handle:
- Byte offset within sectors
- Multi-sector reads
- EOF detection

```c
int cdromRead(int handle, void *buffer, uint32 size) {
    CdlFILE *file = &cdFiles[handle];
    uint32 currentPos = cdFilePos[handle];

    // Clamp to file size
    if (currentPos + size > file->size) {
        size = file->size - currentPos;
    }

    // Calculate sector positions
    uint32 startSector = currentPos / 2048;
    uint32 offsetInSector = currentPos % 2048;
    uint32 sectorsToRead = ((offsetInSector + size) + 2047) / 2048;

    // Convert file start position to absolute sector
    uint32 fileStartSector = CdPosToInt(&file->pos);
    uint32 absoluteSector = fileStartSector + startSector;

    // Convert back to CdlLOC for seek
    CdlLOC readLoc;
    CdIntToPos(absoluteSector, &readLoc);

    // Seek and read
    CdControl(CdlSetloc, (uint8*)&readLoc, NULL);
    while (CdSync(1, NULL) > 0);

    CdRead(sectorsToRead, (uint32*)sectorBuffer, CdlModeSpeed);
    while (CdSync(1, NULL) > 0);

    // Copy requested bytes (accounting for offset)
    memcpy(buffer, sectorBuffer + offsetInSector, size);

    cdFilePos[handle] += size;
    return size;
}
```

### 5. Seeking

Store position in **bytes**, not sectors:

```c
int cdromSeek(int handle, int offset, int whence) {
    uint32 newPos;
    switch (whence) {
        case 0:  // SEEK_SET
            newPos = offset;
            break;
        case 1:  // SEEK_CUR
            newPos = cdFilePos[handle] + offset;
            break;
        case 2:  // SEEK_END
            newPos = cdFiles[handle].size + offset;
            break;
    }

    // Clamp to file boundaries
    if (newPos > cdFiles[handle].size) {
        newPos = cdFiles[handle].size;
    }

    cdFilePos[handle] = newPos;
    return newPos;
}
```

### 6. Mapping to FILE* API

In your stubs file, map FILE* to CD-ROM handles:

```c
FILE *fopen(const char *pathname, const char *mode) {
    int handle = cdromOpen(pathname);
    if (handle < 0) return NULL;
    // Offset by 3 to avoid stdout(1)/stderr(2)
    return (FILE*)(size_t)(handle + 3);
}

int fclose(FILE *stream) {
    if (stream == stdout || stream == stderr) return 0;
    int handle = (int)(size_t)stream - 3;
    return cdromClose(handle);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (stream == stdout || stream == stderr) return 0;
    int handle = (int)(size_t)stream - 3;
    int bytesRead = cdromRead(handle, ptr, size * nmemb);
    return (bytesRead < 0) ? 0 : bytesRead / size;
}
```

## Common Pitfalls

### 1. Position Tracking

❌ **Wrong**: Storing position in sectors

```c
file->pos += sectorsToRead;  // CdlLOC is not an integer!
```

✅ **Correct**: Separate byte position tracking

```c
cdFilePos[handle] += bytesRead;
```

### 2. Filename Case

❌ **Wrong**: Lowercase or mixed case

```c
cdromOpen("resource.map");  // Will fail
```

✅ **Correct**: Uppercase conversion

```c
cdromOpen("RESOURCE.MAP");  // Auto-converted to uppercase
```

### 3. Path Separators

❌ **Wrong**: Unix-style paths

```c
cdromOpen("/DATA/RESOURCE.MAP");
```

✅ **Correct**: DOS-style paths

```c
cdromOpen("\\DATA\\RESOURCE.MAP");
```

### 4. Sector Alignment

Must read full sectors (2048 bytes), then extract the needed bytes:

```c
// Read multiple sectors into buffer
CdRead(sectorsToRead, (uint32*)sectorBuffer, CdlModeSpeed);
// Copy only the requested bytes (with offset)
memcpy(buffer, sectorBuffer + offsetInSector, size);
```

## Memory Considerations

- Sector buffer: 32KB recommended (allows 16 sectors per read)
- File handles: 8 slots sufficient for most applications
- Position tracking: 4 bytes per file handle
- Total overhead: ~32KB + (8 * 28 bytes) = ~32.2KB

## Testing Checklist

- [ ] Files open successfully with various paths
- [ ] Reading from file start works
- [ ] Seeking to arbitrary positions works
- [ ] Reading across sector boundaries works
- [ ] EOF handling (reading past end returns 0)
- [ ] Multiple files can be open simultaneously
- [ ] Uppercase conversion handles all cases

## Integration Checklist

1. Create cdrom_ps1.c with core functions
2. Create cdrom_ps1.h with API declarations
3. Add cdrom_ps1.c to CMakeLists.txt
4. Update ps1_stubs.c to map FILE* to CD-ROM
5. Add cdromInit() call in main() (PS1_BUILD guard)
6. Include cdrom_ps1.h in platform headers
7. Test compilation and linking
8. Verify executable size increase is reasonable (~50-60KB)

## References

- PSn00bSDK CD-ROM API: `/opt/psn00bsdk/PSn00bSDK-0.24-Linux/include/libpsn00b/psxcd.h`
- BCD conversion: `CdPosToInt()` and `CdIntToPos()`
- File search: `CdSearchFile()`
- Sector reading: `CdRead()`
