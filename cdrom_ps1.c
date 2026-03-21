/*
 *  This file is part of 'Johnny Reborn' - PS1 Port
 *
 *  CD-ROM file access layer for PlayStation 1 using PSn00bSDK
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <psxcd.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "mytypes.h"
#include "cdrom_ps1.h"
#include "utils.h"
#include "ps1_debug.h"
#include "resource.h"

/* PS1 CD-ROM sector size */
#define CD_SECTOR_SIZE 2048
#define PS1_PACK_MAGIC 0x4B415053u
#define PS1_PACK_VERSION 1u
#define PS1_PACK_NAME_BYTES 16
#define PS1_PACK_PREFETCH_MAX 1
#define PS1_PACK_HEADER_SIZE 20
#define PS1_PACK_ENTRY_SIZE 28
#define PS1_PACK_FILE_MAX 32

/*
 * Build 28: Test different file path formats with debug output
 * Use ps1DebugPrint since we know it works now
 */
/* Simple test function to check if function calls work */
int simpleTestFunction(void)
{
    /* Simple color test */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    setRGB0(&draw, 255, 0, 128);  /* PINK = Simple function works! */
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    for (int i = 0; i < 120; i++) {
        VSync(0);
    }
    return 99;
}

/* Visual checkpoint helper - disabled for production */
static void checkpoint(int r, int g, int b) {
    (void)r; (void)g; (void)b;  /* Suppress unused warnings */
    /* Debug checkpoints disabled - enable if needed for debugging */
}

/* Pure CD-ROM test - NO GRAPHICS to avoid corruption */
int cdromTestPure(void)
{
    /* Test file search without any graphics calls */
    CdlFILE file;

    /* Wait for CD to be ready - PSX CD needs time to initialize */
    for (int i = 0; i < 1000000; i++) {
        /* Busy wait */
    }

    /* Test 1: SYSTEM.CNF (system file) - should definitely exist */
    if (CdSearchFile(&file, "SYSTEM.CNF") != NULL) {
        return 51;  /* Found system file */
    }

    /* Test 2: JCREBORN.EXE (our own executable) */
    if (CdSearchFile(&file, "JCREBORN.EXE") != NULL) {
        return 50;  /* Found our own exe file */
    }

    /* Test 3: RESOURCE.MAP (original) */
    if (CdSearchFile(&file, "RESOURCE.MAP") != NULL) {
        return 47;  /* File found with uppercase */
    }

    return 42;  /* No files found at all */
}

/* Advanced function to scan RESOURCE.MAP for meaningful data */
int cdromTestResourceMap(void)
{
    CdlFILE file;

    /* Wait for CD to be ready */
    for (int i = 0; i < 1000000; i++) {
        /* Busy wait */
    }

    /* Find RESOURCE.MAP file */
    if (CdSearchFile(&file, "RESOURCE.MAP") == NULL) {
        return 42;  /* File not found */
    }

    /* File found - try reading multiple sectors to find meaningful data */
    /* RESOURCE.MAP might have padding at the start */

    uint32_t buffer[CD_SECTOR_SIZE / 4];  /* 2KB sector buffer */

    /* Try reading first 3 sectors of the file */
    for (int sector = 0; sector < 3; sector++) {
        /* Seek to file position + sector offset */
        if (CdControl(CdlSeekL, (uint8_t*)&file.pos, NULL) == 0) {
            return 43;  /* Seek failed */
        }

        /* Wait for seek to complete */
        for (int i = 0; i < 500000; i++) {
            /* Busy wait */
        }

        /* Clear buffer with test pattern */
        for (int i = 0; i < CD_SECTOR_SIZE / 4; i++) {
            buffer[i] = 0xDEADBEEF;
        }

        if (CdRead(1, buffer, CdlModeSpeed) == 0) {
            return 44;  /* Read failed */
        }

        /* Wait for read to complete */
        uint8_t result[8];
        if (CdReadSync(0, result) == CdlComplete) {
            uint8_t *data = (uint8_t*)buffer;

            if (buffer[0] != 0xDEADBEEF) {
                /* Check for RESOURCE.MAP header patterns */
                /* Look for meaningful data throughout the sector */
                int meaningfulBytes = 0;
                int maxByte = 0;

                for (int i = 0; i < 512; i++) {  /* Check first 512 bytes */
                    if (data[i] != 0) {
                        meaningfulBytes++;
                        if (data[i] > maxByte) {
                            maxByte = data[i];
                        }
                    }
                }

                /* If we found significant non-zero data */
                if (meaningfulBytes >= 20 && maxByte > 1) {
                    /* Check if this looks like RESOURCE.MAP header */
                    /* RESOURCE.MAP usually starts with small integers (header) */
                    if (data[0] < 10 && data[1] < 10 && meaningfulBytes > 50) {
                        return 48;  /* Found meaningful RESOURCE.MAP data! */
                    } else {
                        return 47;  /* Found data but not RESOURCE.MAP header */
                    }
                } else if (meaningfulBytes >= 5) {
                    return 49;  /* Some data but sparse */
                }
            }

            /* Try next sector */
        } else {
            return 45;  /* Read sync failed */
        }
    }

    return 46;  /* All sectors were empty or failed */
}

int cdromFirstFunction(void)
{
    checkpoint(255, 0, 255);    /* MAGENTA = Function entry - FIRST THING */

    /* Test file search + opening */
    CdlFILE file;

    checkpoint(0, 255, 255);    /* CYAN = After CdlFILE declaration */

    /* Wait for CD to be ready - PSX CD needs time to initialize */
    /* Simple delay loop - wait ~1 second */
    for (int i = 0; i < 1000000; i++) {
        /* Busy wait */
    }

    checkpoint(0, 255, 255);    /* CYAN = After wait loop */

    /* Test 1: SYSTEM.CNF (system file) - should definitely exist */
    checkpoint(255, 255, 0);    /* YELLOW = About to test SYSTEM.CNF */

    if (CdSearchFile(&file, "SYSTEM.CNF") != NULL) {
        checkpoint(255, 128, 0);  /* ORANGE = SYSTEM.CNF found! */
        return 51;  /* Found system file */
    }

    checkpoint(255, 0, 0);      /* RED = SYSTEM.CNF not found */

    /* Test 2: JCREBORN.EXE (our own executable) */
    if (CdSearchFile(&file, "JCREBORN.EXE") != NULL) {
        checkpoint(0, 255, 0);    /* GREEN = JCREBORN.EXE found! */
        return 50;  /* Found our own exe file */
    }

    /* Test 3: RESOURCE.MAP (original) */
    if (CdSearchFile(&file, "RESOURCE.MAP") != NULL) {
        checkpoint(0, 0, 255);    /* BLUE = RESOURCE.MAP found! */
        return 47;  /* File found with uppercase */
    }

    checkpoint(128, 128, 128);  /* GRAY = No files found at all */
    return 42;  /* No files found at all */
}

/* Visual debug helper for CD-ROM errors - disabled for production */
static void showCDError(int r, int g, int b) {
    (void)r; (void)g; (void)b;  /* Suppress unused warnings */
    /* Error screens disabled - errors will be logged via ps1Debug instead */
}

/*
 * Build 20: Test function placed BEFORE static data
 * Tests if position in file affects callability
 */
int cdromTestCall2(void)
{
    /* Show GREEN screen = we entered this function! */
    showCDError(0, 255, 0);  /* BRIGHT GREEN */
    return 99;  /* Different return value to distinguish from cdromTestCall */
}

/* CD file handle structure */
#define MAX_CD_FILES 8
static CdlFILE cdFiles[MAX_CD_FILES];
static uint8 cdFileInUse[MAX_CD_FILES];
static uint32 cdFilePos[MAX_CD_FILES];  /* Current position in bytes */
static uint8 *cdReadBuffer = NULL;
static uint32 cdReadBufferPos = 0;
static uint32 cdReadBufferSize = 0;

struct TPs1PackedResourceEntry {
    uint8 resourceTypeCode;
    char name[PS1_PACK_NAME_BYTES];
    uint32 offsetBytes;
    uint32 sizeBytes;
};

struct TPs1ActivePack {
    char adsName[PS1_PACK_NAME_BYTES];
    char packFile[PS1_PACK_FILE_MAX];
    char prefetchedAdsName[PS1_PACK_NAME_BYTES];
    CdlFILE cdfile;
    uint16 packId;
    uint16 entryCount;
    uint8 fileInfoValid;
    struct TPs1PackedResourceEntry *entries;
    uint32 cachedStartSector;  /* CD sector of packFile start (0 = not cached) */
    uint32 cachedFileSize;     /* Pack file size in bytes */
};

static struct TPs1ActivePack ps1PilotActivePack = {"", "", "", {0}, 0, 0, 0, NULL, 0, 0};
static struct TPs1ActivePack ps1PilotPrefetchPack = {"", "", "", {0}, 0, 0, 0, NULL, 0, 0};
uint16 ps1PilotDbgActivePack = 0;
uint16 ps1PilotDbgHits = 0;
uint16 ps1PilotDbgFallbacks = 0;
uint16 ps1PilotDbgLastHitEntry = 0;
uint16 ps1PilotDbgLastFallbackEntry = 0;
uint16 ps1PilotDbgFallbackWhilePackActive = 0;

/* CD-ROM read buffer (1KB static buffer - minimal size for testing) */
/* Must be 4-byte aligned for DMA operations! */
#define CD_BUFFER_SIZE (1 * 1024)
static uint32 cdSectorBuffer[CD_BUFFER_SIZE / sizeof(uint32)];  /* Static buffer - no malloc needed */

/*
 * Initialize CD-ROM subsystem
 */
int cdromInit()
{
    /* Initialize PSn00bSDK CD-ROM library */
    /* Note: This is needed even when booting from CD to use CdSearchFile() */
    CdInit();

    /* Initialize our internal state */
    for (int i = 0; i < MAX_CD_FILES; i++) {
        cdFileInUse[i] = 0;
        cdFilePos[i] = 0;
    }

    cdReadBuffer = NULL;
    cdReadBufferPos = 0;
    cdReadBufferSize = 0;

    /* ps1FilePool initialization is done in cdromResetState() */
    /* which must be called after ps1FilePool is defined */

    /* Build 31: Use minimal 1KB static buffer to avoid BSS issues */
    /* cdSectorBuffer is now a static array, no allocation needed */

    return 0;
}

/*
 * Find a free file handle slot
 */
static int cdromFindFreeSlot()
{
    for (int i = 0; i < MAX_CD_FILES; i++) {
        if (!cdFileInUse[i]) {
            return i;
        }
    }
    return -1;
}

/*
 * Build 19: Minimal test function to see if we can call ANY function in cdrom_ps1.c
 */
int cdromTestCall(void)
{
    /* Show GREEN screen = we entered this function! */
    showCDError(0, 255, 0);  /* BRIGHT GREEN */
    return 42;
}

/*
 * Open a file from CD-ROM
 * Returns file handle ID (0-7) or -1 on error
 */
int cdromOpen2(const char *filename)
{
    ps1DebugPrint("cdromOpen2: Opening file %s", filename);

    /* Find a free file slot */
    int slot = cdromFindFreeSlot();
    if (slot < 0) {
        ps1DebugPrint("cdromOpen2: No free file slots");
        return -1;
    }

    /* Wait for CD to be ready */
    for (int i = 0; i < 1000000; i++) {
        /* Busy wait */
    }

    /* Search for the file on CD-ROM */
    CdlFILE *file = &cdFiles[slot];
    if (CdSearchFile(file, filename) == NULL) {
        ps1DebugPrint("cdromOpen2: File not found: %s", filename);
        return -1;
    }

    /* Mark slot as in use */
    cdFileInUse[slot] = 1;
    cdFilePos[slot] = 0;  /* Start at beginning of file */

    ps1DebugPrint("cdromOpen2: File opened successfully");
    ps1DebugPrint("cdromOpen2: Slot %d, Size %d bytes", slot, file->size);

    return slot;
}

/*
 * Read data from CD file
 * Returns number of bytes read, or -1 on error
 */
int cdromRead(int fileHandle, void *buffer, uint32 size)
{
    if (fileHandle < 0 || fileHandle >= MAX_CD_FILES || !cdFileInUse[fileHandle]) {
        return -1;
    }

    CdlFILE *file = &cdFiles[fileHandle];
    uint32 currentPos = cdFilePos[fileHandle];

    /* Don't read past end of file */
    if (currentPos >= file->size) {
        return 0;  /* EOF */
    }
    if (currentPos + size > file->size) {
        size = file->size - currentPos;
    }

    /* Calculate sector offset from file start */
    uint32 sectorSize = 2048;  /* CD-ROM Mode 1 sector size */
    uint32 startSector = currentPos / sectorSize;
    uint32 offsetInSector = currentPos % sectorSize;
    uint32 sectorsToRead = ((offsetInSector + size) + sectorSize - 1) / sectorSize;

    /* Calculate absolute sector position by converting file->pos to sector number */
    CdlLOC fileLoc = file->pos;
    /* Convert BCD to binary for calculation */
    uint32 fileStartSector = CdPosToInt(&fileLoc);
    uint32 absoluteSector = fileStartSector + startSector;

    /* Convert sector back to CdlLOC */
    CdlLOC readLoc;
    CdIntToPos(absoluteSector, &readLoc);

    /* Seek to position (seek actually happens during CdRead) */
    if (CdControl(CdlSetloc, (uint8*)&readLoc, NULL) == 0) {
        ps1DebugInit();
        ps1DebugClear();
        ps1DebugPrint("CD-ROM Seek Failed");
        ps1DebugPrint("");
        ps1DebugPrint("CdControl(CdlSetloc) returned 0");
        ps1DebugPrint("Absolute sector: %d", absoluteSector);
        ps1DebugFlush();
        ps1DebugWait();
        showCDError(255, 64, 0);  /* ORANGE-RED = Seek failed */
        return -1;
    }

    /* Read data - seek happens here */
    if (CdRead(sectorsToRead, cdSectorBuffer, CdlModeSpeed) == 0) {
        ps1DebugInit();
        ps1DebugClear();
        ps1DebugPrint("CD-ROM Read Failed");
        ps1DebugPrint("");
        ps1DebugPrint("CdRead() returned 0");
        ps1DebugPrint("Sectors: %d", sectorsToRead);
        ps1DebugPrint("Absolute sector: %d", absoluteSector);
        ps1DebugFlush();
        ps1DebugWait();
        showCDError(255, 128, 0);  /* ORANGE = CdRead failed */
    }

    /* Wait for read to complete using CdReadSync with polling */
    /* CdReadSync returns 0 when complete, >0 when busy */
    int timeout = 10000000;  /* Very large timeout */
    int result;
    int initial_timeout = timeout;
    while ((result = CdReadSync(1, NULL)) > 0 && timeout-- > 0) {
        /* Polling - result > 0 means still busy */
        if (timeout % 1000000 == 0) {
            /* Show progress every million iterations */
            ps1DebugInit();
            ps1DebugClear();
            ps1DebugPrint("CD-ROM Reading...");
            ps1DebugPrint("");
            ps1DebugPrint("Iterations: %d", initial_timeout - timeout);
            ps1DebugPrint("Result: %d", result);
            ps1DebugFlush();
        }
    }

    if (timeout <= 0) {
        ps1DebugInit();
        ps1DebugClear();
        ps1DebugPrint("CD-ROM Read Timeout");
        ps1DebugPrint("");
        ps1DebugPrint("Timeout after %d iterations", initial_timeout);
        ps1DebugPrint("Last result: %d", result);
        ps1DebugPrint("Sectors: %d", sectorsToRead);
        ps1DebugFlush();
        ps1DebugWait();
        showCDError(0, 255, 255);  /* CYAN = Read timeout */
    }

    if (result < 0) {
        ps1DebugInit();
        ps1DebugClear();
        ps1DebugPrint("CD-ROM Read Error");
        ps1DebugPrint("");
        ps1DebugPrint("CdReadSync returned: %d", result);
        ps1DebugFlush();
        ps1DebugWait();
        showCDError(255, 255, 255);  /* WHITE = Read error */
    }

    /* Copy requested bytes from buffer (accounting for offset within first sector) */
    /* Cast to uint8* for byte-level access */
    memcpy(buffer, (uint8*)cdSectorBuffer + offsetInSector, size);

    /* Update file position */
    cdFilePos[fileHandle] += size;

    return size;
}

/*
 * Seek to position in CD file
 * whence: SEEK_SET=0, SEEK_CUR=1, SEEK_END=2
 * Returns new position, or -1 on error
 */
int cdromSeek(int fileHandle, int offset, int whence)
{
    if (fileHandle < 0 || fileHandle >= MAX_CD_FILES || !cdFileInUse[fileHandle]) {
        return -1;
    }

    CdlFILE *file = &cdFiles[fileHandle];
    uint32 newPos = 0;

    switch (whence) {
        case 0:  /* SEEK_SET */
            newPos = offset;
            break;
        case 1:  /* SEEK_CUR */
            newPos = cdFilePos[fileHandle] + offset;
            break;
        case 2:  /* SEEK_END */
            newPos = file->size + offset;
            break;
        default:
            return -1;
    }

    /* Clamp to file boundaries */
    if (newPos > file->size) {
        newPos = file->size;
    }

    /* Update byte position */
    cdFilePos[fileHandle] = newPos;

    return newPos;
}

/*
 * Get current position in CD file
 */
int cdromTell(int fileHandle)
{
    if (fileHandle < 0 || fileHandle >= MAX_CD_FILES || !cdFileInUse[fileHandle]) {
        return -1;
    }

    return cdFilePos[fileHandle];
}

/*
 * Close CD file
 */
int cdromClose(int fileHandle)
{
    if (fileHandle < 0 || fileHandle >= MAX_CD_FILES || !cdFileInUse[fileHandle]) {
        return -1;
    }

    cdFileInUse[fileHandle] = 0;

    return 0;
}

/*
 * Get file size
 */
uint32 cdromGetSize(int fileHandle)
{
    if (fileHandle < 0 || fileHandle >= MAX_CD_FILES || !cdFileInUse[fileHandle]) {
        return 0;
    }

    return cdFiles[fileHandle].size;
}

/* ============================================================================
 * PS1 File I/O Wrapper Implementation
 * Provides FILE*-like interface using CD-ROM access for resource loading
 * ============================================================================ */

static PS1File ps1FilePool[4];  /* Support up to 4 open files */

/* Reset CD state after external CD operations (like title screen loading) */
void cdromResetState(void)
{
    /* Wait for any pending CD operations */
    CdReadSync(0, NULL);

    /* Re-initialize the CD subsystem to fully reset state */
    CdInit();

    /* Ensure ps1FilePool is clean and initialized */
    for (int i = 0; i < 4; i++) {
        if (ps1FilePool[i].buffer && !ps1FilePool[i].isOpen) {
            free(ps1FilePool[i].buffer);
            ps1FilePool[i].buffer = NULL;
        }
        ps1FilePool[i].isOpen = 0;
        ps1FilePool[i].bufferSize = 0;
        ps1FilePool[i].currentPos = 0;
    }
}

PS1File* ps1_fopen(const char* filename, const char* mode)
{
    /* Find free file slot */
    PS1File* file = NULL;
    for (int i = 0; i < 4; i++) {
        if (!ps1FilePool[i].isOpen) {
            file = &ps1FilePool[i];
            break;
        }
    }

    if (!file) {
        return NULL;  /* No free slots */
    }

    /* Brief wait for CD to be ready */
    for (volatile int i = 0; i < 1000000; i++);

    /* Build PS1 CD-ROM path: \\FILENAME.EXT;1 */
    char cdPath[64];
    cdPath[0] = '\\';
    strncpy(cdPath + 1, filename, sizeof(cdPath) - 4);
    cdPath[sizeof(cdPath) - 3] = '\0';
    strcat(cdPath, ";1");

    /* Search for file on CD */
    CdlFILE *result = CdSearchFile(&file->cdfile, cdPath);

    if (result == NULL) {
        /* Try without ;1 suffix */
        char altPath[64];
        altPath[0] = '\\';
        strncpy(altPath + 1, filename, sizeof(altPath) - 2);
        altPath[sizeof(altPath) - 1] = '\0';
        result = CdSearchFile(&file->cdfile, altPath);
    }

    if (result == NULL) {
        return NULL;  /* File not found */
    }

    /* Initialize file structure */
    file->currentPos = 0;
    file->isOpen = 1;
    strncpy(file->filename, filename, sizeof(file->filename) - 1);
    file->filename[sizeof(file->filename) - 1] = '\0';

    /* Allocate buffer for entire file */
    file->bufferSize = file->cdfile.size;
    file->buffer = (uint8_t*)malloc(file->bufferSize);

    if (!file->buffer) {
        return NULL;  /* Malloc failed */
    }

    /* Calculate sectors needed */
    int numSectors = (file->bufferSize + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;

    /* Position CD head at file location */
    CdControl(CdlSetloc, (uint8_t*)&file->cdfile.pos, NULL);

    /* Brief wait for seek */
    for (volatile int i = 0; i < 500000; i++);

    /* Start CD read */
    CdRead(numSectors, (uint32_t*)file->buffer, CdlModeSpeed);

    /* Wait for read to complete (blocking) */
    int sync_result = CdReadSync(0, NULL);

    if (sync_result < 0) {
        free(file->buffer);
        file->buffer = NULL;
        return NULL;  /* Read error */
    }

    return file;
}

size_t ps1_fread(void* ptr, size_t size, size_t nmemb, PS1File* file)
{
    if (!file || !file->isOpen || !file->buffer) {
        return 0;
    }

    size_t totalBytes = size * nmemb;
    uint8_t* dest = (uint8_t*)ptr;

    /* Check if read goes past end of buffer */
    if (file->currentPos + totalBytes > file->bufferSize) {
        totalBytes = file->bufferSize - file->currentPos;
        if (totalBytes == 0) return 0;
    }

    /* Simple memory copy from preloaded buffer - no CD-ROM operations! */
    for (size_t i = 0; i < totalBytes; i++) {
        dest[i] = file->buffer[file->currentPos + i];
    }

    file->currentPos += totalBytes;
    return totalBytes / size;
}

int ps1_fseek(PS1File* file, long offset, int whence)
{
    if (!file || !file->isOpen) {
        return -1;
    }

    long newPos;
    switch (whence) {
        case 0:  /* SEEK_SET */
            newPos = offset;
            break;
        case 1:  /* SEEK_CUR */
            newPos = file->currentPos + offset;
            break;
        case 2:  /* SEEK_END */
            newPos = file->cdfile.size + offset;
            break;
        default:
            return -1;
    }

    if (newPos < 0 || newPos > file->cdfile.size) {
        return -1;
    }

    file->currentPos = newPos;
    return 0;
}

long ps1_ftell(PS1File* file)
{
    if (!file || !file->isOpen) {
        return -1;
    }
    return file->currentPos;
}

int ps1_fclose(PS1File* file)
{
    if (!file || !file->isOpen) {
        return -1;
    }

    /* Free preloaded buffer if it exists */
    if (file->buffer) {
        free(file->buffer);
        file->buffer = NULL;
    }

    file->isOpen = 0;
    return 0;
}

/*
 * Wrap a buffer as a PS1File for use with existing decompress functions.
 * The PS1File is stack-allocated by caller; this just initializes it.
 */
void ps1_wrapBuffer(PS1File* file, uint8_t* buffer, uint32_t size)
{
    file->isOpen = 1;
    file->currentPos = 0;
    file->buffer = buffer;
    file->bufferSize = size;
    file->filename[0] = '\0';  /* No filename for wrapped buffers */
}

static uint8_t* ps1_streamReadFromCdFile(const CdlFILE *cdfile, uint32_t offset, uint32_t size);

/*
 * Stream read: Read a range of bytes from a file without loading entire file.
 * This is for dynamic loading - reads only the necessary CD sectors.
 * Returns malloc'd buffer that caller must free, or NULL on error.
 */
uint8_t* ps1_streamRead(const char* filename, uint32_t offset, uint32_t size)
{
    CdlFILE cdfile;
    char cdPath[64];

    if (size == 0) return NULL;

    cdPath[0] = '\\';
    strncpy(cdPath + 1, filename, sizeof(cdPath) - 4);
    cdPath[sizeof(cdPath) - 3] = '\0';
    strcat(cdPath, ";1");

    if (CdSearchFile(&cdfile, cdPath) == NULL) {
        return NULL;  /* File not found */
    }

    /* Calculate sector range needed
     * CD sectors are 2048 bytes each */
    return ps1_streamReadFromCdFile(&cdfile, offset, size);
}

static uint8_t* ps1_streamReadFromCdFile(const CdlFILE *cdfile, uint32_t offset, uint32_t size)
{
    uint32_t startSector;
    uint32_t endByte;
    uint32_t endSector;
    uint32_t numSectors;
    uint32_t bufferSize;
    uint8_t* sectorBuffer;
    CdlLOC loc;
    uint8_t* result;
    uint32_t offsetInBuffer;
    int syncResult;
    int timeout;
    uint32_t sectorsRead;

    enum { PS1_CD_READ_CHUNK_SECTORS = 8 };

    if (cdfile == NULL || size == 0)
        return NULL;

    /* Calculate sector range needed
     * CD sectors are 2048 bytes each */
    startSector = offset / CD_SECTOR_SIZE;
    endByte = offset + size;
    endSector = (endByte + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;
    numSectors = endSector - startSector;
    bufferSize = numSectors * CD_SECTOR_SIZE;

    /* Allocate buffer for the sectors we need */
    sectorBuffer = (uint8_t*)malloc(bufferSize);
    if (!sectorBuffer) {
        return NULL;  /* Malloc failed */
    }

    /* Read in smaller sector chunks. Large single-shot reads on packed BMPs
     * have been intermittently fragile on hardware-style CD timing. */
    sectorsRead = 0;
    while (sectorsRead < numSectors) {
        uint32_t chunkSectors = numSectors - sectorsRead;
        uint8_t *chunkDst = sectorBuffer + (sectorsRead * CD_SECTOR_SIZE);

        if (chunkSectors > PS1_CD_READ_CHUNK_SECTORS)
            chunkSectors = PS1_CD_READ_CHUNK_SECTORS;

        CdIntToPos(CdPosToInt((CdlLOC *)&cdfile->pos) + startSector + sectorsRead, &loc);

        if (CdControl(CdlSetloc, (uint8_t*)&loc, NULL) == 0) {
            free(sectorBuffer);
            return NULL;
        }

        for (volatile int i = 0; i < 100000; i++);

        if (CdRead(chunkSectors, (uint32_t*)chunkDst, CdlModeSpeed) == 0) {
            free(sectorBuffer);
            return NULL;
        }

        timeout = 1000000;
        do {
            syncResult = CdReadSync(1, NULL);
        } while (syncResult > 0 && --timeout > 0);

        if (timeout <= 0 || syncResult < 0) {
            free(sectorBuffer);
            return NULL;  /* Read error */
        }

        sectorsRead += chunkSectors;
    }

    /* Allocate exact-size output buffer and copy the data we need */
    result = (uint8_t*)malloc(size);
    if (!result) {
        free(sectorBuffer);
        return NULL;
    }

    /* Copy from sector buffer at the correct offset */
    offsetInBuffer = offset % CD_SECTOR_SIZE;
    memcpy(result, sectorBuffer + offsetInBuffer, size);

    free(sectorBuffer);
    return result;
}

static uint8_t* ps1_streamReadFromCdFileWhole(const CdlFILE *cdfile, uint32_t offset, uint32_t size)
{
    uint32_t fileSize;
    uint32_t totalSectors;
    uint32_t bufferSize;
    uint8_t *fileBuffer;
    uint8_t *result;
    CdlLOC loc;
    uint32_t sectorsRead;
    int syncResult;
    int timeout;

    enum { PS1_CD_READ_CHUNK_SECTORS = 8 };

    if (cdfile == NULL || size == 0)
        return NULL;

    fileSize = cdfile->size;
    if (offset > fileSize || size > fileSize || (offset + size) > fileSize)
        return NULL;

    totalSectors = (fileSize + CD_SECTOR_SIZE - 1U) / CD_SECTOR_SIZE;
    bufferSize = totalSectors * CD_SECTOR_SIZE;
    fileBuffer = (uint8_t*)malloc(bufferSize);
    if (fileBuffer == NULL)
        return NULL;

    sectorsRead = 0;
    while (sectorsRead < totalSectors) {
        uint32_t chunkSectors = totalSectors - sectorsRead;
        uint8_t *chunkDst = fileBuffer + (sectorsRead * CD_SECTOR_SIZE);

        if (chunkSectors > PS1_CD_READ_CHUNK_SECTORS)
            chunkSectors = PS1_CD_READ_CHUNK_SECTORS;

        CdIntToPos(CdPosToInt((CdlLOC *)&cdfile->pos) + sectorsRead, &loc);
        if (CdControl(CdlSetloc, (uint8_t*)&loc, NULL) == 0) {
            free(fileBuffer);
            return NULL;
        }

        for (volatile int i = 0; i < 100000; i++);

        if (CdRead(chunkSectors, (uint32_t*)chunkDst, CdlModeSpeed) == 0) {
            free(fileBuffer);
            return NULL;
        }

        timeout = 1000000;
        do {
            syncResult = CdReadSync(1, NULL);
        } while (syncResult > 0 && --timeout > 0);

        if (timeout <= 0 || syncResult < 0) {
            free(fileBuffer);
            return NULL;
        }

        sectorsRead += chunkSectors;
    }

    result = (uint8_t*)malloc(size);
    if (result == NULL) {
        free(fileBuffer);
        return NULL;
    }

    memcpy(result, fileBuffer + offset, size);
    free(fileBuffer);
    return result;
}

static uint32 ps1ReadLe32(const uint8_t *ptr)
{
    return ((uint32)ptr[0])
        | ((uint32)ptr[1] << 8)
        | ((uint32)ptr[2] << 16)
        | ((uint32)ptr[3] << 24);
}

static uint8 ps1PilotResourceTypeCode(const char *resourceType)
{
    if (resourceType == NULL) return 0;
    if (strcmp(resourceType, "ads") == 0) return 1;
    if (strcmp(resourceType, "scr") == 0) return 2;
    if (strcmp(resourceType, "ttm") == 0) return 3;
    if (strcmp(resourceType, "bmp") == 0) return 4;
    if (strcmp(resourceType, "psb") == 0) return 5;
    return 0;
}

static uint16 ps1PilotPackDebugId(const char *adsName)
{
    uint32 hash = 0;
    int i;

    if (adsName == NULL || adsName[0] == '\0')
        return 0;

    for (i = 0; adsName[i] != '\0'; i++)
        hash = (hash * 33U) + (uint8)adsName[i];

    return (uint16)((hash % 63U) + 1U);
}

static uint16 ps1PilotResourceDebugSig(const char *resourceType, const char *name)
{
    uint32 hash = 0;
    int i;

    if (resourceType != NULL) {
        for (i = 0; resourceType[i] != '\0'; i++)
            hash = (hash * 33U) + (uint8)resourceType[i];
    }

    hash = (hash * 33U) + 0x2FU;

    if (name != NULL) {
        for (i = 0; name[i] != '\0'; i++)
            hash = (hash * 33U) + (uint8)name[i];
    }

    return (uint16)((hash % 63U) + 1U);
}

static void ps1PilotResetPack(struct TPs1ActivePack *pack)
{
    if (pack == NULL)
        return;

    if (pack->entries != NULL) {
        free(pack->entries);
        pack->entries = NULL;
    }

    pack->adsName[0] = '\0';
    pack->packFile[0] = '\0';
    pack->prefetchedAdsName[0] = '\0';
    memset(&pack->cdfile, 0, sizeof(pack->cdfile));
    pack->packId = 0;
    pack->entryCount = 0;
    pack->fileInfoValid = 0;
    pack->cachedStartSector = 0;
    pack->cachedFileSize = 0;
}

static void ps1PilotResetActivePack(void)
{
    ps1PilotResetPack(&ps1PilotActivePack);
    ps1PilotDbgActivePack = 0;
    ps1PilotDbgHits = 0;
    ps1PilotDbgFallbacks = 0;
    ps1PilotDbgLastHitEntry = 0;
    ps1PilotDbgLastFallbackEntry = 0;
    ps1PilotDbgFallbackWhilePackActive = 0;
}

static int ps1PilotBuildPackFile(const char *adsName, char *outPath, size_t outPathSize)
{
    char stem[PS1_PACK_NAME_BYTES];
    int i;

    if (adsName == NULL || outPath == NULL || outPathSize == 0)
        return 0;

    for (i = 0; i < (PS1_PACK_NAME_BYTES - 1) && adsName[i] != '\0' && adsName[i] != '.'; i++)
        stem[i] = adsName[i];
    stem[i] = '\0';

    if (stem[0] == '\0')
        return 0;

    snprintf(outPath, outPathSize, "PACKS\\%s.PAK", stem);
    return 1;
}

static int ps1PilotRefreshPackFileInfo(struct TPs1ActivePack *pack)
{
    char cdPath[64];
    CdlFILE cdfile;

    if (pack == NULL || pack->packFile[0] == '\0')
        return 0;

    cdPath[0] = '\\';
    strncpy(cdPath + 1, pack->packFile, sizeof(cdPath) - 4);
    cdPath[sizeof(cdPath) - 3] = '\0';
    strcat(cdPath, ";1");

    if (CdSearchFile(&cdfile, cdPath) == NULL) {
        pack->fileInfoValid = 0;
        return 0;
    }

    pack->cdfile = cdfile;
    pack->fileInfoValid = 1;
    return 1;
}

static int ps1PilotLoadPackIndex(const char *adsName, struct TPs1ActivePack *outPack)
{
    uint8_t *headerData;
    uint32 magic;
    uint32 version;
    uint32 entryCount;
    uint32 firstResourceOffset;
    uint32 prefetchCount;
    struct TPs1PackedResourceEntry *entries;
    char packFile[PS1_PACK_FILE_MAX];
    char cdPath[64];
    CdlFILE cdfile;
    uint32 entryBytes;
    uint32 i;

    if (adsName == NULL || adsName[0] == '\0' || outPack == NULL)
        return 0;

    if (!ps1PilotBuildPackFile(adsName, packFile, sizeof(packFile)))
        return 0;

    cdPath[0] = '\\';
    strncpy(cdPath + 1, packFile, sizeof(cdPath) - 4);
    cdPath[sizeof(cdPath) - 3] = '\0';
    strcat(cdPath, ";1");
    if (CdSearchFile(&cdfile, cdPath) == NULL)
        return 0;

    headerData = ps1_streamReadFromCdFile(&cdfile, 0, PS1_PACK_HEADER_SIZE);
    if (headerData == NULL)
        return 0;

    magic = ps1ReadLe32(headerData + 0);
    version = ps1ReadLe32(headerData + 4);
    entryCount = ps1ReadLe32(headerData + 8);
    firstResourceOffset = ps1ReadLe32(headerData + 12);
    prefetchCount = ps1ReadLe32(headerData + 16);
    free(headerData);

    if (magic != PS1_PACK_MAGIC || version != PS1_PACK_VERSION || entryCount == 0)
        return 0;

    entryBytes = entryCount * PS1_PACK_ENTRY_SIZE;
    if (firstResourceOffset < PS1_PACK_HEADER_SIZE + entryBytes + (PS1_PACK_PREFETCH_MAX * PS1_PACK_NAME_BYTES))
        return 0;

    headerData = ps1_streamReadFromCdFile(&cdfile, 0, firstResourceOffset);
    if (headerData == NULL)
        return 0;

    entries = (struct TPs1PackedResourceEntry *)malloc(entryCount * sizeof(*entries));
    if (entries == NULL) {
        free(headerData);
        return 0;
    }

    for (i = 0; i < entryCount; i++) {
        const uint8_t *src = headerData + PS1_PACK_HEADER_SIZE + (i * PS1_PACK_ENTRY_SIZE);
        entries[i].resourceTypeCode = src[0];
        memcpy(entries[i].name, src + 12, PS1_PACK_NAME_BYTES);
        entries[i].name[PS1_PACK_NAME_BYTES - 1] = '\0';
        entries[i].offsetBytes = ps1ReadLe32(src + 4);
        entries[i].sizeBytes = ps1ReadLe32(src + 8);
    }

    ps1PilotResetPack(outPack);
    strncpy(outPack->adsName, adsName, sizeof(outPack->adsName) - 1);
    outPack->adsName[sizeof(outPack->adsName) - 1] = '\0';
    strncpy(outPack->packFile, packFile, sizeof(outPack->packFile) - 1);
    outPack->packFile[sizeof(outPack->packFile) - 1] = '\0';
    outPack->cdfile = cdfile;
    outPack->packId = ps1PilotPackDebugId(adsName);
    outPack->entryCount = (uint16)entryCount;
    outPack->fileInfoValid = 1;
    outPack->entries = entries;
    outPack->prefetchedAdsName[0] = '\0';

    if (prefetchCount > 0) {
        const uint8_t *prefetchBase = headerData + PS1_PACK_HEADER_SIZE + entryBytes;
        memcpy(outPack->prefetchedAdsName, prefetchBase, PS1_PACK_NAME_BYTES);
        outPack->prefetchedAdsName[PS1_PACK_NAME_BYTES - 1] = '\0';
    }

    /* Cache the pack file's CD sector position to eliminate per-resource
     * CdSearchFile calls.  This single lookup amortises across all
     * subsequent resource loads from this pack. */
    {
        CdlFILE packCdFile;
        char cdPackPath[64];
        cdPackPath[0] = '\\';
        strncpy(cdPackPath + 1, packFile, sizeof(cdPackPath) - 4);
        cdPackPath[sizeof(cdPackPath) - 3] = '\0';
        strcat(cdPackPath, ";1");
        if (CdSearchFile(&packCdFile, cdPackPath) != NULL) {
            outPack->cachedStartSector = CdPosToInt(&packCdFile.pos);
            outPack->cachedFileSize = packCdFile.size;
        } else {
            outPack->cachedStartSector = 0;
            outPack->cachedFileSize = 0;
        }
    }

    free(headerData);
    return 1;
}

static void ps1PilotSetActivePackForAds(const char *adsName)
{
    struct TPs1ActivePack tempPack;

    memset(&tempPack, 0, sizeof(tempPack));

    if (adsName != NULL && ps1PilotPrefetchPack.entries != NULL
        && strcmp(ps1PilotPrefetchPack.adsName, adsName) == 0) {
        tempPack = ps1PilotActivePack;
        ps1PilotActivePack = ps1PilotPrefetchPack;
        ps1PilotPrefetchPack = tempPack;
    } else if (!ps1PilotLoadPackIndex(adsName, &ps1PilotActivePack)) {
        ps1PilotResetActivePack();
        ps1PilotResetPack(&ps1PilotPrefetchPack);
        return;
    }

    ps1PilotDbgActivePack = ps1PilotActivePack.packId;
    ps1PilotDbgHits = 0;
    ps1PilotDbgFallbacks = 0;
    ps1PilotDbgLastHitEntry = 0;
    ps1PilotDbgLastFallbackEntry = 0;
    ps1PilotDbgFallbackWhilePackActive = 0;

    if (ps1PilotActivePack.prefetchedAdsName[0] != '\0') {
        if (strcmp(ps1PilotPrefetchPack.adsName, ps1PilotActivePack.prefetchedAdsName) != 0)
            ps1PilotLoadPackIndex(ps1PilotActivePack.prefetchedAdsName, &ps1PilotPrefetchPack);
    } else {
        ps1PilotResetPack(&ps1PilotPrefetchPack);
    }
}

void ps1_pilotPrearmPackForAds(const char *adsName)
{
    if (adsName == NULL || adsName[0] == '\0')
        return;
    ps1PilotSetActivePackForAds(adsName);
}

static const struct TPs1PackedResourceEntry *ps1PilotFindEntry(const char *resourceType, const char *name)
{
    uint8 typeCode = ps1PilotResourceTypeCode(resourceType);
    int i;

    if (ps1PilotActivePack.entries == NULL || typeCode == 0 || name == NULL)
        return NULL;

    for (i = 0; i < ps1PilotActivePack.entryCount; i++) {
        const struct TPs1PackedResourceEntry *entry = &ps1PilotActivePack.entries[i];
        if (entry->resourceTypeCode == typeCode && strcmp(entry->name, name) == 0)
            return entry;
    }

    return NULL;
}

/*
 * Read bytes from the active pack using cached CD sector position.
 * Eliminates per-resource CdSearchFile calls for pack-based loading.
 * Returns malloc'd buffer (caller must free), or NULL on error.
 */
static uint8_t *ps1PilotLoadResource(const char *resourceType, const char *name, uint32 *inOutSize)
{
    const struct TPs1PackedResourceEntry *entry = ps1PilotFindEntry(resourceType, name);
    uint8_t *data;

    if (entry == NULL) {
        if (ps1PilotActivePack.entries != NULL)
            ps1PilotDbgLastFallbackEntry = ps1PilotResourceDebugSig(resourceType, name);
        return NULL;
    }

    if (!ps1PilotActivePack.fileInfoValid) {
        ps1PilotDbgLastFallbackEntry = ps1PilotResourceDebugSig(resourceType, name);
        return NULL;
    }

    data = ps1_streamReadFromCdFile(&ps1PilotActivePack.cdfile, entry->offsetBytes, entry->sizeBytes);
    if (data == NULL) {
        /* Recover once from transient CD state drift before treating the pack
         * entry as a true miss. The pack index already proved the asset exists. */
        cdromResetState();
        if (ps1PilotRefreshPackFileInfo(&ps1PilotActivePack))
            data = ps1_streamReadFromCdFile(&ps1PilotActivePack.cdfile, entry->offsetBytes, entry->sizeBytes);
        if (data == NULL) {
            /* Keep the pack path authoritative: if the direct range read still
             * fails, retry by reading the pack file body and slicing the exact
             * entry bytes out of that image. */
            cdromResetState();
            if (ps1PilotRefreshPackFileInfo(&ps1PilotActivePack)) {
                data = ps1_streamReadFromCdFileWhole(&ps1PilotActivePack.cdfile,
                                                     entry->offsetBytes,
                                                     entry->sizeBytes);
            }
        }
    }
    if (data != NULL) {
        if (inOutSize != NULL)
            *inOutSize = entry->sizeBytes;
        if (ps1PilotDbgHits < 0xFFFFU)
            ps1PilotDbgHits++;
        ps1PilotDbgLastHitEntry = (uint16)((entry - ps1PilotActivePack.entries) + 1);
    } else {
        /* When the entry exists but the sector read fails, keep the overlay
         * value as the pack entry index so screenshot-based validation can
         * distinguish read-path faults from "resource not in pack" faults. */
        ps1PilotDbgLastFallbackEntry = (uint16)((entry - ps1PilotActivePack.entries) + 1);
    }

    return data;
}

/*
 * Try to load a PSB resource from the active pack.
 * psbName is the PSB filename (e.g. "JOHNWALK.PSB").
 * Returns malloc'd buffer on success, NULL if not found in pack.
 * On success, *outSize is set to the resource size.
 */
uint8_t *ps1PilotLoadPsb(const char *psbName, uint32 *outSize)
{
    return ps1PilotLoadResource("psb", psbName, outSize);
}

/*
 * Load an entire file from CD into a malloc'd buffer.
 * Path should be a PS1 CD path like "\\SND\\SOUND00.VAG;1".
 * Returns malloc'd buffer (caller must free), or NULL on error.
 * Sets *outSize to the file size in bytes.
 */
uint8_t *ps1_loadRawFile(const char *path, uint32_t *outSize)
{
    CdlFILE fileInfo;
    if (CdSearchFile(&fileInfo, (char *)path) == NULL) {
        return NULL;
    }

    uint32_t fileSize = fileInfo.size;
    int sectors = (fileSize + 2047) / 2048;
    uint8_t *buf = (uint8_t *)malloc(sectors * 2048);
    if (!buf) return NULL;

    CdControl(CdlSetloc, (uint8_t *)&fileInfo.pos, NULL);
    for (volatile int i = 0; i < 100000; i++);

    CdRead(sectors, (uint32_t *)buf, CdlModeSpeed);
    if (CdReadSync(0, NULL) < 0) {
        free(buf);
        return NULL;
    }

    *outSize = fileSize;
    return buf;
}

/* ============================================================================
 * PS1 Resource Loading Test
 * Tests the complete PS1 resource loading system
 * ============================================================================ */

/* PS1-specific utility functions for reading from CD-ROM */
uint8 ps1_readUint8(PS1File *f) {
    uint8 value;
    if (ps1_fread(&value, 1, 1, f) != 1) {
        return 0;
    }
    return value;
}

uint16 ps1_readUint16(PS1File *f) {
    uint16 value;
    if (ps1_fread(&value, 2, 1, f) != 1) {
        return 0;
    }
    return value;
}

uint32 ps1_readUint32(PS1File *f) {
    uint32 value;
    if (ps1_fread(&value, 4, 1, f) != 1) {
        return 0;
    }
    return value;
}

char* ps1_getString(PS1File *f, int maxlen) {
    char *str = malloc(maxlen + 1);
    if (!str) return NULL;

    int i;
    for (i = 0; i < maxlen; i++) {
        str[i] = ps1_readUint8(f);
        if (str[i] == 0) break;
    }
    str[i] = 0;
    return str;
}

uint16* ps1_readUint16Block(PS1File *f, int count) {
    uint16 *block = malloc(count * sizeof(uint16));
    if (!block) return NULL;
    for (int i = 0; i < count; i++) {
        block[i] = ps1_readUint16(f);
    }
    return block;
}

uint8* ps1_readUint8Block(PS1File *f, int len) {
    uint8 *block = malloc(len);  /* Use malloc instead of safe_malloc */
    if (ps1_fread(block, 1, len, f) != (size_t)len) {
        free(block);
        return NULL;
    }
    return block;
}

/* ============================================================================
 * PS1 Resource Parsing Functions
 * Provides PS1 versions of resource parsing using PS1File* instead of FILE*
 * ============================================================================ */

struct TAdsResource* ps1_parseAdsResource(PS1File *f, const char *resName)
{
    struct TAdsResource *adsResource;
    uint8 *buffer;

    adsResource = malloc(sizeof(struct TAdsResource));
    if (!adsResource) return NULL;

    adsResource->resName = malloc(strlen(resName) + 1);
    strcpy(adsResource->resName, resName);

    /* Read "VER:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "VER:", 4)) {
        free(buffer);
        free(adsResource->resName);
        free(adsResource);
        return NULL;
    }
    free(buffer);

    adsResource->versionSize = ps1_readUint32(f);
    adsResource->versionString = ps1_readUint8Block(f, 5);

    /* Read "ADS:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "ADS:", 4)) {
        free(buffer);
        free(adsResource->versionString);
        free(adsResource->resName);
        free(adsResource);
        return NULL;
    }
    free(buffer);

    adsResource->adsUnknown1 = ps1_readUint8(f);
    adsResource->adsUnknown2 = ps1_readUint8(f);
    adsResource->adsUnknown3 = ps1_readUint8(f);
    adsResource->adsUnknown4 = ps1_readUint8(f);

    /* Read "RES:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "RES:", 4)) {
        free(buffer);
        free(adsResource->versionString);
        free(adsResource->resName);
        free(adsResource);
        return NULL;
    }
    free(buffer);

    adsResource->resSize = ps1_readUint32(f);
    adsResource->numRes = ps1_readUint16(f);

    adsResource->res = malloc(adsResource->numRes * sizeof(struct TAdsRes));
    for (int i = 0; i < adsResource->numRes; i++) {
        adsResource->res[i].id = ps1_readUint16(f);
        adsResource->res[i].name = ps1_getString(f, 40);
    }

    /* Read "SCR:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "SCR:", 4)) {
        free(buffer);
        /* Clean up already allocated resources */
        for (int i = 0; i < adsResource->numRes; i++) {
            free(adsResource->res[i].name);
        }
        free(adsResource->res);
        free(adsResource->versionString);
        free(adsResource->resName);
        free(adsResource);
        return NULL;
    }
    free(buffer);

    adsResource->compressedSize = ps1_readUint32(f) - 5;
    adsResource->compressionMethod = ps1_readUint8(f);
    adsResource->uncompressedSize = ps1_readUint32(f);

    /* Skip decompression — load from pre-extracted ADS/ files on CD instead.
     * This bypasses the buggy LZW decompressor (same approach used for TTM). */
    adsResource->uncompressedData = NULL;
    ps1_fseek(f, adsResource->compressedSize, SEEK_CUR);

    /* Read "TAG:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "TAG:", 4)) {
        free(buffer);
        for (int i = 0; i < adsResource->numRes; i++) {
            free(adsResource->res[i].name);
        }
        free(adsResource->res);
        free(adsResource->versionString);
        free(adsResource->resName);
        free(adsResource);
        return NULL;
    }
    free(buffer);

    adsResource->tagSize = ps1_readUint32(f);
    adsResource->numTags = ps1_readUint16(f);

    adsResource->tags = malloc(adsResource->numTags * sizeof(struct TTags));
    for (int i = 0; i < adsResource->numTags; i++) {
        adsResource->tags[i].id = ps1_readUint16(f);
        adsResource->tags[i].description = ps1_getString(f, 40);
    }

    return adsResource;
}

struct TBmpResource* ps1_parseBmpResource(PS1File *f, const char *resName)
{
    struct TBmpResource *bmpResource;
    uint8 *buffer;

    bmpResource = malloc(sizeof(struct TBmpResource));
    if (!bmpResource) return NULL;

    bmpResource->resName = malloc(strlen(resName) + 1);
    strcpy(bmpResource->resName, resName);

    /* Read "BMP:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "BMP:", 4)) {
        free(buffer);
        free(bmpResource->resName);
        free(bmpResource);
        return NULL;
    }
    free(buffer);

    bmpResource->width = ps1_readUint16(f);
    bmpResource->height = ps1_readUint16(f);

    /* Read "INF:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "INF:", 4)) {
        free(buffer);
        free(bmpResource->resName);
        free(bmpResource);
        return NULL;
    }
    free(buffer);

    bmpResource->dataSize = ps1_readUint32(f);
    bmpResource->numImages = ps1_readUint16(f);

    bmpResource->widths = ps1_readUint16Block(f, bmpResource->numImages);
    bmpResource->heights = ps1_readUint16Block(f, bmpResource->numImages);

    /* Read "BIN:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "BIN:", 4)) {
        free(buffer);
        free(bmpResource->widths);
        free(bmpResource->heights);
        free(bmpResource->resName);
        free(bmpResource);
        return NULL;
    }
    free(buffer);

    bmpResource->compressedSize = ps1_readUint32(f) - 5;
    bmpResource->compressionMethod = ps1_readUint8(f);
    bmpResource->uncompressedSize = ps1_readUint32(f);

    /* Save file offset (not used with pre-extracted files, but keep for compatibility) */
    bmpResource->fileOffset = (uint32)ps1_ftell(f);

    /* Skip all decompression - we load from pre-extracted files in BMP/ directory */
    bmpResource->uncompressedData = NULL;
    ps1_fseek(f, bmpResource->compressedSize, SEEK_CUR);

    bmpResource->lastUsedTick = 0;
    bmpResource->pinCount = 0;

    return bmpResource;
}

struct TPalResource* ps1_parsePalResource(PS1File *f, const char *resName)
{
    struct TPalResource *palResource;
    uint8 *buffer;

    palResource = malloc(sizeof(struct TPalResource));
    if (!palResource) return NULL;

    palResource->resName = malloc(strlen(resName) + 1);
    strcpy(palResource->resName, resName);

    /* Read "PAL:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "PAL:", 4)) {
        free(buffer);
        free(palResource->resName);
        free(palResource);
        return NULL;
    }
    free(buffer);

    palResource->size = ps1_readUint16(f);
    palResource->unknown1 = ps1_readUint8(f);
    palResource->unknown2 = ps1_readUint8(f);

    /* Read "VGA:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "VGA:", 4)) {
        free(buffer);
        free(palResource->resName);
        free(palResource);
        return NULL;
    }
    free(buffer);

    /* Skip size bytes */
    ps1_readUint8(f);
    ps1_readUint8(f);
    ps1_readUint8(f);
    ps1_readUint8(f);

    /* Read 256 RGB colors */
    for (int i = 0; i < 256; i++) {
        palResource->colors[i].r = ps1_readUint8(f);
        palResource->colors[i].g = ps1_readUint8(f);
        palResource->colors[i].b = ps1_readUint8(f);
    }

    return palResource;
}

/* Counter for SCR decompression - prioritize INTRO.SCR for title screen */
static int scrDecompressCount = 0;
#define MAX_SCR_DECOMPRESS 2  /* Allow INTRO.SCR + one more for testing */
static int introScrLoaded = 0;  /* Track if we've loaded INTRO.SCR */

struct TScrResource* ps1_parseScrResource(PS1File *f, const char *resName)
{
    struct TScrResource *scrResource;
    uint8 *buffer;

    scrResource = malloc(sizeof(struct TScrResource));
    if (!scrResource) return NULL;

    scrResource->resName = malloc(strlen(resName) + 1);
    strcpy(scrResource->resName, resName);

    /* Read "SCR:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "SCR:", 4)) {
        free(buffer);
        free(scrResource->resName);
        free(scrResource);
        return NULL;
    }
    free(buffer);

    scrResource->totalSize = ps1_readUint16(f);
    scrResource->flags = ps1_readUint16(f);

    /* Read "DIM:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "DIM:", 4)) {
        free(buffer);
        free(scrResource->resName);
        free(scrResource);
        return NULL;
    }
    free(buffer);

    scrResource->dimSize = ps1_readUint32(f);
    scrResource->width = ps1_readUint16(f);
    scrResource->height = ps1_readUint16(f);

    /* Read "BIN:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "BIN:", 4)) {
        free(buffer);
        free(scrResource->resName);
        free(scrResource);
        return NULL;
    }
    free(buffer);

    scrResource->compressedSize = ps1_readUint32(f) - 5;
    scrResource->compressionMethod = ps1_readUint8(f);
    scrResource->uncompressedSize = ps1_readUint32(f);

    /* Skip all decompression - we load from pre-extracted files in SCR/ directory */
    scrResource->uncompressedData = NULL;
    ps1_fseek(f, scrResource->compressedSize, SEEK_CUR);

    scrResource->lastUsedTick = 0;
    scrResource->pinCount = 0;

    return scrResource;
}

struct TTtmResource* ps1_parseTtmResource(PS1File *f, const char *resName)
{
    struct TTtmResource *ttmResource;
    uint8 *buffer;

    ttmResource = malloc(sizeof(struct TTtmResource));
    if (!ttmResource) return NULL;

    ttmResource->resName = malloc(strlen(resName) + 1);
    strcpy(ttmResource->resName, resName);

    /* Read "VER:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "VER:", 4)) {
        free(buffer);
        free(ttmResource->resName);
        free(ttmResource);
        return NULL;
    }
    free(buffer);

    ttmResource->versionSize = ps1_readUint32(f);
    ttmResource->versionString = ps1_readUint8Block(f, 5);

    /* Read "PAG:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "PAG:", 4)) {
        free(buffer);
        free(ttmResource->versionString);
        free(ttmResource->resName);
        free(ttmResource);
        return NULL;
    }
    free(buffer);

    ttmResource->numPages = ps1_readUint32(f);
    ttmResource->pagUnknown1 = ps1_readUint8(f);
    ttmResource->pagUnknown2 = ps1_readUint8(f);

    /* Read "TT3:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "TT3:", 4)) {
        free(buffer);
        free(ttmResource->versionString);
        free(ttmResource->resName);
        free(ttmResource);
        return NULL;
    }
    free(buffer);

    ttmResource->compressedSize = ps1_readUint32(f) - 5;
    ttmResource->compressionMethod = ps1_readUint8(f);
    ttmResource->uncompressedSize = ps1_readUint32(f);

    /* ALWAYS lazy load TTMs from pre-extracted files on CD (TTM/ directory)
     * This bypasses potential LZW decompression bugs and ensures we get
     * the exact byte-for-byte correct TTM bytecode */
    ttmResource->uncompressedData = NULL;
    ps1_fseek(f, ttmResource->compressedSize, SEEK_CUR);
    printf("TTM: %s will lazy-load from TTM/ (%u bytes)\n", resName, ttmResource->uncompressedSize);
    ttmResource->lastUsedTick = 0;
    ttmResource->pinCount = 0;

    /* Read "TTI:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "TTI:", 4)) {
        free(buffer);
        free(ttmResource->versionString);
        free(ttmResource->resName);
        free(ttmResource);
        return NULL;
    }
    free(buffer);

    ttmResource->ttiUnknown1 = ps1_readUint8(f);
    ttmResource->ttiUnknown2 = ps1_readUint8(f);
    ttmResource->ttiUnknown3 = ps1_readUint8(f);
    ttmResource->ttiUnknown4 = ps1_readUint8(f);

    /* Read "TAG:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (!buffer || memcmp(buffer, "TAG:", 4)) {
        free(buffer);
        free(ttmResource->versionString);
        free(ttmResource->resName);
        free(ttmResource);
        return NULL;
    }
    free(buffer);

    ttmResource->tagSize = ps1_readUint32(f);
    ttmResource->numTags = ps1_readUint16(f);

    ttmResource->tags = malloc(ttmResource->numTags * sizeof(struct TTags));
    for (int i = 0; i < ttmResource->numTags; i++) {
        ttmResource->tags[i].id = ps1_readUint16(f);
        ttmResource->tags[i].description = ps1_getString(f, 40);
    }

    return ttmResource;
}

/* ========================================================================
 * PS1-specific decompression functions
 * These work with PS1File (preloaded buffer) instead of FILE*
 * ======================================================================== */

/* LZW decompression structures */
struct PS1_TCodeTableEntry {
    uint16 prefix;
    uint8 append;
};

/* Static LZW buffers (16KB total) - matches original */
static struct PS1_TCodeTableEntry ps1_codeTable[4096];  /* 12KB */
static uint8 ps1_decodeStack[4096];                      /* 4KB */

/* Bit-reading state - using volatile to prevent compiler optimizations */
static volatile int ps1_nextbit;
static volatile uint8 ps1_current;
static volatile uint32 ps1_inOffset;
static volatile uint32 ps1_maxInOffset;

/* LZW buffer reading state - using static+volatile to avoid struct update and optimization issues */
static volatile uint8 *ps1_lzwBuffer = NULL;
static volatile size_t ps1_lzwPos = 0;
static volatile size_t ps1_lzwSize = 0;

/* LZW debug info - saved for display after decompression */
uint16 ps1_lzwFirstCode = 0;
uint16 ps1_lzwDebugCodes[4] = {0, 0, 0, 0};
uint32 ps1_lzwLoopCount = 0;
uint8 ps1_lzwFirstByte = 0;
uint8 ps1_lzwBufBytes[4] = {0, 0, 0, 0};
uint8 ps1_lzwReturnedBytes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int ps1_lzwByteCount = 0;
uint8 ps1_lzwCurrentAfterOldcode = 0;  /* ps1_current after first getBits */
uint8 ps1_lzwNextbitAfterOldcode = 0;  /* ps1_nextbit after first getBits */
/* Debug for first newcode getBits */
int ps1_lzwGetBitsCallCount = 0;
uint8 ps1_lzwNC1_inputCur = 0;    /* localCurrent at start of first newcode getBits */
uint8 ps1_lzwNC1_inputNb = 0;     /* localNextbit at start of first newcode getBits */
uint16 ps1_lzwNC1_result = 0;     /* result of first newcode getBits */
/* Bit-by-bit debug for first newcode */
uint8 ps1_lzwNC1_bits[9] = {0};   /* Each bit value (0 or 1) */
uint8 ps1_lzwNC1_curBytes[2] = {0}; /* localCurrent bytes used */

/* Internal byte reader using static buffer pointer */
static uint8 ps1_getByte(PS1File *f)
{
    (void)f;  /* Unused - using static buffer instead */
    if (ps1_inOffset >= ps1_maxInOffset) {
        return 0;
    }
    ps1_inOffset++;
    /* Explicit increment to avoid MIPS compiler issues with post-increment */
    uint8 byte = ps1_lzwBuffer[ps1_lzwPos];
    ps1_lzwPos++;

    /* Capture first 8 bytes returned for debug */
    if (ps1_lzwByteCount < 8) {
        ps1_lzwReturnedBytes[ps1_lzwByteCount] = byte;
        ps1_lzwByteCount++;
    }
    return byte;
}

/* Bit reader for LZW - using local variables to avoid volatile issues in loop */
static uint16 ps1_getBits(PS1File *f, uint32 n)
{
    if (n == 0)
        return 0;

    /* Track call count for debug */
    ps1_lzwGetBitsCallCount++;

    /* Copy volatile state to local variables for efficient loop */
    uint8 localCurrent = ps1_current;
    int localNextbit = ps1_nextbit;

    /* Capture input state for 2nd call (first newcode) */
    if (ps1_lzwGetBitsCallCount == 2) {
        ps1_lzwNC1_inputCur = localCurrent;
        ps1_lzwNC1_inputNb = (uint8)localNextbit;
        ps1_lzwNC1_curBytes[0] = localCurrent;  /* First byte used */
    }

    uint32 x = 0;
    uint32 i;

    for (i = 0; i < n; i++) {
        /* Test bit and set if needed - use unsigned shift to avoid MIPS issues */
        uint32 bitMask = ((uint32)1 << (uint32)localNextbit);
        uint8 bitVal = (localCurrent & bitMask) ? 1 : 0;
        if (bitVal) {
            x = x | ((uint32)1 << i);
        }

        /* Capture bits for first newcode debug */
        if (ps1_lzwGetBitsCallCount == 2 && i < 9) {
            ps1_lzwNC1_bits[i] = bitVal;
        }

        localNextbit++;
        if (localNextbit > 7) {
            localCurrent = ps1_getByte(f);
            localNextbit = 0;
            /* Capture second byte for first newcode */
            if (ps1_lzwGetBitsCallCount == 2) {
                ps1_lzwNC1_curBytes[1] = localCurrent;
            }
        }
    }

    /* Write back to volatile state */
    ps1_current = localCurrent;
    ps1_nextbit = localNextbit;

    /* Capture result for 2nd call (first newcode) */
    if (ps1_lzwGetBitsCallCount == 2) {
        ps1_lzwNC1_result = (uint16)x;
    }

    return (uint16)x;
}

/* PS1 LZW decompression */
uint8 *ps1_uncompressLZW(PS1File *f, uint32 inSize, uint32 outSize)
{
    uint8 *outData;
    uint32 stackPtr = 0;
    uint8 n_bits = 9;
    uint32 free_entry = 257;
    uint16 oldcode;
    uint16 lastbyte;
    uint32 bitpos = 0;
    uint32 outOffset = 0;

    if (outSize == 0) {
        return NULL;
    }

    ps1_maxInOffset = inSize;
    ps1_nextbit = 0;
    ps1_inOffset = 0;
    outData = malloc(outSize);
    if (!outData) {
        return NULL;
    }

    /* Initialize static buffer reading - bypass f->currentPos issues */
    ps1_lzwBuffer = f->buffer + f->currentPos;
    ps1_lzwPos = 0;
    ps1_lzwSize = f->bufferSize - f->currentPos;
    ps1_lzwByteCount = 0;  /* Reset byte capture counter */
    ps1_lzwGetBitsCallCount = 0;  /* Reset getBits call counter */

    /* Save buffer bytes for debug */
    for (int i = 0; i < 4; i++) {
        ps1_lzwBufBytes[i] = ps1_lzwBuffer[i];
    }

    ps1_current = ps1_getByte(f);
    ps1_lzwFirstByte = ps1_current;  /* Save for debug */

    lastbyte = oldcode = ps1_getBits(f, n_bits);
    outData[outOffset++] = (uint8)oldcode;

    /* Save debug info after first getBits */
    ps1_lzwFirstCode = oldcode;
    ps1_lzwCurrentAfterOldcode = ps1_current;  /* Should be 0x92 after reading 9 bits */
    ps1_lzwNextbitAfterOldcode = ps1_nextbit;  /* Should be 1 after reading 9 bits */

    /* Debug: track first 4 codes */
    int debugIdx = 0;
    ps1_lzwLoopCount = 0;
    for (int i = 0; i < 4; i++) ps1_lzwDebugCodes[i] = 0;

    while (ps1_inOffset < inSize) {
        uint16 newcode = ps1_getBits(f, n_bits);
        bitpos += n_bits;

        /* Capture first 4 newcodes */
        if (debugIdx < 4) {
            ps1_lzwDebugCodes[debugIdx++] = newcode;
        }
        ps1_lzwLoopCount++;

        if (newcode == 256) {
            uint32 nbits3 = n_bits << 3;
            uint32 nskip = (nbits3 - ((bitpos - 1) % nbits3)) - 1;
            ps1_getBits(f, nskip);
            n_bits = 9;
            free_entry = 256;
            bitpos = 0;
        }
        else {
            uint16 code = newcode;

            if (code >= free_entry) {
                if (stackPtr > 4095)
                    break;

                ps1_decodeStack[stackPtr] = (uint8)lastbyte;
                stackPtr++;
                code = oldcode;
            }

            while (code > 255) {
                if (code > 4095)
                    break;

                ps1_decodeStack[stackPtr] = ps1_codeTable[code].append;
                stackPtr++;
                code = ps1_codeTable[code].prefix;
            }

            ps1_decodeStack[stackPtr] = (uint8)code;
            stackPtr++;
            lastbyte = code;

            while (stackPtr > 0) {
                stackPtr--;

                if (outOffset >= outSize)
                    return outData;

                outData[outOffset++] = ps1_decodeStack[stackPtr];
            }

            if (free_entry < 4096) {
                ps1_codeTable[free_entry].prefix = (uint16)oldcode;
                ps1_codeTable[free_entry].append = (uint8)lastbyte;
                free_entry++;
                uint32 temp = 1 << n_bits;

                if (free_entry >= temp && n_bits < 12) {
                    n_bits++;
                    bitpos = 0;
                }
            }

            oldcode = newcode;
        }
    }

    return outData;
}

/* PS1 RLE decompression */
uint8 *ps1_uncompressRLE(PS1File *f, uint32 inSize, uint32 outSize)
{
    uint8 *outData;
    uint32 outOffset = 0;

    ps1_inOffset = 0;
    ps1_maxInOffset = inSize;

    outData = malloc(outSize);
    if (!outData) {
        printf("ps1_uncompressRLE: malloc failed for %lu bytes\n", (unsigned long)outSize);
        return NULL;
    }

    while (outOffset < outSize && ps1_inOffset < inSize) {
        uint8 control = ps1_readUint8(f);
        ps1_inOffset++;

        if ((control & 0x80) == 0x80) {
            uint8 length = control & 0x7F;
            uint8 b = ps1_readUint8(f);
            ps1_inOffset++;

            for (int i = 0; i < length && outOffset < outSize; i++)
                outData[outOffset++] = b;
        }
        else {
            for (int i = 0; i < control && outOffset < outSize; i++) {
                outData[outOffset++] = ps1_readUint8(f);
                ps1_inOffset++;
            }
        }
    }

    return outData;
}

/* Main PS1 decompression entry point */
uint8 *ps1_uncompress(PS1File *f, uint8 compressionMethod, uint32 inSize, uint32 outSize)
{
    switch (compressionMethod) {
        case 1:
            return ps1_uncompressRLE(f, inSize, outSize);
        case 2:
            return ps1_uncompressLZW(f, inSize, outSize);
        default:
            printf("ps1_uncompress: unknown method %d\n", compressionMethod);
            return NULL;
    }
}

/*
 * Load BMP data on-demand from pre-extracted files in BMP/ directory.
 * Files are already decompressed, so we just read the whole file directly.
 * This is called when grLoadBmp finds uncompressedData is NULL.
 */
void ps1_loadBmpData(struct TBmpResource *bmpResource)
{
    uint32 readSize;

    if (bmpResource == NULL) return;
    if (bmpResource->uncompressedData != NULL) return;  /* Already loaded */
    if (bmpResource->uncompressedSize == 0) return;  /* No data to read */

    readSize = bmpResource->uncompressedSize;
    bmpResource->uncompressedData = ps1PilotLoadResource("bmp", bmpResource->resName, &readSize);
    if (bmpResource->uncompressedData != NULL) {
        bmpResource->uncompressedSize = readSize;
        return;
    }

    /* Once an ADS-family pack is active, BMP payloads are also authoritative.
     * The callers already handle a missing BMP by skipping sprite load cleanly. */
    if (ps1PilotActivePack.entries != NULL) {
        if (ps1PilotDbgFallbackWhilePackActive < 0xFFFFU)
            ps1PilotDbgFallbackWhilePackActive++;
        return;
    }

    /* Build path to pre-extracted file */
    char path[64];
    snprintf(path, sizeof(path), "\\BMP\\%s;1", bmpResource->resName);

    /* Get actual file size from CD (may differ from metadata) */
    CdlFILE cdfile;
    if (CdSearchFile(&cdfile, path) == NULL) {
        printf("ps1_loadBmpData: File not found: %s\n", path);
        return;
    }

    uint32 fileSize = cdfile.size;

    /* Use the smaller of file size and metadata size */
    readSize = (fileSize < bmpResource->uncompressedSize) ? fileSize : bmpResource->uncompressedSize;

    /* Build path for ps1_streamRead (without leading backslash and ;1) */
    snprintf(path, sizeof(path), "BMP\\%s", bmpResource->resName);

    /* Read entire file from CD - already decompressed, no processing needed */
    if (ps1PilotDbgFallbacks < 0xFFFFU)
        ps1PilotDbgFallbacks++;
    ps1PilotDbgLastFallbackEntry = 0;
    bmpResource->uncompressedData = ps1_streamRead(path, 0, readSize);

    /* Update uncompressedSize to match what we actually read */
    if (bmpResource->uncompressedData != NULL && fileSize < bmpResource->uncompressedSize) {
        printf("ps1_loadBmpData: Adjusting uncompressedSize from %u to %u for %s\n",
               bmpResource->uncompressedSize, fileSize, bmpResource->resName);
        bmpResource->uncompressedSize = fileSize;
    }
}

/*
 * Load SCR data on-demand from pre-extracted files in SCR/ directory.
 * Files are already decompressed, so we just read the whole file directly.
 * This is called when grLoadScreen finds uncompressedData is NULL.
 */
void ps1_loadScrData(struct TScrResource *scrResource)
{
    uint32 readSize;

    if (scrResource == NULL) return;
    if (scrResource->uncompressedData != NULL) return;  /* Already loaded */
    if (scrResource->uncompressedSize == 0) return;  /* No data to read */

    readSize = scrResource->uncompressedSize;
    scrResource->uncompressedData = ps1PilotLoadResource("scr", scrResource->resName, &readSize);
    if (scrResource->uncompressedData != NULL) {
        scrResource->uncompressedSize = readSize;
        return;
    }

    /* Once an ADS-family pack is active, the scene-root SCR payload is also
     * authoritative. Keep fallback only for more dynamic secondary assets. */
    if (ps1PilotActivePack.entries != NULL) {
        if (ps1PilotDbgFallbackWhilePackActive < 0xFFFFU)
            ps1PilotDbgFallbackWhilePackActive++;
        return;
    }

    /* Build path to pre-extracted file: "SCR/OCEAN00.SCR" etc.
     * ps1_streamRead will prepend backslash and append ";1" */
    char path[32];
    snprintf(path, sizeof(path), "SCR\\%s", scrResource->resName);

    /* Read entire file from CD - already decompressed, no processing needed
     * Trust the metadata uncompressedSize - the extracted files should match */
    if (ps1PilotDbgFallbacks < 0xFFFFU)
        ps1PilotDbgFallbacks++;
    ps1PilotDbgLastFallbackEntry = 0;
    scrResource->uncompressedData = ps1_streamRead(path, 0, scrResource->uncompressedSize);
}

/*
 * Load TTM bytecode on-demand from pre-extracted files in TTM/ directory.
 * Files are already decompressed, so we just read the whole file directly.
 * This is called when ttmLoadTtm finds uncompressedData is NULL.
 */
void ps1_loadTtmData(struct TTtmResource *ttmResource)
{
    uint32 readSize;

    if (ttmResource == NULL) return;
    if (ttmResource->uncompressedData != NULL) return;  /* Already loaded */
    if (ttmResource->uncompressedSize == 0) return;  /* No data to read */

    readSize = ttmResource->uncompressedSize;
    ttmResource->uncompressedData = ps1PilotLoadResource("ttm", ttmResource->resName, &readSize);
    if (ttmResource->uncompressedData != NULL) {
        ttmResource->uncompressedSize = readSize;
        return;
    }

    /* Once an ADS-family pack is active, TTM bytecode should come from the pack.
     * Keep extracted-file fallback only for BMP during rollout validation. */
    if (ps1PilotActivePack.entries != NULL) {
        if (ps1PilotDbgFallbackWhilePackActive < 0xFFFFU)
            ps1PilotDbgFallbackWhilePackActive++;
        return;
    }

    /* Build path to pre-extracted file: "TTM/FISHWALK.TTM" etc.
     * ps1_streamRead will prepend backslash and append ";1" */
    char path[32];
    snprintf(path, sizeof(path), "TTM\\%s", ttmResource->resName);

    /* Read entire file from CD - already decompressed bytecode */
    if (ps1PilotDbgFallbacks < 0xFFFFU)
        ps1PilotDbgFallbacks++;
    ps1PilotDbgLastFallbackEntry = 0;
    ttmResource->uncompressedData = ps1_streamRead(path, 0, ttmResource->uncompressedSize);
}

/*
 * Load ADS bytecode on-demand from pre-extracted files in ADS/ directory.
 * Files are already decompressed, so we just read the whole file directly.
 * This is called when adsPlay finds uncompressedData is NULL.
 */
void ps1_loadAdsData(struct TAdsResource *adsResource)
{
    uint32 readSize;

    if (adsResource == NULL) return;
    if (adsResource->uncompressedData != NULL) return;  /* Already loaded */
    if (adsResource->uncompressedSize == 0) return;  /* No data to read */

    ps1PilotSetActivePackForAds(adsResource->resName);
    readSize = adsResource->uncompressedSize;
    adsResource->uncompressedData = ps1PilotLoadResource("ads", adsResource->resName, &readSize);
    if (adsResource->uncompressedData != NULL) {
        adsResource->uncompressedSize = readSize;
        return;
    }

    /* Once an ADS-family pack is active, the ADS payload itself is authoritative.
     * Keep fallback only for secondary assets (BMP/SCR/TTM) while validating pack
     * coverage; if the ADS root record is missing, fail the scene load cleanly. */
    if (ps1PilotActivePack.entries != NULL) {
        if (ps1PilotDbgFallbackWhilePackActive < 0xFFFFU)
            ps1PilotDbgFallbackWhilePackActive++;
        return;
    }

    /* Build path to pre-extracted file: "ADS/STAND.ADS" etc.
     * ps1_streamRead will prepend backslash and append ";1" */
    char path[32];
    snprintf(path, sizeof(path), "ADS\\%s", adsResource->resName);

    /* Read entire file from CD - already decompressed bytecode */
    if (ps1PilotDbgFallbacks < 0xFFFFU)
        ps1PilotDbgFallbacks++;
    ps1PilotDbgLastFallbackEntry = 0;
    adsResource->uncompressedData = ps1_streamRead(path, 0, adsResource->uncompressedSize);
}
