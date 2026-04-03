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

/* Conditional includes for PS1 freestanding build */
#ifndef PS1_BUILD
#include <stdlib.h>
#include <string.h>
#else
#include <stddef.h>
#include <string.h>
extern int rand(void);
#endif

#include "mytypes.h"
#include "utils.h"
/* Platform-specific graphics headers */
#ifdef PS1_BUILD
#include "graphics_ps1.h"
#include "sound_ps1.h"
#else
#include "graphics.h"
#include "sound.h"
#endif
#include "ttm.h"
#include "ads.h"
#include "island.h"
#include "config.h"
#include "story.h"
#include "story_data.h"


static int storyCurrentDay = 1;
static char storyBootAdsName[13] = "";
static int storyBootAdsTag = -1;
static int storyBootSceneIndex = -1;
static int storyForcedCurrentDay = -1;
static int storyForcedIslandPosValid = 0;
static int storyForcedIslandX = 0;
static int storyForcedIslandY = 0;
static int storyForcedLowTideValid = 0;
static int storyForcedLowTide = 0;

#ifdef PS1_BUILD
/* Persistent transition diagnostics rendered by graphics_ps1 overlay. */
uint16 ps1StoryDbgPhase = 0;
uint16 ps1StoryDbgSceneTag = 0;
uint16 ps1StoryDbgAdsSig = 0;
uint16 ps1StoryDbgPrevSpot = 0;
uint16 ps1StoryDbgPrevHdg = 0;
uint16 ps1StoryDbgNextSpot = 0;
uint16 ps1StoryDbgNextHdg = 0;
uint16 ps1StoryDbgSeq = 0;
#endif

#ifdef PS1_BUILD
static uint16 ps1StoryDbgFamilyId(const char *adsName)
{
    if (adsName == NULL || adsName[0] == '\0')
        return 0;
    if (strcmp(adsName, "ACTIVITY.ADS") == 0) return 1;
    if (strcmp(adsName, "BUILDING.ADS") == 0) return 2;
    if (strcmp(adsName, "FISHING.ADS") == 0) return 3;
    if (strcmp(adsName, "JOHNNY.ADS") == 0) return 4;
    if (strcmp(adsName, "MARY.ADS") == 0) return 5;
    if (strcmp(adsName, "MISCGAG.ADS") == 0) return 6;
    if (strcmp(adsName, "STAND.ADS") == 0) return 7;
    if (strcmp(adsName, "SUZY.ADS") == 0) return 8;
    if (strcmp(adsName, "VISITOR.ADS") == 0) return 9;
    if (strcmp(adsName, "WALKSTUF.ADS") == 0) return 10;
    return 0;
}
#endif

static int storyIsValidSpot(int spot)
{
    return (spot >= SPOT_A && spot <= SPOT_F);
}

static int storyIsValidHdg(int hdg)
{
    return (hdg >= HDG_S && hdg <= HDG_SE);
}

static void storyUpdateCurrentDay(void);
static void storyCalculateIslandFromDateAndTime(void);
static void storyCalculateIslandFromScene(struct TStoryScene *scene);

static int storyHasValidStart(struct TStoryScene *scene)
{
    return storyIsValidSpot(scene->spotStart) && storyIsValidHdg(scene->hdgStart);
}

static int storyHasValidEnd(struct TStoryScene *scene)
{
    return storyIsValidSpot(scene->spotEnd) && storyIsValidHdg(scene->hdgEnd);
}

static struct TStoryScene *storyFindSceneByAds(const char *adsName, int adsTag)
{
    int i;

    if (adsName == NULL || adsTag < 0)
        return NULL;

    for (i = 0; i < NUM_SCENES; i++) {
        if (!strcmp(storyScenes[i].adsName, adsName) && storyScenes[i].adsTagNo == adsTag)
            return &storyScenes[i];
    }

    return NULL;
}

void storySetBootScene(const char *adsName, uint16 adsTag)
{
    if (adsName == NULL) {
        storyBootAdsName[0] = '\0';
        storyBootAdsTag = -1;
        storyBootSceneIndex = -1;
        return;
    }

    strncpy(storyBootAdsName, adsName, sizeof(storyBootAdsName) - 1);
    storyBootAdsName[sizeof(storyBootAdsName) - 1] = '\0';
    storyBootAdsTag = (int)adsTag;
    storyBootSceneIndex = -1;
}

void storySetBootSceneIndex(int sceneIndex)
{
    if (sceneIndex < 0 || sceneIndex >= NUM_SCENES) {
        storyBootSceneIndex = -1;
        return;
    }

    storyBootSceneIndex = sceneIndex;
    storyBootAdsName[0] = '\0';
    storyBootAdsTag = -1;
}

static int storyBootSingleSceneIndex = -1;

void storySetBootSingleSceneIndex(int sceneIndex)
{
    if (sceneIndex < 0 || sceneIndex >= NUM_SCENES) {
        storyBootSingleSceneIndex = -1;
        return;
    }
    storyBootSingleSceneIndex = sceneIndex;
    storyBootSceneIndex = -1;
    storyBootAdsName[0] = '\0';
    storyBootAdsTag = -1;
}

void storySetForcedCurrentDay(int day)
{
    if (day < 1 || day > 11) {
        storyForcedCurrentDay = -1;
        return;
    }

    storyForcedCurrentDay = day;
}

void storySetIslandOverrides(int hasPosition, int xPos, int yPos, int hasLowTide, int lowTide)
{
    storyForcedIslandPosValid = hasPosition;
    storyForcedIslandX = xPos;
    storyForcedIslandY = yPos;
    storyForcedLowTideValid = hasLowTide;
    storyForcedLowTide = lowTide;
}

int storyHasBootOverridePending(void)
{
    return storyBootSingleSceneIndex >= 0 ||
           storyBootSceneIndex >= 0 ||
           (storyBootAdsName[0] != '\0' && storyBootAdsTag >= 0);
}

void storyPlayBootSceneDirect(int sceneIndex)
{
    if (sceneIndex < 0 || sceneIndex >= NUM_SCENES) return;

    struct TStoryScene *scene = &storyScenes[sceneIndex];

    storyUpdateCurrentDay();
    storyCalculateIslandFromDateAndTime();
    if (scene->flags & ISLAND)
        storyCalculateIslandFromScene(scene);

    adsInit();

    if (scene->flags & ISLAND)
        adsInitIsland();
    else
        adsNoIsland();

#ifdef PS1_BUILD
    extern void ps1_pilotPrearmPackForAds(const char *adsName);
    ps1_pilotPrearmPackForAds(scene->adsName);
#endif

    adsPlay(scene->adsName, scene->adsTagNo);
}

static struct TStoryScene *storyPickScene(
                uint16 wantedFlags, uint16 unwantedFlags)
{
    int scenes[NUM_SCENES];
    int numScenes = 0;


    for (int i=0; i < NUM_SCENES; i++) {

        struct TStoryScene scene = storyScenes[i];

        if ((scene.flags & wantedFlags) == wantedFlags
             && !(scene.flags & unwantedFlags)
             && (scene.dayNo == 0 || scene.dayNo == storyCurrentDay)
           ) {
            scenes[numScenes++] = i;
        }
    }

    return &storyScenes[scenes[rand() % numScenes]];
}


static void storyUpdateCurrentDay()
{
    if (storyForcedCurrentDay >= 1 && storyForcedCurrentDay <= 11) {
        storyCurrentDay = storyForcedCurrentDay;
        debugMsg("The day of the story is forced to: %d", storyCurrentDay);
        return;
    }

    struct TConfig config;
    int today;
    int hasChanged = 0;

    cfgFileRead(&config);
    today = getDayOfYear();

    if (today != config.date) {
        debugMsg("System date has changed since last sequence -> next day of the story");
        config.date = today;
        config.currentDay += 1;
        hasChanged = 1;
    }

    if (config.currentDay < 1 || config.currentDay > 11) {
        config.currentDay = 1;
        hasChanged = 1;
    }

    if (hasChanged)
        cfgFileWrite(&config);

    storyCurrentDay = config.currentDay;
    debugMsg("The day of the story is: %d", storyCurrentDay);
}


static void storyCalculateIslandFromDateAndTime()
{
    // Night ?
    int hour = (getHour() % 8);
    islandState.night = (hour == 0 || hour == 7);

    // Holidays ?
    islandState.holiday = 0;
    char *currentDate = getMonthAndDay();

    // Halloween : 29/10 to 31/10
    if (strcmp("1028", currentDate) < 0 && strcmp(currentDate, "1101") < 0)
        islandState.holiday = 1;
    else
    // St Patrick: 15/03 to 17/03
    if (strcmp("0314", currentDate) < 0 && strcmp(currentDate, "0318") < 0)
        islandState.holiday = 2;
    else
    // Christmas : 23/12 to 25/12
    if (strcmp("1222", currentDate) < 0 && strcmp(currentDate, "1226") < 0)
        islandState.holiday = 3;
    else
    // New year  : 29/12 to 01/01
    if (strcmp("1228", currentDate) < 0 || strcmp(currentDate, "0102") < 0)
        islandState.holiday = 4;

}


static void storyCalculateIslandFromScene(struct TStoryScene *scene)
{
    // Low tide ?
    if ((scene->flags & LOWTIDE_OK) && (rand() % 2))
        islandState.lowTide = 1;
    else
        islandState.lowTide = 0;


    // Randomize the position of the island
    if (scene->flags  & VARPOS_OK) {
        if (rand() % 2) {
            islandState.xPos = -222 + (rand() % 109);
            islandState.yPos = -44  + (rand() % 128);
        }
        else if (rand() % 2) {
            islandState.xPos = -114 + (rand() % 134);
            islandState.yPos = -14  + (rand() % 99 );
        }
        else {
            islandState.xPos = -114 + (rand() % 119);
            islandState.yPos = -73  + (rand() % 60 );
        }
    }
    else {
        if (scene->flags & LEFT_ISLAND) {
            islandState.xPos    = -272;
            islandState.yPos    = 0;
        }
        else {
            islandState.xPos    = 0;
            islandState.yPos    = 0;
        }
    }

    if (storyForcedLowTideValid)
        islandState.lowTide = storyForcedLowTide ? 1 : 0;

    if (storyForcedIslandPosValid) {
        islandState.xPos = storyForcedIslandX;
        islandState.yPos = storyForcedIslandY;
    }


    // How much of the raft was John able to build ?
    if (scene->flags & NORAFT) {
        islandState.raft = 0;
    }
    else {
        switch (storyCurrentDay) {

            case 0:
            case 1:
            case 2:
                islandState.raft = 1;
                break;

            case 3:
            case 4:
            case 5:
                islandState.raft = storyCurrentDay - 1;
                break;

            default:
                islandState.raft = 5;
                break;
        }
    }


    // For scene VISITOR.ADS#3 (cargo), never display holiday items - or they
    // will be drawn over the hull when it fills the screen at the end. This
    // conforms to the behavior of the original - which, moreover, freezes
    // the shore animation while we dont
    if (scene->flags & HOLIDAY_NOK)
        islandState.holiday = 0;
}


void storyPlay()
{
    uint16 wantedFlags   = 0;
    uint16 unwantedFlags = 0;
    int firstSequence = 1;
    struct TStoryScene *bootScene = NULL;
    struct TStoryScene *forcedIntermediateScene = NULL;

    adsInit();

    /* Skip intro if we have an exact-scene boot override pending */
    if (!storyHasBootOverridePending())
        adsPlayIntro();

    while (1) {
#ifdef PS1_BUILD
        if (ps1StoryDbgSeq < 0xFFFFU) ps1StoryDbgSeq++;
        ps1StoryDbgPhase = 1;
#endif

        storyUpdateCurrentDay();
        storyCalculateIslandFromDateAndTime();

        bootScene = NULL;
        forcedIntermediateScene = NULL;

        /* story single: play exactly one scene then exit */
        if (storyBootSingleSceneIndex >= 0 && storyBootSingleSceneIndex < NUM_SCENES) {
            bootScene = &storyScenes[storyBootSingleSceneIndex];
            storyBootSingleSceneIndex = -1;
        }
        else if (storyBootSceneIndex >= 0 && storyBootSceneIndex < NUM_SCENES) {
            struct TStoryScene *requestedScene = &storyScenes[storyBootSceneIndex];
            if (requestedScene->flags & FINAL)
                bootScene = requestedScene;
            else
                forcedIntermediateScene = requestedScene;
            storyBootSceneIndex = -1;
        }
        else if (storyBootAdsName[0] != '\0' && storyBootAdsTag >= 0) {
            bootScene = storyFindSceneByAds(storyBootAdsName, storyBootAdsTag);
            storyBootAdsName[0] = '\0';
            storyBootAdsTag = -1;
        }

        struct TStoryScene *finalScene = bootScene;
        if (finalScene == NULL) {
            unwantedFlags = 0;
            if (firstSequence)
                unwantedFlags |= FIRST;

            finalScene = storyPickScene(FINAL, unwantedFlags);
            /* Transition invariant: if final scene needs a walk-in, it must have valid start metadata. */
            if (!(finalScene->flags & FIRST)) {
                for (int tryPick = 0; tryPick < 32 && !storyHasValidStart(finalScene); tryPick++)
                    finalScene = storyPickScene(FINAL, unwantedFlags);
            }
        }

        if (finalScene->flags & ISLAND) {
            storyCalculateIslandFromScene(finalScene);
            adsInitIsland();
        }
        else {
            adsNoIsland();
        }

        int prevSpot = -1;
        int prevHdg  = -1;
        char lastAdsName[13];
        int lastAdsTag = -1;
        lastAdsName[0] = '\0';

        if (bootScene == NULL && !(finalScene->flags & FIRST)) {

            wantedFlags = 0;
            unwantedFlags |= FINAL;

            if (islandState.lowTide)
                wantedFlags |= LOWTIDE_OK;

            if (islandState.xPos || islandState.yPos)
                wantedFlags |= VARPOS_OK;

            for (int i=0; i < 6 + (rand() % 14); i++) {

                struct TStoryScene *scene = NULL;
                if (forcedIntermediateScene != NULL) {
                    scene = forcedIntermediateScene;
                    forcedIntermediateScene = NULL;
                } else {
                    for (int pickTry = 0; pickTry < 8; pickTry++) {
                        scene = storyPickScene(wantedFlags, unwantedFlags);
                        if (prevSpot != -1 && !storyHasValidStart(scene)) continue;
                        if (!storyHasValidEnd(scene)) continue;
                        if (lastAdsName[0] == '\0') break;
                        if (strcmp(scene->adsName, lastAdsName) != 0) break;
                        if (scene->adsTagNo != lastAdsTag) break;
                    }
                }

                if (prevSpot != -1)
#ifdef PS1_BUILD
                {
                    ps1StoryDbgPhase = 3;
                    ps1StoryDbgPrevSpot = (uint16)prevSpot;
                    ps1StoryDbgPrevHdg = (uint16)prevHdg;
                    ps1StoryDbgNextSpot = (uint16)scene->spotStart;
                    ps1StoryDbgNextHdg = (uint16)scene->hdgStart;
                    adsPlayWalk(prevSpot, prevHdg,
                        scene->spotStart, scene->hdgStart);
                }
#else
                    adsPlayWalk(prevSpot, prevHdg,
                        scene->spotStart, scene->hdgStart);
#endif

                ttmDx = islandState.xPos
                            + (scene->flags & LEFT_ISLAND ? 272 : 0);
                ttmDy = islandState.yPos;

                if (scene->dayNo)
                    soundPlay(0);

#ifdef PS1_BUILD
                ps1StoryDbgPhase = 2;
                ps1StoryDbgAdsSig = ps1StoryDbgFamilyId(scene->adsName);
                ps1StoryDbgSceneTag = (uint16)scene->adsTagNo;
                ps1StoryDbgNextSpot = (uint16)scene->spotEnd;
                ps1StoryDbgNextHdg = (uint16)scene->hdgEnd;
#endif
                adsPlay(scene->adsName, scene->adsTagNo);
#ifdef PS1_BUILD
                if (!ps1AdsLastPlayLaunched) {
                    /* Skip dead scene selections that produced no ADS threads. */
                    continue;
                }
#endif
                strcpy(lastAdsName, scene->adsName);
                lastAdsTag = scene->adsTagNo;

                unwantedFlags |= FIRST;
                if (storyHasValidEnd(scene)) {
                    prevSpot = scene->spotEnd;
                    prevHdg = scene->hdgEnd;
                } else {
                    prevSpot = -1;
                    prevHdg = -1;
                }
            }
        }

        if (prevSpot != -1 && storyHasValidStart(finalScene))
#ifdef PS1_BUILD
        {
            ps1StoryDbgPhase = 4;
            ps1StoryDbgAdsSig = ps1StoryDbgFamilyId(finalScene->adsName);
            ps1StoryDbgSceneTag = (uint16)finalScene->adsTagNo;
            ps1StoryDbgPrevSpot = (uint16)prevSpot;
            ps1StoryDbgPrevHdg = (uint16)prevHdg;
            ps1StoryDbgNextSpot = (uint16)finalScene->spotStart;
            ps1StoryDbgNextHdg = (uint16)finalScene->hdgStart;
            adsPlayWalk(prevSpot, prevHdg, finalScene->spotStart, finalScene->hdgStart);
        }
#else
            adsPlayWalk(prevSpot, prevHdg, finalScene->spotStart, finalScene->hdgStart);
#endif

        if (finalScene->flags & ISLAND) {
            ttmDx = islandState.xPos + (finalScene->flags & LEFT_ISLAND ? 272 : 0);
            ttmDy = islandState.yPos;
        }
        else {
            ttmDx = ttmDy = 0;
        }

        if (finalScene->dayNo)
            soundPlay(0);

#ifdef PS1_BUILD
        ps1StoryDbgPhase = 5;
        ps1StoryDbgAdsSig = ps1StoryDbgFamilyId(finalScene->adsName);
        ps1StoryDbgSceneTag = (uint16)finalScene->adsTagNo;
        ps1StoryDbgNextSpot = (uint16)finalScene->spotEnd;
        ps1StoryDbgNextHdg = (uint16)finalScene->hdgEnd;
#endif
        adsPlay(finalScene->adsName, finalScene->adsTagNo);
#ifdef PS1_BUILD
        if (!ps1AdsLastPlayLaunched) {
            /* Retry a fresh sequence immediately instead of idling on static background. */
            if (finalScene->flags & ISLAND)
                adsReleaseIsland();
            firstSequence = 0;
            continue;
        }
#endif

#ifdef PS1_BUILD
        ps1StoryDbgPhase = 6;
#endif
        grFadeOut();

        if (finalScene->flags & ISLAND)
            adsReleaseIsland();

        if (bootScene != NULL)
            return;

        firstSequence = 0;
    }
}
