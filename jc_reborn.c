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
#include "pause_menu.h"
#else
#include "graphics.h"
#include "events.h"
#include "sound.h"
#endif

#include "ttm.h"
#include "ads.h"
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
static int ps1BootForcedSeed = -1;
static int ps1BootStoryDirectSceneIndex = -1;
static int ps1BootHoldDisplayFrames = 0;

static void extendHoldForCaptureIfNeeded(void)
{
#ifndef PS1_BUILD
    int requiredFrame = -1;

    if (grCaptureFrameNumber >= 0)
        requiredFrame = grCaptureFrameNumber;
    if (grCaptureFrameEnd >= 0 && grCaptureFrameEnd > requiredFrame)
        requiredFrame = grCaptureFrameEnd;

    if (requiredFrame >= 0 && ps1BootHoldDisplayFrames > 0 &&
        ps1BootHoldDisplayFrames <= requiredFrame) {
        ps1BootHoldDisplayFrames = requiredFrame + 1;
    }
#endif
}

static void applyForcedSeedIfNeeded(void)
{
    storySetBootForcedSeedHint(ps1BootForcedSeed);
    if (ps1BootForcedSeed >= 0)
        srand((unsigned int)ps1BootForcedSeed);
}

#ifdef PS1_BUILD
#define PS1_BOOT_OVERRIDE_FILE "BOOTMODE.TXT"

static char ps1BootArgStorage[3][32];
static char ps1BootOverrideBuffer[128];
static char ps1BootOverrideSaved[128];
static uint8 ps1BootOverrideSector[2048];

static int ps1IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int ps1HasBootOverridePending(void)
{
    /* Story overrides should still follow the normal user-facing boot path
     * (title/introduction/ocean) instead of short-circuiting into an
     * exact-scene validation boot. Only low-level runtime overrides should
     * suppress the early title screen. */
    return argBench || argTtm || argAds || numArgs > 0;
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
    ps1BootStoryDirectSceneIndex = -1;
    ps1BootHoldDisplayFrames = 0;
    ps1BootForcedSeed = -1;
    storySetBootForcedSeedHint(-1);
    storySetBootScene(NULL, 0);
    storyClearBootIslandPosition();
    storyClearBootLowTide();
    storyClearBootDay();
    storyClearBootRaft();

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
    char *tokens[16];
    int tokenCount = 0;
    char *cursor = buffer;
    int tokenBase = 0;

    while (*cursor && tokenCount < 16) {
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

    for (;;) {
        int consumed = 0;

        if (tokenCount >= 3 && !strcmp(tokens[tokenCount - 3], "island-pos")) {
            storySetBootIslandPosition(atoi(tokens[tokenCount - 2]),
                                       atoi(tokens[tokenCount - 1]));
            tokenCount -= 3;
            consumed = 1;
        }
        else if (tokenCount >= 2 && !strcmp(tokens[tokenCount - 2], "lowtide")) {
            storySetBootLowTide(atoi(tokens[tokenCount - 1]));
            tokenCount -= 2;
            consumed = 1;
        }
        else if (tokenCount >= 2 && !strcmp(tokens[tokenCount - 2], "day")) {
            storySetBootDay(atoi(tokens[tokenCount - 1]));
            tokenCount -= 2;
            consumed = 1;
        }
        else if (tokenCount >= 2 && !strcmp(tokens[tokenCount - 2], "raft")) {
            storySetBootRaft(atoi(tokens[tokenCount - 1]));
            tokenCount -= 2;
            consumed = 1;
        }
        else if (tokenCount >= 2 && !strcmp(tokens[tokenCount - 2], "night")) {
            storySetBootNight(atoi(tokens[tokenCount - 1]));
            tokenCount -= 2;
            consumed = 1;
        }
        else if (tokenCount >= 2 && !strcmp(tokens[tokenCount - 2], "holiday")) {
            storySetBootHoliday(atoi(tokens[tokenCount - 1]));
            tokenCount -= 2;
            consumed = 1;
        }
        else if (tokenCount >= 2 && !strcmp(tokens[tokenCount - 2], "seed")) {
            ps1BootForcedSeed = atoi(tokens[tokenCount - 1]);
            tokenCount -= 2;
            consumed = 1;
        }

        if (!consumed)
            break;
        if (tokenCount == 0)
            return;
    }

#ifdef PS1_BUILD
    printf("[BOOT] override='%s'\n", buffer);
    if (ps1BootForcedSeed >= 0)
        printf("[BOOT] forced seed=%d\n", ps1BootForcedSeed);
#endif

    if (!strcmp(tokens[0], "story")) {
        if (tokenCount >= 3 && !strcmp(tokens[1], "direct")) {
#ifdef PS1_BUILD
            printf("[BOOT] story direct %d\n", atoi(tokens[2]));
#endif
            ps1BootStoryDirectSceneIndex = atoi(tokens[2]);
            ps1BootHoldDisplayFrames = 600;
            argPlayAll = 0;
            return;
        }
        if (tokenCount >= 3 &&
            (!strcmp(tokens[1], "hold") || !strcmp(tokens[1], "singlehold"))) {
#ifdef PS1_BUILD
            printf("[BOOT] story hold %d\n", atoi(tokens[2]));
#endif
            storySetBootHoldSceneIndex(atoi(tokens[2]));
            ps1BootHoldDisplayFrames = 600;
            return;
        }
        if (tokenCount >= 3 && !strcmp(tokens[1], "single")) {
#ifdef PS1_BUILD
            printf("[BOOT] story single %d\n", atoi(tokens[2]));
#endif
            storySetBootSingleSceneIndex(atoi(tokens[2]));
            return;
        }
        if (tokenCount >= 3 &&
            (!strcmp(tokens[1], "scene") || !strcmp(tokens[1], "index"))) {
#ifdef PS1_BUILD
            printf("[BOOT] story scene %d\n", atoi(tokens[2]));
#endif
            storySetBootSceneIndex(atoi(tokens[2]));
            return;
        }
        if (tokenCount >= 4 && !strcmp(tokens[1], "ads")) {
#ifdef PS1_BUILD
            printf("[BOOT] story ads %s %d\n", tokens[2], atoi(tokens[3]));
#endif
            storySetBootScene(tokens[2], (uint16)atoi(tokens[3]));
            return;
        }
        storySetBootScene(NULL, 0);
        return;
    }

    while (tokenBase < tokenCount &&
           (!strcmp(tokens[tokenBase], "window") ||
            !strcmp(tokens[tokenBase], "nosound"))) {
        tokenBase++;
    }

    if (tokenBase < tokenCount && !strcmp(tokens[tokenBase], "story")) {
        if ((tokenBase + 2) < tokenCount && !strcmp(tokens[tokenBase + 1], "direct")) {
#ifdef PS1_BUILD
            printf("[BOOT] story direct %d\n", atoi(tokens[tokenBase + 2]));
#endif
            ps1BootStoryDirectSceneIndex = atoi(tokens[tokenBase + 2]);
            ps1BootHoldDisplayFrames = 600;
            argPlayAll = 0;
            return;
        }
        if ((tokenBase + 2) < tokenCount &&
            (!strcmp(tokens[tokenBase + 1], "hold") ||
             !strcmp(tokens[tokenBase + 1], "singlehold"))) {
#ifdef PS1_BUILD
            printf("[BOOT] story hold %d\n", atoi(tokens[tokenBase + 2]));
#endif
            storySetBootHoldSceneIndex(atoi(tokens[tokenBase + 2]));
            ps1BootHoldDisplayFrames = 600;
            return;
        }
        if ((tokenBase + 2) < tokenCount && !strcmp(tokens[tokenBase + 1], "single")) {
#ifdef PS1_BUILD
            printf("[BOOT] story single %d\n", atoi(tokens[tokenBase + 2]));
#endif
            storySetBootSingleSceneIndex(atoi(tokens[tokenBase + 2]));
            return;
        }
        if ((tokenBase + 2) < tokenCount &&
            (!strcmp(tokens[tokenBase + 1], "scene") ||
             !strcmp(tokens[tokenBase + 1], "index"))) {
#ifdef PS1_BUILD
            printf("[BOOT] story scene %d\n", atoi(tokens[tokenBase + 2]));
#endif
            storySetBootSceneIndex(atoi(tokens[tokenBase + 2]));
            return;
        }
        if ((tokenBase + 3) < tokenCount && !strcmp(tokens[tokenBase + 1], "ads")) {
#ifdef PS1_BUILD
            printf("[BOOT] story ads %s %d\n",
                   tokens[tokenBase + 2], atoi(tokens[tokenBase + 3]));
#endif
            storySetBootScene(tokens[tokenBase + 2],
                              (uint16)atoi(tokens[tokenBase + 3]));
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
    static int ps1BootOverrideLoadCount = 0;
    uint32 rawSize = 0;
    size_t readCount = 0;
    CdlFILE fileInfo;

    ps1BootOverrideLoadCount++;

    ps1ResetBootArgs();
    memset(ps1BootOverrideBuffer, 0, sizeof(ps1BootOverrideBuffer));
    memset(ps1BootOverrideSector, 0, sizeof(ps1BootOverrideSector));

    if (!CdSearchFile(&fileInfo, "\\BOOTMODE.TXT;1")) {
        return;
    }

    rawSize = fileInfo.size;
    if (rawSize == 0) {
        return;
    }

    CdControl(CdlSetloc, (uint8 *)&fileInfo.pos, NULL);
    if (CdRead(1, (uint32 *)ps1BootOverrideSector, CdlModeSpeed) == 0) {
        return;
    }

    if (CdReadSync(0, 0) < 0) {
        return;
    }

    readCount = (rawSize < (sizeof(ps1BootOverrideBuffer) - 1))
              ? rawSize
              : (sizeof(ps1BootOverrideBuffer) - 1);
    memcpy(ps1BootOverrideBuffer, ps1BootOverrideSector, readCount);
    ps1BootOverrideBuffer[readCount] = '\0';
    memcpy(ps1BootOverrideSaved, ps1BootOverrideBuffer, sizeof(ps1BootOverrideSaved));
    ps1ApplyBootOverride(ps1BootOverrideBuffer);
}

static void ps1ReapplyBootOverride(void)
{
    if (ps1BootOverrideSaved[0] == '\0')
        return;

    ps1ResetBootArgs();
    ps1ApplyBootOverride(ps1BootOverrideSaved);
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
    int stripBytes = 640 * stripHeight * 2;
    uint8 *stripBuffer = (uint8*)malloc(stripBytes);

    if (!stripBuffer) {
        free(stripBuffer);
        free(screenBuffer);
        return;
    }

    for (int strip = 0; strip < numStrips; strip++) {
        int yOffset = strip * stripHeight;
        uint8 *stripData = screenBuffer + (yOffset * 640 * 2);

        memcpy(stripBuffer, stripData, stripBytes);

        RECT rect;
        setRECT(&rect, 0, yOffset, 640, stripHeight);
        LoadImage(&rect, (uint32*)stripBuffer);
        DrawSync(0);
    }

    free(stripBuffer);
    free(screenBuffer);

    VSync(0);

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

#ifndef PS1_BUILD
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
        printf("         jc_reborn [<options>] story single <scene-index>\n");
        printf("         jc_reborn [<options>] story hold <scene-index>\n");
        printf("         jc_reborn [<options>] story direct <scene-index>\n");
        printf("\n");
        printf(" Available options are:\n");
        printf("         window          - play in windowed mode\n");
        printf("         nosound         - quiet mode\n");
        printf("         island          - display the island as background for ADS play\n");
        printf("         debug           - print some debug info on stdout\n");
        printf("         hotkeys         - enable hot keys\n");
        printf("         seed N          - force deterministic RNG seed\n");
        printf("         island-pos X Y  - force deterministic island position\n");
        printf("         lowtide 0|1     - force deterministic low tide state\n");
        printf("         day N           - force story day (1..11)\n");
        printf("         raft N          - force raft build stage\n");
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
            else if (!strcmp(argv[i], "story")) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: story requires a subcommand\n");
                    usage();
                }
                if (!strcmp(argv[i + 1], "single")) {
                    if (i + 2 >= argc) {
                        fprintf(stderr, "Error: story single requires a scene index\n");
                        usage();
                    }
                    storySetBootSingleSceneIndex(atoi(argv[i + 2]));
                    argPlayAll = 1;
                    i += 2;
                }
                else if (!strcmp(argv[i + 1], "hold")) {
                    if (i + 2 >= argc) {
                        fprintf(stderr, "Error: story hold requires a scene index\n");
                        usage();
                    }
                    storySetBootHoldSceneIndex(atoi(argv[i + 2]));
                    ps1BootHoldDisplayFrames = 600;
                    argPlayAll = 1;
                    i += 2;
                }
                else if (!strcmp(argv[i + 1], "direct")) {
                    if (i + 2 >= argc) {
                        fprintf(stderr, "Error: story direct requires a scene index\n");
                        usage();
                    }
                    ps1BootStoryDirectSceneIndex = atoi(argv[i + 2]);
                    ps1BootHoldDisplayFrames = 600;
                    argPlayAll = 0;
                    i += 2;
                }
                else {
                    fprintf(stderr, "Error: unknown story subcommand '%s'\n", argv[i + 1]);
                    usage();
                }
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
            else if (!strcmp(argv[i], "seed")) {
                if (i + 1 < argc) {
                    ps1BootForcedSeed = atoi(argv[++i]);
                } else {
                    fprintf(stderr, "Error: seed requires a value\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "island-pos")) {
                if (i + 2 < argc) {
                    int xPos = atoi(argv[++i]);
                    int yPos = atoi(argv[++i]);
                    storySetBootIslandPosition(xPos, yPos);
                } else {
                    fprintf(stderr, "Error: island-pos requires X and Y values\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "lowtide")) {
                if (i + 1 < argc) {
                    storySetBootLowTide(atoi(argv[++i]));
                } else {
                    fprintf(stderr, "Error: lowtide requires 0 or 1\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "day")) {
                if (i + 1 < argc) {
                    storySetBootDay(atoi(argv[++i]));
                } else {
                    fprintf(stderr, "Error: day requires a value\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "raft")) {
                if (i + 1 < argc) {
                    storySetBootRaft(atoi(argv[++i]));
                } else {
                    fprintf(stderr, "Error: raft requires a value\n");
                    usage();
                }
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
            else if (!strcmp(argv[i], "capture-range")) {
                if (i + 2 < argc) {
                    grCaptureFrameStart = atoi(argv[++i]);
                    grCaptureFrameEnd = atoi(argv[++i]);
                    if (grCaptureFrameStart < 0 ||
                        (grCaptureFrameEnd >= 0 && grCaptureFrameEnd < grCaptureFrameStart)) {
                        fprintf(stderr, "Error: capture-range requires 0 <= start and (end == -1 or start <= end)\n");
                        usage();
                    }
                } else {
                    fprintf(stderr, "Error: capture-range requires START END\n");
                    usage();
                }
            }
            else if (!strcmp(argv[i], "capture-interval")) {
                if (i + 1 < argc) {
                    grCaptureFrameInterval = atoi(argv[++i]);
                    if (grCaptureFrameInterval <= 0) {
                        fprintf(stderr, "Error: capture-interval must be > 0\n");
                        usage();
                    }
                } else {
                    fprintf(stderr, "Error: capture-interval requires a value\n");
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

    extendHoldForCaptureIfNeeded();
}
#endif


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

    /* Read the optional on-disc boot override before startup continues, but
     * keep the user-visible title path intact. Headless validation needs to
     * match the real Flatpak/manual startup sequence, not short-circuit it. */
    ps1LoadBootOverride();

    /* User-facing boots should keep the title path. Exact-scene direct boots
     * are a validation/debug route and should bypass title/ocean handoff
     * contamination so we can verify scene rendering itself. */
    if (ps1BootStoryDirectSceneIndex < 0 && !ps1HasBootOverridePending()) {
        loadTitleScreenEarly();
        /* The title-screen upload path can clobber the parsed boot globals.
         * Reapply the originally parsed override from RAM after the title
         * finishes so dispatch uses the requested route. */
        ps1ReapplyBootOverride();
    }

    /* Parse resource files from CD - needed for background and sprites */
    parseResourceFiles("RESOURCE.MAP");

    /* Seed RNG after CD/resource setup to avoid deterministic startup scenes. */
    ps1SeedRandom();
    storySetBootForcedSeedHint(ps1BootForcedSeed);
    if (ps1BootForcedSeed >= 0)
        srand((unsigned int)ps1BootForcedSeed);

#else
    /* Non-PS1: normal flow */
    parseArgs(argc, argv);

    if (argDump)
        debugMode = 1;

    parseResourceFiles("RESOURCE.MAP");
#endif

#ifdef PS1_BUILD
    /* Resource counts available via extern */
    extern int numPalResources;
    extern struct TPalResource *palResources[];
#endif

    /* Initialize LRU cache for memory management */
    initLRUCache();

#ifdef PS1_BUILD
    grGpuAlreadyInitialized = 0;
    graphicsInit();
    soundInit();
    pauseMenuInit();

    if (numPalResources > 0 && palResources[0]) {
        grLoadPalette(palResources[0]);
    }

    if (ps1BootStoryDirectSceneIndex >= 0) {
        storyPlayBootSceneDirect(ps1BootStoryDirectSceneIndex);
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

    if (ps1BootHoldDisplayFrames > 0) {
        for (int i = 0; i < ps1BootHoldDisplayFrames; i++) {
            VSync(0);
        }
    }

    soundEnd();
    graphicsEnd();
    return 0;
#endif

    if (ps1BootStoryDirectSceneIndex >= 0) {
        printf("Initializing graphics...\n");
        graphicsInit();
        printf("Graphics initialized\n");

        printf("Initializing sound...\n");
        soundInit();
        printf("Sound initialized\n");
        applyForcedSeedIfNeeded();

        printf("Starting direct story scene...\n");
        storyPlayBootSceneDirect(ps1BootStoryDirectSceneIndex);
        if (ps1BootHoldDisplayFrames > 0) {
            for (int i = 0; i < ps1BootHoldDisplayFrames; i++)
                grRefreshDisplay();
        }

        printf("Shutting down sound...\n");
        soundEnd();
        printf("Shutting down graphics...\n");
        graphicsEnd();
        printf("Shutdown complete\n");
    }

    else if (argPlayAll) {
        printf("Initializing graphics...\n");
        graphicsInit();
        printf("Graphics initialized\n");

        printf("Initializing sound...\n");
        soundInit();
        printf("Sound initialized\n");
        applyForcedSeedIfNeeded();

        printf("Starting story mode...\n");
        storyPlay();
        if (ps1BootHoldDisplayFrames > 0) {
            for (int i = 0; i < ps1BootHoldDisplayFrames; i++)
                grRefreshDisplay();
        }

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
        applyForcedSeedIfNeeded();
        adsPlayBench();
        graphicsEnd();
    }

    else if (argTtm) {
        graphicsInit();
        applyForcedSeedIfNeeded();

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
        applyForcedSeedIfNeeded();
        adsInit();

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
