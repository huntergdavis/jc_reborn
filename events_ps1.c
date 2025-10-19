/*
 *  This file is part of 'Johnny Reborn' - PS1 Port
 *
 *  PlayStation 1 input/event handling using PSn00bSDK
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <psxpad.h>
#include <psxapi.h>
#include <psxgpu.h>

#include "mytypes.h"
#include "events_ps1.h"
#include "config.h"

/* Global variables */
int evHotKeysEnabled = 0;

/* Controller data buffers */
static uint8 pad_buff[2][34];

/* Game state variables (matching original events.c) */
extern int quit;
extern int pause;
extern int maxSpeed;
extern int frameAdvance;

/*
 * Initialize input system
 */
void eventsInit()
{
    /* Initialize controller/pad system */
    InitPAD(pad_buff[0], 34, pad_buff[1], 34);

    /* Start reading pads */
    StartPAD();

    /* Don't make pad library acknowledge V-Blank IRQ */
    ChangeClearPAD(0);
}

/*
 * Wait for specified number of ticks (frame timing)
 */
void eventsWaitTick(uint16 delay)
{
    /* Read controller state */
    PADTYPE *pad = (PADTYPE*)pad_buff[0];

    /* Check if controller is connected and stable */
    if (pad->stat == 0) {
        /* Controller buttons (active low, so invert) */
        uint16 buttons = ~(pad->btn);

        /* Only process hotkeys if enabled */
        if (evHotKeysEnabled) {
            /* START button - Pause/Unpause */
            if (buttons & PAD_START) {
                pause = !pause;
                while ((~((PADTYPE*)pad_buff[0])->btn) & PAD_START) {
                    VSync(0);  /* Wait for button release */
                }
            }

            /* SELECT button - Quit */
            if (buttons & PAD_SELECT) {
                quit = 1;
            }

            /* TRIANGLE button - Frame advance (when paused) */
            if (buttons & PAD_TRIANGLE) {
                if (pause) {
                    frameAdvance = 1;
                    while ((~((PADTYPE*)pad_buff[0])->btn) & PAD_TRIANGLE) {
                        VSync(0);  /* Wait for button release */
                    }
                }
            }

            /* CIRCLE button - Toggle max speed */
            if (buttons & PAD_CIRCLE) {
                maxSpeed = !maxSpeed;
                while ((~((PADTYPE*)pad_buff[0])->btn) & PAD_CIRCLE) {
                    VSync(0);  /* Wait for button release */
                }
            }

            /* X button - Reserved */
            /* SQUARE button - Reserved */
            /* L1/L2/R1/R2 - Reserved */
        }
    }

    /* Frame timing */
    if (delay > 0 && !maxSpeed) {
        /* Wait for specified number of V-Blanks */
        for (int i = 0; i < delay; i++) {
            VSync(0);
        }
    }

    /* Handle pause state */
    while (pause && !frameAdvance && !quit) {
        VSync(0);

        /* Check for unpause */
        pad = (PADTYPE*)pad_buff[0];
        if (pad->stat == 0) {
            uint16 buttons = ~(pad->btn);
            if (buttons & PAD_START) {
                pause = 0;
                while ((~((PADTYPE*)pad_buff[0])->btn) & PAD_START) {
                    VSync(0);
                }
            }
            if (buttons & PAD_SELECT) {
                quit = 1;
                break;
            }
        }
    }

    frameAdvance = 0;
}
