/*
 *  This file is part of 'Johnny Reborn' - PS1 Port
 *
 *  Feature toggles and scene control for the PS1 pause menu.
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

#ifndef PS1_FEATURES_H
#define PS1_FEATURES_H

#include "mytypes.h"

/* --- Feature toggle state --- */
extern int ps1SceneOrderSequential;   /* 0 = random (default), 1 = sequential */
extern int ps1DirectControlEnabled;   /* 0 = off (default), 1 = d-pad moves sprites */
extern int ps1CaptionsEnabled;        /* stub for future */
extern int ps1AudioDescEnabled;       /* stub for future */

/* --- Scene control --- */

/* Force-skip the current scene (sets adsStopRequested). */
void ps1SkipToNextScene(void);

/* Request a specific scene index for the next story pick.
 * The index corresponds to the storyScenes[] array in story_data.h.
 * Pass -1 to clear a pending request. */
void ps1RequestScene(int sceneIndex);

/* Returns the pending scene request index, or -1 if none. */
int ps1GetRequestedScene(void);

/* --- Scene ordering --- */
void ps1ToggleSceneOrder(void);

/* --- Direct control --- */
/* Call each frame with current button state to apply d-pad offsets.
 * Only effective when ps1DirectControlEnabled is set. */
void ps1DirectControlUpdate(uint16 buttons);

/* Reset direct control offsets to zero. */
void ps1DirectControlReset(void);

/* --- Time/date --- */
/* Software clock: the PS1 has no RTC, so we maintain a configurable
 * date/time that the story/holiday systems read via the existing
 * getDayOfYear/getHour/getMonthAndDay functions in utils.c. */
void ps1GetCurrentTime(int *month, int *day, int *year, int *hour, int *minute);
void ps1SetCurrentTime(int month, int day, int year, int hour, int minute);

/* --- Info accessors (read-only queries for the pause menu) --- */
const char *ps1GetCurrentAdsName(void);
uint16 ps1GetCurrentAdsTag(void);
int    ps1GetActiveThreadCount(void);
uint32 ps1GetMemoryUsed(void);
uint32 ps1GetMemoryBudget(void);
int    ps1GetCurrentFrame(void);
int    ps1GetSoundMuted(void);
int    ps1GetSceneOrderSequential(void);
int    ps1GetDirectControlEnabled(void);
int    ps1GetCaptionsEnabled(void);

#endif /* PS1_FEATURES_H */
