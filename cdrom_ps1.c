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
static uint8_t ps1ReadBuffer[CD_SECTOR_SIZE];  /* Shared read buffer */

PS1File* ps1_fopen(const char* filename, const char* mode)
{
    /* PURPLE checkpoint = Entered ps1_fopen */
    {
        ResetGraph(0);
        SetVideoMode(MODE_NTSC);
        DRAWENV draw;
        SetDefDrawEnv(&draw, 0, 0, 640, 480);
        setRGB0(&draw, 128, 0, 128);  /* PURPLE = Inside ps1_fopen */
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 60; i++) VSync(0);
    }

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

    /* Wait for CD to be ready - PSX CD needs time to initialize */
    for (int i = 0; i < 1000000; i++) {
        /* Busy wait */
    }

    /* Use CdSearchFile to find the actual file on CD-ROM */
    CdlFILE *result = CdSearchFile(&file->cdfile, filename);

    if (result == NULL) {
        /* File not found - show RED screen for 1 second */
        ResetGraph(0);
        SetVideoMode(MODE_NTSC);
        DRAWENV draw;
        SetDefDrawEnv(&draw, 0, 0, 640, 480);
        setRGB0(&draw, 255, 0, 0);  /* RED = File not found */
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 60; i++) VSync(0);
        return NULL;  /* CdSearchFile failed */
    }

    /* Initialize file structure */
    file->currentPos = 0;
    file->isOpen = 1;
    strncpy(file->filename, filename, sizeof(file->filename) - 1);
    file->filename[sizeof(file->filename) - 1] = '\0';

    /* Preload entire file into buffer to avoid CD-ROM calls during parsing */
    file->bufferSize = file->cdfile.size;
    file->buffer = (uint8_t*)malloc(file->bufferSize);

    if (!file->buffer) {
        return NULL;  /* Malloc failed */
    }

    /* ORANGE checkpoint = About to read from CD-ROM */
    {
        ResetGraph(0);
        SetVideoMode(MODE_NTSC);
        DRAWENV draw;
        SetDefDrawEnv(&draw, 0, 0, 640, 480);
        setRGB0(&draw, 255, 165, 0);  /* ORANGE = About to CD read */
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 60; i++) VSync(0);
    }

    /* Read entire file into buffer using PSn00bSDK CD-ROM API */
    int numSectors = (file->bufferSize + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;

    /* Position CD head at file location first */
    CdControl(CdlSetloc, (uint8_t*)&file->cdfile.pos, NULL);

    /* CRITICAL: Wait for CdControl to complete before reading! */
    while (CdControlB(CdlNop, NULL, NULL) == 0) {
        /* Busy-wait for seek to complete */
    }

    /* BROWN checkpoint = After CdControl completes */
    {
        ResetGraph(0);
        SetVideoMode(MODE_NTSC);
        DRAWENV draw;
        SetDefDrawEnv(&draw, 0, 0, 640, 480);
        setRGB0(&draw, 139, 69, 19);  /* BROWN = CdControl done */
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 60; i++) VSync(0);
    }

    /* Use CdRead() not CdReadRetry() - simpler and more reliable */
    CdRead(numSectors, (uint32_t*)file->buffer, CdlModeSpeed);

    /* LIGHT BLUE checkpoint = After CdReadRetry, before polling */
    {
        ResetGraph(0);
        SetVideoMode(MODE_NTSC);
        DRAWENV draw;
        SetDefDrawEnv(&draw, 0, 0, 640, 480);
        setRGB0(&draw, 173, 216, 230);  /* LIGHT BLUE = CdReadRetry done */
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 60; i++) VSync(0);
    }

    /* CRITICAL FIX: CdReadSync(0, ...) is NON-BLOCKING! Must loop until complete */
    int sync_result;
    int timeout = 1000000;  /* Large timeout for polling */
    while (timeout-- > 0) {
        sync_result = CdReadSync(0, 0);
        if (sync_result == 0) {
            break;  /* Read complete! */
        }
        if (sync_result < 0) {
            /* Error occurred - show WHITE screen */
            {
                ResetGraph(0);
                SetVideoMode(MODE_NTSC);
                DRAWENV draw;
                SetDefDrawEnv(&draw, 0, 0, 640, 480);
                setRGB0(&draw, 255, 255, 255);  /* WHITE = Read error */
                draw.isbg = 1;
                PutDrawEnv(&draw);
                SetDispMask(1);
                for (int i = 0; i < 120; i++) VSync(0);
            }
            free(file->buffer);
            file->buffer = NULL;
            return NULL;
        }
        /* sync_result > 0 means still busy, keep looping */
    }

    if (timeout <= 0) {
        /* Timeout - show YELLOW screen */
        {
            ResetGraph(0);
            SetVideoMode(MODE_NTSC);
            DRAWENV draw;
            SetDefDrawEnv(&draw, 0, 0, 640, 480);
            setRGB0(&draw, 255, 255, 0);  /* YELLOW = Timeout */
            draw.isbg = 1;
            PutDrawEnv(&draw);
            SetDispMask(1);
            for (int i = 0; i < 120; i++) VSync(0);
        }
        free(file->buffer);
        file->buffer = NULL;
        return NULL;
    }

    /* PINK checkpoint = CD read completed successfully! */
    {
        ResetGraph(0);
        SetVideoMode(MODE_NTSC);
        DRAWENV draw;
        SetDefDrawEnv(&draw, 0, 0, 640, 480);
        setRGB0(&draw, 255, 192, 203);  /* PINK = CD read success */
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 120; i++) VSync(0);  /* Hold for 2 seconds */
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
    /* PURPLE = Entered ps1_getString */
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    setRGB0(&draw, 128, 0, 128);
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    for (int j = 0; j < 30; j++) VSync(0);

    char *str = malloc(maxlen + 1);  /* Use malloc instead of safe_malloc */

    if (!str) {
        /* RED = malloc failed */
        setRGB0(&draw, 255, 0, 0);
        PutDrawEnv(&draw);
        for (int j = 0; j < 60; j++) VSync(0);
        return NULL;
    }

    /* BLUE = malloc succeeded, about to start loop */
    setRGB0(&draw, 0, 0, 255);
    PutDrawEnv(&draw);
    for (int j = 0; j < 30; j++) VSync(0);

    int i;
    for (i = 0; i < maxlen; i++) {
        str[i] = ps1_readUint8(f);
        if (str[i] == 0) break;
    }
    str[i] = 0;

    /* WHITE = getString completed successfully */
    setRGB0(&draw, 255, 255, 255);
    PutDrawEnv(&draw);
    for (int j = 0; j < 60; j++) VSync(0);

    return str;
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

struct TScrResource* ps1_parseScrResource(PS1File *f, const char *resName)
{
    struct TScrResource *scrResource;
    uint8 *buffer;

    scrResource = malloc(sizeof(struct TScrResource));  /* Use malloc instead of safe_malloc */
    scrResource->resName = malloc(strlen(resName) + 1);
    strcpy(scrResource->resName, resName);

    /* Visual checkpoint: MAGENTA = Starting SCR parsing */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    setRGB0(&draw, 255, 0, 255);  /* MAGENTA = SCR parsing */
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    for (int i = 0; i < 60; i++) VSync(0);

    /* Read "SCR:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (memcmp(buffer, "SCR:", 4)) {
        /* RED = Invalid SCR header */
        setRGB0(&draw, 255, 0, 0);
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 300; i++) VSync(0);
        free(buffer);
        free(scrResource->resName);
        free(scrResource);
        return NULL;
    }
    free(buffer);

    /* Read totalSize and flags */
    scrResource->totalSize = ps1_readUint16(f);
    scrResource->flags = ps1_readUint16(f);

    /* Read "DIM:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (memcmp(buffer, "DIM:", 4)) {
        /* RED = Invalid DIM header */
        setRGB0(&draw, 255, 0, 0);
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 300; i++) VSync(0);
        free(buffer);
        free(scrResource->resName);
        free(scrResource);
        return NULL;
    }
    free(buffer);

    /* Read dimensions */
    scrResource->dimSize = ps1_readUint32(f);
    scrResource->width = ps1_readUint16(f);
    scrResource->height = ps1_readUint16(f);

    /* Read "BIN:" header */
    buffer = ps1_readUint8Block(f, 4);
    if (memcmp(buffer, "BIN:", 4)) {
        /* RED = Invalid BIN header */
        setRGB0(&draw, 255, 0, 0);
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 300; i++) VSync(0);
        free(buffer);
        free(scrResource->resName);
        free(scrResource);
        return NULL;
    }
    free(buffer);

    /* Read compression info */
    scrResource->compressedSize = ps1_readUint32(f) - 5; // discard size of compressionmethod+uncompressedsize
    scrResource->compressionMethod = ps1_readUint8(f);
    scrResource->uncompressedSize = ps1_readUint32(f);

    /* For now, skip decompression and just store NULL */
    scrResource->uncompressedData = NULL;
    scrResource->lastUsedTick = 0;
    scrResource->pinCount = 0;

    /* CYAN = SCR parsing completed successfully! */
    setRGB0(&draw, 0, 255, 255);
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    for (int i = 0; i < 120; i++) VSync(0);

    return scrResource;
}

void ps1TestResourceLoading(void)
{
    /* Test opening RESOURCE.MAP and reading header */
    PS1File* mapFile = ps1_fopen("RESOURCE.MAP", "rb");

    if (!mapFile) {
        /* RED screen = File not found */
        ResetGraph(0);
        SetVideoMode(MODE_NTSC);
        DRAWENV draw;
        SetDefDrawEnv(&draw, 0, 0, 640, 480);
        setRGB0(&draw, 255, 0, 0);
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 300; i++) VSync(0);
        return;
    }

    /* File opened successfully - read header bytes */
    uint8 header[6];
    header[0] = ps1_readUint8(mapFile);
    header[1] = ps1_readUint8(mapFile);
    header[2] = ps1_readUint8(mapFile);
    header[3] = ps1_readUint8(mapFile);
    header[4] = ps1_readUint8(mapFile);
    header[5] = ps1_readUint8(mapFile);

    /* Skip resource filename (13 bytes) */
    for (int i = 0; i < 13; i++) {
        ps1_readUint8(mapFile);
    }

    /* Read number of entries */
    uint16 numEntries = ps1_readUint16(mapFile);

    ps1_fclose(mapFile);

    /* Show results via colors */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 640, 480);

    /* Check if we got reasonable data */
    if (numEntries > 0 && numEntries < 1000) {
        /* GREEN = Success! Got reasonable number of entries */
        setRGB0(&draw, 0, 255, 0);
    } else if (numEntries == 0) {
        /* YELLOW = No entries found */
        setRGB0(&draw, 255, 255, 0);
    } else {
        /* CYAN = Unreasonable number (data corruption?) */
        setRGB0(&draw, 0, 255, 255);
    }

    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);

    /* Hold result screen */
    for (int i = 0; i < 300; i++) {
        VSync(0);
    }
}
