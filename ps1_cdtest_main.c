/*
 * Minimal PS1 CD-ROM test
 * Just tries to read RESOURCE.MAP from CD and show first byte
 */

#include <psxgpu.h>
#include <psxapi.h>
#include <stdio.h>

#include "cdrom_ps1.h"
#include "mytypes.h"

/* Visual feedback */
static void setScreenColor(int r, int g, int b) {
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 640, 480);
    setRGB0(&draw, r, g, b);
    draw.isbg = 1;
    PutDrawEnv(&draw);
    SetDispMask(1);
}

int main(void) {
    /* RED = Starting */
    setScreenColor(255, 0, 0);

    /* Initialize CD-ROM */
    if (cdromInit() != 0) {
        setScreenColor(128, 0, 0);  /* DARK RED = CD init failed */
        while(1);
    }

    /* GREEN = CD initialized */
    setScreenColor(0, 255, 0);

    /* Try to open RESOURCE.MAP */
    int fd = cdromOpen("RESOURCE.MAP");
    if (fd < 0) {
        setScreenColor(255, 0, 255);  /* MAGENTA = File not found */
        while(1);
    }

    /* CYAN = File opened */
    setScreenColor(0, 255, 255);

    /* Try to read one byte */
    uint8 byte;
    int result = cdromRead(fd, &byte, 1);

    if (result != 1) {
        setScreenColor(255, 255, 0);  /* YELLOW = Read failed */
        while(1);
    }

    /* BLUE = Read succeeded! */
    setScreenColor(0, 0, 255);

    cdromClose(fd);

    /* Success - stay blue */
    while(1);

    return 0;
}
