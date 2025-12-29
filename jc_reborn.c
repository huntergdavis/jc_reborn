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

    /* Show title screen FIRST - instant visual feedback */
    loadTitleScreenEarly();

    /* Parse resource files from CD - needed for background and sprites */
    parseResourceFiles("RESOURCE.MAP");
#else
    /* Non-PS1: normal flow */
    parseArgs(argc, argv);

    if (argDump)
        debugMode = 1;

    parseResourceFiles("RESOURCE.MAP");
#endif

#ifdef PS1_BUILD
    /* Resource counts available via extern */
    extern int numScrResources;
    extern int numBmpResources;
    extern int numAdsResources;
    extern int numTtmResources;
    extern int numPalResources;
    extern struct TScrResource *scrResources[];
    extern struct TBmpResource *bmpResources[];
    extern struct TPalResource *palResources[];
    extern int fontID;  /* For FntPrint debug display */
#endif

    /* Initialize LRU cache for memory management */
    initLRUCache();

#ifdef PS1_BUILD
    /* === FRESH GPU RESET (proven working pattern) === */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    InitGeom();

    /* Minimal OT and primitive buffer */
    #define GAME_OTLEN 8
    #define GAME_PRIMBUF 8192
    static unsigned long gameOT[GAME_OTLEN];
    static char gamePrimBuf[GAME_PRIMBUF];
    char *gameNextPri;

    /* Simple display/draw environments */
    DISPENV gameDisp;
    DRAWENV gameDraw;
    SetDefDispEnv(&gameDisp, 0, 0, 640, 480);
    SetDefDrawEnv(&gameDraw, 0, 0, 640, 480);
    gameDisp.isinter = 1;  /* Interlaced for 640x480 */
    /* isbg=0: Don't clear - grDrawBackground will repaint each frame */
    setRGB0(&gameDraw, 0, 0, 0);
    gameDraw.isbg = 0;

    /* Enable display */
    SetDispMask(1);
    PutDispEnv(&gameDisp);
    PutDrawEnv(&gameDraw);

    /* DEBUG: Flash RED immediately after GPU reset - if this shows, GPU reset works */
    {
        TILE flashTile;
        setTile(&flashTile);
        setXY0(&flashTile, 0, 0);
        setWH(&flashTile, 640, 480);
        setRGB0(&flashTile, 200, 0, 0);
        ClearOTagR(gameOT, GAME_OTLEN);
        addPrim(&gameOT[0], &flashTile);
        DrawOTag(&gameOT[GAME_OTLEN-1]);
        DrawSync(0);
        VSync(0);
        VSync(0);
    }

    /* RE-ENABLE palette and background loading */
    if (numPalResources > 0 && palResources[0]) {
        grLoadPalette(palResources[0]);
    }
    struct TScrResource *bgScr = NULL;
    for (int i = 0; i < numScrResources; i++) {
        if (scrResources[i] && scrResources[i]->uncompressedData) {
            bgScr = scrResources[i];
            break;
        }
    }
    if (bgScr) {
        /* DEBUG: Flash PURPLE before grLoadScreen */
        {
            TILE flashTile;
            setTile(&flashTile);
            setXY0(&flashTile, 0, 0);
            setWH(&flashTile, 640, 480);
            setRGB0(&flashTile, 200, 0, 200);
            ClearOTagR(gameOT, GAME_OTLEN);
            addPrim(&gameOT[0], &flashTile);
            DrawOTag(&gameOT[GAME_OTLEN-1]);
            DrawSync(0);
            VSync(0);
            VSync(0);
        }
        grLoadScreen(bgScr->resName);
    }

    /* Create a simple TTtmSlot to hold sprites */
    static struct TTtmSlot gameTtmSlot;
    memset(&gameTtmSlot, 0, sizeof(gameTtmSlot));

    /* Load first available BMP resource into slot 0 */
    PS1Surface *loadedSprite = NULL;
    int spriteCount = 0;

    /* DEBUG: Track BMP resource info */
    extern int ps1_loadBmpDataProgress;  /* From cdrom_ps1.c */
    struct TBmpResource *testBmp = findBmpResource("JOHNWALK.BMP");

    /* DEBUG: Capture numImages at find time */
    static int testBmpNumImagesAtFind = -1;
    static int testBmpUncompSizeAtFind = -1;
    static int testBmpNumImagesAfterLoad = -1;
    if (testBmp) {
        testBmpNumImagesAtFind = testBmp->numImages;
        testBmpUncompSizeAtFind = testBmp->uncompressedSize;
    }

    /* JOHNWALK should now be decompressed at startup - TEST: just skip grLoadBmp to verify startup works */
    if (testBmp && testBmp->uncompressedData) {
        /* Flash CYAN - testBmp exists and has data */
        {
            TILE flashTile;
            setTile(&flashTile);
            setXY0(&flashTile, 0, 0);
            setWH(&flashTile, 640, 480);
            setRGB0(&flashTile, 0, 200, 200);  /* Cyan */
            ClearOTagR(gameOT, GAME_OTLEN);
            addPrim(&gameOT[0], &flashTile);
            DrawOTag(&gameOT[GAME_OTLEN-1]);
            DrawSync(0);
            VSync(0);
            VSync(0);
        }

        /* Re-enabled grLoadBmp - crashes are inside it */
        grLoadBmp(&gameTtmSlot, 0, "JOHNWALK.BMP");
        spriteCount = gameTtmSlot.numSprites[0];
        if (spriteCount > 0) {
            loadedSprite = gameTtmSlot.sprites[0][0];
        }

        /* Flash BLUE - proceeding to game loop */
        {
            TILE flashTile;
            setTile(&flashTile);
            setXY0(&flashTile, 0, 0);
            setWH(&flashTile, 640, 480);
            setRGB0(&flashTile, 0, 0, 200);
            ClearOTagR(gameOT, GAME_OTLEN);
            addPrim(&gameOT[0], &flashTile);
            DrawOTag(&gameOT[GAME_OTLEN-1]);
            DrawSync(0);
            VSync(0);
            VSync(0);  /* Hold for 2 frames */
        }
    }
    /* Re-apply draw environment after grLoadBmp */
    PutDrawEnv(&gameDraw);

    /* Load BACKGRND.BMP into RAM for framebuffer blitting (island sprites) */
    static struct TTtmSlot islandSlot;
    memset(&islandSlot, 0, sizeof(islandSlot));

    /* Load island sprites using RAM approach (not VRAM textures) */
    struct TBmpResource *bgBmpRes = findBmpResource("BACKGRND.BMP");
    if (bgBmpRes && bgBmpRes->uncompressedData) {
        grLoadBmpRAM(&islandSlot, 0, "BACKGRND.BMP");
    }
    int islandSpriteCount = islandSlot.numSprites[0];
    PS1Surface *islandLandmass = NULL;
    PS1Surface *palmTrunk = NULL;
    PS1Surface *palmLeaves = NULL;
    PS1Surface *palmShadow = NULL;
    if (islandSpriteCount > 0) {
        islandLandmass = islandSlot.sprites[0][0];   /* Sprite 0 = island */
    }
    if (islandSpriteCount > 12) {
        palmLeaves = islandSlot.sprites[0][12];      /* Sprite 12 = leaves */
    }
    if (islandSpriteCount > 13) {
        palmTrunk = islandSlot.sprites[0][13];       /* Sprite 13 = trunk */
    }
    if (islandSpriteCount > 14) {
        palmShadow = islandSlot.sprites[0][14];      /* Sprite 14 = shadow */
    }

    /* Composite island sprites INTO the background tiles WITH TRANSPARENCY
     * This is done ONCE at init - grDrawBackground() will render them each frame
     * Order matters: shadow first, then island, then trunk, then leaves (back to front) */
    if (islandLandmass && islandLandmass->pixels) {
        /* Positions from island.c: island(288,279), trunk(442,148), leaves(365,122), shadow(396,279) */
        grCompositeToBackground(islandLandmass, 288, 279);
        if (palmShadow) grCompositeToBackground(palmShadow, 396, 279);
        if (palmTrunk) grCompositeToBackground(palmTrunk, 442, 148);
        if (palmLeaves) grCompositeToBackground(palmLeaves, 365, 122);
    }

    PutDrawEnv(&gameDraw);

    /* Animation state - STATIC to persist across loop iterations */
    static int currentSprite = 0;
    static int frameCounter = 0;
    static int animCycleCount = 0;  /* Count how many times animation has cycled */

    /* === Main game loop (proven working pattern) === */
    while (1) {
        DrawSync(0);
        VSync(0);
        ClearOTagR(gameOT, GAME_OTLEN);
        gameNextPri = gamePrimBuf;

        /* Re-upload background (with composited island sprites) from RAM to framebuffer each frame */
        grDrawBackground();

        /* Continue incremental BMP loading if in progress
         * This loads 1 additional frame per game tick to spread the work */
        int loadingPending = grIsBmpLoadingPending();
        /* ALWAYS call grContinueBmpLoading to debug - count should grow regardless */
        grContinueBmpLoading();
        /* ALWAYS update spriteCount from the actual slot - fixes animation bug */
        spriteCount = gameTtmSlot.numSprites[0];

        /* DEBUG: Dark background for debug bars */
        {
            TILE *bgTile = (TILE*)gameNextPri;
            setTile(bgTile);
            setXY0(bgTile, 0, 0);
            setWH(bgTile, 360, 115);  /* Extended to cover all bars */
            setRGB0(bgTile, 0, 0, 64);  /* Dark blue background */
            addPrim(&gameOT[1], bgTile);  /* Behind other primitives */
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show ps1_loadBmpDataProgress (0-5) as WHITE bar
         * 0=not called, 1=entered, 2=checks passed, 3=stream start, 4=stream done, 5=decompress done */
        {
            TILE *progTile = (TILE*)gameNextPri;
            setTile(progTile);
            setXY0(progTile, 10, 5);
            setWH(progTile, ps1_loadBmpDataProgress * 40 + 20, 15);
            setRGB0(progTile, 255, 255, 255);  /* White */
            addPrim(&gameOT[0], progTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show spriteCount as LARGE GREEN bar */
        {
            TILE *cntTile2 = (TILE*)gameNextPri;
            setTile(cntTile2);
            setXY0(cntTile2, 10, 25);
            setWH(cntTile2, spriteCount * 5 + 10, 15);  /* Scale: 5px per sprite */
            setRGB0(cntTile2, 0, 255, 0);  /* Bright green */
            addPrim(&gameOT[0], cntTile2);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show if testBmp exists (RED if NULL, GREEN if exists) */
        {
            TILE *bmpTile = (TILE*)gameNextPri;
            setTile(bmpTile);
            setXY0(bmpTile, 10, 45);
            setWH(bmpTile, 30, 15);
            setRGB0(bmpTile, testBmp ? 0 : 255, testBmp ? 255 : 0, 0);
            addPrim(&gameOT[0], bmpTile);
            gameNextPri += sizeof(TILE);
        }
        /* DEBUG: Show if uncompressedData is set (RED if NULL, CYAN if set) */
        {
            TILE *dataTile = (TILE*)gameNextPri;
            setTile(dataTile);
            setXY0(dataTile, 45, 45);
            setWH(dataTile, 30, 15);
            setRGB0(dataTile, (testBmp && testBmp->uncompressedData) ? 0 : 255,
                             (testBmp && testBmp->uncompressedData) ? 255 : 0,
                             (testBmp && testBmp->uncompressedData) ? 255 : 0);  /* Cyan if set */
            addPrim(&gameOT[0], dataTile);
            gameNextPri += sizeof(TILE);
        }
        /* DEBUG: Show numImages at testBmp as ORANGE bar */
        {
            TILE *numTile = (TILE*)gameNextPri;
            setTile(numTile);
            setXY0(numTile, 80, 45);
            int num = testBmp ? testBmp->numImages : 0;
            setWH(numTile, num * 4 + 5, 15);
            setRGB0(numTile, 255, 165, 0);  /* Orange */
            addPrim(&gameOT[0], numTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show animation frame (currentSprite) as YELLOW bar */
        {
            TILE *animTile = (TILE*)gameNextPri;
            setTile(animTile);
            setXY0(animTile, 10, 65);
            setWH(animTile, currentSprite * 5 + 10, 15);
            setRGB0(animTile, 255, 255, 0);  /* Yellow */
            addPrim(&gameOT[0], animTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show malloc result - GREEN=ok, RED=failed, GRAY=not called */
        extern uint32 ps1_loadBmpDataUncompSize;
        extern int ps1_loadBmpDataMallocOk;
        {
            TILE *mallocTile = (TILE*)gameNextPri;
            setTile(mallocTile);
            setXY0(mallocTile, 120, 45);
            setWH(mallocTile, 30, 15);
            if (ps1_loadBmpDataMallocOk == 1) {
                setRGB0(mallocTile, 0, 255, 0);  /* Green = malloc ok */
            } else if (ps1_loadBmpDataMallocOk == 0) {
                setRGB0(mallocTile, 255, 0, 0);  /* Red = malloc failed */
            } else {
                setRGB0(mallocTile, 128, 128, 128);  /* Gray = not called */
            }
            addPrim(&gameOT[0], mallocTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show uncompSize as bar (in KB, divide by 1024) */
        {
            TILE *sizeTile = (TILE*)gameNextPri;
            setTile(sizeTile);
            setXY0(sizeTile, 155, 45);
            /* Width = size in KB, max 200px */
            int sizeKB = ps1_loadBmpDataUncompSize / 1024;
            if (sizeKB > 200) sizeKB = 200;
            setWH(sizeTile, sizeKB + 5, 15);
            setRGB0(sizeTile, 128, 0, 128);  /* Purple */
            addPrim(&gameOT[0], sizeTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show grLoadBmpProgress as CYAN bar (scaled for 0-200 range)
         * 4=images, 10=VRAM reset, 20=multi-tile, 30=numToLoad, 40=srcPtr,
         * 50=ready, 100+=loading frame N, 200=done */
        extern int grLoadBmpProgress;
        extern int grLoadBmpNumImages;
        {
            TILE *loadBmpTile = (TILE*)gameNextPri;
            setTile(loadBmpTile);
            setXY0(loadBmpTile, 10, 85);
            /* Scale: 200 -> 200px, 100 -> 100px, etc */
            int barW = grLoadBmpProgress;
            if (barW > 200) barW = 200;
            setWH(loadBmpTile, barW + 10, 15);
            setRGB0(loadBmpTile, 0, 255, 255);  /* Cyan */
            addPrim(&gameOT[0], loadBmpTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show numImages as MAGENTA bar (should be 42 for JOHNWALK) */
        {
            TILE *numImgTile = (TILE*)gameNextPri;
            setTile(numImgTile);
            setXY0(numImgTile, 70, 85);
            setWH(numImgTile, grLoadBmpNumImages * 4 + 5, 15);  /* ~168px for 42 images */
            setRGB0(numImgTile, 255, 0, 255);  /* Magenta */
            addPrim(&gameOT[0], numImgTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show testBmpNumImagesAtFind as BRIGHT CYAN bar (captured right after findBmpResource) */
        {
            TILE *findTile = (TILE*)gameNextPri;
            setTile(findTile);
            setXY0(findTile, 180, 85);
            setWH(findTile, testBmpNumImagesAtFind * 4 + 5, 15);  /* Should be ~168px for 42 */
            setRGB0(findTile, 0, 255, 255);  /* Bright cyan */
            addPrim(&gameOT[0], findTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show testBmpUncompSizeAtFind / 1024 as WHITE bar (in KB) */
        {
            TILE *findSizeTile = (TILE*)gameNextPri;
            setTile(findSizeTile);
            setXY0(findSizeTile, 10, 100);
            int kb = testBmpUncompSizeAtFind / 1024;
            if (kb > 200) kb = 200;
            setWH(findSizeTile, kb + 5, 10);
            setRGB0(findSizeTile, 255, 255, 255);  /* White */
            addPrim(&gameOT[0], findSizeTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show testBmpNumImagesAfterLoad as ORANGE bar (after ps1_loadBmpData) */
        {
            TILE *afterTile = (TILE*)gameNextPri;
            setTile(afterTile);
            setXY0(afterTile, 120, 100);
            setWH(afterTile, testBmpNumImagesAfterLoad * 4 + 5, 10);
            setRGB0(afterTile, 255, 165, 0);  /* Orange */
            addPrim(&gameOT[0], afterTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show loadFrameProgress as BRIGHT GREEN bar (1-8 = success, negative = error) */
        extern int loadFrameProgress;
        {
            TILE *frameTile = (TILE*)gameNextPri;
            setTile(frameTile);
            setXY0(frameTile, 230, 100);
            int w = loadFrameProgress;
            if (w < 0) w = -w;  /* Show absolute value for errors */
            setWH(frameTile, w * 20 + 5, 10);
            setRGB0(frameTile, loadFrameProgress >= 0 ? 0 : 255,
                              loadFrameProgress >= 0 ? 255 : 0, 0);  /* Green if ok, Red if error */
            addPrim(&gameOT[0], frameTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show loading state - YELLOW bar if loading pending, RED if not */
        {
            TILE *loadTile = (TILE*)gameNextPri;
            setTile(loadTile);
            setXY0(loadTile, 10, 140);
            setWH(loadTile, loadingPending ? 100 : 30, 10);
            setRGB0(loadTile, loadingPending ? 255 : 255, loadingPending ? 255 : 0, 0);
            addPrim(&gameOT[0], loadTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show spriteCount as CYAN bar width (should be 42 for JOHNWALK) */
        {
            TILE *cntTile = (TILE*)gameNextPri;
            setTile(cntTile);
            setXY0(cntTile, 10, 155);
            setWH(cntTile, spriteCount * 4, 10);  /* Width = spriteCount * 4 */
            setRGB0(cntTile, 0, 255, 255);  /* Cyan = actual loaded */
            addPrim(&gameOT[0], cntTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show numToLoad as MAGENTA bar (target frame count) */
        {
            int numToLoad = grGetIncrementalNumToLoad();
            TILE *targetTile = (TILE*)gameNextPri;
            setTile(targetTile);
            setXY0(targetTile, 10, 170);
            setWH(targetTile, numToLoad * 4, 10);  /* Width = numToLoad * 4 */
            setRGB0(targetTile, 255, 0, 255);  /* Magenta = target */
            addPrim(&gameOT[0], targetTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show currentFrame as WHITE bar (progress) */
        {
            int curFrame = grGetIncrementalCurrentFrame();
            TILE *progTile = (TILE*)gameNextPri;
            setTile(progTile);
            setXY0(progTile, 10, 185);
            setWH(progTile, curFrame * 4, 10);  /* Width = currentFrame * 4 */
            setRGB0(progTile, 255, 255, 255);  /* White = progress */
            addPrim(&gameOT[0], progTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show grContinueBmpLoading call count as ORANGE bar */
        {
            int callCount = grGetContinueCallCount();
            TILE *callTile = (TILE*)gameNextPri;
            setTile(callTile);
            setXY0(callTile, 10, 200);
            /* Cap at 200 pixels - 30sec @ 60fps = 1800 calls, show /10 */
            int barWidth = (callCount / 10);
            if (barWidth > 200) barWidth = 200;
            setWH(callTile, barWidth + 1, 10);
            setRGB0(callTile, 255, 165, 0);  /* Orange */
            addPrim(&gameOT[0], callTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show if active was set in grLoadBmp - GREEN=yes, RED=no */
        {
            int activeSet = grGetActiveAfterLoad();
            TILE *actTile = (TILE*)gameNextPri;
            setTile(actTile);
            setXY0(actTile, 10, 215);
            setWH(actTile, 50, 10);
            if (activeSet == 1) {
                setRGB0(actTile, 0, 255, 0);  /* GREEN = active was set */
            } else {
                setRGB0(actTile, 255, 0, 0);  /* RED = active NOT set */
            }
            addPrim(&gameOT[0], actTile);
            gameNextPri += sizeof(TILE);
        }

        /* DEBUG: Show completion check values - BLUE bars */
        {
            int checkFrame = grGetCompletionCheckFrame();
            int checkTotal = grGetCompletionCheckTotal();
            int entryTotal = grGetEntryNumToLoad();
            /* checkFrame bar */
            TILE *cfTile = (TILE*)gameNextPri;
            setTile(cfTile);
            setXY0(cfTile, 10, 230);
            setWH(cfTile, checkFrame * 4, 8);
            setRGB0(cfTile, 100, 100, 255);  /* Light blue = checkFrame */
            addPrim(&gameOT[0], cfTile);
            gameNextPri += sizeof(TILE);
            /* checkTotal bar */
            TILE *ctTile = (TILE*)gameNextPri;
            setTile(ctTile);
            setXY0(ctTile, 10, 240);
            setWH(ctTile, checkTotal * 4, 8);
            setRGB0(ctTile, 0, 0, 255);  /* Dark blue = checkTotal */
            addPrim(&gameOT[0], ctTile);
            gameNextPri += sizeof(TILE);
            /* entryTotal bar - numToLoad at first entry */
            TILE *etTile = (TILE*)gameNextPri;
            setTile(etTile);
            setXY0(etTile, 10, 250);
            setWH(etTile, entryTotal * 4, 8);
            setRGB0(etTile, 128, 0, 128);  /* Purple = entry numToLoad */
            addPrim(&gameOT[0], etTile);
            gameNextPri += sizeof(TILE);
        }

        /* Animate through sprite frames - cycle every 15 frames (~4fps) */
        frameCounter++;
        if (frameCounter >= 15) {
            frameCounter = 0;
            animCycleCount++;
            /* Cycle through all loaded sprites */
            if (spriteCount > 0) {
                currentSprite = (currentSprite + 1) % spriteCount;
            }
        }
        /* Update loadedSprite from current index */
        if (currentSprite < spriteCount && gameTtmSlot.sprites[0][currentSprite] != NULL) {
            loadedSprite = gameTtmSlot.sprites[0][currentSprite];
        }

        /* Base position for animation - this is where the sprite's feet should be */
        int baseX = 350;  /* Center X position */
        int baseY = 364;  /* Bottom Y position (feet on ground) */

        /* Draw actual textured sprite if loaded, otherwise green placeholder */
        if (loadedSprite && loadedSprite->width > 0) {
            /* Calculate draw position: center horizontally, bottom-align vertically */
            int spriteX = baseX - (loadedSprite->width / 2);
            int spriteY = baseY - loadedSprite->height;

            /* === TEST: Use grDrawSpriteExt to verify UV fix === */
            grDrawSpriteExt(&gameOT[0], &gameNextPri, loadedSprite, spriteX, spriteY);
        } else {
            /* Fallback: Green placeholder squares if no sprite loaded */
            int spriteX = baseX - 32;  /* Center 64x64 placeholder */
            int spriteY = baseY - 64;
            POLY_F3 *spr1 = (POLY_F3*)gameNextPri;
            setPolyF3(spr1);
            setXY3(spr1, spriteX, spriteY, spriteX+64, spriteY, spriteX, spriteY+64);
            setRGB0(spr1, 0, 255, 0);
            addPrim(&gameOT[0], spr1);
            gameNextPri += sizeof(POLY_F3);

            POLY_F3 *spr2 = (POLY_F3*)gameNextPri;
            setPolyF3(spr2);
            setXY3(spr2, spriteX+64, spriteY, spriteX+64, spriteY+64, spriteX, spriteY+64);
            setRGB0(spr2, 0, 255, 0);
            addPrim(&gameOT[0], spr2);
            gameNextPri += sizeof(POLY_F3);
        }

        (void)spriteCount;  /* Suppress unused warning */
        (void)islandSpriteCount;

        /* Visual debug: Show each sprite's VRAM X as bar width
         * If all bars are same width = all sprites at same VRAM X (BUG)
         * Different widths = sprites at different positions (CORRECT) */
        for (int i = 0; i < spriteCount && i < 10; i++) {
            PS1Surface *dbgSpr = gameTtmSlot.sprites[0][i];
            TILE *dbgTile = (TILE*)gameNextPri;
            setTile(dbgTile);
            setXY0(dbgTile, 10, 10 + i * 12);
            /* Width = (VRAM_X - 640) to show relative position */
            int barWidth = dbgSpr ? (dbgSpr->x - 640 + 10) : 5;
            if (barWidth < 5) barWidth = 5;
            if (barWidth > 200) barWidth = 200;
            setWH(dbgTile, barWidth, 10);
            if (i == currentSprite) {
                setRGB0(dbgTile, 0, 255, 0);  /* GREEN = current */
            } else {
                setRGB0(dbgTile, 200, 200, 200);  /* White */
            }
            addPrim(&gameOT[0], dbgTile);
            gameNextPri += sizeof(TILE);
        }

        /* Suppress unused variable warnings */
        (void)frameCounter;
        (void)animCycleCount;

        /* Draw OT */
        PutDrawEnv(&gameDraw);
        DrawOTag(gameOT + GAME_OTLEN - 1);
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

