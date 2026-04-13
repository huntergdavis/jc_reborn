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

static char gForegroundPilotScene[16] = "";
static const uint16 kFgPilotProbeHoldFrames = 1800;

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
    uint16 row;

    if (srcPixels == NULL || width == 0 || height == 0)
        return;

    for (row = 0; row < height; row++) {
        uint16 screenY = (uint16)(dstY + row);
        const uint16 *srcRow = srcPixels + ((uint32)row * (uint32)width);

        if (screenY >= 480)
            break;

        if (screenY < 240) {
            if (dstX < 320 && bgTile0 && bgTile0->pixels) {
                uint16 copyW = width;
                if (dstX + copyW > 320)
                    copyW = (uint16)(320 - dstX);
                memcpy(bgTile0->pixels + ((uint32)screenY * 320u) + dstX,
                       srcRow,
                       (size_t)copyW * sizeof(uint16));
            }
            if (dstX + width > 320 && bgTile1 && bgTile1->pixels) {
                uint16 srcOffset = (dstX < 320) ? (uint16)(320 - dstX) : 0;
                uint16 tileX = (dstX < 320) ? 0 : (uint16)(dstX - 320);
                uint16 copyW = (uint16)(width - srcOffset);
                if (tileX + copyW > 320)
                    copyW = (uint16)(320 - tileX);
                memcpy(bgTile1->pixels + ((uint32)screenY * 320u) + tileX,
                       srcRow + srcOffset,
                       (size_t)copyW * sizeof(uint16));
            }
        } else {
            uint16 tileY = (uint16)(screenY - 240);
            if (dstX < 320 && bgTile3 && bgTile3->pixels) {
                uint16 copyW = width;
                if (dstX + copyW > 320)
                    copyW = (uint16)(320 - dstX);
                memcpy(bgTile3->pixels + ((uint32)tileY * 320u) + dstX,
                       srcRow,
                       (size_t)copyW * sizeof(uint16));
            }
            if (dstX + width > 320 && bgTile4 && bgTile4->pixels) {
                uint16 srcOffset = (dstX < 320) ? (uint16)(320 - dstX) : 0;
                uint16 tileX = (dstX < 320) ? 0 : (uint16)(dstX - 320);
                uint16 copyW = (uint16)(width - srcOffset);
                if (tileX + copyW > 320)
                    copyW = (uint16)(320 - tileX);
                memcpy(bgTile4->pixels + ((uint32)tileY * 320u) + tileX,
                       srcRow + srcOffset,
                       (size_t)copyW * sizeof(uint16));
            }
        }
    }
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

void foregroundPilotSetScene(const char *sceneName)
{
    size_t i;

    if (!sceneName) {
        gForegroundPilotScene[0] = '\0';
        return;
    }

    for (i = 0; i + 1 < sizeof(gForegroundPilotScene) && sceneName[i] != '\0'; i++)
        gForegroundPilotScene[i] = sceneName[i];
    gForegroundPilotScene[i] = '\0';
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

void foregroundPilotSetScene(const char *sceneName)
{
    if (!sceneName) {
        gForegroundPilotScene[0] = '\0';
        return;
    }
    strncpy(gForegroundPilotScene, sceneName, sizeof(gForegroundPilotScene) - 1);
    gForegroundPilotScene[sizeof(gForegroundPilotScene) - 1] = '\0';
}

void foregroundPilotPlay(void)
{
    fprintf(stderr, "foreground pilot is PS1-only for now (%s)\n", gForegroundPilotScene);
}

#endif
