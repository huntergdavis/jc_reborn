# PS1 Debug Snippets

Reference for visual debugging techniques used during PS1 port development. Since printf() doesn't output to DuckStation TTY console, these visual techniques are essential.

## Colored Rectangle Markers

Use `grDrawRect()` with VSync delays to show execution progress:

```c
/* Show a colored rectangle for 0.5 seconds to mark execution point */
grDrawRect(NULL, x, y, width, height, color);
grRefreshDisplay();
for (int d = 0; d < 30; d++) VSync(0);  /* 30 frames = 0.5 sec at 60fps */
```

### Color Reference (Palette Index)
- 0: Black
- 1: Red
- 2: Green
- 3: Blue
- 4: Yellow
- 5: Magenta
- 6: Cyan
- 7: White

### Common Marker Patterns

**Before/After Function Call:**
```c
/* BEFORE: Cyan square at top-right */
grDrawRect(NULL, 600, 5, 30, 30, 6);
grRefreshDisplay();
for (int d = 0; d < 30; d++) VSync(0);

suspiciousFunction();  /* The function being tested */

/* AFTER: Magenta square below cyan */
grDrawRect(NULL, 600, 40, 30, 30, 5);
grRefreshDisplay();
for (int d = 0; d < 30; d++) VSync(0);
```

**Execution Progress Sequence:**
```c
/* RED = main() reached */
grDrawRect(NULL, 50, 50, 120, 80, 1);
grRefreshDisplay();
for (int d = 0; d < 120; d++) VSync(0);  /* 2 sec */

/* GREEN = CD-ROM initialized */
grDrawRect(NULL, 200, 50, 120, 80, 2);
grRefreshDisplay();
for (int d = 0; d < 120; d++) VSync(0);

/* BLUE = Resources parsed */
grDrawRect(NULL, 350, 50, 120, 80, 3);
grRefreshDisplay();
for (int d = 0; d < 120; d++) VSync(0);
```

## Encoding Values in Rectangle Size

Use rectangle width to encode numeric values:

```c
/* Width encodes count value (each unit = 10px, max visible ~120px) */
int redWidth = (numAdsResources > 12) ? 120 : numAdsResources * 10;
int greenWidth = (adsWithData > 4) ? 120 : adsWithData * 30;

grDrawRect(NULL, 50, 50, redWidth, 80, 1);   /* RED = total count */
grDrawRect(NULL, 260, 50, greenWidth, 80, 2); /* GREEN = valid count */

/* Presence indicator (solid = found, absent = not found) */
if (testAds) grDrawRect(NULL, 470, 50, 120, 80, 3);  /* BLUE if exists */
```

## Debug Test Loop

Continuous loop showing resource counts as rectangles:

```c
/* Debug test loop - encode counts into rectangle sizes/colors */
/* Top row shows COUNTS, bottom row shows PRESENCE */
int frameCount = 0;
while(1) {
    /* Top row: ADS debug info */
    int redWidth = (numAdsResources > 12) ? 120 : numAdsResources * 10;
    int greenWidth = (adsWithData > 4) ? 120 : adsWithData * 30;

    /* RED = numAdsResources count (width shows count) */
    if (redWidth > 0) grDrawRect(NULL, 50, 50, redWidth, 80, 1);

    /* GREEN = adsWithData count (width shows count) */
    if (greenWidth > 0) grDrawRect(NULL, 260, 50, greenWidth, 80, 2);

    /* BLUE = testAds found (full=found, absent=not found) */
    if (testAds) grDrawRect(NULL, 470, 50, 120, 80, 3);

    /* Bottom row: Other resource counts */
    int yellowWidth = (numScrResources > 12) ? 120 : numScrResources * 10;
    int magentaWidth = (numBmpResources > 12) ? 120 : numBmpResources * 10;
    int cyanWidth = (numTtmResources > 12) ? 120 : numTtmResources * 10;

    if (yellowWidth > 0) grDrawRect(NULL, 50, 200, yellowWidth, 80, 4);   /* SCR */
    if (magentaWidth > 0) grDrawRect(NULL, 260, 200, magentaWidth, 80, 5); /* BMP */
    if (cyanWidth > 0) grDrawRect(NULL, 470, 200, cyanWidth, 80, 6);      /* TTM */

    /* Draw border */
    grDrawLine(NULL, 0, 0, 639, 0, 7);
    grDrawLine(NULL, 0, 479, 639, 479, 7);
    grDrawLine(NULL, 0, 0, 0, 479, 7);
    grDrawLine(NULL, 639, 0, 639, 479, 7);

    grRefreshDisplay();
    frameCount++;
}
```

## Isolating Hangs

When code hangs, use binary search with markers:

```c
/* Step 1: Mark BEFORE suspicious section */
grDrawRect(NULL, 600, 5, 30, 30, 6);  /* Cyan = about to enter */
grRefreshDisplay();
for (int d = 0; d < 30; d++) VSync(0);

/* Step 2: Split suspicious code in half, add marker in middle */
firstHalfOfCode();

grDrawRect(NULL, 600, 40, 30, 30, 4);  /* Yellow = made it halfway */
grRefreshDisplay();
for (int d = 0; d < 30; d++) VSync(0);

secondHalfOfCode();

/* Step 3: Mark AFTER */
grDrawRect(NULL, 600, 75, 30, 30, 5);  /* Magenta = completed */
grRefreshDisplay();
for (int d = 0; d < 30; d++) VSync(0);
```

## Full Screen Color Flash

For early boot debugging (before grDrawRect available):

```c
void showDebugScreen(uint8 r, uint8 g, uint8 b)
{
    DISPENV disp;
    DRAWENV draw;

    ResetGraph(0);
    SetDefDispEnv(&disp, 0, 0, 640, 480);
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    disp.isinter = 1;
    setRGB0(&draw, r, g, b);
    draw.isbg = 1;

    PutDispEnv(&disp);
    PutDrawEnv(&draw);
    SetDispMask(1);

    DrawSync(0);
    for (int i = 0; i < 120; i++) VSync(0);  /* 2 seconds */
}
```

## Debug Findings Reference

### LoadImage Behavior (Dec 2025)
- LoadImage to framebuffer (0,0) works: Background SCR rendering
- LoadImage to texture VRAM (640,4+) hangs: BMP sprite uploads
- Workaround: Keep sprites in RAM, skip VRAM upload for now

### CD State Corruption
After direct CD calls (CdSearchFile, CdControl, CdRead), call `cdromResetState()` before using `ps1_fopen()`.

---

## IMPORTANT: Current State Clarification (Dec 21, 2025)

**What IS working:**
- Title screen (TITLE.RAW) displays correctly
- Background SCR images render (moonlit ocean/sky)
- 60 FPS game loop runs

**What is NOT working - NEVER HAS WORKED:**
- NO Christmas tree has ever been rendered
- NO snow has ever been rendered
- NO BMP sprites of any kind are rendering
- NO animated ADS scenes are playing
- The "Christmas scene" has NEVER been visible - only static backgrounds

**The screenshot shows ONLY:**
- Static moonlit background (SCR resource)
- Stars in sky (part of SCR background image, not sprites)
- Moon (part of SCR background image, not a sprite)
- Ocean/water (part of SCR background image)

**Root cause:** BMP sprite LoadImage to VRAM texture area hangs, so all sprites are disabled. ADS scenes run but have no visible output because sprites cannot be drawn.

This has been repeatedly misidentified as "working" when it's just a static background image with no animation.
