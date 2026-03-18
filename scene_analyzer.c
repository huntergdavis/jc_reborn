/*
 * Scene Resource Analyzer for Johnny Castaway PS1 Port
 *
 * Standalone desktop tool that statically analyzes every scene to determine:
 *   - Peak sprite count per scene
 *   - Memory usage (BMP pixels, TTM bytecode, ADS scripts)
 *   - Concurrent TTM threads/slots
 *   - Which BMP/TTM/SCR resources each scene uses
 *
 * Build: make -f Makefile.analyzer  (from jc_resources/ directory)
 * Usage: ./scene_analyzer
 *
 * Requires RESOURCE.MAP and extracted/ directory in working directory.
 */

#define _GNU_SOURCE  /* for strcasecmp */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include "mytypes.h"
#include "utils.h"
#include "resource.h"

/* ---- Import story_data.h scene table ---- */
#include "story_data.h"

/* ---- Constants matching the PS1 port ---- */
#define MAX_BMP_SLOTS       6
#define MAX_SPRITES_PER_BMP 255
#define MAX_TTM_SLOTS       10
#define MAX_TTM_THREADS     20
#define PS1_MEMORY_BUDGET   (600 * 1024)
#define PS1_POINTER_SIZE    4
#define PS1_TTM_SLOT_OVERHEAD_BYTES 8192

/* ---- Per-scene analysis results ---- */

#define MAX_BMPS_PER_SCENE  32
#define MAX_SCRS_PER_SCENE  4
#define MAX_TTMS_PER_SCENE  10
#define MAX_THREADS_PER_SCENE 20  /* all possible ADD_SCENE calls */
#define MAX_UNIQUE_BMPS     200
#define MAX_UNIQUE_TTMS     100
#define MAX_CLUSTER_GROUPS  32
#define TOP_HEAVY_SCENES    10
#define TOP_TRANSITIONS     10

struct TBmpRef {
    char name[16];
    struct TBmpResource *bmp;  /* NULL if not found */
    int ttmSlot;               /* which TTM slot loaded this */
    int bmpSlot;               /* which BMP slot within TTM */
};

struct TScrRef {
    char name[16];
    struct TScrResource *scr;
};

struct TTtmRef {
    char name[16];
    struct TTtmResource *ttm;
    int slotId;
};

struct TThreadRef {
    int ttmSlot;
    int ttmTag;
    int numPlays;
    int isRandom;     /* from RANDOM block */
    int weight;       /* random weight */
};

struct TSceneAnalysis {
    /* Scene identity */
    char adsName[16];
    int  adsTag;
    int  sceneIndex;  /* index in storyScenes[] */

    /* Resources discovered */
    struct TBmpRef bmps[MAX_BMPS_PER_SCENE];
    int numBmps;

    struct TScrRef scrs[MAX_SCRS_PER_SCENE];
    int numScrs;

    struct TTtmRef ttms[MAX_TTMS_PER_SCENE];
    int numTtms;

    struct TThreadRef threads[MAX_THREADS_PER_SCENE];
    int numThreads;

    /* Computed metrics */
    uint32 totalBmpMemIndexed;   /* 4-bit indexed pixel bytes */
    uint32 totalBmpMem15bit;     /* 15-bit direct pixel bytes */
    uint32 totalBmpUncompressed; /* raw BMP data from resource */
    uint32 totalTtmBytes;        /* TTM bytecode bytes */
    uint32 totalAdsBytes;        /* ADS script bytes */
    uint32 totalScrBytes;        /* SCR background bytes */
    uint32 totalSpriteFrames;    /* total sprite frames across all BMPs */
    uint32 spritePointerBytes;   /* explicit PS1 pointer table bytes */
    uint32 ttmSlotOverheadBytes; /* explicit TTM slot/runtime overhead */
    int    maxConcurrentThreads; /* worst case concurrent threads */
    uint32 peakMemory;           /* estimated peak memory usage */
};

struct TBmpInventoryItem {
    char name[16];
    int refCount;
    uint32 indexedBytes;
    uint32 rawBytes;
    uint16 numFrames;
    uint16 w;
    uint16 h;
};

struct TTtmInventoryItem {
    char name[16];
    uint32 bytes;
    int refCount;
};

struct TSceneCluster {
    char adsName[16];
    int sceneIndices[NUM_SCENES];
    int numScenes;
    uint32 maxPeakMemory;
    uint32 totalPeakMemory;
};

struct TTransitionDelta {
    int fromSceneIndex;
    int toSceneIndex;
    int addedBmps;
    int removedBmps;
    int addedScrs;
    int removedScrs;
    int addedTtms;
    int removedTtms;
    int totalResourceChurn;
    uint32 peakMemoryDelta;
};

/* ---- Globals ---- */
extern struct TAdsResource *adsResources[];
extern struct TBmpResource *bmpResources[];
extern struct TScrResource *scrResources[];
extern struct TTtmResource *ttmResources[];
extern int numAdsResources;
extern int numBmpResources;
extern int numScrResources;
extern int numTtmResources;

static struct TSceneAnalysis sceneResults[NUM_SCENES];

struct TAnalyzerOptions {
    int jsonOutput;
};

/* ---- Helper: deduplicate BMP name ---- */
static int addBmpRef(struct TSceneAnalysis *sa, const char *name, int ttmSlot, int bmpSlot)
{
    for (int i = 0; i < sa->numBmps; i++) {
        if (strcasecmp(sa->bmps[i].name, name) == 0)
            return i;  /* already tracked */
    }
    if (sa->numBmps >= MAX_BMPS_PER_SCENE) return -1;

    int idx = sa->numBmps++;
    strncpy(sa->bmps[idx].name, name, 15);
    sa->bmps[idx].name[15] = 0;
    sa->bmps[idx].bmp = findBmpResource((char*)name);
    sa->bmps[idx].ttmSlot = ttmSlot;
    sa->bmps[idx].bmpSlot = bmpSlot;
    return idx;
}

static int addScrRef(struct TSceneAnalysis *sa, const char *name)
{
    for (int i = 0; i < sa->numScrs; i++) {
        if (strcasecmp(sa->scrs[i].name, name) == 0)
            return i;
    }
    if (sa->numScrs >= MAX_SCRS_PER_SCENE) return -1;

    int idx = sa->numScrs++;
    strncpy(sa->scrs[idx].name, name, 15);
    sa->scrs[idx].name[15] = 0;
    sa->scrs[idx].scr = findScrResource((char*)name);
    return idx;
}

static int addTtmRef(struct TSceneAnalysis *sa, const char *name, int slotId)
{
    for (int i = 0; i < sa->numTtms; i++) {
        if (strcasecmp(sa->ttms[i].name, name) == 0)
            return i;
    }
    if (sa->numTtms >= MAX_TTMS_PER_SCENE) return -1;

    int idx = sa->numTtms++;
    strncpy(sa->ttms[idx].name, name, 15);
    sa->ttms[idx].name[15] = 0;
    sa->ttms[idx].ttm = findTtmResource((char*)name);
    sa->ttms[idx].slotId = slotId;
    return idx;
}

/* ---- Load extracted data for a resource if not already loaded ---- */
static void ensureAdsLoaded(struct TAdsResource *ads)
{
    if (ads->uncompressedData != NULL) return;

    char path[512];
    snprintf(path, sizeof(path), "extracted/ads/%s", ads->resName);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Warning: Cannot open %s\n", path);
        return;
    }
    ads->uncompressedData = malloc(ads->uncompressedSize);
    if (fread(ads->uncompressedData, 1, ads->uncompressedSize, f) != ads->uncompressedSize) {
        fprintf(stderr, "Warning: Short read on %s\n", path);
        free(ads->uncompressedData);
        ads->uncompressedData = NULL;
    }
    fclose(f);
}

static void ensureTtmLoaded(struct TTtmResource *ttm)
{
    if (ttm->uncompressedData != NULL) return;

    char path[512];
    snprintf(path, sizeof(path), "extracted/ttm/%s", ttm->resName);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Warning: Cannot open %s\n", path);
        return;
    }
    ttm->uncompressedData = malloc(ttm->uncompressedSize);
    if (fread(ttm->uncompressedData, 1, ttm->uncompressedSize, f) != ttm->uncompressedSize) {
        fprintf(stderr, "Warning: Short read on %s\n", path);
        free(ttm->uncompressedData);
        ttm->uncompressedData = NULL;
    }
    fclose(f);
}

/* ---- Scan TTM bytecode for LOAD_IMAGE and LOAD_SCREEN instructions ---- */
/* We scan the ENTIRE TTM to find ALL possible resource loads (any tag),
 * since ADD_SCENE can jump to any tag and tags can GOTO_TAG other tags.
 * This gives worst-case resource usage per TTM. */
static void scanTtmForResources(struct TSceneAnalysis *sa,
                                struct TTtmResource *ttmRes,
                                int ttmSlotId)
{
    if (!ttmRes || !ttmRes->uncompressedData) return;

    uint8 *data = ttmRes->uncompressedData;
    uint32 dataSize = ttmRes->uncompressedSize;
    uint32 offset = 0;
    int currentBmpSlot = 0;

    while (offset < dataSize) {
        if (offset + 1 >= dataSize) break;

        uint16 opcode = peekUint16(data, &offset);
        uint8 numArgs = (uint8)(opcode & 0x000f);

        if (numArgs == 0x0f) {
            /* String argument */
            char strArg[20];
            int i = 0;
            while (offset < dataSize && data[offset] != 0)
                strArg[i++] = data[offset++];
            strArg[i++] = data[offset++];  /* null terminator */
            if ((i & 1) == 1 && offset < dataSize)
                offset++;  /* pad to even */

            if (opcode == 0xF02F) {
                /* LOAD_IMAGE */
                addBmpRef(sa, strArg, ttmSlotId, currentBmpSlot);
            }
            else if (opcode == 0xF01F) {
                /* LOAD_SCREEN */
                addScrRef(sa, strArg);
            }
        }
        else {
            /* Numeric arguments */
            uint16 args[10];
            if (numArgs > 0 && numArgs <= 10) {
                peekUint16Block(data, &offset, args, numArgs);
            }

            if (opcode == 0x1051 && numArgs >= 1) {
                /* SET_BMP_SLOT */
                currentBmpSlot = args[0];
                if (currentBmpSlot >= MAX_BMP_SLOTS)
                    currentBmpSlot = MAX_BMP_SLOTS - 1;
            }
        }
    }
}


/* ---- Parse ADS bytecode to extract all ADD_SCENE calls for a given tag ---- */
static void scanAdsForScenes(struct TSceneAnalysis *sa,
                             struct TAdsResource *ads,
                             uint16 targetAdsTag)
{
    if (!ads || !ads->uncompressedData) return;

    uint8 *data = ads->uncompressedData;
    uint32 dataSize = ads->uncompressedSize;

    /* First: find all tag offsets in ADS data */
    struct { uint16 id; uint32 offset; } adsTags[64];
    int numAdsTags = 0;
    uint32 targetOffset = 0;

    {
        uint32 offset = 0;
        while (offset < dataSize) {
            uint16 opcode = peekUint16(data, &offset);
            switch (opcode) {
                case 0x1070: offset += 4; break;
                case 0x1330: offset += 4; break;
                case 0x1350: offset += 4; break;
                case 0x1360: offset += 4; break;
                case 0x1370: offset += 4; break;
                case 0x1420: break;
                case 0x1430: break;
                case 0x1510: break;
                case 0x1520: offset += 10; break;
                case 0x2005: offset += 8; break;
                case 0x2010: offset += 6; break;
                case 0x2014: break;
                case 0x3010: break;
                case 0x3020: offset += 2; break;
                case 0x30ff: break;
                case 0x4000: offset += 6; break;
                case 0xf010: break;
                case 0xf200: offset += 2; break;
                case 0xffff: break;
                case 0xfff0: break;
                default:
                    /* It's a tag number */
                    if (numAdsTags < 64) {
                        adsTags[numAdsTags].id = opcode;
                        adsTags[numAdsTags].offset = offset;
                        numAdsTags++;
                    }
                    if (opcode == targetAdsTag)
                        targetOffset = offset;
                    break;
            }
        }
    }

    if (targetOffset == 0) {
        fprintf(stderr, "Warning: ADS tag %d not found in %s\n", targetAdsTag, ads->resName);
        return;
    }

    /* Second: scan from targetOffset, collecting all ADD_SCENE calls.
     * We also follow GOSUB_TAG (0xf200) to find scenes in subroutine tags.
     * For RANDOM blocks, we collect ALL possible options (worst case). */
    uint32 chunksToScan[64];
    int numChunksToScan = 1;
    int chunksScanned = 0;
    chunksToScan[0] = targetOffset;

    /* Also scan all IF_LASTPLAYED / IF_NOT_RUNNING chunks that were
     * bookmarked under this tag (they are between this tag and the next) */
    {
        uint32 nextTagOffset = dataSize;
        for (int t = 0; t < numAdsTags; t++) {
            if (adsTags[t].offset > targetOffset && adsTags[t].offset < nextTagOffset)
                nextTagOffset = adsTags[t].offset;
        }

        /* Scan the region between targetOffset and nextTagOffset
         * for IF_LASTPLAYED (0x1350) and IF_NOT_RUNNING (0x1360) chunks */
        uint32 off = targetOffset;
        while (off < nextTagOffset && off < dataSize) {
            uint16 op = peekUint16(data, &off);
            switch (op) {
                case 0x1350: {
                    uint16 args[2];
                    peekUint16Block(data, &off, args, 2);
                    /* The offset AFTER the args is the chunk start */
                    if (numChunksToScan < 64)
                        chunksToScan[numChunksToScan++] = off;
                    break;
                }
                case 0x1360: {
                    uint16 args[2];
                    peekUint16Block(data, &off, args, 2);
                    if (numChunksToScan < 64)
                        chunksToScan[numChunksToScan++] = off;
                    break;
                }
                case 0x1070: off += 4; break;
                case 0x1330: off += 4; break;
                case 0x1370: off += 4; break;
                case 0x1420: break;
                case 0x1430: break;
                case 0x1510: break;
                case 0x1520: off += 10; break;
                case 0x2005: off += 8; break;
                case 0x2010: off += 6; break;
                case 0x2014: break;
                case 0x3010: break;
                case 0x3020: off += 2; break;
                case 0x30ff: break;
                case 0x4000: off += 6; break;
                case 0xf010: break;
                case 0xf200: off += 2; break;
                case 0xffff: break;
                case 0xfff0: break;
                default: break;  /* another tag - stop */
            }
        }
    }

    /* Now scan each chunk to find ADD_SCENE calls */
    while (chunksScanned < numChunksToScan) {
        uint32 offset = chunksToScan[chunksScanned++];
        int inRandBlock = 0;

        while (offset < dataSize) {
            uint16 opcode = peekUint16(data, &offset);

            switch (opcode) {
                case 0x1070: offset += 4; break;
                case 0x1330: offset += 4; break;

                case 0x1350:
                    /* IF_LASTPLAYED - ends this chunk's linear flow */
                    offset += 4;
                    goto next_chunk;

                case 0x1360: offset += 4; break;
                case 0x1370: offset += 4; break;
                case 0x1420: break;
                case 0x1430: break;

                case 0x1510:
                    /* PLAY_SCENE - ends this chunk */
                    goto next_chunk;

                case 0x1520: {
                    /* ADD_SCENE_LOCAL */
                    uint16 args[5];
                    peekUint16Block(data, &offset, args, 5);
                    if (sa->numThreads < MAX_THREADS_PER_SCENE) {
                        sa->threads[sa->numThreads].ttmSlot = args[1];
                        sa->threads[sa->numThreads].ttmTag = args[2];
                        sa->threads[sa->numThreads].numPlays = args[3];
                        sa->threads[sa->numThreads].isRandom = 0;
                        sa->threads[sa->numThreads].weight = 0;
                        sa->numThreads++;
                    }
                    break;
                }

                case 0x2005: {
                    /* ADD_SCENE - the main thing we're looking for */
                    uint16 args[4];
                    peekUint16Block(data, &offset, args, 4);
                    if (sa->numThreads < MAX_THREADS_PER_SCENE) {
                        sa->threads[sa->numThreads].ttmSlot = args[0];
                        sa->threads[sa->numThreads].ttmTag = args[1];
                        sa->threads[sa->numThreads].numPlays = args[2];
                        sa->threads[sa->numThreads].isRandom = inRandBlock;
                        sa->threads[sa->numThreads].weight = args[3];
                        sa->numThreads++;
                    }
                    break;
                }

                case 0x2010: offset += 6; break;  /* STOP_SCENE */
                case 0x2014: break;
                case 0x3010: inRandBlock = 1; break;  /* RANDOM_START */
                case 0x3020: offset += 2; break;      /* NOP weight */
                case 0x30ff: inRandBlock = 0; break;  /* RANDOM_END */
                case 0x4000: offset += 6; break;

                case 0xf010: break;  /* FADE_OUT */

                case 0xf200: {
                    /* GOSUB_TAG - follow the jump */
                    uint16 args[1];
                    peekUint16Block(data, &offset, args, 1);
                    for (int t = 0; t < numAdsTags; t++) {
                        if (adsTags[t].id == args[0]) {
                            if (numChunksToScan < 64)
                                chunksToScan[numChunksToScan++] = adsTags[t].offset;
                            break;
                        }
                    }
                    break;
                }

                case 0xffff:
                    goto next_chunk;  /* END */

                case 0xfff0: break;

                default:
                    /* Another tag number - means we ran into a different tag */
                    goto next_chunk;
            }
        }
next_chunk:;
    }
}

/* ---- Calculate memory metrics for a scene ---- */
static void calculateMetrics(struct TSceneAnalysis *sa)
{
    /* BMP memory */
    for (int i = 0; i < sa->numBmps; i++) {
        struct TBmpResource *bmp = sa->bmps[i].bmp;
        if (!bmp) continue;

        sa->totalBmpUncompressed += bmp->uncompressedSize;

        /* Calculate pixel memory for all frames */
        for (int f = 0; f < bmp->numImages; f++) {
            uint16 w = bmp->widths ? bmp->widths[f] : bmp->width;
            uint16 h = bmp->heights ? bmp->heights[f] : bmp->height;
            uint32 pixels = (uint32)w * h;

            /* 4-bit indexed: 0.5 bytes/pixel (nibble-packed) */
            sa->totalBmpMemIndexed += (pixels + 1) / 2;

            /* 15-bit direct: 2 bytes/pixel */
            sa->totalBmpMem15bit += pixels * 2;

            sa->totalSpriteFrames++;
        }
    }

    /* TTM bytecode memory - only count TTMs whose slots are used */
    {
        int usedSlots[MAX_TTM_SLOTS];
        memset(usedSlots, 0, sizeof(usedSlots));
        for (int t = 0; t < sa->numThreads; t++) {
            int slot = sa->threads[t].ttmSlot;
            if (slot >= 0 && slot < MAX_TTM_SLOTS)
                usedSlots[slot] = 1;
        }
        for (int i = 0; i < sa->numTtms; i++) {
            if (sa->ttms[i].ttm && usedSlots[sa->ttms[i].slotId])
                sa->totalTtmBytes += sa->ttms[i].ttm->uncompressedSize;
        }
    }

    /* SCR memory */
    for (int i = 0; i < sa->numScrs; i++) {
        if (sa->scrs[i].scr)
            sa->totalScrBytes += sa->scrs[i].scr->uncompressedSize;
    }

    /* ADS memory */
    /* Find the ADS resource */
    struct TAdsResource *ads = findAdsResource(sa->adsName);
    if (ads)
        sa->totalAdsBytes = ads->uncompressedSize;

    /* Count max concurrent threads (non-random threads always run;
     * for random blocks, assume worst case = largest single option) */
    int nonRandomThreads = 0;
    int maxRandomGroupSize = 0;
    int currentRandomGroup = 0;

    for (int i = 0; i < sa->numThreads; i++) {
        if (sa->threads[i].isRandom) {
            currentRandomGroup++;
        } else {
            if (currentRandomGroup > maxRandomGroupSize)
                maxRandomGroupSize = currentRandomGroup;
            currentRandomGroup = 0;
            nonRandomThreads++;
        }
    }
    if (currentRandomGroup > maxRandomGroupSize)
        maxRandomGroupSize = currentRandomGroup;

    /* Worst case: all non-random threads + 1 random pick
     * But the ADS system can run up to MAX_TTM_THREADS=20 */
    sa->maxConcurrentThreads = nonRandomThreads + (maxRandomGroupSize > 0 ? 1 : 0);
    if (sa->maxConcurrentThreads < 1)
        sa->maxConcurrentThreads = 1;  /* At least 1 thread runs */

    /* Peak memory estimate (PS1 4-bit indexed format) */
    sa->spritePointerBytes = sa->totalSpriteFrames * PS1_POINTER_SIZE;
    sa->ttmSlotOverheadBytes = sa->numTtms * PS1_TTM_SLOT_OVERHEAD_BYTES;
    sa->peakMemory = sa->totalBmpMemIndexed  /* sprite pixel data */
                   + sa->totalTtmBytes       /* TTM bytecode */
                   + sa->totalAdsBytes       /* ADS script */
                   + sa->totalScrBytes       /* background screen */
                   + sa->spritePointerBytes  /* explicit PS1 pointer tables */
                   + sa->ttmSlotOverheadBytes; /* TTM slot overhead */
}

/* ---- Analyze a single scene ---- */
static void analyzeScene(int sceneIndex)
{
    struct TSceneAnalysis *sa = &sceneResults[sceneIndex];
    memset(sa, 0, sizeof(*sa));

    strncpy(sa->adsName, storyScenes[sceneIndex].adsName, 15);
    sa->adsName[15] = 0;
    sa->adsTag = storyScenes[sceneIndex].adsTagNo;
    sa->sceneIndex = sceneIndex;

    /* Find ADS resource */
    struct TAdsResource *ads = findAdsResource(sa->adsName);
    if (!ads) {
        fprintf(stderr, "Warning: ADS resource %s not found\n", sa->adsName);
        return;
    }

    /* Ensure ADS data is loaded */
    ensureAdsLoaded(ads);
    if (!ads->uncompressedData) return;

    /* Record all TTM files referenced by this ADS */
    for (int i = 0; i < ads->numRes; i++) {
        struct TTtmResource *ttm = findTtmResource(ads->res[i].name);
        if (ttm) {
            addTtmRef(sa, ads->res[i].name, ads->res[i].id);
            ensureTtmLoaded(ttm);
        }
    }

    /* Scan ADS bytecode to find all ADD_SCENE calls for this tag */
    scanAdsForScenes(sa, ads, (uint16)sa->adsTag);

    /* Only scan TTMs whose slot IDs are actually used by ADD_SCENE calls.
     * This gives per-tag accuracy instead of per-ADS-file worst case. */
    {
        int usedSlots[MAX_TTM_SLOTS];
        memset(usedSlots, 0, sizeof(usedSlots));
        for (int t = 0; t < sa->numThreads; t++) {
            int slot = sa->threads[t].ttmSlot;
            if (slot >= 0 && slot < MAX_TTM_SLOTS)
                usedSlots[slot] = 1;
        }
        for (int i = 0; i < sa->numTtms; i++) {
            if (sa->ttms[i].ttm && usedSlots[sa->ttms[i].slotId])
                scanTtmForResources(sa, sa->ttms[i].ttm, sa->ttms[i].slotId);
        }
    }

    /* Also add JOHNWALK.BMP since adsPlayWalk always loads it */
    /* (walk scenes use it implicitly) */
    if (strstr(sa->adsName, "WALKSTUF") != NULL) {
        addBmpRef(sa, "JOHNWALK.BMP", 0, 0);
    }

    /* Calculate all metrics */
    calculateMetrics(sa);
}

/* ---- Flag names for display ---- */
static void flagsToString(int flags, char *buf, int bufLen)
{
    buf[0] = 0;
    if (flags & FINAL)       strncat(buf, "FINAL ", bufLen - strlen(buf) - 1);
    if (flags & FIRST)       strncat(buf, "FIRST ", bufLen - strlen(buf) - 1);
    if (flags & ISLAND)      strncat(buf, "ISLAND ", bufLen - strlen(buf) - 1);
    if (flags & LEFT_ISLAND) strncat(buf, "LEFT_ISLAND ", bufLen - strlen(buf) - 1);
    if (flags & VARPOS_OK)   strncat(buf, "VARPOS ", bufLen - strlen(buf) - 1);
    if (flags & LOWTIDE_OK)  strncat(buf, "LOWTIDE ", bufLen - strlen(buf) - 1);
    if (flags & NORAFT)      strncat(buf, "NORAFT ", bufLen - strlen(buf) - 1);
    if (flags & HOLIDAY_NOK) strncat(buf, "HOLIDAY_NOK ", bufLen - strlen(buf) - 1);
}

static void jsonIndent(FILE *out, int depth)
{
    for (int i = 0; i < depth; i++)
        fputs("  ", out);
}

static void jsonPrintEscaped(FILE *out, const char *str)
{
    const unsigned char *p = (const unsigned char *)(str ? str : "");

    fputc('"', out);
    while (*p) {
        switch (*p) {
            case '\\': fputs("\\\\", out); break;
            case '"':  fputs("\\\"", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (*p < 0x20)
                    fprintf(out, "\\u%04x", *p);
                else
                    fputc(*p, out);
                break;
        }
        p++;
    }
    fputc('"', out);
}

static void jsonPrintStringField(FILE *out, int depth, const char *key, const char *value, int trailingComma)
{
    jsonIndent(out, depth);
    jsonPrintEscaped(out, key);
    fputs(": ", out);
    jsonPrintEscaped(out, value);
    if (trailingComma) fputc(',', out);
    fputc('\n', out);
}

static void jsonPrintIntField(FILE *out, int depth, const char *key, int value, int trailingComma)
{
    jsonIndent(out, depth);
    jsonPrintEscaped(out, key);
    fprintf(out, ": %d", value);
    if (trailingComma) fputc(',', out);
    fputc('\n', out);
}

static void jsonPrintUintField(FILE *out, int depth, const char *key, uint32 value, int trailingComma)
{
    jsonIndent(out, depth);
    jsonPrintEscaped(out, key);
    fprintf(out, ": %u", value);
    if (trailingComma) fputc(',', out);
    fputc('\n', out);
}

static const char *spotToString(int spot)
{
    switch (spot) {
        case SPOT_A: return "SPOT_A";
        case SPOT_B: return "SPOT_B";
        case SPOT_C: return "SPOT_C";
        case SPOT_D: return "SPOT_D";
        case SPOT_E: return "SPOT_E";
        case SPOT_F: return "SPOT_F";
        default: return "NONE";
    }
}

static const char *headingToString(int hdg)
{
    switch (hdg) {
        case HDG_S: return "HDG_S";
        case HDG_SW: return "HDG_SW";
        case HDG_W: return "HDG_W";
        case HDG_NW: return "HDG_NW";
        case HDG_N: return "HDG_N";
        case HDG_NE: return "HDG_NE";
        case HDG_E: return "HDG_E";
        case HDG_SE: return "HDG_SE";
        default: return "NONE";
    }
}

static void jsonPrintFlagsArray(FILE *out, int depth, int flags)
{
    int first = 1;

    jsonIndent(out, depth);
    fputs("\"flags\": [", out);
    if (flags & FINAL)       { if (!first) fputs(", ", out); jsonPrintEscaped(out, "FINAL"); first = 0; }
    if (flags & FIRST)       { if (!first) fputs(", ", out); jsonPrintEscaped(out, "FIRST"); first = 0; }
    if (flags & ISLAND)      { if (!first) fputs(", ", out); jsonPrintEscaped(out, "ISLAND"); first = 0; }
    if (flags & LEFT_ISLAND) { if (!first) fputs(", ", out); jsonPrintEscaped(out, "LEFT_ISLAND"); first = 0; }
    if (flags & VARPOS_OK)   { if (!first) fputs(", ", out); jsonPrintEscaped(out, "VARPOS_OK"); first = 0; }
    if (flags & LOWTIDE_OK)  { if (!first) fputs(", ", out); jsonPrintEscaped(out, "LOWTIDE_OK"); first = 0; }
    if (flags & NORAFT)      { if (!first) fputs(", ", out); jsonPrintEscaped(out, "NORAFT"); first = 0; }
    if (flags & HOLIDAY_NOK) { if (!first) fputs(", ", out); jsonPrintEscaped(out, "HOLIDAY_NOK"); first = 0; }
    fputs("],\n", out);
}

static void buildSortedSceneIndices(int sorted[NUM_SCENES])
{
    for (int i = 0; i < NUM_SCENES; i++) sorted[i] = i;

    for (int i = 0; i < NUM_SCENES - 1; i++) {
        for (int j = i + 1; j < NUM_SCENES; j++) {
            if (sceneResults[sorted[j]].peakMemory > sceneResults[sorted[i]].peakMemory) {
                int tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
}

static int collectBmpInventory(struct TBmpInventoryItem items[MAX_UNIQUE_BMPS])
{
    int bmpCount = 0;

    for (int i = 0; i < NUM_SCENES; i++) {
        struct TSceneAnalysis *sa = &sceneResults[i];
        for (int b = 0; b < sa->numBmps; b++) {
            int found = 0;
            for (int k = 0; k < bmpCount; k++) {
                if (strcasecmp(items[k].name, sa->bmps[b].name) == 0) {
                    items[k].refCount++;
                    found = 1;
                    break;
                }
            }
            if (!found && bmpCount < MAX_UNIQUE_BMPS) {
                struct TBmpResource *bmp = sa->bmps[b].bmp;

                strncpy(items[bmpCount].name, sa->bmps[b].name, 15);
                items[bmpCount].name[15] = 0;
                items[bmpCount].refCount = 1;
                items[bmpCount].indexedBytes = 0;
                items[bmpCount].rawBytes = 0;
                items[bmpCount].numFrames = 0;
                items[bmpCount].w = 0;
                items[bmpCount].h = 0;

                if (bmp) {
                    for (int f = 0; f < bmp->numImages; f++) {
                        uint16 w = bmp->widths ? bmp->widths[f] : bmp->width;
                        uint16 h = bmp->heights ? bmp->heights[f] : bmp->height;
                        items[bmpCount].indexedBytes += ((uint32)w * h + 1) / 2;
                        if (w > items[bmpCount].w) items[bmpCount].w = w;
                        if (h > items[bmpCount].h) items[bmpCount].h = h;
                    }
                    items[bmpCount].rawBytes = bmp->uncompressedSize;
                    items[bmpCount].numFrames = bmp->numImages;
                }
                bmpCount++;
            }
        }
    }

    for (int i = 0; i < bmpCount - 1; i++) {
        for (int j = i + 1; j < bmpCount; j++) {
            if (items[j].indexedBytes > items[i].indexedBytes) {
                struct TBmpInventoryItem tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }

    return bmpCount;
}

static int collectTtmInventory(struct TTtmInventoryItem items[MAX_UNIQUE_TTMS])
{
    int ttmCount = 0;

    for (int i = 0; i < NUM_SCENES; i++) {
        struct TSceneAnalysis *sa = &sceneResults[i];
        for (int t = 0; t < sa->numTtms; t++) {
            int found = 0;
            for (int k = 0; k < ttmCount; k++) {
                if (strcasecmp(items[k].name, sa->ttms[t].name) == 0) {
                    items[k].refCount++;
                    found = 1;
                    break;
                }
            }
            if (!found && ttmCount < MAX_UNIQUE_TTMS) {
                strncpy(items[ttmCount].name, sa->ttms[t].name, 15);
                items[ttmCount].name[15] = 0;
                items[ttmCount].refCount = 1;
                items[ttmCount].bytes = sa->ttms[t].ttm ? sa->ttms[t].ttm->uncompressedSize : 0;
                ttmCount++;
            }
        }
    }

    return ttmCount;
}

static int collectSceneClusters(struct TSceneCluster clusters[MAX_CLUSTER_GROUPS])
{
    int clusterCount = 0;

    for (int i = 0; i < NUM_SCENES; i++) {
        int clusterIndex = -1;
        struct TSceneAnalysis *sa = &sceneResults[i];

        for (int c = 0; c < clusterCount; c++) {
            if (strcasecmp(clusters[c].adsName, sa->adsName) == 0) {
                clusterIndex = c;
                break;
            }
        }

        if (clusterIndex < 0 && clusterCount < MAX_CLUSTER_GROUPS) {
            clusterIndex = clusterCount++;
            memset(&clusters[clusterIndex], 0, sizeof(clusters[clusterIndex]));
            strncpy(clusters[clusterIndex].adsName, sa->adsName, 15);
            clusters[clusterIndex].adsName[15] = 0;
        }

        if (clusterIndex >= 0) {
            struct TSceneCluster *cluster = &clusters[clusterIndex];
            cluster->sceneIndices[cluster->numScenes++] = i;
            cluster->totalPeakMemory += sa->peakMemory;
            if (sa->peakMemory > cluster->maxPeakMemory)
                cluster->maxPeakMemory = sa->peakMemory;
        }
    }

    return clusterCount;
}

static int sceneHasBmp(const struct TSceneAnalysis *sa, const char *name)
{
    for (int i = 0; i < sa->numBmps; i++)
        if (strcasecmp(sa->bmps[i].name, name) == 0)
            return 1;
    return 0;
}

static int sceneHasScr(const struct TSceneAnalysis *sa, const char *name)
{
    for (int i = 0; i < sa->numScrs; i++)
        if (strcasecmp(sa->scrs[i].name, name) == 0)
            return 1;
    return 0;
}

static int sceneHasTtm(const struct TSceneAnalysis *sa, const char *name)
{
    for (int i = 0; i < sa->numTtms; i++)
        if (strcasecmp(sa->ttms[i].name, name) == 0)
            return 1;
    return 0;
}

static int collectTransitionDeltas(struct TTransitionDelta deltas[NUM_SCENES - 1])
{
    int count = 0;

    for (int i = 0; i < NUM_SCENES - 1; i++) {
        const struct TSceneAnalysis *from = &sceneResults[i];
        const struct TSceneAnalysis *to = &sceneResults[i + 1];
        struct TTransitionDelta *delta = &deltas[count++];

        memset(delta, 0, sizeof(*delta));
        delta->fromSceneIndex = i;
        delta->toSceneIndex = i + 1;

        for (int b = 0; b < to->numBmps; b++)
            if (!sceneHasBmp(from, to->bmps[b].name))
                delta->addedBmps++;
        for (int b = 0; b < from->numBmps; b++)
            if (!sceneHasBmp(to, from->bmps[b].name))
                delta->removedBmps++;
        for (int s = 0; s < to->numScrs; s++)
            if (!sceneHasScr(from, to->scrs[s].name))
                delta->addedScrs++;
        for (int s = 0; s < from->numScrs; s++)
            if (!sceneHasScr(to, from->scrs[s].name))
                delta->removedScrs++;
        for (int t = 0; t < to->numTtms; t++)
            if (!sceneHasTtm(from, to->ttms[t].name))
                delta->addedTtms++;
        for (int t = 0; t < from->numTtms; t++)
            if (!sceneHasTtm(to, from->ttms[t].name))
                delta->removedTtms++;

        delta->totalResourceChurn = delta->addedBmps + delta->removedBmps
                                  + delta->addedScrs + delta->removedScrs
                                  + delta->addedTtms + delta->removedTtms;
        delta->peakMemoryDelta = (from->peakMemory > to->peakMemory)
            ? (from->peakMemory - to->peakMemory)
            : (to->peakMemory - from->peakMemory);
    }

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (deltas[j].totalResourceChurn > deltas[i].totalResourceChurn
                || (deltas[j].totalResourceChurn == deltas[i].totalResourceChurn
                    && deltas[j].peakMemoryDelta > deltas[i].peakMemoryDelta)) {
                struct TTransitionDelta tmp = deltas[i];
                deltas[i] = deltas[j];
                deltas[j] = tmp;
            }
        }
    }

    return count;
}

static void printJsonResourceArray(FILE *out, int depth, const char *key, const char *names[], int count)
{
    jsonIndent(out, depth);
    jsonPrintEscaped(out, key);
    fputs(": [", out);
    for (int i = 0; i < count; i++) {
        if (i > 0) fputs(", ", out);
        jsonPrintEscaped(out, names[i]);
    }
    fputs("]", out);
}

static void printJsonReport(void)
{
    int sortedScenes[NUM_SCENES];
    struct TBmpInventoryItem bmpInventory[MAX_UNIQUE_BMPS];
    struct TTtmInventoryItem ttmInventory[MAX_UNIQUE_TTMS];
    struct TSceneCluster clusters[MAX_CLUSTER_GROUPS];
    struct TTransitionDelta deltas[NUM_SCENES - 1];
    int bmpCount;
    int ttmCount;
    int clusterCount;
    int deltaCount;
    uint32 globalMaxMemory = 0;
    int globalMaxMemoryScene = 0;
    uint32 globalMaxSprites = 0;
    int globalMaxSpritesScene = 0;
    int globalMaxThreads = 0;
    int globalMaxThreadsScene = 0;
    uint32 globalMaxBmpMem = 0;
    int globalMaxBmpMemScene = 0;
    int budgetViolations = 0;

    buildSortedSceneIndices(sortedScenes);
    bmpCount = collectBmpInventory(bmpInventory);
    ttmCount = collectTtmInventory(ttmInventory);
    clusterCount = collectSceneClusters(clusters);
    deltaCount = collectTransitionDeltas(deltas);

    for (int i = 0; i < NUM_SCENES; i++) {
        struct TSceneAnalysis *sa = &sceneResults[i];
        if (sa->peakMemory > globalMaxMemory) {
            globalMaxMemory = sa->peakMemory;
            globalMaxMemoryScene = i;
        }
        if (sa->totalSpriteFrames > globalMaxSprites) {
            globalMaxSprites = sa->totalSpriteFrames;
            globalMaxSpritesScene = i;
        }
        if (sa->maxConcurrentThreads > globalMaxThreads) {
            globalMaxThreads = sa->maxConcurrentThreads;
            globalMaxThreadsScene = i;
        }
        if (sa->totalBmpMemIndexed > globalMaxBmpMem) {
            globalMaxBmpMem = sa->totalBmpMemIndexed;
            globalMaxBmpMemScene = i;
        }
        if (sa->peakMemory > PS1_MEMORY_BUDGET)
            budgetViolations++;
    }

    fputs("{\n", stdout);
    jsonPrintIntField(stdout, 1, "schema_version", 2, 1);
    jsonPrintStringField(stdout, 1, "generator", "scene_analyzer", 1);
    jsonPrintStringField(stdout, 1, "analysis_kind", "ps1_scene_resource_analysis", 1);

    jsonIndent(stdout, 1);
    fputs("\"assumptions\": {\n", stdout);
    jsonPrintUintField(stdout, 2, "ps1_memory_budget_bytes", PS1_MEMORY_BUDGET, 1);
    jsonPrintIntField(stdout, 2, "pointer_size_bytes", PS1_POINTER_SIZE, 1);
    jsonPrintIntField(stdout, 2, "ttm_slot_overhead_bytes", PS1_TTM_SLOT_OVERHEAD_BYTES, 1);
    jsonPrintIntField(stdout, 2, "max_bmp_slots", MAX_BMP_SLOTS, 1);
    jsonPrintIntField(stdout, 2, "max_ttm_slots", MAX_TTM_SLOTS, 1);
    jsonPrintIntField(stdout, 2, "max_ttm_threads", MAX_TTM_THREADS, 0);
    jsonIndent(stdout, 1);
    fputs("},\n", stdout);

    jsonIndent(stdout, 1);
    fputs("\"summary\": {\n", stdout);
    jsonPrintIntField(stdout, 2, "scene_count", NUM_SCENES, 1);
    jsonPrintIntField(stdout, 2, "budget_violations", budgetViolations, 1);
    jsonIndent(stdout, 2);
    fputs("\"heaviest_scene\": {\n", stdout);
    jsonPrintIntField(stdout, 3, "scene_index", globalMaxMemoryScene, 1);
    jsonPrintStringField(stdout, 3, "ads_name", sceneResults[globalMaxMemoryScene].adsName, 1);
    jsonPrintIntField(stdout, 3, "ads_tag", sceneResults[globalMaxMemoryScene].adsTag, 1);
    jsonPrintUintField(stdout, 3, "peak_memory_bytes", globalMaxMemory, 0);
    jsonIndent(stdout, 2);
    fputs("},\n", stdout);
    jsonIndent(stdout, 2);
    fputs("\"most_sprite_frames_scene\": {\n", stdout);
    jsonPrintIntField(stdout, 3, "scene_index", globalMaxSpritesScene, 1);
    jsonPrintStringField(stdout, 3, "ads_name", sceneResults[globalMaxSpritesScene].adsName, 1);
    jsonPrintIntField(stdout, 3, "ads_tag", sceneResults[globalMaxSpritesScene].adsTag, 1);
    jsonPrintUintField(stdout, 3, "sprite_frames", globalMaxSprites, 0);
    jsonIndent(stdout, 2);
    fputs("},\n", stdout);
    jsonIndent(stdout, 2);
    fputs("\"most_concurrent_threads_scene\": {\n", stdout);
    jsonPrintIntField(stdout, 3, "scene_index", globalMaxThreadsScene, 1);
    jsonPrintStringField(stdout, 3, "ads_name", sceneResults[globalMaxThreadsScene].adsName, 1);
    jsonPrintIntField(stdout, 3, "ads_tag", sceneResults[globalMaxThreadsScene].adsTag, 1);
    jsonPrintIntField(stdout, 3, "max_concurrent_threads", globalMaxThreads, 0);
    jsonIndent(stdout, 2);
    fputs("},\n", stdout);
    jsonIndent(stdout, 2);
    fputs("\"largest_bmp_indexed_scene\": {\n", stdout);
    jsonPrintIntField(stdout, 3, "scene_index", globalMaxBmpMemScene, 1);
    jsonPrintStringField(stdout, 3, "ads_name", sceneResults[globalMaxBmpMemScene].adsName, 1);
    jsonPrintIntField(stdout, 3, "ads_tag", sceneResults[globalMaxBmpMemScene].adsTag, 1);
    jsonPrintUintField(stdout, 3, "bmp_indexed_bytes", globalMaxBmpMem, 0);
    jsonIndent(stdout, 2);
    fputs("}\n", stdout);
    jsonIndent(stdout, 1);
    fputs("},\n", stdout);

    jsonIndent(stdout, 1);
    fputs("\"derived\": {\n", stdout);

    jsonIndent(stdout, 2);
    fputs("\"candidate_scene_clusters\": [\n", stdout);
    for (int c = 0; c < clusterCount; c++) {
        jsonIndent(stdout, 3);
        fputs("{\n", stdout);
        jsonPrintStringField(stdout, 4, "cluster_key", clusters[c].adsName, 1);
        jsonPrintIntField(stdout, 4, "scene_count", clusters[c].numScenes, 1);
        jsonPrintUintField(stdout, 4, "max_peak_memory_bytes", clusters[c].maxPeakMemory, 1);
        jsonPrintUintField(stdout, 4, "total_peak_memory_bytes", clusters[c].totalPeakMemory, 1);
        jsonIndent(stdout, 4);
        fputs("\"scene_indices\": [", stdout);
        for (int i = 0; i < clusters[c].numScenes; i++) {
            if (i > 0) fputs(", ", stdout);
            fprintf(stdout, "%d", clusters[c].sceneIndices[i]);
        }
        fputs("]\n", stdout);
        jsonIndent(stdout, 3);
        fputs(c + 1 < clusterCount ? "},\n" : "}\n", stdout);
    }
    jsonIndent(stdout, 2);
    fputs("],\n", stdout);

    jsonIndent(stdout, 2);
    fputs("\"shared_resources\": {\n", stdout);
    jsonIndent(stdout, 3);
    fputs("\"bmps\": [\n", stdout);
    {
        int emitted = 0;
        int sharedBmpTotal = 0;

        for (int i = 0; i < bmpCount; i++)
            if (bmpInventory[i].refCount >= 2)
                sharedBmpTotal++;

        for (int i = 0; i < bmpCount; i++) {
            if (bmpInventory[i].refCount < 2) continue;
            jsonIndent(stdout, 4);
            fputs("{\n", stdout);
            jsonPrintStringField(stdout, 5, "name", bmpInventory[i].name, 1);
            jsonPrintIntField(stdout, 5, "scene_ref_count", bmpInventory[i].refCount, 1);
            jsonPrintUintField(stdout, 5, "indexed_bytes", bmpInventory[i].indexedBytes, 1);
            jsonPrintUintField(stdout, 5, "raw_bytes", bmpInventory[i].rawBytes, 1);
            jsonPrintIntField(stdout, 5, "num_frames", bmpInventory[i].numFrames, 1);
            jsonPrintIntField(stdout, 5, "max_width", bmpInventory[i].w, 1);
            jsonPrintIntField(stdout, 5, "max_height", bmpInventory[i].h, 0);
            jsonIndent(stdout, 4);
            emitted++;
            fputs(emitted < sharedBmpTotal ? "},\n" : "}\n", stdout);
        }
    }
    jsonIndent(stdout, 3);
    fputs("],\n", stdout);
    jsonIndent(stdout, 3);
    fputs("\"ttms\": [\n", stdout);
    {
        int emitted = 0;
        int sharedTotal = 0;
        for (int i = 0; i < ttmCount; i++)
            if (ttmInventory[i].refCount >= 2)
                sharedTotal++;
        for (int i = 0; i < ttmCount; i++) {
            if (ttmInventory[i].refCount < 2) continue;
            jsonIndent(stdout, 4);
            fputs("{\n", stdout);
            jsonPrintStringField(stdout, 5, "name", ttmInventory[i].name, 1);
            jsonPrintIntField(stdout, 5, "scene_ref_count", ttmInventory[i].refCount, 1);
            jsonPrintUintField(stdout, 5, "bytes", ttmInventory[i].bytes, 0);
            jsonIndent(stdout, 4);
            emitted++;
            fputs(emitted < sharedTotal ? "},\n" : "}\n", stdout);
        }
    }
    jsonIndent(stdout, 3);
    fputs("]\n", stdout);
    jsonIndent(stdout, 2);
    fputs("},\n", stdout);

    jsonIndent(stdout, 2);
    fputs("\"heaviest_transition_deltas\": [\n", stdout);
    for (int i = 0; i < deltaCount && i < TOP_TRANSITIONS; i++) {
        jsonIndent(stdout, 3);
        fputs("{\n", stdout);
        jsonPrintIntField(stdout, 4, "from_scene_index", deltas[i].fromSceneIndex, 1);
        jsonPrintIntField(stdout, 4, "to_scene_index", deltas[i].toSceneIndex, 1);
        jsonPrintIntField(stdout, 4, "total_resource_churn", deltas[i].totalResourceChurn, 1);
        jsonPrintUintField(stdout, 4, "peak_memory_delta_bytes", deltas[i].peakMemoryDelta, 1);
        jsonPrintIntField(stdout, 4, "added_bmps", deltas[i].addedBmps, 1);
        jsonPrintIntField(stdout, 4, "removed_bmps", deltas[i].removedBmps, 1);
        jsonPrintIntField(stdout, 4, "added_scrs", deltas[i].addedScrs, 1);
        jsonPrintIntField(stdout, 4, "removed_scrs", deltas[i].removedScrs, 1);
        jsonPrintIntField(stdout, 4, "added_ttms", deltas[i].addedTtms, 1);
        jsonPrintIntField(stdout, 4, "removed_ttms", deltas[i].removedTtms, 0);
        jsonIndent(stdout, 3);
        fputs(i + 1 < deltaCount && i + 1 < TOP_TRANSITIONS ? "},\n" : "}\n", stdout);
    }
    jsonIndent(stdout, 2);
    fputs("],\n", stdout);

    jsonIndent(stdout, 2);
    fputs("\"likely_prefetch_sets\": [\n", stdout);
    for (int i = 0; i < NUM_SCENES; i++) {
        const struct TSceneAnalysis *sa = &sceneResults[i];
        const char *bmpNames[MAX_BMPS_PER_SCENE];
        const char *scrNames[MAX_SCRS_PER_SCENE];
        const char *ttmNames[MAX_TTMS_PER_SCENE];
        int bmpNamesCount = 0;
        int scrNamesCount = 0;
        int ttmNamesCount = 0;
        int candidateCount = 0;

        for (int j = 0; j < NUM_SCENES; j++) {
            const struct TSceneAnalysis *other = &sceneResults[j];
            if (i == j || strcasecmp(sa->adsName, other->adsName) != 0)
                continue;
            candidateCount++;
            for (int b = 0; b < other->numBmps; b++)
                if (!sceneHasBmp(sa, other->bmps[b].name) && bmpNamesCount < MAX_BMPS_PER_SCENE)
                    bmpNames[bmpNamesCount++] = other->bmps[b].name;
            for (int s = 0; s < other->numScrs; s++)
                if (!sceneHasScr(sa, other->scrs[s].name) && scrNamesCount < MAX_SCRS_PER_SCENE)
                    scrNames[scrNamesCount++] = other->scrs[s].name;
            for (int t = 0; t < other->numTtms; t++)
                if (!sceneHasTtm(sa, other->ttms[t].name) && ttmNamesCount < MAX_TTMS_PER_SCENE)
                    ttmNames[ttmNamesCount++] = other->ttms[t].name;
        }

        jsonIndent(stdout, 3);
        fputs("{\n", stdout);
        jsonPrintIntField(stdout, 4, "scene_index", i, 1);
        jsonPrintStringField(stdout, 4, "heuristic", "ads_family_union", 1);
        jsonPrintIntField(stdout, 4, "candidate_scene_count", candidateCount, 1);
        jsonIndent(stdout, 4);
        fputs("\"candidate_scene_indices\": [", stdout);
        {
            int emitted = 0;
            for (int j = 0; j < NUM_SCENES; j++) {
                if (i == j || strcasecmp(sa->adsName, sceneResults[j].adsName) != 0)
                    continue;
                if (emitted++ > 0) fputs(", ", stdout);
                fprintf(stdout, "%d", j);
            }
        }
        fputs("],\n", stdout);
        printJsonResourceArray(stdout, 4, "additional_bmps", bmpNames, bmpNamesCount);
        fputs(",\n", stdout);
        printJsonResourceArray(stdout, 4, "additional_scrs", scrNames, scrNamesCount);
        fputs(",\n", stdout);
        printJsonResourceArray(stdout, 4, "additional_ttms", ttmNames, ttmNamesCount);
        fputc('\n', stdout);
        jsonIndent(stdout, 3);
        fputs(i + 1 < NUM_SCENES ? "},\n" : "}\n", stdout);
    }
    jsonIndent(stdout, 2);
    fputs("]\n", stdout);

    jsonIndent(stdout, 1);
    fputs("},\n", stdout);

    jsonIndent(stdout, 1);
    fputs("\"scenes\": [\n", stdout);
    for (int i = 0; i < NUM_SCENES; i++) {
        struct TSceneAnalysis *sa = &sceneResults[i];
        struct TStoryScene *story = &storyScenes[i];

        jsonIndent(stdout, 2);
        fputs("{\n", stdout);
        jsonPrintIntField(stdout, 3, "scene_index", i, 1);
        jsonPrintStringField(stdout, 3, "ads_name", sa->adsName, 1);
        jsonPrintIntField(stdout, 3, "ads_tag", sa->adsTag, 1);
        jsonPrintIntField(stdout, 3, "story_day", story->dayNo, 1);
        jsonPrintFlagsArray(stdout, 3, story->flags);
        jsonIndent(stdout, 3);
        fputs("\"story\": {\n", stdout);
        jsonPrintStringField(stdout, 4, "spot_start", spotToString(story->spotStart), 1);
        jsonPrintStringField(stdout, 4, "heading_start", headingToString(story->hdgStart), 1);
        jsonPrintStringField(stdout, 4, "spot_end", spotToString(story->spotEnd), 1);
        jsonPrintStringField(stdout, 4, "heading_end", headingToString(story->hdgEnd), 0);
        jsonIndent(stdout, 3);
        fputs("},\n", stdout);

        jsonIndent(stdout, 3);
        fputs("\"resources\": {\n", stdout);
        jsonIndent(stdout, 4);
        fputs("\"bmps\": [\n", stdout);
        for (int b = 0; b < sa->numBmps; b++) {
            struct TBmpResource *bmp = sa->bmps[b].bmp;
            uint16 maxW = 0;
            uint16 maxH = 0;
            uint32 indexedBytes = 0;
            if (bmp) {
                for (int f = 0; f < bmp->numImages; f++) {
                    uint16 w = bmp->widths ? bmp->widths[f] : bmp->width;
                    uint16 h = bmp->heights ? bmp->heights[f] : bmp->height;
                    indexedBytes += ((uint32)w * h + 1) / 2;
                    if (w > maxW) maxW = w;
                    if (h > maxH) maxH = h;
                }
            }
            jsonIndent(stdout, 5);
            fputs("{\n", stdout);
            jsonPrintStringField(stdout, 6, "name", sa->bmps[b].name, 1);
            jsonPrintIntField(stdout, 6, "ttm_slot", sa->bmps[b].ttmSlot, 1);
            jsonPrintIntField(stdout, 6, "bmp_slot", sa->bmps[b].bmpSlot, 1);
            jsonPrintIntField(stdout, 6, "found", bmp ? 1 : 0, 1);
            jsonPrintIntField(stdout, 6, "num_frames", bmp ? bmp->numImages : 0, 1);
            jsonPrintIntField(stdout, 6, "max_width", maxW, 1);
            jsonPrintIntField(stdout, 6, "max_height", maxH, 1);
            jsonPrintUintField(stdout, 6, "indexed_bytes", indexedBytes, 1);
            jsonPrintUintField(stdout, 6, "raw_bytes", bmp ? bmp->uncompressedSize : 0, 0);
            jsonIndent(stdout, 5);
            fputs(b + 1 < sa->numBmps ? "},\n" : "}\n", stdout);
        }
        jsonIndent(stdout, 4);
        fputs("],\n", stdout);

        jsonIndent(stdout, 4);
        fputs("\"scrs\": [\n", stdout);
        for (int s = 0; s < sa->numScrs; s++) {
            jsonIndent(stdout, 5);
            fputs("{\n", stdout);
            jsonPrintStringField(stdout, 6, "name", sa->scrs[s].name, 1);
            jsonPrintIntField(stdout, 6, "found", sa->scrs[s].scr ? 1 : 0, 1);
            jsonPrintUintField(stdout, 6, "bytes", sa->scrs[s].scr ? sa->scrs[s].scr->uncompressedSize : 0, 0);
            jsonIndent(stdout, 5);
            fputs(s + 1 < sa->numScrs ? "},\n" : "}\n", stdout);
        }
        jsonIndent(stdout, 4);
        fputs("],\n", stdout);

        jsonIndent(stdout, 4);
        fputs("\"ttms\": [\n", stdout);
        for (int t = 0; t < sa->numTtms; t++) {
            jsonIndent(stdout, 5);
            fputs("{\n", stdout);
            jsonPrintStringField(stdout, 6, "name", sa->ttms[t].name, 1);
            jsonPrintIntField(stdout, 6, "slot_id", sa->ttms[t].slotId, 1);
            jsonPrintIntField(stdout, 6, "found", sa->ttms[t].ttm ? 1 : 0, 1);
            jsonPrintUintField(stdout, 6, "bytes", sa->ttms[t].ttm ? sa->ttms[t].ttm->uncompressedSize : 0, 0);
            jsonIndent(stdout, 5);
            fputs(t + 1 < sa->numTtms ? "},\n" : "}\n", stdout);
        }
        jsonIndent(stdout, 4);
        fputs("]\n", stdout);
        jsonIndent(stdout, 3);
        fputs("},\n", stdout);

        jsonIndent(stdout, 3);
        fputs("\"threads\": [\n", stdout);
        for (int t = 0; t < sa->numThreads; t++) {
            jsonIndent(stdout, 4);
            fputs("{\n", stdout);
            jsonPrintIntField(stdout, 5, "ttm_slot", sa->threads[t].ttmSlot, 1);
            jsonPrintIntField(stdout, 5, "ttm_tag", sa->threads[t].ttmTag, 1);
            jsonPrintIntField(stdout, 5, "num_plays", sa->threads[t].numPlays, 1);
            jsonPrintIntField(stdout, 5, "is_random", sa->threads[t].isRandom, 1);
            jsonPrintIntField(stdout, 5, "weight", sa->threads[t].weight, 0);
            jsonIndent(stdout, 4);
            fputs(t + 1 < sa->numThreads ? "},\n" : "}\n", stdout);
        }
        jsonIndent(stdout, 3);
        fputs("],\n", stdout);

        jsonIndent(stdout, 3);
        fputs("\"memory\": {\n", stdout);
        jsonPrintUintField(stdout, 4, "bmp_indexed_bytes", sa->totalBmpMemIndexed, 1);
        jsonPrintUintField(stdout, 4, "bmp_15bit_bytes", sa->totalBmpMem15bit, 1);
        jsonPrintUintField(stdout, 4, "bmp_raw_bytes", sa->totalBmpUncompressed, 1);
        jsonPrintUintField(stdout, 4, "ttm_bytes", sa->totalTtmBytes, 1);
        jsonPrintUintField(stdout, 4, "ads_bytes", sa->totalAdsBytes, 1);
        jsonPrintUintField(stdout, 4, "scr_bytes", sa->totalScrBytes, 1);
        jsonPrintUintField(stdout, 4, "sprite_pointer_bytes", sa->spritePointerBytes, 1);
        jsonPrintUintField(stdout, 4, "ttm_slot_overhead_bytes", sa->ttmSlotOverheadBytes, 1);
        jsonPrintUintField(stdout, 4, "peak_memory_bytes", sa->peakMemory, 0);
        jsonIndent(stdout, 3);
        fputs("},\n", stdout);
        jsonPrintUintField(stdout, 3, "total_sprite_frames", sa->totalSpriteFrames, 1);
        jsonPrintIntField(stdout, 3, "max_concurrent_threads", sa->maxConcurrentThreads, 0);
        jsonIndent(stdout, 2);
        fputs(i + 1 < NUM_SCENES ? "},\n" : "}\n", stdout);
    }
    jsonIndent(stdout, 1);
    fputs("]\n", stdout);
    fputs("}\n", stdout);
}

static void printTextReport(void)
{
    uint32 globalMaxMemory = 0;
    int    globalMaxMemoryScene = 0;
    uint32 globalMaxSprites = 0;
    int    globalMaxSpritesScene = 0;
    int    globalMaxThreads = 0;
    int    globalMaxThreadsScene = 0;
    uint32 globalMaxBmpMem = 0;
    int    globalMaxBmpMemScene = 0;
    int    budgetViolations = 0;

    printf("Johnny Castaway PS1 Scene Resource Analyzer\n");
    printf("============================================\n\n");

    printf("Parsing RESOURCE.MAP...\n");
    printf("Found: %d ADS, %d BMP, %d SCR, %d TTM resources\n\n",
           numAdsResources, numBmpResources, numScrResources, numTtmResources);

    for (int i = 0; i < NUM_SCENES; i++) {
        struct TSceneAnalysis *sa = &sceneResults[i];
        char flagsBuf[256];
        flagsToString(storyScenes[i].flags, flagsBuf, sizeof(flagsBuf));

        printf("=== Scene %d: %s tag %d ===\n", i, sa->adsName, sa->adsTag);
        printf("  Flags: %s\n", flagsBuf);

        if (storyScenes[i].dayNo)
            printf("  Story day: %d\n", storyScenes[i].dayNo);

        printf("  TTM slots: %d", sa->numTtms);
        if (sa->numTtms > 0) {
            printf(" (");
            for (int t = 0; t < sa->numTtms; t++) {
                if (t > 0) printf(", ");
                printf("%s[%d]", sa->ttms[t].name, sa->ttms[t].slotId);
            }
            printf(")");
        }
        printf("\n");

        printf("  ADD_SCENE calls: %d", sa->numThreads);
        if (sa->numThreads > 0) {
            printf(" (");
            for (int t = 0; t < sa->numThreads; t++) {
                if (t > 0) printf(", ");
                printf("slot%d:tag%d", sa->threads[t].ttmSlot, sa->threads[t].ttmTag);
                if (sa->threads[t].isRandom)
                    printf("[rand w=%d]", sa->threads[t].weight);
            }
            printf(")");
        }
        printf("\n");
        printf("  Max concurrent threads: %d\n", sa->maxConcurrentThreads);

        printf("  BMPs loaded: %d", sa->numBmps);
        if (sa->numBmps > 0) {
            printf("\n");
            for (int b = 0; b < sa->numBmps; b++) {
                struct TBmpResource *bmp = sa->bmps[b].bmp;
                if (bmp) {
                    uint32 indexedBytes = 0;
                    uint16 maxW = 0, maxH = 0;
                    for (int f = 0; f < bmp->numImages; f++) {
                        uint16 fw = bmp->widths ? bmp->widths[f] : bmp->width;
                        uint16 fh = bmp->heights ? bmp->heights[f] : bmp->height;
                        indexedBytes += ((uint32)fw * fh + 1) / 2;
                        if (fw > maxW) maxW = fw;
                        if (fh > maxH) maxH = fh;
                    }
                    printf("    %-16s %3d frames, max %3dx%-3d %6.1f KB indexed, %6.1f KB raw\n",
                           sa->bmps[b].name,
                           bmp->numImages, maxW, maxH,
                           indexedBytes / 1024.0,
                           bmp->uncompressedSize / 1024.0);
                } else {
                    printf("    %-16s (NOT FOUND)\n", sa->bmps[b].name);
                }
            }
        } else {
            printf("\n");
        }

        if (sa->numScrs > 0) {
            printf("  SCR backgrounds: ");
            for (int s = 0; s < sa->numScrs; s++) {
                if (s > 0) printf(", ");
                printf("%s", sa->scrs[s].name);
                if (sa->scrs[s].scr)
                    printf(" (%.1f KB)", sa->scrs[s].scr->uncompressedSize / 1024.0);
            }
            printf("\n");
        }

        printf("  Memory breakdown:\n");
        printf("    BMP pixels (4-bit): %7.1f KB\n", sa->totalBmpMemIndexed / 1024.0);
        printf("    BMP pixels (15-bit):%7.1f KB  (if not using indexed)\n", sa->totalBmpMem15bit / 1024.0);
        printf("    BMP raw data:       %7.1f KB\n", sa->totalBmpUncompressed / 1024.0);
        printf("    TTM bytecode:       %7.1f KB\n", sa->totalTtmBytes / 1024.0);
        printf("    ADS script:         %7.1f KB\n", sa->totalAdsBytes / 1024.0);
        printf("    SCR background:     %7.1f KB\n", sa->totalScrBytes / 1024.0);
        printf("    Sprite pointers:    %7.1f KB  (%u frames x %d-byte PS1 ptr)\n",
               sa->spritePointerBytes / 1024.0, sa->totalSpriteFrames, PS1_POINTER_SIZE);
        printf("    TTM slot overhead:  %7.1f KB  (%d x %d bytes)\n",
               sa->ttmSlotOverheadBytes / 1024.0, sa->numTtms, PS1_TTM_SLOT_OVERHEAD_BYTES);
        printf("    Total sprite frames: %u\n", sa->totalSpriteFrames);
        printf("  PEAK MEMORY (estimated): %7.1f KB / %.1f KB budget",
               sa->peakMemory / 1024.0, PS1_MEMORY_BUDGET / 1024.0);
        if (sa->peakMemory > PS1_MEMORY_BUDGET) {
            printf(" *** OVER BUDGET ***");
            budgetViolations++;
        }
        printf("\n\n");

        if (sa->peakMemory > globalMaxMemory) {
            globalMaxMemory = sa->peakMemory;
            globalMaxMemoryScene = i;
        }
        if (sa->totalSpriteFrames > globalMaxSprites) {
            globalMaxSprites = sa->totalSpriteFrames;
            globalMaxSpritesScene = i;
        }
        if (sa->maxConcurrentThreads > globalMaxThreads) {
            globalMaxThreads = sa->maxConcurrentThreads;
            globalMaxThreadsScene = i;
        }
        if (sa->totalBmpMemIndexed > globalMaxBmpMem) {
            globalMaxBmpMem = sa->totalBmpMemIndexed;
            globalMaxBmpMemScene = i;
        }
    }

    printf("================================================================\n");
    printf("                     GLOBAL SUMMARY\n");
    printf("================================================================\n\n");

    printf("PS1 Memory Budget: %.1f KB\n\n", PS1_MEMORY_BUDGET / 1024.0);

    printf("Heaviest scene (peak memory):\n");
    printf("  Scene %d: %s tag %d = %.1f KB\n",
           globalMaxMemoryScene,
           sceneResults[globalMaxMemoryScene].adsName,
           sceneResults[globalMaxMemoryScene].adsTag,
           globalMaxMemory / 1024.0);

    printf("\nMost sprite frames in memory:\n");
    printf("  Scene %d: %s tag %d = %u frames\n",
           globalMaxSpritesScene,
           sceneResults[globalMaxSpritesScene].adsName,
           sceneResults[globalMaxSpritesScene].adsTag,
           globalMaxSprites);

    printf("\nMost concurrent threads:\n");
    printf("  Scene %d: %s tag %d = %d threads\n",
           globalMaxThreadsScene,
           sceneResults[globalMaxThreadsScene].adsName,
           sceneResults[globalMaxThreadsScene].adsTag,
           globalMaxThreads);

    printf("\nLargest BMP pixel memory (4-bit indexed):\n");
    printf("  Scene %d: %s tag %d = %.1f KB\n",
           globalMaxBmpMemScene,
           sceneResults[globalMaxBmpMemScene].adsName,
           sceneResults[globalMaxBmpMemScene].adsTag,
           globalMaxBmpMem / 1024.0);

    printf("\nBudget violations: %d / %d scenes\n", budgetViolations, NUM_SCENES);

    printf("\n--- Top 10 Heaviest Scenes (by peak memory) ---\n");
    {
        int sorted[NUM_SCENES];
        buildSortedSceneIndices(sorted);
        for (int rank = 0; rank < 10 && rank < NUM_SCENES; rank++) {
            int idx = sorted[rank];
            struct TSceneAnalysis *sa = &sceneResults[idx];
            printf("  %2d. %-14s tag %2d: %6.1f KB peak | %3u frames | %d threads | %d BMPs",
                   rank + 1, sa->adsName, sa->adsTag,
                   sa->peakMemory / 1024.0,
                   sa->totalSpriteFrames,
                   sa->maxConcurrentThreads,
                   sa->numBmps);
            if (sa->peakMemory > PS1_MEMORY_BUDGET)
                printf(" OVER!");
            printf("\n");
        }
    }

    printf("\n--- Complete BMP Inventory (all scenes combined) ---\n");
    {
        struct TBmpInventoryItem bmpList[MAX_UNIQUE_BMPS];
        int bmpCount = collectBmpInventory(bmpList);
        uint32 totalIndexed = 0;

        printf("  %-16s %6s %5s %8s %8s %6s\n",
               "Name", "Frames", "WxH", "Indexed", "Raw", "Scenes");
        printf("  %-16s %6s %5s %8s %8s %6s\n",
               "----", "------", "---", "-------", "---", "------");
        for (int i = 0; i < bmpCount; i++) {
            char dim[16];
            snprintf(dim, sizeof(dim), "%dx%d", bmpList[i].w, bmpList[i].h);
            printf("  %-16s %6d %5s %6.1f KB %6.1f KB %6d\n",
                   bmpList[i].name,
                   bmpList[i].numFrames,
                   dim,
                   bmpList[i].indexedBytes / 1024.0,
                   bmpList[i].rawBytes / 1024.0,
                   bmpList[i].refCount);
            totalIndexed += bmpList[i].indexedBytes;
        }
        printf("  %-16s %6s %5s %6.1f KB\n", "TOTAL", "", "", totalIndexed / 1024.0);
        printf("  Unique BMPs used across all scenes: %d / %d total in resources\n",
               bmpCount, numBmpResources);
    }

    printf("\n--- TTM Inventory ---\n");
    {
        struct TTtmInventoryItem ttmList[MAX_UNIQUE_TTMS];
        int ttmCount = collectTtmInventory(ttmList);
        uint32 totalTtm = 0;

        for (int i = 0; i < ttmCount; i++) {
            printf("  %-20s %6.1f KB  (used by %d scenes)\n",
                   ttmList[i].name, ttmList[i].bytes / 1024.0, ttmList[i].refCount);
            totalTtm += ttmList[i].bytes;
        }
        printf("  Total unique TTM bytecode: %.1f KB\n", totalTtm / 1024.0);
    }

    printf("\n================================================================\n");
    printf("Analysis complete. %d scenes analyzed.\n", NUM_SCENES);
}

static int parseOptions(int argc, char **argv, struct TAnalyzerOptions *options)
{
    memset(options, 0, sizeof(*options));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            options->jsonOutput = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--json]\n", argv[0]);
            return 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [--json]\n", argv[0]);
            return -1;
        }
    }

    return 0;
}

/* ---- Main ---- */
int main(int argc, char **argv)
{
    struct TAnalyzerOptions options;
    int parseStatus = parseOptions(argc, argv, &options);

    if (parseStatus != 0)
        return parseStatus < 0 ? 1 : 0;

    parseResourceFiles("RESOURCE.MAP");

    for (int i = 0; i < NUM_SCENES; i++)
        analyzeScene(i);

    if (options.jsonOutput)
        printJsonReport();
    else
        printTextReport();

    return 0;
}
