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

#include "mytypes.h"
#include "utils.h"
#include "resource.h"
#include "uncompress.h"

struct TPalResource *palResources[1];
int numPalResources = 0;

//static struct TMapFile mapFile;


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

    adsResource->uncompressedData = uncompress(f,
                                      adsResource->compressionMethod,
                                      adsResource->compressedSize,
                                      adsResource->uncompressedSize
                                    );

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

    bmpResource->uncompressedData = uncompress(f,
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

    scrResource->uncompressedData = uncompress(f,
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

    ttmResource->uncompressedData = uncompress(f,
                                      ttmResource->compressionMethod,
                                      ttmResource->compressedSize,
                                      ttmResource->uncompressedSize
                                    );

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

void parseResourceFiles()
{

    FILE *fpal;
    fpal = safe_fopen("/cd/JOHNCAST.PAL" ,"rb");

    palResources[numPalResources] = parsePalResource(fpal);
    palResources[numPalResources]->resName = "JOHNCAST.PAL";
    numPalResources++;
    fclose(fpal);

}


struct TAdsResource *findAdsResource(char *searchString)
{
    FILE *f;

    int searchSiz = sizeof(searchString);
    char fullSearch[searchSiz + 4];

    strcpy(fullSearch,"/cd/");
    strcat(fullSearch,searchString);

    f = fopen(fullSearch,"rb");
    struct TAdsResource *adsRes = parseAdsResource(f);
    fclose(f);

    return adsRes;

/*
    struct TAdsResource *result = NULL;

    for (int i=0; i < numAdsResources && result == NULL; i++) {
        if (!strcmp(adsResources[i]->resName, searchString))
            result = adsResources[i];
    }

    if (result == NULL)
        fatalError("ADS resource %s not found.", searchString);

    return result;
    */
}


struct TBmpResource *findBmpResource(char *searchString)
{
  FILE *f;

    int searchSiz = sizeof(searchString);
    char fullSearch[searchSiz + 4];

    strcpy(fullSearch,"/cd/");
    strcat(fullSearch,searchString);

    f = fopen(fullSearch,"rb");

    struct TBmpResource  *tmp = parseBmpResource(f);
    fclose(f);
    return tmp;

    /*
    struct TBmpResource *result = NULL;

    for (int i=0; i < numBmpResources && result == NULL; i++) {
        if (!strcmp(bmpResources[i]->resName, searchString))
            result = bmpResources[i];
    }

    if (result == NULL)
        fatalError("BMP resource %s not found.", searchString);

    return result;
    */
}


struct TScrResource *findScrResource(char *searchString)
{
  FILE *f;

    int searchSiz = sizeof(searchString);
    char fullSearch[searchSiz + 4];

    strcpy(fullSearch,"/cd/");
    strcat(fullSearch,searchString);

    f = fopen(fullSearch,"rb");

    struct TScrResource *tmp = parseScrResource(f);
    fclose(f);
    return tmp;


    /*
    struct TScrResource *result = NULL;

    for (int i=0; i < numScrResources && result == NULL; i++) {
        if (!strcmp(scrResources[i]->resName, searchString))
            result = scrResources[i];
    }

    if (result == NULL)
        fatalError("SCR resource %s not found.", searchString);

    return result;
    */
}


struct TTtmResource *findTtmResource(char *searchString)
{

    FILE *f;

    int searchSiz = sizeof(searchString);
    char fullSearch[searchSiz + 4];

    strcpy(fullSearch,"/cd/");
    strcat(fullSearch,searchString);

    f = fopen(fullSearch,"rb");

    struct TTtmResource *tmp = parseTtmResource(f);
    fclose(f);
    return tmp;

    /*
    struct TTtmResource *result = NULL;

    for (int i=0; i < numTtmResources && result == NULL; i++) {
        if (!strcmp(ttmResources[i]->resName, searchString))
            result = ttmResources[i];
    }

    if (result == NULL)
        fatalError("TTM resource %s not found.", searchString);

    return result;

    */
}

