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
    /* TODO: Implement BMP loading
     * 1. Load BMP resource by name
     * 2. Parse sprite data
     * 3. Upload to VRAM
     * 4. Store in ttmSlot->sprites[slotNo]
     */

    if (debugMode) {
        printf("grLoadBmp: slot=%d, name=%s\n", slotNo, strArg);
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
    /* TODO: Implement sprite drawing using GPU SPRT primitive */
}

/*
 * Draw horizontally flipped sprite
 */
void grDrawSpriteFlip(PS1Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y,
                      uint16 spriteNo, uint16 imageNo)
{
    /* TODO: Implement flipped sprite drawing */
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
    /* TODO: Implement SCR resource loading */
    if (debugMode) {
        printf("grLoadScreen: %s\n", strArg);
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
