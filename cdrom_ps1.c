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

/* Visual checkpoint helper for this function */
static void checkpoint(int r, int g, int b) {
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    setRGB0(&draw, r, g, b);
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    /* Wait 1 second to see the color */
    for (int i = 0; i < 60; i++) {
        VSync(0);
    }
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

/* Visual debug helper for CD-ROM errors */
static void showCDError(int r, int g, int b) {
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    setRGB0(&draw, r, g, b);
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    while(1);  /* Hang with error color */
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

    /* Lazy loading: skip compressed data, will load on demand */
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

    /* Decompress first few BMPs for testing, lazy load the rest */
    static int bmpDecompressCount = 0;
    #define MAX_BMP_DECOMPRESS 3  /* Decompress first 3 BMPs for testing */

    if (bmpDecompressCount < MAX_BMP_DECOMPRESS) {
        bmpResource->uncompressedData = ps1_uncompress(f,
                                            bmpResource->compressionMethod,
                                            bmpResource->compressedSize,
                                            bmpResource->uncompressedSize);
        bmpDecompressCount++;
    } else {
        /* Lazy loading: skip compressed data */
        bmpResource->uncompressedData = NULL;
        ps1_fseek(f, bmpResource->compressedSize, SEEK_CUR);
    }

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

/* Counter for SCR decompression test - only decompress first few */
static int scrDecompressCount = 0;
#define MAX_SCR_DECOMPRESS 1  /* Only decompress first SCR for testing */

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

    /* Decompress first SCR for testing, skip the rest */
    static uint8 savedInputBytes[8];
    static size_t savedFilePos;

    if (scrDecompressCount < MAX_SCR_DECOMPRESS) {
        /* Save input bytes for later display */
        savedFilePos = f->currentPos;
        for (int i = 0; i < 8; i++) {
            savedInputBytes[i] = f->buffer[f->currentPos + i];
        }

        scrResource->uncompressedData = ps1_uncompress(f,
                                            scrResource->compressionMethod,
                                            scrResource->compressedSize,
                                            scrResource->uncompressedSize);
        scrDecompressCount++;

        /* Quick debug: show unique byte count */
        if (scrResource->uncompressedData) {
            uint8 *out = scrResource->uncompressedData;
            uint32 outSize = scrResource->uncompressedSize;

            /* Count unique values in first 1K */
            uint8 foundValues[16] = {0};
            int numUnique = 0;
            int sampleSize = (outSize > 1000) ? 1000 : outSize;

            for (int i = 0; i < sampleSize && numUnique < 16; i++) {
                uint8 val = out[i];
                int found = 0;
                for (int j = 0; j < numUnique; j++) {
                    if (foundValues[j] == val) { found = 1; break; }
                }
                if (!found) {
                    foundValues[numUnique++] = val;
                }
            }

            /* Declare LZW debug vars as extern */
            extern uint16 ps1_lzwFirstCode;
            extern uint16 ps1_lzwDebugCodes[4];
            extern uint32 ps1_lzwLoopCount;
            extern uint8 ps1_lzwFirstByte;
            extern uint8 ps1_lzwBufBytes[4];
            extern uint8 ps1_lzwReturnedBytes[8];
            extern uint8 ps1_lzwCurrentAfterOldcode;
            extern uint8 ps1_lzwNextbitAfterOldcode;
            extern uint8 ps1_lzwNC1_inputCur;
            extern uint8 ps1_lzwNC1_inputNb;
            extern uint16 ps1_lzwNC1_result;
            extern uint8 ps1_lzwNC1_bits[9];
            extern uint8 ps1_lzwNC1_curBytes[2];

            /* LZW decompression successful - skip debug display */
            (void)outSize;  /* Suppress unused warning */
        }
    } else {
        /* Skip compressed data for remaining SCRs */
        scrResource->uncompressedData = NULL;
        ps1_fseek(f, scrResource->compressedSize, SEEK_CUR);
    }

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

    /* Lazy loading: skip compressed data, will load on demand */
    ttmResource->uncompressedData = NULL;
    ttmResource->lastUsedTick = 0;
    ttmResource->pinCount = 0;
    ps1_fseek(f, ttmResource->compressedSize, SEEK_CUR);

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
