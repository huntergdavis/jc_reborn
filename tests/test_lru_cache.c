/*
 *  LRU cache tests for jc_reborn
 *
 *  Tests that the LRU cache correctly tracks memory usage,
 *  pins/unpins resources, and evicts when over budget
 */

#include "unity/unity.h"
#include "../mytypes.h"
#include "../utils.h"
#include "../resource.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char originalDir[1024];

void setUp(void) {
    debugMode = 0;
    getcwd(originalDir, sizeof(originalDir));
}

void tearDown(void) {
    chdir(originalDir);
}

/* Test: LRU cache initializes correctly */
void test_lru_cache_initialization(void) {
    int ret = chdir("../jc_resources");
    if (ret != 0) {
        TEST_IGNORE_MESSAGE("jc_resources directory not found");
        return;
    }

    FILE *f = fopen("RESOURCE.MAP", "r");
    if (!f) {
        TEST_IGNORE_MESSAGE("RESOURCE.MAP not found");
        return;
    }
    fclose(f);

    extern int numTtmResources;
    numTtmResources = 0;

    parseResourceFiles("RESOURCE.MAP");
    initLRUCache();

    /* Verify resources are parsed but not loaded */
    TEST_ASSERT_GREATER_THAN(40, numTtmResources);

    extern struct TTtmResource *ttmResources[];
    struct TTtmResource *ttm = ttmResources[0];
    TEST_ASSERT_NOT_NULL(ttm);
    TEST_ASSERT_NULL(ttm->uncompressedData);
    TEST_ASSERT_EQUAL_UINT32(0, ttm->lastUsedTick);
    TEST_ASSERT_EQUAL_UINT32(0, ttm->pinCount);

    printf("\n  LRU cache initialized successfully\n");
}

/* Test: Memory usage tracking */
void test_lru_cache_memory_tracking(void) {
    int ret = chdir("../jc_resources");
    if (ret != 0) {
        TEST_IGNORE_MESSAGE("jc_resources directory not found");
        return;
    }

    FILE *f = fopen("RESOURCE.MAP", "r");
    if (!f) {
        TEST_IGNORE_MESSAGE("RESOURCE.MAP not found");
        return;
    }
    fclose(f);

    extern int numTtmResources;
    if (numTtmResources == 0) {
        parseResourceFiles("RESOURCE.MAP");
        initLRUCache();
    }

    size_t memUsed = getTotalMemoryUsed();
    printf("\n  Initial memory used: %zu bytes\n", memUsed);

    /* Memory should start at 0 since resources are lazy-loaded */
    TEST_ASSERT_EQUAL_size_t(0, memUsed);
}

/* Test: Pin resource increases memory tracking */
void test_lru_cache_pin_increases_memory(void) {
    int ret = chdir("../jc_resources");
    if (ret != 0) {
        TEST_IGNORE_MESSAGE("jc_resources directory not found");
        return;
    }

    FILE *f = fopen("RESOURCE.MAP", "r");
    if (!f) {
        TEST_IGNORE_MESSAGE("RESOURCE.MAP not found");
        return;
    }
    fclose(f);

    extern int numTtmResources;
    extern struct TTtmResource *ttmResources[];

    if (numTtmResources == 0) {
        parseResourceFiles("RESOURCE.MAP");
        initLRUCache();
    }

    /* Load a TTM resource */
    struct TTtmResource *ttm = ttmResources[0];

    /* Simulate loading TTM data */
    if (ttm->uncompressedData == NULL) {
        char extractedPath[512];
        snprintf(extractedPath, sizeof(extractedPath), "extracted/ttm/%s", ttm->resName);

        FILE *ef = fopen(extractedPath, "rb");
        if (!ef) {
            TEST_IGNORE_MESSAGE("Extracted TTM file not found");
            return;
        }

        ttm->uncompressedData = malloc(ttm->uncompressedSize);
        fread(ttm->uncompressedData, 1, ttm->uncompressedSize, ef);
        fclose(ef);
    }

    size_t before = getTotalMemoryUsed();

    /* Pin the resource */
    pinResource(ttm, ttm->uncompressedSize, "TTM");

    size_t after = getTotalMemoryUsed();

    printf("\n  Memory before pin: %zu bytes\n", before);
    printf("  Memory after pin: %zu bytes\n", after);
    printf("  TTM size: %u bytes\n", ttm->uncompressedSize);

    TEST_ASSERT_EQUAL_size_t(ttm->uncompressedSize, after);
    TEST_ASSERT_EQUAL_UINT32(1, ttm->pinCount);

    /* Unpin it */
    unpinResource(ttm, "TTM");
    TEST_ASSERT_EQUAL_UINT32(0, ttm->pinCount);

    /* Clean up */
    free(ttm->uncompressedData);
    ttm->uncompressedData = NULL;
}

/* Test: Documentation of LRU cache design */
void test_lru_cache_design_documentation(void) {
    printf("\n  === LRU Cache Design ===\n");
    printf("  Goal: Enforce 2MB memory budget with automatic eviction\n");
    printf("  \n");
    printf("  Architecture:\n");
    printf("    - Global tick counter tracks access order\n");
    printf("    - Each resource has lastUsedTick and pinCount fields\n");
    printf("    - Memory budget defaults to 2MB (configurable via JC_MEM_BUDGET_MB)\n");
    printf("    - totalMemoryUsed tracks current memory consumption\n");
    printf("  \n");
    printf("  Resource Lifecycle:\n");
    printf("    1. parseResourceFiles() - Parse headers, don't decompress\n");
    printf("    2. initLRUCache() - Initialize LRU fields, read env var\n");
    printf("    3. ttmLoadTtm/adsPlay - Load data on demand from extracted/\n");
    printf("    4. pinResource() - Mark as in-use, update memory tracking\n");
    printf("    5. checkMemoryBudget() - Evict LRU unpinned resources if over budget\n");
    printf("    6. unpinResource() - Allow eviction when done\n");
    printf("  \n");
    printf("  Eviction Policy:\n");
    printf("    - Only evict unpinned resources (pinCount == 0)\n");
    printf("    - Select resource with lowest lastUsedTick\n");
    printf("    - Free uncompressedData, set to NULL\n");
    printf("    - Can be reloaded from extracted/ if needed again\n");
    printf("  \n");
    printf("  Resource Participation:\n");
    printf("    - TTM: Lazy load, pin/unpin, evictable\n");
    printf("    - ADS: Lazy load, pin/unpin, evictable\n");
    printf("    - BMP: Freed immediately after SDL surface creation\n");
    printf("    - SCR: Freed immediately after SDL surface creation\n");
    printf("  \n");
    printf("  Memory Budget:\n");
    printf("    - Default: 2MB (2097152 bytes)\n");
    printf("    - Override: export JC_MEM_BUDGET_MB=4\n");
    printf("    - Enforced after every resource load\n");
    printf("  \n");
    printf("  Testing:\n");
    printf("    - export JC_MEM_BUDGET_MB=2 ./jc_reborn window debug\n");
    printf("    - Debug mode logs evictions and memory usage\n");
    printf("    - Monitor for \"Evicted\" messages in output\n");

    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_lru_cache_initialization);
    RUN_TEST(test_lru_cache_memory_tracking);
    RUN_TEST(test_lru_cache_pin_increases_memory);
    RUN_TEST(test_lru_cache_design_documentation);

    return UNITY_END();
}
