/*
 *  Unit tests for uncompress.c
 *
 *  Tests RLE and LZW decompression algorithms
 */

#include "unity/unity.h"
#include "../mytypes.h"
#include "../utils.h"
#include "../uncompress.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {
    // Called before each test
    debugMode = 0;
}

void tearDown(void) {
    // Called after each test
}

// Test uncompress with invalid compression method
void test_uncompress_invalid_method(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // Write some dummy data
    fputc(0x00, f);
    rewind(f);

    // Method 0 is invalid, should return NULL
    uint8 *result = uncompress(f, 0, 1, 10);
    TEST_ASSERT_NULL(result);

    // Method 3 is invalid, should return NULL
    rewind(f);
    result = uncompress(f, 3, 1, 10);
    TEST_ASSERT_NULL(result);

    fclose(f);
}

// Test RLE decompression with simple literal run
void test_uncompressRLE_literal_run(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // RLE format: control byte (0x00-0x7F = literal count), then literal bytes
    // Control byte: 0x05 = 5 literal bytes
    fputc(0x05, f); // 5 bytes follow literally
    fputc(0x41, f); // 'A'
    fputc(0x42, f); // 'B'
    fputc(0x43, f); // 'C'
    fputc(0x44, f); // 'D'
    fputc(0x45, f); // 'E'
    rewind(f);

    uint8 *result = uncompress(f, 1, 6, 5); // method 1 = RLE, inSize=6, outSize=5
    TEST_ASSERT_NOT_NULL(result);

    // Verify decompressed data
    TEST_ASSERT_EQUAL_UINT8('A', result[0]);
    TEST_ASSERT_EQUAL_UINT8('B', result[1]);
    TEST_ASSERT_EQUAL_UINT8('C', result[2]);
    TEST_ASSERT_EQUAL_UINT8('D', result[3]);
    TEST_ASSERT_EQUAL_UINT8('E', result[4]);

    free(result);
    fclose(f);
}

// Test RLE decompression with run-length encoding (repeated byte)
void test_uncompressRLE_repeated_byte(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // RLE format: control byte with high bit set (0x80-0xFF) = repeat count
    // Control byte: 0x85 = repeat next byte 5 times (0x80 | 0x05)
    fputc(0x85, f); // Repeat next byte 5 times
    fputc(0x58, f); // 'X'
    rewind(f);

    uint8 *result = uncompress(f, 1, 2, 5); // method 1 = RLE, inSize=2, outSize=5
    TEST_ASSERT_NOT_NULL(result);

    // Verify all 5 bytes are 'X'
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT8('X', result[i]);
    }

    free(result);
    fclose(f);
}

// Test RLE decompression with mixed literal and repeated runs
void test_uncompressRLE_mixed_runs(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // First: 3 literal bytes
    fputc(0x03, f);
    fputc(0x41, f); // 'A'
    fputc(0x42, f); // 'B'
    fputc(0x43, f); // 'C'

    // Second: repeat 'Z' 4 times
    fputc(0x84, f); // 0x80 | 0x04
    fputc(0x5A, f); // 'Z'

    rewind(f);

    uint8 *result = uncompress(f, 1, 6, 7); // inSize=6, outSize=7
    TEST_ASSERT_NOT_NULL(result);

    // Verify: ABC ZZZZ
    TEST_ASSERT_EQUAL_UINT8('A', result[0]);
    TEST_ASSERT_EQUAL_UINT8('B', result[1]);
    TEST_ASSERT_EQUAL_UINT8('C', result[2]);
    TEST_ASSERT_EQUAL_UINT8('Z', result[3]);
    TEST_ASSERT_EQUAL_UINT8('Z', result[4]);
    TEST_ASSERT_EQUAL_UINT8('Z', result[5]);
    TEST_ASSERT_EQUAL_UINT8('Z', result[6]);

    free(result);
    fclose(f);
}

// Test RLE with maximum repeat count (127 = 0x7F | 0x80)
void test_uncompressRLE_max_repeat(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // Maximum repeat count: 0xFF = 127 repeats
    fputc(0xFF, f); // 0x80 | 0x7F = repeat 127 times
    fputc(0x42, f); // 'B'
    rewind(f);

    uint8 *result = uncompress(f, 1, 2, 127);
    TEST_ASSERT_NOT_NULL(result);

    // Verify all 127 bytes are 'B'
    for (int i = 0; i < 127; i++) {
        TEST_ASSERT_EQUAL_UINT8('B', result[i]);
    }

    free(result);
    fclose(f);
}

// Test LZW decompression with simple uncompressed data
void test_uncompressLZW_simple_data(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // Create simple LZW compressed data
    // LZW starts with 9-bit codes
    // This is a minimal test with literal codes
    // Note: Creating valid LZW data by hand is complex, so we test with simple patterns

    // Write a simple pattern: codes for 'A', 'B', 'C' (as 9-bit values)
    // This is a simplified test - real LZW data would come from actual compression

    // For a more realistic test, we'd need actual compressed data from the game
    // For now, test that the function accepts the parameters correctly

    // Write some bytes that won't cause a crash
    for (int i = 0; i < 20; i++) {
        fputc(0x41 + (i % 3), f); // Pattern of ABC...
    }
    rewind(f);

    uint8 *result = uncompress(f, 2, 20, 10); // method 2 = LZW
    TEST_ASSERT_NOT_NULL(result);

    // Just verify we got a result and it allocated memory
    // Real validation would require known good LZW compressed data
    free(result);
    fclose(f);
}

// Test uncompress method selection
void test_uncompress_method_selection(void) {
    FILE *f1 = tmpfile();
    FILE *f2 = tmpfile();
    TEST_ASSERT_NOT_NULL(f1);
    TEST_ASSERT_NOT_NULL(f2);

    // Prepare simple RLE data for method 1
    fputc(0x03, f1);
    fputc(0x11, f1);
    fputc(0x22, f1);
    fputc(0x33, f1);
    rewind(f1);

    // Test method 1 (RLE)
    uint8 *result1 = uncompress(f1, 1, 4, 3);
    TEST_ASSERT_NOT_NULL(result1);
    free(result1);

    // Prepare some data for method 2 (LZW)
    for (int i = 0; i < 10; i++) {
        fputc(i, f2);
    }
    rewind(f2);

    // Test method 2 (LZW)
    uint8 *result2 = uncompress(f2, 2, 10, 5);
    TEST_ASSERT_NOT_NULL(result2);
    free(result2);

    fclose(f1);
    fclose(f2);
}

// Test RLE with single byte output
void test_uncompressRLE_single_byte(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // Single literal byte
    fputc(0x01, f);
    fputc(0x77, f);
    rewind(f);

    uint8 *result = uncompress(f, 1, 2, 1);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_UINT8(0x77, result[0]);

    free(result);
    fclose(f);
}

// Test RLE with zero-length literal run
void test_uncompressRLE_zero_literal(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // Control byte 0x00 means 0 literal bytes (edge case)
    // Then a repeat run
    fputc(0x00, f); // 0 literals
    fputc(0x83, f); // Repeat 3 times
    fputc(0x99, f); // Value to repeat
    rewind(f);

    uint8 *result = uncompress(f, 1, 3, 3);
    TEST_ASSERT_NOT_NULL(result);

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x99, result[i]);
    }

    free(result);
    fclose(f);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_uncompress_invalid_method);
    RUN_TEST(test_uncompressRLE_literal_run);
    RUN_TEST(test_uncompressRLE_repeated_byte);
    RUN_TEST(test_uncompressRLE_mixed_runs);
    RUN_TEST(test_uncompressRLE_max_repeat);
    RUN_TEST(test_uncompressLZW_simple_data);
    RUN_TEST(test_uncompress_method_selection);
    RUN_TEST(test_uncompressRLE_single_byte);
    RUN_TEST(test_uncompressRLE_zero_literal);

    return UNITY_END();
}
