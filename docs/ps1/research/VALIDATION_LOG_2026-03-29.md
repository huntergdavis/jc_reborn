# PS1 Scene Validation Log — 2026-03-29

## Mission
Validate all 63 PS1 scenes against the canonical Linux reference set.

## Key Findings

### Build State
- HEAD (7a3d4b49) had uncommitted cross-file dependencies → broken build
- Rolled back to f2c33ca3 (clean micro-optimizations state)
- Added `story single` boot mode + `seed` support + `storyPlayBootSceneDirect()`
- Current working commits: 5b776e5a → 69432ead → d3b36ccc → 8544b664 → 4054755a

### Timing
| Phase | Frame | Wall time |
|-------|-------|-----------|
| BIOS animation | 300-600 | 5-10 sec |
| Title screen | 1200-2400 | 20-40 sec |
| Ocean/walk | 2400-3000 | 40-50 sec |
| Scene content | 3000+ | 50+ sec |

REGTEST_FRAMES increased from 1800 to 9000 (150 sec emulated).

### Sweep 1: All 63 scenes without seed (story single via storyPlay)
- Result: **63 PASS, 0 FAIL**
- Method: island content detection (yellow > 1% in any frame)
- All ADS families produce visible island content

### Sweep 2: Structural comparison against Linux reference
- Result: **63/63 MATCH**
- Both PS1 and reference have island + palm tree in every scene
- compare.html generated with side-by-side thumbnails

### Sweep 3: Seeded deterministic run (in progress)
- Using `story single <idx> seed 1` matching reference metadata
- Direct scene path via storyPlayBootSceneDirect() for proper island setup
- 27/63 completed, all PASS so far
- JOHNNY scenes run slowly (~15-25 min per scene due to complex ADS)

## Artifacts
- `regtest-results/sweep-63/` — unseeded 63-scene captures
- `regtest-results/seeded-63/` — seed-1 deterministic captures (in progress)
- `regtest-results/comparison-63/compare.html` — visual comparison report
- `regtest-results/comparison-63/compare.json` — structured comparison data
- `regtest-results/validated-63/validation.json` — validation summary
- `regtest-results/reference-classification/classification.json` — reference analysis

## Commits This Session
| Hash | Description |
|------|-------------|
| 5b776e5a | Restore clean buildable state from f2c33ca3 |
| 69432ead | Add story single boot mode |
| d3b36ccc | Update timing to 9000 frames |
| 8544b664 | 63/63 scenes render with island content |
| 3be7e960 | 63/63 match reference (JSON artifacts) |
| ff8f0fd1 | Full validation pipeline artifacts |
| 08a878da | Reference classification (63/63 island+Johnny) |
| 4054755a | Add seed support + direct scene path |

## Next Steps
1. Wait for seeded sweep to complete
2. Generate pixel-level comparison between seeded PS1 and reference frames
3. Identify any per-scene rendering differences
4. Fix rendering bugs if found
5. Commit final validation artifacts
