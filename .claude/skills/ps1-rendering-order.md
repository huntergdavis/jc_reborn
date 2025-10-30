# PS1 Rendering Order - PSn00bSDK Double Buffering

## Critical Insight

When using PSn00bSDK with double buffering, the order of GPU operations is crucial. The primitives must be submitted to the GPU BEFORE the buffer flip and clear operations.

## Correct Rendering Sequence

```c
/* Main render loop */
while (1) {
    /* 1. Build primitives for current frame using CURRENT buffer */
    nextpri = pribuff[db_active];

    /* Create your primitives (TILE, POLY_F3, SPRT, etc.) */
    POLY_F3 *triangle = draw_triangle(...);
    TILE *square = draw_tile(...);

    /* Add primitives to ordering table */
    addPrim(&ot[db_active][OTLEN-1], triangle);
    addPrim(&ot[db_active][OTLEN-1], square);

    /* 2. Submit primitives to GPU - DO THIS FIRST! */
    DrawOTag(&ot[db_active][OTLEN-1]);

    /* 3. Wait for GPU to finish and vertical blank */
    DrawSync(0);
    VSync(0);

    /* 4. Flip buffer index */
    db_active ^= 1;

    /* 5. Apply display/draw environments to OTHER buffer */
    PutDispEnv(&db[db_active].disp);
    PutDrawEnv(&db[db_active].draw);  /* This clears the buffer */

    /* 6. Clear ordering table for next frame */
    ClearOTagR(ot[db_active], OTLEN);
}
```

## Why This Order Matters

1. **DrawOTag() queues GPU commands** - It doesn't execute immediately, just queues the drawing commands
2. **DrawSync/VSync wait for completion** - GPU finishes drawing the primitives while you wait
3. **Buffer flip happens** - Switch which buffer is displayed vs drawn to
4. **PutDrawEnv clears the OTHER buffer** - The buffer you're ABOUT to draw to gets cleared, not the one being displayed
5. **Primitives survive** - Since they were submitted before the flip, they're in the buffer being displayed

## Wrong Order (Don't Do This!)

```c
/* WRONG - will show only background */
while (1) {
    /* Flip and clear first */
    DrawSync(0);
    VSync(0);
    db_active ^= 1;
    PutDispEnv(&db[db_active].disp);
    PutDrawEnv(&db[db_active].draw);  /* Clears screen! */

    /* Build primitives */
    ClearOTagR(ot[db_active], OTLEN);
    // ... build primitives ...

    /* Draw AFTER clear - primitives get wiped out! */
    DrawOTag(&ot[db_active][OTLEN-1]);
}
```

## Display Environment Setup

```c
/* For 640x480 interlaced mode: */
SetDefDispEnv(&disp[0], 0, 0, SCREEN_XRES, SCREEN_YRES);
SetDefDispEnv(&disp[1], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);

/* Draw environments are SWAPPED - you draw to the buffer NOT being displayed */
SetDefDrawEnv(&draw[0], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
SetDefDrawEnv(&draw[1], 0, 0, SCREEN_XRES, SCREEN_YRES);

/* Enable background clear */
setRGB0(&draw[0], r, g, b);
setRGB0(&draw[1], r, g, b);
draw[0].isbg = 1;
draw[1].isbg = 1;
```

## Reference Implementation

See `graphics_ps1.c:348-349` and `ps1_minimal_main.c` for working examples.

## Common Pitfalls

1. **Calling PutDrawEnv before DrawOTag** - Screen gets cleared, wiping out your primitives
2. **Forgetting to flip buffer index** - Drawing to wrong buffer
3. **Not waiting for VSync** - Tearing and flickering
4. **Clearing ordering table too early** - Primitives get lost

## Testing Strategy

When debugging rendering issues:
1. **Change background color each build** - Confirms new code is running
2. **Start with simple TILE primitives** - Easier to debug than POLY_F3
3. **Check primitive buffer overflow** - Ensure nextpri doesn't exceed buffer size
4. **Verify ordering table depth** - Use correct OT index (0 = background, OTLEN-1 = foreground)

## Build Gotchas

- **Multiple source copies** - CMake may copy source to build directory, edit THAT version
- **DuckStation caching** - Force-kill emulator between tests: `pkill -9 -f duckstation`
- **CD image not updating** - Verify .bin/.cue timestamps after mkpsxiso

## Historical Note

This was debugged by comparing commit 2503177 (working) with broken code. The key difference was the order of DrawOTag vs PutDispEnv/PutDrawEnv.
