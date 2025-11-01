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
/* Must be 4-byte aligned for DMA operations! */
#define CD_BUFFER_SIZE (32 * 1024)
static uint32 cdSectorBuffer[CD_BUFFER_SIZE / 4] __attribute__((aligned(4)));  /* Static, properly aligned for DMA */

/*
 * Initialize CD-ROM subsystem
 */
int cdromInit()
{
    /* Don't clear screen - let main() control debug output */
    ps1DebugPrint("cdromInit: ENTRY");
    ps1DebugFlush();

    /* DON'T call CdInit() when booting from CD-ROM! */
    /* The BIOS already initialized it for us. Calling CdInit() crashes! */

    /* CD sector buffer is statically allocated with proper DMA alignment */
    /* No need to malloc - it's a static array */

    /* Just initialize our internal state */
    for (int i = 0; i < MAX_CD_FILES; i++) {
        cdFileInUse[i] = 0;
        cdFilePos[i] = 0;
    }

    ps1DebugPrint("File slots initialized");
    ps1DebugFlush();

    cdReadBuffer = NULL;
    cdReadBufferPos = 0;
    cdReadBufferSize = 0;

    /* DON'T set CD-ROM mode when booting from CD-ROM! */
    /* The BIOS already configured it for us. Trying to change it will fail. */
    /* The CD-ROM is already in the correct mode for reading data (2048 byte sectors) */

    ps1DebugPrint("Calling CdInit()...");
    ps1DebugFlush();

    /* Initialize CD-ROM subsystem - Required for CdSearchFile() to work! */
    /* Despite documentation, calling CdInit() when booting from CD is necessary */
    CdInit();

    ps1DebugPrint("CdInit() returned");
    /* DON'T flush here - graphics might be broken after CdInit()! */

    /* DON'T use printf() - it causes freezes in full engine context */
    /* Use ps1Debug functions instead */

    ps1DebugPrint("cdromInit: COMPLETE");
    /* Still don't flush - let main() do it */
    /* No wait - let execution continue */

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
    /* Don't clear screen - let main() control debug output */
    ps1DebugPrint("cdromOpen: ENTRY");
    ps1DebugPrint("filename: %s", filename ? filename : "NULL");
    ps1DebugFlush();

    int slot = cdromFindFreeSlot();
    if (slot < 0) {
        ps1DebugPrint("ERROR: No free slots");
        ps1DebugFlush();
        ps1DebugWait();
        showCDError(128, 0, 0);  /* DARK RED = No free slots */
    }

    ps1DebugPrint("Got slot: %d", slot);
    ps1DebugFlush();

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

    ps1DebugPrint("Uppercase: %s", upperName);
    ps1DebugFlush();

    /* CD-ROM path - ISO 9660 format with version number */
    /* Correct format: FILENAME.EXT;1 (NO leading backslash!) */
    char cdPath[256];
    snprintf(cdPath, sizeof(cdPath), "%s;1", upperName);

    ps1DebugPrint("CD path: %s", cdPath);
    ps1DebugPrint("Calling CdSearchFile...");
    ps1DebugFlush();

    /* Search for file on CD */
    if (!CdSearchFile(&cdFiles[slot], cdPath)) {
        /* Visual debug: show detailed error */
        ps1DebugPrint("");
        ps1DebugPrint("ERROR: CdSearchFile failed!");
        ps1DebugPrint("");
        ps1DebugPrint("File not on CD or wrong format");
        ps1DebugFlush();
        ps1DebugWait();

        showCDError(255, 0, 128);  /* PINK = CdSearchFile failed */
    }

    ps1DebugPrint("CdSearchFile OK!");
    ps1DebugPrint("File size: %d", cdFiles[slot].size);
    ps1DebugFlush();

    cdFileInUse[slot] = 1;
    cdFilePos[slot] = 0;  /* Start at beginning of file */

    ps1DebugPrint("cdromOpen: SUCCESS - slot %d", slot);
    ps1DebugFlush();
    /* No wait - let execution continue */

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
