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

struct TFgPilotRuntime {
    uint8 active;
    uint8 mode;
    uint16 frameIndex;
    uint16 frameVBlank;
    uint16 displayVBlanks;
    uint16 holdFrames;
    struct TFgPilotHeader header;
    struct TFgPilotEntry currentEntry;
    uint8 *currentFrameData;
};

static char gForegroundPilotScene[16] = "";
static const uint16 kFgPilotProbeHoldFrames = 1800;
static struct TFgPilotRuntime gFgRuntime = {0};
static uint8 gFgConfiguredEver = 0;
static uint8 gFgRequestedEver = 0;
static uint8 gFgAdsMatchEver = 0;
static uint8 gFgStartAttemptEver = 0;
static uint8 gFgStartedEver = 0;
static uint8 gFgComposedEver = 0;

static void fgResetTelemetryFlags(void)
{
    gFgConfiguredEver = 0;
    gFgRequestedEver = 0;
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
    grUpdateDelay = 1;
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
    PS1Surface sprite;

    if (srcPixels == NULL || width == 0 || height == 0)
        return;

    memset(&sprite, 0, sizeof(sprite));
    sprite.width = width;
    sprite.height = height;
    sprite.pixels = (uint16 *)srcPixels;

    grCompositeToBackground(&sprite, (sint16)dstX, (sint16)dstY);
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

static void fgClearScreenDirect(void)
{
    static uint16 *blackStrip = NULL;
    const int stripHeight = 60;
    RECT rect;
    int y;

    if (blackStrip == NULL) {
        blackStrip = (uint16 *)calloc((size_t)640 * (size_t)stripHeight, sizeof(uint16));
        if (blackStrip == NULL)
            return;
    }

    for (y = 0; y < 480; y += stripHeight) {
        setRECT(&rect, 0, y, 640, stripHeight);
        LoadImage(&rect, (uint32 *)blackStrip);
    }
    DrawSync(0);
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
    DrawSync(0);
}

static void fgDrawEntry(const struct TFgPilotEntry *entry, uint8 *frameData)
{
    fgClearScreenDirect();

    if (entry != NULL)
        fgUploadDirect(entry->x, entry->y, entry->width, entry->height, frameData);

    VSync(0);
    eventsWaitTick(grUpdateDelay);
}

static void fgHoldEntry(const struct TFgPilotEntry *entry, uint8 *frameData, uint16 frames)
{
    uint16 i;

    for (i = 0; i < frames; i++)
        fgDrawEntry(entry, frameData);
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
    memset(&gFgRuntime, 0, sizeof(gFgRuntime));
    fgTelemetryUpdate();
}

static int fgRuntimeLoadFishingFrame(uint16 frameIndex)
{
    const char *path = "FG\\FISHING1.FG1";

    if (!fgLoadEntry(path, &gFgRuntime.header, frameIndex, &gFgRuntime.currentEntry))
        return 0;

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

    fgTelemetryUpdate();
    return 1;
}

int foregroundPilotRuntimeStart(const char *sceneName)
{
    fgRuntimeReset();
    gFgRequestedEver = 1;

    if (sceneName == NULL)
        return 0;

    if (fgSceneEquals(sceneName, "testcard")) {
        gFgRuntime.active = 1;
        gFgRuntime.mode = FG_RUNTIME_TESTCARD;
        gFgRuntime.holdFrames = kFgPilotProbeHoldFrames;
        gFgStartedEver = 1;
        fgTelemetryUpdate();
        return 1;
    }

    if (fgSceneEquals(sceneName, "fishing1")) {
        const char *path = "FG\\FISHING1.FG1";
        if (!fgLoadHeader(path, &gFgRuntime.header))
            return 0;
        gFgRuntime.active = 1;
        gFgRuntime.mode = FG_RUNTIME_FISHING1;
        gFgRuntime.displayVBlanks = gFgRuntime.header.displayVBlanks;
        gFgRuntime.holdFrames = 150;
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
    if (!gFgRuntime.active)
        return;

    if (gFgRuntime.mode == FG_RUNTIME_TESTCARD) {
        if (gFgRuntime.holdFrames > 0)
            gFgRuntime.holdFrames--;
        if (gFgRuntime.holdFrames == 0)
            gFgRuntime.active = 0;
        fgTelemetryUpdate();
        return;
    }

    if (gFgRuntime.mode == FG_RUNTIME_FISHING1) {
        if (gFgRuntime.frameIndex + 1 >= gFgRuntime.header.frameCount) {
            if (gFgRuntime.holdFrames > 0)
                gFgRuntime.holdFrames--;
            if (gFgRuntime.holdFrames == 0)
                gFgRuntime.active = 0;
            fgTelemetryUpdate();
            return;
        }

        gFgRuntime.frameVBlank++;
        if (gFgRuntime.frameVBlank < gFgRuntime.displayVBlanks) {
            fgTelemetryUpdate();
            return;
        }

        gFgRuntime.frameVBlank = 0;
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

int foregroundPilotRuntimeRequestedEver(void)
{
    return gFgRequestedEver ? 1 : 0;
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
    const char *path = "FG\\FISHING1.FG1";
    struct TFgPilotHeader header;
    struct TFgPilotEntry lastEntry;
    uint8 *lastFrameData = NULL;
    int haveLastEntry = 0;
    if (!fgLoadHeader(path, &header)) {
        printf("FG pilot: failed to load header %s\n", path);
        return;
    }

    fgInitVisiblePipeline();
    fgInitBlackBackground();
    fgPresentCurrentBackground(15);

    for (uint16 frameIndex = 0; frameIndex < header.frameCount; frameIndex++) {
        struct TFgPilotEntry entry;
        uint8 *frameData;

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
        grBeginFrame();
        grRestoreBgTiles();
        if (frameData != NULL)
            fgBlit16ToBackgroundRect(entry.x, entry.y, entry.width, entry.height,
                                     (const uint16 *)frameData);
        fgPresentCurrentBackground(1);

        {
            uint16 vblanks = header.displayVBlanks;
            if (vblanks > 1)
                fgPresentCurrentBackground((uint16)(vblanks - 1));
        }

        if (lastFrameData != NULL) {
            free(lastFrameData);
            lastFrameData = NULL;
        }
        if (frameData != NULL) {
            lastFrameData = frameData;
            lastEntry = entry;
            haveLastEntry = 1;
        }
    }

    if (haveLastEntry) {
        grBeginFrame();
        grRestoreBgTiles();
        fgBlit16ToBackgroundRect(lastEntry.x, lastEntry.y, lastEntry.width, lastEntry.height,
                                 (const uint16 *)lastFrameData);
        fgPresentCurrentBackground(150);
    } else {
        fgPresentCurrentBackground(150);
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
    return gForegroundPilotScene[0] != '\0';
}

const char *foregroundPilotSceneName(void)
{
    return gForegroundPilotScene;
}

void foregroundPilotSetScene(const char *sceneName)
{
    size_t i;

    fgResetTelemetryFlags();

    if (!sceneName) {
        gForegroundPilotScene[0] = '\0';
        return;
    }

    for (i = 0; i + 1 < sizeof(gForegroundPilotScene) && sceneName[i] != '\0'; i++)
        gForegroundPilotScene[i] = sceneName[i];
    gForegroundPilotScene[i] = '\0';
    gFgConfiguredEver = 1;
    gFgRequestedEver = 1;
}

int foregroundPilotShouldStartForAds(const char *adsName, unsigned short adsTag)
{
    if (!foregroundPilotRequested() || adsName == NULL)
        return 0;

    if ((fgSceneEquals(gForegroundPilotScene, "fishing1") ||
         fgSceneEquals(gForegroundPilotScene, "testcard")) &&
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
    return foregroundPilotRuntimeStart(gForegroundPilotScene);
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

int foregroundPilotRequested(void)
{
    return gForegroundPilotScene[0] != '\0';
}

const char *foregroundPilotSceneName(void)
{
    return gForegroundPilotScene;
}

void foregroundPilotSetScene(const char *sceneName)
{
    if (!sceneName) {
        gForegroundPilotScene[0] = '\0';
        return;
    }
    strncpy(gForegroundPilotScene, sceneName, sizeof(gForegroundPilotScene) - 1);
    gForegroundPilotScene[sizeof(gForegroundPilotScene) - 1] = '\0';
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

int foregroundPilotRuntimeRequestedEver(void)
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
