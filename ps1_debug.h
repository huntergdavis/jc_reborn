/*
 *  This file is part of 'Johnny Reborn' - PS1 Port
 *
 *  Visual debugging system for PS1
 *  Displays text on-screen since printf() doesn't work in DuckStation
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef PS1_DEBUG_H
#define PS1_DEBUG_H

#include <sys/types.h>

/*
 * Initialize visual debugging system
 * Sets up PSn00bSDK font rendering
 */
void ps1DebugInit(void);

/*
 * Clear the debug text buffer
 */
void ps1DebugClear(void);

/*
 * Print a line of text to the debug display
 * Similar to printf but renders to screen
 */
void ps1DebugPrint(const char *fmt, ...);

/*
 * Update the screen with current debug text
 * Call this after printing all your debug messages
 */
void ps1DebugFlush(void);

/*
 * Wait for user input (SELECT button) before continuing
 * Useful for pausing at error points
 */
void ps1DebugWait(void);

/*
 * Show an error screen with text and wait
 * Combines print, flush, and wait
 */
void ps1DebugError(const char *fmt, ...);

#endif /* PS1_DEBUG_H */
