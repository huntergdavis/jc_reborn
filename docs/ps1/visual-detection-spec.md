# Visual Detection Spec: Screenshot-Based Scene and Sprite Identification

Date: 2026-03-22

## 1. The Palette

Johnny Castaway uses a single 16-color palette stored in `JOHNCAST.PAL`. There is only
one PAL resource in the entire game -- the palette does **not** change between scenes.
Every SCR background and every BMP sprite shares the same 16-entry CLUT.

### 1.1 Full Palette Table

| Index | VGA 6-bit (R,G,B) | 8-bit (R,G,B)     | PS1 15-bit | HTML Hex  | Visual Description         |
|-------|--------------------|--------------------|------------|-----------|----------------------------|
| 0     | (42, 0, 42)        | (168, 0, 168)      | 0x0000     | #a800a8   | **Transparent** (magenta)  |
| 1     | ( 0, 0, 42)        | (  0, 0, 168)      | 0x5400     | #0000a8   | Dark Blue                  |
| 2     | ( 0, 42, 0)        | (  0, 168, 0)      | 0x02A0     | #00a800   | Dark Green                 |
| 3     | ( 0, 42, 42)       | (  0, 168, 168)    | 0x56A0     | #00a8a8   | Dark Cyan                  |
| 4     | (42, 0, 0)         | (168, 0, 0)        | 0x0015     | #a80000   | Dark Red                   |
| 5     | ( 0, 0, 0)         | (  0, 0, 0)        | 0x0001     | #000000   | **Black** (0x0001, not 0x0000) |
| 6     | (42, 42, 0)        | (168, 168, 0)      | 0x02B5     | #a8a800   | Dark Yellow / Olive        |
| 7     | (53, 53, 53)       | (212, 212, 212)    | 0x6B5A     | #d4d4d4   | Light Gray                 |
| 8     | (32, 32, 32)       | (128, 128, 128)    | 0x4210     | #808080   | Medium Gray                |
| 9     | ( 0, 0, 63)        | (  0, 0, 252)      | 0x7C00     | #0000fc   | **Bright Blue**            |
| 10    | ( 0, 63, 0)        | (  0, 252, 0)      | 0x03E0     | #00fc00   | Bright Green               |
| 11    | ( 0, 63, 63)       | (  0, 252, 252)    | 0x7FE0     | #00fcfc   | **Bright Cyan**            |
| 12    | (63, 0, 0)         | (252, 0, 0)        | 0x001F     | #fc0000   | Bright Red                 |
| 13    | (63, 0, 63)        | (252, 0, 252)      | 0x7C1F     | #fc00fc   | Bright Magenta (not used as transparent) |
| 14    | (63, 63, 0)        | (252, 252, 0)      | 0x03FF     | #fcfc00   | **Bright Yellow**          |
| 15    | (63, 63, 63)       | (252, 252, 252)    | 0x7FFF     | #fcfcfc   | **White**                  |

### 1.2 Palette Notes

- Index 0 is the transparency key (VGA magenta 168,0,168). On PS1 it maps to 0x0000.
  Transparent pixels are never drawn; they appear as whatever background was underneath.
- Index 5 (true black) is stored as PS1 0x0001 (very dark blue, perceptually black)
  because 0x0000 is reserved for the transparency semaphore.
- The palette is loaded once at startup from `palResources[0]` via `grLoadPalette()`.
  It never changes for the lifetime of the program.
- Only 16 colors are used (indices 0-15). Entries 16-255 in the PAL file are all zero.
- This is the standard EGA/VGA 16-color CGA palette: the same 16 colors that DOS
  screensavers of the era used.

### 1.3 Color Semantic Mapping

Based on the SCR and BMP analysis:

| Palette Index | Primary Use                                              |
|---------------|----------------------------------------------------------|
| 0  (transparent) | Sprite transparency, some ocean whitecaps/foam          |
| 1  (dark blue)   | Deep water, night sky, dark ocean areas                 |
| 2  (dark green)  | Palm tree foliage (dark parts), some vegetation         |
| 3  (dark cyan)   | Water mid-tones, island shoreline edges                 |
| 4  (dark red)    | Johnny's shirt (dark parts), raft wood                  |
| 5  (black)       | Outlines, shadows, dark areas, night sky base           |
| 6  (olive)       | Sand (dark), tree trunk, raft wood, ground details      |
| 7  (light gray)  | Clouds, wave foam highlights, office walls              |
| 8  (med gray)    | Rock/cliff surfaces, office details, wave mid-tones     |
| 9  (bright blue) | **Sky** -- dominates top half of ocean/island scenes    |
| 10 (bright green)| Palm tree leaves (bright), some vegetation highlights   |
| 11 (bright cyan) | **Ocean water** -- dominates bottom half, also sky      |
| 12 (bright red)  | Johnny's shirt (bright), fire, blood/danger elements    |
| 13 (bright magenta) | Very rare in backgrounds; some sprite accents        |
| 14 (bright yellow) | **Sand** -- island beach, also sun/highlights         |
| 15 (white)       | Wave crests, cloud highlights, text, bright accents     |


## 2. Scene Visual Signatures -- Background (SCR) Analysis

### 2.1 SCR File Inventory

| SCR File       | Dimensions | Size     | Primary Colors                        | Description                  |
|----------------|------------|----------|---------------------------------------|------------------------------|
| OCEAN00.SCR    | 640x480    | 153600   | 9(41%), 11(34%), 5(10%), 1(6%)       | Full ocean, no island        |
| OCEAN01.SCR    | 640x480    | 153600   | 9(42%), 11(34%), 5(10%), 1(4%)       | Full ocean variant 2         |
| OCEAN02.SCR    | 640x480    | 153600   | 9(43%), 11(33%), 5(12%), 1(4%)       | Full ocean variant 3         |
| ISLETEMP.SCR   | 640x350    | 112000   | 9(37%), 11(35%), 1(6%), 3(5%)        | Island template (partial h)  |
| ISLAND2.SCR    | 640x350    | 112000   | 9(41%), 11(36%), 15(9%), 3(4%)       | Island variant 2             |
| NIGHT.SCR      | 640x480    | 153600   | 1(69%), 5(27%), 15(3%), 7(2%)        | Night sky + dark ocean       |
| INTRO.SCR      | 640x480    | 153600   | 5(70%), 11(11%), 9(8%), 8(2%)        | Title/intro -- mostly black  |
| SUZBEACH.SCR   | 640x480    | 153600   | 5(68%), 11(15%), 9(10%), 8(2%)       | Suzy's beach (mostly dark)   |
| JOFFICE.SCR    | 640x350    | 112000   | 5(82%), 8(9%), 7(7%), 15(2%)         | Johnny's office (dark)       |
| THEEND.SCR     | 640x350    | 112000   | 5(77%), 12(10%), 1(7%), 14(3%)       | "The End" screen             |

### 2.2 SCR Visual Signatures

**OCEAN scenes** (OCEAN00/01/02): Blue-dominated. ~41% bright blue (index 9) + ~34% bright cyan
(index 11) = 75%+ blue/cyan. ~10% black (index 5). Very little yellow (index 14) or green.
The three variants differ slightly in wave patterns but have nearly identical color distributions.

**ISLETEMP.SCR** (island template): Similar to OCEAN but only 640x350 (partial height).
The bottom 130 lines are filled in from the previously loaded ocean SCR. Contains slightly
more dark blue (index 1, 6%) and dark cyan (index 3, 5%) than pure ocean, representing
the island silhouette and shoreline. More white (9%) for wave foam around the island.

**ISLAND2.SCR**: Very similar to ISLETEMP. Slightly more white (9% vs 9%) and slightly
more bright blue (41% vs 37%). Different island sprite arrangement but same overall
color profile.

**NIGHT.SCR**: Instantly recognizable -- dominated by dark blue (69%) and black (27%).
Almost no bright colors. Only white (3%) and light gray (2%) break the darkness.
No bright blue (index 9), no bright cyan (index 11), no yellow.

**INTRO.SCR**: Dominated by black (70%). Has bright cyan (11%) and bright blue (8%)
for the Sierra logo / "Screen Antics!" text. Contains yellow (2%) and white (2%)
for text highlights. Unlike other dark screens, has some of every color.

**SUZBEACH.SCR**: Suzy's beach scene. 68% black (background), with modest ocean colors
(15% bright cyan, 10% bright blue). Some yellow (1%) for beach sand. Darker overall
than the main island.

**JOFFICE.SCR**: Johnny's office. 82% black -- the darkest SCR. Has gray tones
(9% medium gray, 7% light gray) for office furniture/walls. Almost no color.

**THEEND.SCR**: 77% black. Unique color signature: 10% bright red (index 12) --
no other SCR has significant red. Also 7% dark blue, 3% yellow. The red makes
this trivially detectable.

### 2.3 Key Discriminators Between SCR Types

| Detection Question                          | Method                                      |
|---------------------------------------------|---------------------------------------------|
| Is it an ocean/island scene?                | Bright blue (9) + bright cyan (11) > 50%    |
| Pure ocean vs island?                       | Island has yellow (14) > 2% in lower half   |
| Night scene?                                | Dark blue (1) > 50%, bright blue (9) < 5%   |
| Intro/title screen?                         | Black (5) > 60% AND bright cyan (11) 5-15%  |
| Suzy's beach?                               | Black (5) > 60% AND bright cyan (11) 10-20% |
| Johnny's office?                            | Black (5) > 75% AND medium gray (8) > 5%    |
| The End screen?                             | Bright red (12) > 5%                         |


## 3. Sprite Inventory

### 3.1 Complete BMP Listing by Category

#### Johnny Walking / Jogging (movement sprites)
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| JOHNWALK.BMP   | 48,476      | Johnny walking (standard, all directions)|
| MJJOG1.BMP     | 29,968      | Johnny jogging variant 1                 |
| MJJOG2.BMP     | 36,852      | Johnny jogging variant 2                 |
| MEXCWALK.BMP   | 11,716      | Excited walking                          |
| JCHANGE.BMP    | 7,400       | Johnny changing direction                |
| DRUNKJON.BMP   | 41,068      | Drunk Johnny walking                     |

#### Johnny Standing / Activities (island life)
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| MJ_AMB.BMP     | 27,484      | Johnny ambient/idle                      |
| MJTELE.BMP     | 23,824      | Johnny looking through telescope         |
| MJTELE2.BMP    | 7,376       | Telescope variant 2                      |
| MJREAD.BMP     | 45,272      | Johnny reading                           |
| MJBATH.BMP     | 53,664      | Johnny bathing                           |
| MJDIVE.BMP     | 40,128      | Johnny diving                            |
| MJCOCO.BMP     | 47,324      | Johnny with coconut                      |
| MJBOTTLE.BMP   | 27,132      | Johnny with bottle                       |
| MJBTL2.BMP     | 32,752      | Bottle variant 2                         |
| MJSANDC.BMP    | 22,096      | Johnny sandcastle building               |
| STNDLAY.BMP    | 18,980      | Johnny standing/laying                   |
| SLEEP.BMP      | 24,108      | Johnny sleeping                          |

#### Fishing
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| MJFISH1.BMP    | 76,596      | Johnny fishing 1                         |
| MJFISH2.BMP    | 44,168      | Johnny fishing 2                         |
| MJFISH3.BMP    | 15,632      | Johnny fishing 3                         |
| FISHMAN.BMP    | 6,912       | Fisherman character                      |
| LILFISH.BMP    | 2,160       | Small fish                               |
| GJCATCH1.BMP   | 56,392      | Gag: catch sequence 1                    |
| GJCATCH2.BMP   | 105,656     | Gag: catch sequence 2                    |
| GJCATCH3.BMP   | 13,440      | Gag: catch sequence 3                    |
| GJFFFOOD.BMP   | 138,524     | Gag: fast food fishing                   |
| SHKNFIST.BMP   | 6,688       | Shaking fist                             |

#### Raft / Water
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| MRAFT.BMP      | 10,616      | Raft (static)                            |
| MJRAFT2.BMP    | 23,184      | Johnny on raft 2                         |
| SJRAFT1.BMP    | 13,192      | Side-view raft 1                         |
| SRAFT.BMP      | 3,560       | Small raft view                          |
| SPLASH.BMP     | 21,512      | Water splash                             |

#### Island Elements
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| BACKGRND.BMP   | 92,656      | Background elements                      |
| CLOUDS.BMP     | 10,840      | Cloud sprites                            |
| TRUNK.BMP      | 7,440       | Palm tree trunk                          |
| COCONUTS.BMP   | 852         | Coconut sprites                          |
| COCOHEAD.BMP   | 4,040       | Coconut head                             |
| SANDCAST.BMP   | 34,184      | Sandcastle                               |

#### Building / Fire
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| FIRE.BMP       | 26,700      | Fire (main)                              |
| FIRE1.BMP      | 8,192       | Fire stage 1                             |
| FIRE2.BMP      | 6,800       | Fire stage 2                             |
| FIRE3.BMP      | 4,864       | Fire stage 3                             |
| FIRE4.BMP      | 36,840      | Fire stage 4                             |
| FIRE5.BMP      | 984         | Fire stage 5                             |
| GJCASTLE.BMP   | 10,080      | Gag: castle                              |
| GJBIPLAN.BMP   | 25,400      | Gag: biplane                             |

#### Visitor / Character sprites
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| GJVIS3.BMP     | 34,768      | Visitor scene 3                          |
| GJVIS5.BMP     | 46,880      | Visitor scene 5                          |
| GJVIS52.BMP    | 48,808      | Visitor scene 5 variant 2                |
| GJVIS6.BMP     | 47,608      | Visitor scene 6                          |
| GJPROW.BMP     | 20,392      | Ship prow                                |
| SHIPS.BMP      | 43,324      | Ships                                    |
| TANKER.BMP     | 10,208      | Tanker ship                              |
| LILIPUTS.BMP   | 19,544      | Lilliputians                             |
| SHARK.BMP      | 91,104      | Shark                                    |
| SHARKWLK.BMP   | 7,708       | Shark walking                            |

#### Gull / Nature gags
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| GJGULL1.BMP    | 17,000      | Gull gag 1                               |
| GJGULL1A.BMP   | 38,568      | Gull gag 1A                              |
| GJGULL2.BMP    | 28,548      | Gull gag 2                               |
| GJGULL2A.BMP   | 14,128      | Gull gag 2A                              |
| GJGULL3.BMP    | 43,268      | Gull gag 3                               |
| GJGULL3A.BMP   | 56,808      | Gull gag 3A                              |
| GJHOT.BMP      | 34,312      | Hot weather gag                          |
| GJNAT1.BMP     | 53,652      | Nature gag 1                             |
| GJNAT1LI.BMP   | 35,952      | Nature gag 1 (light/alt)                 |
| GJNAT3.BMP     | 79,884      | Nature gag 3                             |
| GJANGRY.BMP    | 38,568      | Angry gag                                |
| GJKINGKO.BMP   | 42,900      | King Kong gag                            |
| GJRUNAWA.BMP   | 43,556      | Runaway gag                              |
| GJDIVE.BMP     | 83,296      | Dive gag                                 |

#### Mary / Romance
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| SMDATE1-12.BMP | varies      | Date sequence sprites (12 files)         |
| SMGLIMSE.BMP   | 30,728      | Glimpse of Mary                          |
| SMGIFT.BMP     | 52,588      | Gift exchange                            |
| SMGFTWAV.BMP   | 1,940       | Gift wave                                |
| SJGFTASK.BMP   | 19,484      | Gift ask                                 |
| SJGFTJMP.BMP   | 9,728       | Gift jump                                |
| SJGFTSHY.BMP   | 15,024      | Gift shy                                 |
| SJGFTXCH.BMP   | 29,792      | Gift exchange                            |
| SBREAKUP.BMP   | 59,736      | Breakup scene                            |
| SJBRAKUP.BMP   | 2,024       | Breakup side-view                        |
| SLEVEJC1-3.BMP | varies      | Leave JC sequence (3 files)              |
| SLEVEJM1-3.BMP | varies      | Leave JM sequence (3 files)              |

#### Suzy (city scenes)
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| SSUZY1.BMP     | 31,396      | Suzy scene 1                             |
| SSUZY2.BMP     | 48,184      | Suzy scene 2                             |
| SSUZY3.BMP     | 76,720      | Suzy scene 3                             |
| SJMSUZY1.BMP   | 16,148      | Side-view J+Suzy 1                       |
| SJMSUZY2.BMP   | 43,676      | Side-view J+Suzy 2                       |
| SJMSUZY3.BMP   | 72,356      | Side-view J+Suzy 3                       |

#### Misc / Special
| File           | Size (bytes) | Description                              |
|----------------|-------------|------------------------------------------|
| MEANWHIL.BMP   | 8,648       | "Meanwhile..." text overlay              |
| THEEND1.BMP    | 15,740      | "The End" sprite                         |
| ENDCRDTS.BMP   | 7,688       | End credits                              |
| LITEBULB.BMP   | 5,216       | Light bulb (thought indicator)           |
| THNKBUBL.BMP   | 53,412      | Thought bubble                           |
| ZZZZS.BMP      | 196         | Sleep Z's                                |
| WOULDBE.BMP    | 58,036      | "Would-be" rescue scenes                 |
| JOHNWOUL.BMP   | 22,652      | Johnny would-be                          |
| BOAT.BMP       | 7,564       | Boat                                     |
| HOLIDAY.BMP    | 8,892       | Holiday decorations                      |
| SA_DEMO.BMP    | 736         | Screen Antics demo tag                   |
| JATA.BMP       | 3,360       | JATA (unknown)                           |
| SJWORK.BMP     | 12,816      | Side-view Johnny working                 |


## 4. Scene-to-Resource Mapping

### 4.1 ADS Family Resource Tables

Each ADS family loads a specific set of BMPs, SCRs, and TTMs. The SCR determines
the background; the BMPs determine which sprites can appear.

| ADS Family     | SCR Used        | Key BMPs (distinguishing)                         |
|----------------|-----------------|---------------------------------------------------|
| STAND.ADS      | ISLETEMP.SCR    | MJ_AMB, MJTELE, MJTELE2                          |
| ACTIVITY.ADS   | ISLETEMP.SCR    | MJDIVE, GJDIVE, MJREAD, MJBATH, GJNAT1/3, GJHOT  |
| BUILDING.ADS   | ISLETEMP.SCR    | SANDCAST, MJSANDC, GJCASTLE, FIRE/1-5, LILIPUTS  |
| FISHING.ADS    | ISLETEMP.SCR    | MJFISH1/2/3, FISHMAN, LILFISH, GJCATCH1/2/3      |
| VISITOR.ADS    | ISLETEMP.SCR    | GJVIS3/5/6, SHIPS, TANKER, GJPROW, LILIPUTS      |
| MISCGAG.ADS    | ISLETEMP.SCR    | GJHOT, SHARKWLK, SHARK, GJGULL1                  |
| WALKSTUF.ADS   | ISLETEMP.SCR    | JOHNWALK, WOULDBE, BOAT, MJRAFT2, MJJOG1/2       |
| MARY.ADS       | ISLETEMP.SCR    | SMDATE1-12, SMGLIMSE, SJGFT*, SBREAKUP, SLEVE*   |
| JOHNNY.ADS t1  | THEEND.SCR      | THEEND1, ENDCRDTS, MEANWHIL, SJWORK, THNKBUBL    |
| JOHNNY.ADS t6  | JOFFICE.SCR     | SJWORK, THNKBUBL, MEANWHIL, THEEND1              |
| SUZY.ADS       | SUZBEACH.SCR    | SSUZY1/2/3, SJMSUZY1/2/3, MRAFT                  |
| (intro)        | INTRO.SCR       | (played via adsPlayIntro, not a normal ADS)       |

### 4.2 Shared BMPs Across Families

Several BMPs appear across multiple ADS families:
- **JOHNWALK.BMP** -- nearly universal (ACTIVITY, BUILDING, FISHING, MARY, MISCGAG, WALKSTUF, JOHNNY)
- **TRUNK.BMP** -- ACTIVITY, BUILDING, FISHING, MARY, MISCGAG, VISITOR, WALKSTUF
- **LITEBULB.BMP** -- ACTIVITY, JOHNNY, MARY, SUZY, WALKSTUF
- **SPLASH.BMP** -- ACTIVITY, BUILDING, FISHING, VISITOR
- **JCHANGE.BMP** -- ACTIVITY, MARY, WALKSTUF
- **MEXCWALK.BMP** -- ACTIVITY, MARY
- **MEANWHIL.BMP** -- JOHNNY, SUZY


## 5. Island Position Variance

### 5.1 Position Randomization Logic

The island position is controlled by `islandState.xPos` and `islandState.yPos`, applied
as a global offset (`grDx`, `grDy`) to all sprite and background coordinates.

From `story.c` line 268-292, the randomization has three modes:

**Mode: VARPOS_OK (most scenes)**
Three sub-distributions, each 50% chance of the first, 25% each for the others:

| Branch        | xPos Range          | yPos Range          |
|---------------|---------------------|---------------------|
| 50% chance    | [-222, -113]        | [-44, +84]          |
| 25% chance    | [-114, +20]         | [-14, +85]          |
| 25% chance    | [-114, +5]          | [-73, -13]          |

**Mode: LEFT_ISLAND (fishing, visitor, mary scenes)**
Fixed position: xPos = -272, yPos = 0.
Shifts the island fully left so the right side of the screen shows open ocean.

**Mode: Default (no VARPOS_OK, no LEFT_ISLAND)**
Fixed position: xPos = 0, yPos = 0.
The island appears at its canonical center position.

### 5.2 Effective Screen Regions

The ISLETEMP.SCR background is 640x350 pixels. The island content within it occupies
roughly the center-right area (x ~200-600, y ~100-350 in source coordinates).

With the position offset applied:
- **Horizontal range**: The island's left edge can be from x=(-272+200)=-72 to x=(+20+200)=220.
  Its right edge from about x=(-272+600)=328 to x=(+20+600)=620.
- **Vertical range**: The island's top edge can be from y=(-73+100)=27 to y=(+85+100)=185.
  Its bottom edge from about y=(-73+350)=277 to y=(+85+350)=435.

### 5.3 Robust Detection Regions

Given the variance, these screen regions have consistent properties:

| Region             | Y Range   | X Range   | What's Always There           |
|--------------------|-----------|-----------|-------------------------------|
| Top sky strip      | 0-50      | 0-640     | Sky (bright blue / bright cyan) in day scenes |
| Bottom ocean strip | 400-480   | 0-640     | Always ocean (from OCEAN0x base) |
| Center band        | 100-350   | 0-640     | Island content when present   |

For detecting island presence: look for **yellow (index 14)** and **olive (index 6)** pixels
in the y=100-400 band. Ocean-only scenes have negligible amounts of these colors.

For detecting island position: the sand/vegetation cluster will form a contiguous
region whose centroid shifts with xPos/yPos.


## 6. Color Analysis for Detection

### 6.1 Title Screen

The title screen is displayed in two distinct phases:

1. **Early title (TITLE.RAW)**: Pre-rendered 640x480 raw 15-bit framebuffer image,
   loaded directly by `loadTitleScreenEarly()` before resource parsing. Not palette-indexed.
   This shows the Sierra logo and "Johnny Castaway" branding. Cannot be palette-analyzed
   since it bypasses the 16-color palette entirely.

2. **Intro SCR (INTRO.SCR)**: Loaded via `adsPlayIntro()`. Uses the standard palette.
   Signature: 70% black (5), 11% bright cyan (11), 8% bright blue (9).
   Distinguishable from gameplay by the extreme black dominance combined with scattered
   bright cyan accents.

**Detection**: If >60% of pixels are black AND bright cyan is 5-15%, it is the intro screen
(not gameplay). The early raw title is identifiable because it contains colors outside
the 16-entry palette.

### 6.2 Ocean vs Island

| Feature                  | Ocean Only                 | Island Present              |
|--------------------------|----------------------------|-----------------------------|
| Bright blue (9)          | ~41-43%                    | ~37-41%                     |
| Bright cyan (11)         | ~33-34%                    | ~35-36%                     |
| Black (5)                | ~10-12%                    | ~0.2%                       |
| **Bright yellow (14)**   | **0%**                     | **2.2%**                    |
| **Dark cyan (3)**        | **0-2%**                   | **4.5%**                    |
| **Dark green (2)**       | **0%**                     | **1.0%**                    |
| **White (15)**           | ~1.3-1.7%                  | **8.5-9.1%**                |
| Dark blue (1)            | 4-6%                       | 2-6%                        |

**Primary discriminator**: Bright yellow (14) > 1% = island is present.
**Secondary**: Dark green (2) > 0.5% = island is present.
**Tertiary**: White (15) > 5% = island is present (wave foam around island).

### 6.3 Johnny (the character)

Johnny's primary visual signature:
- **Shirt**: Bright red (12) and dark red (4)
- **Pants**: Bright blue (9) and dark blue (1)
- **Skin**: Bright yellow (14) and olive (6)
- **Hair**: Black (5)

Because the sky is also bright blue (9), the pants blend with background. The best
discriminator for Johnny's presence is **bright red (12)** in the sprite compositing
region (roughly the lower-center of the screen where island activities happen).

### 6.4 Mary / Female Characters

Mary uses similar skin tones to Johnny but with different clothing colors. Her dress
tends to use **bright green (10)** or **bright magenta (13)** which are rarely used
for other elements. The SMDATE*.BMP sprites are unique to the MARY.ADS family.

### 6.5 Fire

Fire sprites (FIRE*.BMP) use heavy amounts of:
- **Bright red (12)** and **bright yellow (14)** -- fire colors
- **Dark red (4)** -- embers
These are unique to the BUILDING.ADS fire scenes.

### 6.6 Night Scenes

Night scenes use NIGHT.SCR which is dominated by:
- **Dark blue (1)**: 69% -- the night sky/ocean
- **Black (5)**: 27%
- **White (15)**: 3% -- stars/moonlight
- Almost no bright colors at all

**Detection**: Dark blue (1) > 50% = night scene.

### 6.7 Ships / Visitors

Ships (SHIPS.BMP, TANKER.BMP, GJPROW.BMP) are large sprites that use:
- **Medium gray (8)** and **light gray (7)** for hulls
- **White (15)** for sails/superstructure
- **Dark red (4)** for accent stripes

When a ship is on screen, the proportion of gray tones in the y=50-200 band increases
noticeably compared to normal island scenes.


## 7. Telemetry Bar System

### 7.1 Overview

The PS1 port includes a comprehensive telemetry overlay rendered into bgTile0
(top-left 320x240 quadrant of the screen). When `grPs1TelemetryEnabled == 1`,
colored bars encode scene state directly into the framebuffer.

### 7.2 Telemetry Panel Layout

All bars are drawn by `grDrawCounterBar()` at fixed pixel coordinates in bgTile0.

**Panel 1: Drop Diagnostics (top-left, y=2-16)**
- y=2,  x=6: Black panel 148px wide
- Red bar (0x001F): Thread drops (cumulative)
- Magenta bar (0x7C1F): BMP frame cap hits
- Cyan bar (0x7FE0): Short loads
- White heartbeat at (152, 2)
- Red moving marker at (156 + frame%32, 2)

**Panel 2: Pilot Pack Diagnostics (y=30-60)**
- White (0x7FFF): Active pilot pack ID
- Green (0x03E0): Cumulative pack hits
- Red (0x001F): Cumulative fallback loads
- Yellow (0x03FF): Last successful pack entry index
- Magenta (0x7C1F): Last fallback entry index
- Cyan (0x7FE0): Failed pack lookups while active

**Panel 3: ADS Freeze Diagnostics (y=90-140)**
- Blue (0x03FF): Active threads count
- White (0x7FFF): Mini timer
- Yellow (0x03FF): Scene slot/tag signature
- Magenta (0x7C1F): Replay count
- Cyan (0x03FF): Running thread count
- Green (0x03E0): grUpdateDelay
- White: Replay tries this frame
- Green: Replay draws this frame
- Magenta: Merged carry-forward draws
- Red (0x001F): Played threads with zero draws
- Cyan: Threads played this frame
- Yellow (0x7FE0): Total recorded sprites this frame
- Red: Terminated thread count
- White: ADD_SCENE calls
- Green: TTM tag lookup hits
- Magenta: TTM tag lookup misses

**Panel 4: Memory Diagnostics (y=174-195)**
- Memory used percentage (green/yellow/red by pressure)
- Loaded BMP resources
- Loaded TTM resources
- Loaded ADS resources
- Used KiB / 16
- Budget KiB / 16

**Panel 5: Story Diagnostics (y=222-243)**
- Gray (0x4210): Sequence ID
- White (0x7FFF): Phase
- Green (0x03E0): Scene tag
- Magenta (0x7C1F): ADS family signature
- Yellow (0x03FF): prevSpot/prevHdg
- Cyan (0x7FE0): nextSpot/nextHdg

### 7.3 Key Telemetry Values for Scene Identification

The most directly useful telemetry bar for scene identification is the **ADS family
signature** in Panel 5 (y=232, magenta bar). The `ps1StoryDbgFamilyId()` function
encodes:

| Family ID | ADS File        |
|-----------|-----------------|
| 0         | (unknown/none)  |
| 1         | ACTIVITY.ADS    |
| 2         | BUILDING.ADS    |
| 3         | FISHING.ADS     |
| 4         | JOHNNY.ADS      |
| 5         | MARY.ADS        |
| 6         | MISCGAG.ADS     |
| 7         | STAND.ADS       |
| 8         | SUZY.ADS        |
| 9         | VISITOR.ADS     |
| 10        | WALKSTUF.ADS    |

The **scene tag** (green bar at y=229) directly encodes the ADS tag number (1-16).

Together, (family, tag) uniquely identifies any scene. The ADS freeze panel also
encodes the **scene slot + scene tag** at y=97 as
`((slot & 0x7) << 3) | (tag & 0x7)`.

### 7.4 Telemetry Region Footprint

All telemetry panels occupy the **left 100 pixels** of the screen, between **y=2 and y=243**.
This is entirely within bgTile0 (x=0-319, y=0-239).

**Important**: When telemetry is enabled, the top-left corner of the screen is
contaminated with diagnostic bars. Any visual detection system must either:
1. Exclude the region x=0-160, y=0-243 from color sampling, or
2. Detect telemetry bars and subtract them, or
3. Only sample from the right half (x=200+) and bottom (y=244+) of the screen.


## 8. Detection Strategy Recommendations

### 8.1 Tier 1: Telemetry Bar Decoding (Ground Truth)

When telemetry is enabled (default), the bars encode scene identity directly:

1. **Read the ADS family signature bar** at pixel coordinates (4, 232) in bgTile0.
   Measure the length of the magenta (0x7C1F) bar. Length in pixels = family ID (1-10).
2. **Read the scene tag bar** at (4, 229). Length of green (0x03E0) bar = tag number.
3. **Validate**: Check the heartbeat marker at (152, 2) -- if white pixel present,
   telemetry is active and these readings are reliable.

This gives exact scene identification with no ambiguity.

### 8.2 Tier 2: Background SCR Classification (When Telemetry Unavailable)

Sample a 100x100 region from the **top-right corner** (x=500-600, y=0-100) to avoid
telemetry contamination. Build a 16-bin histogram of palette indices.

Decision tree:
```
if dark_blue(1) > 50%:
    -> NIGHT scene
elif black(5) > 60%:
    if bright_red(12) > 5%:
        -> THEEND.SCR (The End screen)
    elif medium_gray(8) > 5%:
        -> JOFFICE.SCR (Johnny's office)
    elif bright_cyan(11) > 10%:
        if bright_yellow(14) > 0.5%:
            -> SUZBEACH.SCR (Suzy's beach)
        else:
            -> INTRO.SCR (title/intro)
    else:
        -> Unknown dark screen
elif bright_blue(9) + bright_cyan(11) > 50%:
    -> Ocean/Island scene (see Tier 3 for refinement)
else:
    -> Unknown
```

### 8.3 Tier 3: Island vs Ocean Discrimination

For ocean/island scenes, sample the **center-bottom band** (x=200-600, y=250-400):

```
if bright_yellow(14) > 1.0% in this region:
    -> Island present (ISLETEMP or ISLAND2)
elif dark_green(2) > 0.3% OR dark_cyan(3) > 3%:
    -> Island present (edge of island visible)
else:
    -> Pure ocean (OCEAN0x)
```

### 8.4 Tier 4: ADS Family Identification from Sprites

Once we know the background is ISLETEMP (island), we need to distinguish which ADS
family is playing. This requires detecting characteristic sprites:

| Detection Target    | Color Signature in Active Region                        |
|---------------------|---------------------------------------------------------|
| Fire (BUILDING)     | Bright red (12) + bright yellow (14) cluster, y=150-300|
| Ship (VISITOR)      | Large gray (7/8) region, y=50-200                       |
| Fishing pole        | Thin line of dark colors extending from island          |
| Mary scenes         | Bright green (10) or bright magenta (13) near Johnny    |
| Shark (MISCGAG)     | Gray (8) + white (15) in water zone, y=250-400          |
| Raft on water       | Brown (6) + red (4) cluster in water zone               |

**Approach**: After background subtraction (compare current frame to clean background
tile), count non-background palette colors in the sprite compositing region.

### 8.5 Tier 5: Sprite Presence Detection via Dirty Regions

The game already tracks dirty rectangles for efficient rendering. The dirty regions
(`currDirtyMinY[4]` / `currDirtyMaxY[4]` arrays) indicate exactly which rows of
which tiles have been modified by sprite compositing. These can be read as a cheap
proxy for "where are sprites drawn this frame."

### 8.6 Detection System Architecture

Recommended pipeline for each frame:

```
1. Check telemetry heartbeat pixel at (152, 2)
   -> If active: read telemetry bars (Tier 1), done.

2. Sample top-right corner histogram
   -> Classify background SCR (Tier 2)

3. If ocean/island background:
   a. Sample center-bottom histogram
      -> Island present? (Tier 3)
   b. If island:
      - Sample sprite activity region for characteristic colors (Tier 4)
      - Cross-reference with known ADS family color profiles

4. Output: { background_type, island_present, estimated_family, confidence }
```

### 8.7 Confidence Levels

| Level | Meaning                                                      |
|-------|--------------------------------------------------------------|
| HIGH  | Telemetry bars decoded, or background uniquely identifies scene (THEEND, NIGHT, JOFFICE) |
| MEDIUM| Island/ocean discriminated, and characteristic sprite colors detected |
| LOW   | Background classified but sprite family ambiguous (e.g., STAND vs ACTIVITY on island) |

### 8.8 Cross-Validation Strategy

When both telemetry and visual detection are available:
1. Decode telemetry bars -> get (family, tag)
2. Run visual classifier -> get estimated_family
3. If they agree -> high confidence, use telemetry value
4. If they disagree -> log warning, prefer telemetry (it is authoritative)
5. If telemetry unavailable -> use visual detection with appropriate confidence level


## 9. Implementation Considerations

### 9.1 Performance Budget

On PS1 (33 MHz MIPS R3000), sampling a 100x100 region = 10,000 pixels.
With 4-bit indexed pixels (2 per byte), this is 5,000 byte reads + 10,000 histogram
increments. At ~10 cycles per pixel, this is ~100K cycles = ~3ms at 33 MHz.
Acceptable for per-frame use if done during VSync wait.

### 9.2 Memory Cost

- 16-entry histogram: 32 bytes (uint16 per bin)
- Sample region coordinates: 16 bytes
- Decision tree logic: ~200 bytes of code
- Total: negligible

### 9.3 Sampling Points

Recommended fixed sample points that avoid telemetry contamination:
- **Sky sample**: (500, 20) to (600, 50) -- 30x100 = 3000 pixels
- **Ocean sample**: (200, 420) to (600, 460) -- 40x400 = 16000 pixels
- **Island test**: (250, 280) to (550, 350) -- 70x300 = 21000 pixels
- **Sprite test**: (300, 150) to (580, 350) -- 200x280 = 56000 pixels

For quick classification, even a 20x20 sample from sky + ocean + island regions
(1200 total pixels) should suffice for Tier 2/3 detection.
