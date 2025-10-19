/*
 *  This file is part of 'Johnny Reborn' - PS1 Port
 *
 *  Minimal implementations/stubs for missing libc functions
 *  in the PSn00bSDK freestanding environment.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <stddef.h>
#include <stdarg.h>

/* Forward declarations from PSn00bSDK that ARE available */
extern int printf(const char *format, ...);
extern int vsprintf(char *str, const char *format, __gnuc_va_list arg);

/* Implement vprintf using vsprintf + printf */
int vprintf(const char *format, __gnuc_va_list arg) {
    char buffer[1024];  /* Temporary buffer for formatting */
    int result = vsprintf(buffer, format, arg);
    if (result >= 0) {
        printf("%s", buffer);
    }
    return result;
}

/* FILE type stub - used for function signatures */
typedef struct _FILE FILE;
#define stderr ((FILE*)2)
#define stdout ((FILE*)1)

/* Include CD-ROM interface for real file I/O */
#include "cdrom_ps1.h"

/* ============================================================================
 * FILE I/O IMPLEMENTATION
 * PS1 uses CD-ROM for data, not standard FILE* streams.
 * We map FILE* to CD-ROM file handles:
 * - FILE* values 0-7 correspond to CD-ROM handles 0-7
 * - FILE* values 1 and 2 are reserved for stdout/stderr
 * - FILE* values 3-10 can be used for CD-ROM files
 * ============================================================================ */

FILE *fopen(const char *pathname, const char *mode) {
    /* Open file from CD-ROM */
    int handle = cdromOpen(pathname);
    if (handle < 0) {
        return NULL;
    }
    /* Cast handle to FILE* (handle + 3 to avoid stdout/stderr) */
    return (FILE*)(size_t)(handle + 3);
}

int fclose(FILE *stream) {
    /* Don't close stdout/stderr */
    if (stream == stdout || stream == stderr || stream == NULL) {
        return 0;
    }
    /* Convert FILE* back to CD-ROM handle */
    int handle = (int)(size_t)stream - 3;
    return cdromClose(handle);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    /* Don't read from stdout/stderr */
    if (stream == stdout || stream == stderr || stream == NULL) {
        return 0;
    }
    /* Convert FILE* back to CD-ROM handle */
    int handle = (int)(size_t)stream - 3;
    int bytesRead = cdromRead(handle, ptr, size * nmemb);
    if (bytesRead < 0) {
        return 0;
    }
    return bytesRead / size;  /* Return number of elements read */
}

int fseek(FILE *stream, long offset, int whence) {
    /* Don't seek on stdout/stderr */
    if (stream == stdout || stream == stderr || stream == NULL) {
        return -1;
    }
    /* Convert FILE* back to CD-ROM handle */
    int handle = (int)(size_t)stream - 3;
    int result = cdromSeek(handle, offset, whence);
    return (result < 0) ? -1 : 0;
}

long ftell(FILE *stream) {
    /* Return 0 for stdout/stderr */
    if (stream == stdout || stream == stderr || stream == NULL) {
        return 0;
    }
    /* Convert FILE* back to CD-ROM handle */
    int handle = (int)(size_t)stream - 3;
    return cdromTell(handle);
}

int fgetc(FILE *f) {
    /* Return EOF */
    return -1;
}

int fflush(FILE *stream) {
    /* No-op */
    return 0;
}

int fputs(const char *s, FILE *stream) {
    /* Redirect stderr/stdout to printf */
    if (stream == stderr || stream == stdout) {
        printf("%s", s);
        return 0;
    }
    return -1;
}

/* fprintf and vfprintf - redirect to printf for stderr/stdout */
int fprintf(FILE *stream, const char *format, ...) {
    if (stream == stderr || stream == stdout) {
        va_list args;
        va_start(args, format);
        int result = vprintf(format, args);
        va_end(args);
        return result;
    }
    /* For other streams, just return success */
    return 0;
}

int vfprintf(FILE *stream, const char *format, __gnuc_va_list args) {
    if (stream == stderr || stream == stdout) {
        return vprintf(format, args);
    }
    return 0;
}

char *fgets(char *s, int size, FILE *stream) {
    /* Stub: return NULL (EOF) */
    return NULL;
}

int feof(FILE *stream) {
    /* Stub: always EOF */
    return 1;
}

/* ============================================================================
 * PROGRAM CONTROL
 * ============================================================================ */

void exit(int status) {
    /* On PS1, halt with an infinite loop */
    printf("EXIT called with status %d - halting\n", status);
    while(1) {
        /* Infinite loop - could also return to BIOS */
    }
}

/* ============================================================================
 * STRING CONVERSION
 * ============================================================================ */

int atoi(const char *str) {
    int result = 0;
    int sign = 1;

    if (str == NULL) return 0;

    /* Skip whitespace */
    while (*str == ' ' || *str == '\t' || *str == '\n') str++;

    /* Handle sign */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    /* Convert digits */
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result * sign;
}

/* ============================================================================
 * ENVIRONMENT
 * ============================================================================ */

char *getenv(const char *name) {
    /* No environment variables on PS1 */
    return NULL;
}

/* ============================================================================
 * STRING FORMATTING
 * sprintf and snprintf are provided by PSn00bSDK libc
 * ============================================================================ */

/* ============================================================================
 * FILESYSTEM (for dump.c - debug feature not needed on PS1)
 * ============================================================================ */

struct stat {
    int st_mode;
};

int stat(const char *pathname, struct stat *statbuf) {
    /* Always fail - no filesystem access */
    return -1;
}

int mkdir(const char *pathname, int mode) {
    /* Always fail - no directory creation */
    return -1;
}

/* ============================================================================
 * SDL COMPATIBILITY (for bench.c - SDL_GetTicks)
 * ============================================================================ */

unsigned int SDL_GetTicks(void) {
    /* Return a simple tick counter
     * On PS1, we could use VSync counter or timer */
    static unsigned int ticks = 0;
    ticks += 16;  /* Approximate 60Hz frame time in milliseconds */
    return ticks;
}
