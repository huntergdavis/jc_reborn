/*
 *  Unit tests for config.c
 *
 *  Tests configuration file read/write operations
 *  Note: These tests will temporarily modify $HOME/.jc_reborn
 */

#include "unity/unity.h"
#include "../mytypes.h"
#include "../utils.h"
#include "../config.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>

static struct TConfig savedConfig;
static int configExisted = 0;

void setUp(void) {
    debugMode = 0;

    // Save existing config if it exists
    configExisted = 0;
    FILE *f = fopen(".jc_reborn", "r");
    if (f != NULL) {
        configExisted = 1;
        cfgFileRead(&savedConfig);
        fclose(f);
    }
}

void tearDown(void) {
    // Restore original config if it existed
    if (configExisted) {
        cfgFileWrite(&savedConfig);
    } else {
        // Remove test config if none existed before
        char *home = getenv("HOME");
        if (home != NULL) {
            char path[512];
            snprintf(path, sizeof(path), "%s/.jc_reborn", home);
            unlink(path);
        }
    }
}

// Test writing and reading config file
void test_config_write_and_read(void) {
    struct TConfig cfgWrite = {0};
    struct TConfig cfgRead = {0};

    // Set up config values
    cfgWrite.currentDay = 42;
    cfgWrite.date = 20231215;

    // Write config
    cfgFileWrite(&cfgWrite);

    // Read it back
    cfgFileRead(&cfgRead);

    // Verify values match
    TEST_ASSERT_EQUAL_INT(42, cfgRead.currentDay);
    TEST_ASSERT_EQUAL_INT(20231215, cfgRead.date);
}

// Test reading non-existent config file initializes to zeros
void test_config_read_nonexistent(void) {
    struct TConfig cfg = {999, 999}; // Initialize to non-zero

    // Delete config file
    char *home = getenv("HOME");
    if (home != NULL) {
        char path[512];
        snprintf(path, sizeof(path), "%s/.jc_reborn", home);
        unlink(path);
    }

    // Read should initialize to zeros when file doesn't exist
    cfgFileRead(&cfg);

    TEST_ASSERT_EQUAL_INT(0, cfg.currentDay);
    TEST_ASSERT_EQUAL_INT(0, cfg.date);
}

// Test writing config with zero values
void test_config_write_zeros(void) {
    struct TConfig cfgWrite = {0, 0};
    struct TConfig cfgRead = {999, 999}; // Initialize to non-zero

    cfgFileWrite(&cfgWrite);
    cfgFileRead(&cfgRead);

    TEST_ASSERT_EQUAL_INT(0, cfgRead.currentDay);
    TEST_ASSERT_EQUAL_INT(0, cfgRead.date);
}

// Test writing config with large values
void test_config_write_large_values(void) {
    struct TConfig cfgWrite = {0};
    struct TConfig cfgRead = {0};

    cfgWrite.currentDay = 999999;
    cfgWrite.date = 99999999;

    cfgFileWrite(&cfgWrite);
    cfgFileRead(&cfgRead);

    TEST_ASSERT_EQUAL_INT(999999, cfgRead.currentDay);
    TEST_ASSERT_EQUAL_INT(99999999, cfgRead.date);
}

// Test writing config with negative values
void test_config_write_negative_values(void) {
    struct TConfig cfgWrite = {0};
    struct TConfig cfgRead = {0};

    cfgWrite.currentDay = -10;
    cfgWrite.date = -12345;

    cfgFileWrite(&cfgWrite);
    cfgFileRead(&cfgRead);

    TEST_ASSERT_EQUAL_INT(-10, cfgRead.currentDay);
    TEST_ASSERT_EQUAL_INT(-12345, cfgRead.date);
}

// Test multiple write/read cycles
void test_config_multiple_cycles(void) {
    struct TConfig cfg = {0};

    // First cycle
    cfg.currentDay = 1;
    cfg.date = 100;
    cfgFileWrite(&cfg);

    cfgFileRead(&cfg);
    TEST_ASSERT_EQUAL_INT(1, cfg.currentDay);
    TEST_ASSERT_EQUAL_INT(100, cfg.date);

    // Second cycle - different values
    cfg.currentDay = 2;
    cfg.date = 200;
    cfgFileWrite(&cfg);

    cfgFileRead(&cfg);
    TEST_ASSERT_EQUAL_INT(2, cfg.currentDay);
    TEST_ASSERT_EQUAL_INT(200, cfg.date);

    // Third cycle
    cfg.currentDay = 3;
    cfg.date = 300;
    cfgFileWrite(&cfg);

    cfgFileRead(&cfg);
    TEST_ASSERT_EQUAL_INT(3, cfg.currentDay);
    TEST_ASSERT_EQUAL_INT(300, cfg.date);
}

// Test config structure initialization
void test_config_struct_initialization(void) {
    struct TConfig cfg = {0};

    TEST_ASSERT_EQUAL_INT(0, cfg.currentDay);
    TEST_ASSERT_EQUAL_INT(0, cfg.date);
}

// Test partial config file (only currentDay)
void test_config_read_partial_currentDay_only(void) {
    // Write a config with only currentDay
    struct TConfig cfgWrite = {55, 0};
    cfgFileWrite(&cfgWrite);

    // Read it back
    struct TConfig cfgRead = {0};
    cfgFileRead(&cfgRead);

    TEST_ASSERT_EQUAL_INT(55, cfgRead.currentDay);
    TEST_ASSERT_EQUAL_INT(0, cfgRead.date);
}

// Test partial config file (only date)
void test_config_read_partial_date_only(void) {
    // Write a config with only date
    struct TConfig cfgWrite = {0, 77777};
    cfgFileWrite(&cfgWrite);

    // Read it back
    struct TConfig cfgRead = {0};
    cfgFileRead(&cfgRead);

    TEST_ASSERT_EQUAL_INT(0, cfgRead.currentDay);
    TEST_ASSERT_EQUAL_INT(77777, cfgRead.date);
}

// Test that config persists across multiple reads
void test_config_persistence(void) {
    // Write config
    struct TConfig cfg1 = {88, 99};
    cfgFileWrite(&cfg1);

    // Read it multiple times
    struct TConfig cfg2 = {0};
    cfgFileRead(&cfg2);
    TEST_ASSERT_EQUAL_INT(88, cfg2.currentDay);
    TEST_ASSERT_EQUAL_INT(99, cfg2.date);

    struct TConfig cfg3 = {0};
    cfgFileRead(&cfg3);
    TEST_ASSERT_EQUAL_INT(88, cfg3.currentDay);
    TEST_ASSERT_EQUAL_INT(99, cfg3.date);
}

// Test config with boundary values
void test_config_boundary_values(void) {
    struct TConfig cfgWrite = {INT_MAX, INT_MIN};
    cfgFileWrite(&cfgWrite);

    struct TConfig cfgRead = {0};
    cfgFileRead(&cfgRead);

    TEST_ASSERT_EQUAL_INT(INT_MAX, cfgRead.currentDay);
    TEST_ASSERT_EQUAL_INT(INT_MIN, cfgRead.date);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_config_write_and_read);
    RUN_TEST(test_config_read_nonexistent);
    RUN_TEST(test_config_write_zeros);
    RUN_TEST(test_config_write_large_values);
    RUN_TEST(test_config_write_negative_values);
    RUN_TEST(test_config_multiple_cycles);
    RUN_TEST(test_config_struct_initialization);
    RUN_TEST(test_config_read_partial_currentDay_only);
    RUN_TEST(test_config_read_partial_date_only);
    RUN_TEST(test_config_persistence);
    RUN_TEST(test_config_boundary_values);

    return UNITY_END();
}
