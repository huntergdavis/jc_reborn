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

#include <psxpad.h>

#include "mytypes.h"
#include "ps1_features.h"
#include "graphics_ps1.h"
#include "resource.h"

/* These are defined in ads.c / ads.h */
extern char ps1AdsCurrentName[16];
extern uint16 ps1AdsCurrentTag;
extern uint16 ps1AdsDbgActiveThreads;
void adsRequestStop(void);

/* Defined in graphics_ps1.c */
extern int grDx;
extern int grDy;
int grGetCurrentFrame(void);

/* Defined in sound_ps1.c */
extern int soundDisabled;

/* Defined in utils.c (PS1 software clock) */
extern int ps1SoftMonth;
extern int ps1SoftDay;
extern int ps1SoftYear;
extern int ps1SoftHour;
extern int ps1SoftMinute;

/* --- Feature toggle state --- */
int ps1SceneOrderSequential = 0;
int ps1DirectControlEnabled = 0;
int ps1CaptionsEnabled      = 0;
int ps1AudioDescEnabled     = 0;

/* --- Scene request --- */
static int ps1RequestedSceneIndex = -1;

/* --- Scene control --- */

void ps1SkipToNextScene(void)
{
    adsRequestStop();
}

void ps1RequestScene(int sceneIndex)
{
    ps1RequestedSceneIndex = sceneIndex;
}

int ps1GetRequestedScene(void)
{
    return ps1RequestedSceneIndex;
}

/* --- Scene ordering --- */

void ps1ToggleSceneOrder(void)
{
    ps1SceneOrderSequential = !ps1SceneOrderSequential;
}

/* --- Direct control --- */

void ps1DirectControlUpdate(uint16 buttons)
{
    if (!ps1DirectControlEnabled)
        return;

    if (buttons & PAD_UP)    grDy -= 2;
    if (buttons & PAD_DOWN)  grDy += 2;
    if (buttons & PAD_LEFT)  grDx -= 2;
    if (buttons & PAD_RIGHT) grDx += 2;
}

void ps1DirectControlReset(void)
{
    grDx = 0;
    grDy = 0;
}

/* --- Time/date --- */

void ps1GetCurrentTime(int *month, int *day, int *year, int *hour, int *minute)
{
    if (month)  *month  = ps1SoftMonth;
    if (day)    *day    = ps1SoftDay;
    if (year)   *year   = ps1SoftYear;
    if (hour)   *hour   = ps1SoftHour;
    if (minute) *minute = ps1SoftMinute;
}

void ps1SetCurrentTime(int month, int day, int year, int hour, int minute)
{
    if (month >= 1 && month <= 12)  ps1SoftMonth  = month;
    if (day >= 1 && day <= 31)      ps1SoftDay    = day;
    if (year >= 1990 && year <= 2099) ps1SoftYear = year;
    if (hour >= 0 && hour <= 23)    ps1SoftHour   = hour;
    if (minute >= 0 && minute <= 59) ps1SoftMinute = minute;
}

/* --- Info accessors --- */

const char *ps1GetCurrentAdsName(void)
{
    return ps1AdsCurrentName;
}

uint16 ps1GetCurrentAdsTag(void)
{
    return ps1AdsCurrentTag;
}

int ps1GetActiveThreadCount(void)
{
    return (int)ps1AdsDbgActiveThreads;
}

uint32 ps1GetMemoryUsed(void)
{
    return (uint32)getTotalMemoryUsed();
}

uint32 ps1GetMemoryBudget(void)
{
    return (uint32)getMemoryBudget();
}

int ps1GetCurrentFrame(void)
{
    return grGetCurrentFrame();
}

int ps1GetSoundMuted(void)
{
    return soundDisabled;
}

int ps1GetSceneOrderSequential(void)
{
    return ps1SceneOrderSequential;
}

int ps1GetDirectControlEnabled(void)
{
    return ps1DirectControlEnabled;
}

int ps1GetCaptionsEnabled(void)
{
    return ps1CaptionsEnabled;
}
