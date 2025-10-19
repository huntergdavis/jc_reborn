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

#ifndef CDROM_PS1_H
#define CDROM_PS1_H

#include "mytypes.h"

/*
 * Initialize CD-ROM subsystem
 * Returns 0 on success, -1 on error
 */
int cdromInit(void);

/*
 * Open a file from CD-ROM
 * Returns file handle ID (0-7) or -1 on error
 */
int cdromOpen(const char *filename);

/*
 * Read data from CD file
 * Returns number of bytes read, or -1 on error
 */
int cdromRead(int fileHandle, void *buffer, uint32 size);

/*
 * Seek to position in CD file
 * whence: SEEK_SET=0, SEEK_CUR=1, SEEK_END=2
 * Returns new position, or -1 on error
 */
int cdromSeek(int fileHandle, int offset, int whence);

/*
 * Get current position in CD file
 * Returns position in bytes, or -1 on error
 */
int cdromTell(int fileHandle);

/*
 * Close CD file
 * Returns 0 on success, -1 on error
 */
int cdromClose(int fileHandle);

/*
 * Get file size
 * Returns size in bytes, or 0 on error
 */
uint32 cdromGetSize(int fileHandle);

#endif /* CDROM_PS1_H */
