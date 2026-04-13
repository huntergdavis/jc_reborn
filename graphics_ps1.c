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

#include "utils.h"
#include "graphics_ps1.h"
#include "ads.h"
#include "foreground_pilot.h"
#include "resource.h"
#include "events_ps1.h"
#include "cdrom_ps1.h"
#include "psb_format.h"
#include "psb_registry.h"

#define MAX_CAPTURED_DRAWS 255

struct TPs1CapturedSpriteDraw {
    sint16 x;
    sint16 y;
    uint16 width;
    uint16 height;
    uint16 spriteNo;
    uint16 imageNo;
    uint8 flipped;
    const char *bmpName;
};

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
/* Top row tiles (stored in VRAM texture area) - exported for dirty rectangle wiping */
PS1Surface *bgTile0 = NULL;  /* x=0-255,   srcX=0 */
PS1Surface *bgTile1 = NULL;  /* x=256-511, srcX=256 */
static PS1Surface *bgTile2a = NULL; /* x=512-575, srcX=512, width=64 */
static PS1Surface *bgTile2b = NULL; /* x=576-639, srcX=576, width=64 */

/* Bottom row tiles - exported for dirty rectangle wiping */
PS1Surface *bgTile3 = NULL;  /* y=240, x=0-255 */
PS1Surface *bgTile4 = NULL;  /* y=240, x=256-511 */
static PS1Surface *bgTile5a = NULL; /* y=240, x=512-575 */
static PS1Surface *bgTile5b = NULL; /* y=240, x=576-637 */

/* Clean copies of background tiles for composite pattern.
 * Each frame: restore from clean → composite sprites → upload to framebuffer.
 * This avoids multiple LoadImage calls per frame (DMA conflict issue). */
static uint16 *bgTile0Clean = NULL;
static uint16 *bgTile1Clean = NULL;
static uint16 *bgTile3Clean = NULL;
static uint16 *bgTile4Clean = NULL;

/* Dirty-rect tracking: per-tile row-granularity restore/upload.
 * Index: 0=bgTile0, 1=bgTile1, 2=bgTile3, 3=bgTile4.
 * -1 means clean (no rows modified). */
static int currDirtyMinY[4] = {-1, -1, -1, -1};
static int currDirtyMaxY[4] = {-1, -1, -1, -1};
static int prevDirtyMinY[4] = {-1, -1, -1, -1};
static int prevDirtyMaxY[4] = {-1, -1, -1, -1};

/* Byte-pair palette lookup tables (256 entries × 4 bytes = 1KB each).
 * Each entry packs two resolved 16-bit colors for a packed byte:
 *   low16 = even pixel color, high16 = odd pixel color.
 * palLutSierra: high nibble = even (Sierra BMP format).
 * palLutPsb:    low nibble = even (PSB pre-transcoded format). */
static uint32 palLutSierra[256];
static uint32 palLutPsb[256];

static void grDrawRectColor15(sint16 x, sint16 y, uint16 width, uint16 height, uint16 bgColor);
static void grCapturePrintJsonString(const char *value);
static int grCaptureIsMetadataFrame(void);
static void grCaptureEmitFrameMetadataLine(void);
static void grDrawCapturePatternCell(sint16 x, sint16 y, int symbol);
static void grCommitRectToCleanBg(int x, int y, int width, int height);

static inline void markTileDirty(int idx, int minY, int maxY)
{
    if (currDirtyMinY[idx] < 0) {
        currDirtyMinY[idx] = minY;
        currDirtyMaxY[idx] = maxY;
    } else {
        if (minY < currDirtyMinY[idx]) currDirtyMinY[idx] = minY;
        if (maxY > currDirtyMaxY[idx]) currDirtyMaxY[idx] = maxY;
    }
}

static void grMarkAllTilesDirty(void)
{
    for (int i = 0; i < 4; i++) {
        currDirtyMinY[i] = 0;
        currDirtyMaxY[i] = 239;
    }
}

/* Mark dirty region from a screen-space rectangle (x0,y0)-(x1,y1) exclusive */
static void grMarkRectDirty(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 640) x1 = 640;
    if (y1 > 480) y1 = 480;
    if (x0 >= x1 || y0 >= y1) return;

    /* Top row tiles (screen y 0-239) */
    if (y0 < 240) {
        int ty0 = y0;
        int ty1 = (y1 < 240) ? y1 - 1 : 239;
        if (x0 < 320) markTileDirty(0, ty0, ty1);
        if (x1 > 320) markTileDirty(1, ty0, ty1);
    }
    /* Bottom row tiles (screen y 240-479) */
    if (y1 > 240) {
        int ty0 = (y0 > 240) ? y0 - 240 : 0;
        int ty1 = y1 - 240 - 1;
        if (ty1 > 239) ty1 = 239;
        if (x0 < 320) markTileDirty(2, ty0, ty1);
        if (x1 > 320) markTileDirty(3, ty0, ty1);
    }
}

static void grRebuildPaletteLuts(void)
{
    for (int i = 0; i < 256; i++) {
        /* Sierra: high nibble = even pixel, low nibble = odd pixel */
        uint16 pe = ttmPalette[(i >> 4) & 0x0F];
        uint16 po = ttmPalette[i & 0x0F];
        palLutSierra[i] = (uint32)pe | ((uint32)po << 16);
        /* PSB: low nibble = even pixel, high nibble = odd pixel */
        pe = ttmPalette[i & 0x0F];
        po = ttmPalette[(i >> 4) & 0x0F];
        palLutPsb[i] = (uint32)pe | ((uint32)po << 16);
    }
}

struct TPs1SavedZone {
    uint16 x;
    uint16 y;
    uint16 width;
    uint16 height;
    uint8 valid;
};

static struct TPs1SavedZone grPs1SavedZone = {0, 0, 0, 0, 0};

/* Global variables matching original implementation */
int grDx = 0;
int grDy = 0;
int grWindowed = 0;  /* PS1 is always fullscreen, but keep for compatibility */
int grUpdateDelay = 0;

/* Frame capture - stubbed for PS1 */
int grCaptureFrameNumber = -1;
int grCaptureForegroundOnly = 0;
char *grCaptureFilename = NULL;
char *grCaptureDir = NULL;
char *grCaptureMetaDir = NULL;
int grCaptureInterval = 0;
int grCaptureStartFrame = 0;
int grCaptureEndFrame = -1;
int grCaptureOverlay = 0;
int grCaptureOverlayMaskOnly = 0;
static int grCurrentFrame = 0;
static struct TPs1CapturedSpriteDraw grCapturedDraws[MAX_CAPTURED_DRAWS];
static int grCapturedDrawCount = 0;

/* Flag to track if GPU was already initialized (e.g., by loadTitleScreenEarly) */
int grGpuAlreadyInitialized = 0;

/* Current thread being played - used to record sprite draws for replay */
struct TTtmThread *grCurrentThread = NULL;
int grPs1TelemetryEnabled = 1;

/* Persistent debug counters for sprite/frame clipping diagnostics. */
static uint32 gStatThreadDrops = 0;
static uint32 gStatBmpFrameCapHits = 0;
static uint32 gStatBmpShortLoads = 0;
static uint16 gStatBmpMaxRequested = 0;
static uint16 gStatBmpMinLoaded = 0xFFFF;
static uint16 gStatLastBmpSlot = 0;
static uint16 gStatLastBmpFrames = 0;
static uint16 gStatLastBmpStatus = 0;
static uint16 gStatLoadedBmp = 0;
static uint16 gStatLoadedTtm = 0;
static uint16 gStatLoadedAds = 0;

/* Story transition diagnostics from story.c */
extern uint16 ps1StoryDbgPhase;
extern uint16 ps1StoryDbgSceneTag;
extern uint16 ps1StoryDbgAdsSig;
extern uint16 ps1StoryDbgPrevSpot;
extern uint16 ps1StoryDbgPrevHdg;
extern uint16 ps1StoryDbgNextSpot;
extern uint16 ps1StoryDbgNextHdg;
extern uint16 ps1StoryDbgSeq;
extern uint16 ps1AdsDbgActiveThreads;
extern uint16 ps1AdsDbgMini;
extern uint16 ps1AdsDbgRunningThreads;
extern uint16 ps1AdsDbgTerminatedThreads;
extern uint16 ps1AdsDbgSceneSlot;
extern uint16 ps1AdsDbgSceneTag;
extern uint16 ps1AdsDbgReplayCount;
extern uint16 ps1AdsDbgReplayTryFrame;
extern uint16 ps1AdsDbgReplayDrawFrame;
extern uint16 ps1AdsDbgMergeCarryFrame;
extern uint16 ps1AdsDbgNoDrawThreadsFrame;
extern uint16 ps1AdsDbgPlayedThreadsFrame;
extern uint16 ps1AdsDbgRecordedSpritesFrame;
extern uint16 ps1AdsDbgLastStopThread;
extern uint16 ps1AdsDbgLastStopSceneSig;
extern uint16 ps1AdsDbgLastReapThread;
extern uint16 ps1AdsDbgLastReapSceneSig;
extern uint16 ps1AdsDbgLastAddThread;
extern uint16 ps1AdsDbgLastAddSceneSig;
extern uint16 ps1PilotDbgActivePack;
extern uint16 ps1PilotDbgHits;
extern uint16 ps1PilotDbgFallbacks;
extern uint16 ps1PilotDbgLastHitEntry;
extern uint16 ps1PilotDbgLastFallbackEntry;
extern uint16 ps1PilotDbgFallbackWhilePackActive;

void grPs1StatThreadDrop(void)
{
    if (gStatThreadDrops < 0xFFFFFFFFU) gStatThreadDrops++;
}

void grPs1StatBmpFrameCap(uint16 requested, uint16 cap)
{
    (void)cap;
    if (gStatBmpFrameCapHits < 0xFFFFFFFFU) gStatBmpFrameCapHits++;
    if (requested > gStatBmpMaxRequested) gStatBmpMaxRequested = requested;
}

void grPs1StatBmpShortLoad(uint16 requested, uint16 loaded)
{
    if (gStatBmpShortLoads < 0xFFFFFFFFU) gStatBmpShortLoads++;
    if (requested > gStatBmpMaxRequested) gStatBmpMaxRequested = requested;
    if (loaded < gStatBmpMinLoaded) gStatBmpMinLoaded = loaded;
}

void grPs1SetLastBmpTelemetry(uint16 slot, uint16 frames, uint16 status)
{
    gStatLastBmpSlot = slot;
    gStatLastBmpFrames = frames;
    gStatLastBmpStatus = status;
}

static void grDrawCounterBar(int x, int y, int w, int h, uint16 color)
{
    if (!bgTile0 || !bgTile0->pixels) return;
    if (x < 0 || y < 0 || x >= (int)bgTile0->width || y >= (int)bgTile0->height) return;
    if (w <= 0 || h <= 0) return;
    if (x + w > (int)bgTile0->width) w = (int)bgTile0->width - x;
    if (y + h > (int)bgTile0->height) h = (int)bgTile0->height - y;

    for (int yy = 0; yy < h; yy++) {
        uint16 *row = bgTile0->pixels + (y + yy) * (int)bgTile0->width + x;
        for (int xx = 0; xx < w; xx++) row[xx] = color;
    }
}

static int grCaptureStartsWith(const char *text, const char *prefix);
static int grCaptureEquals(const char *lhs, const char *rhs);
static int grCaptureIsActorCandidate(const char *bmpName);
static const char *grCaptureSceneLabel = "";

enum
{
    GR_CAPTURE_ENTITY_JOHNNY = 0,
    GR_CAPTURE_ENTITY_MARY = 1,
    GR_CAPTURE_ENTITY_SUZY = 2,
    GR_CAPTURE_ENTITY_OTHER = 3,
    GR_CAPTURE_ENTITY_COUNT = 4
};

struct TPs1CaptureEntitySummary
{
    int present;
    int left;
    int top;
    int right;
    int bottom;
};

static int grCaptureClassifyEntity(const char *bmpName)
{
    if (!grCaptureIsActorCandidate(bmpName))
        return -1;

    if (bmpName == NULL)
        return -1;

    if (grCaptureEquals(bmpName, "FISHMAN.BMP"))
        return GR_CAPTURE_ENTITY_OTHER;

    if (grCaptureStartsWith(bmpName, "SJMSUZY") ||
        grCaptureStartsWith(bmpName, "SSUZY") ||
        grCaptureStartsWith(bmpName, "SUZY")) {
        return GR_CAPTURE_ENTITY_SUZY;
    }

    if (grCaptureStartsWith(bmpName, "SM") ||
        grCaptureStartsWith(bmpName, "MARY") ||
        grCaptureStartsWith(bmpName, "SLEVEJM") ||
        grCaptureEquals(bmpName, "SASKDATE.BMP") ||
        grCaptureEquals(bmpName, "SBREAKUP.BMP") ||
        grCaptureEquals(bmpName, "SJBRAKUP.BMP")) {
        return GR_CAPTURE_ENTITY_MARY;
    }

    return GR_CAPTURE_ENTITY_JOHNNY;
}

static uint8 grCaptureQuantize63(int value, int maxValue)
{
    if (value < 0)
        value = 0;
    if (value > maxValue)
        value = maxValue;
    return (uint8)(((uint32)value * 63U + (uint32)(maxValue / 2)) / (uint32)maxValue);
}

static void grCaptureSummarizeEntities(struct TPs1CaptureEntitySummary *entities)
{
    int i;

    for (i = 0; i < GR_CAPTURE_ENTITY_COUNT; i++) {
        entities[i].present = 0;
        entities[i].left = 0;
        entities[i].top = 0;
        entities[i].right = 0;
        entities[i].bottom = 0;
    }

    for (i = 0; i < grCapturedDrawCount; i++) {
        const struct TPs1CapturedSpriteDraw *draw = &grCapturedDraws[i];
        int entity = grCaptureClassifyEntity(draw->bmpName);
        int left;
        int top;
        int right;
        int bottom;

        if (entity < 0)
            continue;

        left = draw->x;
        top = draw->y;
        right = draw->x + (int)draw->width;
        bottom = draw->y + (int)draw->height;

        if (!entities[entity].present) {
            entities[entity].present = 1;
            entities[entity].left = left;
            entities[entity].top = top;
            entities[entity].right = right;
            entities[entity].bottom = bottom;
        } else {
            if (left < entities[entity].left) entities[entity].left = left;
            if (top < entities[entity].top) entities[entity].top = top;
            if (right > entities[entity].right) entities[entity].right = right;
            if (bottom > entities[entity].bottom) entities[entity].bottom = bottom;
        }
    }
}

static void grCaptureResetFrameDraws(void)
{
    grCapturedDrawCount = 0;
}

static void grCapturePrintJsonString(const char *value)
{
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    printf("\"");
    while (*p) {
        switch (*p) {
            case '\\':
            case '"':
                printf("\\%c", *p);
                break;
            case '\n':
                printf("\\n");
                break;
            case '\r':
                printf("\\r");
                break;
            case '\t':
                printf("\\t");
                break;
            default:
                if (*p < 0x20)
                    printf("\\u%04x", *p);
                else
                    printf("%c", *p);
                break;
        }
        p++;
    }
    printf("\"");
}

static void grCaptureRecordSpriteDraw(const char *bmpName,
                                      sint16 x,
                                      sint16 y,
                                      uint16 spriteNo,
                                      uint16 imageNo,
                                      uint16 width,
                                      uint16 height,
                                      int flipped)
{
    struct TPs1CapturedSpriteDraw *draw;

    if (bmpName == NULL || width == 0 || height == 0)
        return;
    if (grCapturedDrawCount >= MAX_CAPTURED_DRAWS)
        return;

    draw = &grCapturedDraws[grCapturedDrawCount++];
    draw->x = x;
    draw->y = y;
    draw->width = width;
    draw->height = height;
    draw->spriteNo = spriteNo;
    draw->imageNo = imageNo;
    draw->flipped = (flipped ? 1 : 0);
    draw->bmpName = bmpName;
}

static int grCaptureIsMetadataFrame(void)
{
    if (grCaptureMetaDir == NULL || grCaptureMetaDir[0] == '\0')
        return 0;
    if (grCaptureInterval <= 0)
        return 0;
    if (grCurrentFrame < grCaptureStartFrame)
        return 0;
    if (grCaptureEndFrame >= 0 && grCurrentFrame > grCaptureEndFrame)
        return 0;
    if (((grCurrentFrame - grCaptureStartFrame) % grCaptureInterval) != 0)
        return 0;
    return 1;
}

static void grCaptureEmitFrameMetadataLine(void)
{
    int emitted = 0;
    int i;

    if (!grCaptureIsMetadataFrame())
        return;

    /* PS1 printf uses the BIOS break instruction for TTY output.
     * Calling printf in the game loop crashes/hangs the PS1 unless PCDrv
     * is enabled in the emulator.  The headless regtest captures this
     * metadata from the guest TTY, but the regtest image does not enable
     * PCDrv by default, so skip the emission entirely.  The regtest
     * harness extracts sprite draw data from the capture overlay instead. */
    (void)emitted; (void)i;
    return;

    printf("PS1_CAPTURE_META ");
    printf("{\"frame_number\":%d,", grCurrentFrame);
    printf("\"scene_label\":");
    grCapturePrintJsonString(grCaptureSceneLabel);
    printf(",\"draw_count\":%d,", grCapturedDrawCount);
    printf("\"visible_draw_count\":%d,", grCapturedDrawCount);
    printf("\"overlay_enabled\":%s,", grCaptureOverlay ? "true" : "false");
    printf("\"visible_draws\":[");

    for (i = 0; i < grCapturedDrawCount; i++) {
        const struct TPs1CapturedSpriteDraw *draw = &grCapturedDraws[i];
        if (!grCaptureIsActorCandidate(draw->bmpName))
            continue;
        if (emitted > 0)
            printf(",");
        printf("{\"index\":%d,\"surface_role\":\"replay_layer\",\"bmp_name\":", emitted);
        grCapturePrintJsonString(draw->bmpName);
        printf(",\"image_no\":%u,\"sprite_no\":%u,\"x\":%d,\"y\":%d,\"width\":%u,\"height\":%u,\"flipped\":%s}",
               draw->imageNo,
               draw->spriteNo,
               draw->x,
               draw->y,
               draw->width,
               draw->height,
               draw->flipped ? "true" : "false");
        emitted++;
    }

    printf("]}\n");
}

static uint32 grCaptureCrc32(const uint8 *data, size_t length)
{
    uint32 crc = 0xffffffffu;
    size_t i;

    for (i = 0; i < length; i++) {
        int bit;
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xedb88320u;
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xffffffffu;
}

static int grCaptureStartsWith(const char *text, const char *prefix)
{
    while (*prefix) {
        if (*text++ != *prefix++)
            return 0;
    }
    return 1;
}

static int grCaptureEquals(const char *lhs, const char *rhs)
{
    while (*lhs && *rhs) {
        if (*lhs != *rhs)
            return 0;
        lhs++;
        rhs++;
    }
    return (*lhs == '\0' && *rhs == '\0');
}

static int grCaptureIsActorCandidate(const char *bmpName)
{
    if (bmpName == NULL || *bmpName == '\0')
        return 0;

    if (grCaptureEquals(bmpName, "BACKGRND.BMP") ||
        grCaptureEquals(bmpName, "MRAFT.BMP") ||
        grCaptureEquals(bmpName, "MJRAFT.BMP") ||
        grCaptureEquals(bmpName, "MJRAFT2.BMP") ||
        grCaptureEquals(bmpName, "MJSAND.BMP") ||
        grCaptureEquals(bmpName, "MJSANDC.BMP") ||
        grCaptureEquals(bmpName, "SPLASH.BMP") ||
        grCaptureEquals(bmpName, "LILFISH.BMP") ||
        grCaptureEquals(bmpName, "COCONUTS.BMP") ||
        grCaptureEquals(bmpName, "COCOHEAD.BMP") ||
        grCaptureEquals(bmpName, "MJCOCO.BMP") ||
        grCaptureEquals(bmpName, "MJCOCO1.BMP") ||
        grCaptureEquals(bmpName, "MJBOTTLE.BMP") ||
        grCaptureEquals(bmpName, "SJGFTASK.BMP") ||
        grCaptureEquals(bmpName, "SJGFTJMP.BMP") ||
        grCaptureEquals(bmpName, "SJGFTSHY.BMP") ||
        grCaptureEquals(bmpName, "SJGFTXCH.BMP") ||
        grCaptureEquals(bmpName, "SJWORK.BMP")) {
        return 0;
    }

    if (grCaptureStartsWith(bmpName, "BACKGRND") ||
        grCaptureStartsWith(bmpName, "MJSAND") ||
        grCaptureStartsWith(bmpName, "MRAFT") ||
        grCaptureStartsWith(bmpName, "MJRAFT") ||
        grCaptureStartsWith(bmpName, "GJGULL") ||
        grCaptureStartsWith(bmpName, "GJVIS") ||
        grCaptureStartsWith(bmpName, "GJNAT") ||
        grCaptureStartsWith(bmpName, "SHARK") ||
        grCaptureStartsWith(bmpName, "COCO") ||
        grCaptureStartsWith(bmpName, "LITE") ||
        grCaptureStartsWith(bmpName, "TRUNK") ||
        grCaptureStartsWith(bmpName, "SRAFT")) {
        return 0;
    }

    return grCaptureStartsWith(bmpName, "MJ") ||
           grCaptureStartsWith(bmpName, "SJ") ||
           grCaptureStartsWith(bmpName, "SM") ||
           grCaptureStartsWith(bmpName, "GJ") ||
           grCaptureStartsWith(bmpName, "JOHN") ||
           grCaptureStartsWith(bmpName, "MARY") ||
           grCaptureStartsWith(bmpName, "SUZY");
}

static size_t grCaptureBuildOverlayPayload(uint8 *buffer, size_t capacity)
{
    size_t offset = 0;
    int totalActors = 0;
    int embeddedActors = 0;
    int i;

    if (capacity < 10)
        return 0;

    buffer[offset++] = 'J';
    buffer[offset++] = 'C';
    buffer[offset++] = 'S';
    buffer[offset++] = '1';
    buffer[offset++] = (uint8)(grCurrentFrame & 0xff);
    buffer[offset++] = (uint8)((grCurrentFrame >> 8) & 0xff);
    buffer[offset++] = (uint8)((grCurrentFrame >> 16) & 0xff);
    buffer[offset++] = (uint8)((grCurrentFrame >> 24) & 0xff);
    buffer[offset++] = 0;
    buffer[offset++] = 0;

    for (i = 0; i < grCapturedDrawCount; i++) {
        const struct TPs1CapturedSpriteDraw *draw = &grCapturedDraws[i];
        int xClamped;
        int yClamped;
        uint8 xQuantized;
        uint8 yQuantized;
        unsigned int nameHash = 0;
        const unsigned char *p = (const unsigned char *)(draw->bmpName ? draw->bmpName : "");

        if (!grCaptureIsActorCandidate(draw->bmpName))
            continue;

        totalActors++;

        if (embeddedActors >= 5)
            continue;

        while (*p) {
            nameHash = ((nameHash << 5) - nameHash) + *p;
            p++;
        }

        if (offset + 7 > capacity)
            break;

        xClamped = draw->x;
        yClamped = draw->y;
        if (xClamped < 0) xClamped = 0;
        if (yClamped < 0) yClamped = 0;
        if (xClamped > 639) xClamped = 639;
        if (yClamped > 479) yClamped = 479;

        xQuantized = (uint8)(((uint32)xClamped * 255U + 319U) / 639U);
        yQuantized = (uint8)(((uint32)yClamped * 255U + 239U) / 479U);

        buffer[offset++] = xQuantized;
        buffer[offset++] = yQuantized;
        buffer[offset++] = (uint8)((draw->width > 255) ? 255 : draw->width);
        buffer[offset++] = (uint8)((draw->height > 255) ? 255 : draw->height);
        buffer[offset++] = (uint8)(nameHash & 0xff);
        buffer[offset++] = (uint8)((nameHash >> 8) & 0xff);
        buffer[offset++] = (uint8)((nameHash >> 16) & 0xff);
        embeddedActors++;
    }

    if (offset + 8 <= capacity) {
        uint8 pilotFlags = 0;

        if (foregroundPilotRuntimeActive())
            pilotFlags |= 0x01;
        if (foregroundPilotRuntimeHasFrameData())
            pilotFlags |= 0x02;

        buffer[offset++] = pilotFlags;
        buffer[offset++] = (uint8)(foregroundPilotRuntimeMode() & 0xff);
        buffer[offset++] = (uint8)(foregroundPilotRuntimeFrameIndex() & 0xff);
        buffer[offset++] = (uint8)((foregroundPilotRuntimeFrameIndex() >> 8) & 0xff);
        buffer[offset++] = (uint8)(foregroundPilotRuntimeSourceFrame() & 0xff);
        buffer[offset++] = (uint8)((foregroundPilotRuntimeSourceFrame() >> 8) & 0xff);
        buffer[offset++] = (uint8)(foregroundPilotRuntimeDisplayVBlanks() & 0xff);
        buffer[offset++] = (uint8)((foregroundPilotRuntimeDisplayVBlanks() >> 8) & 0xff);
    }

    buffer[8] = (uint8)totalActors;
    buffer[9] = (uint8)embeddedActors;

    return offset;
}

static void grDrawCaptureOverlay(void)
{
    uint8 payload[512];
    uint8 packet[1024];
    size_t payloadLen;
    size_t packetLen;
    uint32 crc;
    const int widthCells = 40;
    const int heightCells = 6;
    const int cellSize = 8;
    const int originX = SCREEN_WIDTH - (widthCells * cellSize);
    const int originY = 140;
    size_t symbolIndex = 0;

    payloadLen = grCaptureBuildOverlayPayload(payload, sizeof(payload));
    if (!grCaptureOverlayMaskOnly && payloadLen == 0)
        return;

    if (!grCaptureOverlayMaskOnly) {
        crc = grCaptureCrc32(payload, payloadLen);
        packet[0] = (uint8)(payloadLen & 0xff);
        packet[1] = (uint8)((payloadLen >> 8) & 0xff);
        memcpy(packet + 2, payload, payloadLen);
        packet[2 + payloadLen + 0] = (uint8)(crc & 0xff);
        packet[2 + payloadLen + 1] = (uint8)((crc >> 8) & 0xff);
        packet[2 + payloadLen + 2] = (uint8)((crc >> 16) & 0xff);
        packet[2 + payloadLen + 3] = (uint8)((crc >> 24) & 0xff);
        packetLen = payloadLen + 6;
    } else {
        packetLen = 0;
    }

    for (int cellY = 0; cellY < heightCells; cellY++) {
        for (int cellX = 0; cellX < widthCells; cellX++) {
            int value = 0;

            if (symbolIndex < packetLen * 4) {
                size_t byteIndex = symbolIndex / 4;
                int shift = (int)((symbolIndex % 4) * 2);
                value = (packet[byteIndex] >> shift) & 0x3;
                symbolIndex++;
            }

            grDrawCapturePatternCell((sint16)(originX + (cellX * cellSize)),
                                     (sint16)(originY + (cellY * cellSize)),
                                     value);
        }
    }
}

static void grDrawCapturePatternCell(sint16 x, sint16 y, int symbol)
{
    uint16 color = 0x0000;

    switch (symbol & 0x3) {
        case 1: color = 0x7FFF; break;
        case 2: color = 0x03E0; break;
        case 3: color = 0x7C00; break;
        default: color = 0x0000; break;
    }

    grDrawRectColor15(x, y, 8, 8, 0x0000);
    if (color != 0x0000)
        grDrawRectColor15(x, y + 3, 8, 2, color);
}

static void grDrawCaptureActorPanel(void)
{
    struct TPs1CaptureEntitySummary entities[GR_CAPTURE_ENTITY_COUNT];
    const int panelX = 8;
    const int panelY = 140;
    const int panelW = 74;
    const int entityStride = 12;
    const int dataX = panelX + 8;
    const int bucketCount = 4;
    const int bucketStep = 14;
    const int bucketW = 10;
    const int rowH = 3;
    int entity;

    grCaptureSummarizeEntities(entities);
    grDrawCounterBar(panelX, panelY, panelW, 50, 0x0000);
    if (grCaptureOverlayMaskOnly)
        return;

    for (entity = 0; entity < GR_CAPTURE_ENTITY_COUNT; entity++) {
        const struct TPs1CaptureEntitySummary *summary = &entities[entity];
        int baseY = panelY + 2 + (entity * entityStride);
        int centerX = 0;
        int centerY = 0;
        int xPos = 0;
        int yPos = 0;
        uint16 markerColor = 0x7FFF;

        switch (entity) {
            case GR_CAPTURE_ENTITY_JOHNNY: markerColor = 0x001F; break;
            case GR_CAPTURE_ENTITY_MARY: markerColor = 0x03E0; break;
            case GR_CAPTURE_ENTITY_SUZY: markerColor = 0x7C00; break;
            case GR_CAPTURE_ENTITY_OTHER: markerColor = 0x7FFF; break;
        }

        grDrawCounterBar(panelX + 2, baseY, 3, 2, markerColor);

        if (summary->present) {
            centerX = (summary->left + summary->right) / 2;
            centerY = (summary->top + summary->bottom) / 2;
            if (centerX < 0) centerX = 0;
            if (centerX > 639) centerX = 639;
            if (centerY < 0) centerY = 0;
            if (centerY > 479) centerY = 479;
            xPos = (int)(((uint32)centerX * (uint32)bucketCount) / 640U);
            yPos = (int)(((uint32)centerY * (uint32)bucketCount) / 480U);
            if (xPos < 0) xPos = 0;
            if (xPos >= bucketCount) xPos = bucketCount - 1;
            if (yPos < 0) yPos = 0;
            if (yPos >= bucketCount) yPos = bucketCount - 1;
        }

        if (summary->present) {
            grDrawCounterBar(dataX + (xPos * bucketStep), baseY + 0, bucketW, rowH, 0x7FFF);
            grDrawCounterBar(dataX + (yPos * bucketStep), baseY + 5, bucketW, rowH, 0x03E0);
        }
    }
}

static void grRefreshLoadedResourceCounters(void)
{
    /* Refresh every 16 frames to keep telemetry cheap while still useful. */
    if ((grCurrentFrame & 0x0F) != 0) return;

    int bmpLoaded = 0;
    int ttmLoaded = 0;
    int adsLoaded = 0;

    for (int i = 0; i < numBmpResources; i++) {
        if (bmpResources[i] && bmpResources[i]->uncompressedData) bmpLoaded++;
    }
    for (int i = 0; i < numTtmResources; i++) {
        if (ttmResources[i] && ttmResources[i]->uncompressedData) ttmLoaded++;
    }
    for (int i = 0; i < numAdsResources; i++) {
        if (adsResources[i] && adsResources[i]->uncompressedData) adsLoaded++;
    }

    gStatLoadedBmp = (uint16)bmpLoaded;
    gStatLoadedTtm = (uint16)ttmLoaded;
    gStatLoadedAds = (uint16)adsLoaded;
}

static void grDrawDropDiagnostics(void)
{
    /* Visual overlay (top-left) so long-run screenshots can confirm clipping/drops:
     * red=thread drops, magenta=BMP frame caps, cyan=short loads. */
    grDrawCounterBar(2, 2, 148, 16, 0x0000); /* black panel */

    int w0 = (gStatThreadDrops > 140U) ? 140 : (int)gStatThreadDrops;
    int w2 = (gStatBmpFrameCapHits > 140U) ? 140 : (int)gStatBmpFrameCapHits;
    int w3 = (gStatBmpShortLoads > 140U) ? 140 : (int)gStatBmpShortLoads;

    if (w0) grDrawCounterBar(6, 4, w0, 2, 0x001F);
    if (w2) grDrawCounterBar(6, 10, w2, 2, 0x7C1F);
    if (w3) grDrawCounterBar(6, 13, w3, 2, 0x7FE0);

    /* Dim white markers: max requested frame count and minimum loaded frame count. */
    if (gStatBmpMaxRequested) {
        int reqW = (gStatBmpMaxRequested > 140U) ? 140 : (int)gStatBmpMaxRequested;
        grDrawCounterBar(6, 2, reqW, 1, 0x4210);
    }
    if (gStatBmpMinLoaded != 0xFFFFU) {
        int loadW = (gStatBmpMinLoaded > 140U) ? 140 : (int)gStatBmpMinLoaded;
        grDrawCounterBar(6, 15, loadW, 1, 0x4210);
    }

    /* White heartbeat marker means overlay active. */
    grDrawCounterBar(152, 2, 2, 2, 0x7FFF);
    /* Moving red marker proves frame updates are still happening. */
    grDrawCounterBar(156 + (grCurrentFrame & 0x1F), 2, 2, 2, 0x001F);
}

static void grDrawMemoryDiagnostics(void)
{
    /* Mid-left memory/resource pressure panel:
     * row0: memory used percentage (green/yellow/red by pressure)
     * row1: loaded BMP resources
     * row2: loaded TTM resources
     * row3: loaded ADS resources
     * row4: used KiB / 16 (0..63)
     * row5: budget KiB / 16 (0..63) */
    int x = 2;
    int y = 174;
    int panelW = 96;
    int rowH = 3;
    size_t used = getTotalMemoryUsed();
    size_t budget = getMemoryBudget();
    uint16 usedColor = 0x03E0;
    int usedPctW = 0;
    int usedScaled = 0;
    int budgetScaled = 0;

    if (budget > 0U) {
        size_t usedPct = (used * 100U) / budget;
        if (usedPct > 100U) usedPct = 100U;
        usedPctW = (int)(usedPct * 63U / 100U);
        if (usedPct >= 85U) usedColor = 0x001F;
        else if (usedPct >= 70U) usedColor = 0x03FF;
    }

    usedScaled = (int)((used >> 14) & 0x3F);     /* 16 KiB units */
    budgetScaled = (int)(((budget >> 14) > 63U) ? 63U : (budget >> 14));

    grDrawCounterBar(x, y, panelW, 21, 0x0000);
    if (usedPctW > 0) grDrawCounterBar(x + 2, y + 1, usedPctW, rowH, usedColor);
    grDrawCounterBar(x + 2, y + 4, (gStatLoadedBmp & 0x3F), rowH, 0x03FF);
    grDrawCounterBar(x + 2, y + 7, (gStatLoadedTtm & 0x3F), rowH, 0x7C1F);
    grDrawCounterBar(x + 2, y + 10, (gStatLoadedAds & 0x3F), rowH, 0x001F);
    grDrawCounterBar(x + 2, y + 13, usedScaled, rowH, 0x7FFF);
    grDrawCounterBar(x + 2, y + 16, budgetScaled, rowH, 0x4210);
}


static void grDrawStoryDiagnostics(void)
{
    /* Bottom-left persistent transition state panel:
     * row0: sequence id (gray)
     * row1: phase (white)
     * row2: scene tag (green)
     * row3: ads family signature (magenta)
     * row4: prevSpot/prevHdg (yellow)
     * row5: nextSpot/nextHdg (cyan) */
    int x = 2;
    int y = 222;
    int panelW = 96;
    int rowH = 3;

    grDrawCounterBar(x, y, panelW, 21, 0x0000);
    grDrawCounterBar(x + 2, y + 1,  (ps1StoryDbgSeq & 0x3F), rowH, 0x4210);
    grDrawCounterBar(x + 2, y + 4,  (ps1StoryDbgPhase & 0x3F), rowH, 0x7FFF);
    grDrawCounterBar(x + 2, y + 7,  (ps1StoryDbgSceneTag & 0x3F), rowH, 0x03E0);
    grDrawCounterBar(x + 2, y + 10, (ps1StoryDbgAdsSig & 0x3F), rowH, 0x7C1F);

    {
        int prevW = ((ps1StoryDbgPrevSpot & 0x7) * 8) + (ps1StoryDbgPrevHdg & 0x7);
        int nextW = ((ps1StoryDbgNextSpot & 0x7) * 8) + (ps1StoryDbgNextHdg & 0x7);
        grDrawCounterBar(x + 2, y + 13, prevW, rowH, 0x03FF);
        grDrawCounterBar(x + 2, y + 16, nextW, rowH, 0x7FE0);
    }
}

static void grDrawPilotPackDiagnostics(void)
{
    /* Top-left pilot compiled-pack panel, below the drop diagnostics with a gap:
     * row0 white: active pilot pack id
     * row1 green: cumulative pack hits
     * row2 red  : cumulative extracted fallback loads
     * row3 yellow: last successful pack entry index
     * row4 magenta: last extracted fallback entry index (0 = outside active pack)
     * row5 cyan: failed pack-first lookups while a pack stayed active */
    int x = 2;
    int y = 30;
    int panelW = 96;
    int rowH = 2;

    grDrawCounterBar(x, y, panelW, 30, 0x0000);
    grDrawCounterBar(x + 2, y + 1,  (ps1PilotDbgActivePack & 0x3F), rowH, 0x7FFF);
    grDrawCounterBar(x + 2, y + 6,  (ps1PilotDbgHits & 0x3F), rowH, 0x03E0);
    grDrawCounterBar(x + 2, y + 11, (ps1PilotDbgFallbacks & 0x3F), rowH, 0x001F);
    grDrawCounterBar(x + 2, y + 16, (ps1PilotDbgLastHitEntry & 0x3F), rowH, 0x03FF);
    grDrawCounterBar(x + 2, y + 21, (ps1PilotDbgLastFallbackEntry & 0x3F), rowH, 0x7C1F);
    grDrawCounterBar(x + 2, y + 26, (ps1PilotDbgFallbackWhilePackActive & 0x3F), rowH, 0x7FE0);
}

static void grDrawAdsFreezeDiagnostics(void)
{
    /* Mid-left ADS telemetry (kept globals only):
     * row0 blue   : active threads
     * row1 white  : mini timer
     * row2 yellow : scene slot/tag signature
     * row3 magenta: replay count
     * row4 cyan   : running thread count
     * row5 green  : grUpdateDelay (frame wait)
     * row6 white  : replay tries this frame
     * row7 green  : replay draws this frame
     * row8 magenta: merged carry-forward draws
     * row9 red    : played threads with zero draws
     * row10 cyan  : last STOP_SCENE target thread index
     * row11 yellow: last STOP_SCENE target scene signature
     * row12 red   : last reaped thread index
     * row13 white : last reaped scene signature
     * row14 green : last ADD_SCENE thread index
     * row15 magenta: last ADD_SCENE scene signature */
    int x = 2;
    int y = 90;
    int panelW = 96;
    int rowH = 3;

    grDrawCounterBar(x, y, panelW, 50, 0x0000);
    grDrawCounterBar(x + 2, y + 1,  (ps1AdsDbgActiveThreads & 0x3F), rowH, 0x03FF);
    grDrawCounterBar(x + 2, y + 4,  (ps1AdsDbgMini & 0x3F), rowH, 0x7FFF);
    grDrawCounterBar(x + 2, y + 7,
                     (((ps1AdsDbgSceneSlot & 0x7) << 3) | (ps1AdsDbgSceneTag & 0x7)) & 0x3F,
                     rowH, 0x03FF);
    grDrawCounterBar(x + 2, y + 10, (ps1AdsDbgReplayCount & 0x3F), rowH, 0x7C1F);
    grDrawCounterBar(x + 2, y + 13, (ps1AdsDbgRunningThreads & 0x3F), rowH, 0x03FF);
    grDrawCounterBar(x + 2, y + 16, (grUpdateDelay & 0x3F), rowH, 0x03E0);
    grDrawCounterBar(x + 2, y + 19, (ps1AdsDbgReplayTryFrame & 0x3F), rowH, 0x7FFF);
    grDrawCounterBar(x + 2, y + 22, (ps1AdsDbgReplayDrawFrame & 0x3F), rowH, 0x03E0);
    grDrawCounterBar(x + 2, y + 25, (ps1AdsDbgMergeCarryFrame & 0x3F), rowH, 0x7C1F);
    grDrawCounterBar(x + 2, y + 28, (ps1AdsDbgNoDrawThreadsFrame & 0x3F), rowH, 0x001F);
    grDrawCounterBar(x + 2, y + 31, (ps1AdsDbgLastStopThread & 0x3F), rowH, 0x03FF);
    grDrawCounterBar(x + 2, y + 34, (ps1AdsDbgLastStopSceneSig & 0x3F), rowH, 0x7FE0);
    grDrawCounterBar(x + 2, y + 37, (ps1AdsDbgLastReapThread & 0x3F), rowH, 0x001F);
    grDrawCounterBar(x + 2, y + 40, (ps1AdsDbgLastReapSceneSig & 0x3F), rowH, 0x7FFF);
    grDrawCounterBar(x + 2, y + 43, (ps1AdsDbgLastAddThread & 0x3F), rowH, 0x03E0);
    grDrawCounterBar(x + 2, y + 46, (ps1AdsDbgLastAddSceneSig & 0x3F), rowH, 0x7C1F);
}

/* VRAM allocation tracking
 * VRAM Layout for 640x480 interlaced:
 * (0,0)-(639,479): Framebuffer (single buffer for 640x480)
 * (640,0)-(656,1): CLUT (16 colors)
 * (640,2)-(895,2): CLUT 256 (grayscale)
 * (640,4) onwards: Textures
 */
static uint16 nextVRAMX = 640;  /* Start to the right of framebuffer */
static uint16 nextVRAMY = 4;    /* Below CLUTs */

/* VRAM scratch allocator for per-frame GPU sprite textures.
 * Scratch area: (640,4) to (1023,511) — right of framebuffer, below CLUT.
 * Reset each frame by grBeginFrame(). */
static uint16 scratchX = 640;
static uint16 scratchY = 4;
static uint16 scratchRowH = 0;

static void grResetScratch(void)
{
    scratchX = 640;
    scratchY = 4;
    scratchRowH = 0;
}

/* Allocate a VRAM rectangle for a 4-bit texture.
 * vramW = width in VRAM pixels (= sprite pixel width / 4, rounded up).
 * Returns 0 on success (outX/outY set), -1 if VRAM scratch is full. */
static int grAllocScratch(uint16 vramW, uint16 h, uint16 *outX, uint16 *outY)
{
    /* Ensure sprite fits within current texture page (64 VRAM pixels wide) */
    uint16 pageEnd = ((scratchX / 64) + 1) * 64;
    if (scratchX + vramW > pageEnd)
        scratchX = pageEnd;

    /* Horizontal overflow → advance to next row */
    if (scratchX + vramW > 1024) {
        scratchY += scratchRowH;
        scratchX = 640;
        scratchRowH = 0;
    }

    /* Ensure V coordinate won't overflow uint8 within the 256-line bank */
    if ((scratchY & 0xFF) + h > 255) {
        scratchY = ((scratchY >> 8) + 1) << 8;
        scratchX = 640;
        scratchRowH = 0;
    }

    /* Vertical bounds check */
    if (scratchY + h > 512)
        return -1;

    *outX = scratchX;
    *outY = scratchY;
    scratchX += vramW;
    if (h > scratchRowH) scratchRowH = h;
    return 0;
}

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
    grCaptureResetFrameDraws();

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

    /* Rebuild byte-pair LUTs for compositing span functions */
    grRebuildPaletteLuts();
}

/*
 * Swap display buffers
 */
void grRefreshDisplay()
{
    /* Wait for GPU DMA to finish (LoadImage operations) */
    DrawSync(0);

    /* VSync is called in grUpdateDisplay before LoadImage to prevent tearing.
     * This function just ensures DMA is complete for standalone use cases. */
}

void grSetPs1Telemetry(int enabled)
{
    grPs1TelemetryEnabled = (enabled ? 1 : 0);
}

/*
 * Toggle fullscreen (no-op on PS1, always fullscreen)
 */
void grToggleFullScreen()
{
    /* PS1 is always fullscreen */
    grWindowed = !grWindowed;  /* Keep variable for compatibility */
}

/* Batched swap buffer for nibble-swapped sprite data during GPU upload.
 * Each sprite's swapped data is placed at a running offset so multiple
 * LoadImage calls can be queued WITHOUT per-sprite DrawSync.
 * 32KB supports ~10-15 sprites per frame.  Overflow falls back to software.
 * Must be 4-byte aligned for DMA (LoadImage reads uint32 words). */
#define GPU_SWAP_BUF_SIZE 32768
static uint32 gpuSwapBuf32[GPU_SWAP_BUF_SIZE / 4];
static uint32 gpuSwapOffset = 0;  /* Running byte offset into swap buffer */

/* Set to 1 by grBeginFrame(); cleared after DrawOTag in grUpdateDisplay.
 * Prevents DrawOTag on a stale/uninitialized OT when grBeginFrame was
 * not called (e.g. intro screens). */
static int gpuFrameReady = 0;

/*
 * Per-frame initialisation: clear OT, reset primitive buffer and VRAM scratch.
 * Must be called before any sprite draws in a frame.
 */
void grBeginFrame(void)
{
    /* Reset OT and primitive buffer each frame.
     * Required because VRAM-based sprites (from grLoadBmp) still emit
     * GPU primitives into the OT — without reset the buffer overflows. */
    ClearOTagR(ot[0], OT_LENGTH);
    nextPrimitive[0] = primitiveBuffer[0];
    primitiveIndex[0] = 0;
}

/*
 * Upload an indexed sprite to VRAM scratch space and emit GPU primitives.
 * - Nibble-swaps Sierra format (HIGH=even) → PS1 format (LOW=pixel0)
 * - Allocates temporary VRAM rectangle via scratch allocator
 * - Emits DR_TPAGE + SPRT (normal) or POLY_FT4 (flip) into ot[0]
 * Returns 0 on success, -1 on failure (caller should fall back to software).
 */
static int grUploadAndDrawGpuSprite(const uint8 *indexedPixels, uint16 w, uint16 h,
                                     sint16 screenX, sint16 screenY, int flip,
                                     int psbNibbles)
{
    if (!gpuFrameReady) return -1;
    if (w > 256 || w == 0 || h == 0) return -1;

    uint32 indexedSize = ((uint32)w * (uint32)h + 1) / 2;
    /* Round up to 4-byte alignment for DMA */
    uint32 alignedSize = (indexedSize + 3) & ~3u;

    /* Check swap buffer has room for this sprite */
    if (gpuSwapOffset + alignedSize > GPU_SWAP_BUF_SIZE) return -1;

    /* VRAM width for 4-bit texture (1 VRAM pixel = 4 texture pixels) */
    uint16 vramW = (w + 3) / 4;

    uint16 vramX, vramY;
    if (grAllocScratch(vramW, h, &vramX, &vramY) < 0) return -1;

    uint8 *dst = (uint8 *)gpuSwapBuf32 + gpuSwapOffset;
    if (psbNibbles) {
        /* PSB format: already in PS1 nibble order — direct copy, no swap */
        memcpy(dst, indexedPixels, indexedSize);
    } else {
        /* Sierra format: nibble-swap into swap buffer.
         * Sierra: HIGH nibble = even pixel.  PS1: LOW nibble = pixel 0.
         * Process 4 bytes at a time via uint32 bitwise operations. */
        uint32 i = 0;
        const uint32 *src32 = (const uint32 *)indexedPixels;
        uint32 *dst32 = (uint32 *)dst;
        uint32 count32 = indexedSize >> 2;
        for (uint32 j = 0; j < count32; j++) {
            uint32 w = src32[j];
            dst32[j] = ((w & 0x0F0F0F0Fu) << 4) | ((w >> 4) & 0x0F0F0F0Fu);
        }
        i = count32 << 2;
        /* Handle remaining bytes */
        for (; i < indexedSize; i++) {
            uint8 b = indexedPixels[i];
            dst[i] = ((b & 0x0F) << 4) | ((b >> 4) & 0x0F);
        }
    }

    /* Upload to VRAM scratch — NO DrawSync here, all uploads batched */
    RECT r;
    setRECT(&r, vramX, vramY, vramW, h);
    LoadImage(&r, (uint32 *)dst);

    /* Advance offset for next sprite */
    gpuSwapOffset += alignedSize;

    /* Texture page and UV coordinates */
    uint16 tpX = (vramX / 64) * 64;
    uint16 tpY = (vramY / 256) * 256;
    uint8 u0 = ((vramX % 64) * 4) & 0xFF;
    uint8 v0 = (vramY % 256) & 0xFF;

    if (!flip) {
        /* Non-flip: SPRT + DR_TPAGE */
        uint32 needed = sizeof(DR_TPAGE) + sizeof(SPRT);
        if (primitiveIndex[0] + needed > PRIMITIVE_BUFFER_SIZE) return -1;

        SPRT *sp = (SPRT *)nextPrimitive[0];
        nextPrimitive[0] += sizeof(SPRT);
        primitiveIndex[0] += sizeof(SPRT);
        setSprt(sp);
        setXY0(sp, screenX, screenY);
        setWH(sp, w, h);
        setUV0(sp, u0, v0);
        setClut(sp, 640, 0);
        setRGB0(sp, 128, 128, 128);
        addPrim(&ot[0][0], sp);

        DR_TPAGE *tp = (DR_TPAGE *)nextPrimitive[0];
        nextPrimitive[0] += sizeof(DR_TPAGE);
        primitiveIndex[0] += sizeof(DR_TPAGE);
        setDrawTPage(tp, 0, 0, getTPage(0, 0, tpX, tpY));
        addPrim(&ot[0][0], tp);
    } else {
        /* Flip: POLY_FT4 (has built-in tpage field) */
        uint32 needed = sizeof(POLY_FT4);
        if (primitiveIndex[0] + needed > PRIMITIVE_BUFFER_SIZE) return -1;

        POLY_FT4 *poly = (POLY_FT4 *)nextPrimitive[0];
        nextPrimitive[0] += sizeof(POLY_FT4);
        primitiveIndex[0] += sizeof(POLY_FT4);
        setPolyFT4(poly);
        setXY4(poly,
               screenX, screenY,
               screenX + w, screenY,
               screenX, screenY + h,
               screenX + w, screenY + h);
        poly->tpage = getTPage(0, 0, tpX, tpY);
        /* Reversed U for horizontal flip */
        uint8 u1 = u0 + (uint8)w;
        uint8 v1 = v0 + (uint8)h;
        setUV4(poly, u1, v0, u0, v0, u1, v1, u0, v1);
        setClut(poly, 640, 0);
        setRGB0(poly, 128, 128, 128);
        addPrim(&ot[0][0], poly);
    }

    return 0;
}

/*
 * Replay a previously-drawn sprite via GPU upload.
 * Falls back to software composite if GPU path fails.
 */
void grReplaySprite(struct TDrawnSprite *ds)
{
    if (!ds || !ds->indexedPixels) return;

    PS1Surface tmpSfc = {0};
    tmpSfc.indexedPixels = ds->indexedPixels;
    tmpSfc.width = ds->width;
    tmpSfc.height = ds->height;
    tmpSfc.psbNibbles = ds->psbNibbles;
    if (ds->flip)
        grCompositeToBackgroundFlip(&tmpSfc, ds->x, ds->y);
    else
        grCompositeToBackground(&tmpSfc, ds->x, ds->y);
}

/*
 * Update display with all layers
 */
void grUpdateDisplay(struct TTtmThread *ttmBackgroundThread,
                     struct TTtmThread *ttmThreads,
                     struct TTtmThread *ttmHolidayThread)
{
    /* PS1 uses RAM-based compositing - sprites are drawn to bgTile buffers
     * via grCompositeToBackground(). By this point:
     * - grRestoreBgTiles was called before ttmPlay (at frame start)
     * - ttmPlay drew sprites to bgTile via grCompositeToBackground
     * Now we just need to upload and display.
     */

    /* Draw persistent diagnostics directly into composited background. */
    if (grPs1TelemetryEnabled) {
        grRefreshLoadedResourceCounters();
        grDrawDropDiagnostics();
        grDrawMemoryDiagnostics();
        grDrawStoryDiagnostics();
        grDrawPilotPackDiagnostics();
        grDrawAdsFreezeDiagnostics();
    }
    grCaptureEmitFrameMetadataLine();

    /* Wait for VSync BEFORE uploading to framebuffer.
     * This ensures we write during vertical blank when display isn't scanning. */
    VSync(0);

    if (foregroundPilotRuntimeActive())
        foregroundPilotRuntimeCompose();

    /* Draw capture overlay into the final visible frame. Do not merge it back
     * into clean background state. Draw it into the RAM background right before
     * upload so the current frame captures it, and the next frame's restore
     * still wipes it back out. */
    if (grCaptureOverlay) {
        grDrawCaptureOverlay();
        grDrawCaptureActorPanel();
    }

    /* Upload background tiles (with sprites composited in software) to framebuffer */
    grDrawBackground();

    /* Handle frame timing */
    eventsWaitTick(grUpdateDelay);

    grCurrentFrame++;
    grCaptureResetFrameDraws();
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
    sfc->indexedPixels = NULL;
    sfc->indexedOwned = 0;
    sfc->psbNibbles = 0;
    sfc->nextTile = NULL;

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
        if (sfc->indexedPixels && sfc->indexedOwned) free(sfc->indexedPixels);
        if (sfc->pixels) free(sfc->pixels);
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
     * PS1 FIX: Always use RAM-based rendering path.
     *
     * The VRAM/OT path adds primitives to global ot[db], but the main game loop
     * uses a local gameOT for DrawOTag. This causes sprites to never be drawn!
     *
     * RAM-based sprites use grCompositeToBackground() which writes directly to
     * background tile buffers, correctly rendering with the main loop.
     */
    grLoadBmpRAM(ttmSlot, slotNo, strArg);
    return;

    /* === DISABLED: VRAM/OT path (kept for reference) ===
     *
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

    /* For multi-tile sprites, use RAM-based rendering to bypass LoadImage limit */
    if (needsMultiTile) {
        grLoadBmpRAM(ttmSlot, slotNo, strArg);
        return;
    }

    /* Load sprite frames from BMP for animation support */
    int numToLoad = bmpResource->numImages;
    if (numToLoad > 42) {
        numToLoad = 42;  /* Max 42 frames (6 pages/row × 7 rows) */
    }
    /* For multi-tile BMPs, limit to 3 frames (DrawSync limit) */
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

        /* Advance by packed 4-bit frame bytes.
         * Row stride must round up per row for odd widths. */
        srcPtr += (((uint32)width + 1U) / 2U) * (uint32)height;

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
        surface->indexedPixels = NULL;
        surface->indexedOwned = 0;
        surface->psbNibbles = 0;
        surface->clutX = 640;
        surface->clutY = 0;
        /* Multi-tile fields */
        surface->fullWidth = width;
        surface->fullHeight = height;
        surface->tileOffsetX = 0;
        surface->tileOffsetY = 0;
        surface->nextTile = NULL;

        /* Multi-tile: Load bottom portion for tall sprites (>64px)
         * NOTE: No DrawSync after LoadImage - too many DrawSync calls breaks rendering */
        if (height > MAX_SPRITE_DIM && frameIdx < 3) {
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

            /* LoadImage bottom tile - skip DrawSync to test if that's the issue */
            RECT bottomRect;
            setRECT(&bottomRect, vramX, bottomVramY, vramW, bottomH);
            LoadImage(&bottomRect, (uint32*)bottomBuf);
            /* DrawSync(0) removed for testing */

            /* Create PS1Surface for bottom tile */
            PS1Surface *bottomTile = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
            bottomTile->width = safeW;
            bottomTile->height = bottomH;
            bottomTile->x = vramX;  /* Same X as main tile */
            bottomTile->y = bottomVramY;  /* Y=68 */
            bottomTile->pixels = bottomBuf;
            bottomTile->indexedPixels = NULL;
            bottomTile->indexedOwned = 0;
            bottomTile->psbNibbles = 0;
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
    ttmSlot->loadedBmpNames[slotNo] = bmpResource->resName;
}

/*
 * Release BMP sprite sheet from slot
 */
void grReleaseBmp(struct TTtmSlot *ttmSlot, uint16 bmpSlotNo)
{
    if (ttmSlot == NULL || bmpSlotNo >= MAX_BMP_SLOTS) {
        return;
    }

    /* Replay records keep raw indexedPixels pointers into BMP/PSB storage.
     * If this slot is being replaced during the active thread's TTM tick,
     * those records must be dropped before the backing storage is freed. */
    if (grCurrentThread != NULL && grCurrentThread->ttmSlot == ttmSlot) {
        uint8 writeIdx = 0;
        for (uint8 readIdx = 0; readIdx < grCurrentThread->numDrawnSprites; readIdx++) {
            struct TDrawnSprite *ds = &grCurrentThread->drawnSprites[readIdx];
            if (ds->imageNo == bmpSlotNo)
                continue;
            if (writeIdx != readIdx)
                grCurrentThread->drawnSprites[writeIdx] = *ds;
            writeIdx++;
        }
        grCurrentThread->numDrawnSprites = writeIdx;
        if (grCurrentThread->replayWriteCursor > writeIdx)
            grCurrentThread->replayWriteCursor = writeIdx;
    }

    /* Invalidate replay records that reference previous contents of this slot. */
    ttmSlot->spriteGen[bmpSlotNo]++;
    ttmSlot->loadedBmp[bmpSlotNo] = NULL;
    ttmSlot->loadedBmpNames[bmpSlotNo] = NULL;

    /* Free all sprites in this slot (PS1Surface structs only;
     * indexedPixels with indexedOwned=0 are NOT freed here because
     * they point into either the BMP resource data or the PSB buffer). */
    for (int i = 0; i < ttmSlot->numSprites[bmpSlotNo]; i++) {
        if (ttmSlot->sprites[bmpSlotNo][i] != NULL) {
            grFreeLayer(ttmSlot->sprites[bmpSlotNo][i]);
            ttmSlot->sprites[bmpSlotNo][i] = NULL;
        }
    }

    /* Free PSB data buffer if this slot was loaded from a PSB file.
     * Must happen AFTER freeing sprites since they pointed into it. */
    if (ttmSlot->psbData[bmpSlotNo] != NULL) {
        free(ttmSlot->psbData[bmpSlotNo]);
        ttmSlot->psbData[bmpSlotNo] = NULL;
    }

    ttmSlot->numSprites[bmpSlotNo] = 0;
}

/*
 * Try to load a pre-transcoded PSB (PS1 Sprite Bundle) file for a BMP resource.
 * PSB files have nibbles pre-swapped to PS1 order, eliminating runtime swap.
 *
 * Uses the compile-time PSB registry for O(log N) lookup — no CD probe needed
 * for BMPs that don't have PSB versions.
 *
 * On success: sprites loaded into slot, psbData stored for lifecycle management,
 * bmpResource stored in loadedBmp for dedup.
 * Returns 1 on success, 0 if no PSB available or load failed.
 */
static int grTryLoadPsb(struct TTtmSlot *ttmSlot, uint16 slotNo,
                         char *strArg, struct TBmpResource *bmpResource)
{
    char psbPath[64];
    char psbName[32];
    int nameLen;
    int i;
    uint32 psbSize;
    uint8 *psbBuf;
    PSBHeader *hdr;
    PSBFrame *frames;
    uint8 *pixelBase;
    int numToLoad;
    int framesLoaded;
    uint32 frameTableEnd;

    /* Diagnostic guard: keep JOHNWALK on the legacy BMP path until the
     * new PSB-backed sprite route is proven correct across ACTIVITY/MISCGAG. */
    if (strcmp(strArg, "JOHNWALK.BMP") == 0)
        return 0;

    /* Fast registry lookup — avoids any CD access for unknown BMPs. */
    psbSize = psbRegistryLookup(strArg);
    if (psbSize == 0) return 0;

    /* Build PSB name: JOHNWALK.BMP -> JOHNWALK.PSB */
    nameLen = strlen(strArg);
    if (nameLen < 5 || nameLen > 28) return 0;
    memcpy(psbName, strArg, nameLen + 1);
    /* Replace .BMP extension with .PSB */
    if (psbName[nameLen-4] == '.' &&
        (psbName[nameLen-3] == 'B' || psbName[nameLen-3] == 'b') &&
        (psbName[nameLen-2] == 'M' || psbName[nameLen-2] == 'm') &&
        (psbName[nameLen-1] == 'P' || psbName[nameLen-1] == 'p')) {
        psbName[nameLen-3] = 'P';
        psbName[nameLen-2] = 'S';
        psbName[nameLen-1] = 'B';
    } else {
        return 0;
    }

    /* Try loading PSB from the active scene pack first (offset-based,
     * no CdSearchFile needed — much faster than standalone file lookup). */
    psbBuf = ps1PilotLoadPsb(psbName, &psbSize);

    /* Fallback: load standalone PSB file from CD PSB/ directory.
     * This path does a CdSearchFile but only triggers for scenes
     * without a compiled pack or when a PSB is missing from the pack.
     * psbSize was already set from the registry lookup above. */
    if (psbBuf == NULL) {
        snprintf(psbPath, sizeof(psbPath), "PSB\\%s", psbName);
        psbBuf = ps1_streamRead(psbPath, 0, psbSize);
        if (psbBuf == NULL) return 0;
    }

    /* Validate PSB header */
    if (psbSize < sizeof(PSBHeader)) {
        free(psbBuf);
        return 0;
    }

    hdr = (PSBHeader *)psbBuf;
    if (hdr->magic != PSB_MAGIC || hdr->version != PSB_VERSION) {
        free(psbBuf);
        return 0;
    }

    if (hdr->numFrames == 0 || hdr->dataOffset > psbSize) {
        free(psbBuf);
        return 0;
    }

    /* Cross-check totalSize against actual buffer size */
    if (hdr->totalSize > psbSize) {
        free(psbBuf);
        return 0;
    }

    /* Verify frame table fits */
    frameTableEnd = sizeof(PSBHeader) + (uint32)hdr->numFrames * sizeof(PSBFrame);
    if (frameTableEnd > hdr->dataOffset) {
        free(psbBuf);
        return 0;
    }

    /* Release any existing sprites in this slot */
    if (ttmSlot->numSprites[slotNo])
        grReleaseBmp(ttmSlot, slotNo);

    frames = (PSBFrame *)(psbBuf + sizeof(PSBHeader));
    pixelBase = psbBuf + hdr->dataOffset;
    numToLoad = hdr->numFrames;
    if (numToLoad > MAX_SPRITES_PER_BMP)
        numToLoad = MAX_SPRITES_PER_BMP;

    framesLoaded = 0;
    for (i = 0; i < numToLoad; i++) {
        PSBFrame *fr = &frames[i];
        PS1Surface *surface;

        /* Validate frame bounds against actual buffer */
        if (hdr->dataOffset + fr->offset + fr->size > psbSize) break;

        /* Validate frame dimensions — reject corrupt entries that would
         * waste RAM or confuse the compositing path (max 640x480). */
        if (fr->width == 0 || fr->height == 0 ||
            fr->width > 640 || fr->height > 480) break;

        /* Use malloc (not safe_malloc) so OOM falls back to BMP path
         * instead of halting the PS1 via fatalError. */
        surface = (PS1Surface*)malloc(sizeof(PS1Surface));
        if (!surface) break;

        surface->width = fr->width;
        surface->height = fr->height;
        surface->x = 0;  /* RAM-based, not in VRAM */
        surface->y = 0;
        surface->clutX = 0;
        surface->clutY = 0;
        surface->nextTile = NULL;
        surface->pixels = NULL;

        /* Point directly into PSB data buffer (zero-copy).
         * PSB data is in PS1 nibble order — no runtime swap needed. */
        surface->indexedPixels = pixelBase + fr->offset;
        surface->indexedOwned = 0;  /* Don't free per-frame; whole buffer freed on release */
        surface->psbNibbles = 1;    /* Flag: PS1 nibble order */

        /* Multi-tile fields (not used for PSB, but init for safety) */
        surface->fullWidth = fr->width;
        surface->fullHeight = fr->height;
        surface->tileOffsetX = 0;
        surface->tileOffsetY = 0;

        ttmSlot->sprites[slotNo][i] = surface;
        framesLoaded++;
    }

    ttmSlot->numSprites[slotNo] = framesLoaded;

    /* If no frames loaded (corruption or OOM), free everything and fall back. */
    if (framesLoaded == 0) {
        free(psbBuf);
        return 0;
    }

    /* NOTE: The PSB header + frame table (~16 + 12*N bytes) remain in the
     * buffer.  Trimming them via memmove + realloc + pointer fixup was
     * considered (TODO 3), but the savings are tiny (e.g. 1.5KB for 120
     * frames) vs. the memmove cost on a 33MHz R3000 and the fragility of
     * post-realloc pointer adjustment.  Keeping the header in-place is
     * simpler and safer. */

    /* Lifecycle: store PSB buffer in slot so grReleaseBmp can free it. */
    ttmSlot->psbData[slotNo] = psbBuf;

    /* Dedup: store the BMP resource pointer so repeated loads of the same
     * BMP into the same slot are detected and skipped by grLoadBmpRAM. */
    ttmSlot->loadedBmp[slotNo] = bmpResource;
    ttmSlot->loadedBmpNames[slotNo] = bmpResource->resName;

    return 1;
}

/*
 * Load BMP sprites into RAM as 4-bit indexed data (compact storage)
 * Stores raw 4-bit packed pixels in indexedPixels, palette lookup at composite time.
 * This uses 4x less memory than the previous 15-bit direct color approach.
 *
 * Load order:
 * 1. Dedup check — if this exact BMP is already loaded in the slot, keep it.
 * 2. PSB fast path — if a pre-transcoded PSB exists on CD, use it (zero-copy,
 *    no runtime nibble swap, compile-time registry lookup).
 * 3. BMP fallback — decompress from RESOURCE.001 / extracted BMP files.
 */
void grLoadBmpRAM(struct TTtmSlot *ttmSlot, uint16 slotNo, char *strArg)
{
    struct TBmpResource *bmpResource = findBmpResource(strArg);
    gStatLastBmpSlot = (uint16)(slotNo + 1U);
    gStatLastBmpFrames = 0;
    gStatLastBmpStatus = 1;  /* attempt */
    if (!bmpResource) {
        gStatLastBmpStatus = 2;  /* resource missing */
        return;
    }

    /* Dedup: if slot already has this exact BMP loaded (via PSB or BMP), keep it. */
    if (ttmSlot->numSprites[slotNo] > 0 && ttmSlot->loadedBmp[slotNo] == bmpResource) {
        gStatLastBmpFrames = ttmSlot->numSprites[slotNo];
        gStatLastBmpStatus = 3;  /* reused existing slot */
        return;
    }

    /* PSB fast path: try pre-transcoded PSB file (skips nibble swap).
     * Pass bmpResource so PSB loader can set loadedBmp for dedup. */
    if (grTryLoadPsb(ttmSlot, slotNo, strArg, bmpResource)) {
        return;
    }

    /* BMP fallback path */
    if (ttmSlot->numSprites[slotNo])
        grReleaseBmp(ttmSlot, slotNo);

    /* On-demand loading: load BMP data if not already loaded */
    if (!bmpResource->uncompressedData) {
        ps1_loadBmpData(bmpResource);
    }

    if (!bmpResource->uncompressedData) {
        gStatLastBmpStatus = 4;  /* no BMP bytes after load */
        return;
    }
    if (bmpResource->numImages < 1) {
        gStatLastBmpStatus = 5;  /* zero image metadata */
        return;
    }

    {
        int numToLoad = bmpResource->numImages;
        uint8 *srcPtr = bmpResource->uncompressedData;
        int framesLoaded = 0;

        if (numToLoad > MAX_SPRITES_PER_BMP) {
            grPs1StatBmpFrameCap((uint16)bmpResource->numImages, MAX_SPRITES_PER_BMP);
            fatalError("BMP frame overflow: %s has %d frames, MAX_SPRITES_PER_BMP=%d",
                       strArg, numToLoad, MAX_SPRITES_PER_BMP);
        }

        for (int frameIdx = 0; frameIdx < numToLoad; frameIdx++) {
            uint16 width = bmpResource->widths[frameIdx];
            uint16 height = bmpResource->heights[frameIdx];
            uint32 indexedSize = ((uint32)width * (uint32)height + 1) / 2;

            /* Allocate PS1Surface */
            PS1Surface *surface = (PS1Surface*)malloc(sizeof(PS1Surface));
            if (!surface) {
                gStatLastBmpStatus = 6;  /* allocation failure / partial install */
                break;
            }

            surface->width = width;
            surface->height = height;
            surface->x = 0;  /* Not in VRAM - RAM only */
            surface->y = 0;
            surface->clutX = 0;
            surface->clutY = 0;
            surface->nextTile = NULL;
            surface->pixels = NULL;  /* Not using 15-bit direct color */

            /* Zero-copy indexed frame: reference BMP resource memory directly.
             * This removes per-frame malloc/memcpy churn and cuts fragmentation. */
            surface->indexedPixels = srcPtr;
            surface->indexedOwned = 0;
            surface->psbNibbles = 0;

            /* Advance source pointer for next frame */
            srcPtr += indexedSize;

            /* Store in slot */
            ttmSlot->sprites[slotNo][frameIdx] = surface;
            framesLoaded++;
        }

        ttmSlot->numSprites[slotNo] = framesLoaded;
        ttmSlot->loadedBmp[slotNo] = bmpResource;
        ttmSlot->loadedBmpNames[slotNo] = bmpResource->resName;
        gStatLastBmpFrames = (uint16)framesLoaded;
        gStatLastBmpStatus = (framesLoaded == numToLoad) ? 7 : 8;  /* ok / short install */
        if (framesLoaded < numToLoad) {
            grPs1StatBmpShortLoad((uint16)numToLoad, (uint16)framesLoaded);
        }
    }
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
        /* No DrawSync here - let main loop handle sync */
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
        DrawSync(0);  /* Must sync before freeing buffer - LoadImage is async! */

        free(tempBuf);
    }
}

/* Indexed compositing helpers: simple per-byte decode.
 * Keep this path conservative; fishing regressions started after the later
 * LUT/unrolled fast path landed. */
static inline void compositeIndexedSpanFwd(uint16 *dst, const uint8 *src,
                                           uint32 pixelIdx, int count,
                                           const uint16 *pal)
{
    int di = 0;
    if (count <= 0) return;

    if (pixelIdx & 1) {
        uint8 packed = src[pixelIdx >> 1];
        uint16 p = pal[packed & 0x0F];
        if (p) dst[di] = p;
        di++;
        pixelIdx++;
        count--;
    }

    if (count >= 2) {
        while (count >= 2) {
            uint8 packed = src[pixelIdx >> 1];
            uint16 p0 = pal[(packed >> 4) & 0x0F];
            uint16 p1 = pal[packed & 0x0F];
            if (p0) dst[di] = p0;
            if (p1) dst[di + 1] = p1;
            di += 2;
            pixelIdx += 2;
            count -= 2;
        }
    }

    if (count) {
        uint8 packed = src[pixelIdx >> 1];
        uint16 p = pal[(packed >> 4) & 0x0F];
        if (p) dst[di] = p;
    }
}

static inline void compositeIndexedSpanRev(uint16 *dst, const uint8 *src,
                                           uint32 pixelIdx, int count,
                                           const uint16 *pal)
{
    int di = 0;
    if (count <= 0) return;

    if ((pixelIdx & 1) == 0) {
        uint8 packed = src[pixelIdx >> 1];
        uint16 p = pal[(packed >> 4) & 0x0F];
        if (p) dst[di] = p;
        di++;
        pixelIdx--;
        count--;
    }

    if (count >= 2) {
        while (count >= 2) {
            uint8 packed = src[pixelIdx >> 1];
            uint16 p0 = pal[packed & 0x0F];
            uint16 p1 = pal[(packed >> 4) & 0x0F];
            if (p0) dst[di] = p0;
            if (p1) dst[di + 1] = p1;
            di += 2;
            pixelIdx -= 2;
            count -= 2;
        }
    }

    if (count) {
        uint8 packed = src[pixelIdx >> 1];
        uint16 p = pal[packed & 0x0F];
        if (p) dst[di] = p;
    }
}

/* PS1-nibble-order compositing helpers for PSB sprites.
 * Keep these simple as well while chasing fishing sprite loss. */
static inline void compositePsbSpanFwd(uint16 *dst, const uint8 *src,
                                       uint32 pixelIdx, int count,
                                       const uint16 *pal)
{
    int di = 0;
    if (count <= 0) return;

    if (pixelIdx & 1) {
        uint8 packed = src[pixelIdx >> 1];
        uint16 p = pal[(packed >> 4) & 0x0F];
        if (p) dst[di] = p;
        di++;
        pixelIdx++;
        count--;
    }

    if (count >= 2) {
        while (count >= 2) {
            uint8 packed = src[pixelIdx >> 1];
            uint16 p0 = pal[packed & 0x0F];
            uint16 p1 = pal[(packed >> 4) & 0x0F];
            if (p0) dst[di] = p0;
            if (p1) dst[di + 1] = p1;
            di += 2;
            pixelIdx += 2;
            count -= 2;
        }
    }

    if (count) {
        uint8 packed = src[pixelIdx >> 1];
        uint16 p = pal[packed & 0x0F];
        if (p) dst[di] = p;
    }
}

static inline void compositePsbSpanRev(uint16 *dst, const uint8 *src,
                                       uint32 pixelIdx, int count,
                                       const uint16 *pal)
{
    int di = 0;
    if (count <= 0) return;

    if ((pixelIdx & 1) == 0) {
        uint8 packed = src[pixelIdx >> 1];
        uint16 p = pal[packed & 0x0F];
        if (p) dst[di] = p;
        di++;
        pixelIdx--;
        count--;
    }

    if (count >= 2) {
        while (count >= 2) {
            uint8 packed = src[pixelIdx >> 1];
            uint16 p0 = pal[(packed >> 4) & 0x0F];
            uint16 p1 = pal[packed & 0x0F];
            if (p0) dst[di] = p0;
            if (p1) dst[di + 1] = p1;
            di += 2;
            pixelIdx -= 2;
            count -= 2;
        }
    }

    if (count) {
        uint8 packed = src[pixelIdx >> 1];
        uint16 p = pal[(packed >> 4) & 0x0F];
        if (p) dst[di] = p;
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
    if (!sprite) return;
    if (!sprite->pixels && !sprite->indexedPixels) return;

    int sprW = sprite->width;
    int sprH = sprite->height;

    /* Sanity check - prevent hang from corrupt/freed sprite data */
    if (sprW == 0 || sprH == 0 || sprW > 640 || sprH > 480) return;

    /* Choose indexed or direct color path */
    int useIndexed = (sprite->indexedPixels != NULL);
    int usePsb = (useIndexed && sprite->psbNibbles);

    int startSy = 0;
    int endSy = sprH;
    if (screenY < 0) startSy = -screenY;
    if (screenY + endSy > 480) endSy = 480 - screenY;
    if (startSy >= endSy) return;

    int startSx = 0;
    int endSx = sprW;
    if (screenX < 0) startSx = -screenX;
    if (screenX + endSx > 640) endSx = 640 - screenX;
    if (startSx >= endSx) return;

    /* Mark dirty region for this sprite */
    grMarkRectDirty(screenX + startSx, screenY + startSy,
                    screenX + endSx, screenY + endSy);

    const uint16 *pal = ttmPalette;

    for (int sy = startSy; sy < endSy; sy++) {
        int destY = screenY + sy;

        /* Determine tile row once per scanline */
        PS1Surface *tileLeft, *tileRight;
        int tileLocalY;
        if (destY < 240) {
            tileLocalY = destY;
            tileLeft = bgTile0;
            tileRight = bgTile1;
        } else {
            tileLocalY = destY - 240;
            tileLeft = bgTile3;
            tileRight = bgTile4;
        }

        uint16 *rowLeft = (tileLeft && tileLeft->pixels) ? (tileLeft->pixels + tileLocalY * (int)tileLeft->width) : NULL;
        uint16 *rowRight = (tileRight && tileRight->pixels) ? (tileRight->pixels + tileLocalY * (int)tileRight->width) : NULL;
        uint32 srcRowBase = (uint32)sy * (uint32)sprW;
        int destStartX = screenX + startSx;
        int destEndX = screenX + endSx;

        if (usePsb) {
            /* PSB (PS1 nibble order): skip nibble swap — data is pre-transcoded */
            if (rowLeft && destStartX < 320) {
                int lx0 = destStartX;
                int lx1 = (destEndX < 320) ? destEndX : 320;
                int srcX = startSx + (lx0 - destStartX);
                int span = lx1 - lx0;
                compositePsbSpanFwd(rowLeft + lx0, sprite->indexedPixels,
                                    srcRowBase + (uint32)srcX, span, pal);
            }

            if (rowRight && destEndX > 320) {
                int rx0 = (destStartX > 320) ? destStartX : 320;
                int rx1 = destEndX;
                int srcX = startSx + (rx0 - destStartX);
                int span = rx1 - rx0;
                compositePsbSpanFwd(rowRight + (rx0 - 320), sprite->indexedPixels,
                                    srcRowBase + (uint32)srcX, span, pal);
            }
        } else if (useIndexed) {
            if (rowLeft && destStartX < 320) {
                int lx0 = destStartX;
                int lx1 = (destEndX < 320) ? destEndX : 320;
                int srcX = startSx + (lx0 - destStartX);
                int span = lx1 - lx0;
                compositeIndexedSpanFwd(rowLeft + lx0, sprite->indexedPixels,
                                        srcRowBase + (uint32)srcX, span, pal);
            }

            if (rowRight && destEndX > 320) {
                int rx0 = (destStartX > 320) ? destStartX : 320;
                int rx1 = destEndX;
                int srcX = startSx + (rx0 - destStartX);
                int span = rx1 - rx0;
                compositeIndexedSpanFwd(rowRight + (rx0 - 320), sprite->indexedPixels,
                                        srcRowBase + (uint32)srcX, span, pal);
            }
        } else {
            if (rowLeft && destStartX < 320) {
                int lx0 = destStartX;
                int lx1 = (destEndX < 320) ? destEndX : 320;
                int srcX = startSx + (lx0 - destStartX);
                int dx = lx0;
                for (; dx + 1 < lx1; dx += 2, srcX += 2) {
                    uint16 p0 = sprite->pixels[srcRowBase + (uint32)srcX];
                    uint16 p1 = sprite->pixels[srcRowBase + (uint32)srcX + 1];
                    if (p0 != 0x0000) rowLeft[dx] = p0;
                    if (p1 != 0x0000) rowLeft[dx + 1] = p1;
                }
                if (dx < lx1) {
                    uint16 p = sprite->pixels[srcRowBase + (uint32)srcX];
                    if (p != 0x0000) rowLeft[dx] = p;
                }
            }

            if (rowRight && destEndX > 320) {
                int rx0 = (destStartX > 320) ? destStartX : 320;
                int rx1 = destEndX;
                int srcX = startSx + (rx0 - destStartX);
                int dx = rx0;
                for (; dx + 1 < rx1; dx += 2, srcX += 2) {
                    uint16 p0 = sprite->pixels[srcRowBase + (uint32)srcX];
                    uint16 p1 = sprite->pixels[srcRowBase + (uint32)srcX + 1];
                    if (p0 != 0x0000) rowRight[dx - 320] = p0;
                    if (p1 != 0x0000) rowRight[dx - 319] = p1;
                }
                if (dx < rx1) {
                    uint16 p = sprite->pixels[srcRowBase + (uint32)srcX];
                    if (p != 0x0000) rowRight[dx - 320] = p;
                }
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
 * Draw line — software composite to background tiles.
 * (GPU primitives are reserved for the sprite OT rendered by DrawOTag.)
 */
void grDrawLine(PS1Surface *sfc, sint16 x1, sint16 y1, sint16 x2, sint16 y2, uint8 color)
{
    /* Stub — TTM line draws are rare and cosmetic (e.g. fishing line).
     * Previously these GPU primitives were silently accumulated but never
     * rendered (no DrawOTag).  TODO: implement software line rasterizer. */
    (void)sfc; (void)x1; (void)y1; (void)x2; (void)y2; (void)color;
}

/*
 * Draw filled rectangle — software composite to background tiles.
 * Used by TTM DRAW_RECT opcode for screen clears and overlays.
 */
static void grDrawRectColor15(sint16 x, sint16 y, uint16 width, uint16 height, uint16 bgColor)
{
    /* Software fill directly into bgTile buffers (matching composite approach).
     * This replaces the GPU POLY_F3 path that was never rendered before. */
    sint16 x2 = x + (sint16)width;
    sint16 y2 = y + (sint16)height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > 640) x2 = 640;
    if (y2 > 480) y2 = 480;
    if (x >= x2 || y >= y2) return;

    /* Mark dirty region */
    grMarkRectDirty(x, y, x2, y2);

    /* Pack two pixels into uint32 for word-fill */
    uint32 bgColor32 = (uint32)bgColor | ((uint32)bgColor << 16);

    for (sint16 py = y; py < y2; py++) {
        int tileLocalY = (py < 240) ? py : py - 240;
        PS1Surface *tileLeft = (py < 240) ? bgTile0 : bgTile3;
        PS1Surface *tileRight = (py < 240) ? bgTile1 : bgTile4;

        if (x < 320 && tileLeft && tileLeft->pixels) {
            sint16 fillStart = x;
            sint16 fillEnd = (x2 < 320) ? x2 : 320;
            uint16 *dst = tileLeft->pixels + (tileLocalY * (int)tileLeft->width) + fillStart;
            int fillCount = fillEnd - fillStart;
            /* Word-align: handle odd start pixel */
            if (fillCount > 0 && ((uintptr_t)dst & 2)) {
                *dst++ = bgColor;
                fillCount--;
            }
            /* Fill 2 pixels per uint32 store */
            uint32 *dst32 = (uint32 *)dst;
            while (fillCount >= 2) {
                *dst32++ = bgColor32;
                fillCount -= 2;
            }
            /* Handle trailing pixel */
            if (fillCount) {
                *(uint16 *)dst32 = bgColor;
            }
        }

        if (x2 > 320 && tileRight && tileRight->pixels) {
            sint16 fillStart = (x > 320) ? x : 320;
            sint16 fillEnd = x2;
            uint16 *dst = tileRight->pixels + (tileLocalY * (int)tileRight->width) + (fillStart - 320);
            int fillCount = fillEnd - fillStart;
            if (fillCount > 0 && ((uintptr_t)dst & 2)) {
                *dst++ = bgColor;
                fillCount--;
            }
            uint32 *dst32 = (uint32 *)dst;
            while (fillCount >= 2) {
                *dst32++ = bgColor32;
                fillCount -= 2;
            }
            if (fillCount) {
                *(uint16 *)dst32 = bgColor;
            }
        }
    }
}

void grDrawRect(PS1Surface *sfc, sint16 x, sint16 y, uint16 width, uint16 height, uint8 color)
{
    (void)sfc;
    grDrawRectColor15(x, y, width, height, ttmPalette[color & 0xF]);
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
static void grRecordReplaySprite(struct TTtmThread *thread,
                                 PS1Surface *sprite, sint16 x, sint16 y,
                                 uint16 spriteNo, uint16 imageNo, uint8 flip)
{
    if (!thread || !sprite || !sprite->indexedPixels) return;

    /* Deduplicate exact same draw within the frame.
     * Fast-path: check last 8 entries first (draws are sequential). */
    int scanStart = (thread->numDrawnSprites > 8) ? thread->numDrawnSprites - 8 : 0;
    for (int pass = 0; pass < 2; pass++) {
        int lo = (pass == 0) ? scanStart : 0;
        int hi = (pass == 0) ? thread->numDrawnSprites : scanStart;
        for (int i = lo; i < hi; i++) {
            struct TDrawnSprite *ds = &thread->drawnSprites[i];
            if (ds->imageNo == imageNo &&
                ds->spriteNo == spriteNo &&
                ds->flip == flip &&
                ds->x == x &&
                ds->y == y &&
                ds->sceneEpoch == thread->sceneEpoch) {
                ds->indexedPixels = sprite->indexedPixels;
                ds->width = sprite->width;
                ds->height = sprite->height;
                ds->psbNibbles = sprite->psbNibbles;
                ds->bmpName = thread->ttmSlot ? thread->ttmSlot->loadedBmpNames[imageNo] : NULL;
                return;
            }
        }
    }

    uint16 recIdx;
    if (thread->numDrawnSprites >= MAX_DRAWN_SPRITES) {
        /* Keep most recent draws when scene density exceeds replay capacity.
         * Dropping new records causes actor vanish (Johnny lost behind props). */
        recIdx = thread->replayWriteCursor;
        thread->replayWriteCursor++;
        if (thread->replayWriteCursor >= MAX_DRAWN_SPRITES)
            thread->replayWriteCursor = 0;
    } else {
        recIdx = thread->numDrawnSprites++;
    }

    struct TDrawnSprite *ds = &thread->drawnSprites[recIdx];
    ds->indexedPixels = sprite->indexedPixels;
    ds->width = sprite->width;
    ds->height = sprite->height;
    ds->x = x;
    ds->y = y;
    ds->spriteNo = spriteNo;
    ds->imageNo = imageNo;
    ds->sceneEpoch = thread->sceneEpoch;
    ds->flip = flip;
    ds->psbNibbles = sprite->psbNibbles;
    ds->bmpName = thread->ttmSlot ? thread->ttmSlot->loadedBmpNames[imageNo] : NULL;
}

void grDrawSprite(PS1Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y,
                  uint16 spriteNo, uint16 imageNo)
{
    x += grDx;
    y += grDy;

    /* Validate imageNo bounds */
    if (imageNo >= MAX_BMP_SLOTS || ttmSlot->numSprites[imageNo] == 0) {
        return;
    }

    /* Wrap sprite index to available frames (handles frame cap) */
    uint16 actualSpriteNo = spriteNo % ttmSlot->numSprites[imageNo];

    PS1Surface *sprite = ttmSlot->sprites[imageNo][actualSpriteNo];
    if (sprite == NULL) {
        return;
    }

    /* RAM-based sprites (loaded via grLoadBmpRAM) have x=0, y=0 with valid pixel data. */
    if (sprite->x == 0 && sprite->y == 0 && (sprite->pixels != NULL || sprite->indexedPixels != NULL)) {
        grCompositeToBackground(sprite, x, y);
        grRecordReplaySprite(grCurrentThread, sprite, x, y, spriteNo, imageNo, 0);
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
 * Composite sprite to background tiles with horizontal flip
 */
void grCompositeToBackgroundFlip(PS1Surface *sprite, sint16 screenX, sint16 screenY)
{
    if (!sprite) return;
    if (!sprite->pixels && !sprite->indexedPixels) return;

    int sprW = sprite->width;
    int sprH = sprite->height;

    /* Sanity check - prevent hang from corrupt/freed sprite data */
    if (sprW == 0 || sprH == 0 || sprW > 640 || sprH > 480) return;

    /* Choose indexed or direct color path */
    int useIndexed = (sprite->indexedPixels != NULL);
    int usePsb = (useIndexed && sprite->psbNibbles);

    int startSy = 0;
    int endSy = sprH;
    if (screenY < 0) startSy = -screenY;
    if (screenY + endSy > 480) endSy = 480 - screenY;
    if (startSy >= endSy) return;

    int startDestX = screenX < 0 ? 0 : screenX;
    int endDestX = screenX + sprW;
    if (endDestX > 640) endDestX = 640;
    if (startDestX >= endDestX) return;

    /* Mark dirty region for this flipped sprite */
    grMarkRectDirty(startDestX, screenY + startSy, endDestX, screenY + endSy);

    const uint16 *pal = ttmPalette;

    /* Iterate over each visible pixel in the sprite (flipped horizontally) */
    for (int sy = startSy; sy < endSy; sy++) {
        int destY = screenY + sy;

        /* Determine tile row once per scanline */
        PS1Surface *tileLeft, *tileRight;
        int tileLocalY;
        if (destY < 240) {
            tileLocalY = destY;
            tileLeft = bgTile0;
            tileRight = bgTile1;
        } else {
            tileLocalY = destY - 240;
            tileLeft = bgTile3;
            tileRight = bgTile4;
        }

        uint16 *rowLeft = (tileLeft && tileLeft->pixels) ? (tileLeft->pixels + tileLocalY * (int)tileLeft->width) : NULL;
        uint16 *rowRight = (tileRight && tileRight->pixels) ? (tileRight->pixels + tileLocalY * (int)tileRight->width) : NULL;
        uint32 srcRowBase = (uint32)sy * (uint32)sprW;

        if (usePsb) {
            /* PSB (PS1 nibble order): pre-transcoded, no runtime swap */
            if (rowLeft && startDestX < 320) {
                int lx0 = startDestX;
                int lx1 = (endDestX < 320) ? endDestX : 320;
                int srcX = sprW - 1 - (lx0 - screenX);
                int span = lx1 - lx0;
                compositePsbSpanRev(rowLeft + lx0, sprite->indexedPixels,
                                    srcRowBase + (uint32)srcX, span, pal);
            }

            if (rowRight && endDestX > 320) {
                int rx0 = (startDestX > 320) ? startDestX : 320;
                int rx1 = endDestX;
                int srcX = sprW - 1 - (rx0 - screenX);
                int span = rx1 - rx0;
                compositePsbSpanRev(rowRight + (rx0 - 320), sprite->indexedPixels,
                                    srcRowBase + (uint32)srcX, span, pal);
            }
        } else if (useIndexed) {
            if (rowLeft && startDestX < 320) {
                int lx0 = startDestX;
                int lx1 = (endDestX < 320) ? endDestX : 320;
                int srcX = sprW - 1 - (lx0 - screenX);
                int span = lx1 - lx0;
                compositeIndexedSpanRev(rowLeft + lx0, sprite->indexedPixels,
                                        srcRowBase + (uint32)srcX, span, pal);
            }

            if (rowRight && endDestX > 320) {
                int rx0 = (startDestX > 320) ? startDestX : 320;
                int rx1 = endDestX;
                int srcX = sprW - 1 - (rx0 - screenX);
                int span = rx1 - rx0;
                compositeIndexedSpanRev(rowRight + (rx0 - 320), sprite->indexedPixels,
                                        srcRowBase + (uint32)srcX, span, pal);
            }
        } else {
            if (rowLeft && startDestX < 320) {
                int lx0 = startDestX;
                int lx1 = (endDestX < 320) ? endDestX : 320;
                int srcX = sprW - 1 - (lx0 - screenX);
                int dx = lx0;
                for (; dx + 1 < lx1; dx += 2, srcX -= 2) {
                    uint16 p0 = sprite->pixels[srcRowBase + (uint32)srcX];
                    uint16 p1 = sprite->pixels[srcRowBase + (uint32)srcX - 1];
                    if (p0 != 0x0000) rowLeft[dx] = p0;
                    if (p1 != 0x0000) rowLeft[dx + 1] = p1;
                }
                if (dx < lx1) {
                    uint16 p = sprite->pixels[srcRowBase + (uint32)srcX];
                    if (p != 0x0000) rowLeft[dx] = p;
                }
            }

            if (rowRight && endDestX > 320) {
                int rx0 = (startDestX > 320) ? startDestX : 320;
                int rx1 = endDestX;
                int srcX = sprW - 1 - (rx0 - screenX);
                int dx = rx0;
                for (; dx + 1 < rx1; dx += 2, srcX -= 2) {
                    uint16 p0 = sprite->pixels[srcRowBase + (uint32)srcX];
                    uint16 p1 = sprite->pixels[srcRowBase + (uint32)srcX - 1];
                    if (p0 != 0x0000) rowRight[dx - 320] = p0;
                    if (p1 != 0x0000) rowRight[dx - 319] = p1;
                }
                if (dx < rx1) {
                    uint16 p = sprite->pixels[srcRowBase + (uint32)srcX];
                    if (p != 0x0000) rowRight[dx - 320] = p;
                }
            }
        }
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

    /* Validate imageNo bounds */
    if (imageNo >= MAX_BMP_SLOTS || ttmSlot->numSprites[imageNo] == 0) {
        return;
    }

    /* Wrap sprite index to available frames (handles frame cap) */
    uint16 actualSpriteNo = spriteNo % ttmSlot->numSprites[imageNo];

    PS1Surface *sprite = ttmSlot->sprites[imageNo][actualSpriteNo];
    if (sprite == NULL) {
        return;
    }

    /* RAM-based sprites (loaded via grLoadBmpRAM) have x=0, y=0 with valid pixel data. */
    if (sprite->x == 0 && sprite->y == 0 && (sprite->pixels != NULL || sprite->indexedPixels != NULL)) {
        grCompositeToBackgroundFlip(sprite, x, y);
        grRecordReplaySprite(grCurrentThread, sprite, x, y, spriteNo, imageNo, 1);
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

    /* RAM-based sprites (loaded via grLoadBmpRAM) have x=0, y=0 with valid pixel data.
     * Composite to background tiles with transparency (0x0000 = transparent).
     * grDrawBackground() will upload the composited tiles later this frame. */
    if (sprite->x == 0 && sprite->y == 0 && (sprite->pixels != NULL || sprite->indexedPixels != NULL)) {
        grCompositeToBackground(sprite, x, y);
        return 0;
    }

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
 * Create an empty background tile (black, for RAM compositing)
 */
static PS1Surface *createEmptyBgTileRAM(uint16 width, uint16 height)
{
    PS1Surface *tile = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
    tile->width = width;
    tile->height = height;
    tile->x = 0;
    tile->y = 0;
    tile->indexedPixels = NULL;
    tile->indexedOwned = 0;
    tile->psbNibbles = 0;
    tile->nextTile = NULL;
    tile->pixels = (uint16*)safe_malloc(width * height * 2);
    /* Fill with black (0x0000 = transparent/black) */
    for (uint32 i = 0; i < width * height; i++) {
        tile->pixels[i] = 0x0000;
    }
    return tile;
}

/*
 * Initialize empty background
 */
void grInitEmptyBackground()
{
    if (grBackgroundSfc != NULL) {
        /* grLoadScreen sets grBackgroundSfc = bgTile0, so freeing it
         * would leave bgTile0 as a dangling pointer. Null out any match. */
        if (grBackgroundSfc == bgTile0) bgTile0 = NULL;
        if (grBackgroundSfc == bgTile1) bgTile1 = NULL;
        if (grBackgroundSfc == bgTile3) bgTile3 = NULL;
        if (grBackgroundSfc == bgTile4) bgTile4 = NULL;
        grFreeLayer(grBackgroundSfc);
    }

    grBackgroundSfc = grNewEmptyBackground();

    /* Create empty RAM tiles for sprite compositing (needed by grCompositeToBackground)
     * If tiles already exist (e.g. after grFadeOut darkened them), zero their pixels */
    if (bgTile0 == NULL) bgTile0 = createEmptyBgTileRAM(320, 240);
    else memset(bgTile0->pixels, 0, 320 * 240 * 2);
    if (bgTile1 == NULL) bgTile1 = createEmptyBgTileRAM(320, 240);
    else memset(bgTile1->pixels, 0, 320 * 240 * 2);
    if (bgTile3 == NULL) bgTile3 = createEmptyBgTileRAM(320, 240);
    else memset(bgTile3->pixels, 0, 320 * 240 * 2);
    if (bgTile4 == NULL) bgTile4 = createEmptyBgTileRAM(320, 240);
    else memset(bgTile4->pixels, 0, 320 * 240 * 2);
}

/*
 * Save clean copies of background tiles (after loading background, before any sprite compositing).
 * Used by composite pattern to restore pristine background each frame.
 *
 * Called from grLoadScreen after background is loaded/composited.
 * Always updates ALL clean copies to current tile state.
 */
void grSaveCleanBgTiles(void)
{
    uint32 tileSize = 320 * 240 * 2;  /* 320x240 @ 16-bit = 153,600 bytes per tile */

    /* Reuse existing buffers when possible to avoid 600KB free+malloc churn.
     * Only allocate if buffer doesn't exist yet; always overwrite content. */
    if (bgTile0 && bgTile0->pixels) {
        if (!bgTile0Clean) bgTile0Clean = (uint16*)malloc(tileSize);
        if (bgTile0Clean) memcpy(bgTile0Clean, bgTile0->pixels, tileSize);
    } else if (bgTile0Clean) { free(bgTile0Clean); bgTile0Clean = NULL; }

    if (bgTile1 && bgTile1->pixels) {
        if (!bgTile1Clean) bgTile1Clean = (uint16*)malloc(tileSize);
        if (bgTile1Clean) memcpy(bgTile1Clean, bgTile1->pixels, tileSize);
    } else if (bgTile1Clean) { free(bgTile1Clean); bgTile1Clean = NULL; }

    /* Bottom tiles - for partial height images (like ISLETEMP), the bottom
     * tiles have been composited with scene data over the ocean base. */
    if (bgTile3 && bgTile3->pixels) {
        if (!bgTile3Clean) bgTile3Clean = (uint16*)malloc(tileSize);
        if (bgTile3Clean) memcpy(bgTile3Clean, bgTile3->pixels, tileSize);
    } else if (bgTile3Clean) { free(bgTile3Clean); bgTile3Clean = NULL; }

    if (bgTile4 && bgTile4->pixels) {
        if (!bgTile4Clean) bgTile4Clean = (uint16*)malloc(tileSize);
        if (bgTile4Clean) memcpy(bgTile4Clean, bgTile4->pixels, tileSize);
    } else if (bgTile4Clean) { free(bgTile4Clean); bgTile4Clean = NULL; }

    /* New clean baseline: mark all tiles dirty so first frame uploads everything.
     * Set prevDirty too since the framebuffer may not match the new background. */
    grMarkAllTilesDirty();
    for (int i = 0; i < 4; i++) {
        prevDirtyMinY[i] = 0;
        prevDirtyMaxY[i] = 239;
    }
}

/*
 * Free clean tile copies to reclaim memory (~600KB).
 * Called when switching to non-island (black) backgrounds where clean copies aren't needed.
 */
void grFreeCleanBgTiles(void)
{
    if (bgTile0Clean) { free(bgTile0Clean); bgTile0Clean = NULL; }
    if (bgTile1Clean) { free(bgTile1Clean); bgTile1Clean = NULL; }
    if (bgTile3Clean) { free(bgTile3Clean); bgTile3Clean = NULL; }
    if (bgTile4Clean) { free(bgTile4Clean); bgTile4Clean = NULL; }

    /* No clean copies → force full upload on next frame */
    grMarkAllTilesDirty();
    for (int i = 0; i < 4; i++) {
        prevDirtyMinY[i] = 0;
        prevDirtyMaxY[i] = 239;
    }
}

/*
 * Ensure clean background copies exist before frame restore/composite.
 */
void grEnsureCleanBgTiles(void)
{
    if (bgTile0Clean && bgTile1Clean && bgTile3Clean && bgTile4Clean) {
        return;
    }
    grSaveCleanBgTiles();
}

/*
 * Restore background tiles from clean copies (call at start of each frame).
 * Only restores rows that were dirtied by the previous frame's compositing.
 * prevDirty is preserved for grDrawBackground's union upload.
 */
void grRestoreBgTiles(void)
{
    PS1Surface *tiles[4] = { bgTile0, bgTile1, bgTile3, bgTile4 };
    const uint16 *clean[4] = { bgTile0Clean, bgTile1Clean, bgTile3Clean, bgTile4Clean };

    /* Clear currDirty for new frame's compositing */
    for (int i = 0; i < 4; i++) {
        currDirtyMinY[i] = -1;
        currDirtyMaxY[i] = -1;
    }

    for (int i = 0; i < 4; i++) {
        if (!tiles[i] || !tiles[i]->pixels || !clean[i]) continue;

        int minY = prevDirtyMinY[i];
        int maxY = prevDirtyMaxY[i];
        if (minY < 0) continue;  /* tile was clean last frame */

        uint32 w = tiles[i]->width;
        uint16 *dst = tiles[i]->pixels + minY * w;
        const uint16 *src = clean[i] + minY * w;
        uint32 copyBytes = (uint32)(maxY - minY + 1) * w * sizeof(uint16);
        memcpy(dst, src, copyBytes);
    }

}

static void grRestoreTileRect(PS1Surface *dstTile,
                              const uint16 *srcClean,
                              int tileScreenX,
                              int tileScreenY,
                              int rectX,
                              int rectY,
                              int rectW,
                              int rectH)
{
    int copyStartX;
    int copyStartY;
    int copyEndX;
    int copyEndY;
    int row;

    if (dstTile == NULL || dstTile->pixels == NULL || srcClean == NULL)
        return;
    if (rectW <= 0 || rectH <= 0)
        return;

    copyStartX = (rectX > tileScreenX) ? rectX : tileScreenX;
    copyStartY = (rectY > tileScreenY) ? rectY : tileScreenY;
    copyEndX = rectX + rectW;
    copyEndY = rectY + rectH;

    if (copyEndX > tileScreenX + (int)dstTile->width)
        copyEndX = tileScreenX + (int)dstTile->width;
    if (copyEndY > tileScreenY + (int)dstTile->height)
        copyEndY = tileScreenY + (int)dstTile->height;

    if (copyStartX >= copyEndX || copyStartY >= copyEndY)
        return;

    for (row = copyStartY; row < copyEndY; row++) {
        int tileRow = row - tileScreenY;
        int tileCol = copyStartX - tileScreenX;
        int copyWidth = copyEndX - copyStartX;
        uint16 *dst = dstTile->pixels + (tileRow * dstTile->width) + tileCol;
        const uint16 *src = srcClean + (tileRow * dstTile->width) + tileCol;
        memcpy(dst, src, (size_t)copyWidth * sizeof(uint16));
    }
}

static void grRestoreRectFromCleanBg(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
        return;
    if (x + width > SCREEN_WIDTH)
        width = SCREEN_WIDTH - x;
    if (y + height > SCREEN_HEIGHT)
        height = SCREEN_HEIGHT - y;

    /* Tile pixels are being modified — mark dirty for upload */
    grMarkRectDirty(x, y, x + width, y + height);
    if (width <= 0 || height <= 0)
        return;

    grEnsureCleanBgTiles();
    grRestoreTileRect(bgTile0, bgTile0Clean, 0, 0, x, y, width, height);
    grRestoreTileRect(bgTile1, bgTile1Clean, 320, 0, x, y, width, height);
    grRestoreTileRect(bgTile3, bgTile3Clean, 0, 240, x, y, width, height);
    grRestoreTileRect(bgTile4, bgTile4Clean, 320, 240, x, y, width, height);
}

static void grCommitTileRectToClean(PS1Surface *srcTile,
                                    uint16 *dstClean,
                                    int tileScreenX,
                                    int tileScreenY,
                                    int rectX,
                                    int rectY,
                                    int rectW,
                                    int rectH)
{
    int copyStartX;
    int copyStartY;
    int copyEndX;
    int copyEndY;
    int row;

    if (srcTile == NULL || srcTile->pixels == NULL || dstClean == NULL)
        return;
    if (rectW <= 0 || rectH <= 0)
        return;

    copyStartX = (rectX > tileScreenX) ? rectX : tileScreenX;
    copyStartY = (rectY > tileScreenY) ? rectY : tileScreenY;
    copyEndX = rectX + rectW;
    copyEndY = rectY + rectH;

    if (copyEndX > tileScreenX + (int)srcTile->width)
        copyEndX = tileScreenX + (int)srcTile->width;
    if (copyEndY > tileScreenY + (int)srcTile->height)
        copyEndY = tileScreenY + (int)srcTile->height;

    if (copyStartX >= copyEndX || copyStartY >= copyEndY)
        return;

    for (row = copyStartY; row < copyEndY; row++) {
        int tileRow = row - tileScreenY;
        int tileCol = copyStartX - tileScreenX;
        int copyWidth = copyEndX - copyStartX;
        const uint16 *src = srcTile->pixels + (tileRow * srcTile->width) + tileCol;
        uint16 *dst = dstClean + (tileRow * srcTile->width) + tileCol;
        memcpy(dst, src, (size_t)copyWidth * sizeof(uint16));
    }
}

static void grCommitRectToCleanBg(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
        return;
    if (x + width > SCREEN_WIDTH)
        width = SCREEN_WIDTH - x;
    if (y + height > SCREEN_HEIGHT)
        height = SCREEN_HEIGHT - y;
    if (width <= 0 || height <= 0)
        return;

    grEnsureCleanBgTiles();
    grCommitTileRectToClean(bgTile0, bgTile0Clean, 0, 0, x, y, width, height);
    grCommitTileRectToClean(bgTile1, bgTile1Clean, 320, 0, x, y, width, height);
    grCommitTileRectToClean(bgTile3, bgTile3Clean, 0, 240, x, y, width, height);
    grCommitTileRectToClean(bgTile4, bgTile4Clean, 320, 240, x, y, width, height);
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
    /* Upload only dirty rows: union(prevDirty, currDirty) per tile.
     * prevDirty = rows restored at frame start (framebuffer still has old content).
     * currDirty = rows composited this frame (framebuffer has clean/old content). */
    PS1Surface *tiles[4] = { bgTile0, bgTile1, bgTile3, bgTile4 };
    int screenX[4] = { 0, 320, 0, 320 };
    int screenY[4] = { 0, 0, 240, 240 };
    RECT rects[4];  /* Separate RECTs — LoadImage may read asynchronously */

    for (int i = 0; i < 4; i++) {
        if (!tiles[i] || !tiles[i]->pixels) continue;

        /* Compute upload range = union(prevDirty, currDirty) */
        int minY = -1, maxY = -1;
        if (prevDirtyMinY[i] >= 0) {
            minY = prevDirtyMinY[i];
            maxY = prevDirtyMaxY[i];
        }
        if (currDirtyMinY[i] >= 0) {
            if (minY < 0) {
                minY = currDirtyMinY[i];
                maxY = currDirtyMaxY[i];
            } else {
                if (currDirtyMinY[i] < minY) minY = currDirtyMinY[i];
                if (currDirtyMaxY[i] > maxY) maxY = currDirtyMaxY[i];
            }
        }
        if (minY < 0) continue;  /* tile is fully clean — skip upload */

        int h = maxY - minY + 1;
        uint32 w = tiles[i]->width;
        setRECT(&rects[i], screenX[i], screenY[i] + minY, w, h);
        LoadImage(&rects[i], (uint32 *)(tiles[i]->pixels + minY * w));
    }

    /* Wait for DMA completion */
    DrawSync(0);

    /* Advance dirty state: this frame's compositing becomes next frame's restore set */
    for (int i = 0; i < 4; i++) {
        prevDirtyMinY[i] = currDirtyMinY[i];
        prevDirtyMaxY[i] = currDirtyMaxY[i];
    }
}

/*
 * Fade out effect
 */
void grFadeOut()
{
    /* Force full dirty for fade — modifies all pixels */
    grMarkAllTilesDirty();
    for (int i = 0; i < 4; i++) {
        prevDirtyMinY[i] = 0;
        prevDirtyMaxY[i] = 239;
    }

    /* 16 fade steps, ~2 frames each = ~0.5 sec at 60fps.
     * Uses (c >> 1) & 0x3DEF to halve all 3 color channels simultaneously:
     * the mask prevents bit leakage between R/G/B fields and clears STP. */
    for (int step = 0; step < 16; step++) {
        PS1Surface *tiles[] = { bgTile0, bgTile1, bgTile3, bgTile4 };
        for (int t = 0; t < 4; t++) {
            if (!tiles[t] || !tiles[t]->pixels) continue;
            uint32 count = tiles[t]->width * tiles[t]->height;
            uint32 *px32 = (uint32 *)tiles[t]->pixels;
            uint32 count32 = count >> 1;
            /* Process 2 pixels per uint32.
             * (c >> 1) & 0x3DEF works per-pixel; the mask at bit 15
             * also prevents leakage between the two packed pixels. */
            for (uint32 i = 0; i < count32; i++) {
                px32[i] = (px32[i] >> 1) & 0x3DEF3DEFu;
            }
        }

        /* Mark all dirty for each step's upload */
        grMarkAllTilesDirty();

        VSync(0);
        grDrawBackground();
    }
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
    tile->indexedPixels = NULL;
    tile->indexedOwned = 0;
    tile->psbNibbles = 0;
    tile->nextTile = NULL;

    /* Allocate pixel buffer for 15-bit direct color */
    uint32 pixelDataSize = tileWidth * BG_TILE_HEIGHT * 2;
    tile->pixels = (uint16*)safe_malloc(pixelDataSize);

    uint16 *dst = tile->pixels;

    /* Process 2 pixels per byte using palette LUT.
     * Row base increment avoids per-pixel multiply. */
    uint32 srcRowBase = (uint32)srcStartX;
    for (uint16 y = 0; y < BG_TILE_HEIGHT; y++) {
        uint32 srcOff = (uint32)y * (uint32)srcWidth + srcRowBase;
        uint16 *dstRow = dst + (uint32)y * tileWidth;
        uint16 x = 0;
        /* Handle odd start pixel */
        if (srcStartX & 1) {
            uint8 packed = src[srcOff >> 1];
            dstRow[0] = ttmPalette[packed & 0x0F];
            x = 1;
            srcOff++;
        }
        /* Process 2 pixels per byte */
        for (; x + 1 < tileWidth; x += 2, srcOff += 2) {
            uint32 pair = palLutSierra[src[srcOff >> 1]];
            dstRow[x]     = (uint16)pair;
            dstRow[x + 1] = (uint16)(pair >> 16);
        }
        /* Handle trailing pixel */
        if (x < tileWidth) {
            uint8 packed = src[srcOff >> 1];
            dstRow[x] = ttmPalette[(packed >> 4) & 0x0F];
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
 * srcHeight parameter allows partial source data (rest filled with black)
 */
static PS1Surface *createBgTileRAMPartial(uint8 *src, uint16 srcWidth, uint16 srcHeight,
                                           uint16 srcStartX, uint16 srcStartY,
                                           uint16 tileWidth)
{
    PS1Surface *tile = (PS1Surface*)safe_malloc(sizeof(PS1Surface));
    tile->width = tileWidth;
    tile->height = BG_TILE_HEIGHT;
    tile->x = 0;  /* Not in VRAM - just RAM */
    tile->y = 0;
    tile->indexedPixels = NULL;
    tile->indexedOwned = 0;
    tile->psbNibbles = 0;
    tile->nextTile = NULL;

    /* Allocate pixel buffer for 15-bit direct color */
    uint32 pixelDataSize = tileWidth * BG_TILE_HEIGHT * 2;
    tile->pixels = (uint16*)safe_malloc(pixelDataSize);

    uint16 *dst = tile->pixels;

    /* Process 2 pixels per byte using palette LUT, with bounds checking.
     * Row base increment avoids per-pixel multiply. */
    for (uint16 y = 0; y < BG_TILE_HEIGHT; y++) {
        uint32 srcY = srcStartY + y;
        uint16 *dstRow = dst + (uint32)y * tileWidth;
        if (srcY >= srcHeight) {
            /* Entire row is outside source bounds — fill with black */
            memset(dstRow, 0, tileWidth * sizeof(uint16));
            continue;
        }
        uint32 srcOff = srcY * (uint32)srcWidth + srcStartX;
        /* Calculate how many pixels are within source bounds */
        uint16 validW = (srcStartX + tileWidth <= srcWidth) ? tileWidth
                        : (srcStartX < srcWidth ? srcWidth - srcStartX : 0);
        uint16 x = 0;
        /* Handle odd start pixel */
        if ((srcStartX & 1) && x < validW) {
            uint8 packed = src[srcOff >> 1];
            dstRow[0] = ttmPalette[packed & 0x0F];
            x = 1;
            srcOff++;
        }
        /* Process 2 pixels per byte */
        for (; x + 1 < validW; x += 2, srcOff += 2) {
            uint32 pair = palLutSierra[src[srcOff >> 1]];
            dstRow[x]     = (uint16)pair;
            dstRow[x + 1] = (uint16)(pair >> 16);
        }
        /* Handle trailing valid pixel */
        if (x < validW) {
            uint8 packed = src[srcOff >> 1];
            dstRow[x] = ttmPalette[(packed >> 4) & 0x0F];
            x++;
        }
        /* Fill remaining with black */
        if (x < tileWidth) {
            memset(&dstRow[x], 0, (tileWidth - x) * sizeof(uint16));
        }
    }

    /* Don't upload to VRAM - keep in RAM for LoadImage to framebuffer */
    return tile;
}

/*
 * Helper: Create a background tile stored in RAM only (no VRAM upload)
 * For use with LoadImage directly to framebuffer
 * Legacy wrapper - assumes source has enough data
 */
static PS1Surface *createBgTileRAM(uint8 *src, uint16 srcWidth,
                                    uint16 srcStartX, uint16 srcStartY,
                                    uint16 tileWidth)
{
    /* Assume source has enough height for legacy callers */
    return createBgTileRAMPartial(src, srcWidth, srcStartY + BG_TILE_HEIGHT,
                                   srcStartX, srcStartY, tileWidth);
}

/*
 * Load background screen
 */
void grLoadScreen(char *strArg)
{
    struct TScrResource *scrResource = findScrResource(strArg);
    if (scrResource == NULL) return;

    /* Determine partial height from metadata (available without loading data) */
    uint16 srcHeight = scrResource->height;
    int isPartialHeight = (srcHeight < 480);

    /* Free tiles and clean copies BEFORE loading SCR data to ensure
     * enough RAM is available. On PS1 (2MB), tiles+clean copies use ~1.2MB,
     * and the SCR load needs ~300KB temporarily. */
    grFreeCleanBgTiles();

    /* Free top tiles always */
    freeBgTile(&bgTile0);
    freeBgTile(&bgTile1);
    freeBgTile(&bgTile2a);
    freeBgTile(&bgTile2b);

    /* Only free bottom tiles if new image is full height
     * This preserves ocean background for scenes like ISLETEMP (640x350) */
    if (!isPartialHeight) {
        freeBgTile(&bgTile3);
        freeBgTile(&bgTile4);
        freeBgTile(&bgTile5a);
        freeBgTile(&bgTile5b);
    }

    grBackgroundSfc = NULL;

    if (grSavedZonesLayer != NULL) {
        grFreeLayer(grSavedZonesLayer);
        grSavedZonesLayer = NULL;
    }

    /* Now load SCR data with freed memory available */
    if (scrResource->uncompressedData == NULL) {
        ps1_loadScrData(scrResource);
    }
    if (scrResource->uncompressedData == NULL) {
        /* Failed to load — recreate empty tiles so rendering doesn't crash */
        bgTile0 = createEmptyBgTileRAM(320, 240);
        bgTile1 = createEmptyBgTileRAM(320, 240);
        if (!isPartialHeight) {
            bgTile3 = createEmptyBgTileRAM(320, 240);
            bgTile4 = createEmptyBgTileRAM(320, 240);
        }
        grBackgroundSfc = bgTile0;
        return;
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

    /* LoadImage top row directly to framebuffer
     * Use separate RECTs - LoadImage is async and may read RECT after return */
    RECT topRect0, topRect1;
    if (bgTile0 && bgTile0->pixels) {
        setRECT(&topRect0, 0, 0, bgTile0->width, bgTile0->height);
        LoadImage(&topRect0, (uint32*)bgTile0->pixels);
    }
    if (bgTile1 && bgTile1->pixels) {
        setRECT(&topRect1, 320, 0, bgTile1->width, bgTile1->height);
        LoadImage(&topRect1, (uint32*)bgTile1->pixels);
    }
    DrawSync(0);

    /* Bottom row: Handle based on image height and existing tiles
     * For partial height images (like ISLETEMP 640x350), preserve existing ocean tiles */

    /* Calculate how many lines we can read for bottom half (y=240+) */
    uint16 bottomRowLines = (srcHeight > 240) ? (srcHeight - 240) : 0;

    if (bottomRowLines >= 240) {
        /* Full 640x480 image - create bottom row tiles (2x320 to match top row) */
        bgTile3  = createBgTileRAMPartial(src, srcWidth, srcHeight, 0,   240, 320);
        bgTile4  = createBgTileRAMPartial(src, srcWidth, srcHeight, 320, 240, 320);
        bgTile5a = NULL;
        bgTile5b = NULL;
    } else if (isPartialHeight && bgTile3 != NULL && bgTile4 != NULL && bottomRowLines > 0) {
        /* Partial height image with existing bottom tiles (ocean) - composite on top!
         * Copy the available rows (240 to srcHeight-1) onto existing tiles */
        uint16 *dst3 = bgTile3->pixels;
        uint16 *dst4 = bgTile4->pixels;

        for (uint16 y = 0; y < bottomRowLines && y < 240; y++) {
            uint16 srcY = 240 + y;
            for (uint16 x = 0; x < 320; x++) {
                /* Left tile (bgTile3) */
                uint32 srcOffset = (srcY * srcWidth + x) / 2;
                uint8 palIndex;
                if (x & 1) {
                    palIndex = src[srcOffset] & 0x0F;
                } else {
                    palIndex = (src[srcOffset] >> 4) & 0x0F;
                }
                dst3[y * 320 + x] = ttmPalette[palIndex & 0x0F];

                /* Right tile (bgTile4) */
                uint16 srcX = 320 + x;
                srcOffset = (srcY * srcWidth + srcX) / 2;
                if (srcX & 1) {
                    palIndex = src[srcOffset] & 0x0F;
                } else {
                    palIndex = (src[srcOffset] >> 4) & 0x0F;
                }
                dst4[y * 320 + x] = ttmPalette[palIndex & 0x0F];
            }
        }
    } else if (bottomRowLines > 0) {
        /* Partial bottom row with no existing tiles - create with partial data */
        bgTile3  = createBgTileRAMPartial(src, srcWidth, srcHeight, 0,   240, 320);
        bgTile4  = createBgTileRAMPartial(src, srcWidth, srcHeight, 320, 240, 320);
        bgTile5a = NULL;
        bgTile5b = NULL;
    } else {
        /* SCR is only 240 lines or less - create empty tiles for sprite compositing */
        if (bgTile3 == NULL) bgTile3 = createEmptyBgTileRAM(320, 240);
        if (bgTile4 == NULL) bgTile4 = createEmptyBgTileRAM(320, 240);
        bgTile5a = NULL;
        bgTile5b = NULL;
    }

    DrawSync(0);  /* Sync top row uploads */

    /* Disable display during bottom row LoadImage to avoid tearing/corruption */
    SetDispMask(0);

    /* LoadImage bottom row tiles directly to framebuffer (2x320 layout)
     * Use separate RECTs - LoadImage is async and may read RECT after return */
    RECT botRect3, botRect4;

    if (bgTile3 && bgTile3->pixels) {
        setRECT(&botRect3, 0, 240, bgTile3->width, bgTile3->height);
        LoadImage(&botRect3, (uint32*)bgTile3->pixels);
    }
    if (bgTile4 && bgTile4->pixels) {
        setRECT(&botRect4, 320, 240, bgTile4->width, bgTile4->height);
        LoadImage(&botRect4, (uint32*)bgTile4->pixels);
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
    }

    /* Save clean background tiles immediately after loading.
     * This ensures the clean copies don't have any sprites baked in.
     * IMPORTANT: For partial height images, we save top tiles immediately
     * but preserve any existing bottom tile clean copies (ocean base). */
    grSaveCleanBgTiles();
}

/*
 * Copy zone operations - minimal PS1 implementation.
 */
void grCopyZoneToBg(PS1Surface *sfc, uint16 x, uint16 y, uint16 width, uint16 height)
{
    int screenX;
    int screenY;

    (void)sfc;

    screenX = (int)x + grDx;
    screenY = (int)y + grDy;

    /* PS1 draws directly into the composited background tiles instead of
     * keeping a separate saved-zones overlay layer. Treat COPY_ZONE_TO_BG as
     * committing the current rectangle into the clean restore baseline. */
    grCommitRectToCleanBg(screenX, screenY, (int)width + 2, height);
}
void grSaveImage1(PS1Surface *sfc, uint16 x, uint16 y, uint16 width, uint16 height)
{
    /* Johnny's current use of SAVE_IMAGE1 is the same class of operation as
     * COPY_ZONE_TO_BG: define a bounded region that should survive subsequent
     * frame restores. Keep the PS1 behavior explicit and deterministic by
     * committing the current rectangle into the clean background baseline. */
    grCopyZoneToBg(sfc, x, y, width, height);
}
void grSaveZone(PS1Surface *sfc, uint16 x, uint16 y, uint16 width, uint16 height)
{
    (void)sfc;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    grPs1SavedZone.valid = 1;
}

void grRestoreZone(PS1Surface *sfc, uint16 x, uint16 y, uint16 width, uint16 height)
{
    int screenX;
    int screenY;

    (void)sfc;

    if (grPs1SavedZone.valid) {
        grRestoreRectFromCleanBg(grPs1SavedZone.x,
                                 grPs1SavedZone.y,
                                 grPs1SavedZone.width,
                                 grPs1SavedZone.height);
        grPs1SavedZone.valid = 0;
        return;
    }

    if (width == 0 || height == 0)
        return;

    screenX = (int)x + grDx;
    screenY = (int)y + grDy;
    grRestoreRectFromCleanBg(screenX, screenY, width, height);
}

/*
 * Frame capture (not implemented on PS1)
 */
int grCaptureFrame(const char *filename)
{
    /* Frame capture not supported on PS1 hardware */
    return -1;
}

int grCaptureSequenceComplete(void)
{
    return 0;
}

void grCaptureSetSceneLabel(const char *sceneLabel)
{
    grCaptureSceneLabel = sceneLabel ? sceneLabel : "";
}
