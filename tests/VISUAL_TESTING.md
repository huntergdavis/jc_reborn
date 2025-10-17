# Visual Regression Testing

This framework ensures that memory optimizations and code changes don't introduce visual rendering bugs.

## How It Works

1. **Frame Capture**: Captures specific frames during animation playback
2. **Reference Frames**: Stores known-good frames for comparison
3. **Pixel Comparison**: Compares new frames against reference frames
4. **Regression Detection**: Reports visual differences

## Quick Start

### Capture Reference Frames

```bash
# Capture reference frames from current (known-good) build
cd tests
./capture_reference_frames.sh
```

This creates reference frames in `tests/visual_reference/`:
- `gjnat1_frame10.bmp` - Frame 10 of GJNAT1.TTM
- `gjnat1_frame20.bmp` - Frame 20 of GJNAT1.TTM
- `gjvis3_frame15.bmp` - Frame 15 of GJVIS3.TTM

### Run Visual Regression Tests

```bash
cd tests
make test-visual-regression
```

## Manual Frame Capture

Capture any frame from any TTM or ADS:

```bash
cd jc_resources

# Capture frame 50 from a TTM
../jc_reborn window nosound capture-frame 50 capture-output frame50.bmp ttm GJNAT1.TTM

# Capture frame 25 from an ADS
../jc_reborn window nosound capture-frame 25 capture-output frame25.bmp ads ACTIVITY.ADS 0
```

## Adding New Test Cases

1. **Capture reference frame** from current build:
   ```bash
   cd jc_resources
   ../jc_reborn window nosound \
     capture-frame 30 \
     capture-output ../tests/visual_reference/mytest_frame30.bmp \
     ttm MYTEST.TTM
   ```

2. **Add to git**:
   ```bash
   git add tests/visual_reference/mytest_frame30.bmp
   ```

3. **Add test in test_visual_regression.c** (optional):
   ```c
   void test_visual_mytest_frame30(void) {
       // Test code to capture and compare
   }
   ```

## Workflow for Memory Optimizations

1. **Before changes**: Capture reference frames
   ```bash
   cd tests && ./capture_reference_frames.sh
   ```

2. **Make changes**: Implement memory optimization

3. **After changes**: Capture new frames
   ```bash
   cd jc_resources
   ../jc_reborn window nosound capture-frame 10 capture-output test_frame.bmp ttm GJNAT1.TTM
   ```

4. **Compare**: Use image diff tool or pixel comparison
   ```bash
   # Visual comparison (macOS)
   open -a Preview tests/visual_reference/gjnat1_frame10.bmp test_frame.bmp
   ```

## Comparison Strategy

- **Exact Match**: 100% identical pixels (strict)
- **Tolerance**: < 0.1% pixels different (lenient, for minor rendering variations)
- **Max Color Diff**: Reports largest RGB channel difference

## Benefits

- ✅ Catches rendering bugs early
- ✅ Confidence in memory optimizations
- ✅ Automated regression detection
- ✅ Visual proof of correctness
- ✅ Fast feedback loop

## Environment Variables

Set custom memory budget for testing:
```bash
JC_MEM_BUDGET_MB=2 ../jc_reborn window capture-frame 10 ...
```

## Tips

- Capture frames at interesting points (palette changes, sprite rendering, etc.)
- Use consistent frame numbers for reproducibility
- Test with both TTMs (simple) and ADS (complex multi-layer scenes)
- Store reference frames in git for team-wide regression detection
