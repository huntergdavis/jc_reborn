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
#ifndef _FILE_DEFINED
#define _FILE_DEFINED
typedef struct _FILE FILE;
#endif
#define stderr ((FILE*)2)  /* PSn00bSDK doesn't define stderr */
#define fprintf(stream, ...) printf(__VA_ARGS__)  /* Redirect to printf */
/* Declare functions implemented in ps1_stubs.c */
void exit(int status);
int atoi(const char *str);
FILE *fopen(const char *pathname, const char *mode);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
int fclose(FILE *stream);
#else
/* Standard SDL build */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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
#include <psxapi.h>
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
#include "island.h"
#include "story.h"

/* Root counters are exposed by PSn00bSDK on PS1 builds. */
#ifdef PS1_BUILD
static void ps1SeedRandom(void)
{
    uint32 seed = 0x9e3779b9u;

    for (int i = 0; i < 32; i++) {
        uint32 t0 = (uint32)GetRCnt(RCntCNT0);
        uint32 t1 = (uint32)GetRCnt(RCntCNT1);
        uint32 t2 = (uint32)GetRCnt(RCntCNT2);
        seed ^= (t0 << (i & 7)) ^ (t1 << ((i + 3) & 7)) ^ (t2 << ((i + 5) & 7));
        seed = (seed << 5) | (seed >> 27);
        seed += 0x7f4a7c15u + (uint32)i;
    }

    if (seed == 0)
        seed = 1;
    srand(seed);
}
#endif


static int  argDump     = 0;
static int  argBench    = 0;
static int  argTtm      = 0;
static int  argAds      = 0;
static int  argPlayAll  = 0;
static int  argIsland   = 0;

static char *args[3];
static int  numArgs  = 0;

#ifndef PS1_BUILD
static int hostForcedSeed = -1;
static int hostForcedIslandPosValid = 0;
static int hostForcedIslandX = 0;
static int hostForcedIslandY = 0;
static int hostForcedLowTide = -1;
#endif

#ifdef PS1_BUILD
#define PS1_BOOT_OVERRIDE_FILE "BOOTMODE.TXT"

static int ps1BootForcedSeed = -1;  /* -1 = use hardware RNG */
static int ps1BootDirectSceneIndex = -1;  /* -1 = not set; >=0 = play scene directly and exit */
static char ps1BootArgStorage[3][32];

static int ps1IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void ps1ResetBootArgs(void)
{
    argDump = 0;
    argBench = 0;
    argTtm = 0;
    argAds = 0;
    argPlayAll = 1;
    argIsland = 0;
    numArgs = 0;

    for (int i = 0; i < 3; i++) {
        args[i] = NULL;
        ps1BootArgStorage[i][0] = '\0';
    }
}

static int ps1CopyBootArg(int index, const char *src)
{
    if (index < 0 || index >= 3 || !src) {
        return 0;
    }

    strncpy(ps1BootArgStorage[index], src, sizeof(ps1BootArgStorage[index]) - 1);
    ps1BootArgStorage[index][sizeof(ps1BootArgStorage[index]) - 1] = '\0';
    args[index] = ps1BootArgStorage[index];
    return 1;
}

static void ps1ApplyBootOverride(char *buffer)
{
    char *tokens[8];
    int tokenCount = 0;
    char *cursor = buffer;
    int tokenBase = 0;

    while (*cursor && tokenCount < 8) {
        while (*cursor && ps1IsSpace(*cursor)) {
            cursor++;
        }

        if (*cursor == '\0' || *cursor == '#') {
            break;
        }

        tokens[tokenCount++] = cursor;

        while (*cursor && !ps1IsSpace(*cursor) && *cursor != '#') {
            cursor++;
        }

        if (*cursor == '#') {
            *cursor = '\0';
            break;
        }

        if (*cursor == '\0') {
            break;
        }

        *cursor = '\0';
        cursor++;
    }

    if (tokenCount == 0) {
        return;
    }

    /* Scan for trailing "seed N" parameter anywhere in the token list */
    for (int i = 0; i + 1 < tokenCount; i++) {
        if (!strcmp(tokens[i], "seed")) {
            ps1BootForcedSeed = atoi(tokens[i + 1]);
            break;
        }
    }

    if (!strcmp(tokens[0], "story")) {
        if (tokenCount >= 3 && !strcmp(tokens[1], "single")) {
            /* "story single N" plays one scene via normal story loop (keeps running) */
            storySetBootSingleSceneIndex(atoi(tokens[2]));
            return;
        }
        if (tokenCount >= 3 && !strcmp(tokens[1], "direct")) {
            /* "story direct N" plays one scene directly and exits when ADS finishes */
            ps1BootDirectSceneIndex = atoi(tokens[2]);
            return;
        }
        if (tokenCount >= 3 &&
            (!strcmp(tokens[1], "scene") || !strcmp(tokens[1], "index"))) {
            storySetBootSceneIndex(atoi(tokens[2]));
            return;
        }
        if (tokenCount >= 4 && !strcmp(tokens[1], "ads")) {
            storySetBootScene(tokens[2], (uint16)atoi(tokens[3]));
            return;
        }
        storySetBootScene(NULL, 0);
        return;
    }

    if (!strcmp(tokens[0], "island")) {
        argIsland = 1;
        tokenBase = 1;
    }

    if (tokenBase >= tokenCount) {
        return;
    }

    if (!strcmp(tokens[tokenBase], "bench")) {
        argBench = 1;
        argPlayAll = 0;
        return;
    }

    if (!strcmp(tokens[tokenBase], "ttm") && (tokenBase + 1) < tokenCount) {
        if (ps1CopyBootArg(0, tokens[tokenBase + 1])) {
            numArgs = 1;
            argTtm = 1;
            argPlayAll = 0;
        }
        return;
    }

    if (!strcmp(tokens[tokenBase], "ads") && (tokenBase + 2) < tokenCount) {
        if (ps1CopyBootArg(0, tokens[tokenBase + 1]) &&
            ps1CopyBootArg(1, tokens[tokenBase + 2])) {
            numArgs = 2;
            argAds = 1;
            argPlayAll = 0;
        }
        return;
    }
}

static void ps1LoadBootOverride(void)
{
    uint32 rawSize = 0;
    uint8 *rawData;
    char buffer[128];
    size_t readCount = 0;

    ps1ResetBootArgs();

    rawData = ps1_loadRawFile("\\BOOTMODE.TXT;1", &rawSize);
    if (rawData != NULL) {
        readCount = (rawSize < (sizeof(buffer) - 1)) ? rawSize : (sizeof(buffer) - 1);
        memcpy(buffer, rawData, readCount);
        free(rawData);
        buffer[readCount] = '\0';
        ps1ApplyBootOverride(buffer);
        return;
    }
}

/* Load and display title screen from raw file on CD */
/* This runs BEFORE resource parsing for instant visual feedback */
static void loadTitleScreenEarly(void)
{
    /* Initialize graphics for 640x480 interlaced */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);

    /* Set up display environment for 640x480 */
    DISPENV disp;
    DRAWENV draw;
    SetDefDispEnv(&disp, 0, 0, 640, 480);
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    disp.isinter = 1;  /* Interlaced mode */
    draw.isbg = 0;     /* Don't clear - we'll load image directly */
    PutDispEnv(&disp);
    PutDrawEnv(&draw);

    /* Enable display */
    SetDispMask(1);

    /* Allocate buffer for full title screen (640x480 x 2 bytes = 614400) */
    int totalBytes = 640 * 480 * 2;  /* 614400 bytes */
    uint8 *screenBuffer = (uint8*)malloc(totalBytes);
    if (!screenBuffer) {
        return;  /* Can't show title, continue anyway */
    }

    /* Load TITLE.RAW using direct CD calls */
    CdlFILE fileInfo;
    if (!CdSearchFile(&fileInfo, "\\TITLE.RAW;1")) {
        free(screenBuffer);
        return;  /* File not found, continue anyway */
    }

    /* Calculate sectors needed (2048 bytes per sector) */
    int totalSectors = (totalBytes + 2047) / 2048;

    /* Seek to file location */
    CdControl(CdlSetloc, (uint8*)&fileInfo.pos, 0);

    /* Read data */
    CdRead(totalSectors, (uint32*)screenBuffer, CdlModeSpeed);
    CdReadSync(0, 0);

    /* Upload to framebuffer in strips (GPU DMA works better with smaller chunks) */
    int stripHeight = 60;
    int numStrips = 480 / stripHeight;  /* 8 strips */

    for (int strip = 0; strip < numStrips; strip++) {
        int yOffset = strip * stripHeight;
        uint8 *stripData = screenBuffer + (yOffset * 640 * 2);

        RECT rect;
        setRECT(&rect, 0, yOffset, 640, stripHeight);
        LoadImage(&rect, (uint32*)stripData);
        DrawSync(0);
    }

    free(screenBuffer);

    /* Show title for 3 seconds */
    for (int i = 0; i < 180; i++) {  /* 60fps * 3 sec = 180 frames */
        VSync(0);
    }

    /* Reset CD state for subsequent resource loading */
    /* This ensures ps1_fopen works correctly after direct CD calls */
    cdromResetState();

    /* Additional delay for CD to settle */
    for (volatile int i = 0; i < 2000000; i++);
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
        printf("         capture-dir DIR - capture a frame sequence into DIR/frame_XXXXX.bmp\n");
        printf("         capture-meta-dir DIR - emit per-frame sprite metadata JSON into DIR\n");
        printf("         capture-range START END - capture inclusive frame range; END=-1 means until exit\n");
        printf("         capture-interval N - capture every Nth frame in the active range\n");
        printf("         capture-overlay - embed a machine-readable debug overlay in captures\n");
        printf("         capture-scene-label TEXT - annotate metadata with the scene label\n");
        printf("         seed N          - force deterministic RNG seed for host runs\n");
        printf("         island-pos X Y  - force island position for host story/island runs\n");
        printf("         lowtide 0|1     - force low tide state for host story/island runs\n");
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
            else if (!strcmp(argv[i], "capture-dir")) {
                if (i + 1 < argc) {
                    grCaptureDir = argv[++i];
                } else {
                    fprintf(stderr, "Error: capture-dir requires a directory\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "capture-meta-dir")) {
                if (i + 1 < argc) {
                    grCaptureMetaDir = argv[++i];
                } else {
                    fprintf(stderr, "Error: capture-meta-dir requires a directory\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "capture-range")) {
                if (i + 2 < argc) {
                    grCaptureStartFrame = atoi(argv[++i]);
                    grCaptureEndFrame = atoi(argv[++i]);
                } else {
                    fprintf(stderr, "Error: capture-range requires START and END\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "capture-interval")) {
                if (i + 1 < argc) {
                    grCaptureInterval = atoi(argv[++i]);
                    if (grCaptureInterval <= 0) {
                        fprintf(stderr, "Error: capture-interval must be > 0\n");
                        usage();
                    }
                } else {
                    fprintf(stderr, "Error: capture-interval requires N\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "capture-overlay")) {
                grCaptureOverlay = 1;
            }
            else if (!strcmp(argv[i], "capture-scene-label")) {
                if (i + 1 < argc) {
                    grCaptureSetSceneLabel(argv[++i]);
                } else {
                    fprintf(stderr, "Error: capture-scene-label requires text\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "seed")) {
                if (i + 1 < argc) {
                    hostForcedSeed = atoi(argv[++i]);
                } else {
                    fprintf(stderr, "Error: seed requires a value\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "island-pos")) {
                if (i + 2 < argc) {
                    hostForcedIslandX = atoi(argv[++i]);
                    hostForcedIslandY = atoi(argv[++i]);
                    hostForcedIslandPosValid = 1;
                } else {
                    fprintf(stderr, "Error: island-pos requires X and Y\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "lowtide")) {
                if (i + 1 < argc) {
                    hostForcedLowTide = atoi(argv[++i]) ? 1 : 0;
                } else {
                    fprintf(stderr, "Error: lowtide requires 0 or 1\n");
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

    debugMode = 0;  /* Disable debug output on PS1 - vprintf crashes */

    /* Show title screen FIRST - instant visual feedback */
    loadTitleScreenEarly();

    /* Parse resource files from CD - needed for background and sprites */
    parseResourceFiles("RESOURCE.MAP");

    /* Load boot override BEFORE seeding RNG so "seed N" can override. */
    ps1LoadBootOverride();

    /* Seed RNG — use forced seed if specified in BOOTMODE, else hardware RNG. */
    if (ps1BootForcedSeed >= 0) {
        srand((unsigned int)ps1BootForcedSeed);
    } else {
        ps1SeedRandom();
    }
#else
    /* Non-PS1: normal flow */
    parseArgs(argc, argv);

    if (argDump)
        debugMode = 1;

    parseResourceFiles("RESOURCE.MAP");

    storySetIslandOverrides(
        hostForcedIslandPosValid,
        hostForcedIslandX,
        hostForcedIslandY,
        hostForcedLowTide >= 0,
        hostForcedLowTide
    );

    if (hostForcedSeed >= 0)
        srand((unsigned int)hostForcedSeed);
    else
        srand((unsigned int)time(NULL));
#endif

#ifdef PS1_BUILD
    /* Resource counts available via extern */
    extern int numPalResources;
    extern struct TPalResource *palResources[];
#endif

    /* Initialize LRU cache for memory management */
    initLRUCache();

#ifdef PS1_BUILD
    grGpuAlreadyInitialized = 1;
    graphicsInit();
    soundInit();

    if (numPalResources > 0 && palResources[0]) {
        grLoadPalette(palResources[0]);
    }

    if (ps1BootDirectSceneIndex >= 0) {
        storyPlayBootSceneDirect(ps1BootDirectSceneIndex);
    }
    else if (argPlayAll) {
        storyPlay();
    }
    else if (argBench) {
        adsPlayBench();
    }
    else if (argTtm && numArgs >= 1) {
        adsPlaySingleTtm(args[0]);
    }
    else if (argAds && numArgs >= 2) {
        adsInit();

#ifdef PS1_BUILD
        ps1_pilotPrearmPackForAds(args[0]);
#endif
        if (argIsland)
            adsInitIsland();
        else
            adsNoIsland();

        adsPlay(args[0], atoi(args[1]));
    }
    else {
        storyPlay();
    }

    soundEnd();
    graphicsEnd();
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
        adsInit();

        if (hostForcedLowTide >= 0)
            islandState.lowTide = hostForcedLowTide;
        if (hostForcedIslandPosValid) {
            islandState.xPos = hostForcedIslandX;
            islandState.yPos = hostForcedIslandY;
        }

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
