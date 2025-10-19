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

/* ============================================================================
 * FILE I/O STUBS
 * PS1 uses CD-ROM for data, not standard FILE* streams.
 * These are stubs that will be replaced with CD-ROM I/O later.
 * ============================================================================ */

FILE *fopen(const char *pathname, const char *mode) {
    /* TODO: Implement CD-ROM file loading */
    printf("STUB: fopen(%s, %s)\n", pathname, mode);
    return NULL;
}

int fclose(FILE *stream) {
    /* No-op for now */
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    /* Stub: return 0 bytes read */
    printf("STUB: fread() called\n");
    return 0;
}

int fseek(FILE *stream, long offset, int whence) {
    /* No-op */
    return 0;
}

long ftell(FILE *stream) {
    /* Return 0 position */
    return 0;
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
