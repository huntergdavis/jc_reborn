/*
 * Analyze resource sizes to guide memory optimization
 */

#include <stdio.h>
#include <stdlib.h>
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

int main(void) {
    printf("Parsing resources...\n");
    parseResourceFiles("RESOURCE.MAP");

    size_t totalCompressed = 0;
    size_t totalUncompressed = 0;

    printf("\n=== ADS Resources ===\n");
    size_t adsUncompressed = 0;
    for (int i = 0; i < numAdsResources; i++) {
        struct TAdsResource *ads = adsResources[i];
        printf("  %s: %u bytes uncompressed\n", ads->resName, ads->uncompressedSize);
        adsUncompressed += ads->uncompressedSize;
        totalUncompressed += ads->uncompressedSize;
        totalCompressed += ads->compressedSize;
    }
    printf("  Total ADS: %zu bytes (%.2f KB)\n", adsUncompressed, adsUncompressed / 1024.0);

    printf("\n=== BMP Resources ===\n");
    size_t bmpUncompressed = 0;
    for (int i = 0; i < numBmpResources && i < 10; i++) {  // Show first 10
        struct TBmpResource *bmp = bmpResources[i];
        printf("  %s: %u bytes uncompressed\n", bmp->resName, bmp->uncompressedSize);
        bmpUncompressed += bmp->uncompressedSize;
    }
    for (int i = 0; i < numBmpResources; i++) {
        bmpUncompressed += bmpResources[i]->uncompressedSize;
        totalUncompressed += bmpResources[i]->uncompressedSize;
        totalCompressed += bmpResources[i]->compressedSize;
    }
    printf("  ... (%d total BMP resources)\n", numBmpResources);
    printf("  Total BMP: %zu bytes (%.2f KB)\n", bmpUncompressed, bmpUncompressed / 1024.0);

    printf("\n=== SCR Resources ===\n");
    size_t scrUncompressed = 0;
    for (int i = 0; i < numScrResources; i++) {
        struct TScrResource *scr = scrResources[i];
        printf("  %s: %u bytes uncompressed\n", scr->resName, scr->uncompressedSize);
        scrUncompressed += scr->uncompressedSize;
        totalUncompressed += scr->uncompressedSize;
        totalCompressed += scr->compressedSize;
    }
    printf("  Total SCR: %zu bytes (%.2f KB)\n", scrUncompressed, scrUncompressed / 1024.0);

    printf("\n=== TTM Resources ===\n");
    size_t ttmUncompressed = 0;
    for (int i = 0; i < numTtmResources && i < 10; i++) {  // Show first 10
        struct TTtmResource *ttm = ttmResources[i];
        printf("  %s: %u bytes uncompressed\n", ttm->resName, ttm->uncompressedSize);
        ttmUncompressed += ttm->uncompressedSize;
    }
    for (int i = 0; i < numTtmResources; i++) {
        ttmUncompressed += ttmResources[i]->uncompressedSize;
        totalUncompressed += ttmResources[i]->uncompressedSize;
        totalCompressed += ttmResources[i]->compressedSize;
    }
    printf("  ... (%d total TTM resources)\n", numTtmResources);
    printf("  Total TTM: %zu bytes (%.2f KB)\n", ttmUncompressed, ttmUncompressed / 1024.0);

    printf("\n=== SUMMARY ===\n");
    printf("Total compressed: %zu bytes (%.2f MB)\n", totalCompressed, totalCompressed / (1024.0 * 1024.0));
    printf("Total uncompressed: %zu bytes (%.2f MB)\n", totalUncompressed, totalUncompressed / (1024.0 * 1024.0));
    printf("Compression ratio: %.2fx\n", (double)totalUncompressed / totalCompressed);

    printf("\n=== MEMORY OPTIMIZATION TARGETS ===\n");
    printf("1. BMP data: %.2f MB (largest consumer)\n", bmpUncompressed / (1024.0 * 1024.0));
    printf("2. SCR data: %.2f MB\n", scrUncompressed / (1024.0 * 1024.0));
    printf("3. TTM data: %.2f KB\n", ttmUncompressed / 1024.0);
    printf("4. ADS data: %.2f KB\n", adsUncompressed / 1024.0);

    return 0;
}
