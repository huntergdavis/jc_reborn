/*******************************************************************************
	Sega Dreamcast Project

	Project name : johnny_dreamcastaway
	Created on   : 2021-03-20
*******************************************************************************/

#define PROJECT_NAME "johnny_dreamcastaway"

#include <kos.h>

// kosext2fs
#include <ext2/fs_ext2.h>

// kosutils
#include <kos/bspline.h>
#include <kos/img.h>
#include <kos/md5.h>
#include <kos/netcfg.h>
#include <kos/pcx.h>
#include <kos/vector.h>

// libbz2
#include <bzlib/bzlib.h>

// libimageload
#include <imageload/imageload.h>
#include <imageload/jitterdefs.h>

// math
#include <math.h>

// SDL
#include <SDL/SDL.h>

// zlib
#include <zlib/zlib.h>

// jc_reborn

#include "mytypes.h"
#include "utils.h"
#include "resource.h"
#include "dump.h"
#include "graphics.h"
#include "events.h"
#include "sound.h"
#include "ttm.h"
#include "ads.h"
#include "story.h"


#ifdef DEBUG
#include <arch/gdb.h>
#endif

/* These macros tell KOS how to initialize itself. All of this initialization
   happens before main() gets called, and the shutdown happens afterwards. So
   you need to set any flags you want here. Here are some possibilities:

   INIT_NONE         -- don't do any auto init
   INIT_IRQ          -- Enable IRQs
   INIT_THD_PREEMPT  -- Enable pre-emptive threading
   INIT_NET          -- Enable networking (doesn't imply lwIP!)
   INIT_MALLOCSTATS  -- Enable a call to malloc_stats() right before shutdown

   You can OR any or all of those together. If you want to start out with
   the current KOS defaults, use INIT_DEFAULT (or leave it out entirely). */
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_MALLOCSTATS);

/* Declaration of the romdisk
   You can access the files inside it by using the "/rd" mounting point. */
extern uint8 romdisk[];
KOS_INIT_ROMDISK(romdisk);

/* Your program's main entry point */
int main(int argc, char *argv[]) {
#ifdef DEBUG
	/* This is needed for the Debug target.
	   Please don't remove this part of code if you want to use the Code::Blocks debugger.
	   Also, you'll need to configure Dreamcast Tool (dc-tool) from the DreamSDK Manager. */
	gdb_init();
	printf("Connection established to %s!", PROJECT_NAME);
#endif

    /* Your program start here... */
    parseResourceFiles("/rd/RESOURCE.MAP");


    graphicsInit();
    soundInit();

    storyPlay();

    soundEnd();
    graphicsEnd();


	/* Bye... */
    return 0;
}
