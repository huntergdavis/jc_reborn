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
/* Visual debug helper - shows colored screen for 2 seconds */
static void showDebugScreen(int r, int g, int b)
{
    /* Reset GPU to clean state */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);

    /* Create simple draw environment */
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    setRGB0(&draw, r, g, b);
    draw.isbg = 1;  /* Enable background clear */
    PutDrawEnv(&draw);

    /* Enable display */
    SetDispMask(1);

    /* Wait 2 seconds (120 frames at 60fps) */
    for (int i = 0; i < 120; i++) {
        VSync(0);
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
    /* Initialize PS1 subsystems - minimal init here, let subsystems do their own init */
    InitHeap((void*)0x801fff00, 0x00100000);  /* Initialize heap for malloc/printf */

    /* DON'T call CdInit() when booting from CD-ROM! */
    /* The BIOS already initialized it for us, calling CdInit() again crashes! */

    /* VISUAL DEBUG: Since printf doesn't appear in DuckStation TTY,
     * we'll use screen colors to show progress:
     * - RED screen = Reached main()
     * - GREEN screen = Passed graphics init
     * - BLUE screen = Passed resource parsing
     * - PURPLE screen = Starting main loop
     */

    /* Enable debug mode on PS1 to see what's happening */
    debugMode = 1;

    /* VISUAL DEBUG #1: RED screen = We reached main()! */
    showDebugScreen(255, 0, 0);

    /* For PS1, default to playing a single TTM for testing */
    /* This allows visual regression testing against the SDL version */
    if (argc == 1) {
        /* Default test: play SAILING.TTM which is a simple animation */
        argc = 3;
        static char *test_argv[] = {"jcreborn", "ttm", "SAILING"};
        argv = test_argv;
    }
#endif

    parseArgs(argc, argv);

    if (argDump)
        debugMode = 1;

#ifdef PS1_BUILD
    /* Initialize CD-ROM subsystem for PS1 */
    ps1DebugInit();
    ps1DebugClear();
    ps1DebugPrint("main: Before cdromInit()");
    ps1DebugFlush();

    if (cdromInit() < 0) {
        /* If CD-ROM fails, show YELLOW screen and hang */
        ps1DebugPrint("ERROR: cdromInit failed");
        ps1DebugFlush();
        ps1DebugWait();
        showDebugScreen(255, 255, 0);
        while(1);
    }

    ps1DebugPrint("main: cdromInit() completed successfully");
    ps1DebugFlush();

    ps1DebugPrint("main: Build 28: Test MINIMAL file search");
    ps1DebugPrint("main: About to call cdromFirstFunction()");
    ps1DebugFlush();

    /* Build 28: Test with more debug output and safer approach */
    /* Call function and check return value using only visual indicators */
    int result = cdromFirstFunction();

    ps1DebugPrint("main: cdromFirstFunction() returned %d", result);
    ps1DebugFlush();

    /* Test return value - different colors for different file types found */
    if (result == 47) {
        showDebugScreen(0, 255, 255);  /* CYAN = RESOURCE.MAP found! */
    } else if (result == 48) {
        showDebugScreen(0, 255, 0);    /* GREEN = \\RESOURCE.MAP found! */
    } else if (result == 49) {
        showDebugScreen(0, 0, 255);    /* BLUE = resource.map (lowercase) found! */
    } else if (result == 50) {
        showDebugScreen(255, 0, 255);  /* MAGENTA = JCREBORN.EXE found! */
    } else if (result == 51) {
        showDebugScreen(255, 128, 0);  /* ORANGE = SYSTEM.CNF found! */
    } else if (result == 42) {
        showDebugScreen(255, 255, 0);  /* YELLOW = No files found at all */
    } else {
        showDebugScreen(255, 0, 0);    /* RED = Wrong return value */
    }
#endif

    parseResourceFiles("RESOURCE.MAP");

#ifdef PS1_BUILD
    /* VISUAL DEBUG #2.6: MAGENTA screen = parseResourceFiles returned */
    showDebugScreen(255, 0, 255);
#endif

    /* Initialize LRU cache for memory management */
    initLRUCache();

#ifdef PS1_BUILD
    /* VISUAL DEBUG #3: BLUE screen = Resources parsed */
    showDebugScreen(0, 0, 255);
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
        dumpAllResources();
    }

    else if (argBench) {
        graphicsInit();
        adsPlayBench();
        graphicsEnd();
    }

    else if (argTtm) {
#ifdef PS1_BUILD
        /* VISUAL DEBUG #4: PURPLE screen = About to init graphics */
        showDebugScreen(128, 0, 128);
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

