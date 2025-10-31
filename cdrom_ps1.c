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

/* CD file handle structure */
#define MAX_CD_FILES 8
static CdlFILE cdFiles[MAX_CD_FILES];
static uint8 cdFileInUse[MAX_CD_FILES];
static uint32 cdFilePos[MAX_CD_FILES];  /* Current position in bytes */
static uint8 *cdReadBuffer = NULL;
static uint32 cdReadBufferPos = 0;
static uint32 cdReadBufferSize = 0;

/* CD-ROM read buffer (32KB for efficient sector reading) */
#define CD_BUFFER_SIZE (32 * 1024)
static uint8 *cdSectorBuffer = NULL;  /* Malloc'd, not static array! */

/*
 * Initialize CD-ROM subsystem
 */
int cdromInit()
{
    /* DON'T call CdInit() when booting from CD-ROM! */
    /* The BIOS already initialized it for us. Calling CdInit() crashes! */

    /* Allocate CD sector buffer dynamically to reduce BSS size */
    if (cdSectorBuffer == NULL) {
        cdSectorBuffer = (uint8*)malloc(CD_BUFFER_SIZE);
        if (!cdSectorBuffer) {
            ps1DebugInit();
            ps1DebugClear();
            ps1DebugPrint("CD-ROM Init Failed");
            ps1DebugPrint("");
            ps1DebugPrint("Failed to allocate sector buffer");
            ps1DebugPrint("Size needed: %d bytes", CD_BUFFER_SIZE);
            ps1DebugFlush();
            ps1DebugWait();
            return -1;
        }
    }

    /* Just initialize our internal state */
    for (int i = 0; i < MAX_CD_FILES; i++) {
        cdFileInUse[i] = 0;
        cdFilePos[i] = 0;
    }

    cdReadBuffer = NULL;
    cdReadBufferPos = 0;
    cdReadBufferSize = 0;

    /* DON'T set CD-ROM mode when booting from CD-ROM! */
    /* The BIOS already configured it for us. Trying to change it will fail. */
    /* The CD-ROM is already in the correct mode for reading data (2048 byte sectors) */

    /* DEBUG: Silent file search test - just verify CdSearchFile works */
    /* (Visual debugging moved to main() after graphics init) */

    if (debugMode) {
        printf("CD-ROM: Initialized (BIOS boot mode)\n");
    }

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
 * Open a file from CD-ROM
 * Returns file handle ID (0-7) or -1 on error
 */
int cdromOpen(const char *filename)
{
    int slot = cdromFindFreeSlot();
    if (slot < 0) {
        if (debugMode) {
            printf("CD-ROM: No free file slots\n");
        }
        showCDError(128, 0, 0);  /* DARK RED = No free slots */
    }

    /* Convert filename to uppercase (CD-ROM standard) */
    char upperName[256];
    int i = 0;
    while (filename[i] && i < 255) {
        if (filename[i] >= 'a' && filename[i] <= 'z') {
            upperName[i] = filename[i] - 32;  /* Convert to uppercase */
        } else if (filename[i] == '/') {
            upperName[i] = '\\';  /* Convert Unix path to DOS path */
        } else {
            upperName[i] = filename[i];
        }
        i++;
    }
    upperName[i] = '\0';

    /* CD-ROM path - ISO 9660 format with version number */
    /* Try without leading backslash first, then with backslash if that fails */
    char cdPath[256];
    snprintf(cdPath, sizeof(cdPath), "%s;1", upperName);

    /* Search for file on CD */
    if (!CdSearchFile(&cdFiles[slot], cdPath)) {
        /* Visual debug: show detailed error */
        ps1DebugInit();
        ps1DebugClear();
        ps1DebugPrint("CD-ROM File Not Found");
        ps1DebugPrint("");
        ps1DebugPrint("Original filename: %s", filename);
        ps1DebugPrint("Uppercase: %s", upperName);
        ps1DebugPrint("CD path tried: %s", cdPath);
        ps1DebugPrint("");
        ps1DebugPrint("CdSearchFile() returned NULL");
        ps1DebugPrint("");
        ps1DebugPrint("Possible causes:");
        ps1DebugPrint("- File not on CD image");
        ps1DebugPrint("- Wrong path format");
        ps1DebugPrint("- CD not initialized");
        ps1DebugFlush();
        ps1DebugWait();

        showCDError(255, 0, 128);  /* PINK = CdSearchFile failed */
    }

    cdFileInUse[slot] = 1;
    cdFilePos[slot] = 0;  /* Start at beginning of file */

    if (debugMode) {
        printf("CD-ROM: Opened %s (slot %d, size %d)\n",
               cdPath, slot, cdFiles[slot].size);
    }

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

    /* Seek to position */
    if (CdControl(CdlSetloc, (uint8*)&readLoc, NULL) == 0) {
        if (debugMode) {
            printf("CD-ROM: Setloc failed\n");
        }
        return -1;
    }

    /* Wait for seek to complete (with timeout) */
    int timeout = 10000;
    while (CdSync(1, NULL) > 0 && timeout-- > 0);

    if (timeout <= 0) {
        showCDError(255, 255, 0);  /* YELLOW = Seek timeout */
    }

    /* Read data */
    if (CdRead(sectorsToRead, (uint32*)cdSectorBuffer, CdlModeSpeed) == 0) {
        showCDError(255, 128, 0);  /* ORANGE = CdRead failed */
    }

    /* Wait for read to complete (with timeout) */
    timeout = 10000;
    while (CdSync(1, NULL) > 0 && timeout-- > 0);

    if (timeout <= 0) {
        showCDError(255, 255, 255);  /* WHITE = Read completion timeout */
    }

    /* Copy requested bytes from buffer (accounting for offset within first sector) */
    memcpy(buffer, cdSectorBuffer + offsetInSector, size);

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

    if (debugMode) {
        printf("CD-ROM: Closed file handle %d\n", fileHandle);
    }

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
