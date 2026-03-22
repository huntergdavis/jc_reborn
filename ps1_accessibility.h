/*
 *  Accessibility stubs for PS1.
 *
 *  Audio description and ambient ocean sound support.
 *  These are stubs — the flags can be toggled but no audio
 *  is produced yet.
 *
 *  Audio description implementation notes:
 *  - Would need pre-recorded narration as VAG files on the CD,
 *    one per scene, loaded to SPU RAM on demand.
 *  - Runtime text-to-speech is not feasible on PS1 hardware.
 *  - Each VAG narration clip would be ~8-30 KB in SPU ADPCM
 *    (4-bit, ~22 KHz, mono) for 2-5 seconds of speech.
 *  - Playback via SpuSetVoiceAttr() on a dedicated SPU voice.
 *
 *  Ambient ocean implementation notes:
 *  - A looping ocean wave sample (~4-8 KB VAG) loaded to SPU RAM.
 *  - Continuous playback on a dedicated SPU voice channel with
 *    loop-point set in the ADPCM stream.
 *  - Volume modulated by tide state (high/low) and day/night.
 *  - Could share SPU RAM with music if no music is playing.
 */

#ifndef PS1_ACCESSIBILITY_H
#define PS1_ACCESSIBILITY_H

#include "mytypes.h"

/* ------------------------------------------------------------------ */
/*  Audio description                                                 */
/* ------------------------------------------------------------------ */

extern int audioDescEnabled;

void audioDescSetEnabled(int enabled);
int  audioDescGetEnabled(void);

/* Call when a scene starts. In the future this would trigger playback
 * of a pre-recorded narration VAG for the given scene. */
void audioDescOnSceneStart(const char *sceneId);

/* Call when an ADS scene starts (by ADS name + tag). */
void audioDescOnAdsStart(const char *adsName, uint16 adsTag);


/* ------------------------------------------------------------------ */
/*  Ambient ocean sounds                                              */
/* ------------------------------------------------------------------ */

extern int ambientOceanEnabled;

void ambientOceanSetEnabled(int enabled);
int  ambientOceanGetEnabled(void);

/* Call each frame — would manage looping SPU voice for ocean waves. */
void ambientOceanUpdate(void);

#endif /* PS1_ACCESSIBILITY_H */
