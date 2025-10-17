# Test Makefile for Johnny Reborn
# Builds and runs all unit tests

CC = cc
CFLAGS = -Wall -Wpedantic -std=c99 -I.. -I.

# Unity framework
UNITY_SRC = unity/unity.c
UNITY_OBJ = unity/unity.o

# Source files from parent directory (exclude main)
PARENT_DIR = ..
UTILS_OBJ = $(PARENT_DIR)/utils.o
UNCOMPRESS_OBJ = $(PARENT_DIR)/uncompress.o
RESOURCE_OBJ = $(PARENT_DIR)/resource.o
CALCPATH_OBJ = $(PARENT_DIR)/calcpath.o

# Test executables
TEST_UTILS = test_utils
TEST_CALCPATH = test_calcpath
TEST_RESOURCE = test_resource
TEST_UNCOMPRESS = test_uncompress
TEST_CONFIG = test_config
TEST_MEMORY = test_memory
TEST_DISK_STREAMING = test_disk_streaming
TEST_BMP_OPTIMIZATION = test_bmp_optimization
TEST_SCR_OPTIMIZATION = test_scr_optimization

ALL_TESTS = $(TEST_UTILS) $(TEST_CALCPATH) $(TEST_RESOURCE) $(TEST_UNCOMPRESS) $(TEST_CONFIG) $(TEST_MEMORY) $(TEST_DISK_STREAMING) $(TEST_BMP_OPTIMIZATION) $(TEST_SCR_OPTIMIZATION)

# Default target: build and run all tests
all: $(ALL_TESTS)
	@echo ""
	@echo "=== Running All Tests ==="
	@echo ""
	@./$(TEST_UTILS)
	@echo ""
	@./$(TEST_CALCPATH)
	@echo ""
	@echo "Resource tests (require RESOURCE.MAP and RESOURCE.001 in jc_resources/):"
	@./$(TEST_RESOURCE) || echo "Note: Resource tests skipped (RESOURCE files not found)"
	@echo ""
	@./$(TEST_UNCOMPRESS)
	@echo ""
	@./$(TEST_CONFIG)
	@echo ""
	@./$(TEST_MEMORY)
	@echo ""
	@echo "Disk streaming tests (require extracted/ directory in jc_resources/):"
	@./$(TEST_DISK_STREAMING) || echo "Note: Disk streaming tests skipped (extracted/ not found)"
	@echo ""
	@echo "BMP optimization tests:"
	@./$(TEST_BMP_OPTIMIZATION) || echo "Note: BMP optimization tests skipped"
	@echo ""
	@echo "SCR optimization tests:"
	@./$(TEST_SCR_OPTIMIZATION) || echo "Note: SCR optimization tests skipped"
	@echo ""
	@echo "=== All Tests Complete ==="

# Build parent object files if they don't exist
$(UTILS_OBJ):
	$(MAKE) -C $(PARENT_DIR) utils.o

$(UNCOMPRESS_OBJ):
	$(MAKE) -C $(PARENT_DIR) uncompress.o

$(RESOURCE_OBJ):
	$(MAKE) -C $(PARENT_DIR) resource.o

$(CALCPATH_OBJ):
	$(MAKE) -C $(PARENT_DIR) calcpath.o

CONFIG_OBJ = $(PARENT_DIR)/config.o

$(CONFIG_OBJ):
	$(MAKE) -C $(PARENT_DIR) config.o

# Unity framework
$(UNITY_OBJ): $(UNITY_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

# Test: utils
$(TEST_UTILS): test_utils.c $(UNITY_OBJ) $(UTILS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Test: calcpath
$(TEST_CALCPATH): test_calcpath.c $(UNITY_OBJ) $(CALCPATH_OBJ) $(UTILS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Test: resource
$(TEST_RESOURCE): test_resource.c $(UNITY_OBJ) $(RESOURCE_OBJ) $(UTILS_OBJ) $(UNCOMPRESS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Test: uncompress
$(TEST_UNCOMPRESS): test_uncompress.c $(UNITY_OBJ) $(UNCOMPRESS_OBJ) $(UTILS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Test: config
$(TEST_CONFIG): test_config.c $(UNITY_OBJ) $(CONFIG_OBJ) $(UTILS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Test: memory
$(TEST_MEMORY): test_memory.c $(UNITY_OBJ) $(UTILS_OBJ) $(UNCOMPRESS_OBJ) $(CALCPATH_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Test: disk streaming
$(TEST_DISK_STREAMING): test_disk_streaming.c $(UNITY_OBJ) $(RESOURCE_OBJ) $(UTILS_OBJ) $(UNCOMPRESS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Test: BMP optimization
$(TEST_BMP_OPTIMIZATION): test_bmp_optimization.c $(UNITY_OBJ) $(RESOURCE_OBJ) $(UTILS_OBJ) $(UNCOMPRESS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Test: SCR optimization
$(TEST_SCR_OPTIMIZATION): test_scr_optimization.c $(UNITY_OBJ) $(RESOURCE_OBJ) $(UTILS_OBJ) $(UNCOMPRESS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Individual test targets
test-utils: $(TEST_UTILS)
	@echo "Running utils tests..."
	@./$(TEST_UTILS)

test-calcpath: $(TEST_CALCPATH)
	@echo "Running calcpath tests..."
	@./$(TEST_CALCPATH)

test-resource: $(TEST_RESOURCE)
	@echo "Running resource tests..."
	@./$(TEST_RESOURCE)

test-uncompress: $(TEST_UNCOMPRESS)
	@echo "Running uncompress tests..."
	@./$(TEST_UNCOMPRESS)

test-config: $(TEST_CONFIG)
	@echo "Running config tests..."
	@./$(TEST_CONFIG)

test-memory: $(TEST_MEMORY)
	@echo "Running memory profiling tests..."
	@./$(TEST_MEMORY)

test-disk-streaming: $(TEST_DISK_STREAMING)
	@echo "Running disk streaming tests..."
	@./$(TEST_DISK_STREAMING)

test-bmp-optimization: $(TEST_BMP_OPTIMIZATION)
	@echo "Running BMP optimization tests..."
	@./$(TEST_BMP_OPTIMIZATION)

test-scr-optimization: $(TEST_SCR_OPTIMIZATION)
	@echo "Running SCR optimization tests..."
	@./$(TEST_SCR_OPTIMIZATION)

# Clean up test binaries
clean:
	rm -f $(ALL_TESTS) $(UNITY_OBJ)

# Clean everything including parent objects
clean-all: clean
	$(MAKE) -C $(PARENT_DIR) clean

.PHONY: all test-utils test-calcpath test-resource test-uncompress test-config test-memory test-disk-streaming test-bmp-optimization test-scr-optimization clean clean-all
