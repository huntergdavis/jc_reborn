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
 * Build 24: Test function at very start of cdrom_ps1.c
 */
int cdromFirstFunction(void);

/*
 * Initialize CD-ROM subsystem
 * Returns 0 on success, -1 on error
 */
int cdromInit(void);

/*
 * Build 19: Test function to see if we can call functions in cdrom_ps1.c
 */
int cdromTestCall(void);

/*
 * Build 20: Test function placed BEFORE static data
 */
int cdromTestCall2(void);

/*
 * Open a file from CD-ROM
 * Returns file handle ID (0-7) or -1 on error
 */
int cdromOpen2(const char *filename);

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

/*
 * PS1 File I/O wrapper functions for resource loading
 * These provide FILE*-like interface using CD-ROM access
 */

#include <psxcd.h>

typedef struct {
    CdlFILE cdfile;
    long currentPos;
    int isOpen;
    char filename[32];
} PS1File;

PS1File* ps1_fopen(const char* filename, const char* mode);
size_t ps1_fread(void* ptr, size_t size, size_t nmemb, PS1File* file);
int ps1_fseek(PS1File* file, long offset, int whence);
long ps1_ftell(PS1File* file);
int ps1_fclose(PS1File* file);

/* PS1 Resource Loading Test */
void ps1TestResourceLoading(void);

/* PS1-specific utility functions for resource parsing */
uint8 ps1_readUint8(PS1File *f);
uint16 ps1_readUint16(PS1File *f);
uint32 ps1_readUint32(PS1File *f);
char *ps1_getString(PS1File *f, int maxlen);
uint8 *ps1_readUint8Block(PS1File *f, int len);

/* PS1-specific resource parsing functions */
struct TScrResource* ps1_parseScrResource(PS1File *f, const char *resName);

#endif /* CDROM_PS1_H */
