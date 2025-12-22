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
static uint8 *primitiveBuffer[2];  /* Malloc'd, not static array! */
static uint32 primitiveIndex[2];
static uint8 *nextPrimitive[2];

/* PS1 Display and drawing environments */
static DISPENV disp[2];
static DRAWENV draw[2];
static int db = 0;  /* Double buffer index */

/* Ordering tables for GPU command queueing */
#define OT_LENGTH 8
static unsigned long ot[2][OT_LENGTH];

/* Palette (16 colors, matching original TTM format) */
static uint16 ttmPalette[16];

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

/* Pre-allocated tile buffers (allocated in graphicsInit where malloc works) */
PS1Surface *preallocTile0 = NULL;
PS1Surface *preallocTile1 = NULL;
uint16 *preallocPixels0 = NULL;  /* 320x240 = 153,600 bytes */
uint16 *preallocPixels1 = NULL;  /* 320x240 = 153,600 bytes */

/* Single static pixel buffer for chunked background rendering
 * Process 64px wide strips, upload immediately, reuse buffer for next strip
 * This avoids the ~60KB static buffer limit */
#define BG_STRIP_WIDTH 64
static uint16 bgStripBuffer[BG_STRIP_WIDTH * 240];  /* 64x240 = 30,720 bytes */

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

/* VRAM allocation tracking
 * VRAM Layout for 640x480 interlaced:
 * (0,0)-(639,479): Framebuffer (single buffer for 640x480)
 * (640,0)-(656,1): CLUT (16 colors)
 * (640,2)-(895,2): CLUT 256 (grayscale)
 * (640,4) onwards: Textures
 *
 * IMPORTANT: For 4-bit textures, VRAM width = pixel_width / 4
 * because each 16-bit VRAM word holds 4 pixels (4 bits each)
 */
static uint16 nextVRAMX = 640;  /* Start to the right of framebuffer */
static uint16 nextVRAMY = 4;    /* Below CLUTs */

/* Test sprite for debugging VRAM uploads */
static PS1Surface *testSprite = NULL;

/*
 * Initialize PS1 graphics subsystem
 */
void graphicsInit()
{
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

    /* Don't clear background - we use LoadImage to write directly to framebuffer
     * In single-buffer 640x480 mode, we want to preserve the background */
    setRGB0(&draw[0], 0, 0, 0);
    setRGB0(&draw[1], 0, 0, 0);
    draw[0].isbg = 0;  /* Don't clear - background is LoadImage'd */
    draw[1].isbg = 0;

    if (debugMode)
        printf("GPU: Enabling display...\n");

    /* Enable display */
    SetDispMask(1);

    /* Apply first buffer */
    PutDispEnv(&disp[db]);
    PutDrawEnv(&draw[db]);

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

    /* Note: Pre-allocation removed - using static bgStripBuffer for chunked rendering */

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
 * Test function: Create and upload a simple 64x64 test sprite to VRAM
 * Returns 1 on success, 0 on failure/hang
 * Call this to verify LoadImage works for texture area
 */
int grTestSpriteUpload(void)
{
    /* Create a larger 64x64 4-bit test pattern for visibility */
    uint16 testWidth = 64;
    uint16 testHeight = 64;

    /* 4-bit = 2 pixels per byte, so 64x64 = 2048 bytes */
    uint32 dataSize = (testWidth * testHeight) / 2;
    uint8 *testData = (uint8*)malloc(dataSize);
    if (!testData) return 0;

    /* Fill with solid WHITE (palette index 7) for maximum visibility */
    for (uint32 i = 0; i < dataSize; i++) {
        testData[i] = 0x77;  /* Both nibbles = color 7 (white) */
    }

    /* Allocate surface structure */
    testSprite = (PS1Surface*)malloc(sizeof(PS1Surface));
    if (!testSprite) {
        free(testData);
        return 0;
    }

    testSprite->width = testWidth;
    testSprite->height = testHeight;
    testSprite->x = 640;  /* Texture area start */
    testSprite->y = 4;    /* Below CLUTs */
    testSprite->clutX = 640;
    testSprite->clutY = 0;
    testSprite->pixels = (uint16*)testData;

    /* KEY: For 4-bit textures, VRAM width = pixel_width / 4 */
    RECT texRect;
    uint16 vramWidth = testWidth / 4;  /* 64 pixels / 4 = 16 VRAM units */
    setRECT(&texRect, testSprite->x, testSprite->y, vramWidth, testHeight);

    /* Try the upload */
    LoadImage(&texRect, (uint32*)testData);
    DrawSync(0);  /* Wait for completion */

    return 1;  /* Success if we get here */
}

/*
 * Draw the test sprite at given position using POLY_FT4 (textured quad)
 * This gives more explicit control over texture coordinates
 */
void grDrawTestSprite(int x, int y)
{
    if (!testSprite) return;

    /* Allocate POLY_FT4 primitive */
    if (primitiveIndex[db] + sizeof(POLY_FT4) > PRIMITIVE_BUFFER_SIZE) {
        return;
    }

    POLY_FT4 *poly = (POLY_FT4*)nextPrimitive[db];
    nextPrimitive[db] += sizeof(POLY_FT4);
    primitiveIndex[db] += sizeof(POLY_FT4);

    setPolyFT4(poly);

    /* Screen coordinates - quad corners */
    int w = testSprite->width;
    int h = testSprite->height;
    setXY4(poly,
           x,     y,      /* Top-left */
           x + w, y,      /* Top-right */
           x,     y + h,  /* Bottom-left */
           x + w, y + h); /* Bottom-right */

    /* Texture page: 4-bit mode (0), at VRAM X=640
     * getTPage(mode, abr, x, y): mode=0 for 4-bit, x/y in VRAM coords */
    poly->tpage = getTPage(0, 0, 640, 0);

    /* CLUT position */
    poly->clut = getClut(testSprite->clutX, testSprite->clutY);

    /* UV coordinates within texture page
     * Texture is at VRAM (640,4), tpage starts at (640,0)
     * So U=0, V=4 for top-left corner
     * UV coords are 0-indexed, so 64-pixel texture uses 0-63 */
    setUV4(poly,
           0, 4,           /* Top-left UV */
           63, 4,          /* Top-right UV */
           0, 4 + 63,      /* Bottom-left UV */
           63, 4 + 63);    /* Bottom-right UV */

    setRGB0(poly, 128, 128, 128);  /* Normal brightness */

    addPrim(&ot[db][0], poly);
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

    /* Convert VGA 6-bit RGB to PS1 15-bit RGB (5-5-5) */
    for (int i = 0; i < 16; i++) {
        uint8 r = (palResource->colors[i].r << 2) >> 3;  /* 6-bit to 5-bit */
        uint8 g = (palResource->colors[i].g << 2) >> 3;
        uint8 b = (palResource->colors[i].b << 2) >> 3;
        ttmPalette[i] = (b << 10) | (g << 5) | r;
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
    if (sfc != NULL) {
        /* VRAM is managed globally, just free the structure */
        free(sfc);
    }
}

/*
 * Load BMP sprite sheet into slot
 * Uploads 4-bit indexed textures to VRAM for GPU rendering.
 */
/* Static buffer for BMP sprite upload (avoids malloc issues) */
/* Max sprite size: 256x256 at 4-bit = 32KB */
/* Must be uint32 aligned for DMA transfers to work correctly */
#define MAX_SPRITE_UPLOAD_SIZE (256 * 256 / 2)
static uint32 spriteUploadBuffer32[MAX_SPRITE_UPLOAD_SIZE / 4];
#define spriteUploadBuffer ((uint8*)spriteUploadBuffer32)

/* NOTE: We use malloc() for PS1Surface structs instead of a static pool
 * due to a compiler/toolchain bug where storing pointers from static arrays
 * to ttmSlot->sprites causes hangs. malloc'd pointers work correctly. */

/* Global VRAM tracking for sprite textures */
/* Sprites go at X=640+ to avoid framebuffer (0-639), Y=1+ to avoid CLUT at Y=0 */
static uint16 spriteVRAMX = 640;
static uint16 spriteVRAMY = 1;

void grLoadBmp(struct TTtmSlot *ttmSlot, uint16 slotNo, char *strArg)
{
    if (ttmSlot->numSprites[slotNo])
        grReleaseBmp(ttmSlot, slotNo);

    struct TBmpResource *bmpResource = findBmpResource(strArg);

    /* Handle lazy loading - return if data not available */
    if (bmpResource == NULL || bmpResource->uncompressedData == NULL) {
        return;
    }

    uint8 *inPtr = bmpResource->uncompressedData;
    int numImages = bmpResource->numImages;

    /* Limit to 50 sprites per BMP */
    if (numImages > 50) numImages = 50;
    ttmSlot->numSprites[slotNo] = numImages;

    for (int image = 0; image < numImages; image++) {
        uint16 width  = bmpResource->widths[image];
        uint16 height = bmpResource->heights[image];
        uint32 pixelDataSize = (width * height) / 2;
        uint16 vramWidth = width / 4;

        /* Skip invalid sprites */
        if ((width % 2) == 1 || width > 256 || height > 256 || vramWidth == 0) {
            inPtr += pixelDataSize;
            ttmSlot->sprites[slotNo][image] = NULL;
            continue;
        }

        /* Allocate surface struct with malloc (static pool causes compiler bug) */
        PS1Surface *sfc = (PS1Surface*)malloc(sizeof(PS1Surface));
        if (!sfc) {
            inPtr += pixelDataSize;
            ttmSlot->sprites[slotNo][image] = NULL;
            continue;
        }
        sfc->width = width;
        sfc->height = height;
        sfc->pixels = NULL;

        /* Store malloc'd pointer */
        ttmSlot->sprites[slotNo][image] = sfc;

        /* Copy and upload */
        if (pixelDataSize <= MAX_SPRITE_UPLOAD_SIZE) {
            for (uint32 i = 0; i < pixelDataSize; i++) {
                spriteUploadBuffer[i] = inPtr[i];
            }

            /* Check if sprite fits in current row */
            if (spriteVRAMX + vramWidth > 1024) {
                spriteVRAMX = 640;
                spriteVRAMY += 128;
            }

            /* Check VRAM bounds - stop if out of space */
            if (spriteVRAMY + height > 512) {
                /* VRAM full, skip remaining sprites */
                free(sfc);
                inPtr += pixelDataSize;
                ttmSlot->sprites[slotNo][image] = NULL;
                continue;
            }

            /* Upload to tracked VRAM position */
            RECT spriteRect;
            setRECT(&spriteRect, spriteVRAMX, spriteVRAMY, vramWidth, height);
            LoadImage(&spriteRect, (uint32*)spriteUploadBuffer);
            DrawSync(0);

            /* Store VRAM position in surface */
            sfc->x = spriteVRAMX;
            sfc->y = spriteVRAMY;
            /* CLUT is at (640, 0) as loaded in grLoadPalette */
            sfc->clutX = 640;
            sfc->clutY = 0;

            /* Update position for next sprite */
            spriteVRAMX += vramWidth;
        }

        inPtr += pixelDataSize;
    }

    /* Free BMP uncompressed data after uploading - saves memory */
    if (bmpResource->uncompressedData) {
        free(bmpResource->uncompressedData);
        bmpResource->uncompressedData = NULL;
    }
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
 * Draw filled rectangle
 */
void grDrawRect(PS1Surface *sfc, sint16 x, sint16 y, uint16 width, uint16 height, uint8 color)
{
    /* Allocate from primitive buffer, not malloc */
    if (primitiveIndex[db] + sizeof(TILE) > PRIMITIVE_BUFFER_SIZE) {
        return;  /* Buffer full */
    }

    TILE *tile = (TILE*)nextPrimitive[db];
    nextPrimitive[db] += sizeof(TILE);
    primitiveIndex[db] += sizeof(TILE);

    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, width, height);

    /* Convert palette color to RGB - multiply by 8 to scale 5-bit to 8-bit range */
    uint8 r = (ttmPalette[color & 0xF] & 0x1F) << 3;
    uint8 g = ((ttmPalette[color & 0xF] >> 5) & 0x1F) << 3;
    uint8 b = ((ttmPalette[color & 0xF] >> 10) & 0x1F) << 3;
    setRGB0(tile, r, g, b);

    /* Add to ordering table */
    addPrim(&ot[db][0], tile);
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

    /* Allocate DR_TPAGE + SPRT primitives from buffer */
    if (primitiveIndex[db] + sizeof(DR_TPAGE) + sizeof(SPRT) > PRIMITIVE_BUFFER_SIZE) {
        if (debugMode) {
            printf("Warning: Primitive buffer full!\n");
        }
        return;
    }

    /* Add texture page primitive first - tells GPU where texture data is */
    DR_TPAGE *tpage = (DR_TPAGE*)nextPrimitive[db];
    nextPrimitive[db] += sizeof(DR_TPAGE);
    primitiveIndex[db] += sizeof(DR_TPAGE);

    /* Calculate texture page from sprite VRAM position
     * For 4-bit mode: tpage X is in 64-VRAM-word boundaries (= 256 pixels)
     * sprite->x is VRAM X coordinate */
    uint16 tpageBaseX = (sprite->x / 64) * 64;  /* Align to 64-word boundary */
    uint16 tpageBaseY = (sprite->y / 256) * 256;
    setDrawTPage(tpage, 0, 0, getTPage(0, 0, tpageBaseX, tpageBaseY));
    addPrim(&ot[db][0], tpage);

    SPRT *sprt = (SPRT*)nextPrimitive[db];
    nextPrimitive[db] += sizeof(SPRT);
    primitiveIndex[db] += sizeof(SPRT);

    /* Calculate UV coordinates
     * For 4-bit textures: each VRAM word = 4 texture pixels
     * U = (sprite->x - tpageBaseX) * 4 = pixel offset within tpage
     * V = sprite->y - tpageBaseY */
    uint8 u = ((sprite->x - tpageBaseX) * 4) & 0xFF;
    uint8 v = (sprite->y - tpageBaseY) & 0xFF;

    /* Initialize sprite primitive */
    setSprt(sprt);
    setXY0(sprt, x, y);
    setWH(sprt, sprite->width, sprite->height);
    setUV0(sprt, u, v);
    setClut(sprt, sprite->clutX, sprite->clutY);
    setRGB0(sprt, 128, 128, 128);  /* Normal brightness */

    /* Add to ordering table */
    addPrim(&ot[db][0], sprt);

    if (debugMode) {
        printf("Draw sprite: pos=(%d,%d) size=%dx%d VRAM=(%d,%d)\n",
               x, y, sprite->width, sprite->height, sprite->x, sprite->y);
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

    /* Allocate POLY_FT4 primitive from buffer */
    /* PS1 doesn't have hardware flip, so we use textured quad with reversed UVs */
    if (primitiveIndex[db] + sizeof(POLY_FT4) > PRIMITIVE_BUFFER_SIZE) {
        if (debugMode) {
            printf("Warning: Primitive buffer full!\n");
        }
        return;
    }

    POLY_FT4 *poly = (POLY_FT4*)nextPrimitive[db];
    nextPrimitive[db] += sizeof(POLY_FT4);
    primitiveIndex[db] += sizeof(POLY_FT4);

    /* Initialize textured quad */
    setPolyFT4(poly);

    /* Set screen coordinates (normal quad, flipping happens in UV) */
    setXY4(poly,
           x, y,                                    /* Top-left */
           x + sprite->width, y,                    /* Top-right */
           x, y + sprite->height,                   /* Bottom-left */
           x + sprite->width, y + sprite->height);  /* Bottom-right */

    /* Calculate texture page from sprite VRAM position (4-bit mode) */
    uint16 tpageX = sprite->x / 64;
    uint16 tpageY = sprite->y / 256;
    poly->tpage = getTPage(0, 0, tpageX * 64, tpageY * 256);

    /* Set UV coordinates (flipped horizontally, relative to texture page) */
    uint8 baseU = (sprite->x % 256) & 0xFF;
    uint8 baseV = (sprite->y % 256) & 0xFF;
    uint8 u0 = baseU + sprite->width;  /* Right edge */
    uint8 u1 = baseU;                   /* Left edge */
    uint8 v0 = baseV;
    uint8 v1 = baseV + sprite->height;

    setUV4(poly, u0, v0, u1, v0, u0, v1, u1, v1);  /* Flipped U coords */

    setClut(poly, sprite->clutX, sprite->clutY);
    setRGB0(poly, 128, 128, 128);  /* Normal brightness */

    /* Add to ordering table */
    addPrim(&ot[db][0], poly);

    if (debugMode) {
        printf("Draw flipped sprite: pos=(%d,%d) size=%dx%d\n",
               x, y, sprite->width, sprite->height);
    }
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
 * Both top and bottom rows are LoadImage'd directly to framebuffer at init.
 * In single-buffer mode (640x480), the data persists - nothing to do here.
 */
void grDrawBackground(void)
{
    /* Background was written once at init via LoadImage to framebuffer.
     * In single-buffer mode, it persists - no need to redraw each frame. */
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
/*
 * Helper: Convert a vertical strip of SCR data and upload directly to framebuffer
 * This is the chunked rendering approach - process 64px wide strips one at a time
 */
static void convertAndUploadStrip(uint8 *src, uint16 srcWidth, uint16 srcHeight,
                                   uint16 srcStartX, uint16 srcStartY,
                                   uint16 dstX, uint16 dstY,
                                   uint16 stripWidth, uint16 stripHeight)
{
    uint16 *dst = bgStripBuffer;

    /* Clamp strip dimensions to source bounds */
    if (srcStartX + stripWidth > srcWidth) {
        stripWidth = srcWidth - srcStartX;
    }
    if (srcStartY + stripHeight > srcHeight) {
        stripHeight = srcHeight - srcStartY;
    }
    if (stripWidth == 0 || stripHeight == 0) return;

    /* Convert 4-bit indexed to 15-bit color */
    for (uint16 y = 0; y < stripHeight; y++) {
        for (uint16 x = 0; x < stripWidth; x++) {
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

            dst[y * stripWidth + x] = ttmPalette[palIndex & 0x0F];
        }
    }

    /* Upload strip directly to framebuffer */
    RECT rect;
    setRECT(&rect, dstX, dstY, stripWidth, stripHeight);
    LoadImage(&rect, (uint32*)bgStripBuffer);
    DrawSync(0);
}

/* Note: createBgTileRAM removed - using chunked strip rendering instead */

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

    /* DEBUG: Skip search entirely - just use first SCR resource */
    extern struct TScrResource *scrResources[];
    extern int numScrResources;
    if (numScrResources == 0 || !scrResources[0]) return;
    struct TScrResource *scrResource = scrResources[0];

    /* Handle lazy loading - reload from extracted file if needed */
    if (scrResource->uncompressedData == NULL) {
        /* PS1 TODO: Use CD-ROM functions to reload from disc if needed */
        /* For now, just return */
        return;
    }

    if ((scrResource->width % 2) == 1) {
        /* Odd widths not supported */
    }

    if (scrResource->width > 640 || scrResource->height > 480) {
        return;  /* Too large */
    }

    uint16 srcWidth  = scrResource->width;
    uint16 srcHeight = scrResource->height;
    uint8 *src = scrResource->uncompressedData;

    /* Clear tile pointers - we're using chunked rendering instead */
    bgTile0 = NULL;
    bgTile1 = NULL;
    bgTile2a = NULL;
    bgTile2b = NULL;
    bgTile3 = NULL;
    bgTile4 = NULL;
    bgTile5a = NULL;
    bgTile5b = NULL;
    grBackgroundSfc = NULL;

    /* Chunked rendering: Process 64px wide vertical strips
     * Upload each strip directly to framebuffer, then reuse buffer
     * This avoids large static buffer requirements */

    DrawSync(0);

    /* Render top half (y=0-239) in 64px wide strips */
    uint16 numStrips = (srcWidth + BG_STRIP_WIDTH - 1) / BG_STRIP_WIDTH;
    uint16 topHeight = (srcHeight > 240) ? 240 : srcHeight;

    for (uint16 strip = 0; strip < numStrips && strip < 10; strip++) {
        uint16 stripX = strip * BG_STRIP_WIDTH;
        uint16 stripW = BG_STRIP_WIDTH;
        if (stripX + stripW > srcWidth) {
            stripW = srcWidth - stripX;
        }
        convertAndUploadStrip(src, srcWidth, srcHeight,
                              stripX, 0,      /* source x, y */
                              stripX, 0,      /* dest x, y */
                              stripW, topHeight);
    }

    /* Render bottom half (y=240-479) if source is tall enough */
    if (srcHeight > 240) {
        uint16 bottomHeight = srcHeight - 240;
        if (bottomHeight > 240) bottomHeight = 240;

        for (uint16 strip = 0; strip < numStrips && strip < 10; strip++) {
            uint16 stripX = strip * BG_STRIP_WIDTH;
            uint16 stripW = BG_STRIP_WIDTH;
            if (stripX + stripW > srcWidth) {
                stripW = srcWidth - stripX;
            }
            convertAndUploadStrip(src, srcWidth, srcHeight,
                                  stripX, 240,      /* source x, y */
                                  stripX, 240,      /* dest x, y */
                                  stripW, bottomHeight);
        }
    }

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
