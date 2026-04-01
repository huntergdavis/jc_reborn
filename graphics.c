/*
 *  This file is part of 'Johnny Reborn'
 *
 *  An open-source engine for the classic
 *  'Johnny Castaway' screensaver by Sierra.
 *
 *  Copyright (C) 2019 Jeremie GUILLAUME
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
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <SDL2/SDL.h>

#include "mytypes.h"
#include "utils.h"
#include "graphics.h"
#include "resource.h"
#include "events.h"


static SDL_Window *sdl_window;

static uint8 ttmPalette[16][4];

static SDL_Surface *grSavedZonesLayer = NULL;

static SDL_Rect grScreenOrigin = { 0, 0, 0, 0 };   // TODO

SDL_Surface *grBackgroundSfc = NULL;

int grDx = 0;
int grDy = 0;
int grWindowed = 0;
int grUpdateDelay = 0;

/* Frame capture for visual regression testing */
int grCaptureFrameNumber = -1;
char *grCaptureFilename = NULL;
char *grCaptureDir = NULL;
char *grCaptureMetaDir = NULL;
int grCaptureInterval = 0;
int grCaptureStartFrame = 0;
int grCaptureEndFrame = -1;
int grCaptureOverlay = 0;
static int grCurrentFrame = 0;
static char grCaptureSceneLabel[64] = "";

#define MAX_CAPTURED_DRAWS 2048
#define MAX_CAPTURE_SURFACES 64
#define MAX_LEDGER_DRAWS 4096

struct TCapturedSpriteDraw {
    sint16 x;
    sint16 y;
    uint16 width;
    uint16 height;
    uint16 spriteNo;
    uint16 imageNo;
    uint8 flipped;
    const char *bmpName;
};

static struct TCapturedSpriteDraw grCapturedDraws[MAX_CAPTURED_DRAWS];
static int grCapturedDrawCount = 0;

struct TSurfaceCaptureLedger {
    SDL_Surface *surface;
    struct TCapturedSpriteDraw draws[MAX_LEDGER_DRAWS];
    int count;
};

static struct TSurfaceCaptureLedger grCaptureLedgers[MAX_CAPTURE_SURFACES];
static int grCaptureLedgerCount = 0;

/* SDL Surface Pool for memory optimization */
#define MAX_SURFACE_POOL_SIZE 2  /* Reduced to 2 for <500KB target (most scenes use 1-2) */
static SDL_Surface *surfacePool[MAX_SURFACE_POOL_SIZE];
static int surfacePoolInUse[MAX_SURFACE_POOL_SIZE];
static int surfacePoolInitialized = 0;

/* Forward declarations for surface pool */
static void grInitSurfacePool(void);
static void grCleanupSurfacePool(void);
static void grCaptureResetLedger(SDL_Surface *surface);
static struct TSurfaceCaptureLedger *grCaptureFindLedger(SDL_Surface *surface, int create);
void grFreeLayer(SDL_Surface *sfc);

static void grReleaseScreen()
{
    grCaptureResetLedger(grBackgroundSfc);
    free(grBackgroundSfc->pixels);
    SDL_FreeSurface(grBackgroundSfc);
    grBackgroundSfc = NULL;
}


static void grReleaseSavedLayer()
{
    if (grSavedZonesLayer != NULL) {
        grCaptureResetLedger(grSavedZonesLayer);
        grFreeLayer(grSavedZonesLayer);
        grSavedZonesLayer = NULL;
        if (debugMode) {
            printf("Freed grSavedZonesLayer (307KB saved)\n");
        }
    }
}


static void grCaptureResetFrameDraws(void)
{
    grCapturedDrawCount = 0;
}


static struct TSurfaceCaptureLedger *grCaptureFindLedger(SDL_Surface *surface, int create)
{
    int i;

    if (surface == NULL)
        return NULL;

    for (i = 0; i < grCaptureLedgerCount; i++) {
        if (grCaptureLedgers[i].surface == surface)
            return &grCaptureLedgers[i];
    }

    if (!create || grCaptureLedgerCount >= MAX_CAPTURE_SURFACES)
        return NULL;

    grCaptureLedgers[grCaptureLedgerCount].surface = surface;
    grCaptureLedgers[grCaptureLedgerCount].count = 0;
    return &grCaptureLedgers[grCaptureLedgerCount++];
}


static void grCaptureResetLedger(SDL_Surface *surface)
{
    struct TSurfaceCaptureLedger *ledger = grCaptureFindLedger(surface, 0);
    if (ledger != NULL)
        ledger->count = 0;
}


static void grCaptureAppendLedgerDraw(SDL_Surface *surface,
                                      const struct TCapturedSpriteDraw *draw)
{
    struct TSurfaceCaptureLedger *ledger = grCaptureFindLedger(surface, 1);
    if (ledger == NULL || draw == NULL || ledger->count >= MAX_LEDGER_DRAWS)
        return;

    ledger->draws[ledger->count++] = *draw;
}


static void grCaptureRecordSpriteDraw(struct TTtmSlot *ttmSlot,
                                      sint16 x,
                                      sint16 y,
                                      uint16 spriteNo,
                                      uint16 imageNo,
                                      SDL_Surface *srcSfc,
                                      int flipped)
{
    struct TCapturedSpriteDraw *draw;

    if (ttmSlot == NULL || srcSfc == NULL)
        return;

    if (grCapturedDrawCount >= MAX_CAPTURED_DRAWS)
        return;

    draw = &grCapturedDraws[grCapturedDrawCount++];
    draw->x = x;
    draw->y = y;
    draw->width = (uint16)srcSfc->w;
    draw->height = (uint16)srcSfc->h;
    draw->spriteNo = spriteNo;
    draw->imageNo = imageNo;
    draw->flipped = flipped ? 1 : 0;
    draw->bmpName = ttmSlot->loadedBmpNames[imageNo];
}


static void grCaptureRecordSurfaceDraw(SDL_Surface *surface,
                                       struct TTtmSlot *ttmSlot,
                                       sint16 x,
                                       sint16 y,
                                       uint16 spriteNo,
                                       uint16 imageNo,
                                       SDL_Surface *srcSfc,
                                       int flipped)
{
    struct TCapturedSpriteDraw draw;

    if (surface == NULL || ttmSlot == NULL || srcSfc == NULL)
        return;

    draw.x = x;
    draw.y = y;
    draw.width = (uint16)srcSfc->w;
    draw.height = (uint16)srcSfc->h;
    draw.spriteNo = spriteNo;
    draw.imageNo = imageNo;
    draw.flipped = flipped ? 1 : 0;
    draw.bmpName = ttmSlot->loadedBmpNames[imageNo];
    grCaptureAppendLedgerDraw(surface, &draw);
}


static void grCaptureWriteJsonString(FILE *f, const char *value)
{
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    fputc('"', f);
    while (*p) {
        switch (*p) {
            case '\\':
            case '"':
                fputc('\\', f);
                fputc(*p, f);
                break;
            case '\n':
                fputs("\\n", f);
                break;
            case '\r':
                fputs("\\r", f);
                break;
            case '\t':
                fputs("\\t", f);
                break;
            default:
                if (*p < 0x20)
                    fprintf(f, "\\u%04x", *p);
                else
                    fputc(*p, f);
                break;
        }
        p++;
    }
    fputc('"', f);
}


static int grCaptureMkdirIfNeeded(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return -1;

    if (mkdir(path, 0777) == 0 || errno == EEXIST)
        return 0;

    fprintf(stderr, "Warning: could not create capture directory %s: %s\n",
            path, strerror(errno));
    return -1;
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


static size_t grCaptureBuildOverlayPayload(uint8 *buffer, size_t capacity)
{
    size_t offset = 0;
    int limit = grCapturedDrawCount;
    int i;

    if (capacity < 11)
        return 0;

    if (limit > 16)
        limit = 16;

    buffer[offset++] = 'J';
    buffer[offset++] = 'C';
    buffer[offset++] = 'D';
    buffer[offset++] = '1';
    buffer[offset++] = (uint8)(grCurrentFrame & 0xff);
    buffer[offset++] = (uint8)((grCurrentFrame >> 8) & 0xff);
    buffer[offset++] = (uint8)((grCurrentFrame >> 16) & 0xff);
    buffer[offset++] = (uint8)((grCurrentFrame >> 24) & 0xff);
    buffer[offset++] = (uint8)(grCapturedDrawCount & 0xff);
    buffer[offset++] = (uint8)((grCapturedDrawCount >> 8) & 0xff);
    buffer[offset++] = (uint8)limit;

    for (i = 0; i < limit; i++) {
        const struct TCapturedSpriteDraw *draw = &grCapturedDraws[i];
        unsigned int nameHash = 0;
        const unsigned char *p = (const unsigned char *)(draw->bmpName ? draw->bmpName : "");

        while (*p) {
            nameHash = ((nameHash << 5) - nameHash) + *p;
            p++;
        }

        if (offset + 12 > capacity)
            break;

        buffer[offset++] = (uint8)((uint16)draw->x & 0xff);
        buffer[offset++] = (uint8)(((uint16)draw->x >> 8) & 0xff);
        buffer[offset++] = (uint8)((uint16)draw->y & 0xff);
        buffer[offset++] = (uint8)(((uint16)draw->y >> 8) & 0xff);
        buffer[offset++] = (uint8)(draw->width & 0xff);
        buffer[offset++] = (uint8)(draw->height & 0xff);
        buffer[offset++] = (uint8)(draw->spriteNo & 0xff);
        buffer[offset++] = (uint8)(draw->imageNo & 0xff);
        buffer[offset++] = draw->flipped;
        buffer[offset++] = (uint8)(nameHash & 0xff);
        buffer[offset++] = (uint8)((nameHash >> 8) & 0xff);
        buffer[offset++] = (uint8)((nameHash >> 16) & 0xff);
    }

    return offset;
}


static void grCaptureEmbedOverlay(SDL_Surface *sfc)
{
    static const uint8 colors[4][3] = {
        { 0x00, 0x00, 0x00 },
        { 0x00, 0x66, 0xff },
        { 0x00, 0xcc, 0x66 },
        { 0xff, 0xcc, 0x00 }
    };
    uint8 payload[512];
    uint8 packet[1024];
    size_t payloadLen;
    size_t packetLen;
    uint32 crc;
    int widthCells = 32;
    int heightCells = 32;
    int cellSize = 4;
    int originX;
    int originY;
    size_t symbolIndex = 0;
    int cellX;
    int cellY;

    if (sfc == NULL || sfc->format == NULL)
        return;

    payloadLen = grCaptureBuildOverlayPayload(payload, sizeof(payload));
    if (payloadLen == 0)
        return;

    crc = grCaptureCrc32(payload, payloadLen);
    packet[0] = (uint8)(payloadLen & 0xff);
    packet[1] = (uint8)((payloadLen >> 8) & 0xff);
    memcpy(packet + 2, payload, payloadLen);
    packet[2 + payloadLen + 0] = (uint8)(crc & 0xff);
    packet[2 + payloadLen + 1] = (uint8)((crc >> 8) & 0xff);
    packet[2 + payloadLen + 2] = (uint8)((crc >> 16) & 0xff);
    packet[2 + payloadLen + 3] = (uint8)((crc >> 24) & 0xff);
    packetLen = payloadLen + 6;

    originX = sfc->w - (widthCells * cellSize);
    originY = sfc->h - (heightCells * cellSize);
    if (originX < 0 || originY < 0)
        return;

    for (cellY = 0; cellY < heightCells; cellY++) {
        for (cellX = 0; cellX < widthCells; cellX++) {
            int value = 0;
            SDL_Rect rect;

            if ((cellX < 2 && cellY < 2) ||
                (cellX >= widthCells - 2 && cellY < 2) ||
                (cellX < 2 && cellY >= heightCells - 2) ||
                (cellX >= widthCells - 2 && cellY >= heightCells - 2)) {
                value = 3;
            }
            else if (symbolIndex < packetLen * 4) {
                size_t byteIndex = symbolIndex / 4;
                int shift = (int)((symbolIndex % 4) * 2);
                value = (packet[byteIndex] >> shift) & 0x3;
                symbolIndex++;
            }
            rect.x = originX + (cellX * cellSize);
            rect.y = originY + (cellY * cellSize);
            rect.w = cellSize;
            rect.h = cellSize;
            SDL_FillRect(sfc, &rect,
                         SDL_MapRGB(sfc->format,
                                    colors[value][0],
                                    colors[value][1],
                                    colors[value][2]));
        }
    }
}


static const char *grCaptureSurfaceRole(SDL_Surface *surface,
                                        struct TTtmThread *ttmThreads,
                                        struct TTtmThread *ttmHolidayThread,
                                        char *buffer,
                                        size_t bufferSize)
{
    int i;

    if (surface == NULL)
        return "unknown";
    if (surface == grBackgroundSfc)
        return "background";
    if (surface == grSavedZonesLayer)
        return "saved_zones";
    if (ttmHolidayThread != NULL && surface == ttmHolidayThread->ttmLayer)
        return "holiday_layer";

    for (i = 0; i < MAX_TTM_THREADS; i++) {
        if (ttmThreads[i].isRunning && surface == ttmThreads[i].ttmLayer) {
            snprintf(buffer, bufferSize, "thread_layer_%d", i);
            return buffer;
        }
    }

    snprintf(buffer, bufferSize, "surface_%p", (void *)surface);
    return buffer;
}


static int grCaptureCountVisibleDraws(struct TTtmThread *ttmThreads,
                                      struct TTtmThread *ttmHolidayThread)
{
    int total = 0;
    int i;

    for (i = 0; i < grCaptureLedgerCount; i++) {
        SDL_Surface *surface = grCaptureLedgers[i].surface;

        if (surface == NULL)
            continue;
        if (surface == grBackgroundSfc || surface == grSavedZonesLayer) {
            total += grCaptureLedgers[i].count;
            continue;
        }
        if (ttmHolidayThread != NULL && ttmHolidayThread->isRunning &&
            surface == ttmHolidayThread->ttmLayer) {
            total += grCaptureLedgers[i].count;
            continue;
        }
        for (int t = 0; t < MAX_TTM_THREADS; t++) {
            if (ttmThreads[t].isRunning && surface == ttmThreads[t].ttmLayer) {
                total += grCaptureLedgers[i].count;
                break;
            }
        }
    }

    return total;
}


static void grCaptureWriteDrawArray(FILE *f,
                                    const char *fieldName,
                                    const struct TCapturedSpriteDraw *draws,
                                    int drawCount)
{
    int i;

    fprintf(f, "  \"%s\": [\n", fieldName);
    for (i = 0; i < drawCount; i++) {
        const struct TCapturedSpriteDraw *draw = &draws[i];
        fprintf(f, "    {\"index\": %d, \"bmp_name\": ", i);
        grCaptureWriteJsonString(f, draw->bmpName);
        fprintf(f,
                ", \"image_no\": %u, \"sprite_no\": %u, \"x\": %d, \"y\": %d, \"width\": %u, \"height\": %u, \"flipped\": %s}%s\n",
                draw->imageNo,
                draw->spriteNo,
                draw->x,
                draw->y,
                draw->width,
                draw->height,
                draw->flipped ? "true" : "false",
                (i + 1 < drawCount) ? "," : "");
    }
    fprintf(f, "  ]");
}


static void grCaptureWriteVisibleDrawArray(FILE *f,
                                           struct TTtmThread *ttmThreads,
                                           struct TTtmThread *ttmHolidayThread)
{
    int i;
    int emitted = 0;

    fprintf(f, "  \"visible_draws\": [\n");
    for (i = 0; i < grCaptureLedgerCount; i++) {
        SDL_Surface *surface = grCaptureLedgers[i].surface;
        char roleBuffer[32];
        const char *role;
        int d;
        int surfaceVisible = 0;

        if (surface == NULL)
            continue;
        if (surface == grBackgroundSfc || surface == grSavedZonesLayer)
            surfaceVisible = 1;
        if (ttmHolidayThread != NULL && ttmHolidayThread->isRunning &&
            surface == ttmHolidayThread->ttmLayer)
            surfaceVisible = 1;
        for (int t = 0; !surfaceVisible && t < MAX_TTM_THREADS; t++) {
            if (ttmThreads[t].isRunning && surface == ttmThreads[t].ttmLayer)
                surfaceVisible = 1;
        }
        if (!surfaceVisible)
            continue;

        role = grCaptureSurfaceRole(surface, ttmThreads, ttmHolidayThread,
                                    roleBuffer, sizeof(roleBuffer));
        for (d = 0; d < grCaptureLedgers[i].count; d++) {
            const struct TCapturedSpriteDraw *draw = &grCaptureLedgers[i].draws[d];
            fprintf(f, "    {\"index\": %d, \"surface_role\": ", emitted);
            grCaptureWriteJsonString(f, role);
            fprintf(f, ", \"bmp_name\": ");
            grCaptureWriteJsonString(f, draw->bmpName);
            fprintf(f,
                    ", \"image_no\": %u, \"sprite_no\": %u, \"x\": %d, \"y\": %d, \"width\": %u, \"height\": %u, \"flipped\": %s},\n",
                    draw->imageNo,
                    draw->spriteNo,
                    draw->x,
                    draw->y,
                    draw->width,
                    draw->height,
                    draw->flipped ? "true" : "false");
            emitted++;
        }
    }
    if (emitted > 0) {
        long pos = ftell(f);
        fseek(f, pos - 2, SEEK_SET);
        fprintf(f, "\n");
    }
    fprintf(f, "  ]");
}


static void grCaptureWriteFrameMetadata(const char *filename,
                                        struct TTtmThread *ttmThreads,
                                        struct TTtmThread *ttmHolidayThread)
{
    char metaPath[1024];
    FILE *f;

    if (grCaptureMetaDir == NULL || grCaptureMetaDir[0] == '\0')
        return;

    if (grCaptureMkdirIfNeeded(grCaptureMetaDir) != 0)
        return;

    snprintf(metaPath, sizeof(metaPath), "%s/frame_%05d.json",
             grCaptureMetaDir, grCurrentFrame);

    f = fopen(metaPath, "w");
    if (f == NULL) {
        fprintf(stderr, "Warning: could not write capture metadata %s\n", metaPath);
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"frame_number\": %d,\n", grCurrentFrame);
    fprintf(f, "  \"scene_label\": ");
    grCaptureWriteJsonString(f, grCaptureSceneLabel);
    fprintf(f, ",\n");
    fprintf(f, "  \"image_path\": ");
    grCaptureWriteJsonString(f, filename ? filename : "");
    fprintf(f, ",\n");
    fprintf(f, "  \"draw_count\": %d,\n", grCapturedDrawCount);
    fprintf(f, "  \"visible_draw_count\": %d,\n",
            grCaptureCountVisibleDraws(ttmThreads, ttmHolidayThread));
    fprintf(f, "  \"overlay_enabled\": %s,\n", grCaptureOverlay ? "true" : "false");
    grCaptureWriteDrawArray(f, "draws", grCapturedDraws, grCapturedDrawCount);
    fprintf(f, ",\n");
    grCaptureWriteVisibleDrawArray(f, ttmThreads, ttmHolidayThread);
    fprintf(f, "\n}\n");
    fclose(f);
}


static void grPutPixel(SDL_Surface *sfc, uint16 x, uint16 y, uint8 color)
{
    // TODO: Implement Cohen-Sutherland clipping algorithm or such for
    // grDrawLine(), and another ad hoc algorithm for grDrawCircle()

    if (x>=0 && y>=0 && x<640 && y<480) {

        uint8 *pixel = (uint8*) sfc->pixels;

        pixel += (y * sfc->pitch) + (x * sfc->format->BytesPerPixel);

        pixel[0] = ttmPalette[color][0];
        pixel[1] = ttmPalette[color][1];
        pixel[2] = ttmPalette[color][2];
        pixel[3] = 0;
    }
}


static void grDrawHorizontalLine(SDL_Surface *sfc, sint16 x1, sint16 x2, sint16 y, uint8 color)
{
    if (y < 0 || y > 479)
        return;

    x1 = x1 < 0   ? 0   : x1;
    x2 = x2 > 639 ? 639 : x2;

    for (int x=x1; x<=x2; x++)
        grPutPixel(sfc, x, y, color);
}


void grLoadPalette(struct TPalResource *palResource)
{
    if (palResource == NULL)
        fatalError("NULL palette\n");

    for (int i=0; i < 16; i++) {
        ttmPalette[i][0] = palResource->colors[i].b << 2;
        ttmPalette[i][1] = palResource->colors[i].g << 2;
        ttmPalette[i][2] = palResource->colors[i].r << 2;
        ttmPalette[i][3] = 0;
    }
}


void graphicsInit()
{
    SDL_Init(SDL_INIT_VIDEO);

    sdl_window = SDL_CreateWindow(
        "Johnny Reborn ...?",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        (grWindowed ? 0 : SDL_WINDOW_FULLSCREEN)
    );

    if (sdl_window == NULL)
        fatalError("Could not create window: %s", SDL_GetError());

    grScreenOrigin.x = (SCREEN_WIDTH - 640) / 2;
    grScreenOrigin.y = (SCREEN_HEIGHT - 480) / 2;

    if (!grWindowed)
        SDL_ShowCursor(SDL_DISABLE);

    SDL_UpdateWindowSurface(sdl_window);

    grLoadPalette(palResources[0]);  // TODO ?

    eventsInit();
    grCaptureResetFrameDraws();
}


void graphicsEnd()
{
    grCleanupSurfacePool();
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
}


void grRefreshDisplay()
{
    SDL_UpdateWindowSurface(sdl_window);
}


void grToggleFullScreen()
{
    grWindowed = !grWindowed;

    if (grWindowed) {
        SDL_SetWindowFullscreen(sdl_window, 0);
        SDL_ShowCursor(SDL_ENABLE);
    }
    else {
        SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_FULLSCREEN);
        SDL_ShowCursor(SDL_DISABLE);
    }

    SDL_UpdateWindowSurface(sdl_window);
}


void grUpdateDisplay(struct TTtmThread *ttmBackgroundThread,
                     struct TTtmThread *ttmThreads,
                     struct TTtmThread *ttmHolidayThread)
{
    int shouldCapture = 0;
    char defaultFilename[1024];
    const char *filename = NULL;

    // Blit the background
    if (grBackgroundSfc != NULL)
        SDL_BlitSurface(grBackgroundSfc,
                        NULL,
                        SDL_GetWindowSurface(sdl_window),
                        &grScreenOrigin);

    // If not NULL, blit the optional layer of saved zones
    if (grSavedZonesLayer != NULL)
        SDL_BlitSurface(grSavedZonesLayer,
                        NULL,
                        SDL_GetWindowSurface(sdl_window),
                        &grScreenOrigin);


    // Blit successively each thread's layer
    for (int i=0; i < MAX_TTM_THREADS; i++)
        if (ttmThreads[i].isRunning)
            SDL_BlitSurface(ttmThreads[i].ttmLayer,
                            NULL,
                            SDL_GetWindowSurface(sdl_window),
                            &grScreenOrigin);

    // Finally, blit the holiday layer
    if (ttmHolidayThread != NULL)
        if (ttmHolidayThread->isRunning)
            SDL_BlitSurface(ttmHolidayThread->ttmLayer,
                            NULL,
                            SDL_GetWindowSurface(sdl_window),
                            &grScreenOrigin);

    // Wait for the tick ...
    eventsWaitTick(grUpdateDelay);

    // ... and refresh the display
    SDL_UpdateWindowSurface(sdl_window);

    /* Frame capture for visual regression testing */
    if (grCaptureFrameNumber >= 0 && grCurrentFrame == grCaptureFrameNumber) {
        shouldCapture = 1;
        if (grCaptureFilename != NULL) {
            filename = grCaptureFilename;
        } else {
            snprintf(defaultFilename, sizeof(defaultFilename),
                     "frame_%05d.bmp", grCurrentFrame);
            filename = defaultFilename;
        }
    }
    else if (grCaptureDir != NULL && grCaptureDir[0] != '\0' &&
             grCurrentFrame >= grCaptureStartFrame &&
             (grCaptureEndFrame < 0 || grCurrentFrame <= grCaptureEndFrame) &&
             grCaptureInterval > 0 &&
             (((grCurrentFrame - grCaptureStartFrame) % grCaptureInterval) == 0)) {
        if (grCaptureMkdirIfNeeded(grCaptureDir) == 0) {
            shouldCapture = 1;
            snprintf(defaultFilename, sizeof(defaultFilename),
                     "%s/frame_%05d.bmp", grCaptureDir, grCurrentFrame);
            filename = defaultFilename;
        }
    }

    if (shouldCapture && filename != NULL) {
        if (grCaptureFrame(filename) == 0) {
            grCaptureWriteFrameMetadata(filename, ttmThreads, ttmHolidayThread);
            printf("Frame %d captured to %s\n", grCurrentFrame, filename);
            if (grCaptureFrameNumber >= 0)
                grCaptureFrameNumber = -1;
        }
    }

    grCurrentFrame++;
    grCaptureResetFrameDraws();
}


/*
 * Initialize surface pool
 * Called once at graphics init
 */
static void grInitSurfacePool(void)
{
    if (surfacePoolInitialized) {
        return;
    }

    for (int i = 0; i < MAX_SURFACE_POOL_SIZE; i++) {
        surfacePool[i] = NULL;
        surfacePoolInUse[i] = 0;
    }

    surfacePoolInitialized = 1;

    if (debugMode) {
        printf("Surface pool initialized (max %d surfaces)\n", MAX_SURFACE_POOL_SIZE);
    }
}

/*
 * Clean up surface pool
 * Called at graphics shutdown
 */
static void grCleanupSurfacePool(void)
{
    if (!surfacePoolInitialized) {
        return;
    }

    int freed = 0;
    for (int i = 0; i < MAX_SURFACE_POOL_SIZE; i++) {
        if (surfacePool[i] != NULL) {
            SDL_FreeSurface(surfacePool[i]);
            surfacePool[i] = NULL;
            freed++;
        }
        surfacePoolInUse[i] = 0;
    }

    surfacePoolInitialized = 0;

    if (debugMode) {
        printf("Surface pool cleaned up (%d surfaces freed)\n", freed);
    }
}

/*
 * Acquire a surface from the pool
 * Replaces grNewLayer() with pooled allocation
 */
SDL_Surface *grNewLayer()
{
    if (!surfacePoolInitialized) {
        grInitSurfacePool();
    }

    /* Try to find an available surface in the pool */
    for (int i = 0; i < MAX_SURFACE_POOL_SIZE; i++) {
        if (surfacePool[i] != NULL && !surfacePoolInUse[i]) {
            /* Reuse existing surface */
            surfacePoolInUse[i] = 1;
            grCaptureResetLedger(surfacePool[i]);

            /* Clear the surface for reuse with magenta color index */
            /* Find magenta (0xa8, 0, 0xa8) in the palette for transparent color key */
            int magentaIndex = -1;
            for (int j = 0; j < 16; j++) {
                if (ttmPalette[j][2] == 0xa8 && ttmPalette[j][1] == 0 && ttmPalette[j][0] == 0xa8) {
                    magentaIndex = j;
                    break;
                }
            }

            SDL_Rect dest = { 0, 0, 640, 480 };
            if (magentaIndex >= 0) {
                SDL_FillRect(surfacePool[i], &dest, magentaIndex);
            } else {
                /* Fallback if magenta not found */
                SDL_FillRect(surfacePool[i], &dest, 0);
            }

            if (debugMode) {
                printf("Surface pool: reused 8-bit slot %d\n", i);
            }

            return surfacePool[i];
        }
    }

    /* No available surface, try to allocate a new one */
    for (int i = 0; i < MAX_SURFACE_POOL_SIZE; i++) {
        if (surfacePool[i] == NULL) {
            /* Allocate 8-bit indexed surface instead of 32-bit RGBA - 4x memory savings! */
            surfacePool[i] = SDL_CreateRGBSurface(SDL_SWSURFACE, 640, 480, 8, 0, 0, 0, 0);

            if (surfacePool[i] == NULL) {
                fprintf(stderr, "Error: Failed to create surface: %s\n", SDL_GetError());
                return NULL;
            }

            /* Set up the 16-color palette for this indexed surface */
            SDL_Color colors[16];
            for (int j = 0; j < 16; j++) {
                colors[j].r = ttmPalette[j][2];
                colors[j].g = ttmPalette[j][1];
                colors[j].b = ttmPalette[j][0];
                colors[j].a = 255;
            }
            SDL_SetPaletteColors(surfacePool[i]->format->palette, colors, 0, 16);

            /* Find magenta (0xa8, 0, 0xa8) in the palette for transparent color key */
            int magentaIndex = -1;
            for (int j = 0; j < 16; j++) {
                if (ttmPalette[j][2] == 0xa8 && ttmPalette[j][1] == 0 && ttmPalette[j][0] == 0xa8) {
                    magentaIndex = j;
                    break;
                }
            }

            /* Clear surface with magenta color index and set as transparent */
            SDL_Rect dest = { 0, 0, 640, 480 };
            if (magentaIndex >= 0) {
                SDL_FillRect(surfacePool[i], &dest, magentaIndex);
                SDL_SetColorKey(surfacePool[i], SDL_TRUE, magentaIndex);
            } else {
                /* Fallback if magenta not found in palette */
                SDL_FillRect(surfacePool[i], &dest, 0);
            }

            surfacePoolInUse[i] = 1;
            grCaptureFindLedger(surfacePool[i], 1);

            if (debugMode) {
                printf("Surface pool: allocated new 8-bit indexed slot %d (307KB instead of 1.2MB)\n", i);
            }

            return surfacePool[i];
        }
    }

    /* Pool exhausted - fall back to non-pooled 8-bit allocation */
    fprintf(stderr, "Warning: Surface pool exhausted, allocating non-pooled 8-bit surface\n");
    SDL_Surface *sfc = SDL_CreateRGBSurface(SDL_SWSURFACE, 640, 480, 8, 0, 0, 0, 0);

    if (sfc != NULL) {
        /* Set up the 16-color palette for this indexed surface */
        SDL_Color colors[16];
        for (int j = 0; j < 16; j++) {
            colors[j].r = ttmPalette[j][2];
            colors[j].g = ttmPalette[j][1];
            colors[j].b = ttmPalette[j][0];
            colors[j].a = 255;
        }
        SDL_SetPaletteColors(sfc->format->palette, colors, 0, 16);

        /* Find magenta for transparent color key */
        int magentaIndex = -1;
        for (int j = 0; j < 16; j++) {
            if (ttmPalette[j][2] == 0xa8 && ttmPalette[j][1] == 0 && ttmPalette[j][0] == 0xa8) {
                magentaIndex = j;
                break;
            }
        }

        SDL_Rect dest = { 0, 0, 640, 480 };
        if (magentaIndex >= 0) {
            SDL_FillRect(sfc, &dest, magentaIndex);
            SDL_SetColorKey(sfc, SDL_TRUE, magentaIndex);
        } else {
            SDL_FillRect(sfc, &dest, 0);
        }
    }

    grCaptureFindLedger(sfc, 1);

    return sfc;
}


/*
 * Release a surface back to the pool
 * Replaces grFreeLayer() with pooled deallocation
 */
void grFreeLayer(SDL_Surface *sfc)
{
    if (sfc == NULL) {
        return;
    }

    if (!surfacePoolInitialized) {
        /* Pool not initialized, just free directly */
        SDL_FreeSurface(sfc);
        return;
    }

    /* Check if this surface is in the pool */
    for (int i = 0; i < MAX_SURFACE_POOL_SIZE; i++) {
        if (surfacePool[i] == sfc) {
            /* Mark as available for reuse */
            surfacePoolInUse[i] = 0;
            grCaptureResetLedger(sfc);

            if (debugMode) {
                printf("Surface pool: released slot %d\n", i);
            }

            return;
        }
    }

    /* Surface not in pool, free it directly */
    if (debugMode) {
        printf("Surface pool: freeing non-pooled surface\n");
    }
    SDL_FreeSurface(sfc);
}


void grSetClipZone(SDL_Surface *sfc, sint16 x1, sint16 y1, sint16 x2, sint16 y2)
{
    x1 += grDx; y1 += grDy;
    x2 += grDx; y2 += grDy;

    SDL_Rect rect = { x1, y1, x2-x1, y2-y1 };
    SDL_SetClipRect(sfc, &rect);
}


void grCopyZoneToBg(SDL_Surface *sfc, uint16 x, uint16 y, uint16 width, uint16 height)
{
    x += grDx; y += grDy;
    SDL_Rect rect = { (short) x, (short) y, width + 2, height };

    if (grSavedZonesLayer == NULL) {
        grSavedZonesLayer = grNewLayer();
        if (debugMode) {
            printf("Lazy allocated grSavedZonesLayer (307KB 8-bit surface)\n");
        }
    }

    SDL_BlitSurface(sfc, &rect, grSavedZonesLayer, &rect);

    // Note : without the +2 in width+2 above, there would be a graphical
    // glitch (2 unfilled pixels) on the hull of the cargo, caused by an
    // error in coordinates in GJIVS6.TTM
    // Obviously, the original soft rounds the SAVE_IMAGE boundaries on
    // one way or another.
}


void grSaveImage1(SDL_Surface *sfc, uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3) // TODO : rename ?
{
//    ttmSetColors(4,4);
//    ttmDrawRect(arg0,arg1,arg2,arg3);
//    ttmSaveImage0(arg0,arg1,arg2,arg3);
//    ttmUpdate();
}


void grSaveZone(SDL_Surface *sfc, uint16 x, uint16 y, uint16 width, uint16 height)
{
    // Minimalistic implementation: we don't really save the zone,
    // and let grRestoreZone() simply erase the 'saved zones' layer
}


void grRestoreZone(SDL_Surface *sfc, uint16 x, uint16 y, uint16 width, uint16 height)
{
    // In Johnny's TTMs, we never have RESTORE_ZONE called
    // while several zones are saved. So we simply free the
    // whole saved zones layer
    grReleaseSavedLayer();
}


void grDrawPixel(SDL_Surface *sfc, sint16 x, sint16 y, uint8 color)
{
    x += grDx; y += grDy;
    grPutPixel(sfc, x, y, color);
}


void grDrawLine(SDL_Surface *sfc, sint16 x1, sint16 y1, sint16 x2, sint16 y2, uint8 color)
{
    x1 += grDx; y1 += grDy;
    x2 += grDx; y2 += grDy;

    SDL_LockSurface(sfc);

    // Bresenham's line drawing algorithm
    // Note : the code below intends to be pixel-perfect

    uint16 dx, dy, cumul, x, y;
    int xinc, yinc;

    x = x1;
    y = y1;
    dx = abs(x2 - x1);
    dy = abs(y2 - y1);

    xinc = (x2>x1 ? 1 : -1);
    yinc = (y2>y1 ? 1 : -1);

    if (dy < dx) {
        cumul = (dx + 1) >> 1;

        for (int i=0; i < dx; i++) {

            grPutPixel(sfc, x, y, color);

            x += xinc;
            cumul += dy;

            if (cumul > dx) {
                cumul -= dx;
                y += yinc;
            }
        }
    }
    else {
        cumul = (dy + 1) >> 1;

        for (int i=0; i < dy; i++) {

            grPutPixel(sfc, x, y, color);

            y += yinc;
            cumul += dx;

            if (cumul > dy) {
                cumul -= dy;
                x += xinc;
            }
        }
    }

    SDL_UnlockSurface(sfc);
}


void grDrawRect(SDL_Surface *sfc, sint16 x, sint16 y, uint16 width, uint16 height, uint8 color)
{
    x += grDx; y += grDy;

    SDL_Rect dest = { x, y, width, height };
    SDL_FillRect(sfc,
                 &dest,
                 SDL_MapRGB(sfc->format,
                            ttmPalette[color][2],  // TODO ?
                            ttmPalette[color][1],
                            ttmPalette[color][0]
                 )
    );
}


void grDrawCircle(SDL_Surface *sfc, sint16 x1, sint16 y1, uint16 width, uint16 height, uint8 fgColor, uint8 bgColor)
{
    x1 += grDx; y1 += grDy;

    // We can only draw regular circles
    if (width != height) {
        fprintf(stderr, "Warning : grDrawCircle() : unable to draw ellipse\n");
        return;
    }

    // In original data, every width is even
    if (width % 2) {
        fprintf(stderr, "Warning : grDrawCircle() : unable to process odd diameters\n");
        return;
    }

    // Bresenham's circle drawing algorithm
    // Note : the code below intends to be pixel-perfect

    SDL_LockSurface(sfc);

    uint16 r = (width >> 1) - 1;
    uint16 xc = x1 + r;
    uint16 yc = y1 + r;
    sint16 x = 0;
    sint16 y = r;
    int d = 1 - r;

    while (1) {

        grDrawHorizontalLine(sfc, xc-x, xc+x+1, yc+y+1, bgColor);
        grDrawHorizontalLine(sfc, xc-x, xc+x+1, yc-y  , bgColor);

        grDrawHorizontalLine(sfc, xc-y, xc+y+1, yc+x+1, bgColor);
        grDrawHorizontalLine(sfc, xc-y, xc+y+1, yc-x  , bgColor);

        if (y-x <= 1)
            break;

        if (d < 0)
            d += (x << 1) + 3;
        else {
            d += ((x - y) << 1) + 5;
            y--;
        }

        x++;
    }

    if (fgColor != bgColor) {

        x = 0;
        y = r;
        d = 1 - r;

        while (1) {

            grPutPixel(sfc, xc-x  , yc+y+1, fgColor);
            grPutPixel(sfc, xc+x+1, yc+y+1, fgColor);

            grPutPixel(sfc, xc-x  , yc-y  , fgColor);
            grPutPixel(sfc, xc+x+1, yc-y  , fgColor);

            grPutPixel(sfc, xc-y  , yc+x+1, fgColor);
            grPutPixel(sfc, xc+y+1, yc+x+1, fgColor);

            grPutPixel(sfc, xc-y  , yc-x  , fgColor);
            grPutPixel(sfc, xc+y+1, yc-x  , fgColor);

            if (y-x <= 1)
                break;

            if (d < 0)
                d += (x << 1) + 3;
            else {
                d += ((x - y) << 1) + 5;
                y--;
            }

            x++;
        }
    }

    SDL_UnlockSurface(sfc);
}


void grDrawSprite(SDL_Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y, uint16 spriteNo, uint16 imageNo)
{
    if (spriteNo >= ttmSlot->numSprites[imageNo]) {
        fprintf(stderr, "Warning : grDrawSprite(): less than %d sprites loaded in slot %d\n", imageNo, spriteNo);
        return;
    }

    x += grDx; y += grDy;

    SDL_Surface *srcSfc = ttmSlot->sprites[imageNo][spriteNo];
    grCaptureRecordSpriteDraw(ttmSlot, x, y, spriteNo, imageNo, srcSfc, 0);
    grCaptureRecordSurfaceDraw(sfc, ttmSlot, x, y, spriteNo, imageNo, srcSfc, 0);

    SDL_Rect dest = { x, y, 0, 0 };
    SDL_BlitSurface(srcSfc, NULL, sfc, &dest);
}


void grDrawSpriteFlip(SDL_Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y, uint16 spriteNo, uint16 imageNo)
{
    if (spriteNo >= ttmSlot->numSprites[imageNo]) {
        fprintf(stderr, "Warning : grDrawSpriteFlip(): less than %d sprites loaded in slot %d\n", imageNo, spriteNo);
        return;
    }

    x += grDx; y += grDy;

    SDL_Surface *srcSfc = ttmSlot->sprites[imageNo][spriteNo];
    grCaptureRecordSpriteDraw(ttmSlot, x, y, spriteNo, imageNo, srcSfc, 1);
    grCaptureRecordSurfaceDraw(sfc, ttmSlot, x, y, spriteNo, imageNo, srcSfc, 1);
    x += srcSfc->w - 1;

    for (int i=0; i < srcSfc->w; i++) {

        SDL_Rect src = { i, 0, 1, srcSfc->h };
        SDL_Rect dest = { x - i, y, 0, 0 };

        SDL_BlitSurface(srcSfc, &src, sfc, &dest);
    }
}


void grBlitToFramebuffer(SDL_Surface *sprite, sint16 screenX, sint16 screenY)
{
    SDL_Rect dest;

    if (sprite == NULL || grBackgroundSfc == NULL)
        return;

    dest.x = screenX;
    dest.y = screenY;
    dest.w = 0;
    dest.h = 0;
    SDL_BlitSurface(sprite, NULL, grBackgroundSfc, &dest);
}


void grCompositeToBackground(SDL_Surface *sprite, sint16 screenX, sint16 screenY)
{
    grBlitToFramebuffer(sprite, screenX, screenY);
}


void grCompositeToBackgroundFlip(SDL_Surface *sprite, sint16 screenX, sint16 screenY)
{
    SDL_Rect src;
    SDL_Rect dest;
    int i;

    if (sprite == NULL || grBackgroundSfc == NULL)
        return;

    for (i = 0; i < sprite->w; i++) {
        src.x = i;
        src.y = 0;
        src.w = 1;
        src.h = sprite->h;
        dest.x = screenX + sprite->w - 1 - i;
        dest.y = screenY;
        dest.w = 0;
        dest.h = 0;
        SDL_BlitSurface(sprite, &src, grBackgroundSfc, &dest);
    }
}


void grSaveCleanBgTiles(void)
{
}


void grClearScreen(SDL_Surface *sfc)
{
    SDL_Rect rect;

    SDL_GetClipRect(sfc, &rect);
    SDL_SetClipRect(sfc, NULL);
    SDL_FillRect(sfc, NULL, SDL_MapRGB(sfc->format, 0xa8, 0, 0xa8));
    SDL_SetClipRect(sfc, &rect);
    grCaptureResetLedger(sfc);
}


void grLoadScreen(char *strArg)
{
    if (grBackgroundSfc != NULL)
        grReleaseScreen();

    if (grSavedZonesLayer != NULL)
        grReleaseSavedLayer();

    struct TScrResource *scrResource = findScrResource(strArg);

    /* If SCR data was already freed (memory optimization), reload from extracted file */
    if (scrResource->uncompressedData == NULL) {
        char extractedPath[512];
        snprintf(extractedPath, sizeof(extractedPath), "extracted/scr/%s",
                 scrResource->resName);

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

    /* Use 8-bit indexed surface instead of 32-bit RGBA - 4x memory savings! */
    uint8 *outData = safe_malloc(width * height);  /* 1 byte per pixel instead of 4 */

    uint8 *inPtr  = scrResource->uncompressedData;
    uint8 *outPtr = outData;

    /* Expand 4-bit paletted data to 8-bit indices (not full RGBA) */
    for (int inOffset=0; inOffset < width*height/2; inOffset++) {
        *outPtr++ = (inPtr[0] & 0xf0) >> 4;  /* High nibble */
        *outPtr++ = (inPtr[0] & 0x0f);       /* Low nibble */
        inPtr++;
    }

    /* Create 8-bit indexed surface (not 32-bit RGBA) */
    grBackgroundSfc = SDL_CreateRGBSurfaceFrom((void*)outData,
                                      width, height, 8, width, 0, 0, 0, 0);
    grCaptureFindLedger(grBackgroundSfc, 1);
    grCaptureResetLedger(grBackgroundSfc);

    /* Set up the 16-color palette for this indexed surface */
    SDL_Color colors[16];
    for (int i = 0; i < 16; i++) {
        colors[i].r = ttmPalette[i][2];
        colors[i].g = ttmPalette[i][1];
        colors[i].b = ttmPalette[i][0];
        colors[i].a = 255;
    }
    SDL_SetPaletteColors(grBackgroundSfc->format->palette, colors, 0, 16);

    /* Free SCR data after converting to SDL surface - saves memory */
    if (scrResource->uncompressedData) {
        free(scrResource->uncompressedData);
        scrResource->uncompressedData = NULL;
        if (debugMode) {
            printf("Freed SCR data for %s (%u bytes)\n",
                   scrResource->resName, scrResource->uncompressedSize);
        }
    }
}


void grInitEmptyBackground()
{
    if (grBackgroundSfc != NULL)
        grReleaseScreen();

    if (grSavedZonesLayer != NULL)
        grReleaseSavedLayer();

    /* Use 8-bit indexed surface for empty background too - 4x memory savings! */
    uint8 *data = safe_malloc(640 * 480);
    memset(data, 0, 640 * 480);
    grBackgroundSfc = SDL_CreateRGBSurfaceFrom((void*)data,
                                      640, 480, 8, 640, 0, 0, 0, 0);
    grCaptureFindLedger(grBackgroundSfc, 1);
    grCaptureResetLedger(grBackgroundSfc);

    /* Set up palette for empty background */
    SDL_Color colors[16];
    for (int i = 0; i < 16; i++) {
        colors[i].r = ttmPalette[i][2];
        colors[i].g = ttmPalette[i][1];
        colors[i].b = ttmPalette[i][0];
        colors[i].a = 255;
    }
    SDL_SetPaletteColors(grBackgroundSfc->format->palette, colors, 0, 16);
}


void grReleaseBmp(struct TTtmSlot *ttmSlot, uint16 bmpSlotNo)
{
    ttmSlot->spriteGen[bmpSlotNo]++;
    ttmSlot->loadedBmp[bmpSlotNo] = NULL;
    for (int i=0; i < ttmSlot->numSprites[bmpSlotNo]; i++) {
        free(ttmSlot->sprites[bmpSlotNo][i]->pixels);
        SDL_FreeSurface(ttmSlot->sprites[bmpSlotNo][i]);
    }

    ttmSlot->numSprites[bmpSlotNo] = 0;
    ttmSlot->loadedBmpNames[bmpSlotNo] = NULL;
}


void grLoadBmp(struct TTtmSlot *ttmSlot, uint16 slotNo, char *strArg)
{
    if (ttmSlot->numSprites[slotNo])
        grReleaseBmp(ttmSlot, slotNo);

    struct TBmpResource *bmpResource = findBmpResource(strArg);
    ttmSlot->loadedBmp[slotNo] = bmpResource;
    ttmSlot->loadedBmpNames[slotNo] = bmpResource ? bmpResource->resName : strArg;

    /* If BMP data was already freed (memory optimization), reload from extracted file */
    if (bmpResource->uncompressedData == NULL) {
        char extractedPath[512];
        snprintf(extractedPath, sizeof(extractedPath), "extracted/bmp/%s",
                 bmpResource->resName);

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

        /* Use 8-bit indexed surface instead of 32-bit RGBA - 4x memory savings! */
        uint8 *outData = safe_malloc(width * height);  /* 1 byte per pixel instead of 4 */

        uint8 *outPtr = outData;

        /* Expand 4-bit paletted data to 8-bit indices (not full RGBA) */
        for (int inOffset=0; inOffset < (width*height/2); inOffset++) {
            *outPtr++ = (inPtr[0] & 0xf0) >> 4;  /* High nibble */
            *outPtr++ = (inPtr[0] & 0x0f);       /* Low nibble */
            inPtr++;
        }

        /* Create 8-bit indexed surface (not 32-bit RGBA) */
        SDL_Surface *surface = SDL_CreateRGBSurfaceFrom((void*)outData,
                                               width, height, 8, width, 0, 0, 0, 0);

        /* Set up the 16-color palette for this indexed surface */
        SDL_Color colors[16];
        int magentaIndex = -1;
        for (int i = 0; i < 16; i++) {
            colors[i].r = ttmPalette[i][2];
            colors[i].g = ttmPalette[i][1];
            colors[i].b = ttmPalette[i][0];
            colors[i].a = 255;

            /* Find magenta (0xa8, 0, 0xa8) in the palette for transparent color key */
            if (ttmPalette[i][2] == 0xa8 && ttmPalette[i][1] == 0 && ttmPalette[i][0] == 0xa8) {
                magentaIndex = i;
            }
        }
        SDL_SetPaletteColors(surface->format->palette, colors, 0, 16);

        /* Set color key to the magenta palette index for transparency */
        if (magentaIndex >= 0) {
            SDL_SetColorKey(surface, SDL_TRUE, magentaIndex);
        }
        ttmSlot->sprites[slotNo][image] = surface;
    }

    /* Free BMP data after converting to SDL surfaces - saves memory */
    if (bmpResource->uncompressedData) {
        free(bmpResource->uncompressedData);
        bmpResource->uncompressedData = NULL;
        if (debugMode) {
            printf("Freed BMP data for %s (%u bytes)\n",
                   bmpResource->resName, bmpResource->uncompressedSize);
        }
    }
}


void grFadeOut()
{
    static int fadeOutType = 0;
    SDL_Surface *sfc = SDL_GetWindowSurface(sdl_window);
    SDL_Surface *tmpSfc = grNewLayer();


    grDx = grDy = 0;

    switch (fadeOutType) {

        // Circle from center
        case 0:
            // Note: we use tmpSfc to be sure we have a 32bpp surface,
            // which is needed by grDrawCircle()
            for (int radius=20; radius <= 400; radius += 20) {
                grDrawCircle(tmpSfc, 320 - radius, 240 - radius,
                    radius << 1, radius << 1, 5, 5);
                SDL_BlitSurface(tmpSfc, NULL, sfc, &grScreenOrigin);
                eventsWaitTick(1);
                SDL_UpdateWindowSurface(sdl_window);
            }
            break;

        // Rectangle from center
        case 1:
            for (int i=1; i <= 20; i++) {
                grDrawRect(sfc, grScreenOrigin.x + 320 - i*16, grScreenOrigin.y + 240 - i*12, i*32, i*24, 5);
                eventsWaitTick(1);
                SDL_UpdateWindowSurface(sdl_window);
            }
            break;

        // Right to left
        case 2:
            for (int i=600; i >= 0; i -= 40) {
                grDrawRect(sfc, grScreenOrigin.x + i, grScreenOrigin.y, 40, 480, 5);
                eventsWaitTick(1);
                SDL_UpdateWindowSurface(sdl_window);
            }
            break;

        // Left to right
        case 3:
            for (int i=0; i < 640; i += 40) {
                grDrawRect(sfc, grScreenOrigin.x + i, grScreenOrigin.y, 40, 480, 5);
                eventsWaitTick(1);
                SDL_UpdateWindowSurface(sdl_window);
            }
            break;

        // Middle to left and right
        case 4:
            for (int i=0; i < 320; i += 20) {
                grDrawRect(sfc, grScreenOrigin.x + 320+i, grScreenOrigin.y, 20, 480, 5);
                grDrawRect(sfc, grScreenOrigin.x + 300-i, grScreenOrigin.y, 20, 480, 5);
                eventsWaitTick(1);
                SDL_UpdateWindowSurface(sdl_window);
            }
            break;
    }

    grFreeLayer(tmpSfc);

    fadeOutType = (fadeOutType + 1) % 5;
}


/*
 * Frame capture for visual regression testing
 * Saves the current window surface to a BMP file
 * Returns 0 on success, -1 on error
 */
int grCaptureFrame(const char *filename) {
    SDL_Surface *captureSurface = NULL;

    if (sdl_window == NULL) {
        fprintf(stderr, "Error: Cannot capture frame, SDL window not initialized\n");
        return -1;
    }

    SDL_Surface *windowSurface = SDL_GetWindowSurface(sdl_window);
    if (windowSurface == NULL) {
        fprintf(stderr, "Error: Cannot get window surface: %s\n", SDL_GetError());
        return -1;
    }

    if (grCaptureOverlay) {
        captureSurface = SDL_ConvertSurfaceFormat(windowSurface, SDL_PIXELFORMAT_ARGB8888, 0);
        if (captureSurface == NULL) {
            fprintf(stderr, "Error: Cannot convert capture surface: %s\n", SDL_GetError());
            return -1;
        }
        grCaptureEmbedOverlay(captureSurface);
    }
    else {
        captureSurface = windowSurface;
    }

    /* Save as BMP file */
    if (SDL_SaveBMP(captureSurface, filename) != 0) {
        fprintf(stderr, "Error: Cannot save frame to %s: %s\n", filename, SDL_GetError());
        if (captureSurface != windowSurface)
            SDL_FreeSurface(captureSurface);
        return -1;
    }

    if (debugMode) {
        printf("Captured frame to %s (%dx%d)\n", filename,
               captureSurface->w, captureSurface->h);
    }

    if (captureSurface != windowSurface)
        SDL_FreeSurface(captureSurface);

    return 0;
}


void grCaptureSetSceneLabel(const char *sceneLabel)
{
    if (sceneLabel == NULL)
        sceneLabel = "";

    strncpy(grCaptureSceneLabel, sceneLabel, sizeof(grCaptureSceneLabel) - 1);
    grCaptureSceneLabel[sizeof(grCaptureSceneLabel) - 1] = '\0';
}
