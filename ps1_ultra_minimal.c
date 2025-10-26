/*
 * Ultra minimal PS1 test - Rectangles, triangles, and sound
 * Uses proper primitive buffer allocation (like PSn00bSDK examples)
 */

#include <sys/types.h>
#include <stdio.h>
#include <psxgpu.h>
#include <psxapi.h>
#include <psxspu.h>

#define SCREEN_XRES 640
#define SCREEN_YRES 480
#define OTLEN 8
#define BUFFER_SIZE 8192

int main(void)
{
    DISPENV disp;
    DRAWENV draw;
    u_long ot[OTLEN];  /* Ordering table */
    char primbuff[BUFFER_SIZE];  /* Primitive buffer */
    char *nextpri;  /* Next primitive pointer */

    /* Initialize GPU */
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);

    /* Setup display */
    SetDefDispEnv(&disp, 0, 0, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&draw, 0, 0, SCREEN_XRES, SCREEN_YRES);

    /* Clear background to dark green (v12 - FINAL DEMO) */
    setRGB0(&draw, 0, 64, 32);
    draw.isbg = 1;  /* Enable background clear */

    PutDispEnv(&disp);
    PutDrawEnv(&draw);

    /* Initialize ordering table */
    ClearOTagR(ot, OTLEN);

    SetDispMask(1);

    printf("=== PS1 TRIANGLE TEST ===\n");
    printf("GPU initialized - testing POLY_F3 rendering\n");
    printf("Entering render loop...\n");

    /* Render loop */
    while(1) {
        VSync(0);

        /* Clear ordering table and reset primitive buffer */
        ClearOTagR(ot, OTLEN);
        nextpri = primbuff;

        /* LEFT: YELLOW SQUARE (2 triangles) */
        /* Square top-left triangle */
        POLY_F3 *sq1 = (POLY_F3*)nextpri;
        setPolyF3(sq1);
        setXY3(sq1, 80, 180, 240, 180, 80, 320);  /* TL, TR, BL */
        setRGB0(sq1, 255, 255, 0);  /* Yellow */
        addPrim(ot, sq1);
        nextpri += sizeof(POLY_F3);

        /* Square bottom-right triangle */
        POLY_F3 *sq2 = (POLY_F3*)nextpri;
        setPolyF3(sq2);
        setXY3(sq2, 240, 180, 240, 320, 80, 320);  /* TR, BR, BL */
        setRGB0(sq2, 255, 255, 0);  /* Yellow */
        addPrim(ot, sq2);
        nextpri += sizeof(POLY_F3);

        /* RIGHT: RED TRIANGLE */
        POLY_F3 *tri1 = (POLY_F3*)nextpri;
        setPolyF3(tri1);
        setXY3(tri1, 480, 180, 380, 320, 580, 320);
        setRGB0(tri1, 255, 0, 0);  /* Red */
        addPrim(ot, tri1);
        nextpri += sizeof(POLY_F3);

        /* CRITICAL: Call PutDrawEnv BEFORE DrawOTag */
        /* This clears the background (isbg=1) THEN we draw on top */
        PutDrawEnv(&draw);

        /* Draw all primitives via ordering table */
        DrawOTag(ot+OTLEN-1);  /* Draw from END of OT */
    }

    return 0;
}
