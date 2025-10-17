/*
 *  Unit tests for calcpath.c
 *
 *  Tests the pathfinding algorithm for Johnny's walks
 */

#include "unity/unity.h"
#include "../mytypes.h"
#include "../utils.h"
#include "../calcpath.h"
#include "../calcpath_data.h"
#include <stdlib.h>
#include <time.h>

void setUp(void) {
    // Initialize random seed for consistent test results
    srand(42);
    // Disable debug mode to avoid console spam during tests
    debugMode = 0;
}

void tearDown(void) {
    // Called after each test
}

// Test calcPath with same source and destination
void test_calcPath_same_node(void) {
    // Path from A to A (node 0 to node 0)
    int *path = calcPath(0, 0);
    TEST_ASSERT_NOT_NULL(path);

    // Should be just the starting node followed by UNDEF_NODE
    TEST_ASSERT_EQUAL_INT(0, path[0]);
    TEST_ASSERT_EQUAL_INT(UNDEF_NODE, path[1]);
}

// Test calcPath returns path array terminated with UNDEF_NODE
void test_calcPath_terminates_with_undef(void) {
    // Test a valid path in the walk matrix
    // From node 0 (A) to node 4 (E)
    int *path = calcPath(0, 4);
    TEST_ASSERT_NOT_NULL(path);

    // Find the end of the path
    int i = 0;
    while (path[i] != UNDEF_NODE && i < 20) {
        i++;
    }

    // Should have found UNDEF_NODE before running out of reasonable length
    TEST_ASSERT_LESS_THAN(20, i + 1);
    TEST_ASSERT_EQUAL_INT(UNDEF_NODE, path[i]);
}

// Test calcPath finds valid path between connected nodes
void test_calcPath_finds_valid_path(void) {
    // Test path from A (0) to E (4) - these are connected in walkMatrix
    int *path = calcPath(0, 4);
    TEST_ASSERT_NOT_NULL(path);

    // First node should be starting node
    TEST_ASSERT_EQUAL_INT(0, path[0]);

    // Find path length
    int len = 0;
    while (path[len] != UNDEF_NODE && len < 20) {
        // All nodes should be valid indices
        TEST_ASSERT_GREATER_OR_EQUAL(0, path[len]);
        TEST_ASSERT_LESS_THAN(NUM_OF_NODES, path[len]);
        len++;
    }

    // Path should have at least 2 nodes (start and end)
    TEST_ASSERT_GREATER_THAN(1, len);

    // Last node before UNDEF_NODE should be destination
    TEST_ASSERT_EQUAL_INT(4, path[len - 1]);
}

// Test calcPath handles valid node boundaries
void test_calcPath_handles_boundary_nodes(void) {
    // Test with first node (0)
    int *path1 = calcPath(0, 0);
    TEST_ASSERT_NOT_NULL(path1);
    TEST_ASSERT_EQUAL_INT(0, path1[0]);

    // Test with last valid node (NUM_OF_NODES - 1 = 5)
    int *path2 = calcPath(5, 5);
    TEST_ASSERT_NOT_NULL(path2);
    TEST_ASSERT_EQUAL_INT(5, path2[0]);
}

// Test multiple calls return consistent paths (deterministic with fixed seed)
void test_calcPath_is_deterministic_with_fixed_seed(void) {
    srand(42); // Reset seed
    int *path1 = calcPath(0, 4);

    srand(42); // Reset seed again
    int *path2 = calcPath(0, 4);

    TEST_ASSERT_NOT_NULL(path1);
    TEST_ASSERT_NOT_NULL(path2);

    // With the same seed, paths should be identical
    int i = 0;
    while (i < 20) {
        if (path1[i] == UNDEF_NODE && path2[i] == UNDEF_NODE) {
            break;
        }
        TEST_ASSERT_EQUAL_INT(path1[i], path2[i]);
        i++;
    }
}

// Test calcPath doesn't cause infinite loops
void test_calcPath_no_infinite_loops(void) {
    // Try all self-loops (should complete quickly)
    for (int node = 0; node < NUM_OF_NODES; node++) {
        int *path = calcPath(node, node);
        TEST_ASSERT_NOT_NULL(path);
        TEST_ASSERT_EQUAL_INT(node, path[0]);
        TEST_ASSERT_EQUAL_INT(UNDEF_NODE, path[1]);
    }
}

// Test calcPath returns valid path structure
void test_calcPath_returns_valid_structure(void) {
    // Test several known paths
    int testPaths[][2] = {
        {0, 0}, // A to A
        {1, 1}, // B to B
        {2, 2}, // C to C
        {0, 4}, // A to E (should have a path)
    };

    for (int t = 0; t < 4; t++) {
        int from = testPaths[t][0];
        int to = testPaths[t][1];

        int *path = calcPath(from, to);
        TEST_ASSERT_NOT_NULL(path);

        // Verify path starts at source
        TEST_ASSERT_EQUAL_INT(from, path[0]);

        // Verify path ends at destination (find last node before UNDEF_NODE)
        int len = 0;
        while (path[len] != UNDEF_NODE && len < 20) {
            len++;
        }
        TEST_ASSERT_GREATER_THAN(0, len);
        TEST_ASSERT_EQUAL_INT(to, path[len - 1]);
    }
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_calcPath_same_node);
    RUN_TEST(test_calcPath_terminates_with_undef);
    RUN_TEST(test_calcPath_finds_valid_path);
    RUN_TEST(test_calcPath_handles_boundary_nodes);
    RUN_TEST(test_calcPath_is_deterministic_with_fixed_seed);
    RUN_TEST(test_calcPath_no_infinite_loops);
    RUN_TEST(test_calcPath_returns_valid_structure);

    return UNITY_END();
}
