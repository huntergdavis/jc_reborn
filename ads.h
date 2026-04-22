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

void adsInit();
void adsInitIsland();
void adsReleaseIsland();
void adsNoIsland();
void adsPlay(char *adsName, uint16 adsTag);
void adsPlayIntro();
void adsPlayWalk();
void adsPlaySingleTtm(char *ttmName, uint16 startTag);
void adsPlayBench();  // TODO
void adsCaptureCurrentFrame(void);

/* Minimal wave-backdrop enable for the fgpilot scene path (PS1-only).
 * Call adsPilotPreloadBackgrndBmp BEFORE any other scene setup so the
 * ~93 KB PSB load hits a fresh heap; call adsPilotEnableWaveBackdrop AFTER
 * the scene SCRs are loaded to configure the thread + seed wave positions;
 * call adsPilotTickBackgroundWaves once per frame in the main loop. */
void adsPilotPreloadBackgrndBmp(void);
void adsPilotEnableWaveBackdrop(void);
void adsPilotTickBackgroundWaves(void);
void adsPilotStampHoliday(void);

/* Set by adsPlay()/adsPlayWalk(): 1 if at least one scene thread launched. */
extern int ps1AdsLastPlayLaunched;
extern char ps1AdsCurrentName[16];
extern uint16 ps1AdsCurrentTag;
