/*
 * Extract all resources from RESOURCE.001 to individual files
 *
 * This tool extracts and decompresses all resources to separate files
 * on disk, allowing the game to stream them directly without keeping
 * decompressed data in memory.
 *
 * Output directory structure:
 *   extracted/
 *     ads/
 *     bmp/
 *     pal/
 *     scr/
 *     ttm/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "mytypes.h"
#include "utils.h"
#include "resource.h"

extern struct TAdsResource *adsResources[];
extern struct TBmpResource *bmpResources[];
extern struct TPalResource *palResources[];
extern struct TScrResource *scrResources[];
extern struct TTtmResource *ttmResources[];
extern int numAdsResources;
extern int numBmpResources;
extern int numPalResources;
extern int numScrResources;
extern int numTtmResources;

/* Create directory if it doesn't exist */
static int createDir(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0755) == -1) {
            fprintf(stderr, "Failed to create directory %s: %s\n", path, strerror(errno));
            return 0;
        }
    }
    return 1;
}

/* Extract ADS resources */
static int extractAdsResources(const char *baseDir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/ads", baseDir);
    if (!createDir(path)) return 0;

    printf("\nExtracting %d ADS resources...\n", numAdsResources);
    for (int i = 0; i < numAdsResources; i++) {
        struct TAdsResource *ads = adsResources[i];
        snprintf(path, sizeof(path), "%s/ads/%s", baseDir, ads->resName);

        FILE *f = fopen(path, "wb");
        if (!f) {
            fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
            return 0;
        }

        fwrite(ads->uncompressedData, 1, ads->uncompressedSize, f);
        fclose(f);
        printf("  %s: %u bytes\n", ads->resName, ads->uncompressedSize);
    }
    return 1;
}

/* Extract BMP resources */
static int extractBmpResources(const char *baseDir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/bmp", baseDir);
    if (!createDir(path)) return 0;

    printf("\nExtracting %d BMP resources...\n", numBmpResources);
    for (int i = 0; i < numBmpResources; i++) {
        struct TBmpResource *bmp = bmpResources[i];
        snprintf(path, sizeof(path), "%s/bmp/%s", baseDir, bmp->resName);

        FILE *f = fopen(path, "wb");
        if (!f) {
            fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
            return 0;
        }

        fwrite(bmp->uncompressedData, 1, bmp->uncompressedSize, f);
        fclose(f);

        if (i < 10 || i % 20 == 0) {
            printf("  %s: %u bytes\n", bmp->resName, bmp->uncompressedSize);
        }
    }
    printf("  ... (%d total)\n", numBmpResources);
    return 1;
}

/* Extract PAL resources */
static int extractPalResources(const char *baseDir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/pal", baseDir);
    if (!createDir(path)) return 0;

    printf("\nExtracting %d PAL resources...\n", numPalResources);
    for (int i = 0; i < numPalResources; i++) {
        struct TPalResource *pal = palResources[i];
        snprintf(path, sizeof(path), "%s/pal/%s", baseDir, pal->resName);

        FILE *f = fopen(path, "wb");
        if (!f) {
            fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
            return 0;
        }

        /* PAL resources have colors array, not compressed data */
        fwrite(pal->colors, sizeof(struct TColor), 256, f);
        fclose(f);
        printf("  %s: 768 bytes (256 RGB colors)\n", pal->resName);
    }
    return 1;
}

/* Extract SCR resources */
static int extractScrResources(const char *baseDir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/scr", baseDir);
    if (!createDir(path)) return 0;

    printf("\nExtracting %d SCR resources...\n", numScrResources);
    for (int i = 0; i < numScrResources; i++) {
        struct TScrResource *scr = scrResources[i];
        snprintf(path, sizeof(path), "%s/scr/%s", baseDir, scr->resName);

        FILE *f = fopen(path, "wb");
        if (!f) {
            fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
            return 0;
        }

        fwrite(scr->uncompressedData, 1, scr->uncompressedSize, f);
        fclose(f);
        printf("  %s: %u bytes\n", scr->resName, scr->uncompressedSize);
    }
    return 1;
}

/* Extract TTM resources */
static int extractTtmResources(const char *baseDir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/ttm", baseDir);
    if (!createDir(path)) return 0;

    printf("\nExtracting %d TTM resources...\n", numTtmResources);
    for (int i = 0; i < numTtmResources; i++) {
        struct TTtmResource *ttm = ttmResources[i];
        snprintf(path, sizeof(path), "%s/ttm/%s", baseDir, ttm->resName);

        FILE *f = fopen(path, "wb");
        if (!f) {
            fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
            return 0;
        }

        fwrite(ttm->uncompressedData, 1, ttm->uncompressedSize, f);
        fclose(f);

        if (i < 10 || i % 10 == 0) {
            printf("  %s: %u bytes\n", ttm->resName, ttm->uncompressedSize);
        }
    }
    printf("  ... (%d total)\n", numTtmResources);
    return 1;
}

int main(int argc, char *argv[]) {
    const char *outputDir = "extracted";

    if (argc > 1) {
        outputDir = argv[1];
    }

    printf("Extracting resources to: %s/\n", outputDir);
    printf("Parsing RESOURCE.MAP...\n");

    parseResourceFiles("RESOURCE.MAP");

    /* Create base directory */
    if (!createDir(outputDir)) {
        return 1;
    }

    /* Extract all resource types */
    if (!extractAdsResources(outputDir)) return 1;
    if (!extractBmpResources(outputDir)) return 1;
    if (!extractPalResources(outputDir)) return 1;
    if (!extractScrResources(outputDir)) return 1;
    if (!extractTtmResources(outputDir)) return 1;

    printf("\n=== Extraction Complete ===\n");
    printf("Total resources extracted:\n");
    printf("  ADS: %d files\n", numAdsResources);
    printf("  BMP: %d files\n", numBmpResources);
    printf("  PAL: %d files\n", numPalResources);
    printf("  SCR: %d files\n", numScrResources);
    printf("  TTM: %d files\n", numTtmResources);

    return 0;
}
