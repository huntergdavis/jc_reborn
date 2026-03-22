/*
 *  Closed captions for PS1 accessibility.
 *
 *  Caption text extracted from the closed_captions branch.
 *  Each scene has descriptive text explaining what is
 *  happening on screen, displayed as subtitles.
 *
 *  Text is condensed to fit PS1 screen constraints:
 *  max ~35 characters per line for readability at 320px.
 */

#ifndef PS1_CAPTIONS_H
#define PS1_CAPTIONS_H

#include "mytypes.h"

/* ------------------------------------------------------------------ */
/*  Caption data structures                                           */
/* ------------------------------------------------------------------ */

struct TCaption {
    const char *scene_id;   /* e.g. "scene01", "intro" */
    const char *text;       /* Multi-line caption text  */
};

/* Scene-to-ADS mapping for caption lookup by ADS name + tag. */
struct TCaptionSceneMap {
    const char *caption_id; /* "scene00" etc.       */
    const char *ads_name;   /* "ACTIVITY" etc.      */
    uint16      ads_tag;    /* ADS tag number       */
};


/* ------------------------------------------------------------------ */
/*  Caption text — condensed for PS1 (max ~35 chars/line)             */
/* ------------------------------------------------------------------ */

static const struct TCaption captions[] = {

    /* --- Special / environmental captions --- */

    {"intro",
        "This is Johnny Castaway.\n"
        "Stranded on a tiny island\n"
        "with one palm tree.\n"
        "He wears white shorts and a hat."},

    {"christmas",
        "It is Christmas.\n"
        "A small tree with red bulbs\n"
        "and a golden star."},

    {"halloween",
        "It is Halloween.\n"
        "A carved jack-o-lantern\n"
        "sits on the island."},

    {"newyears",
        "It is New Years.\n"
        "A banner reads Happy New Year\n"
        "on the palm tree."},

    {"stpatrick",
        "It is St Patrick's Day.\n"
        "Four-leaf clovers grow\n"
        "on the island."},

    {"night",
        "It is night.\n"
        "The island is bathed\n"
        "in moonlight."},

    {"day",
        "It is day.\n"
        "The sun shines brightly."},

    {"regularday",
        "It is a regular day."},

    {"hightide",
        "It is high tide.\n"
        "Waves lap at the island."},

    {"lowtide",
        "It is low tide.\n"
        "Waves lap at the island."},

    {"fadeout",
        "The scene fades to black."},

    {"walking",
        "Johnny walks around\n"
        "the island."},

    /* --- Numbered scenes (match storyScenes[] indices) --- */

    {"scene00",
        "Johnny dives off the palm tree.\n"
        "A perfect flip into the ocean.\n"
        "Crabs and seagull hold up\n"
        "low scorecards."},

    {"scene01",
        "Johnny dives off the palm tree.\n"
        "It turns into a belly-flop.\n"
        "Crabs and seagull hold up\n"
        "low scorecards."},

    {"scene02",
        "Johnny reads under the tree.\n"
        "A seagull lands on his head.\n"
        "He swings a club but misses\n"
        "and hits himself."},

    {"scene03",
        "Johnny bathes in the ocean.\n"
        "A seagull steals his clothes\n"
        "for its nest.\n"
        "Johnny shivers angrily."},

    {"scene04",
        "Johnny reads under the tree.\n"
        "A seagull swoops down\n"
        "and steals his book."},

    {"scene05",
        "Johnny climbs the palm tree.\n"
        "He looks around, then dives.\n"
        "He walks back and looks around."},

    {"scene06",
        "Johnny fans himself in heat.\n"
        "He does a rain dance in a mask.\n"
        "A cloud appears but no rain.\n"
        "Lightning strikes him to ash."},

    {"scene07",
        "Johnny reads under the tree.\n"
        "He falls asleep.\n"
        "A coconut bonks his head.\n"
        "He wakes and keeps reading."},

    {"scene08",
        "Johnny reads under the tree.\n"
        "He scratches his head confused.\n"
        "The book was upside down.\n"
        "He flips it and reads on."},

    {"scene09",
        "Johnny bathes in the ocean.\n"
        "He scrubs, smells the brush\n"
        "in disgust, grabs his clothes\n"
        "and walks behind the tree."},

    {"scene10",
        "Johnny wears a mask and skirt.\n"
        "A yacht couple takes photos.\n"
        "His grass skirt falls open.\n"
        "The yacht sails away."},

    {"scene11",
        "Johnny builds a sand castle.\n"
        "It crumbles.\n"
        "He stomps it in frustration."},

    {"scene12",
        "Johnny sleeps under the tree.\n"
        "Lilliputians row ashore\n"
        "and tie him down.\n"
        "A seagull nests on him."},

    {"scene13",
        "Johnny sleeps under the tree.\n"
        "Zs float as he snores.\n"
        "He walks to the island edge."},

    {"scene14",
        "Johnny builds a sand castle.\n"
        "Lilliputians claim it as\n"
        "their fortress.\n"
        "Tiny planes attack Johnny."},

    {"scene15",
        "Johnny tries to build a fire.\n"
        "He rubs sticks together.\n"
        "It finally lights!\n"
        "He warms his hands, it dies."},

    {"scene16",
        "Johnny relaxes by a fire.\n"
        "He roasts an old boot.\n"
        "He eats the boot whole."},

    {"scene17",
        "Johnny sleeps under the tree.\n"
        "Lilliputians row ashore\n"
        "and tie him down.\n"
        "He goes back to sleep."},

    {"scene18",
        "Johnny goes fishing.\n"
        "He catches a starfish.\n"
        "He throws it back."},

    {"scene19",
        "Johnny goes fishing.\n"
        "He catches a boot.\n"
        "He keeps the boot."},

    {"scene20",
        "Johnny goes fishing.\n"
        "He catches five green fish.\n"
        "Then an angry octopus.\n"
        "The octopus chokes him."},

    {"scene21",
        "Johnny goes fishing.\n"
        "He catches a shark.\n"
        "The shark drags him around\n"
        "the ocean like a jet-ski."},

    {"scene22",
        "Johnny goes fishing.\n"
        "A shark eats him.\n"
        "The shark spits him back out."},

    {"scene23",
        "Johnny goes fishing.\n"
        "He catches a big green fish.\n"
        "It spits water in his face."},

    {"scene24",
        "Johnny goes fishing.\n"
        "He catches a crab.\n"
        "It snaps his nose."},

    {"scene25",
        "Johnny goes fishing.\n"
        "He catches a boot.\n"
        "He keeps the boot."},

    {"scene26",
        "A clock spins wildly.\n"
        "Sunset silhouette. A plane.\n"
        "Johnny parachutes down.\n"
        "The End."},

    {"scene27",
        "A bottle washes ashore.\n"
        "Johnny writes an S.O.S.\n"
        "He corks the bottle\n"
        "and throws it out to sea."},

    {"scene28",
        "Johnny writes a message.\n"
        "He imagines a clock at 3pm.\n"
        "He throws the bottle out\n"
        "to prepare for his date."},

    {"scene29",
        "A bottle washes ashore.\n"
        "Johnny picks it up excitedly.\n"
        "Sadly, it is his own S.O.S.\n"
        "He throws it back out."},

    {"scene30",
        "Johnny writes an S.O.S.\n"
        "He corks the bottle\n"
        "and throws it out to sea."},

    {"scene31",
        "A clock spins wildly.\n"
        "Johnny types at an office PC.\n"
        "He dreams of the island\n"
        "and the mermaid. He looks sad."},

    {"scene32",
        "Johnny sets up a fancy dinner.\n"
        "A mermaid appears.\n"
        "They eat, toast champagne,\n"
        "and dance. She swims away."},

    {"scene33",
        "A mermaid swims up.\n"
        "She gives Johnny a necklace.\n"
        "He gives her a life preserver.\n"
        "He proposes a date."},

    {"scene34",
        "Johnny fishes at the edge.\n"
        "A mermaid swims up behind him.\n"
        "He thinks it is a fish."},

    {"scene35",
        "Johnny fixes his raft.\n"
        "The mermaid asks what he does.\n"
        "He says he is leaving.\n"
        "She is heartbroken."},

    {"scene36",
        "Johnny packs his bags.\n"
        "The mermaid and shark say bye.\n"
        "The shark shakes his hand.\n"
        "Johnny paddles away."},

    {"scene37",
        "Johnny fans himself in heat.\n"
        "He fans harder and harder.\n"
        "He melts into a puddle."},

    {"scene38",
        "Johnny goes to swim.\n"
        "He dips a toe in the ocean.\n"
        "A shark snaps at him.\n"
        "He scrambles back to shore."},

    {"scene39",
        "Johnny stands at the edge.\n"
        "He taps his foot nervously."},

    {"scene40",
        "Johnny adjusts his pants."},

    {"scene41",
        "Johnny looks over the ocean.\n"
        "He adjusts his hat and pants."},

    {"scene42",
        "Johnny taps his foot."},

    {"scene43",
        "Johnny lifts his hat\n"
        "and looks around."},

    {"scene44",
        "Johnny taps his foot.\n"
        "He lifts his hat\n"
        "and looks around."},

    {"scene45",
        "Johnny taps his foot.\n"
        "He looks back into\n"
        "the distance."},

    {"scene46",
        "Johnny lifts his hat."},

    {"scene47",
        "Johnny taps his foot.\n"
        "He looks at the palm tree."},

    {"scene48",
        "Johnny looks at his raft."},

    {"scene49",
        "Johnny looks over the ocean."},

    {"scene50",
        "Johnny looks around\n"
        "under the palm tree shade."},

    {"scene51",
        "Johnny pulls out a spyglass\n"
        "and scans the horizon."},

    {"scene52",
        "Johnny pulls out a spyglass\n"
        "and scans the horizon."},

    {"scene53",
        "A frog clock spins wildly.\n"
        "A redhead finds the bottle.\n"
        "She imagines a volcano island\n"
        "and a handsome man."},

    {"scene54",
        "A frog clock spins wildly.\n"
        "Johnny's raft reaches her.\n"
        "She kisses him passionately,\n"
        "then scolds him."},

    {"scene55",
        "Johnny scans with a spyglass.\n"
        "A plane flies overhead.\n"
        "He looks the wrong way\n"
        "and misses it entirely."},

    {"scene56",
        "A red boat spots Johnny.\n"
        "He waves excitedly.\n"
        "The boat is enormous,\n"
        "it fills the whole screen."},

    {"scene57",
        "Johnny shakes the palm tree.\n"
        "A coconut bonks his head\n"
        "and flies into the ocean."},

    {"scene58",
        "Johnny shakes the palm tree.\n"
        "A coconut falls down.\n"
        "He chases and catches it.\n"
        "He cracks and eats it."},

    {"scene59",
        "Johnny shakes the palm tree.\n"
        "A coconut falls down.\n"
        "He cracks it on the tree\n"
        "and eats it."},

    {"scene60",
        ""},  /* empty in source data */

    {"scene61",
        "A boat with partygoers sails up.\n"
        "Johnny swims to the boat.\n"
        "He returns very drunk,\n"
        "wearing a party hat."},

    {"scene62",
        "Johnny builds up his raft."},

    {"scene63",
        "Johnny jogs around the island\n"
        "in a grey jogging outfit.\n"
        "He changes back to normal."},

    {NULL, NULL}  /* sentinel */
};


/* ------------------------------------------------------------------ */
/*  Scene-to-ADS mapping                                              */
/*  Maps caption scene IDs to ADS file names and tag numbers.         */
/*  Follows the order of storyScenes[] in story_data.h.               */
/* ------------------------------------------------------------------ */

static const struct TCaptionSceneMap captionSceneMap[] = {
    /* ACTIVITY.ADS scenes */
    {"scene00", "ACTIVITY",  1},
    {"scene01", "ACTIVITY", 12},
    {"scene02", "ACTIVITY", 11},
    {"scene03", "ACTIVITY", 10},
    {"scene04", "ACTIVITY",  4},
    {"scene05", "ACTIVITY",  5},
    {"scene06", "ACTIVITY",  6},
    {"scene07", "ACTIVITY",  7},
    {"scene08", "ACTIVITY",  8},
    {"scene09", "ACTIVITY",  9},

    /* BUILDING.ADS scenes */
    {"scene10", "BUILDING",  1},
    {"scene11", "BUILDING",  4},
    {"scene12", "BUILDING",  3},
    {"scene13", "BUILDING",  2},
    {"scene14", "BUILDING",  5},
    {"scene15", "BUILDING",  7},
    {"scene16", "BUILDING",  6},

    /* FISHING.ADS scenes */
    {"scene17", "FISHING",   1},
    {"scene18", "FISHING",   2},
    {"scene19", "FISHING",   3},
    {"scene20", "FISHING",   4},
    {"scene21", "FISHING",   5},
    {"scene22", "FISHING",   6},
    {"scene23", "FISHING",   7},
    {"scene24", "FISHING",   8},

    /* JOHNNY.ADS scenes */
    {"scene25", "JOHNNY",    1},
    {"scene26", "JOHNNY",    2},
    {"scene27", "JOHNNY",    3},
    {"scene28", "JOHNNY",    4},
    {"scene29", "JOHNNY",    5},
    {"scene30", "JOHNNY",    6},

    /* MARY.ADS scenes */
    {"scene31", "MARY",      1},
    {"scene32", "MARY",      3},
    {"scene33", "MARY",      2},
    {"scene34", "MARY",      4},
    {"scene35", "MARY",      5},

    /* MISCGAG.ADS scenes */
    {"scene36", "MISCGAG",   1},
    {"scene37", "MISCGAG",   2},

    /* STAND.ADS scenes */
    {"scene38", "STAND",     1},
    {"scene39", "STAND",     2},
    {"scene40", "STAND",     3},
    {"scene41", "STAND",     4},
    {"scene42", "STAND",     5},
    {"scene43", "STAND",     6},
    {"scene44", "STAND",     7},
    {"scene45", "STAND",     8},
    {"scene46", "STAND",     9},
    {"scene47", "STAND",    10},
    {"scene48", "STAND",    11},
    {"scene49", "STAND",    12},
    {"scene50", "STAND",    15},
    {"scene51", "STAND",    16},

    /* SUZY.ADS scenes */
    {"scene52", "SUZY",      1},
    {"scene53", "SUZY",      2},

    /* VISITOR.ADS scenes */
    {"scene54", "VISITOR",   1},
    {"scene55", "VISITOR",   3},
    {"scene56", "VISITOR",   4},
    {"scene57", "VISITOR",   6},
    {"scene58", "VISITOR",   7},
    {"scene59", "VISITOR",   5},

    /* WALKSTUF.ADS scenes */
    {"scene60", "WALKSTUF",  1},
    {"scene61", "WALKSTUF",  2},
    {"scene62", "WALKSTUF",  3},

    {NULL, NULL, 0}  /* sentinel */
};

#define NUM_CAPTION_SCENE_MAP  63


/* ------------------------------------------------------------------ */
/*  Caption system API                                                */
/* ------------------------------------------------------------------ */

void captionsSetEnabled(int enabled);
int  captionsGetEnabled(void);

/* Call when a scene starts — looks up caption by scene ID. */
void captionsOnSceneStart(const char *sceneId);

/* Call when an ADS scene starts — looks up caption by ADS name + tag. */
void captionsOnAdsStart(const char *adsName, uint16 adsTag);

/* Call each frame. Returns current caption text, or NULL if none. */
const char *captionsGetCurrent(void);

#endif /* PS1_CAPTIONS_H */
