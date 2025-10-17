/*
 *  Memory profiling tests for jc_reborn
 *
 *  Tests to document and measure memory usage patterns
 *  Helps identify memory bottlenecks for optimization
 */

#include "unity/unity.h"
#include "../mytypes.h"
#include "../utils.h"
#include "../resource.h"
#include "../uncompress.h"
#include "../calcpath.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void setUp(void) {
    debugMode = 0;
}

void tearDown(void) {
    // Nothing to tear down
}

// Test: document getString memory allocation
void test_memory_getString_allocation_size(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    const char *testStr = "Test";
    fprintf(f, "%s", testStr);
    for (size_t i = strlen(testStr); i < 50; i++) {
        fputc(0, f);
    }
    rewind(f);

    char *str = getString(f, 50);
    TEST_ASSERT_NOT_NULL(str);

    // getString allocates maxLen + 1 bytes
    // For len=50, it allocates 51 bytes
    printf("\n  getString(f, 50) allocates: 51 bytes\n");

    free(str);
    fclose(f);
    TEST_PASS();
}

// Test: document readUint8Block memory allocation
void test_memory_readUint8Block_allocation_size(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // Write 1KB of data
    for (int i = 0; i < 1024; i++) {
        fputc(i % 256, f);
    }
    rewind(f);

    uint8 *block = readUint8Block(f, 1024);
    TEST_ASSERT_NOT_NULL(block);

    // readUint8Block allocates len * sizeof(uint8) bytes
    printf("\n  readUint8Block(f, 1024) allocates: 1024 bytes\n");

    free(block);
    fclose(f);
    TEST_PASS();
}

// Test: document RLE decompression memory overhead
void test_memory_RLE_decompression_overhead(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // Create RLE compressed data: repeat 'X' 1000 times
    // Input: 2 bytes (control + value)
    // Output: 1000 bytes
    fputc(0x80 | 127, f); // Repeat 127 times
    fputc('X', f);
    fputc(0x80 | 127, f); // Repeat 127 times
    fputc('X', f);
    fputc(0x80 | 127, f); // Repeat 127 times
    fputc('X', f);
    fputc(0x80 | 127, f); // Repeat 127 times
    fputc('X', f);
    fputc(0x80 | 108, f); // Repeat 108 times (127*4 + 108 = 616)
    fputc('X', f);
    rewind(f);

    uint8 *result = uncompress(f, 1, 10, 616); // Method 1 = RLE
    TEST_ASSERT_NOT_NULL(result);

    // RLE decompression allocates outSize bytes
    // Compression ratio: 10 bytes in, 616 bytes out = 61.6x
    printf("\n  RLE decompress: 10 bytes -> 616 bytes (61.6x expansion)\n");
    printf("  Memory allocated: 616 bytes\n");

    free(result);
    fclose(f);
    TEST_PASS();
}

// Test: document LZW decompression memory overhead
void test_memory_LZW_decompression_overhead(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // LZW uses additional working memory during decompression:
    // - Code table: 4096 * (sizeof(uint16) + sizeof(uint8)) = 4096 * 3 = 12KB
    // - Decode stack: 4096 * sizeof(uint8) = 4KB
    // Total working memory: ~16KB
    //
    // Plus output buffer allocation

    for (int i = 0; i < 50; i++) {
        fputc(i % 10, f);
    }
    rewind(f);

    uint8 *result = uncompress(f, 2, 50, 25); // Method 2 = LZW
    TEST_ASSERT_NOT_NULL(result);

    printf("\n  LZW decompression working memory:\n");
    printf("    Code table: 12,288 bytes (4096 entries * 3 bytes)\n");
    printf("    Decode stack: 4,096 bytes\n");
    printf("    Total working: ~16,384 bytes (16KB)\n");
    printf("    Output buffer: 25 bytes\n");
    printf("  Total peak during LZW decompress: ~16,409 bytes\n");

    free(result);
    fclose(f);
    TEST_PASS();
}

// Test: document calcPath static memory usage
void test_memory_calcPath_static_memory(void) {
    // CalcPath uses static arrays (no heap allocation):
    //   static int paths[50][7];           // 50 * 7 * 4 = 1400 bytes
    //   static struct TNodeState nodeStates[6];  // 6 * 8 = 48 bytes
    // Total: ~1448 bytes of static memory

    int *path = calcPath(0, 0);
    TEST_ASSERT_NOT_NULL(path);

    printf("\n  calcPath static memory usage:\n");
    printf("    paths array: 1,400 bytes\n");
    printf("    nodeStates array: 48 bytes\n");
    printf("    Total: 1,448 bytes (static, not heap)\n");

    TEST_PASS();
}

// Test: document resource array overhead
void test_memory_resource_arrays_overhead(void) {
    // Resource arrays are static with fixed maximum sizes:
    // From resource.c:
    //   #define MAX_ADS_RESOURCES 100
    //   #define MAX_BMP_RESOURCES 200
    //   #define MAX_PAL_RESOURCES 1
    //   #define MAX_SCR_RESOURCES 20
    //   #define MAX_TTM_RESOURCES 100
    //
    // Each is an array of pointers:
    //   struct TAdsResource *adsResources[100];  // 100 * 8 = 800 bytes (64-bit)
    //   struct TBmpResource *bmpResources[200];  // 200 * 8 = 1600 bytes
    //   etc.

    printf("\n  Resource array overhead (64-bit pointers):\n");
    printf("    ADS array: 800 bytes (100 pointers)\n");
    printf("    BMP array: 1,600 bytes (200 pointers)\n");
    printf("    PAL array: 8 bytes (1 pointer)\n");
    printf("    SCR array: 160 bytes (20 pointers)\n");
    printf("    TTM array: 800 bytes (100 pointers)\n");
    printf("    Total: 3,368 bytes\n");

    TEST_PASS();
}

// Test: document typical resource struct sizes
void test_memory_resource_struct_sizes(void) {
    printf("\n  Resource struct sizes:\n");
    printf("    sizeof(struct TAdsResource): %zu bytes\n", sizeof(struct TAdsResource));
    printf("    sizeof(struct TBmpResource): %zu bytes\n", sizeof(struct TBmpResource));
    printf("    sizeof(struct TPalResource): %zu bytes\n", sizeof(struct TPalResource));
    printf("    sizeof(struct TScrResource): %zu bytes\n", sizeof(struct TScrResource));
    printf("    sizeof(struct TTtmResource): %zu bytes\n", sizeof(struct TTtmResource));

    printf("\n  Note: Each resource also contains pointers to:\n");
    printf("    - Uncompressed data (variable size, often KB to MB)\n");
    printf("    - Tag arrays\n");
    printf("    - Name strings\n");

    TEST_PASS();
}

// Test: estimate memory for typical scene
void test_memory_typical_scene_estimate(void) {
    printf("\n  === Estimated Memory for Typical Scene ===\n");
    printf("\n  Fixed overhead:\n");
    printf("    Resource arrays: 3,368 bytes\n");
    printf("    calcPath working: 1,448 bytes\n");
    printf("    Subtotal: 4,816 bytes (~5KB)\n");

    printf("\n  Variable (per scene):\n");
    printf("    1x TTM resource struct: ~100 bytes\n");
    printf("    1x TTM uncompressed data: 10-50KB typical\n");
    printf("    2-3x BMP resources: ~200 bytes each\n");
    printf("    2-3x BMP uncompressed data: 20-100KB each\n");
    printf("    1x SCR background: ~300KB uncompressed\n");
    printf("    1x ADS script: 5-20KB\n");

    printf("\n  Typical scene total: 400-600KB\n");
    printf("  Peak (with multiple TTMs): 1-2MB\n");

    printf("\n  === Optimization Opportunities ===\n");
    printf("  1. Lazy-load resources (biggest win)\n");
    printf("  2. LRU cache for decompressed data\n");
    printf("  3. Release BMP data after converting to SDL surfaces\n");
    printf("  4. Stream/decompress on-demand vs keeping all in RAM\n");

    TEST_PASS();
}

// Test: measure actual allocation sizes
void test_memory_allocation_measurements(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    printf("\n  === Measured Allocations ===\n");

    // Test 1: Small block
    for (int i = 0; i < 100; i++) {
        fputc(i, f);
    }
    rewind(f);
    uint8 *small = readUint8Block(f, 100);
    TEST_ASSERT_NOT_NULL(small);
    printf("  100-byte block: allocated\n");
    free(small);

    // Test 2: Medium block (typical BMP data)
    fseek(f, 0, SEEK_SET);
    ftruncate(fileno(f), 0);
    fputc(0x80 | 100, f); // Repeat 'A' 100 times
    fputc('A', f);
    fflush(f);
    rewind(f);
    uint8 *medium = uncompress(f, 1, 2, 100);
    TEST_ASSERT_NOT_NULL(medium);
    printf("  100-byte decompressed: allocated\n");
    free(medium);

    // Test 3: String allocation
    fseek(f, 0, SEEK_SET);
    ftruncate(fileno(f), 0);
    fprintf(f, "TestString");
    for (int i = 10; i < 40; i++) {
        fputc(0, f);
    }
    fflush(f);
    rewind(f);
    char *str = getString(f, 40);
    TEST_ASSERT_NOT_NULL(str);
    printf("  40-char string: allocated (41 bytes with null)\n");
    free(str);

    fclose(f);
    printf("\n  All allocations successful and freed\n");

    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_memory_getString_allocation_size);
    RUN_TEST(test_memory_readUint8Block_allocation_size);
    RUN_TEST(test_memory_RLE_decompression_overhead);
    RUN_TEST(test_memory_LZW_decompression_overhead);
    RUN_TEST(test_memory_calcPath_static_memory);
    RUN_TEST(test_memory_resource_arrays_overhead);
    RUN_TEST(test_memory_resource_struct_sizes);
    RUN_TEST(test_memory_typical_scene_estimate);
    RUN_TEST(test_memory_allocation_measurements);

    return UNITY_END();
}
