/*
 *  Unit tests for utils.c
 *
 *  Tests the utility functions used throughout jc_reborn
 */

#include "unity/unity.h"
#include "../mytypes.h"
#include "../utils.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

// Test safe_malloc allocates correctly
void test_safe_malloc_allocates_memory(void) {
    void *ptr = safe_malloc(100);
    TEST_ASSERT_NOT_NULL(ptr);
    free(ptr);
}

// Test safe_malloc with zero size
void test_safe_malloc_zero_size(void) {
    void *ptr = safe_malloc(0);
    // malloc(0) is implementation defined, but should not crash
    if (ptr != NULL) {
        free(ptr);
    }
    TEST_PASS();
}

// Test readUint8 reads correct byte value
void test_readUint8_reads_correct_value(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    fputc(0x42, f);
    rewind(f);

    uint8 value = readUint8(f);
    TEST_ASSERT_EQUAL_UINT8(0x42, value);

    fclose(f);
}

// Test readUint16 reads correct little-endian value
void test_readUint16_reads_little_endian(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // Write 0x1234 in little-endian (0x34, 0x12)
    fputc(0x34, f);
    fputc(0x12, f);
    rewind(f);

    uint16 value = readUint16(f);
    TEST_ASSERT_EQUAL_UINT16(0x1234, value);

    fclose(f);
}

// Test readUint32 reads correct little-endian value
void test_readUint32_reads_little_endian(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // Write 0x12345678 in little-endian (0x78, 0x56, 0x34, 0x12)
    fputc(0x78, f);
    fputc(0x56, f);
    fputc(0x34, f);
    fputc(0x12, f);
    rewind(f);

    uint32 value = readUint32(f);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, value);

    fclose(f);
}

// Test readUint16 with maximum value
void test_readUint16_max_value(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    // Write 0xFFFF (maximum uint16)
    fputc(0xFF, f);
    fputc(0xFF, f);
    rewind(f);

    uint16 value = readUint16(f);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, value);

    fclose(f);
}

// Test readUint8Block allocates and reads correctly
void test_readUint8Block_reads_data(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    const uint8 expected[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    fwrite(expected, 1, 5, f);
    rewind(f);

    uint8 *block = readUint8Block(f, 5);
    TEST_ASSERT_NOT_NULL(block);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, block, 5);

    free(block);
    fclose(f);
}

// Test getString reads null-terminated string
void test_getString_reads_string(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    const char *expected = "Hello";
    fprintf(f, "%s", expected);
    // Pad with nulls to fill maxLen
    for (int i = strlen(expected); i < 10; i++) {
        fputc(0, f);
    }
    rewind(f);

    char *str = getString(f, 10);
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_EQUAL_STRING(expected, str);

    free(str);
    fclose(f);
}

// Test getString handles full-length string (no null terminator)
void test_getString_handles_full_length_string(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    const char *input = "1234567890"; // 10 chars, no null
    fwrite(input, 1, 10, f);
    rewind(f);

    char *str = getString(f, 10);
    TEST_ASSERT_NOT_NULL(str);
    // Should be null-terminated by getString
    TEST_ASSERT_EQUAL_INT(10, strlen(str));
    TEST_ASSERT_EQUAL_STRING(input, str);

    free(str);
    fclose(f);
}

// Test debugMode can be toggled
void test_debugMode_toggle(void) {
    extern int debugMode;

    int original = debugMode;
    debugMode = 1;
    TEST_ASSERT_EQUAL(1, debugMode);

    debugMode = 0;
    TEST_ASSERT_EQUAL(0, debugMode);

    debugMode = original; // Restore
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_safe_malloc_allocates_memory);
    RUN_TEST(test_safe_malloc_zero_size);
    RUN_TEST(test_readUint8_reads_correct_value);
    RUN_TEST(test_readUint16_reads_little_endian);
    RUN_TEST(test_readUint32_reads_little_endian);
    RUN_TEST(test_readUint16_max_value);
    RUN_TEST(test_readUint8Block_reads_data);
    RUN_TEST(test_getString_reads_string);
    RUN_TEST(test_getString_handles_full_length_string);
    RUN_TEST(test_debugMode_toggle);

    return UNITY_END();
}
