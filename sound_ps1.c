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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declare FILE to avoid utils.h compilation errors with -ffreestanding */
typedef struct _FILE FILE;

#include "mytypes.h"     /* Defines uint8, uint16, uint32, etc. */
#include "sound_ps1.h"
#include "config.h"
#include "utils.h"
#include "cdrom_ps1.h"

/* Global variables */
int soundDisabled = 0;

/* SPU configuration */
#define MAX_SOUND_EFFECTS 25
#define SPU_DATA_START 0x1010  /* After SPU capture buffers + dummy block */
#define VAG_HEADER_SIZE 48    /* Standard Sony VAG header size */
#define NUM_CHANNELS 8        /* Use 8 channels for round-robin */

/* Sound effect data loaded into SPU RAM */
static uint32_t soundAddresses[MAX_SOUND_EFFECTS];
static uint32_t soundSizes[MAX_SOUND_EFFECTS];
static uint16_t soundSampleRates[MAX_SOUND_EFFECTS];
static int soundsLoaded = 0;
static int nextChannel = 0;

/* Read big-endian uint32 from VAG header */
static uint32_t readBE32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/*
 * Initialize SPU audio system and load VAG files from CD into SPU RAM
 */
void soundInit()
{
    if (soundDisabled) {
        printf("Sound disabled\n");
        return;
    }

    /* Initialize SPU */
    SpuInit();

    /* Set master volume */
    SpuSetCommonMasterVolume(0x3FFF, 0x3FFF);

    /* Clear sound tables */
    for (int i = 0; i < MAX_SOUND_EFFECTS; i++) {
        soundAddresses[i] = 0;
        soundSizes[i] = 0;
        soundSampleRates[i] = 0;
    }

    /* Load VAG files from CD into SPU RAM */
    uint32_t spuAddr = SPU_DATA_START;
    int loaded = 0;

    for (int i = 0; i < MAX_SOUND_EFFECTS; i++) {
        char filename[32];
        sprintf(filename, "\\SND\\SOUND%02d.VAG;1", i);

        uint32_t vagSize = 0;
        uint8_t *vagData = ps1_loadRawFile(filename, &vagSize);
        if (!vagData) continue;

        if (vagSize <= VAG_HEADER_SIZE) {
            free(vagData);
            continue;
        }

        /* Parse VAG header — sample rate is big-endian at offset 16 */
        uint16_t sampleRate = (uint16_t)readBE32(vagData + 16);
        uint32_t adpcmSize = vagSize - VAG_HEADER_SIZE;

        /* Upload ADPCM data (skip VAG header) to SPU RAM */
        SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
        SpuSetTransferStartAddr(spuAddr);
        SpuWrite((uint32_t *)(vagData + VAG_HEADER_SIZE), adpcmSize);
        SpuIsTransferCompleted(SPU_TRANSFER_WAIT);

        soundAddresses[i] = spuAddr;
        soundSizes[i] = adpcmSize;
        soundSampleRates[i] = sampleRate;

        /* Advance SPU address, 16-byte aligned */
        spuAddr += (adpcmSize + 15) & ~15;

        free(vagData);
        loaded++;
    }

    if (loaded > 0) {
        soundsLoaded = 1;
        printf("SPU: loaded %d sounds\n", loaded);
    } else {
        printf("SPU: no VAG files found\n");
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

    /* Stop all SPU channels (0xFFFFFF = all 24 channels) */
    SpuSetKey(0, 0xFFFFFF);
}

/*
 * Play sound effect by number using round-robin channel allocation
 */
void soundPlay(int nb)
{
    if (soundDisabled || !soundsLoaded) {
        return;
    }

    if (nb < 0 || nb >= MAX_SOUND_EFFECTS) {
        return;
    }

    if (soundAddresses[nb] == 0) {
        return;
    }

    int ch = nextChannel;
    nextChannel = (nextChannel + 1) % NUM_CHANNELS;

    /* Convert sample rate to SPU pitch (44100 Hz = 0x1000) */
    uint16_t pitch = getSPUSampleRate(soundSampleRates[nb]);

    /* Set voice parameters using PSn00bSDK macros */
    SpuSetVoiceVolume(ch, 0x3FFF, 0x3FFF);
    SpuSetVoicePitch(ch, pitch);
    SpuSetVoiceStartAddr(ch, soundAddresses[nb]);
    SpuSetVoiceADSR(ch, 0x7F, 0x0, 0x7F, 0x0, 0xF);

    /* Start playback */
    SpuSetKey(1, 1 << ch);
}

/*
 * Load sound effect into SPU RAM (unused — loading happens in soundInit)
 */
int soundLoad(int nb, void *data, uint32_t size)
{
    (void)nb;
    (void)data;
    (void)size;
    return 0;
}
