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
#else
/* PS1 freestanding: provide minimal FILE I/O declarations */
#include <stddef.h>
#ifndef _FILE_DEFINED
#define _FILE_DEFINED
typedef struct _FILE FILE;
#endif
extern FILE *fopen(const char *pathname, const char *mode);
extern size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
extern int fclose(FILE *stream);
extern int fseek(FILE *stream, long offset, int whence);
extern long ftell(FILE *stream);
extern int fflush(FILE *stream);
extern void *malloc(size_t size);
extern void *calloc(size_t nmemb, size_t size);
extern void free(void *ptr);
/* stdio constants */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define stdout ((FILE*)1)
#endif
#include <string.h>

#include "mytypes.h"
#include "utils.h"
#include "resource.h"
#include "uncompress.h"

#ifdef PS1_BUILD
#include "cdrom_ps1.h"
#include <psxgpu.h>
#include <psxgte.h>
#endif /* PS1_BUILD */

#define MAX_ADS_RESOURCES 100
#define MAX_BMP_RESOURCES 200
#define MAX_PAL_RESOURCES 1
#define MAX_SCR_RESOURCES 20
#define MAX_TTM_RESOURCES 100


struct TAdsResource *adsResources[MAX_ADS_RESOURCES];
struct TBmpResource *bmpResources[MAX_BMP_RESOURCES];
struct TPalResource *palResources[MAX_PAL_RESOURCES];
struct TScrResource *scrResources[MAX_SCR_RESOURCES];
struct TTtmResource *ttmResources[MAX_TTM_RESOURCES];
int numAdsResources = 0;
int numBmpResources = 0;
int numPalResources = 0;
int numScrResources = 0;
int numTtmResources = 0;

static struct TMapFile mapFile;

/* LRU Cache globals */
static uint32 globalTick = 0;
static size_t totalMemoryUsed = 0;
static size_t memoryBudget = 256 * 1024;  /* 256KB for <500KB target */

/* Load resource data from extracted file if available, otherwise decompress */
static uint8 *loadOrUncompress(FILE *compressedFile,
                                const char *resourceName,
                                const char *resourceType,
                                uint8 compressionMethod,
                                uint32 compressedSize,
                                uint32 uncompressedSize)
{
    char extractedPath[512];
    FILE *extractedFile;
    uint8 *data;

    /* Try to load from extracted file first */
    snprintf(extractedPath, sizeof(extractedPath), "extracted/%s/%s",
             resourceType, resourceName);

    extractedFile = fopen(extractedPath, "rb");
    if (extractedFile != NULL) {
        /* Load directly from disk - no decompression needed */
        data = safe_malloc(uncompressedSize);
        if (fread(data, 1, uncompressedSize, extractedFile) != uncompressedSize) {
            if (debugMode) {
                printf("Warning: Failed to read %s, falling back to decompression\n",
                       extractedPath);
            }
            free(data);
            fclose(extractedFile);
            /* Fall through to decompression */
        } else {
            fclose(extractedFile);
            /* Skip past compressed data in resource file */
            fseek(compressedFile, compressedSize, SEEK_CUR);
            if (debugMode) {
                printf("Loaded %s from extracted file (saved ~16KB working memory)\n",
                       resourceName);
            }
            return data;
        }
    }

    /* Fall back to decompression */
    return uncompress(compressedFile, compressionMethod,
                      compressedSize, uncompressedSize);
}


static struct TAdsResource *parseAdsResource(FILE *f)
{
    struct TAdsResource *adsResource;
    uint8 *buffer;


    adsResource = safe_malloc(sizeof(struct TAdsResource));

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"VER:",4))
        fatalError("'VER:' string not found while parsing ADS resource");

    free(buffer);

    adsResource->versionSize = readUint32(f);
    adsResource->versionString = readUint8Block(f,5);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"ADS:",4))
        fatalError("'ADS:' string not found while parsing ADS resource");

    free(buffer);

    adsResource->adsUnknown1 = readUint8(f);
    adsResource->adsUnknown2 = readUint8(f);
    adsResource->adsUnknown3 = readUint8(f);
    adsResource->adsUnknown4 = readUint8(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"RES:",4))
        fatalError("'RES:' string not found while parsing ADS resource");

    free(buffer);

    adsResource->resSize = readUint32(f);
    adsResource->numRes = readUint16(f);

    adsResource->res = safe_malloc(adsResource->numRes * sizeof(struct TAdsRes));

    for (int i=0; i < adsResource->numRes; i++) {
        adsResource->res[i].id = readUint16(f);
        adsResource->res[i].name = getString(f,40);
    }

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"SCR:",4))
        fatalError("'SCR:' string not found while parsing ADS resource");

    free(buffer);

    adsResource->compressedSize = readUint32(f) - 5;
    adsResource->compressionMethod = readUint8(f);
    adsResource->uncompressedSize = readUint32(f);

    /* ADS lazy loading: Don't decompress at startup, just skip the compressed data */
    /* This saves ~15KB of memory at startup (only decompress when ADS is played) */
    adsResource->uncompressedData = NULL;  /* Will be loaded on demand in ads.c */
    fseek(f, adsResource->compressedSize, SEEK_CUR);  /* Skip compressed data for now */

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"TAG:",4))
        fatalError("'TAG:' string not found while parsing ADS resource");

    free(buffer);

    adsResource->tagSize = readUint32(f);
    adsResource->numTags = readUint16(f);

    adsResource->tags = safe_malloc(adsResource->numTags * sizeof(struct TTags));

    for (int i=0; i < adsResource->numTags; i++) {
        adsResource->tags[i].id = readUint16(f);
        adsResource->tags[i].description = getString(f,40);
    }

    return adsResource;
}


static struct TBmpResource *parseBmpResource(FILE *f)
{
    struct TBmpResource *bmpResource;
    uint8 *buffer;


    bmpResource = safe_malloc(sizeof(struct TBmpResource));

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"BMP:",4))
        fatalError("'BMP:' string not found while parsing BMP resource");

    free(buffer);

    bmpResource->width = readUint16(f);
    bmpResource->height = readUint16(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"INF:",4))
        fatalError("'INF:' string not found while parsing BMP resource");

    free(buffer);

    bmpResource->dataSize = readUint32(f);
    bmpResource->numImages = readUint16(f);

    bmpResource->widths = readUint16Block(f, bmpResource->numImages);
    bmpResource->heights = readUint16Block(f, bmpResource->numImages);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"BIN:",4))
        fatalError("'BIN:' string not found while parsing BMP resource");

    free(buffer);

    bmpResource->compressedSize = readUint32(f) - 5; // discard size of compressionmethod+uncompressedsize
    bmpResource->compressionMethod = readUint8(f);
    bmpResource->uncompressedSize = readUint32(f);

    bmpResource->uncompressedData = loadOrUncompress(f,
                                      bmpResource->resName,
                                      "bmp",
                                      bmpResource->compressionMethod,
                                      bmpResource->compressedSize,
                                      bmpResource->uncompressedSize
                                    );

    return bmpResource;
}


static struct TPalResource *parsePalResource(FILE *f)
{
    struct TPalResource *palResource;
    uint8 *buffer;


    palResource = safe_malloc(sizeof(struct TPalResource));

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"PAL:",4))
        fatalError("'PAL:' string not found while parsing PAL resource");

    free(buffer);

    palResource->size = readUint16(f);
    palResource->unknown1 = readUint8(f);
    palResource->unknown2 = readUint8(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"VGA:",4))
        fatalError("'VGA:' string not found while parsing PAL resource");

    free(buffer);

    readUint8(f);   // size ?
    readUint8(f);
    readUint8(f);
    readUint8(f);

    for (int i=0; i < 256; i++) {
        palResource->colors[i].r = readUint8(f);
        palResource->colors[i].g = readUint8(f);
        palResource->colors[i].b = readUint8(f);
    }

    return palResource;
}


static struct TScrResource *parseScrResource(FILE *f)
{
    struct TScrResource *scrResource;
    uint8 *buffer;


    scrResource = safe_malloc(sizeof(struct TScrResource));

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"SCR:",4))
        fatalError("'SCR:' string not found while parsing SCR resource");

    free(buffer);

    scrResource->totalSize = readUint16(f);
    scrResource->flags = readUint16(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"DIM:",4))
        fatalError("'DIM:' string not found while parsing SCR resource");

    free(buffer);

    scrResource->dimSize = readUint32(f);
    scrResource->width = readUint16(f);
    scrResource->height = readUint16(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"BIN:",4))
        fatalError("'BIN:' string not found while parsing SCR resource");

    free(buffer);

    scrResource->compressedSize = readUint32(f) - 5; // discard size of compressionmethod+uncompressedsize
    scrResource->compressionMethod = readUint8(f);
    scrResource->uncompressedSize = readUint32(f) ;

    scrResource->uncompressedData = loadOrUncompress(f,
                                      scrResource->resName,
                                      "scr",
                                      scrResource->compressionMethod,
                                      scrResource->compressedSize,
                                      scrResource->uncompressedSize
                                    );

    return scrResource;
}


static struct TTtmResource *parseTtmResource(FILE *f)
{
    struct TTtmResource *ttmResource;
    uint8 *buffer;

    ttmResource = safe_malloc(sizeof(struct TTtmResource));

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"VER:",4))
        fatalError("'VER:' string not found while parsing TTM resource");

    free(buffer);

    ttmResource->versionSize = readUint32(f);
    ttmResource->versionString = readUint8Block(f,5);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"PAG:",4))
        fatalError("'PAG:' string not found while parsing TTM resource");

    free(buffer);

    ttmResource->numPages = readUint32(f);
    ttmResource->pagUnknown1 = readUint8(f);
    ttmResource->pagUnknown2 = readUint8(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"TT3:",4))
        fatalError("'TT3:' string not found while parsing TTM resource");

    free(buffer);

    ttmResource->compressedSize = readUint32(f) - 5; // discard size of compressionmethod+uncompressedsize
    ttmResource->compressionMethod = readUint8(f);
    ttmResource->uncompressedSize = readUint32(f);

    /* TTM lazy loading: Don't decompress at startup, just skip the compressed data */
    /* This saves ~284KB of memory at startup (only decompress when TTM is played) */
    ttmResource->uncompressedData = NULL;  /* Will be loaded on demand in ttmLoadTtm() */
    fseek(f, ttmResource->compressedSize, SEEK_CUR);  /* Skip compressed data for now */

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"TTI:",4))
        fatalError("'TTI:' string not found while parsing TTM resource");

    free(buffer);

    ttmResource->ttiUnknown1 = readUint8(f);
    ttmResource->ttiUnknown2 = readUint8(f);
    ttmResource->ttiUnknown3 = readUint8(f);
    ttmResource->ttiUnknown4 = readUint8(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"TAG:",4))
        fatalError("'TAG:' string not found while parsing TTM resource");

    free(buffer);

    ttmResource->tagSize = readUint32(f);
    ttmResource->numTags = readUint16(f);

    ttmResource->tags = safe_malloc(ttmResource->numTags * sizeof(struct TTags));

    for (int i=0; i < ttmResource->numTags; i++) {
        ttmResource->tags[i].id = readUint16(f);
        ttmResource->tags[i].description = getString(f,40);
    }

    return ttmResource;
}


static void parseMapFile(char *fileName)
{
#ifdef PS1_BUILD
    PS1File *f_map;

    if (debugMode) {
        printf("PS1: parseMapFile() called with: %s\n", fileName);
        fflush(stdout);
    }

    f_map = ps1_fopen(fileName,"rb");

    if (f_map == NULL) {
        printf("ERROR: Cannot open map file: %s\n", fileName);
        fatalError("Resources map file not found: %s\n", fileName);
    }

    if (debugMode)
        printf("Map file opened successfully\n");

    mapFile.unknown1 = ps1_readUint8(f_map);   // first 5 uint8s unknown
    mapFile.unknown2 = ps1_readUint8(f_map);
    mapFile.unknown3 = ps1_readUint8(f_map);
    mapFile.unknown4 = ps1_readUint8(f_map);   // ? number of resources files available in this index
    mapFile.unknown5 = ps1_readUint8(f_map);
    mapFile.unknown6 = ps1_readUint8(f_map);

    mapFile.resFileName = (char *) ps1_getString(f_map,13);

    mapFile.numEntries = ps1_readUint16(f_map);

    mapFile.Entries = safe_malloc(mapFile.numEntries * sizeof(struct TMapFileEntry));

    for (int i=0; i<mapFile.numEntries; i++) {
        mapFile.Entries[i].length = ps1_readUint32(f_map);
        mapFile.Entries[i].offset = ps1_readUint32(f_map);
    }

    ps1_fclose(f_map);

    if (debugMode) {
        printf("PS1: parseMapFile() completed successfully\n");
        fflush(stdout);
    }
#else
    FILE *f_map; // , *f_res;  // TODO

    if (debugMode)
        printf("Opening map file: %s\n", fileName);

    f_map = fopen(fileName,"rb");

    if (f_map == NULL) {
        printf("ERROR: Cannot open map file: %s\n", fileName);
        fatalError("Resources map file not found: %s\n", fileName);
    }

    if (debugMode)
        printf("Map file opened successfully\n");

    mapFile.unknown1 = readUint8(f_map);   // first 5 uint8s unknown
    mapFile.unknown2 = readUint8(f_map);
    mapFile.unknown3 = readUint8(f_map);
    mapFile.unknown4 = readUint8(f_map);   // ? number of resources files available in this index
    mapFile.unknown5 = readUint8(f_map);
    mapFile.unknown6 = readUint8(f_map);

    mapFile.resFileName = (char *) getString(f_map,13);

    mapFile.numEntries = readUint16(f_map);

    mapFile.Entries = safe_malloc(mapFile.numEntries * sizeof(struct TMapFileEntry));

    for (int i=0; i<mapFile.numEntries; i++) {
        mapFile.Entries[i].length = readUint32(f_map);
        mapFile.Entries[i].offset = readUint32(f_map);
    }

    fclose(f_map);
#endif
}


static void parseResourceFile(char * filename)
{
#ifdef PS1_BUILD
    PS1File *f;

    if (debugMode) {
        printf("PS1: parseResourceFile() called for: %s\n", mapFile.resFileName);
        fflush(stdout);
    }

    /* Visual checkpoint: CYAN = Starting parseResourceFile */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    setRGB0(&draw, 0, 255, 255);  /* CYAN = parseResourceFile entry */
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    for (int i = 0; i < 120; i++) VSync(0);

    f = ps1_fopen(mapFile.resFileName,"rb");

    if (f == NULL) {
        /* RED = Resource file not found */
        setRGB0(&draw, 255, 0, 0);
        draw.isbg = 1;
        PutDrawEnv(&draw);
        SetDispMask(1);
        for (int i = 0; i < 300; i++) VSync(0);
        fatalError("Main resources file not found: %s\n", mapFile.resFileName);
    }

    /* BLUE = Resource file opened successfully */
    setRGB0(&draw, 0, 0, 255);
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    for (int i = 0; i < 120; i++) VSync(0);

    if (debugMode) {
        printf("Loading resources ");
        fflush (stdout);
    }

    for (int i=0; i < mapFile.numEntries; i++) {

        ps1_fseek(f, mapFile.Entries[i].offset, SEEK_SET);

        mapFile.Entries[i].resName = (char *) ps1_readUint8Block(f,13);
        mapFile.Entries[i].resSize = ps1_readUint32(f);

        char *resName = mapFile.Entries[i].resName;
        char *resType = resName + strlen(resName) - 4;  // get the extension .BMP .ADS etc.

        if (debugMode) {
             putchar('.');
             fflush(stdout);
        }

        /* For PS1, temporarily skip resource parsing to test file access */
        /* TODO: Implement PS1 versions of parseAdsResource, parseBmpResource, etc. */
        if (!strcmp(resType, ".ADS")) {
            // adsResources[numAdsResources] = parseAdsResource(f);
            // adsResources[numAdsResources]->resName = resName;
            // numAdsResources++;
            if (debugMode) printf("ADS ");
        }
        else if (!strcmp(resType, ".BMP")) {
            // bmpResources[numBmpResources] = parseBmpResource(f);
            // bmpResources[numBmpResources]->resName = resName;
            // numBmpResources++;
            if (debugMode) printf("BMP ");
        }
        else if (!strcmp(resType, ".PAL")) {
            // palResources[numPalResources] = parsePalResource(f);
            // palResources[numPalResources]->resName = resName;
            // numPalResources++;
            if (debugMode) printf("PAL ");
        }
        else if (!strcmp(resType, ".SCR")) {
            scrResources[numScrResources] = ps1_parseScrResource(f, resName);
            if (scrResources[numScrResources] != NULL) {
                numScrResources++;
                if (debugMode) printf("SCR-OK ");
            } else {
                if (debugMode) printf("SCR-FAIL ");
            }
        }
        else if (!strcmp(resType, ".TTM")) {
            // ttmResources[numTtmResources] = parseTtmResource(f);
            // ttmResources[numTtmResources]->resName = resName;
            // numTtmResources++;
            if (debugMode) printf("TTM ");
        }
    }

    ps1_fclose(f);

    /* MAGENTA = Resource parsing loop completed */
    setRGB0(&draw, 255, 0, 255);
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    for (int i = 0; i < 120; i++) VSync(0);

    if (debugMode) {
        printf("\nPS1: Resource file parsing completed (resource parsing temporarily disabled)\n");
    }

    /* GREEN = parseResourceFile completed successfully */
    setRGB0(&draw, 0, 255, 0);
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
    for (int i = 0; i < 180; i++) VSync(0);
#else
    FILE *f;

    f = fopen(mapFile.resFileName,"rb");

    if (f == NULL)
        fatalError("Main resources file not found: %s\n", mapFile.resFileName);

    if (debugMode) {
        printf("Loading resources ");
        fflush (stdout);
    }

    for (int i=0; i < mapFile.numEntries; i++) {

        fseek(f, mapFile.Entries[i].offset, SEEK_SET);

        mapFile.Entries[i].resName = (char *) readUint8Block(f,13);
        mapFile.Entries[i].resSize = readUint32(f);

        char *resName = mapFile.Entries[i].resName;
        char *resType = resName + strlen(resName) - 4;  // get the extension .BMP .ADS etc.

        if (debugMode) {
             putchar('.');
             fflush(stdout);
        }

        if (!strcmp(resType, ".ADS")) {
            adsResources[numAdsResources] = parseAdsResource(f);
            adsResources[numAdsResources]->resName = resName;
            numAdsResources++;
        }
        else if (!strcmp(resType, ".BMP")) {
            bmpResources[numBmpResources] = parseBmpResource(f);
            bmpResources[numBmpResources]->resName = resName;
            numBmpResources++;
        }
        else if (!strcmp(resType, ".PAL")) {
            palResources[numPalResources] = parsePalResource(f);
            palResources[numPalResources]->resName = resName;
            numPalResources++;
        }
        else if (!strcmp(resType, ".SCR")) {
            scrResources[numScrResources] = parseScrResource(f);
            scrResources[numScrResources]->resName = resName;
            numScrResources++;
        }
        else if (!strcmp(resType, ".TTM")) {
            ttmResources[numTtmResources] = parseTtmResource(f);
            ttmResources[numTtmResources]->resName = resName;
            numTtmResources++;
        }
        // Note: there is one .VIN type file too (FILES.VIN)
        // We dont process it since it's nothing else than a list
        // of files, which we dont need
    }

    fclose(f);

    if (debugMode)
        putchar('\n');
#endif
}


void parseResourceFiles(char * filename)
{
#ifdef PS1_BUILD
    if (debugMode) {
        printf("PS1: parseResourceFiles() called with: %s\n", filename);
        fflush(stdout);
    }
#endif
    parseMapFile(filename);
#ifdef PS1_BUILD
    if (debugMode) {
        printf("PS1: parseMapFile() completed, calling parseResourceFile()...\n");
        fflush(stdout);
    }
#endif
    parseResourceFile(filename);
#ifdef PS1_BUILD
    if (debugMode) {
        printf("PS1: parseResourceFile() completed\n");
        fflush(stdout);
    }
#endif
}


struct TAdsResource *findAdsResource(char *searchString)
{
    struct TAdsResource *result = NULL;

    for (int i=0; i < numAdsResources && result == NULL; i++) {
        if (!strcmp(adsResources[i]->resName, searchString))
            result = adsResources[i];
    }

    if (result == NULL)
        fatalError("ADS resource %s not found.", searchString);

    return result;
}


struct TBmpResource *findBmpResource(char *searchString)
{
    struct TBmpResource *result = NULL;

    for (int i=0; i < numBmpResources && result == NULL; i++) {
        if (!strcmp(bmpResources[i]->resName, searchString))
            result = bmpResources[i];
    }

    if (result == NULL)
        fatalError("BMP resource %s not found.", searchString);

    return result;
}


struct TScrResource *findScrResource(char *searchString)
{
    struct TScrResource *result = NULL;

    for (int i=0; i < numScrResources && result == NULL; i++) {
        if (!strcmp(scrResources[i]->resName, searchString))
            result = scrResources[i];
    }

    if (result == NULL)
        fatalError("SCR resource %s not found.", searchString);

    return result;
}


struct TTtmResource *findTtmResource(char *searchString)
{
    struct TTtmResource *result = NULL;

    for (int i=0; i < numTtmResources && result == NULL; i++) {
        if (!strcmp(ttmResources[i]->resName, searchString))
            result = ttmResources[i];
    }

    if (result == NULL)
        fatalError("TTM resource %s not found.", searchString);

    return result;
}


/* ============================================================================
 * LRU Cache Implementation
 * ============================================================================ */

void initLRUCache(void) {
    globalTick = 0;
    totalMemoryUsed = 0;
    
    /* Check for JC_MEM_BUDGET_MB environment variable */
    char *budgetEnv = getenv("JC_MEM_BUDGET_MB");
    if (budgetEnv != NULL) {
        int budgetMB = atoi(budgetEnv);
        if (budgetMB > 0) {
            memoryBudget = (size_t)budgetMB * 1024 * 1024;
            if (debugMode) {
                printf("LRU cache: Memory budget set to %d MB\n", budgetMB);
            }
        }
    }
    
    /* Initialize all resource LRU fields */
    for (int i = 0; i < numAdsResources; i++) {
        adsResources[i]->lastUsedTick = 0;
        adsResources[i]->pinCount = 0;
    }
    for (int i = 0; i < numBmpResources; i++) {
        bmpResources[i]->lastUsedTick = 0;
        bmpResources[i]->pinCount = 0;
    }
    for (int i = 0; i < numScrResources; i++) {
        scrResources[i]->lastUsedTick = 0;
        scrResources[i]->pinCount = 0;
    }
    for (int i = 0; i < numTtmResources; i++) {
        ttmResources[i]->lastUsedTick = 0;
        ttmResources[i]->pinCount = 0;
    }
}

void touchResource(void *resource) {
    globalTick++;
    
    /* Update lastUsedTick based on resource type */
    for (int i = 0; i < numAdsResources; i++) {
        if (adsResources[i] == resource) {
            adsResources[i]->lastUsedTick = globalTick;
            return;
        }
    }
    for (int i = 0; i < numBmpResources; i++) {
        if (bmpResources[i] == resource) {
            bmpResources[i]->lastUsedTick = globalTick;
            return;
        }
    }
    for (int i = 0; i < numScrResources; i++) {
        if (scrResources[i] == resource) {
            scrResources[i]->lastUsedTick = globalTick;
            return;
        }
    }
    for (int i = 0; i < numTtmResources; i++) {
        if (ttmResources[i] == resource) {
            ttmResources[i]->lastUsedTick = globalTick;
            return;
        }
    }
}

void pinResource(void *resource, uint32 size, const char *type) {
    touchResource(resource);
    
    /* Increment pin count */
    for (int i = 0; i < numAdsResources; i++) {
        if (adsResources[i] == resource) {
            adsResources[i]->pinCount++;
            if (adsResources[i]->uncompressedData != NULL) {
                totalMemoryUsed += size;
            }
            return;
        }
    }
    for (int i = 0; i < numTtmResources; i++) {
        if (ttmResources[i] == resource) {
            ttmResources[i]->pinCount++;
            if (ttmResources[i]->uncompressedData != NULL) {
                totalMemoryUsed += size;
            }
            return;
        }
    }
}

void unpinResource(void *resource, const char *type) {
    /* Decrement pin count */
    for (int i = 0; i < numAdsResources; i++) {
        if (adsResources[i] == resource) {
            if (adsResources[i]->pinCount > 0) {
                adsResources[i]->pinCount--;
            }
            return;
        }
    }
    for (int i = 0; i < numTtmResources; i++) {
        if (ttmResources[i] == resource) {
            if (ttmResources[i]->pinCount > 0) {
                ttmResources[i]->pinCount--;
            }
            return;
        }
    }
}

void checkMemoryBudget(void) {
    if (totalMemoryUsed <= memoryBudget) {
        return;
    }
    
    if (debugMode) {
        printf("LRU cache: Memory over budget (%.2f MB / %.2f MB), evicting...\n",
               totalMemoryUsed / (1024.0 * 1024.0),
               memoryBudget / (1024.0 * 1024.0));
    }
    
    /* Find and evict LRU unpinned resources */
    while (totalMemoryUsed > memoryBudget) {
        void *lruResource = NULL;
        uint32 lruTick = globalTick + 1;
        size_t lruSize = 0;
        char lruType[10] = "";
        
        /* Find LRU unpinned ADS */
        for (int i = 0; i < numAdsResources; i++) {
            if (adsResources[i]->uncompressedData != NULL &&
                adsResources[i]->pinCount == 0 &&
                adsResources[i]->lastUsedTick < lruTick) {
                lruResource = adsResources[i];
                lruTick = adsResources[i]->lastUsedTick;
                lruSize = adsResources[i]->uncompressedSize;
                strcpy(lruType, "ADS");
            }
        }
        
        /* Find LRU unpinned TTM */
        for (int i = 0; i < numTtmResources; i++) {
            if (ttmResources[i]->uncompressedData != NULL &&
                ttmResources[i]->pinCount == 0 &&
                ttmResources[i]->lastUsedTick < lruTick) {
                lruResource = ttmResources[i];
                lruTick = ttmResources[i]->lastUsedTick;
                lruSize = ttmResources[i]->uncompressedSize;
                strcpy(lruType, "TTM");
            }
        }
        
        /* Evict the LRU resource */
        if (lruResource != NULL) {
            if (strcmp(lruType, "ADS") == 0) {
                struct TAdsResource *ads = (struct TAdsResource *)lruResource;
                if (debugMode) {
                    printf("LRU cache: Evicting %s (%.2f KB)\n",
                           ads->resName, lruSize / 1024.0);
                }
                free(ads->uncompressedData);
                ads->uncompressedData = NULL;
                totalMemoryUsed -= lruSize;
            } else if (strcmp(lruType, "TTM") == 0) {
                struct TTtmResource *ttm = (struct TTtmResource *)lruResource;
                if (debugMode) {
                    printf("LRU cache: Evicting %s (%.2f KB)\n",
                           ttm->resName, lruSize / 1024.0);
                }
                free(ttm->uncompressedData);
                ttm->uncompressedData = NULL;
                totalMemoryUsed -= lruSize;
            }
        } else {
            /* No unpinned resources to evict */
            if (debugMode) {
                printf("LRU cache: All resources pinned, cannot evict\n");
            }
            break;
        }
    }
}

size_t getTotalMemoryUsed(void) {
    return totalMemoryUsed;
}
