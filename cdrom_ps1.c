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
#include <stdlib.h>
#include <string.h>

#include "mytypes.h"
#include "cdrom_ps1.h"
#include "utils.h"

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
            if (debugMode) {
                printf("CD-ROM: ERROR - Failed to allocate sector buffer\n");
            }
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

    /* Set CD-ROM mode for data reading (2048 byte sectors) */
    uint8 mode = CdlModeSpeed;  /* Normal speed, no XA filter */
    if (CdControlB(CdlSetmode, &mode, NULL) == 0) {
        if (debugMode) {
            printf("CD-ROM: WARNING - Failed to set mode\n");
        }
        /* Don't fail - BIOS might have set it already */
    }

    /* Wait for mode set to complete */
    int timeout = 1000;
    while (CdSync(1, NULL) > 0 && timeout-- > 0);

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
        return -1;
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

    /* Add CD-ROM path prefix if not present */
    char cdPath[256];
    if (upperName[0] != '\\') {
        snprintf(cdPath, sizeof(cdPath), "\\%s", upperName);
    } else {
        snprintf(cdPath, sizeof(cdPath), "%s", upperName);
    }

    /* Search for file on CD */
    if (!CdSearchFile(&cdFiles[slot], cdPath)) {
        if (debugMode) {
            printf("CD-ROM: File not found: %s\n", cdPath);
        }
        return -1;
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
        if (debugMode) {
            printf("CD-ROM: Seek timeout\n");
        }
        return -1;
    }

    /* Read data */
    if (CdRead(sectorsToRead, (uint32*)cdSectorBuffer, CdlModeSpeed) == 0) {
        if (debugMode) {
            printf("CD-ROM: CdRead failed\n");
        }
        return -1;
    }

    /* Wait for read to complete (with timeout) */
    timeout = 10000;
    while (CdSync(1, NULL) > 0 && timeout-- > 0);

    if (timeout <= 0) {
        if (debugMode) {
            printf("CD-ROM: Read timeout\n");
        }
        return -1;
    }

    /* Copy requested bytes from buffer (accounting for offset within first sector) */
    memcpy(buffer, cdSectorBuffer + offsetInSector, size);

    /* Update file position */
    cdFilePos[fileHandle] += size;

    if (debugMode && size > 100) {
        printf("CD-ROM: Read %u bytes from handle %d at pos %u\n",
               size, fileHandle, cdFilePos[fileHandle] - size);
    }

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
