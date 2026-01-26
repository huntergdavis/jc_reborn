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

#ifndef GRAPHICS_PS1_H
#define GRAPHICS_PS1_H

#include <psxgpu.h>
#include <psxgte.h>

/* Forward declaration for LRU cache */
struct TTtmResource;

#define SCREEN_WIDTH        640
#define SCREEN_HEIGHT       480

#define MAX_BMP_SLOTS       6
#define MAX_SPRITES_PER_BMP 8  /* Further reduced for PS1 memory constraints */
#define MAX_TTM_SLOTS       10
#define MAX_TTM_THREADS     10

struct TAdsScene {
    uint16 slot;
    uint16 tag;
    uint16 numPlays;
};

/* PS1 Sprite structure - replaces SDL_Surface */
/* Supports multi-tile sprites for dimensions > 64 pixels */
typedef struct PS1Surface {
    uint16 *pixels;     /* Pixel data in VRAM */
    uint16 width;       /* This tile's width (max 64) */
    uint16 height;      /* This tile's height (max 64) */
    uint16 x, y;        /* Position in VRAM */
    uint16 clutX, clutY; /* CLUT position in VRAM */
    /* Multi-tile support */
    uint16 fullWidth;   /* Original sprite full width */
    uint16 fullHeight;  /* Original sprite full height */
    uint16 tileOffsetX; /* This tile's X offset in original sprite */
    uint16 tileOffsetY; /* This tile's Y offset in original sprite */
    struct PS1Surface *nextTile; /* Next tile in chain (NULL if last/only) */
} PS1Surface;

/* Compatibility alias for code that uses SDL_Surface */
typedef PS1Surface SDL_Surface;

struct TTtmSlot {
    uint8       *data;
    uint32      dataSize;
    struct      TTtmTag *tags;
    int         numTags;
    int         numSprites[MAX_BMP_SLOTS];
    PS1Surface  *sprites[MAX_BMP_SLOTS][MAX_SPRITES_PER_BMP];
    struct TTtmResource *ttmResource;  /* For LRU cache unpinning */
};

struct TTtmTag {
    uint16 id;
    uint32 offset;
};

struct TTtmThread {
    struct TTtmSlot   *ttmSlot;
    int    isRunning;
    uint16 sceneSlot;
    uint16 sceneTag;
    short  sceneTimer;
    uint16 sceneIterations;
    uint32 ip;
    uint16 delay;
    uint16 timer;
    uint32 nextGotoOffset;
    uint8  selectedBmpSlot;
    uint8  fgColor;
    uint8  bgColor;
    PS1Surface *ttmLayer;
};

extern PS1Surface *grBackgroundSfc;

extern int grDx;
extern int grDy;
extern int grWindowed;
extern int grUpdateDelay;

/* Frame capture for visual regression testing */
extern int grCaptureFrameNumber;
extern char *grCaptureFilename;

/* Flag to track if GPU was already initialized (e.g., by loadTitleScreenEarly)
 * Set this to 1 BEFORE calling graphicsInit() if GPU is already set up */
extern int grGpuAlreadyInitialized;

void graphicsInit();
void graphicsEnd();
void grRefreshDisplay();
void grToggleFullScreen();
void grUpdateDisplay(struct TTtmThread *ttmBackgroundThread,
                     struct TTtmThread *ttmThreads,
                     struct TTtmThread *ttmHolidayThreads);

PS1Surface *grNewEmptyBackground();
PS1Surface *grNewLayer();
void grFreeLayer(PS1Surface *sfc);

void grLoadBmp(struct TTtmSlot *ttmSlot, uint16 slotNo, char *strArg);
void grLoadBmpRAM(struct TTtmSlot *ttmSlot, uint16 slotNo, char *strArg);
void grReleaseBmp(struct TTtmSlot *ttmSlot, uint16 bmpSlotNo);
void grBlitToFramebuffer(PS1Surface *sprite, sint16 screenX, sint16 screenY);
void grCompositeToBackground(PS1Surface *sprite, sint16 screenX, sint16 screenY);

void grSetClipZone(PS1Surface *sfc, sint16 x1, sint16 y1, sint16 x2, sint16 y2);
void grCopyZoneToBg(PS1Surface *sfc, uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3);
void grSaveImage1(PS1Surface *sfc, uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3);
void grSaveZone(PS1Surface *sfc, uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3);
void grRestoreZone(PS1Surface *sfc, uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3);
void grDrawPixel(PS1Surface *sfc, sint16 x, sint16 y, uint8 color);
void grDrawLine(PS1Surface *sfc, sint16 x1, sint16 y1, sint16 x2, sint16 y2, uint8 color);
void grDrawRect(PS1Surface *sfc, sint16 x, sint16 y, uint16 width, uint16 height, uint8 color);
void grDrawCircle(PS1Surface *sfc, sint16 x1, sint16 y1, uint16 width, uint16 height, uint8 fgColor, uint8 bgColor);
void grDrawSprite(PS1Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y, uint16 spriteNo, uint16 imageNo);
void grDrawSpriteFlip(PS1Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y, uint16 spriteNo, uint16 imageNo);

/* Extended sprite drawing - allows caller to provide their own OT and primitive buffer */
int grDrawSpriteExt(unsigned long *extOT, char **nextPri, PS1Surface *sprite, sint16 x, sint16 y);
void grInitEmptyBackground();
void grSaveCleanBgTiles(void);
void grRestoreBgTiles(void);
void grClearScreen(PS1Surface *sfc);

/* Background tiles - exported for dirty rectangle wiping */
extern PS1Surface *bgTile0;
extern PS1Surface *bgTile1;
extern PS1Surface *bgTile3;
extern PS1Surface *bgTile4;
void grDrawBackground(void);
void grFadeOut();

void grLoadPalette();
void grLoadScreen(char *strArg);

/* Frame capture for visual regression testing */
int grCaptureFrame(const char *filename);

#endif /* GRAPHICS_PS1_H */
