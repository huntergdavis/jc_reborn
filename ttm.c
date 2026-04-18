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
#include <stdio.h>
#include <stdlib.h>
#else
/* PS1 minimal stdio declarations */
#ifndef _FILE_DEFINED
#define _FILE_DEFINED
typedef struct _FILE FILE;
#endif
extern int printf(const char *format, ...);
extern int fprintf(FILE *stream, const char *format, ...);
#define stderr ((FILE*)2)
#include <stdlib.h>  /* For free() */
#endif

#include <string.h>
#include "mytypes.h"
#include "utils.h"

/* (#77) On PS1, make debugMsg a no-op macro to eliminate argument evaluation */
#ifdef PS1_BUILD
#undef debugMsg
#define debugMsg(...) ((void)0)
#endif

#include "resource.h"
/* Platform-specific graphics and sound headers */
#ifdef PS1_BUILD
#include "graphics_ps1.h"
#include "sound_ps1.h"
#include "cdrom_ps1.h"
#include "ads.h"
#include "ps1_restore_pilots.h"
#else
#include "graphics.h"
#include "sound.h"
#endif
#include "ttm.h"


int ttmDx = 0;
int ttmDy = 0;
static int gTtmStaticBaseBuildMode = 0;

#ifdef PS1_BUILD
static int ttmStringEquals(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return 0;
    return strcmp(a, b) == 0;
}

static int ttmIsBuildingMjsandProbe(struct TTtmThread *ttmThread)
{
    const struct TTtmResource *ttmResource;

    if (ttmThread == NULL || ttmThread->ttmSlot == NULL)
        return 0;
    if (!ttmStringEquals(ps1AdsCurrentName, "BUILDING.ADS") || ps1AdsCurrentTag != 1)
        return 0;

    ttmResource = ttmThread->ttmSlot->ttmResource;
    if (ttmResource == NULL || ttmResource->resName == NULL)
        return 0;

    return ttmStringEquals(ttmResource->resName, "MJSAND.TTM");
}

static int ttmBmpAllowedInStaticBase(const char *bmpName)
{
    if (bmpName == NULL)
        return 0;
    return ttmStringEquals(bmpName, "TRUNK.BMP") ||
           ttmStringEquals(bmpName, "BACKGRND.BMP") ||
           ttmStringEquals(bmpName, "MRAFT.BMP") ||
           ttmStringEquals(bmpName, "HOLIDAY.BMP");
}

static int ttmPilotContainsAdsTag(const struct TPs1RestorePilot *pilot, uint16 adsTag)
{
    uint16 i;
    if (pilot == NULL)
        return 0;
    for (i = 0; i < pilot->adsTagCount; i++) {
        if (pilot->adsTags[i] == adsTag)
            return 1;
    }
    return 0;
}

static const struct TPs1RestorePilot *ttmFindActiveRestorePilot(void)
{
    /* (#26) Cache the last pilot lookup keyed by ADS name+tag */
    static char cachedName[16] = "";
    static uint16 cachedTag = 0xFFFF;
    static const struct TPs1RestorePilot *cachedPilot = NULL;
    static int cacheValid = 0;
    uint16 i;

    /* Check if cache is still valid */
    if (cacheValid && cachedTag == ps1AdsCurrentTag &&
        strcmp(cachedName, ps1AdsCurrentName) == 0)
        return cachedPilot;

    /* Cache miss - do the full lookup */
    cachedPilot = NULL;
    for (i = 0; i < PS1_RESTORE_PILOT_COUNT; i++) {
        const struct TPs1RestorePilot *pilot = &gPs1RestorePilots[i];
        if (!ttmStringEquals(ps1AdsCurrentName, pilot->adsName))
            continue;
        if (ttmPilotContainsAdsTag(pilot, ps1AdsCurrentTag)) {
            cachedPilot = pilot;
            break;
        }
    }

    /* Store cache key */
    {
        int j;
        for (j = 0; j < 15 && ps1AdsCurrentName[j] != '\0'; j++)
            cachedName[j] = ps1AdsCurrentName[j];
        cachedName[j] = '\0';
    }
    cachedTag = ps1AdsCurrentTag;
    cacheValid = 1;

    return cachedPilot;
}

static const struct TPs1RestorePilotTtm *ttmFindRestorePilotTtm(const struct TPs1RestorePilot *pilot,
                                                                const char *ttmName)
{
    uint16 i;
    if (pilot == NULL)
        return NULL;
    for (i = 0; i < pilot->ttmCount; i++) {
        if (ttmStringEquals(pilot->ttms[i].ttmName, ttmName))
            return &pilot->ttms[i];
    }
    return NULL;
}

static int ttmApplyRestorePilotClear(struct TTtmThread *ttmThread, uint16 regionId)
{
    const struct TTtmResource *ttmResource;
    const struct TPs1RestorePilot *pilot;
    const struct TPs1RestorePilotTtm *pilotTtm;

    if (ttmThread == NULL || ttmThread->ttmSlot == NULL)
        return 0;
    pilot = ttmFindActiveRestorePilot();
    if (pilot == NULL)
        return 0;

    ttmResource = ttmThread->ttmSlot->ttmResource;
    if (ttmResource == NULL || ttmResource->resName == NULL)
        return 0;

    pilotTtm = ttmFindRestorePilotTtm(pilot, ttmResource->resName);
    if (pilotTtm == NULL || pilotTtm->clearRegionId != regionId)
        return 0;

    grRestoreZone(ttmThread->ttmLayer,
                  pilotTtm->rect.x,
                  pilotTtm->rect.y,
                  pilotTtm->rect.width,
                  pilotTtm->rect.height);
    return 1;
}

static int ttmApplyRestorePilotSaveImage1(struct TTtmThread *ttmThread)
{
    const struct TTtmResource *ttmResource;
    const struct TPs1RestorePilot *pilot;
    const struct TPs1RestorePilotTtm *pilotTtm;

    if (ttmThread == NULL || ttmThread->ttmSlot == NULL)
        return 0;
    pilot = ttmFindActiveRestorePilot();
    if (pilot == NULL)
        return 0;

    ttmResource = ttmThread->ttmSlot->ttmResource;
    if (ttmResource == NULL || ttmResource->resName == NULL)
        return 0;

    pilotTtm = ttmFindRestorePilotTtm(pilot, ttmResource->resName);
    if (pilotTtm == NULL || pilotTtm->saveImageRegionId != ttmThread->currentRegionId)
        return 0;

    grSaveImage1(ttmThread->ttmLayer,
                 pilotTtm->rect.x,
                 pilotTtm->rect.y,
                 pilotTtm->rect.width,
                 pilotTtm->rect.height);
    return 1;
}
#endif


static uint32 ttmFindPreviousTag(struct TTtmSlot *ttmSlot, uint32 offset)
{
    /* (#41) Binary search (upper_bound - 1) since tags are sorted by offset */
    int lo = 0, hi = ttmSlot->numTags - 1;
    uint32 result = 0;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ttmSlot->tags[mid].offset < offset) {
            result = ttmSlot->tags[mid].offset;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    return result;
}


uint32 ttmFindTag(struct TTtmSlot *ttmSlot, uint16 reqdTag)
{
    /* (#40) Tags are sorted by offset, not ID - use last-hit cache */
    static int lastHit = 0;
    int i, n = ttmSlot->numTags;

    /* Check last hit and nearby tags first (common case: sequential access) */
    if (lastHit < n) {
        int start = (lastHit > 2) ? lastHit - 2 : 0;
        int end = (lastHit + 3 < n) ? lastHit + 3 : n;
        for (i = start; i < end; i++) {
            if (ttmSlot->tags[i].id == reqdTag) {
                lastHit = i;
                return ttmSlot->tags[i].offset;
            }
        }
    }

    /* Full linear scan fallback */
    for (i = 0; i < n; i++) {
        if (ttmSlot->tags[i].id == reqdTag) {
            lastHit = i;
            return ttmSlot->tags[i].offset;
        }
    }

#ifndef PS1_BUILD
    fprintf(stderr, "Warning : TTM tag #%d not found, returning offset 0000\n", reqdTag);
#endif

    return 0;
}

static int ttmIsValidOffset(struct TTtmSlot *ttmSlot, uint32 offset)
{
    return ttmSlot != NULL &&
           ttmSlot->data != NULL &&
           offset < ttmSlot->dataSize;
}

static int ttmStoreTag(struct TTtmSlot *ttmSlot, int *tagNo, uint16 id, uint32 offset)
{
    if (ttmSlot == NULL || tagNo == NULL || ttmSlot->tags == NULL) {
        return 0;
    }

    if (*tagNo >= ttmSlot->numTags) {
        debugMsg("Warning : TTM tag table overflow (%u at %lu, capacity %d)",
                 id, (unsigned long)offset, ttmSlot->numTags);
        return 0;
    }

    ttmSlot->tags[*tagNo].id     = id;
    ttmSlot->tags[*tagNo].offset = offset;
    (*tagNo)++;
    return 1;
}

#ifdef PS1_BUILD
static void ttmDiagInvalidIp(const char *reason, struct TTtmThread *ttmThread, uint32 offset)
{
    static int diagCount = 0;
    const char *name = NULL;

    if (diagCount >= 16)
        return;
    diagCount++;

    if (ttmThread != NULL && ttmThread->ttmSlot != NULL &&
        ttmThread->ttmSlot->ttmResource != NULL) {
        name = ttmThread->ttmSlot->ttmResource->resName;
    }

    printf("TTM_DIAG %s ads=%s:%u sceneSlot=%u sceneTag=%u ttm=%s ip=0x%08lX next=0x%08lX dataSize=%lu delay=%u timer=%u running=%d\n",
           reason ? reason : "?",
           ps1AdsCurrentName,
           (unsigned int)ps1AdsCurrentTag,
           (unsigned int)(ttmThread ? ttmThread->sceneSlot : 0),
           (unsigned int)(ttmThread ? ttmThread->sceneTag : 0),
           name ? name : "?",
           (unsigned long)offset,
           (unsigned long)(ttmThread ? ttmThread->nextGotoOffset : 0),
           (unsigned long)((ttmThread && ttmThread->ttmSlot) ? ttmThread->ttmSlot->dataSize : 0),
           (unsigned int)(ttmThread ? ttmThread->delay : 0),
           (unsigned int)(ttmThread ? ttmThread->timer : 0),
           ttmThread ? ttmThread->isRunning : -1);
}

static int ttmDiagStringOverflow(struct TTtmThread *ttmThread, uint16 opcode, uint32 startOffset, int copied)
{
    static int diagCount = 0;
    const char *name = NULL;

    if (diagCount >= 16)
        return 0;
    diagCount++;

    if (ttmThread != NULL && ttmThread->ttmSlot != NULL &&
        ttmThread->ttmSlot->ttmResource != NULL) {
        name = ttmThread->ttmSlot->ttmResource->resName;
    }

    printf("TTM_DIAG string_overflow ads=%s:%u sceneSlot=%u sceneTag=%u ttm=%s opcode=0x%04X start=0x%08lX copied=%d\n",
           ps1AdsCurrentName,
           (unsigned int)ps1AdsCurrentTag,
           (unsigned int)(ttmThread ? ttmThread->sceneSlot : 0),
           (unsigned int)(ttmThread ? ttmThread->sceneTag : 0),
           name ? name : "?",
           (unsigned int)opcode,
           (unsigned long)startOffset,
           copied);
    return 1;
}

static int ttmDiagArgsOverflow(struct TTtmThread *ttmThread, uint16 opcode, uint8 numArgs)
{
    static int diagCount = 0;
    const char *name = NULL;

    if (diagCount >= 16)
        return 0;
    diagCount++;

    if (ttmThread != NULL && ttmThread->ttmSlot != NULL &&
        ttmThread->ttmSlot->ttmResource != NULL) {
        name = ttmThread->ttmSlot->ttmResource->resName;
    }

    printf("TTM_DIAG args_overflow ads=%s:%u sceneSlot=%u sceneTag=%u ttm=%s opcode=0x%04X numArgs=%u\n",
           ps1AdsCurrentName,
           (unsigned int)ps1AdsCurrentTag,
           (unsigned int)(ttmThread ? ttmThread->sceneSlot : 0),
           (unsigned int)(ttmThread ? ttmThread->sceneTag : 0),
           name ? name : "?",
           (unsigned int)opcode,
           (unsigned int)numArgs);
    return 1;
}
#endif


void ttmLoadTtm(struct TTtmSlot *ttmSlot, char *ttmName)     // TODO
{
    struct TTtmResource *ttmResource = findTtmResource(ttmName);

#ifdef PS1_BUILD
    if (ttmResource == NULL) {
        ttmSlot->data = NULL;
        return;
    }
#endif

    debugMsg("---- Loading %s", ttmResource->resName);

    /* TTM lazy loading: Load TTM data on demand from extracted file if not already loaded */
    if (ttmResource->uncompressedData == NULL) {
#ifdef PS1_BUILD
        /* PS1: Load from pre-extracted TTM file on CD */
        ps1_loadTtmData(ttmResource);
        if (ttmResource->uncompressedData == NULL) {
            ttmSlot->data = NULL;
            return;
        }
#else
        char extractedPath[512];
        snprintf(extractedPath, sizeof(extractedPath), "extracted/ttm/%s",
                 ttmResource->resName);

        FILE *f = fopen(extractedPath, "rb");
        if (f) {
            ttmResource->uncompressedData = safe_malloc(ttmResource->uncompressedSize);
            if (fread(ttmResource->uncompressedData, 1, ttmResource->uncompressedSize, f) !=
                ttmResource->uncompressedSize) {
                fatalError("Failed to load TTM data from extracted file");
            }
            fclose(f);
            if (debugMode) {
                printf("Loaded TTM data for %s from disk (%u bytes)\n",
                       ttmResource->resName, ttmResource->uncompressedSize);
            }
        } else {
            fatalError("TTM data not loaded and extracted file not found - cannot load %s", ttmName);
        }
#endif
    }

    /* Pin TTM resource to prevent eviction while in use */
    pinResource(ttmResource, ttmResource->uncompressedSize, "TTM");

    /* Check memory budget and potentially evict unused resources */
    checkMemoryBudget();

    ttmSlot->data     = ttmResource->uncompressedData;
    ttmSlot->ttmResource = ttmResource;  /* Store reference for unpinning later */
    ttmSlot->dataSize = ttmResource->uncompressedSize;
    ttmSlot->numTags  = ttmResource->numTags;
    ttmSlot->tags     = safe_malloc(ttmSlot->numTags * sizeof(struct TTtmTag));

    // we have to bookmark every tag for later jumps
    uint32 offset=0;
    int tagNo = 0;

    while (offset < ttmSlot->dataSize) {

        uint16 opcode = peekUint16(ttmSlot->data, &offset);

        if (opcode == 0x1111 || opcode == 0x1101) {
            uint16 arg = peekUint16(ttmSlot->data, &offset);
            ttmStoreTag(ttmSlot, &tagNo, arg, offset);
        }
        else {

            uint8 numArgs = (uint8) opcode & 0x000f;

            if (numArgs == 0x0f) {
                while ((offset + 1) < ttmSlot->dataSize &&
                       ttmSlot->data[offset] != 0 &&
                       ttmSlot->data[offset + 1] != 0)
                    offset += 2;
                if ((offset + 1) >= ttmSlot->dataSize)
                    break;
                offset += 2;
            }
            else {
                if ((offset + ((uint32)numArgs << 1)) > ttmSlot->dataSize)
                    break;
                offset += (numArgs << 1);
            }
        }
    }

    // TODO : in SASKDATE.TTM, num SET_SCENE != ttmResource->numTags
    while (tagNo < ttmSlot->numTags) {
        ttmSlot->tags[tagNo].id = 0;
        ttmSlot->tags[tagNo].offset = 0;
        tagNo++;
    }

#ifdef PS1_BUILD
    /* (#43) One-time TTM data verification - moved from ttmPlay() to avoid
     * per-frame static-flag check overhead */
    {
        static int verifyDone = 0;
        if (!verifyDone && ttmSlot->data != NULL && ttmSlot->dataSize >= 0x2C) {
            uint16 firstOpcode = ttmSlot->data[0] | (ttmSlot->data[1] << 8);
            static uint16 checkPixels[16];
            if (firstOpcode == 0x1061) {
                /* Small GREEN dot at top-right = TTM data valid */
                for (int i = 0; i < 16; i++) checkPixels[i] = (0 << 10) | (31 << 5) | 0;
            } else {
                /* Small RED dot = TTM data invalid */
                for (int i = 0; i < 16; i++) checkPixels[i] = (0 << 10) | (0 << 5) | 31;
            }
            RECT checkRect;
            setRECT(&checkRect, 620, 10, 8, 8);
            LoadImage(&checkRect, (uint32*)checkPixels);
            DrawSync(0);
            verifyDone = 1;
        }
    }
#endif
}


void ttmInitSlot(struct TTtmSlot *ttmSlot)
{
    if (ttmSlot == NULL)
        return;

    memset(ttmSlot, 0, sizeof(*ttmSlot));
}


void ttmResetSlot(struct TTtmSlot *ttmSlot)
{
    if (ttmSlot == NULL)
        return;

    if (ttmSlot->data != NULL) {
        /* Unpin TTM resource to allow LRU eviction */
        if (ttmSlot->ttmResource != NULL) {
            unpinResource(ttmSlot->ttmResource, "TTM");
            ttmSlot->ttmResource = NULL;
        }

        ttmSlot->data = NULL;
        free(ttmSlot->tags);
        ttmSlot->tags = NULL;
        ttmSlot->dataSize = 0;
        ttmSlot->numTags = 0;
    }

    for (int i=0; i < MAX_BMP_SLOTS; i++) {
        if (ttmSlot->numSprites[i])
            grReleaseBmp(ttmSlot, i);
    }

    memset(ttmSlot->numSprites, 0, sizeof(ttmSlot->numSprites));
    memset(ttmSlot->loadedBmp, 0, sizeof(ttmSlot->loadedBmp));
    memset(ttmSlot->loadedBmpNames, 0, sizeof(ttmSlot->loadedBmpNames));
    memset(ttmSlot->psbData, 0, sizeof(ttmSlot->psbData));
    memset(ttmSlot->sprites, 0, sizeof(ttmSlot->sprites));
}


void ttmPlay(struct TTtmThread *ttmThread)     // TODO
{
    uint8 *data;
    uint32 offset;
    uint32 opcodeOffset;
    uint16 opcode;
    uint8 numArgs;
    uint16 args[10];
    char *strArg;
    int continueLoop = 1;
    struct TTtmSlot *ttmSlot;


    grDx = ttmDx;
    grDy = ttmDy;

    ttmSlot = ttmThread->ttmSlot;
    offset = ttmThread->ip;
    data = ttmSlot->data;
    strArg = ttmThread->currentStringArg;

    if (!ttmIsValidOffset(ttmSlot, offset)) {
#ifdef PS1_BUILD
        ttmDiagInvalidIp("bad_entry", ttmThread, offset);
#endif
        ttmThread->isRunning = 2;
        return;
    }

    while (continueLoop) {
        if (offset + 1 >= ttmSlot->dataSize) {
#ifdef PS1_BUILD
            ttmDiagInvalidIp("past_end", ttmThread, offset);
#endif
            ttmThread->isRunning = 2;
            break;
        }

        opcodeOffset = offset;
        opcode = peekUint16(data, &offset);

        numArgs = (uint8) opcode & 0x0000f;

        if (numArgs == 0x0f) {        // arg is a string

            int i=0;
            int overflow = 0;

            while (offset < ttmSlot->dataSize && data[offset] != 0) {
                if (i < (int)sizeof(ttmThread->currentStringArg) - 1) {
                    strArg[i++] = data[offset++];
                } else {
                    overflow = 1;
                    offset++;
                }
            }

            if (offset >= ttmSlot->dataSize) {
#ifdef PS1_BUILD
                ttmDiagInvalidIp("string_past_end", ttmThread, opcodeOffset);
#endif
                ttmThread->isRunning = 2;
                break;
            }

            strArg[i++] = data[offset++];

            if ((i & 0x01) == 0x01)   // always read an even number of uint8s
            {
                if (offset >= ttmSlot->dataSize) {
#ifdef PS1_BUILD
                    ttmDiagInvalidIp("string_align_past_end", ttmThread, opcodeOffset);
#endif
                    ttmThread->isRunning = 2;
                    break;
                }
                if (i < (int)sizeof(ttmThread->currentStringArg) - 1)
                    strArg[i++] = data[offset++];
                else
                    offset++;
            }
            strArg[i] = '\0';
#ifdef PS1_BUILD
            if (overflow)
                ttmDiagStringOverflow(ttmThread, opcode, opcodeOffset, i);
#endif
        }
        else {                        // args are numArgs words
            if (numArgs > (uint8)(sizeof(args) / sizeof(args[0]))) {
#ifdef PS1_BUILD
                ttmDiagArgsOverflow(ttmThread, opcode, numArgs);
#endif
                ttmThread->isRunning = 2;
                break;
            }
            peekUint16Block(data, &offset, args, numArgs);
        }


        switch (opcode) {

            case 0x0080:
                debugMsg("    DRAW_BACKGROUND");
                // Free images slots - see for example tag 11 of GFFFOOD.TTM
                break;

            case 0x0110:
                debugMsg("    PURGE");
                if (ttmThread->sceneTimer) {
                    ttmThread->nextGotoOffset = ttmFindPreviousTag(ttmSlot, offset);
                    continueLoop = 0;  /* Exit to let main loop handle the jump */
                } else {
                    ttmThread->isRunning = 2;
                }
                break;

            case 0x0FF0:
                debugMsg("    UPDATE");
                if (!gTtmStaticBaseBuildMode)
                    continueLoop = 0;
                break;

            case 0x1021:
                debugMsg("    SET_DELAY %d", args[0]);
                ttmThread->timer = ttmThread->delay = (args[0] > 4 ? args[0] : 4);  // TODO ?
                break;

            case 0x1051:
                debugMsg("    SET_BMP_SLOT %d", args[0]);
                if (args[0] < MAX_BMP_SLOTS) {
                    ttmThread->selectedBmpSlot = args[0];
                } else {
                    ttmThread->selectedBmpSlot = (MAX_BMP_SLOTS - 1);
                }
                break;

            case 0x1061:
                debugMsg("    SET_PALETTE_SLOT %d", args[0]);
                break;

            case 0x1101:
                debugMsg("    :LOCAL_TAG %d", args[0]);
                break;

            case 0x1111:
                debugMsg("\n    :TAG %d ------------------------", args[0]);
                break;

            case 0x1121:
                // is called before SAVE_IMAGE1, defines the id of the region
                // for further use by CLEAR_SCREEN
                // (see WOULDBE.TTM for a nice example)
                debugMsg("    TTM_UNKNOWN_1 %d", args[0]);
                ttmThread->currentRegionId = args[0];
                break;

            case 0x1201:
                // ex TTM_UNKNOWN_2
                debugMsg("    GOTO_TAG %d", args[0]);
                ttmThread->nextGotoOffset = ttmFindTag(ttmSlot, args[0]);
                if (args[0] != 0 &&
                    !ttmIsValidOffset(ttmSlot, ttmThread->nextGotoOffset)) {
#ifdef PS1_BUILD
                    ttmDiagInvalidIp("bad_goto_tag", ttmThread, ttmThread->nextGotoOffset);
#endif
                    ttmThread->isRunning = 2;
                    ttmThread->nextGotoOffset = 0;
                    continueLoop = 0;
                    break;
                }
                continueLoop = 0;  /* Exit to let main loop handle the jump */
                break;

            case 0x2002:
                debugMsg("    SET_COLORS %d %d", args[0], args[1]);
                ttmThread->fgColor = args[0];
                ttmThread->bgColor = args[1];
                break;

            case 0x2012:
                // args always == (0,0)
                // at beginning of scenes, near LOAD_IMAGEs
                debugMsg("    SET_FRAME1 %d %d", args[0], args[1]);
                break;

            case 0x2022:
                debugMsg("    TIMER %d %d", args[0], args[1]);
                // Really, really not sure about this formula... but things
                // do work not so bad like that
                ttmThread->delay = ttmThread->timer = (args[0] + args[1]) / 2;
                if (ttmThread->delay == 0) ttmThread->delay = 1;
                if (ttmThread->timer == 0) ttmThread->timer = 1;
                break;

            case 0x4004:
                debugMsg("    SET_CLIP_ZONE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grSetClipZone(ttmThread->ttmLayer, args[0], args[1], args[2], args[3]);
                break;

            case 0x4204:
                debugMsg("    COPY_ZONE_TO_BG %d %d %d %d", args[0], args[1], args[2], args[3]);
#ifdef PS1_BUILD
                if (ttmIsBuildingMjsandProbe(ttmThread))
                    break;
#endif
                grCopyZoneToBg(ttmThread->ttmLayer, args[0], args[1], args[2], args[3]);
                break;

            case 0x4214:
                // defines the zone to be redrawn at each update ?
                // but seems not used in the original
                debugMsg("    SAVE_IMAGE1 %d %d %d %d", args[0], args[1], args[2], args[3]);
 #ifdef PS1_BUILD
                if (ttmIsBuildingMjsandProbe(ttmThread))
                    break;
                if (!ttmApplyRestorePilotSaveImage1(ttmThread))
                    grSaveImage1(ttmThread->ttmLayer, args[0], args[1], args[2], args[3]);
 #else
                grSaveImage1(ttmThread->ttmLayer, args[0], args[1], args[2], args[3]);
 #endif
                break;

            case 0xA002:
                debugMsg("    DRAW_PIXEL %d %d", args[0], args[1]);
                grDrawPixel(ttmThread->ttmLayer, args[0], args[1], ttmThread->fgColor);
                break;

            case 0xA054:
                // only once, in GJGULIVR.TTM.txt
                debugMsg("    SAVE_ZONE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grSaveZone(ttmThread->ttmLayer, args[0], args[1], args[2], args[3]);
                break;

            case 0xA064:
                // only once, in GJGULIVR.TTM.txt
                debugMsg("    RESTORE_ZONE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grRestoreZone(ttmThread->ttmLayer, args[0], args[1], args[2], args[3]);
                break;

            case 0xA0A4:
                debugMsg("    DRAW_LINE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grDrawLine(ttmThread->ttmLayer, args[0], args[1], args[2], args[3], ttmThread->fgColor);
                break;

            case 0xA104:
                debugMsg("    DRAW_RECT %d %d %d %d", args[0], args[1], args[2], args[3]);
                grDrawRect(ttmThread->ttmLayer, args[0], args[1], args[2], args[3], ttmThread->fgColor);
                break;

            case 0xA404:
                debugMsg("    DRAW_CIRCLE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grDrawCircle(ttmThread->ttmLayer, args[0], args[1], args[2], args[3], ttmThread->fgColor, ttmThread->bgColor);
                break;

            case 0xA504:
                debugMsg("    DRAW_SPRITE %d %d %d %d", args[0], args[1], args[2], args[3]);
#ifdef PS1_BUILD
                if (gTtmStaticBaseBuildMode &&
                    !ttmBmpAllowedInStaticBase((args[3] < MAX_BMP_SLOTS) ? ttmSlot->loadedBmpNames[args[3]] : NULL)) {
                    break;
                }
#endif
                grDrawSprite(ttmThread->ttmLayer, ttmThread->ttmSlot, args[0], args[1], args[2], args[3]);
                break;

            case 0xA524:
                debugMsg("    DRAW_SPRITE_FLIP %d %d %d %d", args[0], args[1], args[2], args[3]);
#ifdef PS1_BUILD
                if (gTtmStaticBaseBuildMode &&
                    !ttmBmpAllowedInStaticBase((args[3] < MAX_BMP_SLOTS) ? ttmSlot->loadedBmpNames[args[3]] : NULL)) {
                    break;
                }
#endif
                grDrawSpriteFlip(ttmThread->ttmLayer, ttmThread->ttmSlot, args[0], args[1], args[2], args[3]);
                break;

            case 0xA601:
                // arg : indicates the SAVE_IMAGE1 nb to be used ?
                debugMsg("    CLEAR_SCREEN %d", args[0]);
#ifdef PS1_BUILD
                if (ttmIsBuildingMjsandProbe(ttmThread))
                    break;
                if (ttmApplyRestorePilotClear(ttmThread, args[0]))
                    break;

                /* Default PS1 path still suppresses generic clears. The pilot
                 * hook above is intentionally scene-scoped and TTM-scoped. */
#else
                grClearScreen(ttmThread->ttmLayer);
#endif
#ifdef PS1_BUILD
                /* Keep replay records on PS1. CLEAR_SCREEN semantics are tied to
                 * SDL layer clears; wiping records here causes Johnny to vanish
                 * during delayed scene ticks (e.g. fire-building sequence). */
#endif
                break;

            case 0xB606:
                debugMsg("    DRAW_SCREEN %d %d %d %d %d %d", args[0], args[1], args[2], args[3], args[4], args[5]);
                break;

            case 0xC051:
                debugMsg("    PLAY_SAMPLE %d", args[0]);
                soundPlay(args[0]);
                break;

            case 0xF01F:
                debugMsg("    LOAD_SCREEN %s", strArg);
                if (!gTtmStaticBaseBuildMode)
                    grLoadScreen(strArg);
                break;

            case 0xF02F:
                debugMsg("    LOAD_IMAGE %s", strArg);
                grLoadBmp(ttmSlot, ttmThread->selectedBmpSlot, strArg);
                break;

            case 0xF05F:
                debugMsg("    LOAD_PALETTE %s", strArg);
                break;

            default:
                debugMsg("    UNKNOWN OPCODE 0x%04X", opcode);
                break;
        }

        if (offset >= ttmSlot->dataSize) {
            ttmThread->isRunning = 2;
            continueLoop = 0;
        }
    }

    ttmThread->ip = offset;
}

void ttmSetStaticBaseBuildMode(int enabled)
{
    gTtmStaticBaseBuildMode = enabled ? 1 : 0;
}
