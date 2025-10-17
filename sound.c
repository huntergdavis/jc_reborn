/*
 *  This file is part of 'Johnny Reborn'
 *
 *  An open-source engine for the classic
 *  'Johnny Castaway' screensaver by Sierra.
 *
 *  Copyright (C) 2019 Jeremie GUILLAUME
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

#include <SDL2/SDL.h>
#include <string.h>

#include "mytypes.h"
#include "utils.h"
#include "sound.h"


#define NUM_OF_SOUNDS  25
#define SOUND_CACHE_SIZE 3  /* LRU cache: keep 3 most recent sounds in memory */


struct TSound {
    uint32  length;
    uint8   *data;
    uint32  lastUsedTick;  /* For LRU eviction */
    uint8   cached;        /* 1 if loaded in memory, 0 if needs loading */
};


int soundDisabled = 0;


static struct TSound sounds[NUM_OF_SOUNDS];
static struct TSound *currentSound;
static uint32 soundTick = 0;  /* Increments on each soundPlay() */

static uint8  *currentPtr;
static uint32 currentRemaining;


static void soundCallback(void *userdata, uint8 *stream, int rqdLen)
{
    if (currentRemaining > rqdLen) {
        memcpy(stream, currentPtr, rqdLen);
        currentPtr += rqdLen;
        currentRemaining -= rqdLen;
    }
    else {
        memcpy(stream, currentPtr, currentRemaining);
        memset(stream + currentRemaining, 127, rqdLen - currentRemaining);  // 127 == silence
        currentRemaining = 0;
        //SDL_PauseAudio(1);
    }
}


/* Load a sound file on-demand (LRU cache with 3 slots) */
static void soundLoadOnDemand(int nb)
{
    if (sounds[nb].cached) {
        /* Already in cache, just update LRU tick */
        sounds[nb].lastUsedTick = soundTick;
        return;
    }

    /* Count how many sounds are currently cached */
    int cachedCount = 0;
    for (int i = 0; i < NUM_OF_SOUNDS; i++) {
        if (sounds[i].cached) {
            cachedCount++;
        }
    }

    /* If cache is full, evict least recently used */
    if (cachedCount >= SOUND_CACHE_SIZE) {
        int lruIndex = -1;
        uint32 lruTick = 0xFFFFFFFF;

        for (int i = 0; i < NUM_OF_SOUNDS; i++) {
            if (sounds[i].cached && sounds[i].lastUsedTick < lruTick) {
                lruTick = sounds[i].lastUsedTick;
                lruIndex = i;
            }
        }

        if (lruIndex >= 0) {
            /* Evict LRU sound */
            if (debugMode) {
                debugMsg("Sound LRU: evicting sound%d.wav", lruIndex);
            }
            SDL_FreeWAV(sounds[lruIndex].data);
            sounds[lruIndex].data = NULL;
            sounds[lruIndex].length = 0;
            sounds[lruIndex].cached = 0;
        }
    }

    /* Load the requested sound */
    char filename[20];
    sprintf(filename, "sound%d.wav", nb);

    SDL_AudioSpec audioSpec;
    if (SDL_LoadWAV(filename, &audioSpec, &sounds[nb].data, &sounds[nb].length) == NULL) {
        sounds[nb].data = NULL;
        sounds[nb].length = 0;
        sounds[nb].cached = 0;
        debugMsg("SDL_LoadWAV() warning: %s", SDL_GetError());
    } else {
        sounds[nb].cached = 1;
        sounds[nb].lastUsedTick = soundTick;
        if (debugMode) {
            debugMsg("Sound LRU: loaded sound%d.wav (%u bytes)", nb, sounds[nb].length);
        }
    }
}


void soundInit()
{
    if (soundDisabled)
        return;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        debugMsg("SDL init audio error: %s", SDL_GetError());
        soundDisabled = 1;
        return;
    }

    /* Initialize sound metadata (don't load WAV files yet) */
    for (int i = 0; i < NUM_OF_SOUNDS; i++) {
        sounds[i].data = NULL;
        sounds[i].length = 0;
        sounds[i].lastUsedTick = 0;
        sounds[i].cached = 0;
    }

    /* Load first sound to get audio spec, then open audio device */
    SDL_AudioSpec audioSpec;
    char filename[20];
    sprintf(filename, "sound0.wav");

    if (SDL_LoadWAV(filename, &audioSpec, &sounds[0].data, &sounds[0].length) == NULL) {
        debugMsg("SDL_LoadWAV() error loading sound0.wav: %s", SDL_GetError());
        /* Try to continue with default spec */
        audioSpec.freq = 22050;
        audioSpec.format = AUDIO_U8;
        audioSpec.channels = 1;
    } else {
        sounds[0].cached = 1;
        sounds[0].lastUsedTick = 0;
        if (debugMode) {
            debugMsg("Sound LRU: pre-loaded sound0.wav (%u bytes, %d Hz)", sounds[0].length, audioSpec.freq);
        }
    }

    /* Configure audio callback */
    audioSpec.samples = 512;       /* Smaller buffer for lower latency */
    audioSpec.callback = soundCallback;
    audioSpec.userdata = NULL;

    if (SDL_OpenAudio(&audioSpec, NULL) < 0) {
        debugMsg("SDL_OpenAudio() error: %s", SDL_GetError());
        soundDisabled = 1;
        return;
    }

    currentRemaining = 0;
    soundTick = 0;
    SDL_PauseAudio(0);

    if (debugMode) {
        debugMsg("Sound system initialized with LRU cache (max %d sounds in memory)", SOUND_CACHE_SIZE);
    }
}


void soundEnd()
{
    if (soundDisabled)
        return;

    SDL_CloseAudio();

    for (int i=0; i < NUM_OF_SOUNDS; i++)
        if (sounds[i].data != NULL)
            SDL_FreeWAV(sounds[i].data);
}


void soundPlay(int nb)
{
    if (soundDisabled)
        return;

    if (nb < 0 || NUM_OF_SOUNDS <= nb) {
        debugMsg("soundPlay(): wrong sound sample index #%d", nb);
        return;
    }

    soundTick++;  /* Increment for LRU tracking */

    /* Load sound on-demand if not cached */
    soundLoadOnDemand(nb);

    if (sounds[nb].length && sounds[nb].data) {

        SDL_LockAudio();

        currentSound     = &sounds[nb];
        currentPtr       = currentSound->data;
        currentRemaining = currentSound->length;

        SDL_UnlockAudio();
    }
    else {
        debugMsg("Non-existent sound sample #%d", nb);
    }
}

