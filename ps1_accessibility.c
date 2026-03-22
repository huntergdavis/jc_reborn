/*
 *  Accessibility stubs for PS1.
 *
 *  Audio description and ambient ocean sound — stub implementations.
 *  These set flags only; actual audio playback is not yet implemented.
 *  See ps1_accessibility.h for implementation notes.
 */

#include <stddef.h>

#include "ps1_accessibility.h"

/* ------------------------------------------------------------------ */
/*  Audio description stubs                                           */
/* ------------------------------------------------------------------ */

int audioDescEnabled = 0;

void audioDescSetEnabled(int enabled)
{
    audioDescEnabled = enabled;
}

int audioDescGetEnabled(void)
{
    return audioDescEnabled;
}

void audioDescOnSceneStart(const char *sceneId)
{
    if (!audioDescEnabled || sceneId == NULL) return;

    /*
     * TODO: Look up the scene's narration VAG file on the CD,
     * load it into SPU RAM, and trigger playback on a dedicated
     * SPU voice channel.
     *
     * Implementation would roughly be:
     *   1. Map sceneId -> "NARR_XX.VAG" filename
     *   2. CdRead() the VAG data into a RAM buffer
     *   3. SpuSetTransferStartAddr() to an SPU RAM slot
     *   4. SpuWrite() the ADPCM data
     *   5. SpuSetVoiceAttr() with pitch, volume, ADPCM addr
     *   6. SpuSetKey(SPU_ON, voice_mask)
     *
     * Each narration clip: ~8-30 KB SPU ADPCM, 2-5 seconds,
     * mono 22050 Hz, 4-bit ADPCM encoding.
     */
}

void audioDescOnAdsStart(const char *adsName, uint16 adsTag)
{
    if (!audioDescEnabled || adsName == NULL) return;

    /*
     * TODO: Map ADS name + tag to a scene ID, then call
     * audioDescOnSceneStart().  Could reuse the same
     * captionSceneMap[] table from ps1_captions.h.
     */
    (void)adsTag;
}


/* ------------------------------------------------------------------ */
/*  Ambient ocean sound stubs                                         */
/* ------------------------------------------------------------------ */

int ambientOceanEnabled = 0;

void ambientOceanSetEnabled(int enabled)
{
    ambientOceanEnabled = enabled;

    /*
     * TODO: When enabling, load a looping ocean wave VAG sample
     * (~4-8 KB) into SPU RAM and start continuous playback.
     * When disabling, SpuSetKey(SPU_OFF, ocean_voice_mask).
     *
     * The VAG sample should have its loop-point flags set so
     * the SPU hardware loops it automatically without CPU
     * intervention each frame.
     */
}

int ambientOceanGetEnabled(void)
{
    return ambientOceanEnabled;
}

void ambientOceanUpdate(void)
{
    if (!ambientOceanEnabled) return;

    /*
     * TODO: Per-frame ocean ambient management.
     *
     * Possible behaviours:
     *   - Modulate volume based on islandState.lowTide
     *     (quieter at low tide, louder at high tide).
     *   - Adjust pitch or volume for night vs day.
     *   - Fade in/out when scenes transition.
     *   - Verify the SPU voice is still playing and restart
     *     if the loop was interrupted (e.g. by scene audio).
     */
}
