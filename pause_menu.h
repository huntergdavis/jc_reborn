/*
 *  This file is part of 'Johnny Reborn' - PS1 Port
 *
 *  Pause menu overlay for PS1 build
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

#ifndef PAUSE_MENU_H
#define PAUSE_MENU_H

enum PauseMenuState {
    PAUSE_MENU_MAIN,
    PAUSE_MENU_SCENE_INFO,
    PAUSE_MENU_CONTROLS,
    PAUSE_MENU_SET_TIME,
};

/* Initialize the pause menu system (call once during startup, after graphicsInit). */
void pauseMenuInit(void);

/* Show / hide the pause menu overlay. */
void pauseMenuShow(void);
void pauseMenuHide(void);

/* Query whether the pause menu is currently visible. */
int  pauseMenuIsVisible(void);

/* Return the current sub-screen state. */
enum PauseMenuState pauseMenuGetState(void);

/* Set the sub-screen state (e.g. back to main). */
void pauseMenuSetState(enum PauseMenuState state);

/*
 * Run one frame of the pause menu.
 * Reads controller input, updates menu state, and draws the overlay.
 * Returns 1 while the menu should stay open, 0 when the user resumes.
 */
int  pauseMenuUpdate(void);

/* Flag: set to 1 by the menu when "Next Scene" is chosen.
 * Cleared by the ADS/story loop after it acts on it. */
extern int pauseMenuRequestNextScene;

#endif /* PAUSE_MENU_H */
