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

/* New function to test reading RESOURCE.MAP content */
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

    /* File found, now try to read some data from it */
    /* Use CdControl to seek to file position */
    if (CdControl(CdlSeekL, (uint8_t*)&file.pos, NULL) == 0) {
        return 43;  /* Seek failed */
    }

    /* Wait for seek to complete */
    for (int i = 0; i < 1000000; i++) {
        /* Busy wait */
    }

    /* Try to read first sector of the file */
    uint32_t buffer[CD_SECTOR_SIZE / 4];  /* 2KB sector buffer */

    /* Clear buffer first to detect if data is actually read */
    for (int i = 0; i < CD_SECTOR_SIZE / 4; i++) {
        buffer[i] = 0xDEADBEEF;  /* Fill with pattern */
    }

    if (CdRead(1, buffer, CdlModeSpeed) == 0) {
        return 44;  /* Read failed */
    }

    /* Wait for read to complete */
    uint8_t result[8];
    if (CdReadSync(0, result) == CdlComplete) {
        /* Read successful - check if we got meaningful data */
        uint8_t *data = (uint8_t*)buffer;

        /* Check if buffer changed from our test pattern */
        if (buffer[0] != 0xDEADBEEF) {
            /* Buffer was modified, we got some data */
            /* Check first 16 bytes for non-zero content */
            int nonZeroCount = 0;
            for (int i = 0; i < 16; i++) {
                if (data[i] != 0) {
                    nonZeroCount++;
                }
            }

            if (nonZeroCount >= 3) {
                return 48;  /* Read successful with meaningful data */
            } else {
                return 49;  /* Read successful but mostly zeros */
            }
        } else {
            return 46;  /* Buffer unchanged - read may have failed */
        }
    }

    return 45;  /* Read sync failed */
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
