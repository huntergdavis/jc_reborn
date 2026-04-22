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
#include <hwregs_c.h>
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
int soundMuted = 0;

/* SPU configuration */
#define MAX_SOUND_EFFECTS 25
#define SPU_DATA_START 0x1010  /* After SPU capture buffers + dummy block */
#define VAG_HEADER_SIZE 48    /* Standard Sony VAG header size */
#define NUM_CHANNELS 8        /* Use 8 channels for round-robin */

/* Sound effect data loaded into SPU RAM */
static uint32_t soundAddresses[MAX_SOUND_EFFECTS];
static uint32_t soundSizes[MAX_SOUND_EFFECTS];
static uint16_t soundSampleRates[MAX_SOUND_EFFECTS];
static uint16_t soundPitches[MAX_SOUND_EFFECTS];  /* Pre-computed pitch values */
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
        /* SPU DMA moves data in 64-byte blocks; pad up or the final ADPCM
         * flag byte (end-of-sample) gets truncated and the voice never
         * stops, producing silence or noise instead of our sample. */
        uint32_t dmaSize = (adpcmSize + 63u) & ~63u;

        /* Check SPU RAM overflow (512KB total) */
        if (spuAddr + dmaSize > 512 * 1024) {
            printf("SPU: out of RAM at sound %d\n", i);
            free(vagData);
            break;
        }

        uint8_t *dmaBuf = (uint8_t *)malloc(dmaSize);
        if (!dmaBuf) {
            free(vagData);
            continue;
        }
        memcpy(dmaBuf, vagData + VAG_HEADER_SIZE, adpcmSize);
        if (dmaSize > adpcmSize)
            memset(dmaBuf + adpcmSize, 0, dmaSize - adpcmSize);

        /* Upload ADPCM data (skip VAG header) to SPU RAM */
        SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
        SpuSetTransferStartAddr(spuAddr);
        SpuWrite((uint32_t *)dmaBuf, dmaSize);
        SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
        free(dmaBuf);

        soundAddresses[i] = spuAddr;
        soundSizes[i] = adpcmSize;
        soundSampleRates[i] = sampleRate;
        soundPitches[i] = getSPUSampleRate(sampleRate);

        /* Advance by the DMA-aligned amount so the next sample does not
         * overlap the padding tail of this one. */
        spuAddr += dmaSize;

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
    if (soundDisabled || soundMuted || !soundsLoaded) {
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

    /* Key-off first so a reused channel stops cleanly before we reprogram it. */
    SpuSetKey(0, 1 << ch);

    /* Direct register writes mirroring PSn00bSDK's vagsample example.
     * ADSR1=0x00FF (AR=0 → instant attack; without this, short samples
     * finish before the envelope ramps up and you hear nothing).
     * ADSR2=0x0000 (no sustain/release curve). */
    SPU_CH_FREQ(ch)  = soundPitches[nb];
    SPU_CH_ADDR(ch)  = getSPUAddr(soundAddresses[nb]);
    SPU_CH_VOL_L(ch) = 0x3FFF;
    SPU_CH_VOL_R(ch) = 0x3FFF;
    SPU_CH_ADSR1(ch) = 0x00FF;
    SPU_CH_ADSR2(ch) = 0x0000;

    /* Start playback */
    SpuSetKey(1, 1 << ch);
}

/*
 * Stop a specific sound (key-off any channel playing it)
 */
void soundStop(int nb)
{
    if (soundDisabled || !soundsLoaded) return;
    if (nb < 0 || nb >= MAX_SOUND_EFFECTS) return;
    /* Key-off all channels that might be playing this sound.
     * We don't track which channel plays which sound, so stop all. */
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        SpuSetKey(0, 1 << ch);
    }
}

/*
 * Toggle sound mute on/off. Keys-off all voices when muting.
 */
void soundMuteToggle(void)
{
    soundMuted = !soundMuted;
    if (soundMuted) {
        /* Silence all active voices immediately */
        SpuSetKey(0, 0xFFFFFF);
    }
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
