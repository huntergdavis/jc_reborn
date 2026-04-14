#include <string.h>
#include <stdio.h>

#include "foreground_pilot.h"

#ifdef PS1_BUILD
#include <stdlib.h>
#include <psxapi.h>

#include "mytypes.h"
#include "ads.h"
#include "events_ps1.h"
#include "graphics_ps1.h"
#include "cdrom_ps1.h"

extern uint16 ps1AdsDbgActiveThreads;
extern uint16 ps1AdsDbgReplayCount;
extern uint16 ps1AdsDbgRunningThreads;
extern uint16 ps1AdsDbgReplayDrawFrame;
extern uint16 ps1AdsDbgMergeCarryFrame;
extern uint16 ps1AdsDbgNoDrawThreadsFrame;

struct TFgPilotHeader {
    char magic[4];
    uint16 version;
    uint16 frameCount;
    uint16 displayVBlanks;
    uint16 reserved0;
    uint16 screenWidth;
    uint16 screenHeight;
    uint16 unionX;
    uint16 unionY;
    uint16 unionWidth;
    uint16 unionHeight;
    uint32 tableOffset;
    uint32 dataOffset;
};

struct TFgPilotEntry {
    uint16 sourceFrame;
    uint16 x;
    uint16 y;
    uint16 width;
    uint16 height;
    uint16 reserved0;
    uint32 dataOffset;
    uint32 dataSize;
};

struct TFgPilotEntryTable {
    struct TFgPilotEntry *entries;
    uint16 count;
};

struct TFgPilotRuntime {
    uint8 active;
    uint8 mode;
    uint16 frameIndex;
    uint16 frameVBlank;
    uint16 displayVBlanks;
    uint16 holdFrames;
    uint16 presentedVBlanks;
    uint32 sceneClockTick;
    struct TFgPilotHeader header;
    struct TFgPilotEntryTable entryTable;
    struct TFgPilotEntry currentEntry;
    uint8 *currentFrameData;
};

struct TFgPilotTiming {
    uint32 loadEntryTicks;
    uint32 loadDataTicks;
    uint32 beginFrameTicks;
    uint32 restoreTicks;
    uint32 blitTicks;
    uint32 presentTicks;
    uint32 totalTicks;
    uint16 framesPlayed;
    uint16 presentsRequested;
};

static char gForegroundPilotScene[16] = "";
static unsigned char gForegroundPilotRequestedMode = 0;
static const uint16 kFgPilotProbeHoldFrames = 1800;
static const uint16 kFgPilotHeaderFlagDeltaBlack = 0x0001;
static const uint16 kFgPilotHeaderFlagHostTicks = 0x0002;
static const uint16 kFgPilotHeaderFlagHostDeadlines = 0x0004;
static struct TFgPilotRuntime gFgRuntime = {0};
static uint8 gFgConfiguredEver = 0;
static uint8 gFgSetClearedEver = 0;
static uint8 gFgAdsMatchEver = 0;
static uint8 gFgStartAttemptEver = 0;
static uint8 gFgStartedEver = 0;
static uint8 gFgComposedEver = 0;

static void fgResetTelemetryFlags(void)
{
    gFgConfiguredEver = 0;
    gFgSetClearedEver = 0;
    gFgAdsMatchEver = 0;
    gFgStartAttemptEver = 0;
    gFgStartedEver = 0;
    gFgComposedEver = 0;
}

enum {
    FG_RUNTIME_NONE = 0,
    FG_RUNTIME_TESTCARD = 1,
    FG_RUNTIME_FISHING1 = 2
};

static int fgSceneEquals(const char *a, const char *b);

static uint16 fgConvertHostTicksToVBlanks(uint16 ticks)
{
    uint32 scaled = (uint32)ticks * 6u;
    uint16 hold = (uint16)((scaled + 4u) / 5u);
    return hold > 0 ? hold : 1;
}

static uint16 fgEntryHoldVBlanks(const struct TFgPilotHeader *header,
                                 const struct TFgPilotEntry *entry,
                                 uint16 presentedVBlanks)
{
    uint16 hold = 0;

    if (entry != NULL)
        hold = entry->reserved0;
    if (hold == 0 && header != NULL)
        hold = header->displayVBlanks;
    if (hold == 0)
        hold = 1;

    if (header != NULL && (header->reserved0 & kFgPilotHeaderFlagHostDeadlines) != 0) {
        uint16 targetVBlanks = fgConvertHostTicksToVBlanks(hold);
        hold = (targetVBlanks > presentedVBlanks)
            ? (uint16)(targetVBlanks - presentedVBlanks)
            : 1;
    } else if (header != NULL && (header->reserved0 & kFgPilotHeaderFlagHostTicks) != 0) {
        hold = fgConvertHostTicksToVBlanks(hold);
    }

    return hold;
}

static uint32 fgReadTickCounter(void)
{
    return (uint32)VSync(-1);
}

static uint32 fgElapsedTicks(uint32 startTick)
{
    uint32 endTick = fgReadTickCounter();
    return (endTick >= startTick) ? (endTick - startTick) : 0;
}

static uint16 fgElapsedVBlanksSince(uint32 *lastTick)
{
    uint32 nowTick;
    uint32 elapsed;

    if (lastTick == NULL)
        return 0;

    nowTick = fgReadTickCounter();
    elapsed = (nowTick >= *lastTick) ? (nowTick - *lastTick) : 0;
    *lastTick = nowTick;
    return (uint16)(elapsed > 0 ? elapsed : 0);
}

static void fgPrintTimingSummary(const struct TFgPilotTiming *timing)
{
    if (timing == NULL || timing->framesPlayed == 0)
        return;

    printf("FG timing: frames=%u presents=%u total=%u\n",
           (unsigned int)timing->framesPlayed,
           (unsigned int)timing->presentsRequested,
           (unsigned int)timing->totalTicks);
    printf("FG timing: entry=%u data=%u begin=%u restore=%u blit=%u present=%u\n",
           (unsigned int)timing->loadEntryTicks,
           (unsigned int)timing->loadDataTicks,
           (unsigned int)timing->beginFrameTicks,
           (unsigned int)timing->restoreTicks,
           (unsigned int)timing->blitTicks,
           (unsigned int)timing->presentTicks);
}

static const char *fgFishing1OverlayPackPath(void)
{
    return "FG\\FISHING1.FG1";
}

static const char *fgFishing1DirectPackPath(void)
{
    return "FG\\FISHING1D.FG1";
}

static int fgHeaderUsesDeltaBlack(const struct TFgPilotHeader *header)
{
    return (header != NULL && (header->reserved0 & kFgPilotHeaderFlagDeltaBlack) != 0) ? 1 : 0;
}

static uint8 fgSceneModeForName(const char *sceneName)
{
    if (fgSceneEquals(sceneName, "testcard"))
        return FG_RUNTIME_TESTCARD;
    if (fgSceneEquals(sceneName, "fishing1"))
        return FG_RUNTIME_FISHING1;
    return FG_RUNTIME_NONE;
}

static uint16 fgReadU16(const uint8 *p)
{
    return (uint16)((uint16)p[0] | ((uint16)p[1] << 8));
}

static uint32 fgReadU32(const uint8 *p)
{
    return (uint32)p[0] |
           ((uint32)p[1] << 8) |
           ((uint32)p[2] << 16) |
           ((uint32)p[3] << 24);
}

static int fgLoadHeader(const char *path, struct TFgPilotHeader *out)
{
    uint8 *data;

    if (!path || !out)
        return 0;

    data = ps1_streamRead(path, 0, 32);
    if (!data)
        return 0;

    memcpy(out->magic, data, 4);
    out->version = fgReadU16(data + 4);
    out->frameCount = fgReadU16(data + 6);
    out->displayVBlanks = fgReadU16(data + 8);
    out->reserved0 = fgReadU16(data + 10);
    out->screenWidth = fgReadU16(data + 12);
    out->screenHeight = fgReadU16(data + 14);
    out->unionX = fgReadU16(data + 16);
    out->unionY = fgReadU16(data + 18);
    out->unionWidth = fgReadU16(data + 20);
    out->unionHeight = fgReadU16(data + 22);
    out->tableOffset = fgReadU32(data + 24);
    out->dataOffset = fgReadU32(data + 28);
    free(data);

    if (memcmp(out->magic, "FGP1", 4) != 0)
        return 0;
    if (out->version != 1)
        return 0;
    if (out->frameCount == 0)
        return 0;

    return 1;
}

static int fgLoadEntry(const char *path, const struct TFgPilotHeader *header,
                       uint16 frameIndex, struct TFgPilotEntry *out)
{
    uint8 *data;
    uint32 offset;

    if (!path || !header || !out || frameIndex >= header->frameCount)
        return 0;

    offset = header->tableOffset + ((uint32)frameIndex * 20u);
    data = ps1_streamRead(path, offset, 20);
    if (!data)
        return 0;

    out->sourceFrame = fgReadU16(data + 0);
    out->x = fgReadU16(data + 2);
    out->y = fgReadU16(data + 4);
    out->width = fgReadU16(data + 6);
    out->height = fgReadU16(data + 8);
    out->reserved0 = fgReadU16(data + 10);
    out->dataOffset = fgReadU32(data + 12);
    out->dataSize = fgReadU32(data + 16);
    free(data);

    return 1;
}

static void fgFreeEntryTable(struct TFgPilotEntryTable *table)
{
    if (table == NULL)
        return;
    if (table->entries != NULL) {
        free(table->entries);
        table->entries = NULL;
    }
    table->count = 0;
}

static int fgLoadEntryTable(const char *path, const struct TFgPilotHeader *header,
                            struct TFgPilotEntryTable *out)
{
    uint8 *data;
    uint32 tableSize;

    if (!path || !header || !out || header->frameCount == 0)
        return 0;

    memset(out, 0, sizeof(*out));
    tableSize = (uint32)header->frameCount * 20u;
    data = ps1_streamRead(path, header->tableOffset, tableSize);
    if (!data)
        return 0;

    out->entries = (struct TFgPilotEntry *)malloc((size_t)header->frameCount * sizeof(struct TFgPilotEntry));
    if (out->entries == NULL) {
        free(data);
        return 0;
    }

    out->count = header->frameCount;
    for (uint16 i = 0; i < header->frameCount; i++) {
        const uint8 *src = data + ((uint32)i * 20u);
        struct TFgPilotEntry *dst = &out->entries[i];
        dst->sourceFrame = fgReadU16(src + 0);
        dst->x = fgReadU16(src + 2);
        dst->y = fgReadU16(src + 4);
        dst->width = fgReadU16(src + 6);
        dst->height = fgReadU16(src + 8);
        dst->reserved0 = fgReadU16(src + 10);
        dst->dataOffset = fgReadU32(src + 12);
        dst->dataSize = fgReadU32(src + 16);
    }

    free(data);
    return 1;
}

static const struct TFgPilotEntry *fgGetEntryFromTable(const struct TFgPilotEntryTable *table,
                                                       uint16 frameIndex)
{
    if (table == NULL || table->entries == NULL || frameIndex >= table->count)
        return NULL;

    return &table->entries[frameIndex];
}

static int fgSceneEquals(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static int fgAdsNameEquals(const char *adsName, const char *baseName)
{
    size_t adsLen;
    size_t baseLen;

    if (!adsName || !baseName)
        return 0;
    if (strcmp(adsName, baseName) == 0)
        return 1;

    adsLen = strlen(adsName);
    baseLen = strlen(baseName);
    if (adsLen == baseLen + 4 &&
        memcmp(adsName, baseName, baseLen) == 0 &&
        strcmp(adsName + baseLen, ".ADS") == 0) {
        return 1;
    }

    return 0;
}

static void fgTelemetryUpdate(void)
{
    if (!gFgRuntime.active) {
        ps1AdsDbgActiveThreads = 0;
        ps1AdsDbgReplayCount = 0;
        ps1AdsDbgRunningThreads = 0;
        ps1AdsDbgReplayDrawFrame = 0;
        ps1AdsDbgMergeCarryFrame = 0;
        ps1AdsDbgNoDrawThreadsFrame = 0;
        return;
    }

    ps1AdsDbgActiveThreads = 55;
    ps1AdsDbgReplayCount = (uint16)(gFgRuntime.header.frameCount & 0x3F);
    ps1AdsDbgRunningThreads = (uint16)(gFgRuntime.frameIndex & 0x3F);
    ps1AdsDbgReplayDrawFrame = (uint16)(gFgRuntime.currentEntry.sourceFrame & 0x3F);
    ps1AdsDbgMergeCarryFrame = (uint16)(gFgRuntime.displayVBlanks & 0x3F);
    ps1AdsDbgNoDrawThreadsFrame = (uint16)((gFgRuntime.currentFrameData != NULL) ? 1 : 0);
}

static void fgInitVisiblePipeline(void)
{
    adsInit();
    adsNoIsland();
    grUpdateDelay = 0;
}

static void fgInitBlackBackground(void)
{
    grInitEmptyBackground();
    grSaveCleanBgTiles();
}

static void fgBlit16ToBackgroundRect(uint16 dstX, uint16 dstY,
                                     uint16 width, uint16 height,
                                     const uint16 *srcPixels)
{
    if (srcPixels == NULL || width == 0 || height == 0)
        return;
    grCompositeDirect16ToBackground(srcPixels, width, height, (sint16)dstX, (sint16)dstY);
}

static int fgEntriesShareBounds(const struct TFgPilotEntry *a,
                                const struct TFgPilotEntry *b)
{
    if (a == NULL || b == NULL)
        return 0;

    return a->x == b->x &&
           a->y == b->y &&
           a->width == b->width &&
           a->height == b->height;
}

static void fgPresentCurrentBackground(uint16 holdFrames)
{
    uint16 i;

    for (i = 0; i < holdFrames; i++) {
        grUpdateDisplay(NULL, NULL, NULL);
    }
}

static uint8 *fgLoadRawFileDirect(const char *cdPath, uint32 *outSize)
{
    CdlFILE fileInfo;
    uint32 totalBytes;
    uint8 *buffer;
    int totalSectors;

    if (cdPath == NULL || outSize == NULL)
        return NULL;

    if (CdSearchFile(&fileInfo, (char *)cdPath) == NULL) {
        printf("FG pilot: CdSearchFile failed for %s\n", cdPath);
        return NULL;
    }

    totalBytes = (uint32)fileInfo.size;
    totalSectors = (int)((totalBytes + 2047u) / 2048u);
    buffer = (uint8 *)malloc((size_t)totalSectors * 2048u);
    if (buffer == NULL)
        return NULL;

    CdControl(CdlSetloc, (uint8 *)&fileInfo.pos, 0);
    CdRead(totalSectors, (uint32 *)buffer, CdlModeSpeed);
    if (CdReadSync(0, 0) < 0) {
        printf("FG pilot: CdReadSync failed for %s\n", cdPath);
        free(buffer);
        return NULL;
    }

    *outSize = totalBytes;
    cdromResetState();
    return buffer;
}

static void fgInitDisplayDirect(void)
{
    DISPENV disp;
    DRAWENV draw;

    ResetGraph(0);
    SetVideoMode(MODE_NTSC);

    SetDefDispEnv(&disp, 0, 0, 640, 480);
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    disp.isinter = 1;
    draw.isbg = 0;

    PutDispEnv(&disp);
    PutDrawEnv(&draw);
    SetDispMask(1);
}

static void fgInitDisplayDirect240p(void)
{
    DISPENV disp;
    DRAWENV draw;

    ResetGraph(0);
    SetVideoMode(MODE_NTSC);

    SetDefDispEnv(&disp, 0, 0, 640, 240);
    SetDefDrawEnv(&draw, 0, 0, 640, 240);
    disp.isinter = 0;
    draw.isbg = 0;

    PutDispEnv(&disp);
    PutDrawEnv(&draw);
    SetDispMask(1);
}

static void fgShowRawFrame(const char *cdPath, uint16 holdFrames)
{
    uint32 rawSize = 0;
    uint8 *screenBuffer;

    if (cdPath == NULL)
        return;

    screenBuffer = fgLoadRawFileDirect(cdPath, &rawSize);
    if (screenBuffer == NULL)
        return;
    if (rawSize < (uint32)(640 * 480 * 2)) {
        printf("FG pilot: short raw frame %s (%u bytes)\n", cdPath, (unsigned int)rawSize);
        free(screenBuffer);
        return;
    }

    fgInitVisiblePipeline();
    fgInitBlackBackground();
    grBeginFrame();
    grRestoreBgTiles();
    fgBlit16ToBackgroundRect(0, 0, 640, 480, (const uint16 *)screenBuffer);
    free(screenBuffer);
    fgPresentCurrentBackground(holdFrames);
}

static void fgClearRectDirect(uint16 x, uint16 y, uint16 width, uint16 height)
{
    static uint16 *blackStrip = NULL;
    const int stripHeight = 60;
    RECT rect;
    uint16 remainingHeight;
    uint16 clearWidth;
    uint16 clearHeight;
    uint16 clearY;

    if (blackStrip == NULL) {
        blackStrip = (uint16 *)calloc((size_t)640 * (size_t)stripHeight, sizeof(uint16));
        if (blackStrip == NULL)
            return;
    }

    if (x >= 640 || y >= 480 || width == 0 || height == 0)
        return;

    clearWidth = width;
    clearHeight = height;
    clearY = y;

    if (x + clearWidth > 640)
        clearWidth = (uint16)(640 - x);
    if (clearY + clearHeight > 480)
        clearHeight = (uint16)(480 - clearY);
    if (clearWidth == 0 || clearHeight == 0)
        return;

    remainingHeight = clearHeight;
    while (remainingHeight > 0) {
        uint16 chunkHeight = remainingHeight;
        if (chunkHeight > (uint16)stripHeight)
            chunkHeight = (uint16)stripHeight;

        setRECT(&rect, x, clearY, clearWidth, chunkHeight);
        LoadImage(&rect, (uint32 *)blackStrip);
        clearY = (uint16)(clearY + chunkHeight);
        remainingHeight = (uint16)(remainingHeight - chunkHeight);
    }
}

static void fgClearScreenDirect(void)
{
    fgClearRectDirect(0, 0, 640, 480);
}

static void fgUploadDirect(uint16 x, uint16 y, uint16 width, uint16 height, const uint8 *frameData)
{
    RECT rect;

    if (frameData == NULL || width == 0 || height == 0)
        return;

    if (x >= 640 || y >= 480)
        return;
    if (x + width > 640)
        width = (uint16)(640 - x);
    if (y + height > 480)
        height = (uint16)(480 - y);
    if (width == 0 || height == 0)
        return;

    setRECT(&rect, x, y, width, height);
    LoadImage(&rect, (uint32 *)frameData);
}

static void fgUploadDirectHalfY(uint16 x, uint16 y, uint16 width, uint16 height, const uint8 *frameData)
{
    static uint16 *scaledBuffer = NULL;
    static uint32 scaledCapacityPixels = 0;
    const uint16 *srcPixels = (const uint16 *)frameData;
    uint16 scaledHeight;
    uint32 requiredPixels;
    uint32 dstIndex = 0;
    RECT rect;
    uint16 srcY;

    if (frameData == NULL || width == 0 || height == 0)
        return;

    if (x >= 640 || y >= 480)
        return;
    if (x + width > 640)
        width = (uint16)(640 - x);
    if (y + height > 480)
        height = (uint16)(480 - y);
    if (width == 0 || height == 0)
        return;

    scaledHeight = (uint16)((height + 1u) / 2u);
    if (((uint16)(y / 2u)) + scaledHeight > 240)
        scaledHeight = (uint16)(240 - (y / 2u));
    if (scaledHeight == 0)
        return;

    requiredPixels = (uint32)width * (uint32)scaledHeight;
    if (requiredPixels > scaledCapacityPixels) {
        uint16 *newBuffer = (uint16 *)realloc(scaledBuffer, requiredPixels * sizeof(uint16));
        if (newBuffer == NULL)
            return;
        scaledBuffer = newBuffer;
        scaledCapacityPixels = requiredPixels;
    }

    for (srcY = 0; srcY < height && dstIndex < requiredPixels; srcY = (uint16)(srcY + 2u)) {
        memcpy(&scaledBuffer[dstIndex],
               &srcPixels[(uint32)srcY * (uint32)width],
               (size_t)width * sizeof(uint16));
        dstIndex += width;
    }

    setRECT(&rect, x, (uint16)(y / 2u), width, scaledHeight);
    LoadImage(&rect, (uint32 *)scaledBuffer);
}

static void fgWaitPresentedFrame(void)
{
    VSync(0);
    eventsWaitTick(grUpdateDelay);
}

static void fgDrawEntry(const struct TFgPilotEntry *entry, uint8 *frameData,
                        const struct TFgPilotEntry *prevEntry, int clearPrev)
{
    if (clearPrev && prevEntry != NULL)
        fgClearRectDirect(prevEntry->x, prevEntry->y, prevEntry->width, prevEntry->height);

    if (entry != NULL)
        fgUploadDirect(entry->x, entry->y, entry->width, entry->height, frameData);

    DrawSync(0);
    fgWaitPresentedFrame();
}

static void fgHoldEntry(const struct TFgPilotEntry *entry, uint8 *frameData, uint16 frames,
                        const struct TFgPilotEntry *prevEntry, int clearPrev)
{
    uint16 i;

    if (frames == 0)
        return;

    fgDrawEntry(entry, frameData, prevEntry, clearPrev);
    for (i = 1; i < frames; i++)
        fgWaitPresentedFrame();
}

static void fgDrawEntryHalfY(const struct TFgPilotEntry *entry, uint8 *frameData,
                             const struct TFgPilotEntry *prevEntry, int clearPrev)
{
    if (clearPrev && prevEntry != NULL) {
        fgClearRectDirect(prevEntry->x,
                          (uint16)(prevEntry->y / 2u),
                          prevEntry->width,
                          (uint16)((prevEntry->height + 1u) / 2u));
    }

    if (entry != NULL)
        fgUploadDirectHalfY(entry->x, entry->y, entry->width, entry->height, frameData);

    DrawSync(0);
    fgWaitPresentedFrame();
}

static void fgHoldEntryHalfY(const struct TFgPilotEntry *entry, uint8 *frameData, uint16 frames,
                             const struct TFgPilotEntry *prevEntry, int clearPrev)
{
    uint16 i;

    if (frames == 0)
        return;

    fgDrawEntryHalfY(entry, frameData, prevEntry, clearPrev);
    for (i = 1; i < frames; i++)
        fgWaitPresentedFrame();
}

static void fgPlayTestCard(void)
{
    static uint16 *colors[4] = { NULL, NULL, NULL, NULL };
    static const uint16 colorValues[4] = { 0x001f, 0x03e0, 0x03ff, 0x7c1f };
    const uint16 rectW = 120;
    const uint16 rectH = 80;
    uint16 i;

    fgInitVisiblePipeline();
    fgInitBlackBackground();

    for (int c = 0; c < 4; c++) {
        if (colors[c] == NULL) {
            colors[c] = (uint16 *)malloc((size_t)rectW * (size_t)rectH * sizeof(uint16));
            if (colors[c] == NULL)
                return;
            for (uint32 j = 0; j < (uint32)rectW * (uint32)rectH; j++)
                colors[c][j] = colorValues[c];
        }
    }

    for (i = 0; i < 120; i++) {
        grBeginFrame();
        grRestoreBgTiles();
        fgBlit16ToBackgroundRect(24, 24, rectW, rectH, colors[0]);
        fgBlit16ToBackgroundRect(176, 24, rectW, rectH, colors[1]);
        fgBlit16ToBackgroundRect(24, 136, rectW, rectH, colors[2]);
        fgBlit16ToBackgroundRect(176, 136, rectW, rectH, colors[3]);
        fgPresentCurrentBackground(1);
    }
}

static void fgRuntimeReset(void)
{
    if (gFgRuntime.currentFrameData != NULL) {
        free(gFgRuntime.currentFrameData);
        gFgRuntime.currentFrameData = NULL;
    }
    fgFreeEntryTable(&gFgRuntime.entryTable);
    memset(&gFgRuntime, 0, sizeof(gFgRuntime));
    fgTelemetryUpdate();
}

static int fgRuntimeLoadFishingFrame(uint16 frameIndex)
{
    const char *path = fgFishing1OverlayPackPath();
    const struct TFgPilotEntry *entry = fgGetEntryFromTable(&gFgRuntime.entryTable, frameIndex);

    if (entry == NULL)
        return 0;
    gFgRuntime.currentEntry = *entry;

    if (gFgRuntime.currentFrameData != NULL) {
        free(gFgRuntime.currentFrameData);
        gFgRuntime.currentFrameData = NULL;
    }

    if (gFgRuntime.currentEntry.dataSize > 0 &&
        gFgRuntime.currentEntry.width > 0 &&
        gFgRuntime.currentEntry.height > 0) {
        gFgRuntime.currentFrameData = ps1_streamRead(path,
                                                     gFgRuntime.currentEntry.dataOffset,
                                                     gFgRuntime.currentEntry.dataSize);
        if (gFgRuntime.currentFrameData == NULL)
            return 0;
    }

    gFgRuntime.displayVBlanks = fgEntryHoldVBlanks(&gFgRuntime.header,
                                                   &gFgRuntime.currentEntry,
                                                   gFgRuntime.presentedVBlanks);
    fgTelemetryUpdate();
    return 1;
}

int foregroundPilotRuntimeStart(const char *sceneName)
{
    fgRuntimeReset();

    if (sceneName == NULL)
        return 0;

    if (fgSceneEquals(sceneName, "testcard")) {
        gFgRuntime.active = 1;
        gFgRuntime.mode = FG_RUNTIME_TESTCARD;
        gFgRuntime.holdFrames = kFgPilotProbeHoldFrames;
        gFgRuntime.sceneClockTick = fgReadTickCounter();
        gFgStartedEver = 1;
        fgTelemetryUpdate();
        return 1;
    }

    if (fgSceneEquals(sceneName, "fishing1")) {
        const char *path = fgFishing1OverlayPackPath();
        if (!fgLoadHeader(path, &gFgRuntime.header))
            return 0;
        if (!fgLoadEntryTable(path, &gFgRuntime.header, &gFgRuntime.entryTable)) {
            fgRuntimeReset();
            return 0;
        }
        gFgRuntime.active = 1;
        gFgRuntime.mode = FG_RUNTIME_FISHING1;
        gFgRuntime.displayVBlanks = 1;
        gFgRuntime.holdFrames = 150;
        gFgRuntime.sceneClockTick = fgReadTickCounter();
        if (!fgRuntimeLoadFishingFrame(0)) {
            fgRuntimeReset();
            return 0;
        }
        gFgStartedEver = 1;
        fgTelemetryUpdate();
        return 1;
    }

    return 0;
}

void foregroundPilotRuntimeCompose(void)
{
    const uint16 rectW = 120;
    const uint16 rectH = 80;

    if (!gFgRuntime.active)
        return;

    gFgComposedEver = 1;

    if (gFgRuntime.mode == FG_RUNTIME_TESTCARD) {
        static uint16 *colors[4] = { NULL, NULL, NULL, NULL };
        static const uint16 colorValues[4] = { 0x001f, 0x03e0, 0x03ff, 0x7c1f };

        for (int c = 0; c < 4; c++) {
            if (colors[c] == NULL) {
                colors[c] = (uint16 *)malloc((size_t)rectW * (size_t)rectH * sizeof(uint16));
                if (colors[c] == NULL)
                    return;
                for (uint32 j = 0; j < (uint32)rectW * (uint32)rectH; j++)
                    colors[c][j] = colorValues[c];
            }
        }

        fgBlit16ToBackgroundRect(24, 24, rectW, rectH, colors[0]);
        fgBlit16ToBackgroundRect(176, 24, rectW, rectH, colors[1]);
        fgBlit16ToBackgroundRect(24, 136, rectW, rectH, colors[2]);
        fgBlit16ToBackgroundRect(176, 136, rectW, rectH, colors[3]);
        return;
    }

    if (gFgRuntime.mode == FG_RUNTIME_FISHING1 && gFgRuntime.currentFrameData != NULL) {
        fgBlit16ToBackgroundRect(gFgRuntime.currentEntry.x,
                                 gFgRuntime.currentEntry.y,
                                 gFgRuntime.currentEntry.width,
                                 gFgRuntime.currentEntry.height,
                                 (const uint16 *)gFgRuntime.currentFrameData);
    }
}

void foregroundPilotRuntimeAdvance(void)
{
    uint16 elapsedVBlanks;

    if (!gFgRuntime.active)
        return;

    elapsedVBlanks = fgElapsedVBlanksSince(&gFgRuntime.sceneClockTick);
    if (elapsedVBlanks == 0)
        elapsedVBlanks = 1;

    if (gFgRuntime.mode == FG_RUNTIME_TESTCARD) {
        if (gFgRuntime.holdFrames > elapsedVBlanks)
            gFgRuntime.holdFrames = (uint16)(gFgRuntime.holdFrames - elapsedVBlanks);
        else
            gFgRuntime.holdFrames = 0;
        if (gFgRuntime.holdFrames == 0)
            gFgRuntime.active = 0;
        fgTelemetryUpdate();
        return;
    }

    if (gFgRuntime.mode == FG_RUNTIME_FISHING1) {
        uint16 frameHoldVBlanks = gFgRuntime.displayVBlanks;

        if (gFgRuntime.frameIndex + 1 >= gFgRuntime.header.frameCount) {
            if (gFgRuntime.holdFrames > elapsedVBlanks)
                gFgRuntime.holdFrames = (uint16)(gFgRuntime.holdFrames - elapsedVBlanks);
            else
                gFgRuntime.holdFrames = 0;
            if (gFgRuntime.holdFrames == 0)
                gFgRuntime.active = 0;
            fgTelemetryUpdate();
            return;
        }

        gFgRuntime.frameVBlank = (uint16)(gFgRuntime.frameVBlank + elapsedVBlanks);
        if (gFgRuntime.frameVBlank < frameHoldVBlanks) {
            gFgRuntime.displayVBlanks = frameHoldVBlanks;
            fgTelemetryUpdate();
            return;
        }

        gFgRuntime.frameVBlank = 0;
        gFgRuntime.presentedVBlanks = (uint16)(gFgRuntime.presentedVBlanks + frameHoldVBlanks);
        gFgRuntime.frameIndex++;
        if (!fgRuntimeLoadFishingFrame(gFgRuntime.frameIndex))
            gFgRuntime.active = 0;
        fgTelemetryUpdate();
    }
}

int foregroundPilotRuntimeActive(void)
{
    return gFgRuntime.active ? 1 : 0;
}

int foregroundPilotRuntimeMode(void)
{
    return gFgRuntime.active ? (int)gFgRuntime.mode : 0;
}

unsigned short foregroundPilotRuntimeFrameIndex(void)
{
    return gFgRuntime.active ? gFgRuntime.frameIndex : 0;
}

unsigned short foregroundPilotRuntimeSourceFrame(void)
{
    return (gFgRuntime.active && gFgRuntime.currentFrameData != NULL)
        ? gFgRuntime.currentEntry.sourceFrame
        : 0;
}

unsigned short foregroundPilotRuntimeDisplayVBlanks(void)
{
    return gFgRuntime.active ? gFgRuntime.displayVBlanks : 0;
}

int foregroundPilotRuntimeHasFrameData(void)
{
    return (gFgRuntime.active && gFgRuntime.currentFrameData != NULL) ? 1 : 0;
}

int foregroundPilotConfiguredEver(void)
{
    return gFgConfiguredEver ? 1 : 0;
}

int foregroundPilotSetClearedEver(void)
{
    return gFgSetClearedEver ? 1 : 0;
}

int foregroundPilotRequestedNow(void)
{
    return foregroundPilotRequested();
}

int foregroundPilotRuntimeAdsMatchEver(void)
{
    return gFgAdsMatchEver ? 1 : 0;
}

int foregroundPilotRuntimeStartAttemptedEver(void)
{
    return gFgStartAttemptEver ? 1 : 0;
}

int foregroundPilotRuntimeStartedEver(void)
{
    return gFgStartedEver ? 1 : 0;
}

int foregroundPilotRuntimeComposedEver(void)
{
    return gFgComposedEver ? 1 : 0;
}

void foregroundPilotRuntimeEnd(void)
{
    fgRuntimeReset();
}

static void fgPlayFishing1(void)
{
    const char *path = fgFishing1OverlayPackPath();
    CdlFILE cdfile;
    struct TFgPilotHeader header;
    struct TFgPilotEntryTable entryTable;
    struct TFgPilotTiming timing;
    uint32 playStartTick;
    uint32 sceneClockTick;
    uint8 *frameBuffer = NULL;
    uint8 *streamScratch = NULL;
    uint32 maxFrameDataSize = 0;
    uint32 maxStreamScratchSize = 0;
    uint16 presentedVBlanks = 0;
    const struct TFgPilotEntry *prevEntry = NULL;
    int haveLastEntry = 0;

    memset(&timing, 0, sizeof(timing));

    playStartTick = fgReadTickCounter();
    sceneClockTick = playStartTick;
    if (!fgLoadHeader(path, &header)) {
        printf("FG pilot: failed to load header %s\n", path);
        return;
    }
    if (!fgLoadEntryTable(path, &header, &entryTable)) {
        printf("FG pilot: failed to load entry table %s\n", path);
        return;
    }
    if (!ps1_streamResolveFile(path, &cdfile)) {
        printf("FG pilot: failed to resolve file %s\n", path);
        fgFreeEntryTable(&entryTable);
        return;
    }
    for (uint16 i = 0; i < entryTable.count; i++) {
        if (entryTable.entries[i].dataSize > maxFrameDataSize)
            maxFrameDataSize = entryTable.entries[i].dataSize;
    }
    maxStreamScratchSize = ((maxFrameDataSize + 2047u) / 2048u) * 2048u + 2048u;
    if (maxFrameDataSize > 0) {
        frameBuffer = (uint8 *)malloc(maxFrameDataSize);
        if (frameBuffer == NULL) {
            printf("FG pilot: failed to alloc frame buffer %u\n", (unsigned int)maxFrameDataSize);
            fgFreeEntryTable(&entryTable);
            return;
        }
        streamScratch = (uint8 *)malloc(maxStreamScratchSize);
        if (streamScratch == NULL) {
            printf("FG pilot: failed to alloc stream scratch %u\n", (unsigned int)maxStreamScratchSize);
            fgFreeEntryTable(&entryTable);
            free(frameBuffer);
            return;
        }
    }

    fgInitVisiblePipeline();
    fgInitBlackBackground();
    fgPresentCurrentBackground(15);

    if (entryTable.entries == NULL || entryTable.count == 0) {
        fgFreeEntryTable(&entryTable);
        if (streamScratch != NULL)
            free(streamScratch);
        if (frameBuffer != NULL)
            free(frameBuffer);
        return;
    }

    for (uint16 frameIndex = 0; frameIndex < entryTable.count; frameIndex++) {
        const struct TFgPilotEntry *entry = &entryTable.entries[frameIndex];
        const uint8 *frameData;
        uint16 holdVBlanks;

        frameData = NULL;
        if (entry->dataSize > 0 && entry->width > 0 && entry->height > 0) {
            uint32 tickStart = fgReadTickCounter();
            if (frameBuffer == NULL ||
                !ps1_streamReadIntoFileBuffered(&cdfile, entry->dataOffset, entry->dataSize,
                                               frameBuffer, streamScratch, maxStreamScratchSize)) {
                printf("FG pilot: failed to stream frame %u\n", (unsigned int)frameIndex);
                break;
            }
            frameData = frameBuffer;
            timing.loadDataTicks += fgElapsedTicks(tickStart);
        }

        holdVBlanks = fgEntryHoldVBlanks(&header, entry, presentedVBlanks);

        {
            uint32 tickStart = fgReadTickCounter();
            grBeginFrame();
            timing.beginFrameTicks += fgElapsedTicks(tickStart);
        }

        {
            uint32 tickStart = fgReadTickCounter();
            if (prevEntry != NULL && frameData != NULL &&
                fgEntriesShareBounds(prevEntry, entry)) {
                grRestoreAndCompositeDirect16BackgroundRectForFrame(entry->x, entry->y,
                                                                    entry->width, entry->height,
                                                                    (const uint16 *)frameData);
            } else if (prevEntry != NULL) {
                grRestoreBackgroundRectForFrame(prevEntry->x, prevEntry->y,
                                                prevEntry->width, prevEntry->height);
            } else {
                grRestoreBgTiles();
            }
            timing.restoreTicks += fgElapsedTicks(tickStart);
        }

        if (frameData != NULL &&
            !(prevEntry != NULL && fgEntriesShareBounds(prevEntry, entry))) {
            uint32 tickStart = fgReadTickCounter();
            fgBlit16ToBackgroundRect(entry->x, entry->y, entry->width, entry->height,
                                     (const uint16 *)frameData);
            timing.blitTicks += fgElapsedTicks(tickStart);
        }

        {
            uint32 tickStart = fgReadTickCounter();
            fgPresentCurrentBackground(holdVBlanks);
            timing.presentTicks += fgElapsedTicks(tickStart);
        }
        timing.framesPlayed++;
        timing.presentsRequested = (uint16)(timing.presentsRequested + holdVBlanks);
        presentedVBlanks = (uint16)(presentedVBlanks + fgElapsedVBlanksSince(&sceneClockTick));

        if (frameData != NULL)
            haveLastEntry = 1;
        prevEntry = entry;
    }

    if (haveLastEntry) {
        uint32 tickStart = fgReadTickCounter();
        fgPresentCurrentBackground(150);
        timing.presentTicks += fgElapsedTicks(tickStart);
    } else {
        uint32 tickStart = fgReadTickCounter();
        fgPresentCurrentBackground(150);
        timing.presentTicks += fgElapsedTicks(tickStart);
    }
    timing.totalTicks = fgElapsedTicks(playStartTick);
    fgPrintTimingSummary(&timing);

    fgFreeEntryTable(&entryTable);
    if (streamScratch != NULL)
        free(streamScratch);
    if (frameBuffer != NULL)
        free(frameBuffer);
}

static void fgPlayFishing1Progressive240(void)
{
    const char *path = fgFishing1OverlayPackPath();
    struct TFgPilotHeader header;
    struct TFgPilotEntry lastEntry;
    struct TFgPilotEntry prevEntry;
    uint8 *lastFrameData = NULL;
    uint16 presentedVBlanks = 0;
    int haveLastEntry = 0;
    int havePrevEntry = 0;
    if (!fgLoadHeader(path, &header)) {
        printf("FG pilot: failed to load header %s\n", path);
        return;
    }

    fgInitDisplayDirect240p();
    fgClearRectDirect(0, 0, 640, 240);
    fgHoldEntryHalfY(NULL, NULL, 15, NULL, 0);

    for (uint16 frameIndex = 0; frameIndex < header.frameCount; frameIndex++) {
        struct TFgPilotEntry entry;
        uint8 *frameData;
        uint16 holdVBlanks;

        if (!fgLoadEntry(path, &header, frameIndex, &entry)) {
            printf("FG pilot: failed to load entry %u\n", (unsigned int)frameIndex);
            break;
        }

        frameData = NULL;
        if (entry.dataSize > 0 && entry.width > 0 && entry.height > 0) {
            frameData = ps1_streamRead(path, entry.dataOffset, entry.dataSize);
            if (!frameData) {
                printf("FG pilot: failed to stream frame %u\n", (unsigned int)frameIndex);
                break;
            }
        }

        holdVBlanks = fgEntryHoldVBlanks(&header, &entry, presentedVBlanks);

        fgHoldEntryHalfY(&entry, frameData, holdVBlanks,
                         havePrevEntry ? &prevEntry : NULL, 1);
        presentedVBlanks = (uint16)(presentedVBlanks + holdVBlanks);

        if (lastFrameData != NULL) {
            free(lastFrameData);
            lastFrameData = NULL;
        }
        if (frameData != NULL) {
            lastFrameData = frameData;
            lastEntry = entry;
            haveLastEntry = 1;
        }
        prevEntry = entry;
        havePrevEntry = 1;
    }

    if (haveLastEntry) {
        fgHoldEntryHalfY(&lastEntry, lastFrameData, 150, &lastEntry, 0);
    } else {
        fgHoldEntryHalfY(NULL, NULL, 150, NULL, 0);
    }

    if (lastFrameData != NULL)
        free(lastFrameData);
}

static void fgPlayFishing1Raw(void)
{
    fgShowRawFrame("\\FG\\FISH24.RAW;1", kFgPilotProbeHoldFrames);
}

static void fgPlayTitleCopy(void)
{
    fgShowRawFrame("\\TITLE.RAW;1", kFgPilotProbeHoldFrames);
}

static void fgPlayIsleTest(void)
{
    uint16 i;

    fgInitVisiblePipeline();
    grLoadScreen("ISLETEMP.SCR");
    for (i = 0; i < kFgPilotProbeHoldFrames; i++)
        grUpdateDisplay(NULL, NULL, NULL);
}

static void fgPlayAdsIntro(void)
{
    adsInit();
    adsNoIsland();
    adsPlayIntro();
}

static void fgPlayAdsFishing1(void)
{
    adsInit();
    adsNoIsland();
    adsPlay("FISHING", 1);
}

static void fgShowSolidColor(uint8 r, uint8 g, uint8 b, uint16 holdFrames)
{
    DISPENV disp;
    DRAWENV draw;
    uint16 i;

    ResetGraph(0);
    SetDefDispEnv(&disp, 0, 0, 640, 480);
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    disp.isinter = 1;
    setRGB0(&draw, r, g, b);
    draw.isbg = 1;

    PutDispEnv(&disp);
    PutDrawEnv(&draw);
    SetDispMask(1);

    DrawSync(0);
    for (i = 0; i < holdFrames; i++)
        VSync(0);
}

static void fgPlaySolidRed(void)
{
    fgShowSolidColor(255, 0, 0, kFgPilotProbeHoldFrames);
}

int foregroundPilotRequested(void)
{
    return gForegroundPilotRequestedMode != FG_RUNTIME_NONE;
}

const char *foregroundPilotSceneName(void)
{
    return gForegroundPilotScene;
}

void foregroundPilotSetScene(const char *sceneName)
{
    size_t i;

    if (!sceneName) {
        gForegroundPilotScene[0] = '\0';
        gForegroundPilotRequestedMode = FG_RUNTIME_NONE;
        gFgSetClearedEver = 1;
        return;
    }

    for (i = 0; i + 1 < sizeof(gForegroundPilotScene) && sceneName[i] != '\0'; i++)
        gForegroundPilotScene[i] = sceneName[i];
    gForegroundPilotScene[i] = '\0';
    gForegroundPilotRequestedMode = fgSceneModeForName(gForegroundPilotScene);
    gFgConfiguredEver = 1;
}

int foregroundPilotShouldStartForAds(const char *adsName, unsigned short adsTag)
{
    if (!foregroundPilotRequested() || adsName == NULL)
        return 0;

    if ((gForegroundPilotRequestedMode == FG_RUNTIME_FISHING1 ||
         gForegroundPilotRequestedMode == FG_RUNTIME_TESTCARD) &&
        fgAdsNameEquals(adsName, "FISHING") && adsTag == 1) {
        gFgAdsMatchEver = 1;
        return 1;
    }

    return 0;
}

int foregroundPilotRuntimeStartRequested(void)
{
    if (!foregroundPilotRequested())
        return 0;

    gFgStartAttemptEver = 1;
    switch (gForegroundPilotRequestedMode) {
        case FG_RUNTIME_TESTCARD:
            return foregroundPilotRuntimeStart("testcard");
        case FG_RUNTIME_FISHING1:
            return foregroundPilotRuntimeStart("fishing1");
        default:
            return foregroundPilotRuntimeStart(gForegroundPilotScene);
    }
}

int foregroundPilotRuntimeStartIfRequested(void)
{
    if (!foregroundPilotRequested())
        return 1;
    if (foregroundPilotRuntimeActive())
        return 1;
    return foregroundPilotRuntimeStartRequested();
}

void foregroundPilotPlay(void)
{
    if (fgSceneEquals(gForegroundPilotScene, "testcard")) {
        fgPlayTestCard();
        return;
    }

    if (fgSceneEquals(gForegroundPilotScene, "fishing1")) {
        fgPlayFishing1();
        return;
    }

    if (fgSceneEquals(gForegroundPilotScene, "fishing1p")) {
        fgPlayFishing1Progressive240();
        return;
    }

    if (fgSceneEquals(gForegroundPilotScene, "fishing1raw")) {
        fgPlayFishing1Raw();
        return;
    }

    if (fgSceneEquals(gForegroundPilotScene, "titlecopy")) {
        fgPlayTitleCopy();
        return;
    }

    if (fgSceneEquals(gForegroundPilotScene, "isletest")) {
        fgPlayIsleTest();
        return;
    }

    if (fgSceneEquals(gForegroundPilotScene, "adsintro")) {
        fgPlayAdsIntro();
        return;
    }

    if (fgSceneEquals(gForegroundPilotScene, "adsfishing1")) {
        fgPlayAdsFishing1();
        return;
    }

    if (fgSceneEquals(gForegroundPilotScene, "solidred")) {
        fgPlaySolidRed();
        return;
    }

    printf("FG pilot: unknown scene '%s'\n", gForegroundPilotScene);
}

#else

static char gForegroundPilotScene[16] = "";
static unsigned char gForegroundPilotRequestedMode = 0;

int foregroundPilotRequested(void)
{
    return gForegroundPilotRequestedMode != 0;
}

const char *foregroundPilotSceneName(void)
{
    return gForegroundPilotScene;
}

void foregroundPilotSetScene(const char *sceneName)
{
    if (!sceneName) {
        gForegroundPilotScene[0] = '\0';
        gForegroundPilotRequestedMode = 0;
        return;
    }
    strncpy(gForegroundPilotScene, sceneName, sizeof(gForegroundPilotScene) - 1);
    gForegroundPilotScene[sizeof(gForegroundPilotScene) - 1] = '\0';
    gForegroundPilotRequestedMode = 1;
}

int foregroundPilotShouldStartForAds(const char *adsName, unsigned short adsTag)
{
    (void)adsName;
    (void)adsTag;
    return 0;
}

int foregroundPilotRuntimeStartRequested(void)
{
    return 0;
}

int foregroundPilotRuntimeStartIfRequested(void)
{
    return foregroundPilotRequested() ? 0 : 1;
}

int foregroundPilotRuntimeMode(void)
{
    return 0;
}

unsigned short foregroundPilotRuntimeFrameIndex(void)
{
    return 0;
}

unsigned short foregroundPilotRuntimeSourceFrame(void)
{
    return 0;
}

unsigned short foregroundPilotRuntimeDisplayVBlanks(void)
{
    return 0;
}

int foregroundPilotRuntimeHasFrameData(void)
{
    return 0;
}

int foregroundPilotConfiguredEver(void)
{
    return foregroundPilotRequested() ? 1 : 0;
}

int foregroundPilotSetClearedEver(void)
{
    return 0;
}

int foregroundPilotRequestedNow(void)
{
    return foregroundPilotRequested() ? 1 : 0;
}

int foregroundPilotRuntimeAdsMatchEver(void)
{
    return 0;
}

int foregroundPilotRuntimeStartAttemptedEver(void)
{
    return 0;
}

int foregroundPilotRuntimeStartedEver(void)
{
    return 0;
}

int foregroundPilotRuntimeComposedEver(void)
{
    return 0;
}

void foregroundPilotPlay(void)
{
    fprintf(stderr, "foreground pilot is PS1-only for now (%s)\n", gForegroundPilotScene);
}

#endif
