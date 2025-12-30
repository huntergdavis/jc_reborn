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
        grLoadScreen(bgScr->resName);
    }

    /* Create a simple TTtmSlot to hold sprites */
    static struct TTtmSlot gameTtmSlot;
    memset(&gameTtmSlot, 0, sizeof(gameTtmSlot));

    /* Load first available BMP resource into slot 0 */
    PS1Surface *loadedSprite = NULL;
    int spriteCount = 0;

    /* TEST: Directly load JOHNWALK.BMP to test streaming dynamic loading.
     * JOHNWALK has uncompressedData = NULL (skipped at startup),
     * so grLoadBmp will trigger ps1_loadBmpData with streaming reads. */
    grLoadBmpRAM(&gameTtmSlot, 0, "JOHNWALK.BMP");
    spriteCount = gameTtmSlot.numSprites[0];
    if (spriteCount > 0) {
        loadedSprite = gameTtmSlot.sprites[0][0];
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

    /* Save clean background tiles (with island already composited).
     * Each frame we'll restore from this, composite Johnny sprite, then upload. */
    grSaveCleanBgTiles();

    PutDrawEnv(&gameDraw);

    /* Animation state */
    int currentSprite = 0;
    int frameCounter = 0;

    /* Dirty rectangle: save only sprite area instead of full tiles (memory efficient) */
    static uint16 *savedSpriteArea = NULL;
    static int savedX = 0, savedY = 0, savedW = 0, savedH = 0;

    /* Allocate sprite save buffer once (max 64x80 @ 16-bit = 10KB) */
    if (!savedSpriteArea) {
        savedSpriteArea = (uint16*)malloc(64 * 80 * 2);
    }

    /* === Main game loop (proven working pattern) === */
    while (1) {
        DrawSync(0);
        VSync(0);
        ClearOTagR(gameOT, GAME_OTLEN);
        gameNextPri = gamePrimBuf;

        /* Restore previous sprite area from saved copy (dirty rectangle) */
        if (savedSpriteArea && savedW > 0 && savedH > 0) {
            /* Restore saved pixels back to bgTiles */
            for (int y = 0; y < savedH; y++) {
                int destY = savedY + y;
                if (destY < 0 || destY >= 480) continue;
                for (int x = 0; x < savedW; x++) {
                    int destX = savedX + x;
                    if (destX < 0 || destX >= 640) continue;

                    uint16 pixel = savedSpriteArea[y * savedW + x];

                    /* Determine which tile (bgTile* declared in graphics_ps1.h) */
                    PS1Surface *tile = NULL;
                    int tileLocalX, tileLocalY;

                    if (destY < 240) {
                        tileLocalY = destY;
                        if (destX < 320) { tile = bgTile0; tileLocalX = destX; }
                        else { tile = bgTile1; tileLocalX = destX - 320; }
                    } else {
                        tileLocalY = destY - 240;
                        if (destX < 320) { tile = bgTile3; tileLocalX = destX; }
                        else { tile = bgTile4; tileLocalX = destX - 320; }
                    }

                    if (tile && tile->pixels) {
                        tile->pixels[tileLocalY * tile->width + tileLocalX] = pixel;
                    }
                }
            }
        }

        /* Don't use grRestoreBgTiles - using dirty rectangle instead */
        /* grRestoreBgTiles(); */

        /* Animate through sprite frames - slower for debugging */
        if (++frameCounter >= 15) {  /* Change frame every 15 vsyncs (~4 fps) */
            frameCounter = 0;
            if (spriteCount > 1) {
                currentSprite = (currentSprite + 1) % spriteCount;
                loadedSprite = gameTtmSlot.sprites[0][currentSprite];
            }
        }

        /* Base position for animation - this is where the sprite's feet should be */
        int baseX = 350;  /* Center X position */
        int baseY = 320;  /* Bottom Y position (feet on island) - adjusted up more */

        /* Draw actual textured sprite if loaded, otherwise green placeholder */
        if (loadedSprite && loadedSprite->width > 0) {
            /* Calculate draw position: center horizontally, bottom-align vertically */
            int spriteX = baseX - (loadedSprite->width / 2);
            int spriteY = baseY - loadedSprite->height;

            /* SAVE sprite area BEFORE compositing (dirty rectangle) */
            if (savedSpriteArea && loadedSprite->width <= 64 && loadedSprite->height <= 80) {
                savedX = spriteX;
                savedY = spriteY;
                savedW = loadedSprite->width;
                savedH = loadedSprite->height;

                /* bgTile* declared in graphics_ps1.h */
                for (int y = 0; y < savedH; y++) {
                    int srcY = savedY + y;
                    if (srcY < 0 || srcY >= 480) continue;
                    for (int x = 0; x < savedW; x++) {
                        int srcX = savedX + x;
                        if (srcX < 0 || srcX >= 640) continue;

                        PS1Surface *tile = NULL;
                        int tileLocalX, tileLocalY;
                        if (srcY < 240) {
                            tileLocalY = srcY;
                            if (srcX < 320) { tile = bgTile0; tileLocalX = srcX; }
                            else { tile = bgTile1; tileLocalX = srcX - 320; }
                        } else {
                            tileLocalY = srcY - 240;
                            if (srcX < 320) { tile = bgTile3; tileLocalX = srcX; }
                            else { tile = bgTile4; tileLocalX = srcX - 320; }
                        }

                        if (tile && tile->pixels) {
                            savedSpriteArea[y * savedW + x] = tile->pixels[tileLocalY * tile->width + tileLocalX];
                        } else {
                            savedSpriteArea[y * savedW + x] = 0x0000;
                        }
                    }
                }
            }

            /* === Composite sprite to bgTiles === */
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

        /* Upload composited background tiles (with sprite already composited) to framebuffer */
        grDrawBackground();

        /* Debug: Show sprite info on screen */
        if (loadedSprite) {
            uint8 dbgU = ((loadedSprite->x % 64) * 4) & 0xFF;
            uint16 dbgTpage = loadedSprite->x / 64;
            FntPrint("Fr:%d VX:%d U:%d TP:%d Cnt:%d\n",
                     currentSprite, loadedSprite->x, dbgU, dbgTpage, spriteCount);
        }
        FntFlush(fontID);

        /* Draw OT (for font, any VRAM-based sprites) */
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

