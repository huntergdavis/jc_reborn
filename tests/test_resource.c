/*
 *  Unit tests for resource.c
 *
 *  Tests resource loading, parsing, and lookup functions
 *  Note: These tests require RESOURCE.MAP and RESOURCE.001 in jc_resources/
 */

#include "unity/unity.h"
#include "../mytypes.h"
#include "../utils.h"
#include "../resource.h"
#include <string.h>
#include <stdio.h>

static int resourcesLoaded = 0;

void setUp(void) {
    // Load resources once for all tests
    if (!resourcesLoaded) {
        // Try to load from jc_resources directory
        FILE *test = fopen("jc_resources/RESOURCE.MAP", "r");
        if (test != NULL) {
            fclose(test);
            parseResourceFiles("jc_resources/RESOURCE.MAP");
            resourcesLoaded = 1;
        }
    }
}

void tearDown(void) {
    // Resources stay loaded across tests for efficiency
}

// Test that resources were loaded successfully
void test_resources_loaded(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping resource tests");
    }

    extern int numAdsResources;
    extern int numBmpResources;
    extern int numTtmResources;
    extern int numScrResources;
    extern int numPalResources;

    TEST_ASSERT_GREATER_THAN(0, numAdsResources);
    TEST_ASSERT_GREATER_THAN(0, numBmpResources);
    TEST_ASSERT_GREATER_THAN(0, numTtmResources);
    TEST_ASSERT_GREATER_THAN(0, numScrResources);
    TEST_ASSERT_GREATER_THAN(0, numPalResources);
}

// Test findTtmResource with known TTM name
void test_findTtmResource_finds_existing(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping");
    }

    // Try to find INTRO.TTM (known to exist in Johnny Castaway)
    struct TTtmResource *ttm = findTtmResource("INTRO");
    TEST_ASSERT_NOT_NULL(ttm);
    TEST_ASSERT_NOT_NULL(ttm->resName);
    TEST_ASSERT_EQUAL_STRING("INTRO", ttm->resName);
}

// Test findTtmResource returns NULL for non-existent resource
void test_findTtmResource_returns_null_for_nonexistent(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping");
    }

    struct TTtmResource *ttm = findTtmResource("NONEXISTENT_RESOURCE_XYZ");
    TEST_ASSERT_NULL(ttm);
}

// Test findAdsResource with known ADS name
void test_findAdsResource_finds_existing(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping");
    }

    // Try to find ISLAND.ADS (known to exist)
    struct TAdsResource *ads = findAdsResource("ISLAND");
    TEST_ASSERT_NOT_NULL(ads);
    TEST_ASSERT_NOT_NULL(ads->resName);
    TEST_ASSERT_EQUAL_STRING("ISLAND", ads->resName);
}

// Test findAdsResource returns NULL for non-existent resource
void test_findAdsResource_returns_null_for_nonexistent(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping");
    }

    struct TAdsResource *ads = findAdsResource("NONEXISTENT_ADS_XYZ");
    TEST_ASSERT_NULL(ads);
}

// Test findBmpResource with known BMP name
void test_findBmpResource_finds_existing(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping");
    }

    // BMP resources exist - try to find one
    extern struct TBmpResource *bmpResources[];
    extern int numBmpResources;

    if (numBmpResources > 0) {
        const char *firstBmpName = bmpResources[0]->resName;
        struct TBmpResource *bmp = findBmpResource(firstBmpName);
        TEST_ASSERT_NOT_NULL(bmp);
        TEST_ASSERT_EQUAL_STRING(firstBmpName, bmp->resName);
    }
}

// Test findScrResource with known SCR name
void test_findScrResource_finds_existing(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping");
    }

    // SCR resources exist - try to find one
    extern struct TScrResource *scrResources[];
    extern int numScrResources;

    if (numScrResources > 0) {
        const char *firstScrName = scrResources[0]->resName;
        struct TScrResource *scr = findScrResource(firstScrName);
        TEST_ASSERT_NOT_NULL(scr);
        TEST_ASSERT_EQUAL_STRING(firstScrName, scr->resName);
    }
}

// Test TTM resource has valid structure
void test_ttmResource_has_valid_structure(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping");
    }

    struct TTtmResource *ttm = findTtmResource("INTRO");
    if (ttm == NULL) {
        TEST_IGNORE_MESSAGE("INTRO.TTM not found");
    }

    TEST_ASSERT_NOT_NULL(ttm->resName);
    TEST_ASSERT_NOT_NULL(ttm->versionString);
    TEST_ASSERT_GREATER_THAN(0, ttm->versionSize);
    TEST_ASSERT_NOT_NULL(ttm->uncompressedData);
    TEST_ASSERT_GREATER_THAN(0, ttm->uncompressedSize);
    TEST_ASSERT_GREATER_OR_EQUAL(0, ttm->numTags);
}

// Test ADS resource has valid structure
void test_adsResource_has_valid_structure(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping");
    }

    struct TAdsResource *ads = findAdsResource("ISLAND");
    if (ads == NULL) {
        TEST_IGNORE_MESSAGE("ISLAND.ADS not found");
    }

    TEST_ASSERT_NOT_NULL(ads->resName);
    TEST_ASSERT_NOT_NULL(ads->versionString);
    TEST_ASSERT_GREATER_THAN(0, ads->versionSize);
    TEST_ASSERT_NOT_NULL(ads->uncompressedData);
    TEST_ASSERT_GREATER_THAN(0, ads->uncompressedSize);
    TEST_ASSERT_GREATER_OR_EQUAL(0, ads->numTags);
    TEST_ASSERT_GREATER_OR_EQUAL(0, ads->numRes);
}

// Test BMP resource has valid structure
void test_bmpResource_has_valid_structure(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping");
    }

    extern struct TBmpResource *bmpResources[];
    extern int numBmpResources;

    if (numBmpResources > 0) {
        struct TBmpResource *bmp = bmpResources[0];
        TEST_ASSERT_NOT_NULL(bmp->resName);
        TEST_ASSERT_GREATER_THAN(0, bmp->width);
        TEST_ASSERT_GREATER_THAN(0, bmp->height);
        TEST_ASSERT_NOT_NULL(bmp->uncompressedData);
        TEST_ASSERT_GREATER_THAN(0, bmp->uncompressedSize);
    }
}

// Test resource arrays are properly populated
void test_resource_arrays_populated(void) {
    if (!resourcesLoaded) {
        TEST_IGNORE_MESSAGE("RESOURCE files not found - skipping");
    }

    extern struct TAdsResource *adsResources[];
    extern struct TBmpResource *bmpResources[];
    extern struct TTtmResource *ttmResources[];
    extern int numAdsResources;
    extern int numBmpResources;
    extern int numTtmResources;

    // Check that arrays have non-null entries
    for (int i = 0; i < numAdsResources; i++) {
        TEST_ASSERT_NOT_NULL(adsResources[i]);
        TEST_ASSERT_NOT_NULL(adsResources[i]->resName);
    }

    for (int i = 0; i < numBmpResources; i++) {
        TEST_ASSERT_NOT_NULL(bmpResources[i]);
        TEST_ASSERT_NOT_NULL(bmpResources[i]->resName);
    }

    for (int i = 0; i < numTtmResources; i++) {
        TEST_ASSERT_NOT_NULL(ttmResources[i]);
        TEST_ASSERT_NOT_NULL(ttmResources[i]->resName);
    }
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_resources_loaded);
    RUN_TEST(test_findTtmResource_finds_existing);
    RUN_TEST(test_findTtmResource_returns_null_for_nonexistent);
    RUN_TEST(test_findAdsResource_finds_existing);
    RUN_TEST(test_findAdsResource_returns_null_for_nonexistent);
    RUN_TEST(test_findBmpResource_finds_existing);
    RUN_TEST(test_findScrResource_finds_existing);
    RUN_TEST(test_ttmResource_has_valid_structure);
    RUN_TEST(test_adsResource_has_valid_structure);
    RUN_TEST(test_bmpResource_has_valid_structure);
    RUN_TEST(test_resource_arrays_populated);

    return UNITY_END();
}
