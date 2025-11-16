/*
 *  This file is part of 'Johnny Reborn' - PS1 Port
 *
 *  Visual debugging system for PS1
 *  Displays text on-screen since printf() doesn't work in DuckStation
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <psxapi.h>
#include <psxpad.h>

#include "ps1_debug.h"

/* Font stream ID */
static int fontID = -1;

/* Screen dimensions - 640x480 interlaced high res mode */
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

/* Text buffer for accumulating debug messages */
#define MAX_DEBUG_LINES 20
#define MAX_LINE_LENGTH 80
static char debugLines[MAX_DEBUG_LINES][MAX_LINE_LENGTH];
static int numDebugLines = 0;

/* Background color for debug screen - CHANGE THIS EACH BUILD! */
/* Build 24: Light green */
static int bgR = 220, bgG = 255, bgB = 220;  /* Light green */

/*
 * Initialize visual debugging system
 */
void ps1DebugInit(void)
{
    /* Reset GPU and set video mode (do this ONCE at startup) */
    /* Use 640x480 interlaced high res mode */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);

    /* Load built-in PSX BIOS font (8x8 characters) */
    FntLoad(960, 0);

    /* Create font stream at top-left of screen */
    /* FntOpen(x, y, width, height, clear_background, max_chars) */
    fontID = FntOpen(10, 10, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 20, 0, 512);

    /* Enable display */
    SetDispMask(1);

    /* Clear debug buffer */
    ps1DebugClear();
}

/*
 * Clear the debug text buffer
 */
void ps1DebugClear(void)
{
    numDebugLines = 0;
    for (int i = 0; i < MAX_DEBUG_LINES; i++) {
        debugLines[i][0] = '\0';
    }
}

/*
 * Print a line of text to the debug display
 */
void ps1DebugPrint(const char *fmt, ...)
{
    if (numDebugLines >= MAX_DEBUG_LINES) {
        return;  /* Buffer full */
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(debugLines[numDebugLines], MAX_LINE_LENGTH, fmt, args);
    va_end(args);

    numDebugLines++;
}

/*
 * Update the screen with current debug text
 */
void ps1DebugFlush(void)
{
    /* Set up draw environment with background color */
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    setRGB0(&draw, bgR, bgG, bgB);
    draw.isbg = 1;  /* Enable background clear */
    PutDrawEnv(&draw);

    /* Print all accumulated debug lines */
    for (int i = 0; i < numDebugLines; i++) {
        FntPrint(fontID, "%s\n", debugLines[i]);
    }

    /* Flush font buffer to VRAM */
    FntFlush(fontID);

    /* DON'T use DrawSync or VSync - they can hang after CdInit()! */
    /* The GPU will render when it's ready */
}

/*
 * Wait for user input (SELECT button) before continuing
 */
void ps1DebugWait(void)
{
    /* Show "Press SELECT to continue" message */
    FntPrint(fontID, "\nPress SELECT to continue...\n");
    FntFlush(fontID);

    /* Initialize pad */
    uint8_t pad_buff[2][34];
    InitPAD(pad_buff[0], 34, pad_buff[1], 34);
    StartPAD();
    ChangeClearPAD(0);

    /* Wait for SELECT button press */
    int waiting = 1;
    while (waiting) {
        PADTYPE *pad = (PADTYPE*)pad_buff[0];

        if (pad->stat == 0) {
            /* Pad connected */
            if ((pad->type == 0x4) || (pad->type == 0x5) || (pad->type == 0x7)) {
                /* Digital pad, DualShock, or Analog */
                if (!(pad->btn & PAD_SELECT)) {
                    /* SELECT button pressed */
                    waiting = 0;
                }
            }
        }

        VSync(0);  /* Wait for next frame */
    }

    /* Wait for button release */
    while (1) {
        PADTYPE *pad = (PADTYPE*)pad_buff[0];

        if (pad->stat == 0) {
            if ((pad->type == 0x4) || (pad->type == 0x5) || (pad->type == 0x7)) {
                if (pad->btn & PAD_SELECT) {
                    /* Button released */
                    break;
                }
            }
        }

        VSync(0);
    }

    StopPAD();
}

/*
 * Show an error screen with text and wait
 */
void ps1DebugError(const char *fmt, ...)
{
    /* Set error background color (dark red) */
    bgR = 64;
    bgG = 0;
    bgB = 0;

    /* Clear and add error header */
    ps1DebugClear();
    ps1DebugPrint("=== ERROR ===");
    ps1DebugPrint("");

    /* Add error message */
    char errorMsg[MAX_LINE_LENGTH];
    va_list args;
    va_start(args, fmt);
    vsnprintf(errorMsg, MAX_LINE_LENGTH, fmt, args);
    va_end(args);

    ps1DebugPrint("%s", errorMsg);

    /* Flush and wait */
    ps1DebugFlush();
    ps1DebugWait();

    /* Reset background color */
    bgR = 0;
    bgG = 0;
    bgB = 64;
}
