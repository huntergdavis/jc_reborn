/*
 *  This file is part of 'Johnny Reborn'
 *
 *  An open-source engine for the classic
 *  'Johnny Castaway' screensaver by Sierra.
 *
 *  Copyright (C) 2019 Jeremie GUILLAUME
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/* PS1 Build - needs special header handling */
#ifdef PS1_BUILD
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>  /* Provides exit(), atoi(), malloc(), etc. */
#include <string.h>
#define stderr ((FILE*)2)  /* PSn00bSDK doesn't define stderr */
#define fprintf(stream, ...) printf(__VA_ARGS__)  /* Redirect to printf */
/* Declare functions implemented in ps1_stubs.c */
void exit(int status);
int atoi(const char *str);
#else
/* Standard SDL build */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#endif

#include "mytypes.h"
#include "utils.h"
#include "resource.h"
#include "dump.h"

/* Platform-specific headers */
#ifdef PS1_BUILD
#include <psxgpu.h>
#include <psxgte.h>
#include <psxcd.h>
#include "graphics_ps1.h"
#include "events_ps1.h"
#include "sound_ps1.h"
#include "cdrom_ps1.h"
#include "ps1_debug.h"
#else
#include "graphics.h"
#include "events.h"
#include "sound.h"
#endif

#include "ttm.h"
#include "ads.h"
#include "story.h"


static int  argDump     = 0;
static int  argBench    = 0;
static int  argTtm      = 0;
static int  argAds      = 0;
static int  argPlayAll  = 0;
static int  argIsland   = 0;

static char *args[3];
static int  numArgs  = 0;

#ifdef PS1_BUILD
/* Visual debug helper - shows colored screen for ~1 second with busy loop */
static void showDebugScreen(int r, int g, int b)
{
    /* Reset GPU to clean state */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);

    /* Create simple draw environment */
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 320, 240);  /* Use 320x240 for stability */
    setRGB0(&draw, r, g, b);
    draw.isbg = 1;  /* Enable background clear */
    PutDrawEnv(&draw);

    /* Enable display */
    SetDispMask(1);

    /* Use busy loop - VSync can hang in some situations */
    /* ~1 second with busy loop (adjust count as needed) */
    for (volatile int i = 0; i < 5000000; i++) {
        /* Busy wait */
    }
}

#endif

static void usage()
{
        printf("\n");
        printf(" Usage :\n");
        printf("         jc_reborn\n");
        printf("         jc_reborn help\n");
        printf("         jc_reborn version\n");
        printf("         jc_reborn dump\n");
        printf("         jc_reborn [<options>] bench\n");
        printf("         jc_reborn [<options>] ttm <TTM name>\n");
        printf("         jc_reborn [<options>] ads <ADS name> <ADS tag no>\n");
        printf("\n");
        printf(" Available options are:\n");
        printf("         window          - play in windowed mode\n");
        printf("         nosound         - quiet mode\n");
        printf("         island          - display the island as background for ADS play\n");
        printf("         debug           - print some debug info on stdout\n");
        printf("         hotkeys         - enable hot keys\n");
        printf("         capture-frame N - capture frame N to file (for visual testing)\n");
        printf("         capture-output FILE - specify output file for captured frame\n");
        printf("\n");
        printf(" While-playing hot-keys (if enabled):\n");
        printf("         Esc        - Terminate immediately\n");
        printf("         Alt+Return - Toggle full screen / windowed mode\n");
        printf("         Space      - Toggle pause / unpause\n");
        printf("         Return     - When paused, advance one frame\n");
        printf("         <M>        - toggle max / normal speed\n");
        printf("\n");
        exit(1);
}


static void version()
{
        printf("\n");
        printf("    Johnny Reborn, an open-source engine for\n");
        printf("    the classic Johnny Castaway screensaver by Sierra.\n");
        printf("    Development version Copyright (C) 2019 Jeremie GUILLAUME\n");
        printf("\n");
        exit(1);
}


static void parseArgs(int argc, char **argv)
{
    int numExpectedArgs = 0;

    for (int i=1; i < argc; i++) {

        if (numExpectedArgs) {
            args[numArgs++] = argv[i];
            numExpectedArgs--;
        }
        else {
            if (!strcmp(argv[i], "help")) {
                usage();
            }
            if (!strcmp(argv[i], "version")) {
                version();
            }
            else if (!strcmp(argv[i], "dump")) {
                argDump = 1;
            }
            else if (!strcmp(argv[i], "bench")) {
                argBench = 1;
            }
            else if (!strcmp(argv[i], "ttm")) {
                argTtm = 1;
                numExpectedArgs = 1;
            }
            else if (!strcmp(argv[i], "ads")) {
                argAds = 1;
                numExpectedArgs = 2;
            }
            else if (!strcmp(argv[i], "window")) {
                grWindowed = 1;
            }
            else if (!strcmp(argv[i], "nosound")) {
                soundDisabled = 1;
            }
            else if (!strcmp(argv[i], "island")) {
                argIsland = 1;
            }
            else if (!strcmp(argv[i], "debug")) {
                debugMode = 1;
            }
            else if (!strcmp(argv[i], "hotkeys")) {
                evHotKeysEnabled = 1;
            }
            else if (!strcmp(argv[i], "capture-frame")) {
                if (i + 1 < argc) {
                    grCaptureFrameNumber = atoi(argv[++i]);
                    if (grCaptureFrameNumber < 0) {
                        fprintf(stderr, "Error: capture-frame must be >= 0\n");
                        usage();
                    }
                } else {
                    fprintf(stderr, "Error: capture-frame requires a frame number\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "capture-output")) {
                if (i + 1 < argc) {
                    grCaptureFilename = argv[++i];
                } else {
                    fprintf(stderr, "Error: capture-output requires a filename\n");
                    usage();
                }
            }
        }
    }

    if (numExpectedArgs)
        usage();

    if (argDump + argBench + argTtm + argAds > 1)
        usage();

    if (argDump + argBench + argTtm + argAds == 0)
        argPlayAll = 1;
}


int main(int argc, char **argv)
{
#ifdef PS1_BUILD
    /* Initialize debug system FIRST, before any CD operations */
    /* FntLoad must happen before CdInit or it causes hangs */
    ps1DebugInit();

    /* Initialize CD-ROM subsystem */
    if (cdromInit() < 0) {
        ps1DebugError("CD-ROM init failed!");
        while(1);
    }

    debugMode = 1;

    /* Skip parseArgs on PS1 - argc/argv not valid */
    parseResourceFiles("RESOURCE.MAP");
#else
    /* Non-PS1: normal flow */
    parseArgs(argc, argv);

    if (argDump)
        debugMode = 1;

    parseResourceFiles("RESOURCE.MAP");
#endif

#ifdef PS1_BUILD
    /* Get resource counts */
    extern int numScrResources;
    extern int numBmpResources;
    extern int numAdsResources;
    extern int numTtmResources;
    extern int numPalResources;

    /* Show resource loading results */
    ps1DebugClear();
    ps1DebugPrint("=== Resources Loaded ===");
    ps1DebugPrint("");
    ps1DebugPrint("ADS: %d", numAdsResources);
    ps1DebugPrint("BMP: %d", numBmpResources);
    ps1DebugPrint("PAL: %d", numPalResources);
    ps1DebugPrint("SCR: %d", numScrResources);
    ps1DebugPrint("TTM: %d", numTtmResources);
    ps1DebugPrint("");
    ps1DebugPrint("Total: %d",
        numAdsResources + numBmpResources + numPalResources +
        numScrResources + numTtmResources);
    ps1DebugFlush();

    /* Brief pause to see results */
    for (volatile int i = 0; i < 10000000; i++);
#endif

    /* Initialize LRU cache for memory management */
    initLRUCache();

#ifdef PS1_BUILD
    /* Initialize graphics */
    ps1DebugClear();
    ps1DebugPrint("Initializing graphics...");
    ps1DebugFlush();
    for (volatile int i = 0; i < 3000000; i++);

    graphicsInit();

    /* Try to find a decompressed SCR and display it */
    ps1DebugClear();
    ps1DebugPrint("Looking for SCR...");
    ps1DebugFlush();
    for (volatile int i = 0; i < 3000000; i++);

    extern struct TScrResource *scrResources[];
    extern int numScrResources;
    struct TScrResource *testScr = NULL;

    /* Find first SCR with decompressed data */
    for (int i = 0; i < numScrResources; i++) {
        if (scrResources[i] && scrResources[i]->uncompressedData) {
            testScr = scrResources[i];
            break;
        }
    }

    if (testScr) {
        ps1DebugClear();
        ps1DebugPrint("Found SCR: %s", testScr->resName);
        ps1DebugPrint("Size: %dx%d", testScr->width, testScr->height);
        ps1DebugPrint("Data: %lu bytes", (unsigned long)testScr->uncompressedSize);
        ps1DebugFlush();
        for (volatile int i = 0; i < 5000000; i++);

        /* Try to load and display this SCR */
        grLoadScreen(testScr->resName);

        /* Debug: check background state */
        extern PS1Surface *grBackgroundSfc;
        ps1DebugClear();
        ps1DebugPrint("grLoadScreen done!");
        if (grBackgroundSfc) {
            ps1DebugPrint("bgSfc: %dx%d", grBackgroundSfc->width, grBackgroundSfc->height);
            ps1DebugPrint("VRAM: (%d,%d)", grBackgroundSfc->x, grBackgroundSfc->y);
            ps1DebugPrint("pixels: %s", grBackgroundSfc->pixels ? "YES" : "NULL");
        } else {
            ps1DebugPrint("grBackgroundSfc is NULL!");
        }
        ps1DebugFlush();
        for (volatile int i = 0; i < 8000000; i++);
    } else {
        ps1DebugClear();
        ps1DebugPrint("No decompressed SCR found!");
        ps1DebugFlush();
        for (volatile int i = 0; i < 5000000; i++);
    }

    /* Graphics test loop */
    int frameCount = 0;
    while(1) {
        /* Draw background if loaded */
        extern PS1Surface *grBackgroundSfc;
        if (grBackgroundSfc && grBackgroundSfc->pixels) {
            /* Draw background as full-screen sprite */
            grDrawBackground();
        } else if (!testScr) {
            /* Draw colored rectangles if no background */
            grDrawRect(NULL, 20, 20, 80, 60, 1);    /* Red */
            grDrawRect(NULL, 120, 20, 80, 60, 2);   /* Green */
            grDrawRect(NULL, 220, 20, 80, 60, 3);   /* Blue */
            grDrawRect(NULL, 20, 100, 80, 60, 4);   /* Yellow */
            grDrawRect(NULL, 120, 100, 80, 60, 5);  /* Magenta */
            grDrawRect(NULL, 220, 100, 80, 60, 6);  /* Cyan */
        }

        /* Draw border to show screen bounds */
        grDrawLine(NULL, 0, 0, 319, 0, 7);      /* Top */
        grDrawLine(NULL, 0, 239, 319, 239, 7);  /* Bottom */
        grDrawLine(NULL, 0, 0, 0, 239, 7);      /* Left */
        grDrawLine(NULL, 319, 0, 319, 239, 7);  /* Right */

        /* Swap buffers and display */
        grRefreshDisplay();
        frameCount++;
    }

    return 0;
#endif

    if (argPlayAll) {
        printf("Initializing graphics...\n");
        graphicsInit();
        printf("Graphics initialized\n");

        printf("Initializing sound...\n");
        soundInit();
        printf("Sound initialized\n");

        printf("Starting story mode...\n");
        storyPlay();

        printf("Shutting down sound...\n");
        soundEnd();
        printf("Shutting down graphics...\n");
        graphicsEnd();
        printf("Shutdown complete\n");
    }

    else if (argDump) {
#ifdef PS1_BUILD
        /* VISUAL DEBUG: ORANGE screen = argDump path */
        showDebugScreen(255, 165, 0);
#endif
        dumpAllResources();
    }

    else if (argBench) {
#ifdef PS1_BUILD
        /* VISUAL DEBUG: PINK screen = argBench path */
        showDebugScreen(255, 192, 203);
#endif
        graphicsInit();
        adsPlayBench();
        graphicsEnd();
    }

    else if (argTtm) {
#ifdef PS1_BUILD
        /* VISUAL DEBUG: YELLOW screen = Reached TTM section */
        showDebugScreen(255, 255, 0);
#endif
        graphicsInit();

#ifdef PS1_BUILD
        /* PS1: Simple render test - bypass TTM logic for now */
        printf("PS1: Starting simple render test (300 frames)...\n");

        int frame_count = 0;
        while (frame_count < 300) {  /* Run for 5 seconds at 60fps */
            grRefreshDisplay();

            frame_count++;
            if ((frame_count % 60) == 0) {
                printf("Frame %d\n", frame_count);
            }
        }
        printf("PS1: Render test complete\n");
#else
        soundInit();
        adsPlaySingleTtm(args[0]);
        soundEnd();
#endif

        graphicsEnd();
    }

    else if (argAds) {

        graphicsInit();
        soundInit();

        if (argIsland)
            adsInitIsland();
        else
            adsNoIsland();

        adsPlay(args[0], atoi(args[1]));

        soundEnd();
        graphicsEnd();
    }

    return 0;
}

