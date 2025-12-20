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
 */
static uint16 nextVRAMX = 640;  /* Start to the right of framebuffer */
static uint16 nextVRAMY = 4;    /* Below CLUTs */

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

    /* Set background clear color */
    setRGB0(&draw[0], 0, 0, 0);
    setRGB0(&draw[1], 0, 0, 0);
    draw[0].isbg = 1;  /* Enable background clear */
    draw[1].isbg = 1;

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
 */
void grLoadBmp(struct TTtmSlot *ttmSlot, uint16 slotNo, char *strArg)
{
    if (ttmSlot->numSprites[slotNo])
        grReleaseBmp(ttmSlot, slotNo);

    struct TBmpResource *bmpResource = findBmpResource(strArg);

    /* Handle lazy loading - reload from extracted file if needed */
    if (bmpResource->uncompressedData == NULL) {
        /* PS1 TODO: Use CD-ROM functions to reload from disc if needed */
        /* For now, fatal error if data was freed */
        fatalError("BMP data freed - PS1 CD-ROM reloading not yet implemented");
    }

    uint8 *inPtr = bmpResource->uncompressedData;

    ttmSlot->numSprites[slotNo] = bmpResource->numImages;

    for (int image=0; image < bmpResource->numImages; image++) {

        if ((bmpResource->widths[image] % 2) == 1)
            fatalError("grLoadBmp(): can't manage odd widths");

        uint16 width  = bmpResource->widths[image];
        uint16 height = bmpResource->heights[image];

        /* Allocate PS1Surface structure */
        PS1Surface *surface = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
        surface->width = width;
        surface->height = height;

        /* Allocate VRAM position for this sprite */
        surface->x = nextVRAMX;
        surface->y = nextVRAMY;

        /* Allocate pixel buffer for 4-bit indexed data */
        /* PS1 uses 4-bit textures, so we keep the data as-is (packed nibbles) */
        uint32 pixelDataSize = (width * height) / 2;  /* 4-bit = 0.5 bytes per pixel */
        surface->pixels = (uint16*)safe_malloc(pixelDataSize);

        /* Copy packed 4-bit data directly */
        memcpy(surface->pixels, inPtr, pixelDataSize);
        inPtr += pixelDataSize;

        /* Upload texture to VRAM using DMA */
        RECT rect;
        setRECT(&rect, surface->x, surface->y, width / 4, height);  /* Width in 16-bit units for 4-bit */
        LoadImage(&rect, (uint32*)surface->pixels);

        /* Set CLUT position (color lookup table) */
        /* For now, use a fixed position - we'll upload palette here */
        surface->clutX = 640;
        surface->clutY = 0;  /* Right of framebuffer */

        /* Store sprite in slot */
        ttmSlot->sprites[slotNo][image] = surface;

        /* Update VRAM allocation tracking */
        nextVRAMX += width;
        if (nextVRAMX >= 1024) {  /* VRAM width limit */
            nextVRAMX = 0;
            nextVRAMY += height;
        }

        if (debugMode) {
            printf("Loaded sprite %d: %dx%d at VRAM(%d,%d)\n",
                   image, width, height, surface->x, surface->y);
        }
    }

    /* Free BMP data after converting to PS1 surfaces - saves memory */
    if (bmpResource->uncompressedData) {
        free(bmpResource->uncompressedData);
        bmpResource->uncompressedData = NULL;
        if (debugMode) {
            printf("Freed BMP data for %s (%u bytes)\n",
                   bmpResource->resName, bmpResource->uncompressedSize);
        }
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

    /* Allocate SPRT primitive from buffer */
    if (primitiveIndex[db] + sizeof(SPRT) > PRIMITIVE_BUFFER_SIZE) {
        if (debugMode) {
            printf("Warning: Primitive buffer full!\n");
        }
        return;
    }

    SPRT *sprt = (SPRT*)nextPrimitive[db];
    nextPrimitive[db] += sizeof(SPRT);
    primitiveIndex[db] += sizeof(SPRT);

    /* Initialize sprite primitive */
    setSprt(sprt);
    setXY0(sprt, x, y);
    setWH(sprt, sprite->width, sprite->height);
    setUV0(sprt, sprite->x & 0xFF, sprite->y & 0xFF);  /* Texture coords in VRAM (8-bit) */
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

    /* Set UV coordinates (flipped horizontally) */
    uint8 u0 = sprite->x + sprite->width;  /* Right edge */
    uint8 u1 = sprite->x;                   /* Left edge */
    uint8 v0 = sprite->y;
    uint8 v1 = sprite->y + sprite->height;

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
 * Copies texture data directly to framebuffer using LoadImage (CPU blit)
 * This avoids PS1 GPU polygon size limitations
 */
void grDrawBackground(void)
{
    if (grBackgroundSfc == NULL || grBackgroundSfc->pixels == NULL) {
        return;
    }

    /* Copy the scaled texture directly to framebuffer at (0,0)
     * The texture is 256x240, we need to tile/scale it to fill 640x480
     * For now, just draw it at 1:1 in the top-left and tile */

    RECT srcRect, dstRect;

    /* Draw single tile at top-left (1x1 grid for testing) */
    for (int ty = 0; ty < 1; ty++) {
        for (int tx = 0; tx < 1; tx++) {
            /* Calculate destination position */
            int dstX = tx * grBackgroundSfc->width;
            int dstY = ty * grBackgroundSfc->height;

            /* Clip to screen bounds */
            int copyW = grBackgroundSfc->width;
            int copyH = grBackgroundSfc->height;
            if (dstX + copyW > 640) copyW = 640 - dstX;
            if (dstY + copyH > 480) copyH = 480 - dstY;
            if (copyW <= 0 || copyH <= 0) continue;

            /* Copy from texture VRAM location to framebuffer */
            setRECT(&srcRect, grBackgroundSfc->x, grBackgroundSfc->y, copyW, copyH);
            setRECT(&dstRect, dstX, dstY, copyW, copyH);
            MoveImage(&srcRect, dstX, dstY);
        }
    }
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
 * Load background screen
 */
void grLoadScreen(char *strArg)
{
    if (grBackgroundSfc != NULL) {
        grFreeLayer(grBackgroundSfc);
        grBackgroundSfc = NULL;
    }

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
    uint16 srcHeight = scrResource->height;

    /* Use 256x240 texture (back to working size) */
    uint16 dstWidth  = 256;
    uint16 dstHeight = 240;

    /* Allocate PS1Surface for background */
    grBackgroundSfc = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
    grBackgroundSfc->width = dstWidth;
    grBackgroundSfc->height = dstHeight;
    grBackgroundSfc->x = nextVRAMX;
    grBackgroundSfc->y = nextVRAMY;

    /* Convert to 15-bit direct color (no CLUT needed - simplest approach) */
    uint32 dstPixelDataSize = dstWidth * dstHeight * 2;  /* 16-bit = 2 bytes per pixel */
    grBackgroundSfc->pixels = (uint16*)safe_malloc(dstPixelDataSize);

    uint8 *src = scrResource->uncompressedData;
    uint16 *dst = grBackgroundSfc->pixels;

    /* Calculate scale factors (fixed point 8.8) */
    uint32 xScale = (srcWidth << 8) / dstWidth;   /* src pixels per dst pixel * 256 */
    uint32 yScale = (srcHeight << 8) / dstHeight;

    /* Convert indexed pixels to 15-bit using palette lookup */
    for (uint16 y = 0; y < dstHeight; y++) {
        uint32 srcY = (y * yScale) >> 8;
        for (uint16 x = 0; x < dstWidth; x++) {
            /* Get source pixel position */
            uint32 srcX = (x * xScale) >> 8;

            /* Source is 4-bit packed: 2 pixels per byte, high nibble first */
            uint32 srcOffset = (srcY * srcWidth + srcX) / 2;

            uint8 palIndex;
            if (srcX & 1) {
                palIndex = src[srcOffset] & 0x0F;  /* Low nibble */
            } else {
                palIndex = (src[srcOffset] >> 4) & 0x0F;  /* High nibble */
            }

            /* Look up actual palette color - ttmPalette is already in BGR555 format */
            dst[y * dstWidth + x] = ttmPalette[palIndex & 0x0F];
        }
    }

    /* Upload background to VRAM using DMA
     * For 15-bit textures: 1 pixel per 16-bit VRAM word */
    RECT rect;
    uint16 vramWidth = dstWidth;  /* VRAM width in 16-bit units = pixels */
    setRECT(&rect, grBackgroundSfc->x, grBackgroundSfc->y, vramWidth, dstHeight);
    LoadImage(&rect, (uint32*)grBackgroundSfc->pixels);

    /* Wait for DMA transfer to complete */
    DrawSync(0);

    /* Set CLUT position - use 16-color palette at (640, 0) */
    grBackgroundSfc->clutX = 640;
    grBackgroundSfc->clutY = 0;

    /* Update VRAM allocation (use actual VRAM width) */
    nextVRAMX += vramWidth;
    if (nextVRAMX >= 1024) {
        nextVRAMX = 320;  /* Reset to right of framebuffers */
        nextVRAMY += dstHeight;
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
