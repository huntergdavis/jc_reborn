/*
 *  Closed-caption display system for PS1.
 *
 *  Provides subtitle text for each scene, intended for
 *  accessibility.  The caption text is stored in ps1_captions.h
 *  as const string data (lives in ROM / read-only section).
 *
 *  Usage:
 *    captionsSetEnabled(1);                 // turn on
 *    captionsOnSceneStart("scene05");       // by scene ID
 *    captionsOnAdsStart("ACTIVITY", 5);     // by ADS name+tag
 *    const char *txt = captionsGetCurrent(); // each frame
 */

#include <stddef.h>
#include <string.h>

#include "ps1_captions.h"

/* ------------------------------------------------------------------ */
/*  State                                                             */
/* ------------------------------------------------------------------ */

static int captionsEnabled      = 0;
static const char *currentCaption = NULL;
static int captionDisplayTimer  = 0;

/* ~5 seconds at 60 fps */
#define CAPTION_DURATION_FRAMES  300


/* ------------------------------------------------------------------ */
/*  Enable / disable                                                  */
/* ------------------------------------------------------------------ */

void captionsSetEnabled(int enabled)
{
    captionsEnabled = enabled;
    if (!enabled) {
        currentCaption = NULL;
        captionDisplayTimer = 0;
    }
}

int captionsGetEnabled(void)
{
    return captionsEnabled;
}


/* ------------------------------------------------------------------ */
/*  Scene start — lookup by scene ID string (e.g. "scene05")          */
/* ------------------------------------------------------------------ */

void captionsOnSceneStart(const char *sceneId)
{
    if (!captionsEnabled || sceneId == NULL) return;

    currentCaption = NULL;
    captionDisplayTimer = 0;

    for (int i = 0; captions[i].scene_id != NULL; i++) {
        if (strcmp(captions[i].scene_id, sceneId) == 0) {
            /* Skip empty captions (e.g. scene60) */
            if (captions[i].text[0] == '\0') return;
            currentCaption = captions[i].text;
            captionDisplayTimer = CAPTION_DURATION_FRAMES;
            return;
        }
    }
}


/* ------------------------------------------------------------------ */
/*  Scene start — lookup by ADS name + tag number                     */
/*  Translates the ADS identity to a scene ID, then looks up the      */
/*  caption text.  The ADS name comparison ignores the ".ADS" suffix.  */
/* ------------------------------------------------------------------ */

void captionsOnAdsStart(const char *adsName, uint16 adsTag)
{
    if (!captionsEnabled || adsName == NULL) return;

    /* Strip ".ADS" suffix if present — compare only the base name. */
    char baseName[16];
    int len = 0;
    while (adsName[len] != '\0' && adsName[len] != '.' && len < 15) {
        baseName[len] = adsName[len];
        len++;
    }
    baseName[len] = '\0';

    for (int i = 0; captionSceneMap[i].caption_id != NULL; i++) {
        if (strcmp(captionSceneMap[i].ads_name, baseName) == 0
            && captionSceneMap[i].ads_tag == adsTag) {
            captionsOnSceneStart(captionSceneMap[i].caption_id);
            return;
        }
    }

    /* No mapping found — clear any previous caption. */
    currentCaption = NULL;
    captionDisplayTimer = 0;
}


/* ------------------------------------------------------------------ */
/*  Per-frame tick — returns current text or NULL                      */
/* ------------------------------------------------------------------ */

const char *captionsGetCurrent(void)
{
    if (!captionsEnabled || captionDisplayTimer <= 0)
        return NULL;

    captionDisplayTimer--;
    return currentCaption;
}
