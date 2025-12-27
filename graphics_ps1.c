/*
 *  This file is part of 'Johnny Reborn' - PS1 Port
 *
 *  PlayStation 1 graphics implementation using PSn00bSDK
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <psxapi.h>

#include "mytypes.h"

/* Forward declare FILE to avoid utils.h compilation errors with -ffreestanding */
typedef struct _FILE FILE;

#include "utils.h"
#include "graphics_ps1.h"
#include "resource.h"
#include "events_ps1.h"

/* Primitive buffer for GPU commands */
#define PRIMITIVE_BUFFER_SIZE 32768
uint8 *primitiveBuffer[2];  /* Malloc'd, not static array! */
uint32 primitiveIndex[2];
uint8 *nextPrimitive[2];

/* PS1 Display and drawing environments */
static DISPENV disp[2];
DRAWENV draw[2];
int db = 0;  /* Double buffer index */

/* Ordering tables for GPU command queueing */
#define OT_LENGTH 8
unsigned long ot[2][OT_LENGTH];

/* Palette (16 colors, matching original TTM format) - exported for jc_reborn.c */
uint16 ttmPalette[16];

/* Layer management */
static PS1Surface *grSavedZonesLayer = NULL;
PS1Surface *grBackgroundSfc = NULL;

/* Background tiles for pixel-perfect 640x480 rendering
 * Top row: 3 tiles (256+256+128 = 640 pixels wide, 240 tall)
 * Bottom row will be added later */
#define BG_TILE_HEIGHT 240
/* Top row tiles (stored in VRAM texture area) */
static PS1Surface *bgTile0 = NULL;  /* x=0-255,   srcX=0 */
static PS1Surface *bgTile1 = NULL;  /* x=256-511, srcX=256 */
static PS1Surface *bgTile2a = NULL; /* x=512-575, srcX=512, width=64 */
static PS1Surface *bgTile2b = NULL; /* x=576-639, srcX=576, width=64 */

/* Bottom row tiles - need VRAM space outside framebuffer (0-639, 0-479)
 * VRAM is 1024x512. Texture area: x=640-1023, y=0-511
 * Top row uses: (640,4)-(895,243), (640,244)-(895,483), (896,4)-(959,243), (960,4)-(1021,243)
 * Bottom row can use y=484+ area since VRAM is 512 tall: LIMITED SPACE
 * Alternative: Use same VRAM locations as top row, MoveImage top first, then load bottom, MoveImage bottom */
static PS1Surface *bgTile3 = NULL;  /* y=240, x=0-255 */
static PS1Surface *bgTile4 = NULL;  /* y=240, x=256-511 */
static PS1Surface *bgTile5a = NULL; /* y=240, x=512-575 */
static PS1Surface *bgTile5b = NULL; /* y=240, x=576-637 */

/* Global variables matching original implementation */
int grDx = 0;
int grDy = 0;
int grWindowed = 0;  /* PS1 is always fullscreen, but keep for compatibility */
int grUpdateDelay = 0;

/* Frame capture - stubbed for PS1 */
int grCaptureFrameNumber = -1;
char *grCaptureFilename = NULL;
static int grCurrentFrame = 0;

/* Flag to track if GPU was already initialized (e.g., by loadTitleScreenEarly) */
int grGpuAlreadyInitialized = 0;

/* VRAM allocation tracking
 * VRAM Layout for 640x480 interlaced:
 * (0,0)-(639,479): Framebuffer (single buffer for 640x480)
 * (640,0)-(656,1): CLUT (16 colors)
 * (640,2)-(895,2): CLUT 256 (grayscale)
 * (640,4) onwards: Textures
 */
static uint16 nextVRAMX = 640;  /* Start to the right of framebuffer */
static uint16 nextVRAMY = 4;    /* Below CLUTs */

/*
 * Initialize PS1 graphics subsystem
 */
void graphicsInit()
{
    /* Skip GPU reset if already initialized (e.g., by loadTitleScreenEarly)
     * CRITICAL: Calling ResetGraph(0) after GPU is already initialized
     * conflicts with the existing GPU state and causes hangs */
    if (!grGpuAlreadyInitialized) {
        if (debugMode)
            printf("GPU: Resetting GPU...\n");

        /* Reset GPU and set video mode */
        ResetGraph(0);
        SetVideoMode(MODE_NTSC);

        if (debugMode)
            printf("GPU: Initializing GTE...\n");

        /* Initialize geometry transformation engine */
        InitGeom();

        if (debugMode)
            printf("GPU: Setting up display buffers (%dx%d)...\n", SCREEN_WIDTH, SCREEN_HEIGHT);

        /* Setup display environments for 640x480 interlaced mode
         * Single buffer mode since 2x640x480 won't fit in VRAM
         * Both buffers point to same location - no flipping needed */
        SetDefDispEnv(&disp[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        SetDefDispEnv(&disp[1], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        /* Enable interlaced mode for 640x480 */
        disp[0].isinter = 1;
        disp[1].isinter = 1;

        /* Setup drawing environments - both draw to same buffer */
        SetDefDrawEnv(&draw[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        SetDefDrawEnv(&draw[1], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        /* Set background clear color - but DON'T enable clear!
         * We use grDrawBackground() LoadImage to paint the background each frame,
         * so isbg=0 preserves it for OT primitives to draw on top */
        setRGB0(&draw[0], 0, 0, 0);
        setRGB0(&draw[1], 0, 0, 0);
        draw[0].isbg = 0;  /* Don't clear - grDrawBackground handles it */
        draw[1].isbg = 0;

        if (debugMode)
            printf("GPU: Enabling display...\n");

        /* Enable display */
        SetDispMask(1);

        /* Apply first buffer */
        PutDispEnv(&disp[db]);
        PutDrawEnv(&draw[db]);
    } else {
        if (debugMode)
            printf("GPU: Skipping ResetGraph - already initialized\n");

        /* Still need to setup display/draw environments for rendering
         * GPU is already on, but we need proper environment structs */
        SetDefDispEnv(&disp[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        SetDefDispEnv(&disp[1], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        disp[0].isinter = 1;  /* Interlaced mode for 640x480 */
        disp[1].isinter = 1;

        SetDefDrawEnv(&draw[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        SetDefDrawEnv(&draw[1], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        setRGB0(&draw[0], 0, 0, 0);
        setRGB0(&draw[1], 0, 0, 0);
        draw[0].isbg = 0;  /* Don't clear - grDrawBackground handles it */
        draw[1].isbg = 0;

        /* Apply environments immediately (like the non-ELSE branch) */
        PutDispEnv(&disp[db]);
        PutDrawEnv(&draw[db]);
    }

    if (debugMode)
        printf("GPU: Initializing ordering tables...\n");

    /* Clear ordering tables */
    ClearOTagR(ot[0], OT_LENGTH);
    ClearOTagR(ot[1], OT_LENGTH);

    /* Allocate primitive buffers dynamically to reduce BSS size */
    primitiveBuffer[0] = (uint8*)malloc(PRIMITIVE_BUFFER_SIZE);
    primitiveBuffer[1] = (uint8*)malloc(PRIMITIVE_BUFFER_SIZE);
    if (!primitiveBuffer[0] || !primitiveBuffer[1]) {
        printf("ERROR: Failed to allocate primitive buffers\n");
        while(1);
    }

    /* Initialize primitive buffers */
    nextPrimitive[0] = primitiveBuffer[0];
    nextPrimitive[1] = primitiveBuffer[1];
    primitiveIndex[0] = 0;
    primitiveIndex[1] = 0;

    if (debugMode)
        printf("GPU: Loading default palette...\n");

    /* Load default palette with distinct, bright colors for testing
     * PS1 uses BGR555 format: (B << 10) | (G << 5) | R
     * Each channel is 5 bits (0-31) */
    ttmPalette[0]  = (0 << 10)  | (0 << 5)  | 0;   /* 0: Black */
    ttmPalette[1]  = (0 << 10)  | (0 << 5)  | 31;  /* 1: Red */
    ttmPalette[2]  = (0 << 10)  | (31 << 5) | 0;   /* 2: Green */
    ttmPalette[3]  = (31 << 10) | (0 << 5)  | 0;   /* 3: Blue */
    ttmPalette[4]  = (0 << 10)  | (31 << 5) | 31;  /* 4: Yellow */
    ttmPalette[5]  = (31 << 10) | (0 << 5)  | 31;  /* 5: Magenta */
    ttmPalette[6]  = (31 << 10) | (31 << 5) | 0;   /* 6: Cyan */
    ttmPalette[7]  = (31 << 10) | (31 << 5) | 31;  /* 7: White */
    ttmPalette[8]  = (16 << 10) | (16 << 5) | 16;  /* 8: Gray */
    ttmPalette[9]  = (0 << 10)  | (0 << 5)  | 20;  /* 9: Dark Red */
    ttmPalette[10] = (0 << 10)  | (20 << 5) | 0;   /* 10: Dark Green */
    ttmPalette[11] = (20 << 10) | (0 << 5)  | 0;   /* 11: Dark Blue */
    ttmPalette[12] = (0 << 10)  | (20 << 5) | 20;  /* 12: Orange */
    ttmPalette[13] = (20 << 10) | (0 << 5)  | 20;  /* 13: Purple */
    ttmPalette[14] = (20 << 10) | (20 << 5) | 0;   /* 14: Teal */
    ttmPalette[15] = (20 << 10) | (20 << 5) | 20;  /* 15: Light Gray */

    /* Upload 16-color CLUT for primitives at (640, 0) - right of framebuffer */
    RECT clutRect16;
    setRECT(&clutRect16, 640, 0, 16, 1);  /* 16 colors, 1 row */
    LoadImage(&clutRect16, (uint32*)ttmPalette);

    /* Create and upload 256-color grayscale CLUT for SCR textures at (640, 2) */
    static uint16 clut256[256];
    for (int i = 0; i < 256; i++) {
        /* Convert 8-bit grayscale to BGR555 */
        uint8 val = (i >> 3) & 0x1F;  /* Scale 0-255 to 0-31 */
        clut256[i] = (val << 10) | (val << 5) | val;  /* Grayscale */
    }
    RECT clutRect256;
    setRECT(&clutRect256, 640, 2, 256, 1);  /* 256 colors, 1 row */
    LoadImage(&clutRect256, (uint32*)clut256);

    if (debugMode)
        printf("GPU: Initializing event system...\n");

    /* Initialize event system */
    eventsInit();

    if (debugMode)
        printf("GPU: Graphics initialization complete!\n");
}

/*
 * Shutdown graphics subsystem
 */
void graphicsEnd()
{
    /* Free allocated surfaces */
    if (grBackgroundSfc != NULL) {
        grFreeLayer(grBackgroundSfc);
        grBackgroundSfc = NULL;
    }

    if (grSavedZonesLayer != NULL) {
        grFreeLayer(grSavedZonesLayer);
        grSavedZonesLayer = NULL;
    }

    SetDispMask(0);
}

/*
 * Load palette from resource
 */
void grLoadPalette(struct TPalResource *palResource)
{
    if (palResource == NULL) {
        fatalError("NULL palette\n");
    }

    /* Convert VGA 6-bit RGB to PS1 15-bit RGB (5-5-5)
     * Magenta (0xa8, 0, 0xa8) = VGA 6-bit (42, 0, 42) is the transparent color.
     * Match SDL version: only EXACT magenta (168, 0, 168 in 8-bit) is transparent. */
    for (int i = 0; i < 16; i++) {
        uint8 vgaR = palResource->colors[i].r;  /* 6-bit VGA values (0-63) */
        uint8 vgaG = palResource->colors[i].g;
        uint8 vgaB = palResource->colors[i].b;

        /* Convert to 8-bit like SDL version */
        uint8 r8 = vgaR << 2;
        uint8 g8 = vgaG << 2;
        uint8 b8 = vgaB << 2;

        /* Check for EXACT magenta (0xa8=168, 0, 0xa8=168) only - matches SDL behavior */
        int isMagenta = (r8 == 0xa8) && (g8 == 0) && (b8 == 0xa8);

        if (isMagenta) {
            /* Magenta = 0x0000 = fully transparent on PS1 */
            ttmPalette[i] = 0x0000;
        } else {
            uint8 r = r8 >> 3;  /* 8-bit to 5-bit */
            uint8 g = g8 >> 3;
            uint8 b = b8 >> 3;
            uint16 color = (b << 10) | (g << 5) | r;

            /* IMPORTANT: If color is 0x0000 (black), use 0x0001 instead
             * because 0x0000 is reserved for transparency on PS1 */
            if (color == 0x0000) {
                color = 0x0001;  /* Very dark blue, nearly black */
            }
            ttmPalette[i] = color;
        }
    }

    /* Upload CLUT (Color Lookup Table) to VRAM */
    RECT clutRect;
    setRECT(&clutRect, 640, 0, 16, 1);  /* 16 colors, 1 row, at (640, 0) */
    LoadImage(&clutRect, (uint32*)ttmPalette);
}

/*
 * Swap display buffers
 */
void grRefreshDisplay()
{
    /* Wait for GPU to finish previous frame's drawing */
    DrawSync(0);

    /* Wait for vertical blank */
    VSync(0);

    /* Flip buffers - display the one we just drew to */
    db = !db;
    PutDispEnv(&disp[db]);
    PutDrawEnv(&draw[db]);

    /* Submit the ordering table to GPU for drawing
     * This renders all primitives added since last refresh
     * OT is linked from end to start, so submit from last entry */
    DrawOTag(&ot[1-db][OT_LENGTH - 1]);

    /* Clear next ordering table for new frame */
    ClearOTagR(ot[db], OT_LENGTH);

    /* Reset primitive buffer for next frame */
    nextPrimitive[db] = primitiveBuffer[db];
    primitiveIndex[db] = 0;
}

/*
 * Toggle fullscreen (no-op on PS1, always fullscreen)
 */
void grToggleFullScreen()
{
    /* PS1 is always fullscreen */
    grWindowed = !grWindowed;  /* Keep variable for compatibility */
}

/*
 * Update display with all layers
 */
void grUpdateDisplay(struct TTtmThread *ttmBackgroundThread,
                     struct TTtmThread *ttmThreads,
                     struct TTtmThread *ttmHolidayThread)
{
    /* Main rendering function that composites all layers */

    /* 1. Draw background layer if it exists */
    if (grBackgroundSfc != NULL && grBackgroundSfc->pixels != NULL) {
        /* Allocate SPRT for background */
        if (primitiveIndex[db] + sizeof(SPRT) <= PRIMITIVE_BUFFER_SIZE) {
            SPRT *bgSprt = (SPRT*)nextPrimitive[db];
            nextPrimitive[db] += sizeof(SPRT);
            primitiveIndex[db] += sizeof(SPRT);

            setSprt(bgSprt);
            setXY0(bgSprt, 0, 0);
            setWH(bgSprt, grBackgroundSfc->width, grBackgroundSfc->height);
            setUV0(bgSprt, grBackgroundSfc->x & 0xFF, grBackgroundSfc->y & 0xFF);
            setClut(bgSprt, grBackgroundSfc->clutX, grBackgroundSfc->clutY);
            setRGB0(bgSprt, 128, 128, 128);

            addPrim(&ot[db][7], bgSprt);  /* Lowest priority in OT */
        }
    }

    /* 2. Draw saved zones layer if it exists */
    if (grSavedZonesLayer != NULL && grSavedZonesLayer->pixels != NULL) {
        if (primitiveIndex[db] + sizeof(SPRT) <= PRIMITIVE_BUFFER_SIZE) {
            SPRT *zonesSprt = (SPRT*)nextPrimitive[db];
            nextPrimitive[db] += sizeof(SPRT);
            primitiveIndex[db] += sizeof(SPRT);

            setSprt(zonesSprt);
            setXY0(zonesSprt, 0, 0);
            setWH(zonesSprt, grSavedZonesLayer->width, grSavedZonesLayer->height);
            setUV0(zonesSprt, grSavedZonesLayer->x & 0xFF, grSavedZonesLayer->y & 0xFF);
            setClut(zonesSprt, grSavedZonesLayer->clutX, grSavedZonesLayer->clutY);
            setRGB0(zonesSprt, 128, 128, 128);

            addPrim(&ot[db][6], zonesSprt);
        }
    }

    /* 3. Draw background thread layer */
    if (ttmBackgroundThread != NULL && ttmBackgroundThread->ttmLayer != NULL) {
        PS1Surface *layer = ttmBackgroundThread->ttmLayer;
        if (layer->pixels != NULL) {
            if (primitiveIndex[db] + sizeof(SPRT) <= PRIMITIVE_BUFFER_SIZE) {
                SPRT *layerSprt = (SPRT*)nextPrimitive[db];
                nextPrimitive[db] += sizeof(SPRT);
                primitiveIndex[db] += sizeof(SPRT);

                setSprt(layerSprt);
                setXY0(layerSprt, 0, 0);
                setWH(layerSprt, layer->width, layer->height);
                setUV0(layerSprt, layer->x & 0xFF, layer->y & 0xFF);
                setClut(layerSprt, layer->clutX, layer->clutY);
                setRGB0(layerSprt, 128, 128, 128);

                addPrim(&ot[db][5], layerSprt);
            }
        }
    }

    /* 4. Draw all active TTM thread layers */
    for (int i = 0; i < 10; i++) {  /* Max 10 TTM threads */
        if (ttmThreads[i].isRunning && ttmThreads[i].ttmLayer != NULL) {
            PS1Surface *layer = ttmThreads[i].ttmLayer;
            if (layer->pixels != NULL) {
                if (primitiveIndex[db] + sizeof(SPRT) <= PRIMITIVE_BUFFER_SIZE) {
                    SPRT *layerSprt = (SPRT*)nextPrimitive[db];
                    nextPrimitive[db] += sizeof(SPRT);
                    primitiveIndex[db] += sizeof(SPRT);

                    setSprt(layerSprt);
                    setXY0(layerSprt, 0, 0);
                    setWH(layerSprt, layer->width, layer->height);
                    setUV0(layerSprt, layer->x & 0xFF, layer->y & 0xFF);
                    setClut(layerSprt, layer->clutX, layer->clutY);
                    setRGB0(layerSprt, 128, 128, 128);

                    addPrim(&ot[db][4], layerSprt);
                }
            }
        }
    }

    /* 5. Draw holiday thread layer if active */
    if (ttmHolidayThread != NULL && ttmHolidayThread->isRunning && ttmHolidayThread->ttmLayer != NULL) {
        PS1Surface *layer = ttmHolidayThread->ttmLayer;
        if (layer->pixels != NULL) {
            if (primitiveIndex[db] + sizeof(SPRT) <= PRIMITIVE_BUFFER_SIZE) {
                SPRT *layerSprt = (SPRT*)nextPrimitive[db];
                nextPrimitive[db] += sizeof(SPRT);
                primitiveIndex[db] += sizeof(SPRT);

                setSprt(layerSprt);
                setXY0(layerSprt, 0, 0);
                setWH(layerSprt, layer->width, layer->height);
                setUV0(layerSprt, layer->x & 0xFF, layer->y & 0xFF);
                setClut(layerSprt, layer->clutX, layer->clutY);
                setRGB0(layerSprt, 128, 128, 128);

                addPrim(&ot[db][3], layerSprt);
            }
        }
    }

    /* Submit all primitives and swap buffers */
    DrawOTag(&ot[db][OT_LENGTH - 1]);
    grRefreshDisplay();

    /* Handle frame timing */
    eventsWaitTick(grUpdateDelay);

    grCurrentFrame++;
}

/*
 * Create a new empty background surface
 */
PS1Surface *grNewEmptyBackground()
{
    PS1Surface *sfc = (PS1Surface*)malloc(sizeof(PS1Surface));
    if (sfc == NULL) {
        fatalError("Failed to allocate PS1Surface");
    }

    sfc->width = SCREEN_WIDTH;
    sfc->height = SCREEN_HEIGHT;
    sfc->x = nextVRAMX;
    sfc->y = nextVRAMY;
    sfc->pixels = NULL;  /* Will be allocated in VRAM */

    /* Update VRAM allocation tracking */
    nextVRAMY += SCREEN_HEIGHT;
    if (nextVRAMY >= 512) {  /* VRAM height limit */
        nextVRAMX += SCREEN_WIDTH;
        nextVRAMY = 0;
    }

    return sfc;
}

/*
 * Create a new layer surface for TTM animations
 */
PS1Surface *grNewLayer()
{
    return grNewEmptyBackground();
}

/*
 * Free a layer surface
 */
void grFreeLayer(PS1Surface *sfc)
{
    while (sfc != NULL) {
        PS1Surface *next = sfc->nextTile;
        /* VRAM is managed globally, just free the structure */
        free(sfc);
        sfc = next;
    }
}

/*
 * Load BMP sprite sheet into slot
 */
void grLoadBmp(struct TTtmSlot *ttmSlot, uint16 slotNo, char *strArg)
{
    /*
     * CRITICAL PATTERN: LoadImage MUST be called with a simple uint16* buffer,
     * NOT through a struct member like surface->pixels. Creating the PS1Surface
     * AFTER LoadImage works; creating it BEFORE and using surface->pixels
     * as the LoadImage source breaks OT primitive rendering.
     *
     * Additionally, sprite dimensions must be capped at 64x64 to prevent
     * OT rendering issues with large textures.
     */
    #define MAX_SPRITE_DIM 64  /* Keep at 64 - larger causes VRAM/UV issues */

    if (ttmSlot->numSprites[slotNo])
        grReleaseBmp(ttmSlot, slotNo);

    struct TBmpResource *bmpResource = findBmpResource(strArg);
    if (!bmpResource) return;

    /* On-demand loading: decompress BMP if not already loaded */
    if (!bmpResource->uncompressedData) {
        ps1_loadBmpData(bmpResource);
    }
    if (!bmpResource->uncompressedData) return;  /* Still NULL = load failed */
    if (bmpResource->numImages < 1) return;

    /* Reset VRAM tracking to ensure sprites start at clean position
     * This prevents cumulative drift from previous allocations */
    nextVRAMX = 640;  /* Start of texture area */
    nextVRAMY = 4;    /* Below CLUTs */

    /* Detect if ANY frame needs multi-tile (height > 64) */
    int needsMultiTile = 0;
    for (int i = 0; i < bmpResource->numImages && i < MAX_SPRITES_PER_BMP; i++) {
        if (bmpResource->heights[i] > MAX_SPRITE_DIM) {
            needsMultiTile = 1;
            break;
        }
    }

    /* Load sprite frames from BMP for animation support */
    int numToLoad = bmpResource->numImages;
    if (numToLoad > 42) {
        numToLoad = 42;  /* Max 42 frames (6 pages/row × 7 rows) */
    }
    /* For multi-tile BMPs, limit to 3 frames for debugging */
    if (needsMultiTile && numToLoad > 3) {
        numToLoad = 3;
    }

    uint8 *srcPtr = bmpResource->uncompressedData;

    /* DEBUG: Track multi-tile loading progress */
    static int debugMultiTileFrame = -1;

    for (int frameIdx = 0; frameIdx < numToLoad; frameIdx++) {
        if (needsMultiTile) {
            debugMultiTileFrame = frameIdx;
        }
        uint16 width = bmpResource->widths[frameIdx];
        uint16 height = bmpResource->heights[frameIdx];
        uint16 safeW = (width > MAX_SPRITE_DIM) ? MAX_SPRITE_DIM : width;
        uint16 safeH = (height > MAX_SPRITE_DIM) ? MAX_SPRITE_DIM : height;

        /* Step 1: Allocate and copy pixel data to a SIMPLE buffer */
        uint32 copySize = (safeW * safeH) / 2;  /* 4-bit = 0.5 bytes/pixel */
        uint16 *copyBuf = (uint16*)safe_malloc(copySize);

        /* Copy with proper row stride when capping dimensions
         * IMPORTANT: PS1 4-bit textures expect LOW nibble = pixel 0, HIGH nibble = pixel 1
         * Sierra BMP format is HIGH nibble = pixel 0, LOW nibble = pixel 1
         * We must swap nibbles during copy! */
        uint8 *dst = (uint8*)copyBuf;
        uint8 *src = srcPtr;
        uint32 srcRowBytes = width / 2;   /* 4-bit = 2 pixels per byte */
        uint32 dstRowBytes = safeW / 2;

        for (uint16 y = 0; y < safeH; y++) {
            for (uint32 x = 0; x < dstRowBytes; x++) {
                uint8 srcByte = src[x];
                /* Swap nibbles: high->low, low->high */
                dst[x] = ((srcByte & 0x0F) << 4) | ((srcByte >> 4) & 0x0F);
            }
            dst += dstRowBytes;
            src += srcRowBytes;
        }

        /* Advance source pointer for next frame (use full frame size, not capped) */
        srcPtr += (width * height) / 2;

        /* Step 2: LoadImage to VRAM BEFORE creating PS1Surface */
        /* For 4-bit textures, texture page is 64 VRAM pixels (256 texture pixels) wide.
         * UV coordinates are 8-bit (0-255). Check if sprite would cause UV wrap. */
        uint16 vramW = safeW / 4;  /* 4-bit: VRAM width = texture pixels / 4 */

        /* Calculate UV offset that would result from current VRAM position */
        uint16 uOffset = ((nextVRAMX % 64) * 4);  /* UV offset in texture pixels */

        /* AGGRESSIVE: Always align to page boundary - ensures U=0 for every sprite */
        if (uOffset != 0) {
            /* Not at page start - align to next texture page */
            uint16 nextPage = ((nextVRAMX / 64) + 1) * 64;
            nextVRAMX = nextPage;
            if (nextVRAMX >= 1024) {
                nextVRAMX = 640;
                nextVRAMY += MAX_SPRITE_DIM;
            }
        }

        /* Check if we've exhausted VRAM - stop loading more frames */
        if (nextVRAMY + safeH > 512) {
            break;  /* Out of VRAM space */
        }

        uint16 vramX = nextVRAMX;
        uint16 vramY = nextVRAMY;
        RECT rect;
        setRECT(&rect, vramX, vramY, vramW, safeH);
        LoadImage(&rect, (uint32*)copyBuf);
        DrawSync(0);

        /* Step 3: NOW create PS1Surface AFTER LoadImage completed */
        PS1Surface *surface = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
        surface->width = safeW;
        surface->height = safeH;
        surface->x = vramX;
        surface->y = vramY;
        surface->pixels = copyBuf;
        surface->clutX = 640;
        surface->clutY = 0;
        /* Multi-tile fields */
        surface->fullWidth = width;
        surface->fullHeight = height;
        surface->tileOffsetX = 0;
        surface->tileOffsetY = 0;
        surface->nextTile = NULL;

        /* Multi-tile: Load bottom portion for tall sprites
         * DEBUG: Only load bottom tiles for frames 0-1, skip frame 2 */
        if (height > MAX_SPRITE_DIM && frameIdx < 2) {
            uint16 bottomH = height - MAX_SPRITE_DIM;
            if (bottomH > MAX_SPRITE_DIM) bottomH = MAX_SPRITE_DIM;
            uint16 bottomVramY = vramY + MAX_SPRITE_DIM;

            /* Allocate buffer for bottom tile */
            uint32 bottomCopySize = (safeW * bottomH) / 2;
            uint16 *bottomBuf = (uint16*)safe_malloc(bottomCopySize);

            /* src already points to row 64 after main tile copy loop */
            uint8 *bottomSrc = src;
            uint8 *bottomDst = (uint8*)bottomBuf;

            /* Copy rows 64-77 (feet) with nibble swap */
            for (uint16 by = 0; by < bottomH; by++) {
                for (uint32 bx = 0; bx < dstRowBytes; bx++) {
                    uint8 srcByte = bottomSrc[bx];
                    bottomDst[bx] = ((srcByte & 0x0F) << 4) | ((srcByte >> 4) & 0x0F);
                }
                bottomDst += dstRowBytes;
                bottomSrc += srcRowBytes;
            }

            /* LoadImage bottom tile at SAME X column as main tile (same texture page) */
            RECT bottomRect;
            setRECT(&bottomRect, vramX, bottomVramY, vramW, bottomH);
            LoadImage(&bottomRect, (uint32*)bottomBuf);
            DrawSync(0);

            /* Create PS1Surface for bottom tile */
            PS1Surface *bottomTile = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
            bottomTile->width = safeW;
            bottomTile->height = bottomH;
            bottomTile->x = vramX;  /* Same X as main tile */
            bottomTile->y = bottomVramY;  /* Y=68 */
            bottomTile->pixels = bottomBuf;
            bottomTile->clutX = 640;
            bottomTile->clutY = 0;
            bottomTile->fullWidth = width;
            bottomTile->fullHeight = height;
            bottomTile->tileOffsetX = 0;
            bottomTile->tileOffsetY = MAX_SPRITE_DIM;
            bottomTile->nextTile = NULL;

            /* Link: main tile -> bottom tile */
            surface->nextTile = bottomTile;
        }

        /* Store in slot */
        ttmSlot->sprites[slotNo][frameIdx] = surface;

        /* Update VRAM tracking for next sprite */
        nextVRAMX += vramW;
        if (nextVRAMX >= 1024) {
            nextVRAMX = 640;
            nextVRAMY += MAX_SPRITE_DIM;
        }
    }

    ttmSlot->numSprites[slotNo] = numToLoad;
}

/*
 * Release BMP sprite sheet from slot
 */
void grReleaseBmp(struct TTtmSlot *ttmSlot, uint16 bmpSlotNo)
{
    if (ttmSlot == NULL || bmpSlotNo >= MAX_BMP_SLOTS) {
        return;
    }

    /* Free all sprites in this slot */
    for (int i = 0; i < ttmSlot->numSprites[bmpSlotNo]; i++) {
        if (ttmSlot->sprites[bmpSlotNo][i] != NULL) {
            grFreeLayer(ttmSlot->sprites[bmpSlotNo][i]);
            ttmSlot->sprites[bmpSlotNo][i] = NULL;
        }
    }

    ttmSlot->numSprites[bmpSlotNo] = 0;
}

/*
 * Load BMP sprites into RAM with 15-bit direct color (for framebuffer blitting)
 * Unlike grLoadBmp, this does NOT upload to VRAM texture area.
 * Use grBlitToFramebuffer() to draw these sprites.
 */
void grLoadBmpRAM(struct TTtmSlot *ttmSlot, uint16 slotNo, char *strArg)
{
    if (ttmSlot->numSprites[slotNo])
        grReleaseBmp(ttmSlot, slotNo);

    struct TBmpResource *bmpResource = findBmpResource(strArg);
    if (!bmpResource) return;

    /* On-demand loading: decompress BMP if not already loaded */
    if (!bmpResource->uncompressedData) {
        ps1_loadBmpData(bmpResource);
    }
    if (!bmpResource->uncompressedData) return;  /* Still NULL = load failed */
    if (bmpResource->numImages < 1) return;

    int numToLoad = bmpResource->numImages;
    if (numToLoad > MAX_SPRITES_PER_BMP) {
        numToLoad = MAX_SPRITES_PER_BMP;
    }

    uint8 *srcPtr = bmpResource->uncompressedData;

    for (int frameIdx = 0; frameIdx < numToLoad; frameIdx++) {
        uint16 width = bmpResource->widths[frameIdx];
        uint16 height = bmpResource->heights[frameIdx];

        /* Allocate PS1Surface */
        PS1Surface *surface = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
        surface->width = width;
        surface->height = height;
        surface->x = 0;  /* Not in VRAM - RAM only */
        surface->y = 0;
        surface->clutX = 0;
        surface->clutY = 0;

        /* Allocate 15-bit direct color buffer */
        uint32 pixelCount = width * height;
        surface->pixels = (uint16*)safe_malloc(pixelCount * 2);

        /* Convert 4-bit indexed to 15-bit direct color using palette */
        uint16 *dst = surface->pixels;
        for (uint32 i = 0; i < pixelCount; i++) {
            uint8 palIndex;
            if (i & 1) {
                palIndex = srcPtr[i / 2] & 0x0F;
            } else {
                palIndex = (srcPtr[i / 2] >> 4) & 0x0F;
            }
            dst[i] = ttmPalette[palIndex & 0x0F];
        }

        /* Advance source pointer for next frame */
        srcPtr += (width * height) / 2;

        /* Store in slot */
        ttmSlot->sprites[slotNo][frameIdx] = surface;
    }

    ttmSlot->numSprites[slotNo] = numToLoad;
}

/*
 * Blit a RAM-stored sprite directly to framebuffer using LoadImage
 * Sprite must have been loaded with grLoadBmpRAM (15-bit direct color)
 * NOTE: No transparency support yet - black pixels will show
 */
void grBlitToFramebuffer(PS1Surface *sprite, sint16 screenX, sint16 screenY)
{
    if (!sprite || !sprite->pixels) return;

    /* Clip to screen bounds */
    sint16 srcX = 0, srcY = 0;
    uint16 blitW = sprite->width;
    uint16 blitH = sprite->height;

    /* Left edge clipping */
    if (screenX < 0) {
        srcX = -screenX;
        blitW -= srcX;
        screenX = 0;
    }
    /* Top edge clipping */
    if (screenY < 0) {
        srcY = -screenY;
        blitH -= srcY;
        screenY = 0;
    }
    /* Right edge clipping */
    if (screenX + blitW > 640) {
        blitW = 640 - screenX;
    }
    /* Bottom edge clipping */
    if (screenY + blitH > 480) {
        blitH = 480 - screenY;
    }

    /* Nothing to draw if fully clipped */
    if (blitW <= 0 || blitH <= 0) return;

    /* If no clipping needed, direct LoadImage */
    if (srcX == 0 && srcY == 0 && blitW == sprite->width && blitH == sprite->height) {
        RECT dstRect;
        setRECT(&dstRect, screenX, screenY, sprite->width, sprite->height);
        LoadImage(&dstRect, (uint32*)sprite->pixels);
    } else {
        /* Need to copy clipped region to temp buffer */
        uint16 *tempBuf = (uint16*)malloc(blitW * blitH * 2);
        if (!tempBuf) return;
        uint16 *src = sprite->pixels;
        uint16 *dst = tempBuf;

        for (uint16 y = 0; y < blitH; y++) {
            memcpy(dst, &src[(srcY + y) * sprite->width + srcX], blitW * 2);
            dst += blitW;
        }

        RECT dstRect;
        setRECT(&dstRect, screenX, screenY, blitW, blitH);
        LoadImage(&dstRect, (uint32*)tempBuf);

        free(tempBuf);
    }
}

/*
 * Composite a RAM-stored sprite into the background tile buffers WITH TRANSPARENCY
 * Skips pixels with value 0x0000 (transparent)
 * This modifies the bgTile RAM buffers so grDrawBackground() renders with transparency
 *
 * Background tile layout:
 * - bgTile0: x=0-319, y=0-239
 * - bgTile1: x=320-639, y=0-239
 * - bgTile3: x=0-319, y=240-479
 * - bgTile4: x=320-639, y=240-479
 */
void grCompositeToBackground(PS1Surface *sprite, sint16 screenX, sint16 screenY)
{
    if (!sprite || !sprite->pixels) return;

    uint16 sprW = sprite->width;
    uint16 sprH = sprite->height;
    uint16 *sprPixels = sprite->pixels;

    /* Iterate over each pixel in the sprite */
    for (uint16 sy = 0; sy < sprH; sy++) {
        sint16 destY = screenY + sy;
        if (destY < 0 || destY >= 480) continue;

        for (uint16 sx = 0; sx < sprW; sx++) {
            sint16 destX = screenX + sx;
            if (destX < 0 || destX >= 640) continue;

            uint16 pixel = sprPixels[sy * sprW + sx];

            /* Skip transparent pixels (0x0000) */
            if (pixel == 0x0000) continue;

            /* Determine which tile and local coordinates */
            PS1Surface *tile = NULL;
            uint16 tileLocalX, tileLocalY;

            if (destY < 240) {
                /* Top row */
                tileLocalY = destY;
                if (destX < 320) {
                    tile = bgTile0;
                    tileLocalX = destX;
                } else {
                    tile = bgTile1;
                    tileLocalX = destX - 320;
                }
            } else {
                /* Bottom row */
                tileLocalY = destY - 240;
                if (destX < 320) {
                    tile = bgTile3;
                    tileLocalX = destX;
                } else {
                    tile = bgTile4;
                    tileLocalX = destX - 320;
                }
            }

            /* Write pixel to tile buffer if tile exists */
            if (tile && tile->pixels) {
                tile->pixels[tileLocalY * tile->width + tileLocalX] = pixel;
            }
        }
    }
}

/*
 * Set clipping rectangle
 */
void grSetClipZone(PS1Surface *sfc, sint16 x1, sint16 y1, sint16 x2, sint16 y2)
{
    /* Set clip rectangle in draw environment */
    draw[db].clip.x = x1;
    draw[db].clip.y = y1;
    draw[db].clip.w = x2 - x1;
    draw[db].clip.h = y2 - y1;
    PutDrawEnv(&draw[db]);
}

/*
 * Draw pixel
 */
void grDrawPixel(PS1Surface *sfc, sint16 x, sint16 y, uint8 color)
{
    /* TODO: Implement pixel drawing using GPU primitive */
    /* For now, stub */
}

/*
 * Draw line
 */
void grDrawLine(PS1Surface *sfc, sint16 x1, sint16 y1, sint16 x2, sint16 y2, uint8 color)
{
    /* Allocate from primitive buffer, not malloc */
    if (primitiveIndex[db] + sizeof(LINE_F2) > PRIMITIVE_BUFFER_SIZE) {
        return;  /* Buffer full */
    }

    LINE_F2 *line = (LINE_F2*)nextPrimitive[db];
    nextPrimitive[db] += sizeof(LINE_F2);
    primitiveIndex[db] += sizeof(LINE_F2);

    setLineF2(line);
    setXY2(line, x1, y1, x2, y2);

    /* Convert palette color to RGB */
    uint8 r = (ttmPalette[color & 0xF] & 0x1F) << 3;
    uint8 g = ((ttmPalette[color & 0xF] >> 5) & 0x1F) << 3;
    uint8 b = ((ttmPalette[color & 0xF] >> 10) & 0x1F) << 3;
    setRGB0(line, r, g, b);

    /* Add to ordering table */
    addPrim(&ot[db][0], line);
}

/*
 * Draw filled rectangle using two triangles (TILE primitive doesn't work on PS1)
 */
void grDrawRect(PS1Surface *sfc, sint16 x, sint16 y, uint16 width, uint16 height, uint8 color)
{
    /* Need space for 2 triangles */
    if (primitiveIndex[db] + 2 * sizeof(POLY_F3) > PRIMITIVE_BUFFER_SIZE) {
        return;  /* Buffer full */
    }

    /* Convert palette color to RGB */
    uint8 r = (ttmPalette[color & 0xF] & 0x1F) << 3;
    uint8 g = ((ttmPalette[color & 0xF] >> 5) & 0x1F) << 3;
    uint8 b = ((ttmPalette[color & 0xF] >> 10) & 0x1F) << 3;

    sint16 x2 = x + width;
    sint16 y2 = y + height;

    /* Top-left triangle */
    POLY_F3 *tri1 = (POLY_F3*)nextPrimitive[db];
    nextPrimitive[db] += sizeof(POLY_F3);
    primitiveIndex[db] += sizeof(POLY_F3);

    setPolyF3(tri1);
    setXY3(tri1, x, y, x2, y, x, y2);
    setRGB0(tri1, r, g, b);
    addPrim(&ot[db][0], tri1);

    /* Bottom-right triangle */
    POLY_F3 *tri2 = (POLY_F3*)nextPrimitive[db];
    nextPrimitive[db] += sizeof(POLY_F3);
    primitiveIndex[db] += sizeof(POLY_F3);

    setPolyF3(tri2);
    setXY3(tri2, x2, y, x2, y2, x, y2);
    setRGB0(tri2, r, g, b);
    addPrim(&ot[db][0], tri2);
}

/*
 * Draw circle (hollow)
 */
void grDrawCircle(PS1Surface *sfc, sint16 x1, sint16 y1, uint16 width, uint16 height,
                  uint8 fgColor, uint8 bgColor)
{
    /* TODO: Implement circle/ellipse drawing using line primitives */
    /* This requires Bresenham's ellipse algorithm */
}

/*
 * Draw sprite from BMP slot
 */
void grDrawSprite(PS1Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y,
                  uint16 spriteNo, uint16 imageNo)
{
    x += grDx;
    y += grDy;

    if (spriteNo >= ttmSlot->numSprites[imageNo]) {
        if (debugMode) {
            printf("Warning: sprite %d not found in slot %d\n", spriteNo, imageNo);
        }
        return;
    }

    PS1Surface *sprite = ttmSlot->sprites[imageNo][spriteNo];
    if (sprite == NULL) {
        return;
    }

    /* Draw all tiles in this sprite's linked list */
    PS1Surface *tile = sprite;
    while (tile != NULL) {
        /* Allocate DR_TPAGE + SPRT primitives from buffer */
        if (primitiveIndex[db] + sizeof(DR_TPAGE) + sizeof(SPRT) > PRIMITIVE_BUFFER_SIZE) {
            if (debugMode) {
                printf("Warning: Primitive buffer full!\n");
            }
            return;
        }

        /* Calculate screen position for this tile */
        sint16 tileX = x + tile->tileOffsetX;
        sint16 tileY = y + tile->tileOffsetY;

        /* Add texture page primitive first - tells GPU where texture data is */
        DR_TPAGE *tpage = (DR_TPAGE*)nextPrimitive[db];
        nextPrimitive[db] += sizeof(DR_TPAGE);
        primitiveIndex[db] += sizeof(DR_TPAGE);

        /* Calculate texture page from tile VRAM position
         * tpage X: in 64-pixel units (tile->x / 64)
         * tpage Y: in 256-pixel units (tile->y / 256)
         * Color mode: 0 = 4-bit CLUT (16 colors) */
        uint16 tpageX = tile->x / 64;
        uint16 tpageY = tile->y / 256;
        setDrawTPage(tpage, 0, 0, getTPage(0, 0, tpageX * 64, tpageY * 256));
        addPrim(&ot[db][0], tpage);

        SPRT *sprt = (SPRT*)nextPrimitive[db];
        nextPrimitive[db] += sizeof(SPRT);
        primitiveIndex[db] += sizeof(SPRT);

        /* Initialize sprite primitive */
        setSprt(sprt);
        setXY0(sprt, tileX, tileY);
        setWH(sprt, tile->width, tile->height);
        /* UV coords are relative to texture page (0-255 range)
         * For 4-bit textures: U = ((vram_x % 64) * 4) & 0xFF
         * This is because each texture page is 64 VRAM pixels = 256 texture pixels */
        setUV0(sprt, ((tile->x % 64) * 4) & 0xFF, (tile->y % 256) & 0xFF);
        setClut(sprt, tile->clutX, tile->clutY);
        setRGB0(sprt, 128, 128, 128);  /* Normal brightness */

        /* Add to ordering table */
        addPrim(&ot[db][0], sprt);

        if (debugMode) {
            printf("Draw tile: pos=(%d,%d) size=%dx%d VRAM=(%d,%d)\n",
                   tileX, tileY, tile->width, tile->height, tile->x, tile->y);
        }

        tile = tile->nextTile;
    }
}

/*
 * Draw horizontally flipped sprite
 */
void grDrawSpriteFlip(PS1Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y,
                      uint16 spriteNo, uint16 imageNo)
{
    x += grDx;
    y += grDy;

    if (spriteNo >= ttmSlot->numSprites[imageNo]) {
        if (debugMode) {
            printf("Warning: sprite %d not found in slot %d\n", spriteNo, imageNo);
        }
        return;
    }

    PS1Surface *sprite = ttmSlot->sprites[imageNo][spriteNo];
    if (sprite == NULL) {
        return;
    }

    /* Draw all tiles in this sprite's linked list (flipped) */
    PS1Surface *tile = sprite;
    while (tile != NULL) {
        /* Allocate POLY_FT4 primitive from buffer */
        /* PS1 doesn't have hardware flip, so we use textured quad with reversed UVs */
        if (primitiveIndex[db] + sizeof(POLY_FT4) > PRIMITIVE_BUFFER_SIZE) {
            if (debugMode) {
                printf("Warning: Primitive buffer full!\n");
            }
            return;
        }

        /* Calculate flipped screen position for this tile
         * For horizontal flip: tile at offsetX goes to (fullWidth - offsetX - tileWidth) */
        sint16 tileX = x + (sprite->fullWidth - tile->tileOffsetX - tile->width);
        sint16 tileY = y + tile->tileOffsetY;

        POLY_FT4 *poly = (POLY_FT4*)nextPrimitive[db];
        nextPrimitive[db] += sizeof(POLY_FT4);
        primitiveIndex[db] += sizeof(POLY_FT4);

        /* Initialize textured quad */
        setPolyFT4(poly);

        /* Set screen coordinates (normal quad, flipping happens in UV) */
        setXY4(poly,
               tileX, tileY,                              /* Top-left */
               tileX + tile->width, tileY,                /* Top-right */
               tileX, tileY + tile->height,               /* Bottom-left */
               tileX + tile->width, tileY + tile->height); /* Bottom-right */

        /* Calculate texture page from tile VRAM position (4-bit mode) */
        uint16 tpageX = tile->x / 64;
        uint16 tpageY = tile->y / 256;
        poly->tpage = getTPage(0, 0, tpageX * 64, tpageY * 256);

        /* Set UV coordinates (flipped horizontally, relative to texture page)
         * For 4-bit textures: U = ((vram_x % 64) * 4) & 0xFF */
        uint8 baseU = ((tile->x % 64) * 4) & 0xFF;
        uint8 baseV = (tile->y % 256) & 0xFF;
        uint8 u0 = baseU + tile->width;  /* Right edge */
        uint8 u1 = baseU;                 /* Left edge */
        uint8 v0 = baseV;
        uint8 v1 = baseV + tile->height;

        setUV4(poly, u0, v0, u1, v0, u0, v1, u1, v1);  /* Flipped U coords */

        setClut(poly, tile->clutX, tile->clutY);
        setRGB0(poly, 128, 128, 128);  /* Normal brightness */

        /* Add to ordering table */
        addPrim(&ot[db][0], poly);

        if (debugMode) {
            printf("Draw flipped tile: pos=(%d,%d) size=%dx%d\n",
                   tileX, tileY, tile->width, tile->height);
        }

        tile = tile->nextTile;
    }
}

/*
 * Extended sprite drawing - allows caller to provide their own OT and primitive buffer
 * Walks linked list of tiles for multi-tile sprites (sprites > 64 pixels)
 * Returns 0 on success, -1 on failure
 */
int grDrawSpriteExt(unsigned long *extOT, char **nextPri, PS1Surface *sprite, sint16 x, sint16 y)
{
    if (sprite == NULL || extOT == NULL || nextPri == NULL) {
        return -1;
    }

    x += grDx;
    y += grDy;

    /* Calculate texture page ONCE from first tile - all tiles share same tpage */
    uint16 tpageX = sprite->x / 64;
    uint16 tpageY = sprite->y / 256;

    /* Walk linked list of tiles */
    PS1Surface *tile = sprite;
    while (tile != NULL) {
        /* Screen position for this tile */
        sint16 tileX = x + tile->tileOffsetX;
        sint16 tileY = y + tile->tileOffsetY;

        /* Allocate DR_TPAGE primitive */
        DR_TPAGE *tpage = (DR_TPAGE*)(*nextPri);
        *nextPri += sizeof(DR_TPAGE);

        /* Use SAME texture page for all tiles (calculated from first tile) */
        setDrawTPage(tpage, 0, 0, getTPage(0, 0, tpageX * 64, tpageY * 256));

        /* Allocate SPRT primitive */
        SPRT *sprt = (SPRT*)(*nextPri);
        *nextPri += sizeof(SPRT);

        /* Initialize sprite primitive */
        setSprt(sprt);
        setXY0(sprt, tileX, tileY);
        setWH(sprt, tile->width, tile->height);
        /* UV coords are relative to texture page (0-255 range)
         * For 4-bit textures: U = ((vram_x % 64) * 4) & 0xFF */
        setUV0(sprt, ((tile->x % 64) * 4) & 0xFF, (tile->y % 256) & 0xFF);
        setClut(sprt, tile->clutX, tile->clutY);
        setRGB0(sprt, 128, 128, 128);  /* Normal brightness */

        /* Add to ordering table - sprt FIRST so tpage renders BEFORE it
         * (addPrim adds to HEAD, so last added = first rendered) */
        addPrim(extOT, sprt);
        addPrim(extOT, tpage);

        tile = tile->nextTile;
    }

    return 0;
}

/*
 * Initialize empty background
 */
void grInitEmptyBackground()
{
    if (grBackgroundSfc != NULL) {
        grFreeLayer(grBackgroundSfc);
    }

    grBackgroundSfc = grNewEmptyBackground();
}

/*
 * Clear screen to black
 */
void grClearScreen(PS1Surface *sfc)
{
    /* Clear entire screen using TILE primitive */
    grDrawRect(sfc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
}

/*
 * Draw background surface to screen
 * Re-LoadImages all tiles to framebuffer to restore background each frame.
 * Required when isbg=0 to erase previous frame's sprites.
 */
void grDrawBackground(void)
{
    RECT dstRect;

    /* Top row tiles (y=0-239) */
    if (bgTile0 && bgTile0->pixels) {
        setRECT(&dstRect, 0, 0, bgTile0->width, bgTile0->height);
        LoadImage(&dstRect, (uint32*)bgTile0->pixels);
    }
    if (bgTile1 && bgTile1->pixels) {
        setRECT(&dstRect, 320, 0, bgTile1->width, bgTile1->height);
        LoadImage(&dstRect, (uint32*)bgTile1->pixels);
    }

    /* Bottom row tiles (y=240-479) */
    if (bgTile3 && bgTile3->pixels) {
        setRECT(&dstRect, 0, 240, bgTile3->width, bgTile3->height);
        LoadImage(&dstRect, (uint32*)bgTile3->pixels);
    }
    if (bgTile4 && bgTile4->pixels) {
        setRECT(&dstRect, 320, 240, bgTile4->width, bgTile4->height);
        LoadImage(&dstRect, (uint32*)bgTile4->pixels);
    }

    /* Wait for DMA completion before drawing sprites */
    DrawSync(0);
}

/*
 * Fade out effect
 */
void grFadeOut()
{
    /* TODO: Implement fade effect using GPU blend modes */
}

/*
 * Helper: Create a single background tile
 * src = source image data (4-bit packed)
 * srcWidth = total width of source image
 * srcStartX = x offset in source to start copying from
 * tileWidth = width of this tile (256 or 128)
 * vramX, vramY = where to place in VRAM
 */
static PS1Surface *createBgTile(uint8 *src, uint16 srcWidth,
                                 uint16 srcStartX, uint16 tileWidth,
                                 uint16 vramX, uint16 vramY)
{
    PS1Surface *tile = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
    tile->width = tileWidth;
    tile->height = BG_TILE_HEIGHT;
    tile->x = vramX;
    tile->y = vramY;

    /* Allocate pixel buffer for 15-bit direct color */
    uint32 pixelDataSize = tileWidth * BG_TILE_HEIGHT * 2;
    tile->pixels = (uint16*)safe_malloc(pixelDataSize);

    uint16 *dst = tile->pixels;

    /* 1:1 pixel copy from source region */
    for (uint16 y = 0; y < BG_TILE_HEIGHT; y++) {
        for (uint16 x = 0; x < tileWidth; x++) {
            uint32 srcX = srcStartX + x;
            uint32 srcY = y;

            /* Source is 4-bit packed: 2 pixels per byte, high nibble first */
            uint32 srcOffset = (srcY * srcWidth + srcX) / 2;

            uint8 palIndex;
            if (srcX & 1) {
                palIndex = src[srcOffset] & 0x0F;
            } else {
                palIndex = (src[srcOffset] >> 4) & 0x0F;
            }

            dst[y * tileWidth + x] = ttmPalette[palIndex & 0x0F];
        }
    }

    /* Upload tile to VRAM */
    RECT rect;
    setRECT(&rect, tile->x, tile->y, tileWidth, BG_TILE_HEIGHT);
    LoadImage(&rect, (uint32*)tile->pixels);

    return tile;
}

/* Helper to free a tile */
static void freeBgTile(PS1Surface **tile)
{
    if (*tile != NULL) {
        if ((*tile)->pixels) free((*tile)->pixels);
        free(*tile);
        *tile = NULL;
    }
}

/*
 * Helper: Create a background tile stored in RAM only (no VRAM upload)
 * For use with LoadImage directly to framebuffer
 */
static PS1Surface *createBgTileRAM(uint8 *src, uint16 srcWidth,
                                    uint16 srcStartX, uint16 srcStartY,
                                    uint16 tileWidth)
{
    PS1Surface *tile = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
    tile->width = tileWidth;
    tile->height = BG_TILE_HEIGHT;
    tile->x = 0;  /* Not in VRAM - just RAM */
    tile->y = 0;

    /* Allocate pixel buffer for 15-bit direct color */
    uint32 pixelDataSize = tileWidth * BG_TILE_HEIGHT * 2;
    tile->pixels = (uint16*)safe_malloc(pixelDataSize);

    uint16 *dst = tile->pixels;

    /* 1:1 pixel copy from source region */
    for (uint16 y = 0; y < BG_TILE_HEIGHT; y++) {
        for (uint16 x = 0; x < tileWidth; x++) {
            uint32 srcX = srcStartX + x;
            uint32 srcY = srcStartY + y;

            /* Source is 4-bit packed: 2 pixels per byte, high nibble first */
            uint32 srcOffset = (srcY * srcWidth + srcX) / 2;

            uint8 palIndex;
            if (srcX & 1) {
                palIndex = src[srcOffset] & 0x0F;
            } else {
                palIndex = (src[srcOffset] >> 4) & 0x0F;
            }

            dst[y * tileWidth + x] = ttmPalette[palIndex & 0x0F];
        }
    }

    /* Don't upload to VRAM - keep in RAM for LoadImage to framebuffer */
    return tile;
}

/*
 * Load background screen
 */
void grLoadScreen(char *strArg)
{
    /* Free existing tiles */
    freeBgTile(&bgTile0);
    freeBgTile(&bgTile1);
    freeBgTile(&bgTile2a);
    freeBgTile(&bgTile2b);
    freeBgTile(&bgTile3);
    freeBgTile(&bgTile4);
    freeBgTile(&bgTile5a);
    freeBgTile(&bgTile5b);
    grBackgroundSfc = NULL;  /* Points to bgTile0, already freed */

    if (grSavedZonesLayer != NULL) {
        grFreeLayer(grSavedZonesLayer);
        grSavedZonesLayer = NULL;
    }

    struct TScrResource *scrResource = findScrResource(strArg);

    /* Handle lazy loading - reload from extracted file if needed */
    if (scrResource->uncompressedData == NULL) {
        /* PS1 TODO: Use CD-ROM functions to reload from disc if needed */
        /* For now, fatal error if data was freed */
        fatalError("SCR data freed - PS1 CD-ROM reloading not yet implemented");
    }

    if ((scrResource->width % 2) == 1) {
        if (debugMode) {
            printf("Warning: grLoadScreen(): can't manage odd widths\n");
        }
    }

    if (scrResource->width > 640 || scrResource->height > 480) {
        fatalError("grLoadScreen(): can't manage more than 640x480 resolutions");
    }

    uint16 srcWidth  = scrResource->width;
    uint8 *src = scrResource->uncompressedData;

    /* Create tiles for top row (y=0-239)
     * VRAM layout:
     * - Tile 0  (256x240) at VRAM(640, 4)   - srcX=0,   y=4-243
     * - Tile 1  (256x240) at VRAM(640, 244) - srcX=256, y=244-483
     * DEBUG: Test single 64px tile at x=896 to isolate VRAM issue
     */
    /* Top row: LoadImage directly to framebuffer at init
     * Use 2 tiles of 320px each to cover 640px total */
    bgTile0  = createBgTileRAM(src, srcWidth, 0,   0, 320);   /* top row, x=0-319 */
    bgTile1  = createBgTileRAM(src, srcWidth, 320, 0, 320);   /* top row, x=320-639 */
    bgTile2a = NULL;
    bgTile2b = NULL;

    /* LoadImage top row directly to framebuffer */
    RECT topRect;
    if (bgTile0 && bgTile0->pixels) {
        setRECT(&topRect, 0, 0, bgTile0->width, bgTile0->height);
        LoadImage(&topRect, (uint32*)bgTile0->pixels);
    }
    if (bgTile1 && bgTile1->pixels) {
        setRECT(&topRect, 320, 0, bgTile1->width, bgTile1->height);
        LoadImage(&topRect, (uint32*)bgTile1->pixels);
    }
    DrawSync(0);

    /* Bottom row: Only create if SCR has enough lines
     * Many SCR files are 640x350, not 640x480 - must check actual height */
    uint16 srcHeight = scrResource->height;

    /* Debug: Print SCR dimensions */
    printf("SCR: %s - %dx%d (uncompressed: %u bytes)\n",
           scrResource->resName, srcWidth, srcHeight, scrResource->uncompressedSize);

    /* Calculate how many lines we can read for bottom half (y=240+) */
    uint16 bottomRowLines = (srcHeight > 240) ? (srcHeight - 240) : 0;

    if (bottomRowLines >= 240) {
        /* Full 640x480 image - create bottom row tiles (2x320 to match top row) */
        bgTile3  = createBgTileRAM(src, srcWidth, 0,   240, 320);
        bgTile4  = createBgTileRAM(src, srcWidth, 320, 240, 320);
        bgTile5a = NULL;
        bgTile5b = NULL;
    } else if (bottomRowLines > 0) {
        /* Partial bottom row (e.g., 640x350 = 110 lines for bottom)
         * For now, leave bottom row NULL - shows black
         * TODO: Create partial-height tiles to show available lines */
        printf("SCR has only %d lines for bottom half (need 240)\n", bottomRowLines);
        bgTile3  = NULL;
        bgTile4  = NULL;
        bgTile5a = NULL;
        bgTile5b = NULL;
    } else {
        /* SCR is only 240 lines or less - no bottom row data at all */
        bgTile3  = NULL;
        bgTile4  = NULL;
        bgTile5a = NULL;
        bgTile5b = NULL;
    }

    DrawSync(0);  /* Sync top row uploads */

    /* Disable display during bottom row LoadImage to avoid tearing/corruption */
    SetDispMask(0);

    /* LoadImage bottom row tiles directly to framebuffer (2x320 layout) */
    RECT dstRect;

    if (bgTile3 && bgTile3->pixels) {
        setRECT(&dstRect, 0, 240, bgTile3->width, bgTile3->height);
        LoadImage(&dstRect, (uint32*)bgTile3->pixels);
    }
    if (bgTile4 && bgTile4->pixels) {
        setRECT(&dstRect, 320, 240, bgTile4->width, bgTile4->height);
        LoadImage(&dstRect, (uint32*)bgTile4->pixels);
    }

    DrawSync(0);  /* Sync bottom row uploads */

    /* Re-enable display */
    SetDispMask(1);

    /* Set grBackgroundSfc to first tile for compatibility with existing code */
    grBackgroundSfc = bgTile0;

    /* Free SCR data after converting - saves memory */
    if (scrResource->uncompressedData) {
        free(scrResource->uncompressedData);
        scrResource->uncompressedData = NULL;
        if (debugMode) {
            printf("Freed SCR data for %s (%u bytes)\n",
                   scrResource->resName, scrResource->uncompressedSize);
        }
    }
}

/*
 * Copy zone operations - stubbed for now
 */
void grCopyZoneToBg(PS1Surface *sfc, uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3) {}
void grSaveImage1(PS1Surface *sfc, uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3) {}
void grSaveZone(PS1Surface *sfc, uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3) {}
void grRestoreZone(PS1Surface *sfc, uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3) {}

/*
 * Frame capture (not implemented on PS1)
 */
int grCaptureFrame(const char *filename)
{
    /* Frame capture not supported on PS1 hardware */
    return -1;
}
