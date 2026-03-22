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

#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>
#include <stdio.h>
#include <string.h>

#include "mytypes.h"
#include "pause_menu.h"
#include "graphics_ps1.h"
#include "sound_ps1.h"
#include "resource.h"

/* ---------------------------------------------------------------------------
 *  External telemetry / debug state from ads.c, story.c, graphics_ps1.c
 * ------------------------------------------------------------------------- */
extern char   ps1AdsCurrentName[16];
extern uint16 ps1AdsCurrentTag;
extern uint16 ps1AdsDbgActiveThreads;
extern uint16 ps1AdsDbgRecordedSpritesFrame;

extern uint16 ps1StoryDbgPhase;
extern uint16 ps1StoryDbgSeq;

/* Font stream from ps1_debug.c -- reused for pause menu text. */
extern int fontID;

/* Controller pad buffer from events_ps1.c. */

/* ---------------------------------------------------------------------------
 *  Menu state
 * ------------------------------------------------------------------------- */
static int              menuVisible  = 0;
static int              menuCursor   = 0;
static enum PauseMenuState menuState = PAUSE_MENU_MAIN;

/* "Next scene" request flag consumed by ads/story loop. */
int pauseMenuRequestNextScene = 0;

/* Time/date editing fields. */
static int editField  = 0;   /* 0=month,1=day,2=year,3=hour,4=min */
static int editMonth  = 6;
static int editDay    = 30;
static int editYear   = 2026;
static int editHour   = 12;
static int editMinute = 0;

/* Debounce: tracks which buttons were held last frame so we only act on
 * fresh presses (not auto-repeat while held). */
static uint16 prevButtons = 0;

/* ---------------------------------------------------------------------------
 *  Main menu item descriptors
 * ------------------------------------------------------------------------- */
enum {
    MENU_RESUME,
    MENU_SOUND,
    MENU_TELEMETRY,
    MENU_CAPTIONS,
    MENU_SCENE_ORDER,
    MENU_DIRECT_CONTROL,
    MENU_NEXT_SCENE,
    MENU_SET_TIME,
    MENU_SCENE_INFO,
    MENU_CONTROLS,
    MENU_COUNT
};

/* Clamp helpers */
static int clampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int daysInMonth(int m, int y)
{
    static const int dim[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12) return 30;
    int d = dim[m];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) d = 29;
    return d;
}

/* ---------------------------------------------------------------------------
 *  Pad reading helpers
 * ------------------------------------------------------------------------- */

/* Returns buttons that are newly pressed this frame (not held from last). */
static uint16 pmNewPress(uint16 cur)
{
    uint16 fresh = cur & ~prevButtons;
    return fresh;
}

/* ---------------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------------- */
void pauseMenuInit(void)
{
    /* Reload BIOS font into VRAM -- graphicsInit may have overwritten it. */
    FntLoad(960, 0);

    /* We keep the fontID from ps1_debug.c if still valid; otherwise open a
     * new stream. FntOpen returns a small integer stream id. */
    if (fontID < 0) {
        fontID = FntOpen(0, 0, 640, 480, 0, 1024);
    }

    menuVisible = 0;
    menuCursor  = 0;
    menuState   = PAUSE_MENU_MAIN;

    /* We piggyback on the events_ps1 pad buffers (extern pad_buff). */
}

void pauseMenuShow(void)
{
    /* Reload font VRAM each time we enter the menu -- the game's
     * LoadImage uploads may have clobbered the (960,0) font area. */
    FntLoad(960, 0);

    menuVisible = 1;
    menuCursor  = 0;
    menuState   = PAUSE_MENU_MAIN;
    prevButtons = 0xFFFF;  /* Treat all buttons as "held" so the initial
                              press that opened the menu is not re-acted. */
}

void pauseMenuHide(void)
{
    menuVisible = 0;
}

int pauseMenuIsVisible(void)
{
    return menuVisible;
}

enum PauseMenuState pauseMenuGetState(void)
{
    return menuState;
}

void pauseMenuSetState(enum PauseMenuState state)
{
    menuState = state;
    if (state == PAUSE_MENU_MAIN)
        menuCursor = 0;
    prevButtons = 0xFFFF;  /* suppress stale press */
}

/* ---------------------------------------------------------------------------
 *  Drawing helpers -- all use FntPrint into the existing fontID stream
 * ------------------------------------------------------------------------- */

/* Dim the background by writing a dark semi-transparent overlay into bgTile
 * RAM buffers.  This is cheap: just halve every pixel once when the menu
 * first appears.  We track whether we already dimmed so repeated frames
 * don't keep halving. */
static int bgDimmed = 0;

static void dimBackground(void)
{
    if (bgDimmed) return;

    PS1Surface *tiles[] = { bgTile0, bgTile1, bgTile3, bgTile4 };
    for (int t = 0; t < 4; t++) {
        if (!tiles[t] || !tiles[t]->pixels) continue;
        uint32 count = (uint32)tiles[t]->width * tiles[t]->height;
        uint16 *px = tiles[t]->pixels;
        for (uint32 i = 0; i < count; i++) {
            uint16 c = px[i];
            if (c == 0) continue;
            uint16 r = (c & 0x001F) >> 1;
            uint16 g = ((c >> 5) & 0x1F) >> 1;
            uint16 b = ((c >> 10) & 0x1F) >> 1;
            px[i] = (b << 10) | (g << 5) | r;
        }
    }
    bgDimmed = 1;
}

static void drawSeparator(void)
{
    FntPrint(fontID, "----------------------------\n");
}

/* ---------------------------------------------------------------------------
 *  Sub-screen: Scene Info
 * ------------------------------------------------------------------------- */
static void drawSceneInfo(void)
{
    size_t used   = getTotalMemoryUsed();
    size_t budget = getMemoryBudget();

    FntPrint(fontID, "\n");
    drawSeparator();
    FntPrint(fontID, "     SCENE INFO\n");
    drawSeparator();
    FntPrint(fontID, "\n");
    FntPrint(fontID, " ADS: %s tag %d\n", ps1AdsCurrentName, (int)ps1AdsCurrentTag);
    FntPrint(fontID, " Threads: %d active\n", (int)ps1AdsDbgActiveThreads);
    FntPrint(fontID, " Memory: %dKB / %dKB\n",
             (int)(used / 1024), (int)(budget / 1024));
    FntPrint(fontID, " Sprites: %d drawn\n", (int)ps1AdsDbgRecordedSpritesFrame);
    FntPrint(fontID, " Story phase: %d  seq: %d\n",
             (int)ps1StoryDbgPhase, (int)ps1StoryDbgSeq);
    FntPrint(fontID, "\n");
    FntPrint(fontID, " (START to go back)\n");
}

/* ---------------------------------------------------------------------------
 *  Sub-screen: Controls
 * ------------------------------------------------------------------------- */
static void drawControls(void)
{
    FntPrint(fontID, "\n");
    drawSeparator();
    FntPrint(fontID, "     CONTROLS\n");
    drawSeparator();
    FntPrint(fontID, "\n");
    FntPrint(fontID, " START      Pause / Resume\n");
    FntPrint(fontID, " X          Next Scene\n");
    FntPrint(fontID, " CIRCLE     Max Speed\n");
    FntPrint(fontID, " TRIANGLE   Frame Advance\n");
    FntPrint(fontID, " SELECT     Quit\n");
    FntPrint(fontID, "\n");
    FntPrint(fontID, " (START to go back)\n");
}

/* ---------------------------------------------------------------------------
 *  Sub-screen: Set Time/Date
 * ------------------------------------------------------------------------- */
static void drawSetTime(void)
{
    const char *fieldNames[] = {"Month","Day","Year","Hour","Min"};

    FntPrint(fontID, "\n");
    drawSeparator();
    FntPrint(fontID, "   SET TIME AND DATE\n");
    drawSeparator();
    FntPrint(fontID, "\n");

    /* Month */
    FntPrint(fontID, " %s Month: %s%02d%s\n",
             editField == 0 ? ">" : " ",
             editField == 0 ? "[" : " ",
             editMonth,
             editField == 0 ? "]" : " ");

    /* Day */
    FntPrint(fontID, " %s Day:   %s%02d%s\n",
             editField == 1 ? ">" : " ",
             editField == 1 ? "[" : " ",
             editDay,
             editField == 1 ? "]" : " ");

    /* Year */
    FntPrint(fontID, " %s Year:  %s%04d%s\n",
             editField == 2 ? ">" : " ",
             editField == 2 ? "[" : " ",
             editYear,
             editField == 2 ? "]" : " ");

    /* Hour */
    FntPrint(fontID, " %s Hour:  %s%02d%s\n",
             editField == 3 ? ">" : " ",
             editField == 3 ? "[" : " ",
             editHour,
             editField == 3 ? "]" : " ");

    /* Minute */
    FntPrint(fontID, " %s Min:   %s%02d%s\n",
             editField == 4 ? ">" : " ",
             editField == 4 ? "[" : " ",
             editMinute,
             editField == 4 ? "]" : " ");

    FntPrint(fontID, "\n");
    FntPrint(fontID, " UP/DOWN select field\n");
    FntPrint(fontID, " LEFT/RIGHT adjust value\n");
    FntPrint(fontID, " X to confirm, START back\n");

    (void)fieldNames;
}

/* ---------------------------------------------------------------------------
 *  Main menu drawing
 * ------------------------------------------------------------------------- */
static void drawMainMenu(void)
{
    const char *soundLabel   = soundDisabled ? "OFF" : "ON";
    const char *telLabel     = grPs1TelemetryEnabled ? "ON" : "OFF";

    FntPrint(fontID, "\n");
    drawSeparator();
    FntPrint(fontID, "    JOHNNY CASTAWAY\n");
    FntPrint(fontID, "      - PAUSED -\n");
    drawSeparator();
    FntPrint(fontID, "\n");

    /* Menu items -- cursor arrow on selected line */
    FntPrint(fontID, " %s Resume\n",
             menuCursor == MENU_RESUME ? ">" : " ");
    FntPrint(fontID, " %s Sound: %s\n",
             menuCursor == MENU_SOUND ? ">" : " ", soundLabel);
    FntPrint(fontID, " %s Telemetry: %s\n",
             menuCursor == MENU_TELEMETRY ? ">" : " ", telLabel);
    FntPrint(fontID, " %s Captions: (soon)\n",
             menuCursor == MENU_CAPTIONS ? ">" : " ");
    FntPrint(fontID, " %s Scene Order: (soon)\n",
             menuCursor == MENU_SCENE_ORDER ? ">" : " ");
    FntPrint(fontID, " %s Direct Control: (soon)\n",
             menuCursor == MENU_DIRECT_CONTROL ? ">" : " ");
    FntPrint(fontID, " %s Next Scene\n",
             menuCursor == MENU_NEXT_SCENE ? ">" : " ");
    FntPrint(fontID, " %s Set Time/Date\n",
             menuCursor == MENU_SET_TIME ? ">" : " ");
    FntPrint(fontID, " %s Scene Info\n",
             menuCursor == MENU_SCENE_INFO ? ">" : " ");
    FntPrint(fontID, " %s Controls\n",
             menuCursor == MENU_CONTROLS ? ">" : " ");

    drawSeparator();
}

/* ---------------------------------------------------------------------------
 *  Input handling per sub-screen
 * ------------------------------------------------------------------------- */

/* Handle main menu input.  Returns 0 if user chose Resume. */
static int handleMainInput(uint16 pressed)
{
    if (pressed & PAD_UP) {
        menuCursor--;
        if (menuCursor < 0) menuCursor = MENU_COUNT - 1;
    }
    if (pressed & PAD_DOWN) {
        menuCursor++;
        if (menuCursor >= MENU_COUNT) menuCursor = 0;
    }

    /* X = select current item */
    if (pressed & PAD_CROSS) {
        switch (menuCursor) {
        case MENU_RESUME:
            return 0;  /* close menu */

        case MENU_SOUND:
            soundDisabled = !soundDisabled;
            break;

        case MENU_TELEMETRY:
            grSetPs1Telemetry(!grPs1TelemetryEnabled);
            break;

        case MENU_NEXT_SCENE:
            pauseMenuRequestNextScene = 1;
            return 0;  /* close menu to let scene advance */

        case MENU_SET_TIME:
            menuState = PAUSE_MENU_SET_TIME;
            editField = 0;
            prevButtons = 0xFFFF;
            break;

        case MENU_SCENE_INFO:
            menuState = PAUSE_MENU_SCENE_INFO;
            prevButtons = 0xFFFF;
            break;

        case MENU_CONTROLS:
            menuState = PAUSE_MENU_CONTROLS;
            prevButtons = 0xFFFF;
            break;

        /* Stubs for unimplemented features */
        case MENU_CAPTIONS:
        case MENU_SCENE_ORDER:
        case MENU_DIRECT_CONTROL:
        default:
            break;
        }
    }

    /* START on main menu = resume */
    if (pressed & PAD_START)
        return 0;

    return 1;  /* keep menu open */
}

/* Returns 0 if user goes back to main. */
static int handleSubInput(uint16 pressed)
{
    if (pressed & PAD_START) {
        menuState = PAUSE_MENU_MAIN;
        menuCursor = 0;
        prevButtons = 0xFFFF;
        return 1;
    }
    return 1;
}

/* Set Time input -- LEFT/RIGHT adjust value, UP/DOWN change field. */
static int handleSetTimeInput(uint16 pressed)
{
    if (pressed & PAD_START) {
        menuState = PAUSE_MENU_MAIN;
        menuCursor = 0;
        prevButtons = 0xFFFF;
        return 1;
    }

    if (pressed & PAD_UP) {
        editField--;
        if (editField < 0) editField = 4;
    }
    if (pressed & PAD_DOWN) {
        editField++;
        if (editField > 4) editField = 0;
    }

    int delta = 0;
    if (pressed & PAD_RIGHT) delta = 1;
    if (pressed & PAD_LEFT)  delta = -1;

    if (delta) {
        switch (editField) {
        case 0: editMonth  = clampInt(editMonth + delta, 1, 12); break;
        case 1: editDay    = clampInt(editDay + delta, 1, daysInMonth(editMonth, editYear)); break;
        case 2: editYear   = clampInt(editYear + delta, 1990, 2099); break;
        case 3: editHour   = clampInt(editHour + delta, 0, 23); break;
        case 4: editMinute = clampInt(editMinute + delta, 0, 59); break;
        }
    }

    /* X = confirm -- write values into the PS1 time stubs.
     * The game reads time via getHour()/getMonthAndDay()/getDayOfYear() in
     * utils.c, which return hard-coded values on PS1.  We update those stubs
     * through extern globals exposed specifically for the pause menu. */
    if (pressed & PAD_CROSS) {
        /* Export to the global overrides (defined in utils.c PS1 section). */
        extern int ps1SoftHour;
        extern int ps1SoftMonth;
        extern int ps1SoftDay;

        ps1SoftHour  = editHour;
        ps1SoftMonth = editMonth;
        ps1SoftDay   = editDay;
        /* getDayOfYear() in utils.c computes from ps1SoftMonth/ps1SoftDay */

        /* Return to main menu after confirming. */
        menuState = PAUSE_MENU_MAIN;
        menuCursor = 0;
        prevButtons = 0xFFFF;
    }

    return 1;
}

/* ---------------------------------------------------------------------------
 *  pauseMenuUpdate -- one frame of the overlay
 *
 *  Called in a tight VSync loop while paused.  Reads pad, updates state,
 *  draws text, uploads background.  Returns 1 to stay paused, 0 to resume.
 * ------------------------------------------------------------------------- */
int pauseMenuUpdate(void)
{
    if (!menuVisible) return 0;

    /* Dim background on first frame of pause. */
    dimBackground();

    /* Upload (dimmed) background so text draws on top of a visible scene. */
    grDrawBackground();
    DrawSync(0);

    /* Read pad through the game's shared pad buffer (events_ps1.c owns
     * InitPAD, so we just peek at its buffer via the extern). */
    extern uint8 pad_buff[2][34];
    uint16 cur = 0;
    {
        PADTYPE *pad = (PADTYPE *)pad_buff[0];
        if (pad->stat == 0)
            cur = ~(pad->btn);
    }
    uint16 pressed = pmNewPress(cur);
    prevButtons = cur;

    /* Dispatch to current sub-screen input handler. */
    int keepOpen = 1;
    switch (menuState) {
    case PAUSE_MENU_MAIN:
        keepOpen = handleMainInput(pressed);
        break;
    case PAUSE_MENU_SET_TIME:
        keepOpen = handleSetTimeInput(pressed);
        break;
    case PAUSE_MENU_SCENE_INFO:
    case PAUSE_MENU_CONTROLS:
        keepOpen = handleSubInput(pressed);
        break;
    }

    if (!keepOpen) {
        menuVisible = 0;
        bgDimmed = 0;
        return 0;
    }

    /* Draw the appropriate screen. */
    switch (menuState) {
    case PAUSE_MENU_MAIN:       drawMainMenu();  break;
    case PAUSE_MENU_SCENE_INFO: drawSceneInfo(); break;
    case PAUSE_MENU_CONTROLS:   drawControls();  break;
    case PAUSE_MENU_SET_TIME:   drawSetTime();   break;
    }

    /* Flush the font stream to GPU -- renders text primitives this frame. */
    FntFlush(fontID);

    /* VSync to pace at 60 fps while paused. */
    VSync(0);

    return 1;
}
