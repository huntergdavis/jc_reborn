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
struct TBmpResource;

#define SCREEN_WIDTH        640
#define SCREEN_HEIGHT       480

#define MAX_BMP_SLOTS       6
#define MAX_SPRITES_PER_BMP 255
#define MAX_TTM_SLOTS       10
#define MAX_TTM_THREADS     20

struct TAdsScene {
    uint16 slot;
    uint16 tag;
    uint16 numPlays;
};

/* PS1 Sprite structure - replaces SDL_Surface */
/* Supports multi-tile sprites for dimensions > 64 pixels */
typedef struct PS1Surface {
    uint16 *pixels;     /* 15-bit direct color pixel data (NULL if using indexedPixels) */
    uint8  *indexedPixels; /* 4-bit packed indexed pixel data (NULL if using pixels) */
    uint8  indexedOwned; /* 1 if indexedPixels must be freed with this surface */
    uint8  psbNibbles;  /* 1 if indexedPixels are PS1 nibble order (pre-swapped PSB) */
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
    uint16      spriteGen[MAX_BMP_SLOTS];
    struct TBmpResource *loadedBmp[MAX_BMP_SLOTS];
    const char  *loadedBmpNames[MAX_BMP_SLOTS];
    uint8       *psbData[MAX_BMP_SLOTS]; /* PSB buffer per-slot (sprites point into it) */
    PS1Surface  *sprites[MAX_BMP_SLOTS][MAX_SPRITES_PER_BMP];
    struct TTtmResource *ttmResource;  /* For LRU cache unpinning */
};

struct TTtmTag {
    uint16 id;
    uint32 offset;
};

#define MAX_DRAWN_SPRITES 255

struct TDrawnSprite {
    uint8  *indexedPixels;  /* Snapshot — always valid (BMP data never freed) */
    uint16 width, height;  /* Sprite dimensions at record time */
    sint16 x, y;           /* Screen position */
    uint16 spriteNo;       /* Dedup key */
    uint16 imageNo;        /* Dedup key */
    uint16 sceneEpoch;     /* Dedup key (iteration boundary) */
    uint8  flip;
    uint8  psbNibbles;  /* 1 if indexedPixels are PS1 nibble order (pre-swapped PSB) */
    const char *bmpName; /* Source BMP name for capture overlay/replay diagnostics */
};

struct TTtmThread {
    struct TTtmSlot   *ttmSlot;
    int    isRunning;
    char   currentStringArg[20];
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
    uint16 currentRegionId;
    PS1Surface *ttmLayer;
    uint16 sceneEpoch;
    /* Track composited sprites for frame replay */
    struct TDrawnSprite drawnSprites[MAX_DRAWN_SPRITES];
    uint8  numDrawnSprites;
    uint8  replayWriteCursor;
    /* Per-thread actor continuity (avoid cross-scene/global contamination). */
    struct TDrawnSprite lastActorReplay;
    uint8  lastActorReplayValid;
};

extern PS1Surface *grBackgroundSfc;

extern int grDx;
extern int grDy;
extern int grWindowed;
extern int grUpdateDelay;

/* Frame capture for visual regression testing */
extern int grCaptureFrameNumber;
extern int grCaptureForegroundOnly;
extern char *grCaptureFilename;
extern char *grCaptureDir;
extern char *grCaptureMetaDir;
extern int grCaptureInterval;
extern int grCaptureStartFrame;
extern int grCaptureEndFrame;
extern int grCaptureOverlay;
extern int grCaptureOverlayMaskOnly;
extern int grPs1TelemetryEnabled;
void grCaptureSetSceneLabel(const char *sceneLabel);

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
void grCompositeToBackgroundFlip(PS1Surface *sprite, sint16 screenX, sint16 screenY);

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
void grFreeCleanBgTiles(void);
void grEnsureCleanBgTiles(void);
void grRestoreBgTiles(void);
extern struct TTtmThread *grCurrentThread;
void grClearScreen(PS1Surface *sfc);

/* GPU sprite rendering — per-frame lifecycle */
void grBeginFrame(void);
void grDebugOverlayBox(int x, int y, int w, int h, uint16 color);
void grReplaySprite(struct TDrawnSprite *ds);

/* Background tiles - exported for dirty rectangle wiping */
extern PS1Surface *bgTile0;
extern PS1Surface *bgTile1;
extern PS1Surface *bgTile3;
extern PS1Surface *bgTile4;
void grDrawBackground(void);
void grFadeOut();
void grPs1StatThreadDrop(void);
void grPs1StatBmpFrameCap(uint16 requested, uint16 cap);
void grPs1StatBmpShortLoad(uint16 requested, uint16 loaded);
void grPs1SetLastBmpTelemetry(uint16 slot, uint16 frames, uint16 status);

void grLoadPalette();
void grLoadScreen(char *strArg);
int grGetCurrentFrame(void);

/* Frame capture for visual regression testing */
int grCaptureFrame(const char *filename);
int grCaptureSequenceComplete(void);

#endif /* GRAPHICS_PS1_H */
