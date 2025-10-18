/*
 *  This file is part of 'Johnny Reborn' - PS1 Port
 *
 *  PlayStation 1 audio implementation using PSn00bSDK SPU
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

#include <psxspu.h>
#include <psxapi.h>

#include "sound_ps1.h"
#include "config.h"

/* Global variables */
int soundDisabled = 0;

/* SPU configuration */
#define MAX_SOUND_EFFECTS 25
#define SPU_RAM_BASE 0x1000  /* Start of SPU RAM for sound effects */

/* Sound effect data (to be loaded from CD) */
static uint32 soundAddresses[MAX_SOUND_EFFECTS];
static uint32 soundSizes[MAX_SOUND_EFFECTS];
static int soundsLoaded = 0;

/*
 * Initialize SPU audio system
 */
void soundInit()
{
    if (soundDisabled) {
        if (debugMode) {
            printf("Sound disabled\n");
        }
        return;
    }

    /* Initialize SPU */
    SpuInit();

    /* Set SPU common attributes */
    SpuCommonAttr attr;
    attr.mask = (SPU_COMMON_MVOLL | SPU_COMMON_MVOLR);
    attr.mvol.left = 0x3FFF;   /* Max volume */
    attr.mvol.right = 0x3FFF;
    SpuSetCommonAttr(&attr);

    /* Enable SPU */
    SpuSetKey(SPU_ON, SPU_ALLCH);

    /* TODO: Load sound effects from CD into SPU RAM */
    /* For now, mark all sounds as not loaded */
    for (int i = 0; i < MAX_SOUND_EFFECTS; i++) {
        soundAddresses[i] = 0;
        soundSizes[i] = 0;
    }

    soundsLoaded = 0;

    if (debugMode) {
        printf("SPU initialized\n");
    }
}

/*
 * Shutdown audio system
 */
void soundEnd()
{
    if (soundDisabled) {
        return;
    }

    /* Stop all SPU channels */
    SpuSetKey(SPU_OFF, SPU_ALLCH);

    /* Quit SPU */
    SpuQuit();
}

/*
 * Play sound effect by number
 */
void soundPlay(int nb)
{
    if (soundDisabled || !soundsLoaded) {
        return;
    }

    if (nb < 0 || nb >= MAX_SOUND_EFFECTS) {
        if (debugMode) {
            printf("Invalid sound number: %d\n", nb);
        }
        return;
    }

    /* Check if sound is loaded */
    if (soundAddresses[nb] == 0) {
        if (debugMode) {
            printf("Sound %d not loaded\n", nb);
        }
        return;
    }

    /* TODO: Implement SPU playback
     * 1. Find free SPU channel
     * 2. Set voice attributes (pitch, volume, ADSR)
     * 3. Set voice address to soundAddresses[nb]
     * 4. Start playback with SpuSetKey()
     */

    if (debugMode) {
        printf("Playing sound %d (not yet implemented)\n", nb);
    }
}

/*
 * Load sound effect into SPU RAM
 * This should be called during resource loading
 */
int soundLoad(int nb, void *data, uint32 size)
{
    if (soundDisabled) {
        return 0;
    }

    if (nb < 0 || nb >= MAX_SOUND_EFFECTS) {
        return -1;
    }

    /* TODO: Upload sound data to SPU RAM
     * 1. Convert WAV to PS1 ADPCM format if needed
     * 2. Transfer to SPU RAM using SpuSetTransferMode()
     * 3. Store address in soundAddresses[nb]
     */

    soundAddresses[nb] = SPU_RAM_BASE + (nb * 0x4000);  /* Placeholder */
    soundSizes[nb] = size;

    return 0;
}
