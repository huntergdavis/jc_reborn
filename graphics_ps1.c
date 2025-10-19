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
#include <psxgpu.h>
#include <psxgte.h>
#include <psxapi.h>

#include "mytypes.h"
#include "utils.h"
#include "graphics_ps1.h"
#include "resource.h"
#include "events_ps1.h"

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

/* VRAM allocation tracking */
static uint16 nextVRAMX = 0;
static uint16 nextVRAMY = 480;  /* Start after framebuffers */

/*
 * Initialize PS1 graphics subsystem
 */
void graphicsInit()
{
    /* Reset GPU */
    ResetGraph(0);

    /* Initialize geometry transformation engine */
    InitGeom();

    /* Set screen geometry for 640x480 @ 60Hz */
    SetGeomScreen(320);  /* Half of screen width for perspective */

    /* Setup display environments for double buffering */
    /* Buffer 0: (0, 0) - Buffer 1: (0, 480) */
    SetDefDispEnv(&disp[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDispEnv(&disp[1], 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);

    /* Setup drawing environments */
    SetDefDrawEnv(&draw[0], 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDrawEnv(&draw[1], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    /* Enable display */
    SetDispMask(1);

    /* Apply first buffer */
    PutDispEnv(&disp[db]);
    PutDrawEnv(&draw[db]);

    /* Clear ordering tables */
    ClearOTagR(ot[0], OT_LENGTH);
    ClearOTagR(ot[1], OT_LENGTH);

    /* Load default palette (will be replaced by grLoadPalette) */
    for (int i = 0; i < 16; i++) {
        ttmPalette[i] = (i << 10) | (i << 5) | i;  /* Grayscale */
    }

    /* Initialize event system */
    eventsInit();
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
}

/*
 * Swap display buffers
 */
void grRefreshDisplay()
{
    /* Wait for GPU to finish drawing */
    DrawSync(0);

    /* Wait for vertical blank */
    VSync(0);

    /* Flip buffers */
    db = !db;
    PutDispEnv(&disp[db]);
    PutDrawEnv(&draw[db]);

    /* Clear next ordering table */
    ClearOTagR(ot[db], OT_LENGTH);
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
    /* TODO: Implement layer blitting using GPU primitives */
    /* This is the main rendering function that composites all layers */

    /* 1. Clear background */
    /* 2. Blit background surface if exists */
    /* 3. Blit saved zones layer if exists */
    /* 4. Blit each active TTM thread layer */
    /* 5. Blit holiday thread layer if active */

    /* For now, just swap buffers */
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
        char extractedPath[512];
        snprintf(extractedPath, sizeof(extractedPath), "extracted/bmp/%s",
                 bmpResource->resName);

        /* On PS1, we'd use CD-ROM functions here instead of fopen */
        FILE *f = fopen(extractedPath, "rb");
        if (f) {
            bmpResource->uncompressedData = safe_malloc(bmpResource->uncompressedSize);
            if (fread(bmpResource->uncompressedData, 1, bmpResource->uncompressedSize, f) !=
                bmpResource->uncompressedSize) {
                fatalError("Failed to reload BMP data from extracted file");
            }
            fclose(f);
            if (debugMode) {
                printf("Reloaded BMP data for %s from disk (%u bytes)\n",
                       bmpResource->resName, bmpResource->uncompressedSize);
            }
        } else {
            fatalError("BMP data freed and extracted file not found - cannot reload");
        }
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

        /* TODO: Upload to VRAM using LoadImage()
         * RECT rect = {surface->x, surface->y, width, height};
         * LoadImage(&rect, surface->pixels);
         */

        /* Set CLUT position (color lookup table) */
        /* For now, use a fixed position - we'll upload palette here */
        surface->clutX = 0;
        surface->clutY = 480;  /* Below framebuffers */

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
    /* TODO: Set GPU clipping rectangle */
    RECT clip = {x1, y1, x2 - x1, y2 - y1};
    SetDrawClip(&clip);
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
    /* TODO: Implement line drawing using GPU LINE primitive */
    LINE_F2 *line = (LINE_F2*)malloc(sizeof(LINE_F2));

    setLineF2(line);
    setXY2(line, x1, y1, x2, y2);
    setRGB0(line,
            ttmPalette[color] & 0x1F,
            (ttmPalette[color] >> 5) & 0x1F,
            (ttmPalette[color] >> 10) & 0x1F);

    /* Add to ordering table */
    addPrim(&ot[db][0], line);
}

/*
 * Draw filled rectangle
 */
void grDrawRect(PS1Surface *sfc, sint16 x, sint16 y, uint16 width, uint16 height, uint8 color)
{
    /* TODO: Implement rectangle drawing using GPU TILE primitive */
    TILE *tile = (TILE*)malloc(sizeof(TILE));

    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, width, height);
    setRGB0(tile,
            ttmPalette[color] & 0x1F,
            (ttmPalette[color] >> 5) & 0x1F,
            (ttmPalette[color] >> 10) & 0x1F);

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

    /* TODO: Implement actual SPRT primitive drawing
     * For now, we'll create a simple SPRT structure
     *
     * SPRT *sprt = (SPRT*)malloc(sizeof(SPRT));
     * setSprt(sprt);
     * setXY0(sprt, x, y);
     * setWH(sprt, sprite->width, sprite->height);
     * setUV0(sprt, sprite->x, sprite->y);  // Texture coords in VRAM
     * setClut(sprt, sprite->clutX, sprite->clutY);
     * addPrim(&ot[db][0], sprt);
     */

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

    /* TODO: Implement flipped sprite drawing
     * PS1 doesn't have hardware sprite flipping, so we'd need to:
     * 1. Upload a flipped version to VRAM, or
     * 2. Use polygon primitives (POLY_FT4) with flipped UV coordinates
     *
     * Option 2 is more efficient:
     * POLY_FT4 *poly = (POLY_FT4*)malloc(sizeof(POLY_FT4));
     * setPolyFT4(poly);
     * setXY4(poly, x+w, y, x, y, x+w, y+h, x, y+h);  // Flipped X coords
     * setUVWH(poly, sprite->x, sprite->y, sprite->width, sprite->height);
     * setClut(poly, sprite->clutX, sprite->clutY);
     * addPrim(&ot[db][0], poly);
     */

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
        char extractedPath[512];
        snprintf(extractedPath, sizeof(extractedPath), "extracted/scr/%s",
                 scrResource->resName);

        /* On PS1, we'd use CD-ROM functions here instead of fopen */
        FILE *f = fopen(extractedPath, "rb");
        if (f) {
            scrResource->uncompressedData = safe_malloc(scrResource->uncompressedSize);
            if (fread(scrResource->uncompressedData, 1, scrResource->uncompressedSize, f) !=
                scrResource->uncompressedSize) {
                fatalError("Failed to reload SCR data from extracted file");
            }
            fclose(f);
            if (debugMode) {
                printf("Reloaded SCR data for %s from disk (%u bytes)\n",
                       scrResource->resName, scrResource->uncompressedSize);
            }
        } else {
            fatalError("SCR data freed and extracted file not found - cannot reload");
        }
    }

    if ((scrResource->width % 2) == 1) {
        fprintf(stderr, "Warning: grLoadScreen(): can't manage odd widths\n");
    }

    if (scrResource->width > 640 || scrResource->height > 480) {
        fatalError("grLoadScreen(): can't manage more than 640x480 resolutions");
    }

    uint16 width  = scrResource->width;
    uint16 height = scrResource->height;

    /* Allocate PS1Surface for background */
    grBackgroundSfc = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
    grBackgroundSfc->width = width;
    grBackgroundSfc->height = height;
    grBackgroundSfc->x = nextVRAMX;
    grBackgroundSfc->y = nextVRAMY;

    /* Allocate pixel buffer for 4-bit indexed data */
    uint32 pixelDataSize = (width * height) / 2;  /* 4-bit = 0.5 bytes per pixel */
    grBackgroundSfc->pixels = (uint16*)safe_malloc(pixelDataSize);

    /* Copy packed 4-bit data directly */
    memcpy(grBackgroundSfc->pixels, scrResource->uncompressedData, pixelDataSize);

    /* TODO: Upload to VRAM using LoadImage() */

    /* Set CLUT position */
    grBackgroundSfc->clutX = 0;
    grBackgroundSfc->clutY = 480;

    /* Update VRAM allocation */
    nextVRAMX += width;
    if (nextVRAMX >= 1024) {
        nextVRAMX = 0;
        nextVRAMY += height;
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

    if (debugMode) {
        printf("Loaded screen: %dx%d at VRAM(%d,%d)\n",
               width, height, grBackgroundSfc->x, grBackgroundSfc->y);
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
