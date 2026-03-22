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

/* Conditional includes for PS1 freestanding build */
#ifndef PS1_BUILD
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#else
#include <stddef.h>
#include <psxgpu.h>  /* For RECT, LoadImage, setRECT */
#ifndef _FILE_DEFINED
#define _FILE_DEFINED
typedef struct _FILE FILE;
#endif
extern int rand(void);
extern void *malloc(size_t size);
extern void free(void *ptr);
extern int fprintf(FILE *stream, const char *format, ...);
extern int printf(const char *format, ...);
extern void *memcpy(void *dest, const void *src, size_t n);
extern int strcmp(const char *s1, const char *s2);
extern unsigned int SDL_GetTicks(void);
#define stderr ((FILE*)2)
#endif

#include "mytypes.h"
#include "utils.h"
#include "events.h"
#include "resource.h"
/* Platform-specific graphics headers */
#ifdef PS1_BUILD
#include "graphics_ps1.h"
#include "cdrom_ps1.h"
#else
#include "graphics.h"
#endif
#include "ttm.h"
#include "island.h"
#include "walk.h"
#include "bench.h"
#include "ads.h"
#ifdef PS1_BUILD
#include "ps1_debug.h"
#include "ps1_restore_pilots.h"
#endif


#define MAX_RANDOM_OPS        10
#define MAX_ADS_CHUNKS        100
#define MAX_ADS_CHUNKS_LOCAL  1

#define OP_ADD_SCENE   0
#define OP_STOP_SCENE  1
#define OP_NOP         2


struct TAdsChunk {
    struct TAdsScene scene;
    uint32 offset;
};

struct TAdsRandOp {
    int    type;
    uint16 slot;
    uint16 tag;
    uint16 numPlays;
    uint16 weight;
};


static struct TAdsChunk adsChunks[MAX_ADS_CHUNKS];
static int    numAdsChunks;

static struct TAdsChunk adsChunksLocal[MAX_ADS_CHUNKS_LOCAL];
static int    numAdsChunksLocal;

static struct TTtmSlot ttmBackgroundSlot;
static struct TTtmSlot ttmHolidaySlot;
static struct TTtmSlot *ttmSlots = NULL;  /* Malloc'd, not static array! */

static struct TTtmThread ttmBackgroundThread;
static struct TTtmThread ttmHolidayThread;
static struct TTtmThread *ttmThreads = NULL;  /* Malloc'd, not static array! */

static struct TTtmTag *adsTags;
static int    adsNumTags = 0;

static struct TAdsRandOp adsRandOps[MAX_RANDOM_OPS];
static int    adsNumRandOps    = 0;

static int    numThreads       = 0;
static int    adsStopRequested = 0;
void adsRequestStop(void) { adsStopRequested = 1; }
int ps1AdsLastPlayLaunched = 0;
char ps1AdsCurrentName[16] = "";
uint16 ps1AdsCurrentTag = 0;

static void adsStopScene(int sceneNo);

#ifdef PS1_BUILD
static const struct TPs1RestorePilot *cachedPilot = NULL;
static char cachedPilotAdsName[16] = {0};
static uint16 cachedPilotAdsTag = 0xFFFF;

static void adsCacheRestorePilotResult(const struct TPs1RestorePilot *pilot)
{
    size_t i;

    cachedPilot = pilot;
    cachedPilotAdsTag = ps1AdsCurrentTag;
    for (i = 0; i + 1 < sizeof(cachedPilotAdsName) && ps1AdsCurrentName[i] != '\0'; i++)
        cachedPilotAdsName[i] = ps1AdsCurrentName[i];
    cachedPilotAdsName[i] = '\0';
}
#endif

static void adsSetCurrentScene(char *adsName, uint16 adsTag)
{
    int i;

    for (i = 0; i < (int)(sizeof(ps1AdsCurrentName) - 1) && adsName != NULL && adsName[i] != '\0'; i++)
        ps1AdsCurrentName[i] = adsName[i];
    ps1AdsCurrentName[i] = '\0';
    ps1AdsCurrentTag = adsTag;

#ifdef PS1_BUILD
    /* Invalidate pilot cache when scene changes */
    cachedPilotAdsName[0] = '\0';
#endif
}

static int adsStringEquals(const char *a, const char *b)
{
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

static int adsPilotContainsAdsTag(const struct TPs1RestorePilot *pilot, uint16 adsTag)
{
    int i;
    if (pilot == NULL)
        return 0;
    for (i = 0; i < pilot->adsTagCount; i++) {
        if (pilot->adsTags[i] == adsTag)
            return 1;
    }
    return 0;
}

static int adsPilotNeedsDeferredBmpPriming(const struct TPs1RestorePilot *pilot)
{
    return pilot != NULL && adsStringEquals(pilot->adsName, "MARY.ADS");
}

static int adsPilotBypassesReplayPolicy(const struct TPs1RestorePilot *pilot)
{
    return pilot != NULL && adsStringEquals(pilot->adsName, "ACTIVITY.ADS");
}

static const struct TPs1RestorePilot *adsFindActiveRestorePilot(void)
{
    int i;

    /* Return cached result if name+tag haven't changed */
    if (cachedPilotAdsName[0] != '\0' &&
        cachedPilotAdsTag == ps1AdsCurrentTag &&
        adsStringEquals(cachedPilotAdsName, ps1AdsCurrentName))
        return cachedPilot;

    for (i = 0; i < PS1_RESTORE_PILOT_COUNT; i++) {
        const struct TPs1RestorePilot *pilot = &gPs1RestorePilots[i];
        if (!adsStringEquals(ps1AdsCurrentName, pilot->adsName))
            continue;
        if (adsPilotContainsAdsTag(pilot, ps1AdsCurrentTag)) {
            adsCacheRestorePilotResult(pilot);
            return pilot;
        }
    }

    /* Cache the negative result too. */
    adsCacheRestorePilotResult(NULL);
    return NULL;
}

static int adsUseRestorePilotReplayPolicy(void)
{
    const struct TPs1RestorePilot *pilot = adsFindActiveRestorePilot();

    if (pilot == NULL)
        return 0;

    /* Keep replay/handoff exceptions narrow instead of reopening replay carry
     * for every validated pilot route. */
    if (adsPilotBypassesReplayPolicy(pilot))
        return 0;

    return 1;
}


#ifdef PS1_BUILD
static void adsPrimeRestorePilotResources(const struct TPs1RestorePilot *pilot)
{
    uint16 i;
    int preloadAllBmps = 1;
    int preloadJohnwalk = 1;

    if (pilot == NULL)
        return;

    for (i = 0; i < pilot->scrCount; i++) {
        struct TScrResource *scrResource = findScrResource((char *)pilot->scrs[i]);
        if (scrResource != NULL && scrResource->uncompressedData == NULL)
            ps1_loadScrData(scrResource);
    }

    for (i = 0; i < pilot->sceneTtmCount; i++) {
        struct TTtmResource *ttmResource = findTtmResource((char *)pilot->sceneTtms[i]);
        if (ttmResource != NULL && ttmResource->uncompressedData == NULL)
            ps1_loadTtmData(ttmResource);
    }

    /* Some routes are story-valid without a live pilot but collapse when the
     * full pilot BMP set is preloaded up front. Keep the pilot pack active,
     * but do not front-load the whole sprite sheet set until those routes are
     * validated under live activation. */
    if (adsPilotNeedsDeferredBmpPriming(pilot)) {
        preloadAllBmps = 0;
        preloadJohnwalk = 0;
    }

    /* Several island routes still need JOHNWALK ready for the first composed
     * frame. Load it first when the pilot contract includes it. */
    for (i = 0; i < pilot->bmpCount; i++) {
        if (!adsStringEquals(pilot->bmps[i], "JOHNWALK.BMP"))
            continue;

        if (!preloadJohnwalk)
            break;

        {
            struct TBmpResource *bmpResource = findBmpResource((char *)pilot->bmps[i]);
            if (bmpResource != NULL && bmpResource->uncompressedData == NULL)
                ps1_loadBmpData(bmpResource);
        }
        break;
    }

    if (!preloadAllBmps)
        return;

    for (i = 0; i < pilot->bmpCount; i++) {
        if (adsStringEquals(pilot->bmps[i], "JOHNWALK.BMP"))
            continue;
        struct TBmpResource *bmpResource = findBmpResource((char *)pilot->bmps[i]);
        if (bmpResource != NULL && bmpResource->uncompressedData == NULL)
            ps1_loadBmpData(bmpResource);
    }
}
#endif

#ifdef PS1_BUILD
#define ADS_THREAD_RUNNING 1
#define ADS_THREAD_TERMINATED 2

/* Persistent debug telemetry for overlay (kept globals). */
uint16 ps1AdsDbgActiveThreads = 0;
uint16 ps1AdsDbgMini = 0;
uint16 ps1AdsDbgRunningThreads = 0;
uint16 ps1AdsDbgTerminatedThreads = 0;
uint16 ps1AdsDbgSceneSlot = 0;
uint16 ps1AdsDbgSceneTag = 0;
uint16 ps1AdsDbgReplayCount = 0;
uint16 ps1AdsDbgReplayTryFrame = 0;
uint16 ps1AdsDbgReplayDrawFrame = 0;
uint16 ps1AdsDbgMergeCarryFrame = 0;
uint16 ps1AdsDbgNoDrawThreadsFrame = 0;
uint16 ps1AdsDbgPlayedThreadsFrame = 0;
uint16 ps1AdsDbgRecordedSpritesFrame = 0;
uint16 ps1AdsDbgAddSceneCalls = 0;
uint16 ps1AdsDbgTagLookupHits = 0;
uint16 ps1AdsDbgTagLookupMisses = 0;

static struct TDrawnSprite gPrevReplayScratch[MAX_DRAWN_SPRITES];
/* One-shot carry used to bridge scene/thread handoff gaps. */
static struct TDrawnSprite gHandoffReplay[MAX_DRAWN_SPRITES];
static uint8 gHandoffReplayCount = 0;
static uint8 gHandoffReplayValid = 0;

static inline void adsDbgAddU16(uint16 *acc, uint16 add)
{
    uint32 sum;
    if (!acc) return;
    sum = (uint32)(*acc) + (uint32)add;
    *acc = (sum > 0xFFFFU ? 0xFFFFU : (uint16)sum);
}

static int adsIsActorCandidate(const struct TDrawnSprite *ds)
{
    uint32 area;
    if (!ds) return 0;
    if (ds->width >= 8 && ds->height >= 16 &&
        ds->width <= 96 && ds->height <= 140) {
        area = (uint32)ds->width * (uint32)ds->height;
        if (area >= 180 && area <= 4500)
            return 1;
    }
    return 0;
}

static int adsActorNearMatch(const struct TDrawnSprite *a, const struct TDrawnSprite *b)
{
    if (!a || !b) return 0;
    if ((unsigned)((int)a->x - (int)b->x + 18) > 36u) return 0;
    if ((unsigned)((int)a->y - (int)b->y + 18) > 36u) return 0;
    if ((unsigned)((int)a->width - (int)b->width + 12) > 24u) return 0;
    if ((unsigned)((int)a->height - (int)b->height + 12) > 24u) return 0;
    return 1;
}

static uint8 adsCaptureReplayRecords(struct TTtmThread *thread)
{
    uint8 count;
    if (!thread) return 0;
    count = thread->numDrawnSprites;
    if (count > 0) {
        int bestArea = -1;
        int haveNear = 0;
        memcpy(gPrevReplayScratch, thread->drawnSprites,
               (size_t)count * sizeof(struct TDrawnSprite));
        for (uint8 i = 0; i < count; i++) {
            const struct TDrawnSprite *ds = &thread->drawnSprites[i];
            int area;
            if (!adsIsActorCandidate(ds)) continue;
            if (thread->lastActorReplayValid &&
                !adsActorNearMatch(ds, &thread->lastActorReplay)) {
                continue;
            }
            area = (int)ds->width * (int)ds->height;
            if (area > bestArea) {
                bestArea = area;
                thread->lastActorReplay = *ds;
                haveNear = 1;
            }
        }
        if (!haveNear && !thread->lastActorReplayValid) {
            /* First-time actor discovery only. When lastActorReplayValid is
             * already true, we must NOT re-acquire blindly or fire/prop
             * sprites can hijack actor tracking. */
            for (uint8 i = 0; i < count; i++) {
                const struct TDrawnSprite *ds = &thread->drawnSprites[i];
                int area;
                if (!adsIsActorCandidate(ds)) continue;
                area = (int)ds->width * (int)ds->height;
                if (area > bestArea) {
                    bestArea = area;
                    thread->lastActorReplay = *ds;
                    haveNear = 1;
                }
            }
        }
        if (haveNear) thread->lastActorReplayValid = 1;
    }
    return count;
}

/* Preserve prior records that did not receive a nearby replacement this tick.
 * Match by screen proximity so animation frame/image changes still replace
 * the same logical actor and do not trail old frames. */
static uint8 adsMergeReplayByProximity(struct TTtmThread *thread,
                                       const struct TDrawnSprite *prevRecords,
                                       uint8 prevCount)
{
    uint8 carried = 0;
    if (!thread || !prevRecords || prevCount == 0) return 0;

    for (uint8 p = 0; p < prevCount; p++) {
        const struct TDrawnSprite *prev = &prevRecords[p];
        int matched = 0;
        /* If this prev record IS the tracked main actor, require that any
         * "replacement" also looks actor-sized.  Scenes often pack Johnny
         * AND props (fire, tools) into the same BMP so they share imageNo.
         * Without this guard, a nearby fire sprite can falsely match
         * Johnny's prev record, causing him to be "replaced" not carried. */
        int prevIsActor = (thread->lastActorReplayValid &&
                           adsActorNearMatch(prev, &thread->lastActorReplay));
        for (uint8 n = 0; n < thread->numDrawnSprites; n++) {
            const struct TDrawnSprite *cur = &thread->drawnSprites[n];

            /* Treat as same logical actor when it's near the prior position and
             * uses the same image bank with close animation frame index. */
            if (cur->imageNo == prev->imageNo &&
                (unsigned)((int)cur->spriteNo - (int)prev->spriteNo + 8) <= 16u &&
                (unsigned)((int)cur->x - (int)prev->x + 24) <= 48u &&
                (unsigned)((int)cur->y - (int)prev->y + 24) <= 48u) {
                /* Don't let a small prop/fire sprite replace the main actor. */
                if (prevIsActor && !adsIsActorCandidate(cur))
                    continue;
                matched = 1;
                break;
            }

            /* Tight fallback for static/slow props with same dimensions. */
            if ((unsigned)((int)cur->width - (int)prev->width + 4) <= 8u &&
                (unsigned)((int)cur->height - (int)prev->height + 4) <= 8u &&
                (unsigned)((int)cur->x - (int)prev->x + 8) <= 16u &&
                (unsigned)((int)cur->y - (int)prev->y + 8) <= 16u) {
                if (prevIsActor && !adsIsActorCandidate(cur))
                    continue;
                matched = 1;
                break;
            }
        }

        if (!matched && thread->numDrawnSprites < MAX_DRAWN_SPRITES) {
            /* Prevent duplicate trails: don't carry if an equivalent record
             * is already present after this frame's draws. */
            int dup = 0;
            for (uint8 n = 0; n < thread->numDrawnSprites; n++) {
                const struct TDrawnSprite *cur = &thread->drawnSprites[n];
                if (cur->imageNo == prev->imageNo &&
                    (unsigned)((int)cur->x - (int)prev->x + 6) <= 12u &&
                    (unsigned)((int)cur->y - (int)prev->y + 6) <= 12u) {
                    dup = 1;
                    break;
                }
            }
            if (dup) continue;
            thread->drawnSprites[thread->numDrawnSprites++] = *prev;
            carried++;
        }
    }

    return carried;
}

static void adsCaptureHandoffReplay(struct TTtmThread *thread)
{
    int bestArea = -1;
    if (!thread || thread->numDrawnSprites == 0) return;

    if (adsUseRestorePilotReplayPolicy()) {
        gHandoffReplayValid = 0;
        gHandoffReplayCount = 0;
        return;
    }

    /* Transition carry is actor-only to avoid propagating unrelated props. */
    for (uint8 i = 0; i < thread->numDrawnSprites; i++) {
        struct TDrawnSprite *ds = &thread->drawnSprites[i];
        if (adsIsActorCandidate(ds)) {
            int area = (int)ds->width * (int)ds->height;
            if (area > bestArea) {
                bestArea = area;
                gHandoffReplay[0] = *ds;
            }
        }
    }

    if (bestArea < 0) {
        gHandoffReplayValid = 0;
        gHandoffReplayCount = 0;
        return;
    }

    gHandoffReplayCount = 1;
    gHandoffReplayValid = 1;
}

static void adsSeedFromHandoffReplay(struct TTtmThread *thread)
{
    if (!thread || !gHandoffReplayValid || gHandoffReplayCount == 0) return;
    if (adsUseRestorePilotReplayPolicy()) return;
    if (thread->numDrawnSprites != 0) return;

    memcpy(thread->drawnSprites, gHandoffReplay,
           (size_t)gHandoffReplayCount * sizeof(struct TDrawnSprite));
    thread->numDrawnSprites = gHandoffReplayCount;
    thread->lastActorReplay = gHandoffReplay[0];
    thread->lastActorReplayValid = 1;
    for (uint8 i = 0; i < thread->numDrawnSprites; i++)
        thread->drawnSprites[i].sceneEpoch = thread->sceneEpoch;

    /* One-shot seed; avoid stale carry leaking into unrelated scenes. */
    gHandoffReplayValid = 0;
    gHandoffReplayCount = 0;
}

/* Mid-scene fail-safe: if this frame lost the main actor entirely but previous
 * frame had one, inject one record for continuity. This is one-frame recovery,
 * not persistent carry, so it avoids trail accumulation. */
static void adsRecoverMissingActor(struct TTtmThread *thread,
                                   const struct TDrawnSprite *prevRecords,
                                   uint8 prevCount)
{
    struct TDrawnSprite candidate;
    int bestArea = -1;
    uint8 haveCandidate = 0;

    if (!thread) return;

    for (uint8 i = 0; i < thread->numDrawnSprites; i++) {
        const struct TDrawnSprite *cur = &thread->drawnSprites[i];
        if (thread->lastActorReplayValid) {
            if (adsActorNearMatch(cur, &thread->lastActorReplay)) return;
        } else if (adsIsActorCandidate(cur)) {
            return;
        }
    }

    for (uint8 i = 0; i < prevCount; i++) {
        const struct TDrawnSprite *prev = &prevRecords[i];
        if (thread->lastActorReplayValid &&
            !adsActorNearMatch(prev, &thread->lastActorReplay))
            continue;
        if (adsIsActorCandidate(prev)) {
            int area = (int)prev->width * (int)prev->height;
            if (area > bestArea) {
                bestArea = area;
                candidate = *prev;
                haveCandidate = 1;
            }
        }
    }
    if (!haveCandidate && thread->lastActorReplayValid) {
        candidate = thread->lastActorReplay;
        haveCandidate = 1;
    }
    if (!haveCandidate) return;
    if (thread->numDrawnSprites >= MAX_DRAWN_SPRITES) return;

    /* Do not duplicate if an equivalent actor is already present. */
    for (uint8 i = 0; i < thread->numDrawnSprites; i++) {
        if (adsActorNearMatch(&thread->drawnSprites[i], &candidate)) return;
    }

    candidate.sceneEpoch = thread->sceneEpoch;
    thread->drawnSprites[thread->numDrawnSprites++] = candidate;
    thread->lastActorReplay = candidate;
    thread->lastActorReplayValid = 1;
}
#endif

static void adsReapTerminatedThreads(void)
{
    for (int i = 0; i < MAX_TTM_THREADS; i++) {
        if (ttmThreads[i].isRunning == 2) {
            adsStopScene(i);
        }
    }
}

static void adsLoad(uint8 *data, uint32 dataSize, uint16 numTags, uint16 tag, uint32 *tagOffset)
{
    uint32 offset = 0;
    uint16 args[10];
    int bookmarkingChunks = 0;
    int bookmarkingIfNotRunnings = 0;

    numAdsChunks      = 0;
    numAdsChunksLocal = 0;
    *tagOffset        = 0;
    adsNumTags        = 0;
    adsTags           = safe_malloc(numTags * sizeof(struct TTtmTag));


    while (offset < dataSize) {

        uint16 opcode = peekUint16(data, &offset);

        switch (opcode) {

            case 0x1350:     // IF_LASTPLAYED

                if (bookmarkingChunks) {
                    bookmarkingIfNotRunnings = 0;
                    peekUint16Block(data, &offset, args, 2);
                    adsChunks[numAdsChunks].scene.slot = args[0];
                    adsChunks[numAdsChunks].scene.tag  = args[1];
                    adsChunks[numAdsChunks].offset     = offset;
                    numAdsChunks++;
                }
                else {
                    offset += 2<<1;
                }

                break;

            case 0x1360:     // IF_NOT_RUNNING

                // We only bookmark the IF_NOT_RUNNINGs
                // preceding the first IF_LAST_PLAYED or IF_IS_RUNNING

                if (bookmarkingChunks && bookmarkingIfNotRunnings) {
                    peekUint16Block(data, &offset, args, 2);
                    adsChunks[numAdsChunks].scene.slot = args[0];
                    adsChunks[numAdsChunks].scene.tag  = args[1];
                    adsChunks[numAdsChunks].offset     = offset;
                    numAdsChunks++;
                }
                else {
                    offset += 2<<1;
                }

                break;

            case 0x1370:     // IF_IS_RUNNING
                bookmarkingIfNotRunnings = 0;
                offset += 2<<1;
                break;

            case 0x1070: offset += 2<<1; break;
            case 0x1330: offset += 2<<1; break;
            case 0x1420: offset += 0<<1; break;
            case 0x1430: offset += 0<<1; break; /* OR */
            case 0x1510: offset += 0<<1; break;
            case 0x1520: offset += 5<<1; break;
            case 0x2005: offset += 4<<1; break;
            case 0x2010: offset += 3<<1; break;
            case 0x2014: offset += 0<<1; break;
            case 0x3010: offset += 0<<1; break;
            case 0x3020: offset += 1<<1; break;
            case 0x30ff: offset += 0<<1; break;
            case 0x4000: offset += 3<<1; break;
            case 0xf010: offset += 0<<1; break;
            case 0xf200: offset += 1<<1; break;
            case 0xffff: offset += 0<<1; break;
            case 0xfff0: offset += 0<<1; break;

            default:
                adsTags[adsNumTags].id     = opcode;
                adsTags[adsNumTags].offset = offset;
                adsNumTags++;

                if (opcode == tag) {
                    *tagOffset = offset;
                    bookmarkingChunks = 1;
                    bookmarkingIfNotRunnings = 1;
                }
                else {
                    bookmarkingChunks = 0;
                    bookmarkingIfNotRunnings = 0;
                }

                break;
        }
    }

    if (adsNumTags != numTags)
        debugMsg("Warning : didn't find every tag in ADS data");

    if (*tagOffset == 0)
        debugMsg("Warning : ADS tag #%d not found, starting from offset 0", tag);
}


static void adsReleaseAds()
{
    free(adsTags);
}


static uint32 adsFindTag(uint16 reqdTag)
{
    uint32 result = 0;
    int i = 0;

    while (result == 0 && i < adsNumTags) {

        if (adsTags[i].id == reqdTag)
            result = adsTags[i].offset;
        else
            i++;
    }

    if (result == 0)
        fprintf(stderr, "Warning : ADS tag #%d not found, returning offset 0000\n", reqdTag);

    return result;
}


static void adsAddScene(uint16 ttmSlotNo, uint16 ttmTag, uint16 arg3)
{
    /* Reclaim any terminated threads before attempting to add a new one. */
    adsReapTerminatedThreads();

#ifdef PS1_BUILD
    if (grPs1TelemetryEnabled) {
        adsDbgAddU16(&ps1AdsDbgAddSceneCalls, 1);
        ps1AdsDbgSceneSlot = ttmSlotNo;
        ps1AdsDbgSceneTag = ttmTag;
    }
    {
        uint16 probeOrdinal = ps1AdsDbgAddSceneCalls;
        int probeX = 400 + ((probeOrdinal > 0 ? (probeOrdinal - 1) : 0) & 0x3) * 40;
        grDebugOverlayBox(probeX, 20, 24, 24, 0x7FFF);
    }
    grDebugOverlayBox(580, 388, 32, 32, 0x7C1F);
#endif

    for (int i=0; i < MAX_TTM_THREADS; i++) {

        struct TTtmThread *ttmThread = &ttmThreads[i];

        if (ttmThread->isRunning == 1) {

            if (ttmThread->sceneSlot == ttmSlotNo && ttmThread->sceneTag == ttmTag) {
                debugMsg("(%d,%d) thread is already running - didn't add extra one\n", ttmSlotNo, ttmTag);
                return;
            }
        }
    }

    int i=0;

    while (i < MAX_TTM_THREADS && ttmThreads[i].isRunning)
        i++;

    /* Guard against thread-pool overflow: avoid writing past ttmThreads[]. */
    if (i >= MAX_TTM_THREADS) {
        fatalError("ADS thread overflow: add scene (%d,%d) with MAX_TTM_THREADS=%d",
                   ttmSlotNo, ttmTag, MAX_TTM_THREADS);
#ifdef PS1_BUILD
        grPs1StatThreadDrop();
#endif
    }

    struct TTtmThread *ttmThread = &ttmThreads[i];
    uint32 startIp;

    ttmThread->ttmSlot         = &ttmSlots[ttmSlotNo];
    ttmThread->isRunning       = 1;
    ttmThread->sceneSlot       = ttmSlotNo;
    ttmThread->sceneTag        = ttmTag;
    ttmThread->sceneTimer      = 0;
    ttmThread->sceneIterations = 0;
    ttmThread->delay           = 4;
    ttmThread->timer           = 0;
    ttmThread->nextGotoOffset  = 0;
    ttmThread->selectedBmpSlot = 0;
    ttmThread->fgColor         = 0x0f;
    ttmThread->bgColor         = 0x0f;
    ttmThread->currentRegionId = 0;
#ifdef PS1_BUILD
    ttmThread->sceneEpoch++;
    ttmThread->numDrawnSprites = 0;
    ttmThread->replayWriteCursor = 0;
    ttmThread->lastActorReplayValid = 0;
    adsSeedFromHandoffReplay(ttmThread);
#endif

    if (ttmSlotNo) {
        struct TTtmSlot *slot = &ttmSlots[ttmSlotNo];
        if (slot->numTags > 0 && slot->tags != NULL && slot->tags[0].id == ttmTag)
            ttmThread->ip = 0;
        else
            ttmThread->ip = ttmFindTag(slot, ttmTag);
    } else
        ttmThread->ip = 0;
    startIp = ttmThread->ip;

    if (((short)arg3) < 0) {
        ttmThread->sceneTimer = -((short)arg3);
    }
    else if (((short)arg3) > 0) {
        ttmThread->sceneIterations = arg3 - 1;
    }

    ttmThread->ttmLayer = grNewLayer();

#ifdef PS1_BUILD
    if (ttmThread->ttmSlot != NULL && ttmThread->ttmSlot->data != NULL)
        grDebugOverlayBox(520, 388, 12, 12, 0x03E0);
    else
        grDebugOverlayBox(520, 388, 12, 12, 0x001F);

    if (ttmThread->ttmSlot != NULL && ttmThread->ttmSlot->tags != NULL)
        grDebugOverlayBox(536, 388, 12, 12, 0x03E0);
    else
        grDebugOverlayBox(536, 388, 12, 12, 0x001F);

    if (ttmSlotNo == 0 || startIp != 0)
        grDebugOverlayBox(552, 388, 12, 12, 0x7FE0);
    else
        grDebugOverlayBox(552, 388, 12, 12, 0x7C00);
#endif

    if (numThreads < MAX_TTM_THREADS)
        numThreads++;
}


static void adsStopScene(int sceneNo)
{
#ifdef PS1_BUILD
    adsCaptureHandoffReplay(&ttmThreads[sceneNo]);
#endif
    grFreeLayer(ttmThreads[sceneNo].ttmLayer);
    ttmThreads[sceneNo].isRunning = 0;
#ifdef PS1_BUILD
    ttmThreads[sceneNo].numDrawnSprites = 0;
    ttmThreads[sceneNo].replayWriteCursor = 0;
    ttmThreads[sceneNo].lastActorReplayValid = 0;
#endif
    if (numThreads > 0)
        numThreads--;
}


static void adsStopSceneByTtmTag(uint16 ttmSlotNo, uint16 ttmTag)
{
    struct TTtmThread *ttmThread;

    for (int i=0; i < MAX_TTM_THREADS; i++) {

        ttmThread = &ttmThreads[i];

        if (ttmThread->isRunning) {

            if (ttmThread->sceneSlot == ttmSlotNo && ttmThread->sceneTag == ttmTag)
                adsStopScene(i);
        }
    }
}


static int isSceneRunning(uint16 ttmSlotNo, uint16 ttmTag)
{
    for (int i=0; i < MAX_TTM_THREADS; i++) {

        struct TTtmThread thread = ttmThreads[i];

        if (    thread.isRunning == 1
             && thread.sceneSlot == ttmSlotNo
             && thread.sceneTag  == ttmTag    ) {
            return 1;
        }
    }

    return 0;
}


static struct TAdsRandOp *adsRandomPickOp()
{
    int totalWeight = 0;
    int partialWeight = 0;
    int res;


    // Pick in a list of weighted elements

    for (int i=0; i < adsNumRandOps; i++)
        totalWeight += adsRandOps[i].weight;

    int a = rand() % totalWeight;

    for (res=0; res < adsNumRandOps; res++) {
        partialWeight += adsRandOps[res].weight;
        if (a < partialWeight)
            break;
    }

    return &adsRandOps[res];
}


static void adsRandomStart()
{
    adsNumRandOps = 0;
}


static void adsRandomAddScene(uint16 ttmSlotNo, uint16 ttmTag, uint16 numPlays,
                              uint16 weight)
{
    adsRandOps[adsNumRandOps].type      = OP_ADD_SCENE;
    adsRandOps[adsNumRandOps].slot      = ttmSlotNo;
    adsRandOps[adsNumRandOps].tag       = ttmTag;
    adsRandOps[adsNumRandOps].numPlays  = numPlays;
    adsRandOps[adsNumRandOps].weight    = weight;
    adsNumRandOps++;
}


static void adsRandomStopSceneByTtmTag(uint16 ttmSlotNo, uint16 ttmTag,
                                       uint16 weight)
{
    adsRandOps[adsNumRandOps].type      = OP_STOP_SCENE;
    adsRandOps[adsNumRandOps].slot      = ttmSlotNo;
    adsRandOps[adsNumRandOps].tag       = ttmTag;
    adsRandOps[adsNumRandOps].numPlays  = 0;
    adsRandOps[adsNumRandOps].weight    = weight;
    adsNumRandOps++;
}


static void adsRandomNop(uint16 weight)
{
    adsRandOps[adsNumRandOps].type      = OP_NOP;
    adsRandOps[adsNumRandOps].slot      = 0;
    adsRandOps[adsNumRandOps].tag       = 0;
    adsRandOps[adsNumRandOps].numPlays  = 0;
    adsRandOps[adsNumRandOps].weight    = weight;
    adsNumRandOps++;
}


static void adsRandomEnd()
{
    if (adsNumRandOps) {

       struct TAdsRandOp *op = adsRandomPickOp();

       switch (op->type) {

           case OP_ADD_SCENE:
               debugMsg("RANDOM: chose ADD_SCENE %d %d", op->slot, op->tag);
               adsAddScene(op->slot, op->tag, op->numPlays);
               break;

           case OP_STOP_SCENE:
               debugMsg("RANDOM: chose STOP_SCENE %d %d", op->slot, op->tag);
               adsStopSceneByTtmTag(op->slot, op->tag);
               break;

           default:
               debugMsg("RANDOM: chose NOP");
               break;
       }
    }
    else {
        debugMsg("RANDOM : no operation to choose from");
    }
}

#ifdef PS1_BUILD
static void adsDrawBuildingThreadProbes(void)
{
    int sawTag1 = 0;
    int sawTag16 = 0;
    int tag16HasSprites = 0;

    if (strcmp(ps1AdsCurrentName, "BUILDING.ADS") != 0)
        return;

    for (int i = 0; i < MAX_TTM_THREADS; i++) {
        if (ttmThreads[i].isRunning != ADS_THREAD_RUNNING)
            continue;
        if (ttmThreads[i].sceneSlot != 1)
            continue;

        if (ttmThreads[i].sceneTag == 1)
            sawTag1 = 1;
        if (ttmThreads[i].sceneTag == 16) {
            sawTag16 = 1;
            if (ttmThreads[i].numDrawnSprites > 0)
                tag16HasSprites = 1;
        }
    }

    if (sawTag1)
        grDebugOverlayBox(320, 360, 40, 40, 0x7FFF);
    if (sawTag16)
        grDebugOverlayBox(376, 360, 40, 40, 0x03E0);
    if (tag16HasSprites)
        grDebugOverlayBox(432, 360, 40, 40, 0x03FF);
}
#endif


/* Initialize TTM slots and runtime thread state for ADS playback. */
void adsInit()
{
    /* Allocate TTM slots/threads dynamically to reduce BSS size */
    if (ttmSlots == NULL) {
        ttmSlots = (struct TTtmSlot*)malloc(MAX_TTM_SLOTS * sizeof(struct TTtmSlot));
        if (!ttmSlots) {
            if (debugMode) {
                printf("ERROR: Failed to allocate TTM slots\n");
            }
            return;
        }
    }

    if (ttmThreads == NULL) {
        ttmThreads = (struct TTtmThread*)malloc(MAX_TTM_THREADS * sizeof(struct TTtmThread));
        if (!ttmThreads) {
            if (debugMode) {
                printf("ERROR: Failed to allocate TTM threads\n");
            }
            return;
        }
    }

    for (int i=0; i < MAX_TTM_SLOTS; i++)
        ttmInitSlot(&ttmSlots[i]);

    for (int i=0; i < MAX_TTM_THREADS; i++) {
        ttmThreads[i].isRunning = 0;
        ttmThreads[i].timer     = 0;
#ifdef PS1_BUILD
        ttmThreads[i].numDrawnSprites = 0;
        ttmThreads[i].replayWriteCursor = 0;
        ttmThreads[i].sceneEpoch = 0;
        ttmThreads[i].lastActorReplayValid = 0;
#endif
    }
#ifdef PS1_BUILD
    ps1AdsDbgActiveThreads = 0;
    ps1AdsDbgRunningThreads = 0;
    ps1AdsDbgTerminatedThreads = 0;
    ps1AdsDbgSceneSlot = 0;
    ps1AdsDbgSceneTag = 0;
    ps1AdsDbgReplayCount = 0;
    ps1AdsDbgReplayTryFrame = 0;
    ps1AdsDbgReplayDrawFrame = 0;
    ps1AdsDbgMergeCarryFrame = 0;
    ps1AdsDbgNoDrawThreadsFrame = 0;
    ps1AdsDbgPlayedThreadsFrame = 0;
    ps1AdsDbgRecordedSpritesFrame = 0;
    ps1AdsDbgAddSceneCalls = 0;
    ps1AdsDbgTagLookupHits = 0;
    ps1AdsDbgTagLookupMisses = 0;
    gHandoffReplayCount = 0;
    gHandoffReplayValid = 0;
#endif

    grUpdateDelay = 0;
    ttmBackgroundThread.isRunning = 0;
    ttmHolidayThread.isRunning    = 0;
    numThreads = 0;
    adsStopRequested = 0;
}


void adsPlaySingleTtm(char *ttmName)
{
    adsInit();
    ttmLoadTtm(ttmSlots, ttmName);
#ifdef PS1_BUILD
    if (ttmSlots[0].data == NULL) return;
#endif
    adsAddScene(0,0,0);
    ttmThreads[0].ip = 0;

    while (ttmThreads[0].ip < ttmSlots[0].dataSize) {
#ifdef PS1_BUILD
        /* Reset GPU frame state and restore clean background */
        grBeginFrame();
        grRestoreBgTiles();
        grCurrentThread = &ttmThreads[0];
#endif
        ttmPlay(ttmThreads);
#ifdef PS1_BUILD
        grCurrentThread = NULL;
#endif
        ttmThreads[0].isRunning = 1;
        grUpdateDisplay(NULL, ttmThreads, NULL);
        grUpdateDelay = ttmThreads[0].delay;
    }

    adsStopScene(0);
    ttmResetSlot(&ttmSlots[0]);
}


static void adsPlayChunk(uint8 *data, uint32 dataSize, uint32 offset)
{
    uint16 opcode;
    uint16 args[10];
    int inRandBlock          = 0;
    int inOrBlock            = 0;
    int inSkipBlock          = 0;
    int inIfLastplayedLocal  = 0;
    int continueLoop         = 1;


    while (continueLoop && offset < dataSize) {

        opcode = peekUint16(data, &offset);

        switch (opcode) {

            case 0x1070:
                // Inside an IF_LASTPLAYED chunk, local IF_LASTPLAYED
                // which overrides the global IF_LASTPLAYEDs.
                peekUint16Block(data, &offset, args, 2);
                debugMsg("IF_LASTPLAYED_LOCAL");
                inIfLastplayedLocal = 1;
                adsChunksLocal[numAdsChunksLocal].scene.slot = args[0];
                adsChunksLocal[numAdsChunksLocal].scene.tag  = args[1];
                adsChunksLocal[numAdsChunksLocal].offset     = offset;
                numAdsChunksLocal++;
                break;

            case 0x1330:
                // Always just before a call to ADD_SCENE with same (ttm,tag)
                // references tags which init commands : LOAD_IMAGE LOAD_SCREEN etc.
                //   - one exception: FISHING.ADS tag 3
                //   - seems to be a synonym of "IF_NOT_RUNNING"
                //   - if so, our implementation works fine anyway by ignoring this one...
                peekUint16Block(data, &offset, args, 2);
                debugMsg("IF_UNKNOWN_1 %d %d", args[0], args[1]);
                break;

            case 0x1350:
                peekUint16Block(data, &offset, args, 2);
                debugMsg("IF_LASTPLAYED %d %d", args[0], args[1]);

                if (!inOrBlock)
                    continueLoop = 0;

                inOrBlock = 0;

                break;

            case 0x1360:
                peekUint16Block(data, &offset, args, 2);
                debugMsg("IF_NOT_RUNNING %d %d", args[0], args[1]);
                if (isSceneRunning(args[0], args[1]))
                    inSkipBlock = 1;
                break;

            case 0x1370:
                peekUint16Block(data, &offset, args, 2);
                debugMsg("IF_IS_RUNNING %d %d", args[0], args[1]);
                inSkipBlock = !isSceneRunning(args[0], args[1]);
                break;

            case 0x1420:
                debugMsg("AND");
                break;

            case 0x1430:
                debugMsg("OR");
                inOrBlock = 1;
                break;

            case 0x1510:
                /* PLAY_SCENE acts as a closing marker for several statement
                 * block forms in the authored ADS scripts. */
                debugMsg("PLAY_SCENE");
                if (inSkipBlock)
                    inSkipBlock = 0;
                else
                    continueLoop = 0;
                break;

            case 0x1520:
                // Only in ACTIVITY.ADS tag 7, after IF_LASTPLAYED_LOCAL
                peekUint16Block(data, &offset, args, 5);
                debugMsg("ADD_SCENE_LOCAL");

                if (inIfLastplayedLocal) {
                    // First pass : the scene was queued by IF_LASTPLAYED_LOCAL,
                    // nothing more to do for now
                    inIfLastplayedLocal = 0;
                }
                else {
                    // Second pass (we were called directly from the scheduler)
                    // --> we launch the execution of the scene
                    adsAddScene(args[1],args[2],args[3]);
                }

                break;

            case 0x2005:
                peekUint16Block(data, &offset, args, 4);
                debugMsg("ADD_SCENE %d %d %d %d", args[0], args[1], args[2], args[3]);

                if (!inSkipBlock) {
                    if (inRandBlock)
                        adsRandomAddScene(args[0],args[1],args[2], args[3]);
                    else
                        adsAddScene(args[0],args[1],args[2]);
                }

                break;

            case 0x2010:
                peekUint16Block(data, &offset, args, 3);
                debugMsg("STOP_SCENE %d %d %d", args[0], args[1], args[2]);

                if (!inSkipBlock) {
                    if (inRandBlock)
                        adsRandomStopSceneByTtmTag(args[0], args[1], args[2]);
                    else
                        adsStopSceneByTtmTag(args[0], args[1]);
                }

                break;

            case 0x3010:
                debugMsg("RANDOM_START");
                adsRandomStart();
                inRandBlock = 1;
                break;

            case 0x3020:
                peekUint16Block(data, &offset, args, 1);
                debugMsg("NOP");
                if (inRandBlock)
                    adsRandomNop(args[0]);
                break;

            case 0x30ff:
                debugMsg("RANDOM_END");
                adsRandomEnd();
                inRandBlock = 0;
                break;

            case 0x4000:
                peekUint16Block(data, &offset, args, 3);
                debugMsg("UNKNOWN_6");    // only in BUILDING.ADS tag 7
                break;

            case 0xf010:
                debugMsg("FADE_OUT");
                break;

            case 0xf200:
                peekUint16Block(data, &offset, args, 1);
                debugMsg("GOSUB_TAG %d", args[0]);    // ex UNKNOWN_8
                /* The verified content only uses this as a single nested jump
                 * target, so direct chunk playback is sufficient here. */
                adsPlayChunk(data, dataSize, adsFindTag(args[0]));
                break;

            case 0xffff:
                debugMsg("END");

                if (inSkipBlock)
                    inSkipBlock = 0;
                else
                    adsStopRequested = 1;

                break;

            case 0xfff0:
                debugMsg("END_IF");
                break;

            default :
                debugMsg(":TAG %d\n", opcode);
                break;

        }
    }
}


static void adsPlayTriggeredChunks(uint8 *data, uint32 dataSize, uint16 ttmSlotNo, uint16 ttmTag)
{
    int handledLocal = 0;

    /* First handle any queued local trigger chunks. */

    if (numAdsChunksLocal) {
        for (int i=0; i < numAdsChunksLocal; i++)
            if (adsChunksLocal[i].scene.slot == ttmSlotNo && adsChunksLocal[i].scene.tag == ttmTag) {
                adsPlayChunk(data, dataSize, adsChunksLocal[i].offset);
                handledLocal = 1;
            }
    }

    /* Then process the general trigger list. A non-empty local queue should
     * not suppress unrelated IF_LASTPLAYED chains; only a matching local
     * trigger takes precedence for this ended scene. */
    if (!handledLocal) {
        // Note : in a few rare cases (eg BUILDING.ADS tag #2), the ADS script
        // contains several 'IF_LASTPLAYED' commands for one given scene.

        for (int i=0; i < numAdsChunks; i++)
            if (adsChunks[i].scene.slot == ttmSlotNo && adsChunks[i].scene.tag == ttmTag)
                adsPlayChunk(data, dataSize, adsChunks[i].offset);
    }
}


void adsPlay(char *adsName, uint16 adsTag)
{
    uint32 offset;
    uint8  *data;
    uint32 dataSize;

    struct TAdsResource *adsResource = findAdsResource(adsName);

#ifdef PS1_BUILD
    adsSetCurrentScene(adsName, adsTag);
    printf("[ADS] request ads=%s tag=%u\n", adsName ? adsName : "(null)", adsTag);
#endif

#ifdef PS1_BUILD
    if (adsResource == NULL) {
        printf("[ADS] missing resource ads=%s tag=%u\n", adsName ? adsName : "(null)", adsTag);
        return;  /* Resource not found - skip scene silently */
    }
#endif

    debugMsg("\n\n========== Playing ADS: %s:%d ==========\n", adsResource->resName, adsTag);

    /* ADS lazy loading: Load ADS data on demand from extracted file if not already loaded */
    if (adsResource->uncompressedData == NULL) {
#ifdef PS1_BUILD
        /* PS1: Load from pre-extracted ADS file on CD */
        ps1_loadAdsData(adsResource);
        if (adsResource->uncompressedData == NULL) {
            printf("[ADS] load failed ads=%s tag=%u\n", adsResource->resName, adsTag);
            return;  /* ADS data load failed - skip scene */
        }
#else
        char extractedPath[512];
        snprintf(extractedPath, sizeof(extractedPath), "extracted/ads/%s",
                 adsResource->resName);

        FILE *f = fopen(extractedPath, "rb");
        if (f) {
            adsResource->uncompressedData = safe_malloc(adsResource->uncompressedSize);
            if (fread(adsResource->uncompressedData, 1, adsResource->uncompressedSize, f) !=
                adsResource->uncompressedSize) {
                fatalError("Failed to load ADS data from extracted file");
            }
            fclose(f);
            if (debugMode) {
                printf("Loaded ADS data for %s from disk (%u bytes)\n",
                       adsResource->resName, adsResource->uncompressedSize);
            }
        } else {
            fatalError("ADS data not loaded and extracted file not found - cannot load %s", adsName);
        }
#endif
    }

#ifdef PS1_BUILD
    /* Keep pilot resource priming tied to every scene launch, not only the
     * first ADS bytecode load. Some routes revisit a resident ADS and still
     * need the scene-scoped contract re-established deterministically. */
    adsPrimeRestorePilotResources(adsFindActiveRestorePilot());
#endif

    /* Pin ADS resource to prevent eviction while in use */
    pinResource(adsResource, adsResource->uncompressedSize, "ADS");

    /* Check memory budget and potentially evict unused resources */
    checkMemoryBudget();

    data = adsResource->uncompressedData;
    dataSize = adsResource->uncompressedSize;

    for (int i=0; i < adsResource->numRes; i++)
        ttmLoadTtm(&ttmSlots[adsResource->res[i].id], adsResource->res[i].name);

#ifdef PS1_BUILD
    /* If any TTM failed to load, skip this scene gracefully */
    {
        int loadOk = 1;
        for (int i=0; i < adsResource->numRes; i++) {
            if (ttmSlots[adsResource->res[i].id].data == NULL) {
                loadOk = 0;
                break;
            }
        }
        if (!loadOk) {
            printf("[ADS] ttm load failed ads=%s tag=%u\n", adsResource->resName, adsTag);
            for (int i=0; i < adsResource->numRes; i++)
                ttmResetSlot(&ttmSlots[adsResource->res[i].id]);
            unpinResource(adsResource, "ADS");
            return;
        }
    }
#endif

    adsLoad(data, dataSize, adsResource->numTags, adsTag, &offset);

    adsStopRequested = 0;
    ps1AdsLastPlayLaunched = 0;
    grUpdateDelay = 0;

    // Play the first ADS chunk of the sequence
    adsPlayChunk(data, dataSize, offset);

#ifdef PS1_BUILD
    /* Recovery: some ADS control paths can parse without launching a scene
     * (no ADD_SCENE emitted), which leaves PS1 on a static background.
     * If that happens, retry one bookmarked chunk instead of idling. */
    if (numThreads == 0 && !adsStopRequested && numAdsChunks > 0) {
        int pick = rand() % numAdsChunks;
        printf("[ADS] initial launch empty; retry chunk=%d/%d ads=%s tag=%u\n",
               pick, numAdsChunks, adsResource->resName, adsTag);
        adsPlayChunk(data, dataSize, adsChunks[pick].offset);
    }
#endif
    if (numThreads > 0) {
        ps1AdsLastPlayLaunched = 1;
    } else {
        printf("[ADS] no threads ads=%s tag=%u numAdsChunks=%d stop=%d\n",
               adsResource->resName, adsTag, numAdsChunks, adsStopRequested);
    }

    // Main ADS loop
#ifdef PS1_BUILD
    grEnsureCleanBgTiles();
#endif
    while (numThreads) {
        /* Self-heal thread-count desync: trust actual running threads over numThreads counter. */
        {
            int activeRunning = 0;
            int firstIdx = -1;
            int runningCount = 0;
            int terminatedCount = 0;
            for (int i = 0; i < MAX_TTM_THREADS; i++) {
                if (ttmThreads[i].isRunning == ADS_THREAD_RUNNING ||
                    ttmThreads[i].isRunning == ADS_THREAD_TERMINATED) {
                    activeRunning++;
                    if (firstIdx < 0) firstIdx = i;
                    if (ttmThreads[i].isRunning == ADS_THREAD_RUNNING) runningCount++;
                    else terminatedCount++;
                }
            }
            if (activeRunning == 0) {
                numThreads = 0;
                break;
            }
            numThreads = activeRunning;
#ifdef PS1_BUILD
            if (grPs1TelemetryEnabled) {
                ps1AdsDbgActiveThreads = (uint16)activeRunning;
                ps1AdsDbgRunningThreads = (uint16)runningCount;
                ps1AdsDbgTerminatedThreads = (uint16)terminatedCount;
                if (firstIdx >= 0) {
                    ps1AdsDbgSceneSlot = ttmThreads[firstIdx].sceneSlot;
                    ps1AdsDbgSceneTag  = ttmThreads[firstIdx].sceneTag;
                    ps1AdsDbgReplayCount = ttmThreads[firstIdx].numDrawnSprites;
                } else {
                    ps1AdsDbgSceneSlot = 0;
                    ps1AdsDbgSceneTag  = 0;
                    ps1AdsDbgReplayCount = 0;
                }
                ps1AdsDbgReplayTryFrame = 0;
                ps1AdsDbgReplayDrawFrame = 0;
                ps1AdsDbgMergeCarryFrame = 0;
                ps1AdsDbgNoDrawThreadsFrame = 0;
                ps1AdsDbgPlayedThreadsFrame = 0;
                ps1AdsDbgRecordedSpritesFrame = 0;
            }
#endif
        }

#ifdef PS1_BUILD
        grBeginFrame();
        grRestoreBgTiles();
#endif

        if (ttmBackgroundThread.isRunning && ttmBackgroundThread.timer == 0) {
            debugMsg("    ------> Animate bg");
            ttmBackgroundThread.timer = ttmBackgroundThread.delay;
            islandAnimate(&ttmBackgroundThread);
        }
#ifdef PS1_BUILD
        else if (ttmBackgroundThread.isRunning) {
            islandRedrawWave(&ttmBackgroundThread);
        }
#endif

        for (int i=0; i < MAX_TTM_THREADS; i++) {

            // Call ttmPlay() for each thread which timer reaches 0
            if (ttmThreads[i].isRunning == ADS_THREAD_RUNNING && ttmThreads[i].timer == 0) {
                debugMsg("    ------> Thread #%d", i);
                ttmThreads[i].timer = ttmThreads[i].delay;
#ifdef PS1_BUILD
                uint8 prevCount = adsCaptureReplayRecords(&ttmThreads[i]);
                if (grPs1TelemetryEnabled)
                    adsDbgAddU16(&ps1AdsDbgReplayTryFrame, prevCount);
                ttmThreads[i].numDrawnSprites = 0;
                grDx = 0;
                grDy = 0;
                grCurrentThread = &ttmThreads[i];
                if (grPs1TelemetryEnabled)
                    adsDbgAddU16(&ps1AdsDbgPlayedThreadsFrame, 1);
#endif
                ttmPlay(&ttmThreads[i]);
#ifdef PS1_BUILD
                grCurrentThread = NULL;
                if (ttmThreads[i].isRunning == ADS_THREAD_TERMINATED) {
                    ttmThreads[i].timer = 0;
                } else if (!adsUseRestorePilotReplayPolicy()) {
                    uint8 carried = adsMergeReplayByProximity(&ttmThreads[i],
                                                              gPrevReplayScratch,
                                                              prevCount);
                    adsRecoverMissingActor(&ttmThreads[i], gPrevReplayScratch, prevCount);
                    if (grPs1TelemetryEnabled)
                        adsDbgAddU16(&ps1AdsDbgMergeCarryFrame, carried);
                }
                if (grPs1TelemetryEnabled) {
                    if (ttmThreads[i].numDrawnSprites == 0)
                        adsDbgAddU16(&ps1AdsDbgNoDrawThreadsFrame, 1);
                    adsDbgAddU16(&ps1AdsDbgRecordedSpritesFrame, ttmThreads[i].numDrawnSprites);
                }
#endif
            }
#ifdef PS1_BUILD
            else if (ttmThreads[i].isRunning == ADS_THREAD_RUNNING ||
                     ttmThreads[i].isRunning == ADS_THREAD_TERMINATED) {
                /* Replay via GPU path */
                for (int j = 0; j < ttmThreads[i].numDrawnSprites; j++) {
                    if (grPs1TelemetryEnabled) {
                        adsDbgAddU16(&ps1AdsDbgReplayTryFrame, 1);
                        adsDbgAddU16(&ps1AdsDbgReplayDrawFrame, 1);
                    }
                    grReplaySprite(&ttmThreads[i].drawnSprites[j]);
                }
            }
#endif
        }

#ifdef PS1_BUILD
        adsDrawBuildingThreadProbes();
#endif

#ifndef PS1_BUILD
        if (debugMode) {

            debugMsg("\n  +------ THREADS: %d -------", numThreads);

            if (ttmBackgroundThread.isRunning) {
                    debugMsg("  |");
                    debugMsg("  | #bg:");
                    debugMsg("  |   delay........... %d" , ttmBackgroundThread.delay    );
                    debugMsg("  |   timer........... %d" , ttmBackgroundThread.timer    );
            }

            for (int i=0; i < MAX_TTM_THREADS; i++) {
                if (ttmThreads[i].isRunning) {
                    debugMsg("  |");
                    debugMsg("  | #%d: (%d,%d)", i, ttmThreads[i].sceneSlot, ttmThreads[i].sceneTag);
                    debugMsg("  |   sceneTimer...... %d" , ttmThreads[i].sceneTimer     );
                    debugMsg("  |   isRunning....... %d" , ttmThreads[i].isRunning      );
                    debugMsg("  |   offset.......... %ld", ttmThreads[i].ip             );
                    debugMsg("  |   nextGotoOffset.. %ld", ttmThreads[i].nextGotoOffset );
                    debugMsg("  |   delay........... %d" , ttmThreads[i].delay          );
                    debugMsg("  |   timer........... %d" , ttmThreads[i].timer          );
                }
            }

            debugMsg("  +-------------------------\n");
        }
#endif

        // Refresh display
#ifdef PS1_BUILD
        grUpdateDisplay(&ttmBackgroundThread, ttmThreads, &ttmHolidayThread);
#else
        grUpdateDisplay(&ttmBackgroundThread, ttmThreads, &ttmHolidayThread);
#endif

        // Determine min timer and collect active indices in a single pass
        {
            uint16 mini = 300;
            int activeIdx[MAX_TTM_THREADS];
            int numActive = 0;

            if (ttmBackgroundThread.isRunning)
                mini = ttmBackgroundThread.timer;

            for (int i = 0; i < MAX_TTM_THREADS; i++) {
                if (ttmThreads[i].isRunning == ADS_THREAD_RUNNING) {
                    activeIdx[numActive++] = i;
                    if (ttmThreads[i].delay < mini)
                        mini = ttmThreads[i].delay;
                    if (ttmThreads[i].timer < mini)
                        mini = ttmThreads[i].timer;
                }
            }

            /* Prevent zero-tick loops from starving progression and display updates. */
            if (mini == 0) mini = 1;

            // Decrease all timers by the shortest one, and wait that amount of time
#ifdef PS1_BUILD
            if (grPs1TelemetryEnabled)
                ps1AdsDbgMini = mini;
#endif
            if (ttmBackgroundThread.timer > mini)
                ttmBackgroundThread.timer -= mini;
            else
                ttmBackgroundThread.timer = 0;

            for (int j = 0; j < numActive; j++) {
                int idx = activeIdx[j];
                if (ttmThreads[idx].timer > mini)
                    ttmThreads[idx].timer -= mini;
                else
                    ttmThreads[idx].timer = 0;
            }

            debugMsg(" ******* WAIT: %d ticks *******\n", mini);
            grUpdateDelay = mini;
        }

        // Various threads processes
        for (int i=0; i < MAX_TTM_THREADS; i++) {

            if (ttmThreads[i].isRunning == ADS_THREAD_RUNNING && ttmThreads[i].timer == 0) {

                // Process jumps
                if (ttmThreads[i].nextGotoOffset) {
                    ttmThreads[i].ip = ttmThreads[i].nextGotoOffset;
                    ttmThreads[i].nextGotoOffset = 0;
                }

                // Managing the timer which was indicated in ADD_SCENE arg3 (neg. value)
                if (ttmThreads[i].sceneTimer > 0) {
                    ttmThreads[i].sceneTimer -= ttmThreads[i].delay;
                    if (ttmThreads[i].sceneTimer <= 0)
                        ttmThreads[i].isRunning = ADS_THREAD_TERMINATED;
                }
            }

            /* Free terminated threads immediately to avoid stale replay/timer side effects. */
            if (ttmThreads[i].isRunning == ADS_THREAD_TERMINATED) {

                // Managing the numPlays which was indicated in ADD_SCENE arg3 (postive value)
                if (ttmThreads[i].sceneIterations) {
                    ttmThreads[i].sceneIterations--;
                    ttmThreads[i].isRunning = ADS_THREAD_RUNNING;
                    ttmThreads[i].sceneEpoch++;
                    ttmThreads[i].numDrawnSprites = 0;
                    ttmThreads[i].lastActorReplayValid = 0;
                    ttmThreads[i].ip = ttmFindTag(&ttmSlots[ttmThreads[i].sceneSlot], ttmThreads[i].sceneTag);
                    ttmThreads[i].timer = 0;
                }

                // Is there one (or more) IF_LASTPLAYED matching the terminated thread ?
                else {
                    uint16 endedSlot = ttmThreads[i].sceneSlot;
                    uint16 endedTag = ttmThreads[i].sceneTag;
#ifdef PS1_BUILD
                    adsCaptureHandoffReplay(&ttmThreads[i]);
#endif
                    adsStopScene(i);
                    if (!adsStopRequested)
                        adsPlayTriggeredChunks(data, dataSize, endedSlot, endedTag);
#ifdef PS1_BUILD
                    /* If handoff wasn't consumed (no new thread started),
                     * inject actor into a surviving thread to prevent vanish. */
                    if (!adsUseRestorePilotReplayPolicy() &&
                        gHandoffReplayValid && gHandoffReplayCount > 0) {
                        for (int k = 0; k < MAX_TTM_THREADS; k++) {
                            if (ttmThreads[k].isRunning == ADS_THREAD_RUNNING &&
                                ttmThreads[k].numDrawnSprites < MAX_DRAWN_SPRITES) {
                                gHandoffReplay[0].sceneEpoch = ttmThreads[k].sceneEpoch;
                                ttmThreads[k].drawnSprites[ttmThreads[k].numDrawnSprites++] = gHandoffReplay[0];
                                ttmThreads[k].lastActorReplay = gHandoffReplay[0];
                                ttmThreads[k].lastActorReplayValid = 1;
                                gHandoffReplayValid = 0;
                                gHandoffReplayCount = 0;
                                break;
                            }
                        }
                    }
#endif
                }
            }
        }

    }

    for (int i=0; i < MAX_TTM_SLOTS; i++)
        ttmResetSlot(&ttmSlots[i]);

    grRestoreZone(NULL, 0, 0, 0, 0);

    adsReleaseAds();

    /* Unpin ADS resource to allow LRU eviction */
    unpinResource(adsResource, "ADS");
}


void adsPlayBench()
{
    int numsLayers[] = { 1, 4, 8 };

    uint32 startTicks, counter;

    adsInit();

    for (int i=0; i < 8; i++) {
        ttmThreads[i].ttmSlot         = &ttmSlots[0];
        ttmThreads[i].isRunning       = 1;
        ttmThreads[i].selectedBmpSlot = 0;
        ttmThreads[i].ttmLayer        = grNewLayer();
    }

    benchInit(ttmSlots);
    grUpdateDelay = 0;

    for (int j=0; j < (int)(sizeof(numsLayers) / sizeof(numsLayers[0])); j++) {

        int numLayers = numsLayers[j];

        for (int i=0; i < MAX_TTM_THREADS; i++)
            ttmThreads[i].isRunning = (i<numLayers ? 1 : 0);

        startTicks = SDL_GetTicks();
        counter = 0;

        while ((SDL_GetTicks() - startTicks) <= 3000) {

            for (int i=0; i < numLayers; i++)
                benchPlay(&ttmThreads[i], i);

            grUpdateDisplay(NULL, ttmThreads, NULL);

            counter++;
        }

        printf(" %d-layers test --> %d fps\n", numLayers, counter/3);
    }

    for (int i=0; i < 8; i++)
        adsStopScene(i);

    ttmResetSlot(&ttmSlots[0]);
}


void adsPlayIntro()
{
    grLoadScreen("INTRO.SCR");
    grUpdateDelay = 100;
    grUpdateDisplay(NULL, ttmThreads, NULL);
    grFadeOut();
    ttmResetSlot(&ttmSlots[0]);
}


void adsInitIsland()
{
    // Init the background thread (animated waves)
    // and call islandInit() to draw the background

    ttmInitSlot(&ttmBackgroundSlot);

    ttmBackgroundThread.ttmSlot   = &ttmBackgroundSlot;
    ttmBackgroundThread.isRunning = 3;
    ttmBackgroundThread.delay     = 40;
    ttmBackgroundThread.timer     = 0;

    islandInit(&ttmBackgroundThread);


    // Init the "holiday" layer and thread

    ttmInitSlot(&ttmHolidaySlot);

    ttmHolidayThread.ttmSlot   = &ttmHolidaySlot;
    ttmHolidayThread.isRunning = 0;
    ttmHolidayThread.delay     = 0;
    ttmHolidayThread.timer     = 0;

    islandInitHoliday(&ttmHolidayThread);

    /* Save bg tiles with island drawn so grRestoreBgTiles() preserves it */
    grSaveCleanBgTiles();

}


void adsReleaseIsland()
{
    islandClearWaveCache();
    ttmBackgroundThread.isRunning = 0;
    ttmResetSlot(&ttmBackgroundSlot);

    if (ttmHolidayThread.isRunning) {
        ttmHolidayThread.isRunning = 0;
        grFreeLayer(ttmHolidayThread.ttmLayer);
    }
}


void adsNoIsland()
{
    grDx = grDy = 0;
    grInitEmptyBackground();
#ifdef PS1_BUILD
    /* On PS1, don't save black tiles as clean copies — saves 600KB of RAM
     * and prevents corrupting clean copies that island scenes need.
     * Non-island scenes (JOHNNY.ADS intro/outro) don't need sprite erasing. */
    grFreeCleanBgTiles();
#else
    grSaveCleanBgTiles();
#endif
}


void adsPlayWalk(int fromSpot, int fromHdg, int toSpot, int toHdg)
{
    ps1AdsLastPlayLaunched = 1;
    adsAddScene(0,0,0);
    grLoadBmp(ttmSlots, 0, "JOHNWALK.BMP");

    grDx = islandState.xPos;
    grDy = islandState.yPos;

    ttmThreads[0].timer = ttmThreads[0].delay = 6; // 12 ?

    walkInit(fromSpot, fromHdg, toSpot, toHdg);

#ifdef PS1_BUILD
    ttmThreads[0].numDrawnSprites = 0;
    ttmThreads[0].replayWriteCursor = 0;
    ttmThreads[0].lastActorReplayValid = 0;
    grCurrentThread = &ttmThreads[0];
#endif
    ttmThreads[0].delay = walkAnimate(&ttmThreads[0], ttmBackgroundThread.ttmSlot);
#ifdef PS1_BUILD
    grCurrentThread = NULL;
    grEnsureCleanBgTiles();
#endif

    while (ttmThreads[0].delay) {

#ifdef PS1_BUILD
        /* Reset GPU frame state and restore clean background */
        grBeginFrame();
        grRestoreBgTiles();
#endif

        // Call each thread which timer reaches 0
        if (!ttmBackgroundThread.timer) {
            debugMsg("    ------> Animate bg");
            ttmBackgroundThread.timer = ttmBackgroundThread.delay;
            islandAnimate(&ttmBackgroundThread);
        }
#ifdef PS1_BUILD
        else if (ttmBackgroundThread.isRunning) {
            islandRedrawWave(&ttmBackgroundThread);
        }
#endif

        if (!ttmThreads[0].timer) {
            debugMsg("    ------> Animate walking");
#ifdef PS1_BUILD
            uint8 prevCount = adsCaptureReplayRecords(&ttmThreads[0]);
            ttmThreads[0].numDrawnSprites = 0;
            ttmThreads[0].replayWriteCursor = 0;
            grCurrentThread = &ttmThreads[0];
#endif
            ttmThreads[0].timer = ttmThreads[0].delay =
                walkAnimate(&ttmThreads[0], ttmBackgroundThread.ttmSlot);
#ifdef PS1_BUILD
            grCurrentThread = NULL;
            adsMergeReplayByProximity(&ttmThreads[0], gPrevReplayScratch, prevCount);
            adsRecoverMissingActor(&ttmThreads[0], gPrevReplayScratch, prevCount);
            /* Walk diagnostics: show BMP load status and draw count */
            ps1AdsDbgActiveThreads = 77;  /* marker: walk mode */
            ps1AdsDbgRunningThreads = ttmThreads[0].numDrawnSprites;
            ps1AdsDbgReplayCount = ttmSlots[0].numSprites[0];  /* JOHNWALK frames loaded */
            ps1AdsDbgReplayDrawFrame = ttmThreads[0].delay;
            ps1AdsDbgMergeCarryFrame = prevCount;
            if (ttmThreads[0].numDrawnSprites > 0) {
                sint16 sx = ttmThreads[0].drawnSprites[0].x;
                sint16 sy = ttmThreads[0].drawnSprites[0].y;
                ps1AdsDbgNoDrawThreadsFrame = (sx < 0) ? 0 : (uint16)sx;
                ps1AdsDbgPlayedThreadsFrame = (sy < 0) ? 0 : (uint16)sy;
                ps1AdsDbgRecordedSpritesFrame =
                    (uint16)ttmThreads[0].drawnSprites[0].width;
            } else {
                ps1AdsDbgNoDrawThreadsFrame = 90;
                ps1AdsDbgPlayedThreadsFrame = 90;
                ps1AdsDbgRecordedSpritesFrame = 0;
            }
#endif
        }
#ifdef PS1_BUILD
        else {
            /* Replay via GPU path */
            for (int j = 0; j < ttmThreads[0].numDrawnSprites; j++)
                grReplaySprite(&ttmThreads[0].drawnSprites[j]);
            /* Walk replay diagnostics */
            ps1AdsDbgActiveThreads = 88;  /* marker: walk replay mode */
            ps1AdsDbgRunningThreads = ttmThreads[0].numDrawnSprites;
            ps1AdsDbgReplayCount = ttmSlots[0].numSprites[0];
            ps1AdsDbgReplayDrawFrame = ttmThreads[0].delay;
            if (ttmThreads[0].numDrawnSprites > 0) {
                ps1AdsDbgNoDrawThreadsFrame =
                    (ttmThreads[0].drawnSprites[0].x < 0) ? 0 :
                    (uint16)ttmThreads[0].drawnSprites[0].x;
                ps1AdsDbgPlayedThreadsFrame =
                    (ttmThreads[0].drawnSprites[0].y < 0) ? 0 :
                    (uint16)ttmThreads[0].drawnSprites[0].y;
                ps1AdsDbgRecordedSpritesFrame =
                    (uint16)ttmThreads[0].drawnSprites[0].width;
            } else {
                ps1AdsDbgNoDrawThreadsFrame = 0;
                ps1AdsDbgPlayedThreadsFrame = 1;
                ps1AdsDbgRecordedSpritesFrame = 1;
            }
        }
#endif

        // Refresh display
#ifdef PS1_BUILD
        grUpdateDisplay(&ttmBackgroundThread, ttmThreads, &ttmHolidayThread);
#else
        grUpdateDisplay(&ttmBackgroundThread, ttmThreads, &ttmHolidayThread);
#endif

        // Determine min timer from the two threads
        uint16 mini = 300;
        if (ttmBackgroundThread.timer < ttmThreads[0].timer)
            mini = ttmBackgroundThread.timer;
        else
            mini = ttmThreads[0].timer;

        /* Prevent zero-tick walk loops which can lock animation progression. */
        if (mini == 0) mini = 1;

        // Decrease all timers by the shortest one, and wait that amount of time
        if (ttmBackgroundThread.timer > mini)
            ttmBackgroundThread.timer -= mini;
        else
            ttmBackgroundThread.timer = 0;

        if (ttmThreads[0].timer > mini)
            ttmThreads[0].timer -= mini;
        else
            ttmThreads[0].timer = 0;

        debugMsg(" ******* WAIT: %d ticks *******\n", mini);
        grUpdateDelay = mini;
    }

    adsStopScene(0);
}
