# PS1 Audio Implementation Spec

## 1. Sound Inventory

### 1.1 Source Format

All 23 original WAV files share identical format:
- **Sample rate:** 11025 Hz
- **Bit depth:** 8-bit unsigned
- **Channels:** 1 (mono)
- **Container:** RIFF WAV

Note: `convert-sounds.sh` comment says "22050 Hz" but this is incorrect. Every
WAV file is 11025 Hz as verified by parsing headers.

### 1.2 Complete Sound Table

| Idx | WAV Size | PCM Frames | Duration | TTM Refs | TTM Files | RMS  | Peak | Description                     |
|----:|---------:|-----------:|---------:|---------:|----------:|-----:|-----:|:--------------------------------|
|   0 |   10,768 |     10,262 |   0.931s |    2 (C) |     0 (C) |  6.6 |   44 | Day scene start ambience        |
|   1 |   11,264 |     11,072 |   1.004s |       28 |        18 |  8.9 |   98 | Splash / water                  |
|   2 |    1,536 |      1,488 |   0.135s |        1 |         1 | 14.3 |   43 | Short click / pop               |
|   3 |    7,680 |      7,392 |   0.670s |       21 |         8 |  7.7 |   76 | Action sound                    |
|   4 |    5,120 |      4,992 |   0.453s |       22 |        13 | 15.7 |   72 | Action / impact                 |
|   5 |    3,072 |      2,816 |   0.255s |       45 |        21 | 19.9 |   69 | Footstep / thud                 |
|   6 |   15,872 |     15,744 |   1.428s |       67 |        12 | 16.4 |   63 | Long splash / crash             |
|   7 |   15,360 |     14,976 |   1.358s |       13 |         5 | 11.3 |   43 | Long action                     |
|   8 |    2,560 |      2,304 |   0.209s |       19 |        14 | 15.0 |   58 | Short impact                    |
|   9 |    3,584 |      3,040 |   0.276s |       49 |        21 |  9.1 |   41 | Short action (heavily used)     |
|  10 |   20,480 |     20,224 |   1.834s |       19 |         4 | 11.8 |   68 | Long sound                      |
|  11 |      --- |        --- |      --- |        1 |         1 |  --- |  --- | **NO WAV EXISTS** (GJCATCH2)    |
|  12 |    5,632 |      5,438 |   0.493s |        4 |         2 |  6.9 |   61 | Medium action (Suzy/date)       |
|  13 |      --- |        --- |      --- |        0 |         0 |  --- |  --- | Gap in numbering (never used)   |
|  14 |   11,776 |     11,328 |   1.027s |        3 |         3 |  6.5 |   51 | Dive / splash                   |
|  15 |    3,072 |      2,838 |   0.257s |       19 |        11 | 14.4 |   71 | Short effect                    |
|  16 |    7,680 |      7,604 |   0.690s |       78 |        16 |  2.4 |   22 | Walking / footsteps (most used) |
|  17 |    4,608 |      4,253 |   0.386s |        0 |         0 | 15.5 |   42 | **Unused** (WAV exists, 0 refs) |
|  18 |   14,336 |     13,943 |   1.265s |        3 |         1 |  6.8 |   47 | Long effect (SJMSUZY only)      |
|  19 |    3,584 |      3,288 |   0.298s |       18 |         7 |  9.7 |   64 | Short effect                    |
|  20 |    7,680 |      7,215 |   0.654s |       19 |         6 | 11.0 |   35 | Medium effect                   |
|  21 |    5,120 |      4,838 |   0.439s |       37 |         4 |  9.1 |   47 | Medium effect                   |
|  22 |    1,536 |      1,292 |   0.117s |       36 |         5 | 28.3 |   67 | Short tick / tap                |
|  23 |    2,048 |      1,515 |   0.137s |       16 |        11 | 23.7 |   59 | Short tick / tap                |
|  24 |    9,728 |      9,672 |   0.877s |       17 |        16 |  8.5 |   52 | Long effect (widely used)       |

**(C)** = Called from C code (`story.c`), not TTM scripts. Sound 0 plays at
every daytime scene start via `soundPlay(0)` in `story.c:448` and `story.c:503`.

### 1.3 Key Observations

- **23 WAV files exist** on disk (indices 0-10, 12, 14-24; gaps at 11 and 13).
- **22 sounds are actually triggered** by the game (21 via TTM + sound 0 via C code).
- **Sound 11**: Referenced once in GJCATCH2.TTM but no WAV file exists. The
  desktop build silently ignores this (prints debug warning). PS1 should do same.
- **Sound 17**: WAV exists but is never referenced by any TTM or C code. Can be
  excluded from the PS1 build entirely.
- **Sound 0**: Only triggered from `story.c` (not TTMs). Plays at every
  daytime scene start, making it moderately frequent in practice.
- **Most frequently referenced**: Sound 16 (78 refs), Sound 6 (67 refs),
  Sound 9 (49 refs), Sound 5 (45 refs).
- **All sounds are short effects** -- longest is 1.83s (sound 10). No looping
  sounds, no music.

## 2. Current PS1 Implementation Status

The existing `sound_ps1.c` already implements a working basic audio system:

- Calls `SpuInit()` and sets master volume.
- Loads all 25 VAG files from CD at startup (`\\SND\\SOUND00.VAG;1` etc.).
- Uploads each VAG's ADPCM data to SPU RAM sequentially starting at 0x1010.
- Uses round-robin across 8 SPU channels for playback.
- Parses VAG header for sample rate, computes SPU pitch via `getSPUSampleRate()`.
- ADSR set to max attack, max sustain, no release (0x7F/0x0/0x7F/0x0/0xF).

The existing VAG files in `jc_resources/extracted/snd/` were generated by the
custom `scripts/wav2vag.py` encoder.

## 3. SPU RAM Budget

### 3.1 SPU RAM Layout

```
0x0000 - 0x0FFF  [4,096 B]  Reserved (capture buffers)
0x1000 - 0x100F  [   16 B]  PSn00bSDK dummy sample block
0x1010 - onwards              Sound effect ADPCM data
                  ...
0x7FFFF                       End of 512 KB SPU RAM
```

Usable space for sound data: **520,176 bytes** (0x1010 to 0x7FFFF).

### 3.2 Current ADPCM Sizes (from existing VAG files)

| Idx | ADPCM Bytes | Notes               |
|----:|------------:|:---------------------|
|   0 |       5,904 |                      |
|   1 |       6,368 |                      |
|   2 |         896 |                      |
|   3 |       4,256 |                      |
|   4 |       2,896 |                      |
|   5 |       1,648 |                      |
|   6 |       9,040 |                      |
|   7 |       8,592 |                      |
|   8 |       1,360 |                      |
|   9 |       1,776 |                      |
|  10 |      11,600 |                      |
|  12 |       3,152 |                      |
|  14 |       6,512 |                      |
|  15 |       1,664 |                      |
|  16 |       4,384 |                      |
|  17 |       2,464 | Unused, can exclude  |
|  18 |       8,000 |                      |
|  19 |       1,920 |                      |
|  20 |       4,160 |                      |
|  21 |       2,800 |                      |
|  22 |         784 |                      |
|  23 |         912 |                      |
|  24 |       5,568 |                      |
| **Total** | **96,656** | **94.4 KB** |

**Current SPU RAM usage: 18.6%** of usable space. This is very comfortable.

### 3.3 After Optimizations

| Optimization               | Total ADPCM | Savings     |
|:---------------------------|------------:|------------:|
| Current (as-is)            |  96,656 B   |         --- |
| Remove unused sound 17     |  94,192 B   |   2,464 B   |
| Trim silence (all sounds)  |  79,360 B   |  14,832 B   |
| **Remove 17 + trim all**   |  **77,072 B** | **19,584 B** |

Trimmed + pruned total: **75.3 KB** (14.8% of SPU RAM).

### 3.4 Verdict: Sample Rate Reduction Not Needed

The source WAVs are already at 11025 Hz, which is the lowest practical rate for
these sound effects. Downsampling to 5512 Hz would save ~50% but produce
unacceptably muffled audio for effects that contain transients (clicks, splashes,
impacts). Since total usage is only ~95 KB out of ~508 KB available, there is no
reason to reduce sample rate.

Similarly, mono downmixing is not applicable since all sources are already mono.

## 4. Loading Strategy: Full Preload at Startup

### 4.1 Recommendation: Preload All Sounds

**Justification:**
- Total ADPCM data is only ~95 KB, fitting easily in the ~508 KB of usable SPU RAM.
- All 22 active sounds can be loaded simultaneously with 413 KB to spare.
- Preloading eliminates CD seek latency during gameplay (CD seeks are 100-300ms).
- No system RAM is consumed -- data goes directly from CD to main RAM buffer to
  SPU RAM, then the main RAM buffer is freed.
- The existing `sound_ps1.c` already implements this approach correctly.

**Why not on-demand loading:**
- CD access during scene playback would cause visible frame hitches.
- The CD drive is already used for loading scene BMP/TTM/SCR data.
- Contention between sound loading and scene data loading would be complex.
- There is no memory pressure to justify the complexity.

**Why not streaming:**
- All sounds are under 2 seconds. Streaming is for long audio (music, voice).
- Streaming requires dedicated SPU channels, IRQ handlers, and double-buffering
  logic. Massive overkill for short one-shot effects.

### 4.2 Main RAM Impact During Loading

During `soundInit()`, each VAG file is loaded from CD into a temporary main RAM
buffer, then DMA'd to SPU RAM, then freed. The largest single VAG file is
SOUND10.VAG at 11,648 bytes. Peak transient main RAM usage is ~12 KB (one
file at a time). This is negligible.

After all uploads complete, zero main RAM is consumed by audio. The
`soundAddresses[]`, `soundSizes[]`, and `soundSampleRates[]` arrays total only
25 x 12 = 300 bytes of static data.

## 5. Conversion Pipeline

### 5.1 Current Pipeline

```
sound{N}.wav  -->  scripts/wav2vag.py  -->  jc_resources/extracted/snd/SOUND{NN}.VAG
```

The existing `wav2vag.py` script:
1. Reads WAV header to get sample rate, channels, bit depth.
2. Converts 8-bit unsigned to 16-bit signed PCM.
3. Encodes to PS1 SPU ADPCM (4-bit, 28 samples per 16-byte block).
4. Uses brute-force best-fit across 5 filter coefficients x 13 shift values.
5. Prepends 48-byte VAG header (magic "VAGp", version 0x20, big-endian sizes).
6. Adds leading silent block (16 bytes) and trailing end block (flag 0x07).

### 5.2 Recommended Enhanced Pipeline

```
sound{N}.wav
    |
    v
[1] Trim silence (leading + trailing, threshold = 5 from center)
    |
    v
[2] Skip sound 17 (unused) and sound 11/13 (no WAV)
    |
    v
[3] Encode to ADPCM (existing wav2vag.py encoder)
    |
    v
[4] Write VAG with header
    |
    v
SOUND{NN}.VAG --> placed in CD image under \SND\
```

### 5.3 Implementation Details

**Silence trimming** should be added to `wav2vag.py`. Trim to the nearest ADPCM
block boundary (28 samples) to avoid cutting mid-block. Trim thresholds per the
analysis:

| Idx | Leading Silence | Trailing Silence | Total Trimmable | % of File |
|----:|----------------:|-----------------:|----------------:|----------:|
|   0 |              99 |            2,772 |           2,871 |     28.0% |
|   1 |              89 |            1,115 |           1,204 |     10.9% |
|   5 |             205 |              677 |             882 |     31.3% |
|   9 |             549 |              315 |             864 |     28.4% |
|  10 |             340 |            2,938 |           3,278 |     16.2% |
|  16 |             984 |            1,302 |           2,286 |     30.1% |
|  21 |             174 |            1,696 |           1,870 |     38.7% |
|  24 |             988 |            3,202 |           4,190 |     43.3% |

Sounds 0, 5, 9, 16, 21, and 24 benefit most from trimming (>28% reduction).

**Sound 17 exclusion**: Simply skip it in the conversion script. The
`soundPlay()` function already handles missing sounds gracefully (returns
immediately if `soundAddresses[nb] == 0`).

### 5.4 No External Tool Dependencies

The project already has a pure-Python VAG encoder (`scripts/wav2vag.py`) that
produces correct output. No need for external `wav2vag` binaries, `ffmpeg`, or
PSn00bSDK tools (PSn00bSDK 0.24 does not include a VAG encoder).

## 6. SPU RAM Address Map

### 6.1 Proposed Layout (All Sounds Preloaded, With Trimming)

All addresses are 16-byte aligned (ADPCM block size). Sounds are packed
sequentially starting at 0x1010.

```
Address     Size    Sound   Duration  Description
-------     ----    -----   --------  -----------
0x00000     4096    ---     ---       Reserved (SPU capture buffers)
0x01000       16    ---     ---       PSn00bSDK dummy block
0x01010     4256    snd00   0.670s    Day scene ambience
0x020B0     5680    snd01   0.895s    Splash / water
0x036E0      768    snd02   0.114s    Short click
0x039E0     3856    snd03   0.606s    Action sound
0x048F0     2448    snd04   0.383s    Action / impact
0x05280     1152    snd05   0.175s    Footstep / thud
0x05700     8608    snd06   1.359s    Long splash / crash
0x078A0     8064    snd07   1.274s    Long action
0x09820     1200    snd08   0.184s    Short impact
0x09CD0     1280    snd09   0.197s    Short action
0x0A1D0     9728    snd10   1.537s    Long sound
0x0C7D0     2464    snd12   0.385s    Medium action
0x0D170     6048    snd14   0.955s    Dive / splash
0x0E910     1392    snd15   0.216s    Short effect
0x0EE80     3072    snd16   0.482s    Walking / footsteps
0x0FA80     7680    snd18   1.214s    Long effect (Suzy scenes)
0x11880     1760    snd19   0.273s    Short effect
0x11F60     3776    snd20   0.593s    Medium effect
0x12E20     1728    snd21   0.269s    Medium effect
0x134E0      720    snd22   0.107s    Short tick
0x137B0      688    snd23   0.103s    Short tick
0x13A60     3168    snd24   0.497s    Long effect
0x146C0     FREE    ---     ---       Remaining 430 KB free

Total sounds in SPU RAM: 22 (excluding snd11, snd13, snd17)
Total ADPCM:             79,536 B (77.7 KB)
Free SPU RAM:            440,640 B (430.3 KB, available for future music/reverb)
```

Note: Sound 18 is only used in SJMSUZY.TTM (3 references). It could optionally
be loaded on-demand to save 7.7 KB, but this is unnecessary given available space.

### 6.2 16-Byte Alignment

The existing code already aligns addresses:
```c
spuAddr += (adpcmSize + 15) & ~15;
```

The PSn00bSDK example rounds to 64-byte boundaries for DMA efficiency:
```c
int _size = (size + 63) & 0xffffffc0;
```

Recommendation: Use 64-byte alignment for DMA transfer reliability, 16-byte
alignment for address tracking. The existing code should be updated:

```c
/* DMA transfers should be padded to 64-byte boundaries */
SpuWrite((uint32_t *)(vagData + VAG_HEADER_SIZE), (adpcmSize + 63) & ~63);

/* But SPU address tracking can use 16-byte alignment */
spuAddr += (adpcmSize + 15) & ~15;
```

## 7. Implementation Changes Required

### 7.1 Changes to sound_ps1.c

The existing implementation is already functional. Required changes are minimal:

1. **Fix DMA transfer size alignment**: Round `SpuWrite()` size to 64-byte
   boundary (matching PSn00bSDK examples).

2. **Fix ADSR parameters**: Current values `(0x7F, 0x0, 0x7F, 0x0, 0xF)` use
   the `SpuSetVoiceADSR` macro which maps to specific register fields. A better
   configuration for one-shot effects that disable the envelope entirely:
   ```c
   SPU_CH_ADSR1(ch) = 0x00FF;  /* Max attack, no decay */
   SPU_CH_ADSR2(ch) = 0x0000;  /* No sustain/release rate */
   ```
   This matches the PSn00bSDK `vagsample` example exactly.

3. **Stop channel before reuse**: Add `SpuSetKey(0, 1 << ch)` before setting
   new parameters, to avoid glitches when a channel is reused while still
   playing:
   ```c
   SpuSetKey(0, 1 << ch);
   SPU_CH_FREQ(ch)  = getSPUSampleRate(soundSampleRates[nb]);
   SPU_CH_ADDR(ch)  = getSPUAddr(soundAddresses[nb]);
   SPU_CH_VOL_L(ch) = 0x3FFF;
   SPU_CH_VOL_R(ch) = 0x3FFF;
   SPU_CH_ADSR1(ch) = 0x00FF;
   SPU_CH_ADSR2(ch) = 0x0000;
   SpuSetKey(1, 1 << ch);
   ```

4. **Consider reducing channel count**: 8 round-robin channels is generous. The
   game rarely plays more than 2-3 overlapping sounds. 4 channels would suffice
   and leave more channels free for potential future music playback.

### 7.2 Changes to wav2vag.py

Add silence trimming:

```python
def trim_silence(samples, threshold=5*256, block_size=28):
    """Trim leading and trailing silence, aligned to ADPCM block boundaries."""
    # Find first non-silent sample
    first = 0
    for i, s in enumerate(samples):
        if abs(s) > threshold:
            first = i
            break
    # Align to block boundary (round down)
    first = (first // block_size) * block_size

    # Find last non-silent sample
    last = len(samples) - 1
    for i in range(len(samples) - 1, -1, -1):
        if abs(samples[i]) > threshold:
            last = i
            break
    # Align to block boundary (round up)
    last = ((last + block_size) // block_size) * block_size

    return samples[first:last]
```

### 7.3 Changes to cd_layout.xml

Remove SOUND17.VAG entry if excluding unused sound:
```xml
<!-- Remove this line: -->
<file name="SOUND17.VAG" type="data" source="..."/>
```

### 7.4 Changes to convert-sounds.sh

Add skip for sound 17:
```bash
if [ $i -eq 17 ]; then
    echo "  SKIP: sound17.wav (unused by game)"
    continue
fi
```

## 8. Memory Savings Summary

| Configuration                    | SPU RAM Used | % of Usable | Savings vs Current |
|:---------------------------------|-------------:|------------:|-------------------:|
| Current (all 23 VAGs, no trim)   |    96,656 B  |       18.6% |                --- |
| Drop sound 17 only               |    94,192 B  |       18.1% |          2,464 B   |
| Trim silence only (keep all)     |    81,824 B  |       15.7% |         14,832 B   |
| **Drop 17 + trim silence**       | **79,536 B** |   **15.3%** |     **17,120 B**   |

All configurations leave >400 KB of SPU RAM free. The primary value of
optimization is not space savings (which are unnecessary) but improved audio
quality through silence removal -- sounds play without audible dead time at the
start, making them feel more responsive.

## 9. PSn00bSDK API Reference

### 9.1 Available Functions

From `psxspu.h` in PSn00bSDK 0.24:

| Function                      | Purpose                               |
|:------------------------------|:--------------------------------------|
| `SpuInit()`                   | Initialize SPU hardware                |
| `SpuWrite(data, size)`        | DMA upload data to SPU RAM             |
| `SpuRead(data, size)`         | DMA download from SPU RAM (not needed) |
| `SpuSetTransferMode(mode)`    | Set DMA or I/O transfer mode           |
| `SpuSetTransferStartAddr(a)`  | Set target address in SPU RAM          |
| `SpuIsTransferCompleted(w)`   | Wait for / poll DMA completion         |

### 9.2 Available Macros (Register Access)

| Macro                              | Purpose                              |
|:-----------------------------------|:-------------------------------------|
| `getSPUAddr(addr)`                 | Convert byte address to SPU units    |
| `getSPUSampleRate(rate)`           | Convert Hz to 4.12 fixed-point pitch |
| `SpuSetCommonMasterVolume(l, r)`   | Set master L/R volume                |
| `SpuSetVoiceVolume(ch, l, r)`      | Set channel L/R volume               |
| `SpuSetVoicePitch(ch, pitch)`      | Set channel playback rate            |
| `SpuSetVoiceStartAddr(ch, addr)`   | Set channel start address            |
| `SpuSetVoiceADSR(ch, ar,dr,sr,rr,sl)` | Set ADSR envelope               |
| `SpuSetKey(enable, bits)`          | Start (1) or stop (0) channels       |
| `SPU_CH_LOOP_ADDR(ch)`            | Direct register: loop address        |
| `SPU_CH_ADSR1(ch)`, `SPU_CH_ADSR2(ch)` | Direct ADSR registers           |

### 9.3 VAG File Format

```
Offset  Size    Field           Endian  Value
0       4       Magic           ---     "VAGp" (0x70474156)
4       4       Version         BE      0x00000020
8       4       Interleave      BE      0 (mono)
12      4       Data size       BE      Size of ADPCM data after header
16      4       Sample rate     BE      11025
20      12      Reserved        ---     0
32      16      Name            ASCII   "sound0\0..."
48      ...     ADPCM data      ---     16-byte blocks
```

Each ADPCM block (16 bytes):
- Byte 0: `(shift & 0xF) | (filter << 4)`
- Byte 1: Flags (0=normal, 1=loop end, 3=loop start, 7=stop)
- Bytes 2-15: 28 nibbles of 4-bit ADPCM delta samples

## 10. Future Considerations

### 10.1 Music Support

With ~430 KB of SPU RAM free after loading all sound effects, there is ample
room for background music if desired. Options:

- **SPU streaming from CD**: Use PSn00bSDK's streaming example pattern.
  Double-buffer ~16 KB in SPU RAM, stream from CD. Requires 2 channels for
  stereo. The `cdstream` example provides a complete reference implementation.
- **XA audio**: PS1's native CD-XA interleaved audio. Higher quality than SPU
  ADPCM at similar bitrates. Requires authoring XA sectors on the CD.

### 10.2 Reverb

The PS1 SPU has built-in reverb effects. With abundant free SPU RAM, a small
reverb buffer could add atmospheric depth. The reverb work RAM would need to be
allocated at the end of SPU RAM (SPU hardware requirement). Even a basic "room"
preset uses only ~32 KB.

### 10.3 Sound Priority System

If future scenes require many simultaneous sounds, a priority system could
replace round-robin channel allocation. Assign priorities based on sound type
(high-frequency transients > ambient) and preempt lowest-priority channels.
Not needed currently since max concurrent sounds in any scene is 2-3.

### 10.4 Volume Scaling

The original screensaver likely had volume variation per sound. The ADS/TTM
scripts might encode volume parameters that are currently ignored. The
`PLAY_SAMPLE` opcode (0xC051) takes a single argument (sound index) with no
volume parameter, so per-sound volume would need to be hardcoded or derived from
context. The SPU channel volume registers (`SPU_CH_VOL_L/R`) support 15-bit
signed values (0x0000-0x3FFF), so fine-grained volume control is trivially
possible.
