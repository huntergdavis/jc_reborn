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

static void fgDrawEntry(const struct TFgPilotEntry *entry, uint8 *frameData)
{
    PS1Surface sprite;

    grBeginFrame();
    grRestoreBgTiles();
    VSync(0);
    grDrawBackground();

    if (frameData != NULL && entry != NULL && entry->width > 0 && entry->height > 0) {
        memset(&sprite, 0, sizeof(sprite));
        sprite.pixels = (uint16 *)frameData;
        sprite.width = entry->width;
        sprite.height = entry->height;
        grBlitToFramebuffer(&sprite, (sint16)entry->x, (sint16)entry->y);
        DrawSync(0);
    }

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
    uint16 i;

    grInitEmptyBackground();
    grUpdateDelay = 1;

    for (i = 0; i < 120; i++) {
        grBeginFrame();
        grRestoreBgTiles();
        grDrawRect(NULL, 24, 24, 120, 80, 1);
        grDrawRect(NULL, 176, 24, 120, 80, 2);
        grDrawRect(NULL, 24, 136, 120, 80, 4);
        grDrawRect(NULL, 176, 136, 120, 80, 5);
        grUpdateDisplay(NULL, NULL, NULL);
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

    adsInit();
    adsInitIsland();
    grUpdateDelay = 1;
    fgHoldEntry(NULL, NULL, 15);

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
        fgDrawEntry(&entry, frameData);

        {
            uint16 vblanks = header.displayVBlanks;
            if (vblanks == 0)
                vblanks = 1;
            for (uint16 i = 1; i < vblanks; i++)
                VSync(0);
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
        fgHoldEntry(&lastEntry, lastFrameData, 150);
    } else {
        fgHoldEntry(NULL, NULL, 150);
    }

    if (lastFrameData != NULL)
        free(lastFrameData);
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
