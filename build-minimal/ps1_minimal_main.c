/*
 * Minimal PS1 Test - Draw colored triangles and rectangles to verify GPU works
 * This bypasses all game logic to isolate GPU initialization issues
 */

#include <sys/types.h>
#include <stdio.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <psxapi.h>
#include <psxcd.h>

#define SCREEN_XRES 640
#define SCREEN_YRES 480
#define OTLEN 8  /* Ordering table length */

/* Double buffer structure */
typedef struct {
    DISPENV disp;
    DRAWENV draw;
} DB;

/* Globals */
DB db[2];           /* Double buffer */
int db_active = 0;  /* Active buffer index */
u_long ot[2][OTLEN]; /* Ordering tables */
char pribuff[2][32768]; /* Primitive buffers */
char *nextpri;      /* Next primitive pointer */

/* Draw a flat colored rectangle */
TILE *draw_tile(int x, int y, int w, int h, int r, int g, int b)
{
    TILE *tile = (TILE*)nextpri;

    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, w, h);
    setRGB0(tile, r, g, b);

    nextpri += sizeof(TILE);
    return tile;
}

/* Draw a flat colored triangle */
POLY_F3 *draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, int r, int g, int b)
{
    POLY_F3 *tri = (POLY_F3*)nextpri;

    setPolyF3(tri);
    setXY3(tri, x0, y0, x1, y1, x2, y2);
    setRGB0(tri, r, g, b);

    nextpri += sizeof(POLY_F3);
    return tri;
}

int main(void)
{
    int frame_count = 0;

    /* Initialize heap for malloc/printf */
    InitHeap((void*)0x801fff00, 0x00100000);

    /* DON'T call CdInit() - it breaks when booting from CD! */
    /* The BIOS already initialized CD-ROM for us */

    printf("=== PS1 MINIMAL TEST START ===\n");
    printf("Heap initialized at 0x801fff00\n");
    printf("Initializing graphics...\n");

    /* Reset GPU */
    ResetGraph(0);

    /* Set video mode */
    SetVideoMode(MODE_NTSC);

    printf("Setting up display environments...\n");

    /* Setup display buffer 0 (shown while drawing buffer 1) */
    SetDefDispEnv(&db[0].disp, 0, 0, SCREEN_XRES, SCREEN_YRES);
    /* Setup display buffer 1 (shown while drawing buffer 0) */
    SetDefDispEnv(&db[1].disp, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);

    /* Setup draw buffer 0 (draw here while showing buffer 1) */
    SetDefDrawEnv(&db[0].draw, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    /* Setup draw buffer 1 (draw here while showing buffer 0) */
    SetDefDrawEnv(&db[1].draw, 0, 0, SCREEN_XRES, SCREEN_YRES);

    /* Set background clear colors - DARK BLUE to verify new build */
    setRGB0(&db[0].draw, 0, 0, 128);  /* Dark blue background */
    setRGB0(&db[1].draw, 0, 0, 128);
    db[0].draw.isbg = 1;  /* Enable background clear */
    db[0].draw.dtd = 1;   /* Enable dithering */
    db[1].draw.isbg = 1;
    db[1].draw.dtd = 1;

    printf("Applying initial environments...\n");

    /* Apply environments */
    PutDispEnv(&db[0].disp);
    PutDrawEnv(&db[0].draw);

    /* Initialize ordering tables */
    ClearOTagR(ot[0], OTLEN);
    ClearOTagR(ot[1], OTLEN);

    /* Enable display */
    SetDispMask(1);

    printf("Display enabled - starting main loop\n");
    printf("Should see: dark blue background with triangle and square\n");

    /* Main render loop */
    while (1) {
        /* Build primitives for this frame */
        nextpri = pribuff[db_active];

        /* Draw a triangle on the left */
        POLY_F3 *triangle = draw_triangle(
            150, 100,   /* top point */
            50, 250,    /* bottom left */
            250, 250,   /* bottom right */
            255, 255, 0 /* yellow */
        );

        /* Draw a square on the right */
        TILE *square = draw_tile(350, 150, 150, 150, 0, 255, 255); /* cyan */

        /* Add to ordering table */
        addPrim(&ot[db_active][OTLEN-1], triangle);
        addPrim(&ot[db_active][OTLEN-1], square);

        /* Submit primitives to GPU */
        DrawOTag(&ot[db_active][OTLEN-1]);

        /* Wait for GPU and VBlank */
        DrawSync(0);
        VSync(0);

        /* Flip buffers */
        db_active ^= 1;

        /* Apply display/draw environments (clears OTHER buffer) */
        PutDispEnv(&db[db_active].disp);
        PutDrawEnv(&db[db_active].draw);

        /* Clear ordering table for next frame */
        ClearOTagR(ot[db_active], OTLEN);

        frame_count++;

        /* Print progress every 60 frames */
        if ((frame_count % 60) == 0) {
            printf("Frame %d rendered\n", frame_count);
        }
    }

    return 0;
}
