# Regtest Harness Worklog — 2026-03-28

Goal
- Replace the old final-frame/hash-based PS1-vs-reference harness with a scene-sequence harness that compares PS1 output against the canonical host baseline corpus in `regtest-references/`.

Current status
- Canonical host baseline corpus exists in `regtest-references/` for all 63 scenes.
- Core sequence comparator has been upgraded to:
  - align on scene entry
  - separate PS1 `pre-scene`, `in-scene`, and `post-scene` frames
  - report `MATCH`, `PIXEL_MISMATCH`, `TIMING_MISMATCH`, or `ALIGNMENT_FAILED`
- `regtest-scene.sh` can now request sequence-based baseline analysis against a canonical scene directory.
- `compare-reference-batch.sh` has been rewired toward sequence comparison and canonical reference defaults.
- `build-ps1.sh` now recreates `build-ps1/` if cleanup removed it.
- Raw PS1 output roots with nested Docker frame directories are now discovered correctly by `compare-sequence-runs.py`.
- `regtest-scene.sh` now has a `--fast-baseline` mode for interactive validation runs that skips full telemetry/visual-batch postprocess.
- Host capture now stamps engine-truth `scene_start_frame` / `scene_end_frame` from the executable itself.

Known issues
- Full-frame PS1 telemetry decode is still too expensive for interactive per-frame validation unless `--fast-baseline` is used.
- First live validation scene (`BUILDING 1`) still does not produce a positive PS1-to-host alignment yet.
- Old Dockerized regtest outputs can leave root-owned files in result directories; reruns should use fresh output dirs or the harness should isolate/chown them.
- The sequence comparator is still too slow once the PS1 side has thousands of candidate frames; entry-anchor search needs one more optimization pass before it becomes a comfortable interactive loop.

Validated so far
- Canonical host references are present and organized.
- Shell/python syntax for the updated harness scripts is clean.
- The PS1 validation lane now reaches the sequence comparator instead of failing earlier on missing build directories or missing result metadata.
- `BUILDING 1` host reference starts at real scene content; `visual_detect.py` is unreliable on host BMP frames and must not be used as the host entry oracle.
- `BUILDING 1` PS1 `story single 10` remains in title/ocean flow for a long time; the first scene-like sampled frame in a 4200-frame probe is `frame_03660.png`.
- That same PS1 probe still does not match the canonical `BUILDING 1` host baseline by `frame_04200`; this is a real content mismatch, not just a missing-frames plumbing failure.
- A short `story direct 10` PS1 validation run still spends hundreds of frames in BIOS startup, so even the direct lane needs a larger boot budget than 600 frames.
- `story direct` is not an apples-to-apples baseline route. `story single` preserves the normal startup flow, while `story direct` bypasses it and adds a 600-frame hold tail by design.
- Fresh host `ACTIVITY 1` capture with engine markers reports `scene_start_frame = 7`, `scene_end_frame = 328`, `frames_captured = 328`. So host scene boundaries no longer need to be guessed visually.
- A cheap sampled PS1 scan for `ACTIVITY 1` shows first non-title content around frame `2700`, but it is `ocean`, not the host activity scene.
- The comparator now fails fast on missing anchors instead of hanging for minutes; remaining speed pain is reduced to long known-fail searches rather than general unusability.
- Fresh host-vs-host `ACTIVITY 1` comparison is an exact `MATCH` with `frame_offset = 0`, `common_frame_count = 328`, and `average_palette_index_diff_pixels = 0.0`. The comparator now has a real positive-control path.
- Long sampled PS1 run for `ACTIVITY 1` reaches `ocean` around frame `2700` and does not reach any host-matchable `ACTIVITY 1` scene even after entering `island` content around frame `6300`.
- Late-window compare against the long sampled PS1 run still reports `missing verified scene_entry anchor`, so the PS1 side is eventually showing the wrong island scene, not merely arriving late to the right one.

Next steps
1. Trace why PS1 `story single` boots are entering the wrong island scene for `ACTIVITY 1`.
2. Use `ACTIVITY 1` as the first live PS1 route/debug target until the requested scene is actually reached.
3. After that, revalidate `BUILDING 1` on the same harness and continue scene-by-scene.
4. Keep the harness stable and avoid further heuristic scene-entry work unless a new gap appears.


Progress update
- Fast direct validation lane shows PS1 still in BIOS/logo at ~frame 1500 and plain ocean at frames 2000-2400 for `story direct 10`.
- This strongly suggests a real PS1 scene-routing mismatch for `BUILDING 1`, not just harness timing error.
- The remaining harness gap is no longer host scene detection; it is PS1 entry-window search and comparator speed over long PS1 captures.
- With host scene markers in place, the next blocker is no longer “when does host scene begin?” but “why does PS1 `story single` land in ocean content for scenes like `ACTIVITY 1`?”
- Next debug target after the speed pass: trace PS1 story-route handling for `ACTIVITY 1` / `BUILDING 1` and fix that before deeper pixel work.
- Comparator confidence is now materially higher: host capture + host reference can round-trip to an exact match, so the remaining failures are on the PS1 path or in PS1-to-host alignment policy, not in basic compare mechanics.
- Practical conclusion: the harness is now good enough for scene debugging. The current blocker is a real PS1 route/content bug, first visible on `ACTIVITY 1`.
- Debugging moved into `ACTIVITY 1` as the first real PS1 scene bug target.
- `story single 0`, `story scene 0`, and exact `story ads ACTIVITY.ADS 1` all hit the same wrong ocean window by frame `4200`, so the failure is not scene-index mapping.
- Narrowing the old random empty-launch ADS retry from all families down to `BUILDING.ADS` did not change `ACTIVITY 1` behavior.
- Added robust current-ADS family/tag markers into the PS1 scene-marker strip so sampled frames can now tell us exactly what PS1 thinks is active during the bad window.
- Added current ADS family/tag telemetry to the PS1 scene-marker strip.
- First marker probe on `ACTIVITY 1` shows `current_ads_family=0` / `current_ads_tag=0` at frames `2700` and `4200`, with `ADD_SCENE` seen but `launched=false`.
- Long exact boot `story ads ACTIVITY.ADS 1` still does not reach host `ACTIVITY 1`.
- Sampled frames `2700` and `4200` remain in failed-launch ocean (`launched=false`, `family=0`, `tag=0`).
- By frames `6300` and `8700`, PS1 has transitioned into live island content with Johnny/raft/palm that visual detection classifies as FISHING-like, not ACTIVITY.
- So `ACTIVITY.ADS 1` on PS1 is not merely late; it eventually turns into the wrong scene family/content.
- Added requested-scene and boot-pending markers to the robust PS1 strip to separate story selection bugs from ADS launch bugs.
- Fixed `regtest-scene.sh` scene-label parsing so scratch labels with spaces no longer corrupt `ADS_NAME` / `SCENE_TAG` or break result summary generation.
- Switched requested/current ADS markers to white bit-cells for more robust decode.
- On the `ACTIVITY.ADS 1` bitmark probe, `boot_pending` stays asserted at frames `2700` and `4200` while the run remains in failed-launch ocean.
- That rules out early override consumption as the main remaining bug; the failure is downstream of story selection, inside PS1 ADS/TTM launch state.
- Enlarged the isolated black telemetry backdrop for requested/current ADS bit-cell rows so lower rows no longer bleed against live scene pixels.
- Added `ps1AdsDbgZeroIpLaunches` telemetry to distinguish tag-resolution failure from later immediate thread death.
- Added `zero_ip_launch` to the robust scene-marker decoder and reran an exact `story ads ACTIVITY.ADS 1` PS1 probe on the rebuilt image.
- Fresh decode at frames `2700` and `4200` shows:
  - `launched=false`
  - `add_scene=true`
  - `tag_hit=true`
  - `bmp_fail=true`
  - `sprite_seen=true`
  - `boot_pending=1`
  - `zero_ip_launch=true`
- However, authored bytecode inspection shows `ACTIVITY.ADS tag 1` starts with valid nonzero launches:
  - `slot 1 / GJDIVE.TTM / tag 12 / ip 42`
  - `slot 1 / GJDIVE.TTM / tag 13 / ip 126`
- That means the later `zero_ip_launch` signal in the ocean window is a downstream symptom, not proof that the initial `ACTIVITY 1` launch target is bad.
- The next debug question is no longer “does `ACTIVITY.ADS 1` target slot 0?” It is “why do the valid initial `GJDIVE` scene launches fail to survive into a live PS1 scene before the run drifts into unrelated content?”
- Added sticky robust-strip bits for the authored `ACTIVITY 1` startup launches:
  - `activity_init12_launched`
  - `activity_init13_launched`
  - `activity_init12_ended`
  - `activity_init13_ended`
- Fresh exact PS1 probe on the rebuilt image still reproduces the same failure:
  - title/transition noise around `300-600`
  - black by `900-1200`
  - wrong-ocean failure window by `2700` and `4200`
- In that bad ocean window, the new strip bits indicate PS1 is at least touching the authored `GJDIVE` startup path rather than a totally unrelated scene family from the outset:
  - `activity_init12_launched = true`
  - `activity_init13_launched = false` on the sampled decode
  - `activity_init12_ended = false`
  - `activity_init13_ended = false`
- The decode is still too coarse to treat the exact `12` vs `13` split as final truth, but it is enough to shift the hypothesis again:
  - PS1 is not simply ignoring `ACTIVITY.ADS 1`
  - it is entering the `ACTIVITY` launch path, then collapsing before a stable live scene forms
- Next target: instrument per-thread startup failure more directly around `GJDIVE.TTM` tags `12` and `13` so we can tell whether one of them is never started, immediately terminates, or is superseded by a later bad launch.
- Added live robust-strip bits for:
  - `activity_init12_running`
  - `activity_init13_running`
- Fresh exact probe with those rows shows:
  - at `300` and `600`: both `12` and `13` appear as launched/running during startup/title-path noise
  - at `2700` and `4200`: neither `12` nor `13` is running anymore
  - the bad ocean window still shows:
    - `add_scene=true`
    - `tag_hit=true`
    - `zero_ip_launch=true`
    - `bmp_fail=true`
    - `sprite_seen=true`
- So the current read is stronger now:
  - the authored `ACTIVITY` startup TTM threads do exist early
  - they are gone long before the later wrong-ocean window
  - the bad ocean state is a later broken relaunch/fallback condition, not the original `GJDIVE 12/13` scene simply staying alive in the wrong form
- Next target: identify what later `ADD_SCENE` / relaunch path is taking over after the initial `ACTIVITY` startup threads disappear.
- Added sticky robust-strip bits for later authored `ACTIVITY` relaunch edges:
  - `activity_tag7_launched`
  - `activity_tag8_launched`
  - `activity_tag9_launched`
  - `activity_tag11_launched`
  - `activity_tag14_launched`
  - `activity_slot2_tag2_launched`
- Fresh exact `story ads ACTIVITY.ADS 1` probe to `4200` with those rows:
  - [result.json](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-relaunchtrace/result.json)
  - At `frame_02700` the bad ocean window still shows:
    - `launched=false`
    - `add_scene=true`
    - `tag_hit=true`
    - `zero_ip_launch=true`
    - none of `activity_tag7/8/9/11/14` or `activity_slot2_tag2` are set
- Long exact probe to `9000` with `900`-frame sampling:
  - [result.json](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-relaunchtrace-long/result.json)
  - By `frame_06300` and still at `frame_08100`, the later wrong live scene shows:
    - `launched=true`
    - `tag_miss=true`
    - `zero_ip_launch=true`
    - `activity_tag11_launched=true`
    - `activity_slot2_tag2_launched=true`
    - `activity_tag7/8/9/14=false`
    - `activity_init12_running=true`
    - `activity_init13_running=true`
- This narrows the active PS1 bug again:
  - the bad ocean window at `2700-4200` happens before the later authored `ACTIVITY` relaunch chain is touched
  - by `6300+`, the authored relaunch chain has advanced at least as far as `ACTIVITY tag 11 -> slot 2 tag 2`
  - the eventual wrong live scene therefore appears during or after that later authored relaunch handoff, not during initial story selection and not during the first `GJDIVE 12/13` startup launches
- Next target: instrument the transition from `ACTIVITY tag 11 / slot2:2` into the later `zero_ip_launch` live scene takeover, because that is now the narrowest unexplained edge in the failure.
- Added later handoff liveness/end rows:
  - `activity_tag11_running`
  - `activity_slot2_tag2_running`
  - `activity_tag11_ended`
  - `activity_slot2_tag2_ended`
- Fresh rebuilt long probe:
  - [result.json](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-handofftrace-long/result.json)
- Decoded checkpoints:
  - `frame_02700`:
    - `add_scene=true`
    - `tag_hit=true`
    - `zero_ip_launch=true`
    - `activity_tag11_launched=false`
    - `activity_slot2_tag2_launched=false`
    - `activity_tag11_running=false`
    - `activity_slot2_tag2_running=false`
  - `frame_06300` and `frame_08100`:
    - `launched=true`
    - `tag_miss=true`
    - `zero_ip_launch=true`
    - `activity_tag11_launched=true`
    - `activity_slot2_tag2_launched=true`
    - `activity_tag11_running=false`
    - `activity_slot2_tag2_running=false`
    - `activity_tag11_ended=false`
    - `activity_slot2_tag2_ended=false`
- That tightens the read further:
  - by the later wrong-scene window, the authored `ACTIVITY` relaunch chain has definitely touched `tag11 -> slot2:2`
  - but those threads are no longer running there
  - and they are not being observed to terminate cleanly through `adsStopScene()`
  - the later wrong live scene therefore looks like a takeover after launched threads disappear without a normal observed stop, while `zero_ip_launch` continues to accumulate
- Next target: instrument where launched `ACTIVITY tag11 / slot2:2` threads are being invalidated or superseded without a normal stop, and connect that to the continuing `zero_ip_launch` takeover path.
- Dumped the authored reachable `ACTIVITY.ADS 1` chunk graph with the real parser:
  - entry chunk bookmarks `1:13`, `1:8`, `1:7`, `1:9`, `1:14`, `2:2`
  - direct chunk contents confirm the authored loop:
    - `1:13` randomly launches `1:8`, `1:7`, `1:9`
    - `1:8/7/9` can launch `1:14`
    - `1:14` launches `2:2`
    - `2:2` relaunches `1:13`
- Ran a denser transition probe:
  - [result.json](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-transitiontrace/result.json)
- Decoded transition band:
  - `frame_05400` through `frame_06000`:
    - `launched=false`
    - `add_scene=true`
    - `tag_hit=true`
    - `zero_ip_launch=true`
    - `activity_tag11_launched=false`
    - `activity_slot2_tag2_launched=false`
  - `frame_06150`:
    - `launched=true`
    - `tag_miss=true`
    - `zero_ip_launch=true`
    - `activity_tag11_launched=true`
    - `activity_slot2_tag2_launched=true`
    - neither is running anymore by the time of the sample
- That narrows the failure boundary again:
  - the later authored `ACTIVITY` relaunch loop first becomes visible between `6000` and `6150`
  - `zero_ip_launch` is already active well before that switch
  - so the zero-IP takeover is not a consequence of the `tag11/slot2:2` handoff; it predates it and persists through it
- Next target: instrument the exact launch happening in the `6000-6150` window and record the slot/tag/IP of the first live scene that appears once `launched=true` flips on.
- Found a real decoder bug in `scripts/decode-ps1-bars.py`: scene-marker rows were being scaled against `448` lines instead of native `480`, which vertically misaligned the robust strip decode on PS1 screenshots.
- After fixing the scene-marker scale to `img.height / 480.0`, the later handoff frame reads materially differently and more plausibly:
  - `frame_06150` now decodes as:
    - `launched = true`
    - `activity_tag11_running = true`
    - `activity_slot2_tag2_running = true`
    - `activity_live_slot2 = true`
    - `activity_live_ip_zero = true`
    - `activity_live_ip_nonzero = false`
  - the live-thread tag one-hots for `2`, `11`, and `13` are all false in that same frame
- Current read after the decoder fix:
  - the first visible live thread at the handoff is a `slot 2` thread with `ip == 0`
  - it is not one of the expected tag one-hots currently instrumented (`2`, `11`, `13`)
  - meanwhile the authored `ACTIVITY` handoff threads (`tag11` and `slot2:2`) are still marked as running in the same frame
- This narrows the PS1 bug again:
  - the bad takeover at the handoff is now consistent with a live `slot 2` zero-IP thread preempting the intended authored loop
  - the next probe should identify that live `slot 2` tag exactly, rather than continuing to infer from the old compact bitcells or noisy emulator logs

## 2026-03-28 12:59 PDT

- Found and fixed a real PS1 libc corruption source in [ps1_stubs.c](/home/hunter/workspace/jc_reborn/ps1_stubs.c):
  - `vprintf()` was using raw `vsprintf()` into a fixed `char buffer[1024]`
  - switched it to bounded `vsnprintf(buffer, sizeof(buffer), ...)`
- Validation result:
  - this fix was real but not sufficient by itself
  - the focused `ACTIVITY.ADS 1` rerun still eventually degraded into `UnknownReadHandler` / `UnknownWriteHandler` at `pc 0x8002D88C / 0x8002C998`
  - however, the corruption wave moved much later in wall-clock time, which proved the `vprintf` overflow was one contributor

## 2026-03-28 14:30 PDT

- Added later-runtime boot-state probes instead of early boot `printf`/`fatalError` traps.
- The compact boot bit-cell rows and the enlarged story bar panel are both too visually contaminated in live frames to trust numerically:
  - at `frame_02700` in the stable wrong-ocean window, the new boot-state rows still decode as zero
  - but the story bars saturate to junk widths like `90`, so those panels are not a reliable proof surface
- Switched to clean late yes/no traps in `story.c`:
  1. trap if `storyPlayPreparedScene(ACTIVITY.ADS, 1)` is entered and launch fails
  2. trap if `storyPlayPreparedScene(ACTIVITY.ADS, 1)` is entered at all
  3. trap if `storyPlay()` reaches the `storyBootAdsName/storyBootAdsTag` boot-scene branch for `ACTIVITY.ADS 1`
- Results:
  - none of those traps fired in stable exact PS1 runs
  - not by `2700` frames:
    - [launchfailtrap](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-launchfailtrap/result.json)
    - [bootbranchtrap](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-bootbranchtrap/result.json)
  - and not even by `6300` frames for the prepared-scene entry trap:
    - [entrytrap-6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-entrytrap-6300/result.json)
- This is the cleanest narrowing so far:
  - the stable PS1 path is not reaching `storyPlayPreparedScene(ACTIVITY.ADS, 1)`
  - and it is not even reaching the `storyPlay()` boot-scene resolution branch for `story ads ACTIVITY.ADS 1`
  - so the active root cause is upstream of ADS launch and upstream of story scene dispatch
- Current best hypothesis:
  - `BOOTMODE.TXT` is present on disc, but the `story ads ...` override is not being retained/applied by the runtime path that reaches `storyPlay()`
  - the next target is the PS1 boot-override load/reapply path, not ACTIVITY ADS internals

- Follow-up probes on the same branch:
  - Added a trap in `storyPlay()` itself to dump raw boot-state (`storyBootSingleSceneIndex`, `storyBootSceneIndex`, `storyBootAdsName`, `storyBootAdsTag`) on loop entry.
  - The run terminated extremely early with only 3 captured frames:
    - [storyloopbootstate](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-storyloopbootstate/result.json)
  - But, as with earlier very-early guest traps, the expected guest fatal text still did not surface in the extracted logs.
- Practical interpretation:
  - early/pre-dispatch guest fatal output remains an unreliable proof surface under the current regtest path
  - later negative traps remain trustworthy:
    - the stable path still does not reach `storyPlay()`'s boot-scene branch for `ACTIVITY.ADS 1`
    - and still does not reach `storyPlayPreparedScene(ACTIVITY.ADS, 1)` by `6300`
  - so the active bug remains upstream of story scene dispatch, in PS1 boot-override retention/application or in the transition from boot parsing into the story loop
- Then removed the remaining live PS1 scene-trace `printf()` traffic from the hot `ACTIVITY` path while preserving the strip markers and the hard zero-IP trap:
  - removed `[STORY] ...` boot/final prints from [story.c](/home/hunter/workspace/jc_reborn/story.c)
  - removed `[ADS] initial launch empty ...`, `[ADSPLAY] after chunk ...`, and `[ACT1LIVE] ...` prints from [ads.c](/home/hunter/workspace/jc_reborn/ads.c)
- This produced the first clean long `ACTIVITY 1` PS1 run without fatal corruption:
  - [result.json](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-quiettrace6300/result.json)
  - `frames = 6300`
  - `interval = 150`
  - `exit_code = 0`
  - `timed_out = false`
  - `frames_captured = 41`
  - `has_fatal_error = false`
- Clean visual timeline from that run:
  - `frame_02700` through `frame_06000`: still wrong `ocean`
  - `frame_06150` and `frame_06300`: wrong live `island` / `FISHING`-like scene
- Clean robust-strip decode at the handoff:
  - [frame_06000.png](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-quiettrace6300/20260328-125856/frames/jcreborn/frame_06000.png)
    - `scene_markers launched=no add_scene=no tag_hit=yes tag_miss=yes bmp_ok=no sprite_seen=yes`
  - [frame_06150.png](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-quiettrace6300/20260328-125856/frames/jcreborn/frame_06150.png)
    - `scene_markers launched=yes add_scene=yes tag_hit=no tag_miss=no bmp_ok=yes sprite_seen=no`
  - [frame_06300.png](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-quiettrace6300/20260328-125856/frames/jcreborn/frame_06300.png)
    - `scene_markers launched=yes add_scene=yes tag_hit=no tag_miss=no bmp_ok=yes sprite_seen=no`
- Current read after removing the corrupting trace path:
  - the earlier catastrophic memory corruption was at least partly self-induced by our PS1 `printf` tracing
  - the underlying `ACTIVITY 1` scene bug still remains, now exposed cleanly again
  - the real failure shape is:
    - failed-launch / wrong-ocean window through `6000`
    - then wrong live `FISHING`-like scene by `6150`
- Next target:
  - compare this clean `quiettrace6300` PS1 run directly against the canonical host `ACTIVITY-1` reference window
  - then resume debugging the actual `6000-6150` handoff with strip markers and narrow traps only, not live formatted PS1 logging

## 2026-03-28 13:02 PDT

- Completed the clean PS1-vs-host validation on the no-printf `ACTIVITY 1` run:
  - PS1 run:
    - [result.json](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-quiettrace6300/result.json)
  - compare result:
    - [/tmp/activity1-quiettrace6300-vs-host.json](/tmp/activity1-quiettrace6300-vs-host.json)
- Compare verdict:
  - `verdict = PIXEL_MISMATCH`
  - `frame_offset = 1800`
  - `common_frame_count = 3`
  - `reference_coverage_ratio = 0.008426966292134831`
  - `average_palette_index_diff_pixels = 126892.66666666667`
- Practical meaning:
  - the harness is now doing its job on a clean PS1 run
  - the PS1 `ACTIVITY 1` sequence only brushes the host scene window at entry, then diverges almost immediately
  - this is a real scene/content bug, not a remaining harness artifact
- Clean visual shape remains:
  - `frame_02700` through `frame_06000`: wrong `ocean`
  - `frame_06150` and `frame_06300`: wrong `island` / `FISHING`-like scene
- Current confidence statement:
  - host baseline path is trusted
  - sequence comparator is trusted for this scene
  - PS1 logging-induced corruption has been reduced enough that the real `ACTIVITY 1` failure is visible again
- Next target:
  - keep `ACTIVITY 1` as the first active PS1 fix
  - debug the actual `6000-6150` handoff on the clean quiet path, without restoring the old formatted PS1 trace traffic

## 2026-03-28 13:31 PDT

- Ran a widened `ACTIVITY 1` live-tag probe on the clean quiet path:
  - [result.json](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-livetags/result.json)
- The run completed cleanly through `6300` frames and kept the same visible failure shape:
  - `frame_06000`: wrong `ocean`
  - `frame_06150`, `frame_06300`: wrong live `island` scene
- Decoded robust-strip state at `frame_06150` / `frame_06300`:
  - `current_ads_family_estimate = 1`
  - `current_ads_tag_estimate = 1`
  - `launched = true`
  - `add_scene = true`
  - `zero_ip_launch = true`
  - `activity_live_slot2 = true`
  - `activity_live_ip_zero = true`
  - authored ACTIVITY live-tag one-hots remain all false for the currently instrumented tags:
    - `activity_live_tag7 = false`
    - `activity_live_tag8 = false`
    - `activity_live_tag9 = false`
    - `activity_live_tag12 = false`
    - `activity_live_tag14 = false`
    - `activity_live_tag2 = false`
    - `activity_live_tag11 = false`
    - `activity_live_tag13 = false`
- Practical read:
  - the wrong live scene still appears to be occurring inside `ACTIVITY.ADS tag 1`, not from a clean family switch
  - but the active live thread visible in the robust strip still does not match any authored ACTIVITY tag we were explicitly drawing
- Added a more direct overlay probe:
  - raw robust rows for exact `firstIdx.sceneSlot` low bits
  - raw robust rows for exact `firstIdx.sceneTag` low bits
  - decoder support for `live_scene_slot_robust` / `live_scene_tag_robust`
- That follow-up rerun did **not** stay clean:
  - [regtest.log](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-livetags-robust/regtest.log)
  - it regressed into heavy `UnknownReadHandler` spam at `pc 0x8002D9FC` before producing trustworthy postprocess output
- Current state:
  - harness remains good enough on the clean `quiettrace6300` / `livetags` path
  - `ACTIVITY 1` is still the first real scene bug
  - the next fix/debug target is still the ACTIVITY handoff itself, but the new exact-tag probe needs to be rerun in a way that preserves the clean runtime behavior

## 2026-03-28 13:40 PDT

- Reverted the invasive exact-tag overlay rows after they destabilized the runtime, and switched to one-shot engine traps instead.
- Two focused ACTIVITY probes completed cleanly:
  - [zeroip-launchtrap result](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-zeroip-launchtrap/result.json)
  - [startup-alive-trap result](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-startup-alive-trap/result.json)
- New stable behavior on both runs:
  - `frame_06000`, `frame_06150`, and `frame_06300` all remain wrong `ocean`
  - the earlier wrong-island handoff is not stable enough to treat as the real bug surface
- Important negative results:
  - the non-background zero-`ip` launch trap did **not** fire after frame `5000`
  - the startup-thread-alive trap for `ACTIVITY slot1 tag12/tag13` did **not** fire after frame `5000`
- Static authored check:
  - `GJDIVE.TTM tag 12` reaches `0x0110` and then `0x0FF0` almost immediately, so it should not live for thousands of frames
  - the fact that the engine trap did not see tag `12` or `13` still running means the older strip decode on those startup bits was over-reporting
- Current stable read:
  - harness is still at practical confidence
  - `ACTIVITY 1` does not reach the intended ACTIVITY scene content on PS1
  - the stable bug surface is now: booted ACTIVITY scene collapses back to ocean and stays there through `6300`
  - the immediate next target is no longer “later ACTIVITY handoff tag”; it is “why no later successful ACTIVITY launch happens at all on the stable run”

## 2026-03-28 13:46 PDT

- Pulled the authored startup graph directly from generated analysis and extracted bytecode:
  - `ACTIVITY.ADS tag 1` starts with `slot 1 tag 12` and `slot 1 tag 13`
  - later authored graph contains `slot 1 tags 7/8/9/14/11` and `slot 2 tag 2`
- Dumped `GJDIVE.TTM tag 12` from extracted content:
  - it reaches `0x0110` and then `0x0FF0` almost immediately
  - so it should terminate quickly, not remain the active long-lived state
- Added and ran a one-shot trigger trap in `adsPlayTriggeredChunks()` for `ACTIVITY slot1 tag12/tag13`:
  - [trigger-trap result](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-trigger-trap/result.json)
- Result:
  - trap did **not** fire
  - stable visual output remained wrong `ocean` through `6300`
  - no evidence that startup `tag 12/13` transitions are reaching the authored trigger path on the stable PS1 run
- Current read:
  - the stable ACTIVITY bug is now below later scene handoff logic
  - the next likely fault surface is the startup-thread termination / trigger pipeline itself:
    - either the startup thread is not terminating in the way the ADS scheduler expects
    - or the termination is happening without the expected `adsPlayTriggeredChunks(slot=1, tag=12/13)` handoff

## 2026-03-28 13:51 PDT

- Added and ran a one-shot termination trap on the ACTIVITY startup threads:
  - [terminate-trap result](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-terminate-trap/result.json)
- Result:
  - trap did **not** fire
  - run stayed wrong `ocean` through `6300`
  - visual/state hash remained on the same stable bad branch as the trigger-trap run
- Combined with the previous negative trigger result:
  - startup `tag 12/13` is not reaching `adsPlayTriggeredChunks(slot=1, tag=12/13)`
  - and it is also not entering the normal `ADS_THREAD_TERMINATED` branch in a way the engine sees
- Current narrowest read:
  - the ACTIVITY startup branch is being lost, overwritten, or bypassed before normal termination/trigger processing
  - next target should move earlier than `ADS_THREAD_TERMINATED`, likely around:
    - `ttmPlay()` changing `isRunning`
    - `nextGotoOffset` / `ip` evolution for `GJDIVE tag 12/13`
    - or direct thread-slot reuse before the expected termination path

## 2026-03-28 13:55 PDT

- Added a one-shot opcode trap directly inside `ttmPlay()` for ACTIVITY startup tags `12/13` at:
  - `0x0110` (`PURGE`)
  - `0x0FF0` (`UPDATE`)
- Reran the exact ACTIVITY boot:
  - [ttm-opcode-trap result](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-ttm-opcode-trap/result.json)
- Result:
  - trap did **not** fire
  - run again stayed on the same stable bad branch: wrong `ocean` through `6300`
- Combined with earlier negatives:
  - startup `tag 12/13` does not reach normal trigger handoff
  - does not enter the observed `ADS_THREAD_TERMINATED` branch
  - and now does not hit the expected authored early `PURGE` / `UPDATE` path in a way the runtime exposes
- Current best read:
  - the ACTIVITY startup thread is diverging before normal TTM control flow
  - next target must move even earlier than opcode-level lifetime handling:
    - startup thread `ip` initialization
    - TTM slot/data binding
    - or thread-slot reuse/corruption before the first expected `UPDATE`

## 2026-03-28 13:59 PDT

- Added the earliest startup-birth trap so `ACTIVITY slot1 tag12/tag13` would abort immediately after `adsAddScene()` assigns `ip`:
  - trap condition:
    - `ps1AdsCurrentName == ACTIVITY.ADS`
    - `ps1AdsCurrentTag == 1`
    - `ttmSlotNo == 1`
    - `ttmTag == 12 || ttmTag == 13`
  - emitted fields if hit:
    - `frame`
    - `idx`
    - `slot`
    - `tag`
    - `ip`
    - `dataSize`
    - `firstOpcode`
    - `arg3`
- Reran the exact scene on that build:
  - [addscene-birth-trap run](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-addscene-birth-trap)
- Result:
  - the birth trap did **not** fire
  - no `ACT1 addScene ...` fatal line appeared in `printf.log`, `regtest.log`, or extracted `tty-output.txt`
  - the run still completed capture/postprocess on the same stable bad branch
- This is stronger than the later negative traps:
  - startup `tag12/tag13` is not being observed through:
    - normal `adsAddScene()` birth on the expected ACTIVITY startup path
    - normal trigger handoff
    - normal `ADS_THREAD_TERMINATED`
    - normal early authored `PURGE` / `UPDATE`
- Current best read:
  - either the startup launch request is not reaching the expected `adsAddScene(slot1, tag12/13)` call at all on PS1
  - or `ps1AdsCurrentName/Tag` is already no longer the expected ACTIVITY context by the time the launch is born
- Next target:
  - move one layer earlier than thread birth
  - instrument the exact launch request state before `adsAddScene()` is called:
    - requested ADS family/tag
    - current chunk slot/tag being executed
    - requested `ttmSlotNo` / `ttmTag`
    - whether the ACTIVITY startup launch is being rewritten before thread creation

## 2026-03-28 14:03 PDT

- Added an even earlier callsite trap in `adsPlayChunk()` for authored startup launches:
  - `ADD_SCENE slot1 tag12/tag13`
  - `ADD_SCENE_LOCAL slot1 tag12/tag13`
  - trap payload includes:
    - `frame`
    - requested `slot/tag/arg3`
    - `inRandBlock`
    - `inSkipBlock`
    - current trigger slot/tag
- Reran the exact scene:
  - [addscene-callsite-trap run](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-addscene-callsite-trap)
- Result:
  - the callsite trap did **not** fire
  - stable run still completed through `6300`
  - no `ACT1 add_scene_chunk` or `ACT1 add_scene_local` line appeared in logs
- This strengthens the earlier negative traps:
  - on the stable PS1 path, the authored startup `ADD_SCENE slot1 tag12/tag13` opcodes are not being observed at all

## 2026-03-28 14:05 PDT

- Added a one-shot trap immediately after `adsLoad()` inside `adsPlay()` for:
  - `adsResource->resName == ACTIVITY.ADS`
  - `adsTag == 1`
  - trap payload would have printed:
    - selected `offset`
    - `dataSize`
    - `numTags`
    - first three opcodes at the chosen entry point
- Reran the exact scene:
  - [adsload-offset-trap run](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-adsload-offset-trap)
- Result:
  - the `adsLoad()` trap did **not** fire
  - run still exited cleanly with 41 captured frames
  - no `ACT1 adsLoad ...` line appeared in logs
- Current strongest read:
  - on the stable PS1 run, we are not even entering `adsPlay("ACTIVITY.ADS", 1)` on the path we thought we were
  - so the bug surface has moved out of ADS execution and back into the boot override / story dispatch path that is supposed to invoke `adsPlay`
- Next target:
  - instrument the story boot override path directly:
    - where `story ads ACTIVITY.ADS 1` is parsed
    - where that request is held/persisted
    - and the exact site that is supposed to call `adsPlay(boot_ads_name, boot_tag)`

## 2026-03-28 14:11 PDT

- Probed progressively earlier boot/dispatch sites for the same exact override:
  - `storySetBootScene("ACTIVITY.ADS", 1)`
  - `storyPlay()` boot-scene selection for `storyBootAdsName/storyBootAdsTag`
  - `ps1ApplyBootOverride()` token parse for `story ads ACTIVITY.ADS 1`
  - `ps1LoadBootOverride()` after successful BOOTMODE read
  - `ps1LoadBootOverride()` early-return branches (`CdSearchFile`, zero-size, `CdRead`, `CdReadSync`)
- Trap strings for all of those probes are definitely present in the built executable and in the rebuilt disc snapshot:
  - `build-ps1/jcreborn.exe`
  - snapshot `disc/jcreborn.bin`
- The rebuilt disc snapshot also definitely contains the staged override string:
  - `story ads ACTIVITY.ADS 1 seed 1`
- Yet none of those early boot traps fire at runtime, and no existing `[BOOT] ...` prints appear in regtest logs either.
- Practical conclusion:
  - for very-early pre-graphics PS1 boot code, `printf`/`fatalError`-style probes are no longer a trustworthy validation surface under the current regtest path
  - this does **not** invalidate the later scene harness, only the latest method of proving early-boot execution
- Next target:
  - switch early boot diagnostics away from print/fatal probes and onto a non-print side effect that can be observed later in the frame output or runtime mode
  - use that to prove whether `BOOTMODE.TXT` is actually being loaded/applied on PS1

## 2026-03-28 14:48 PDT

- Confirmed a critical code-path detail in `jc_reborn.c`:
  - `ps1HasBootOverridePending()` intentionally ignores `story ...` overrides
  - so `story ads ACTIVITY.ADS 1` always goes through the title path first
  - only `ps1ReapplyBootOverride()` can preserve that request for later story dispatch
- Tried three temporary late behavioral latches keyed on:
  - `ps1BootDbgStoryAdsApplied`
  - `ps1BootDbgAdsSig == ACTIVITY`
  - `ps1BootDbgAdsTag == 1`
- Latch attempts:
  - temporary `storyPlay()` reroute to `BUILDING.ADS 1`
  - temporary `main()` reroute to bench mode
  - temporary `main()` reroute to low-level `island ads BUILDING.ADS 1`
  - temporary `main()` hard early exit
- Validation runs:
  - [behavioral-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-behavioral-latch/result.json)
  - [main-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-main-latch/result.json)
  - [main-latch-ads](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-main-latch-ads/result.json)
  - [main-exit-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-main-exit-latch/result.json)
- Strongest result:
  - the `main()` hard-exit latch did **not** trigger
  - the run still followed the usual long path and dumped the normal sampled frames
  - so the `story ads ACTIVITY.ADS 1` request is not surviving far enough to trip a post-reapply latch in `main()`
- Caveat:
  - 2700-frame visual controls are weak because several different boots still look the same there
  - but the hard-exit latch is not visual, and its failure is the strongest proof in this set
- Current best read:
  - the active bug is upstream of story dispatch and likely still inside the PS1 boot-override retention/reapply path
  - specifically: `BOOTMODE.TXT` may be read, but the parsed `story ads ...` request is not surviving into the later runtime state we expected
- Next target:
  - inspect where story boot globals are cleared between `ps1ApplyBootOverride()` and the later `storyPlay()` path
  - focus on `storyResetBootState()` / `storySetBoot*()` callsites and any startup path that may be wiping story override state after reapply

## 2026-03-28 15:03 PDT

- Replaced the old RAM-only post-title reapply with a second `ps1LoadBootOverride()` after `loadTitleScreenEarly()` in [jc_reborn.c](/home/hunter/workspace/jc_reborn/jc_reborn.c).
- Validation:
  - old long failing probe:
    - [quiettrace6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-quiettrace6300/result.json)
  - new long probe with reload-after-title:
    - [reload-after-title-6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-reload-after-title-6300/result.json)
- Important result:
  - state hashes changed
  - but sampled PNGs at `2700`, `6000`, `6150`, and `6300` remained pixel-identical to the old failing run
- So the reload changes internal state retention, but it is not yet a visible scene fix.

- Then reran hard latches on the reload-after-title path:
  - top-of-`storyPlay()` boot-vars latch:
    - [story-loop-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-story-loop-latch/result.json)
    - fired
  - `storyBootAdsName/storyBootAdsTag` ads branch latch:
    - [boot-branch-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-boot-branch-latch/result.json)
    - fired
  - `storyBootSingleSceneIndex` hijack latch:
    - [single-index-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-single-index-latch/result.json)
    - did not fire
  - `storyBootSceneIndex` hijack latch:
    - [scene-index-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-scene-index-latch/result.json)
    - did not fire
  - non-null `bootScene = storyFindSceneByAds(...)` latch:
    - [bootscene-null-check](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-bootscene-null-check/result.json)
    - fired
  - later post-selection / prepared-scene latches:
    - [prepared-exit-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-prepared-exit-latch/result.json)
    - [postselect-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-postselect-latch/result.json)
    - did not fire

- Current narrow read:
  - with reload-after-title, the `story ads ACTIVITY.ADS 1` override now survives into `storyPlay()`
  - it reaches the ads boot branch
  - it resolves a non-null `bootScene`
  - it is not being hijacked by `storyBootSingleSceneIndex` or `storyBootSceneIndex`
  - but the path still does not survive to the later post-selection / prepared-scene launch boundary
- Next target:
  - inspect the exact control flow between a non-null `bootScene` and the later post-selection / prepared-scene path
  - this is now the narrowest unexplained edge for `ACTIVITY 1`

## 2026-03-28 15:13 PDT

- Probed deeper around the exact `bootScene -> finalScene -> storyPrepareSceneState()` handoff.
- Results:
  - `finalScene = bootScene` immediate latch:
    - [finalscene-assign-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-finalscene-assign-latch/result.json)
    - fired
  - top of `storyPrepareSceneState(ACTIVITY.ADS, 1)`:
    - [preparestate-entry-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-preparestate-entry-latch/result.json)
    - fired
  - after `storyCalculateIslandFromScene()` but before `adsInitIsland()`:
    - [pre-adsinitisland-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-pre-adsinitisland-latch/result.json)
    - fired
  - after `adsInitIsland()`:
    - [post-adsinitisland-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-post-adsinitisland-latch/result.json)
    - fired
- So:
  - `ACTIVITY.ADS 1` definitely survives through:
    - ads boot-branch lookup
    - `finalScene = bootScene`
    - `storyPrepareSceneState(finalScene)`
    - `storyCalculateIslandFromScene(finalScene)`
    - `adsInitIsland()`
- A same-run sticky-latch attempt that tried to carry “boot scene resolved” state forward to the later post-prepare block did **not** trip:
  - [sticky-resolve-latch](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-sticky-resolve-latch/result.json)
- Current best read:
  - the path definitely reaches and returns from `storyPrepareSceneState()`
  - but the later post-prepare point is still not being observed with the same proof method
  - so the remaining gap is now after island prep and before or during the later debug/state block inside `storyPlay()`
- Next target:
  - instrument the immediate span after `storyPrepareSceneState(finalScene)` with a more explicit side effect than the prior return-style latch
  - likely by storing a sticky runtime flag after `adsInitIsland()` and validating it from a later safe point or overlay decode instead of relying on local early returns

## 2026-03-28 15:34 PDT

- The harness remains at practical full confidence for scene debugging.
- `ACTIVITY 1` is still the active PS1 target, but the active bug surface has moved out of story routing and into the raft BMP load path during island prep.

- Exact narrowing chain:
  - `bootScene != NULL` survives immediately before `storyPrepareSceneState(finalScene)`:
    - [bootscene-nonnull-pre-prepare](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-bootscene-nonnull-pre-prepare/result.json)
    - fired
  - the same latch after `storyPrepareSceneState(finalScene)` did not fire:
    - [bootscene-nonnull-post-prepare](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-bootscene-nonnull-post-prepare/result.json)
    - did not fire
  - skipping `adsInitIsland()` made the later latch fire:
    - [skip-adsinitisland-post-prepare](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-skip-adsinitisland-post-prepare/result.json)
    - fired
  - so the first corrupting call is inside `adsInitIsland()`

- Then narrowed inside `adsInitIsland()` / `islandInit()`:
  - returning after screen-load/layer setup:
    - [island-cut-after-screenload](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-cut-after-screenload/result.json)
    - fired
  - returning after raft setup / before `BACKGRND.BMP`:
    - [island-cut-before-backgrnd](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-cut-before-backgrnd/result.json)
    - did not fire
  - returning immediately after `grLoadBmp(ttmSlot, 0, "MRAFT.BMP")`:
    - [island-cut-after-mraft-load](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-cut-after-mraft-load/result.json)
    - did not fire
- So the first bad edge is the raft BMP load itself.

- Then narrowed inside `grLoadBmpRAM("MRAFT.BMP")`:
  - forcing `MRAFT.BMP` off the PSB fast path did not help:
    - [mraft-no-psb-cut-after-load](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-no-psb-cut-after-load/result.json)
    - still bad
  - returning before any BMP-byte load did help:
    - [mraft-cut-before-bmp-load](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-cut-before-bmp-load/result.json)
    - fired
  - returning immediately after `ps1_loadBmpData()` did not:
    - [mraft-cut-after-bmp-load](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-cut-after-bmp-load/result.json)
    - still bad
- So the corrupting operation is in the raw BMP data load path, before any frame-install loop.

- Final split on the BMP data loader:
  - replacing `MRAFT.BMP` load with a same-sized dummy zeroed heap buffer:
    - [mraft-dummy-buffer-post-load-cut](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-dummy-buffer-post-load-cut/result.json)
    - fired
  - replacing it with `ps1_loadRawFile("\\BMP\\MRAFT.BMP;1", ...)`:
    - [mraft-rawfile-post-load-cut](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-rawfile-post-load-cut/result.json)
    - still bad

- Current best read:
  - the first bad operation for stable `ACTIVITY 1` is an actual PS1 CD-read path for `MRAFT.BMP`
  - it is not:
    - story override retention
    - scene dispatch
    - `adsInitIsland()` generally
    - `BACKGRND.BMP`
    - PSB decoding specifically
    - frame-install into the sprite slot
    - assigning a heap buffer to `bmpResource->uncompressedData`
  - it is specifically triggered by real CD-backed `MRAFT.BMP` loading

- Next target:
  - inspect/fix the low-level PS1 CD-read path used by `ps1_loadBmpData()` / `ps1_loadRawFile()`
  - likely around `CdRead` buffer handling / alignment / transfer semantics rather than scene logic

## 2026-03-28 15:38 PDT

- Continued narrowing the `ACTIVITY 1` failure after the raft-load boundary.
- The new question was whether the bad edge is specific to `MRAFT.BMP` or to any real CD-backed read at that point in scene prep.

- Additional splits:
  - forcing `MRAFT.BMP` off PSB and returning before any frame-install loop:
    - [mraft-no-psb-preloop-cut](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-no-psb-preloop-cut/result.json)
    - still bad
  - returning before any BMP-byte load at all:
    - [mraft-cut-before-bmp-load](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-cut-before-bmp-load/result.json)
    - fired
  - returning immediately after `ps1_loadBmpData()`:
    - [mraft-cut-after-bmp-load](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-cut-after-bmp-load/result.json)
    - still bad
- So the first bad operation is definitely in the raw BMP data load path, before any frame metadata/surface install.

- Then split the load path itself:
  - substitute a same-sized dummy zeroed heap buffer instead of any real read:
    - [mraft-dummy-buffer-post-load-cut](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-dummy-buffer-post-load-cut/result.json)
    - fired
  - substitute `ps1_loadRawFile("\\BMP\\MRAFT.BMP;1", ...)`:
    - [mraft-rawfile-post-load-cut](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-rawfile-post-load-cut/result.json)
    - still bad
  - substitute `ps1_loadRawFile("\\BMP\\BOAT.BMP;1", ...)` for the raft load:
    - [boat-rawfile-post-load-cut](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-boat-rawfile-post-load-cut/result.json)
    - still bad

- Current best read:
  - the active bug is no longer asset-specific to `MRAFT.BMP`
  - it is a generic real CD-read hazard at that exact scene-prep point
  - specifically:
    - any actual live PS1 CD-backed BMP read there corrupts later state
    - a same-sized heap allocation without CD I/O does not
  - so the next fix target is not scene logic or BMP parsing; it is the PS1 CD read path / DMA / buffer handling semantics used by `ps1_loadBmpData()` and `ps1_loadRawFile()`

- Next target:
  - test safer alternate CD read semantics around `CdRead` / `CdReadSync`, likely with a different buffering strategy or synchronization policy, then rerun `ACTIVITY 1`

## 2026-03-28 15:39 PDT

- Kept the same stable `ACTIVITY 1` proof surface and focused only on the low-level read semantics.

- New discriminators:
  - substitute `ps1_loadRawFile("\\BMP\\BOAT.BMP;1", ...)` instead of the raft load:
    - [boat-rawfile-post-load-cut](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-boat-rawfile-post-load-cut/result.json)
    - still bad
  - substitute a static-bounce-buffer reader that does real `CdRead` into static storage, then `memcpy` to heap:
    - [boat-bounce-post-load-cut](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-boat-bounce-post-load-cut/result.json)
    - still bad

- Current best read:
  - this is not `MRAFT.BMP`-specific
  - this is not a “DMA directly into malloc’d heap buffer” bug
  - the active failure is any real live `CdRead` at that scene-prep moment
  - so the next target is the PS1 CD state/synchronization path itself:
    - `CdControl(CdlSetloc, ...)`
    - `CdRead(...)`
    - `CdReadSync(...)`
    - possible interaction with current engine state / callbacks / timing

- Additional check:
  - inserting `cdromResetState()` immediately before the live special-case read:
    - [boat-bounce-post-load-cut-cdreset](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-boat-bounce-post-load-cut-cdreset/result.json)
    - still bad

- Updated read:
  - this is not merely stale CD state left over from earlier boot/title work
  - the next target is the live `CdRead`/`CdReadSync` semantics themselves rather than reset ordering

- One more discriminator:
  - switching the special-case live read to the legacy `ps1_fopen("BMP\\BOAT.BMP", "rb")` / `ps1_fread(...)` whole-file path:
    - [boat-ps1fopen-post-load-cut](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-boat-ps1fopen-post-load-cut/result.json)
    - still bad

- Current best read:
  - it is not a bug isolated to `ps1_streamRead()`
  - it is not a bug isolated to `ps1_loadRawFile()`
  - it is not a bug isolated to pack reads vs fallback reads
  - it is not a bug isolated to DMA directly into heap
  - it is any live CD-backed read path at that scene-prep moment

- Next target:
  - stop trying alternate live readers
  - shift to an architectural fix:
    - prefetch/cache needed BMP bytes before entering the sensitive scene-prep window, or
    - otherwise guarantee `ACTIVITY 1` island prep does not issue live CD reads there

## 2026-03-28 16:08 PDT

- Tried the architectural fix in two steps:
  - added island-support BMP priming (`MRAFT.BMP`, `BACKGRND.BMP`, `HOLIDAY.BMP`) to `adsPrimeRestorePilotResources()` for any restore pilot that uses `ISLETEMP.SCR`
  - then moved priming earlier into `storyPrepareSceneState()` and prearmed the pilot pack before priming

- Validation runs:
  - [prefetch-6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-prefetch-6300/20260328-155810/frames/jcreborn/frame_06150.png)
  - [prepprime-6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-prepprime-6300/20260328-160137/frames/jcreborn/frame_06150.png)
  - [prearm-prime-6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-prearm-prime-6300/20260328-160314/frames/jcreborn/frame_06150.png)

- Result:
  - none of those three ACTIVITY 1 runs changed the late failure band
  - frames `6000`, `6150`, and `6300` stayed on the same wrong ocean output
  - so the naive “prime earlier” hypothesis is not sufficient by itself

- Follow-up probe:
  - added a temporary `islandInit()` residency trap around `MRAFT.BMP`
  - short proof run:
    - [mraft-fatalproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-mraft-fatalproof/result.json)
  - this did **not** trip the late fatal by `3000` frames
  - sampled visuals on that run were title/black rather than the old ocean proof surface

- Current best read:
  - the harness remains good enough for scene debugging
  - `ACTIVITY 1` is still the active scene
  - the early-prime fix did not solve the issue
  - the next unresolved edge is no longer just “load MRAFT earlier”; it is the exact scene/boot context that exists when island prep is attempted

- Immediate next target:
  - replace the temporary `islandInit()` residency trap with a cleaner proof of the active ADS/tag/context at island-prep time
  - then determine whether the wrong-ocean path is entering island prep under the wrong scene identity, or whether the resource residency is still being lost before use

## 2026-03-28 16:18 PDT

- Replaced the failed overlay/decoder angle with a stronger runtime proof:
  - temporarily forced a distinctive `ACTIVITY 1`-only island state in `storyPrepareSceneState()`:
    - `night = 1`
    - `holiday = 3`

- Validation run:
  - [nightproof-6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-nightproof-6300/20260328-161502/frames/jcreborn/frame_06150.png)

- Important result:
  - the old wrong bright-cyan day ocean at `6000/6150/6300` changed into a night-ocean variant
  - so the visible bad path is definitely carrying the prepared `ACTIVITY 1` island state forward
  - this rules out the hypothesis that the wrong-ocean branch is some unrelated post-title scene path ignoring `storyPrepareSceneState(finalScene)`

- Current best read:
  - `ACTIVITY 1` background/island state prep is being applied
  - but the actual scene content / sprite-thread side is still not launching or composing correctly
  - so the remaining bug is now narrower again:
    - not story boot routing
    - not final-scene selection
    - not island-state preparation
    - still in the transition from prepared scene state into live ADS/TTM scene content

- Cleanup:
  - reverted the temporary night/holiday proof patch after recording the result

- Next target:
  - return to the clean `ACTIVITY 1` path and debug why prepared ACTIVITY background state survives while the ACTIVITY scene threads/content still collapse into empty ocean

## 2026-03-28 16:21 PDT

- Removed the remaining intrusive ACTIVITY-specific fatal traps from `ads.c` so the runtime proof surface is passive again:
  - zero-IP launch abort
  - trigger abort
  - startup-thread alive/terminated aborts
  - slot-2 zero-IP handoff aborts

- Clean validation run:
  - [cleantelemetry-6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-cleantelemetry-6300/20260328-161912/frames/jcreborn/frame_06150.png)

- Result:
  - removing those ACTIVITY-specific aborts did **not** change the visible scene behavior
  - frame `6150` is back to the same bright day-ocean failure shape as before
  - so the remaining bug is not an artifact of the old ACT1 fatal instrumentation

- Current best read:
  - harness is still at practical full confidence
  - the prepared-scene state can be proven to survive
  - the cleaned runtime still collapses to empty ocean instead of real ACTIVITY scene content
  - next target remains the live ADS/TTM content path rather than story boot or island-state prep

## 2026-03-28 16:30 PDT

- Added two temporary late proofs for the clean `ACTIVITY 1` path:
  - in `story.c`, after `storyPlayPreparedScene(finalScene, ...)`, fatal if `ACTIVITY.ADS 1` returns with `ps1AdsLastPlayLaunched == 0`
  - in `ads.c`, inside the `adsPlay()` main loop, fatal if `ACTIVITY.ADS 1` is still running past frame `5400` with `ps1AdsLastPlayLaunched == 0`

- Validation runs:
  - [launchproof-6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-launchproof-6300/result.json)
  - [adsloop-launchtrap](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-adsloop-launchtrap/result.json)

- Result:
  - neither proof fired
  - both runs still landed on the same stable late-ocean failure band:
    - `frame_05400` through `frame_06300`
  - `scene_markers_last.launched` remained `false` in both runs

- Important narrowing:
  - the visible bad path is not simply:
    - `storyPlayPreparedScene()` returning immediately with no launch, or
    - `adsPlay()` staying live into the ocean band with no launch
  - so the remaining false-`launched` state is being reached through a different path than those two proof sites capture

- Cleanup:
  - reverted both temporary proof guards after recording the negative result

- Next target:
  - move the proof surface away from those two sites and identify where the late ocean path is actually re-entering/retrying scene control while leaving `ps1AdsLastPlayLaunched` false

## 2026-03-28 16:35 PDT

- Tested the most obvious remaining re-entry hypothesis directly in `storyPlay()`:
  - temporary proof patch on the `if (!ps1AdsLastPlayLaunched)` retry branch
  - for `ACTIVITY.ADS 1`, force a visible island-state mutation there:
    - `night = 1`
    - `holiday = 3`

- Validation run:
  - [retrybranch-nightproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-retrybranch-nightproof/result.json)

- Result:
  - the late `5400–6300` ocean frames did **not** change at all
  - same day-ocean output
  - same `state_hash`
  - same false `launched` marker

- Important narrowing:
  - the stable late-ocean band is not coming from the obvious `storyPlay()` failed-launch retry branch either
  - combined with the earlier negative proofs, the bad path is bypassing:
    - the post-`storyPlayPreparedScene()` failed-launch branch
    - the late `adsPlay()` in-loop no-launch proof
    - the obvious `storyPlay()` failed-launch retry branch side effect

- Cleanup:
  - reverted the temporary retry-branch proof patch after recording the result

- Next target:
  - move one layer outside of `storyPlay()` control-flow assumptions and identify what code path is drawing the late ocean band while all three expected no-launch branches fail to explain it

## 2026-03-28 16:40 PDT

- Tested whether the late ACTIVITY ocean band was being sustained by the persistent island background thread itself:
  - temporary proof patch in `storyPrepareSceneState()`
  - for `ACTIVITY.ADS 1`, call `adsReleaseIsland()` immediately after `adsInitIsland()`

- Validation run:
  - [noislandbg-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-noislandbg-proof/result.json)

- Result:
  - no change at all
  - same late ocean frames
  - same `state_hash`
  - same false `launched` marker

- Important narrowing:
  - the late bad band is not being sustained by the background/wave thread
  - it is a static ocean background already baked into `grBackgroundSfc` during island prep
  - the real missing piece is still the ACTIVITY scene content layer, not the island background thread lifetime

- Cleanup:
  - reverted the temporary `adsReleaseIsland()` proof patch after recording the result

- Next target:
  - focus directly on why ACTIVITY scene content never composites over that already-prepared ocean background

## 2026-03-28 16:45 PDT

- Proved the late ACTIVITY ocean band is not receiving live sprite draws at all:
  - temporary fatal traps added to:
    - `grDrawSprite()`
    - `grDrawSpriteFlip()`
  - only for `ACTIVITY.ADS 1` after frame `5400`

- Validation run:
  - [latedraw-trap](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-latedraw-trap/result.json)

- Result:
  - neither draw trap fired
  - the run still reached the same stable late-ocean band unchanged
  - so by `5400–6300`, no ACTIVITY scene sprite draw is reaching the renderer at all

- Important narrowing:
  - the missing ACTIVITY content layer is failing before `grDrawSprite()` / `grDrawSpriteFlip()`
  - the ocean band is just the prepared static background with no live content ops arriving in the late window

- Cleanup:
  - reverted the temporary late draw traps after recording the result

- Next target:
  - move one layer earlier than rendering:
    - determine why `ttmPlay()` for the supposed ACTIVITY content is no longer issuing `DRAW_SPRITE` opcodes by the late window

## 2026-03-28 16:50 PDT

- Moved one layer earlier than rendering and proved no ACTIVITY opcode execution reaches the late window either:
  - temporary fatal trap at the entry of `ttmPlay()`
  - only for `ACTIVITY.ADS 1` after frame `5400`

- Validation run:
  - [latettmp-trap](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-latettmp-trap/result.json)

- Result:
  - the `ttmPlay()` trap did **not** fire
  - the run still reached the same stable late-ocean band unchanged

- Important narrowing:
  - by `5400–6300`, no ACTIVITY thread is even reaching opcode execution anymore
  - combined with the earlier late draw proof, the ACTIVITY content layer is dead before:
    - `ttmPlay()`
    - `grDrawSprite()`
    - `grDrawSpriteFlip()`

- Cleanup:
  - reverted the temporary `ttmPlay()` trap after recording the result

- Next target:
  - move earlier again to the thread lifecycle itself:
    - why the ACTIVITY scene threads have vanished before the late window while the prepared static ocean background remains on screen

## 2026-03-28 16:59 PDT

- Added a sticky `ps1StoryDbgBootReturnSeen` proof bit for the `if (bootScene != NULL) { ... return; }` success-return branch in `storyPlay()`.
- First attempted to expose that bit in the top-left drop panel, but that surface was contaminated by unrelated lit pixels earlier in the run and was not trustworthy.
- Moved the same proof bit into the compact scene-identity strip and decoded it directly on the late ACTIVITY frame.

- Validation runs:
  - [bootreturn-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-bootreturn-proof/result.json)
  - [bootreturn-compactproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-bootreturn-compactproof/result.json)

- Clean result:
  - at `frame_06150`, `boot_return_seen_estimate = 0`
  - so the late ocean band is **not** post-return residue from the `bootScene != NULL` success path

- Important narrowing:
  - the stable late bad band is still being reached before the boot-scene success-return branch
  - that rules out one of the last obvious “scene already ended and left its background behind” explanations

## 2026-03-28 17:02 PDT

- Added sticky ACTIVITY attempt counters:
  - `ps1StoryDbgActivityPrepareCount`
  - `ps1StoryDbgActivityPlayPreparedCount`
- The compact-strip decode for those counters was too noisy to trust numerically, so I switched back to a behavioral proof.

- Behavioral proof:
  - only on the **second** `storyPrepareSceneState(ACTIVITY.ADS, 1)` call, forced `islandState.night = 1` and `holiday = 3`

- Validation run:
  - [second-prepare-nightproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-second-prepare-nightproof/result.json)

- Result:
  - the late `5400–6300` ocean stayed the same bright day ocean
  - no night mutation appeared

- Important narrowing:
  - the stable bad path is not repeatedly re-preparing `ACTIVITY 1` before the late window
  - current best read is now:
    - a single ACTIVITY scene-prep/background-prep pass survives
    - live ACTIVITY content still dies before late `ttmPlay()` and late draw
    - and the engine has not yet taken the normal boot-scene success-return path by the late sampled window

- Cleanup:
  - reverted the temporary second-prepare nightproof after recording the result

- Next target:
  - prove whether `storyPlayPreparedScene(finalScene, ...)` is being entered on the stable ACTIVITY path in the current clean runtime, using a similarly late-safe behavioral proof rather than a noisy counter decode

## 2026-03-28 17:06 PDT

- Tried a simpler ACTIVITY entry proof by exposing sticky booleans for:
  - `storyPrepareSceneState(ACTIVITY.ADS, 1)` seen
  - `storyPlayPreparedScene(ACTIVITY.ADS, 1)` seen
- The compact-strip decode for those added rows still overlapped other overlay content and was not reliable enough to trust numerically.

- Switched back to the behavior surface that already proved reliable:
  - temporary mutation at the top of `storyPlayPreparedScene(ACTIVITY.ADS, 1)`:
    - force `night/holiday`
    - rebuild island via `adsReleaseIsland(); adsInitIsland();`

- Validation run:
  - [playprepared-nightproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-playprepared-nightproof/result.json)

- Result:
  - late `5400–6300` output stayed the same bright day ocean
  - no night mutation appeared

- Important narrowing:
  - on the stable clean ACTIVITY failure path, the renderer-visible late ocean is not coming through `storyPlayPreparedScene(finalScene, ...)`
  - combined with the earlier proofs:
    - it is not a repeated second prepare
    - it is not the boot-scene success-return residue
    - it is not late `ttmPlay()` / late draw activity

- Current best read:
  - the bad ACTIVITY path is diverging before the normal prepared-scene playback function
  - but after enough ACTIVITY-specific scene preparation/background work has already happened to leave the static ocean in place

- Cleanup:
  - reverted the temporary `storyPlayPreparedScene()` nightproof after recording the result

- Next target:
  - identify the pre-`storyPlayPreparedScene()` path that can leave ACTIVITY-prepared ocean background on screen while never entering the normal prepared-scene playback path

## 2026-03-28 17:09 PDT

- Tested whether the stable bad ACTIVITY path is still using the resolved `bootScene != NULL` / `finalScene` preparation path at all.
- Temporary behavioral proof:
  - immediately after `storyPrepareSceneState(finalScene)`, only when:
    - `bootScene != NULL`
    - `finalScene == ACTIVITY.ADS tag 1`
    - `finalScene` is an island scene
  - force `night/holiday`
  - rebuild island via `adsReleaseIsland(); adsInitIsland();`

- Validation run:
  - [bootscene-nightproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-bootscene-nightproof/result.json)

- Result:
  - late `5400–6300` output stayed the same bright day ocean
  - no night mutation appeared

- Important narrowing:
  - the stable bad ACTIVITY path is not the normal resolved `bootScene/finalScene` preparation path either
  - combined with earlier proofs, the visible late ocean is now outside all of these expected sites:
    - boot-scene success return
    - repeated second prepare
    - normal `storyPlayPreparedScene(finalScene, ...)`
    - normal resolved `bootScene/finalScene` island-prep mutation point

- Cleanup:
  - reverted the temporary `bootScene/finalScene` nightproof after recording the result

- Current best read:
  - the late ACTIVITY bad band is being reached through an earlier or parallel path that still leaves an ACTIVITY-like prepared ocean background in place, but bypasses the normal boot-scene/final-scene playback and return surfaces we have tested

- Next target:
  - step out of `story.c` proofs and instrument who is writing the late ocean background surface (`grBackgroundSfc`) in the ACTIVITY failure path, since the control-flow proofs in the normal scene path are now mostly exhausted

## 2026-03-28 17:15 PDT

- Switched proof surface from `story.c` control flow to the actual background writer in `island.c`.
- Added a helper in `story.c` / [story.h](/home/hunter/workspace/jc_reborn/story.h):
  - `storyBootOverrideMatches(const char *adsName, uint16 adsTag)`
- First writer proof:
  - only under boot override `ACTIVITY.ADS 1`, force `islandState.night = 1`, `holiday = 3` at the top of `islandInit()`
- Validation run:
  - [islandinit-nightproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-islandinit-nightproof/result.json)
- Result:
  - the late bad band changed completely
  - state hash changed from the old stable ocean hash
- Important conclusion:
  - the late ACTIVITY failure surface is definitely being produced through `islandInit()`
  - this is not just stale framebuffer residue or a path entirely outside island background setup

## 2026-03-28 17:17 PDT

- Narrowed `islandInit()` one more step:
  - for boot override `ACTIVITY.ADS 1`, return immediately after the initial `grLoadScreen(...)`
  - skip all later raft/cloud/island compositing in `islandInit()`

- Validation run:
  - [islandinit-loadonly-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-islandinit-loadonly-proof/result.json)

- Result:
  - the late `5400–6300` bad band stayed **exactly the same** as the original stable failure:
    - same state hash
    - same bright day-ocean frames

- Important conclusion:
  - the later raft/cloud/island compositing in `islandInit()` is **not** required for the stable ACTIVITY failure
  - the initial `grLoadScreen("OCEAN0?.SCR")` done by `islandInit()` is sufficient by itself to produce the late bad surface

- Cleanup:
  - reverted the temporary early return after `grLoadScreen(...)`

- Current best read:
  - the stable late ACTIVITY failure is now tightly narrowed to the ocean screen load path itself
  - the wrong surface is being baked in at `grLoadScreen(...)`, before later island composition or normal scene playback ever matters

- Next target:
  - instrument why `islandInit()` is reaching the `OCEAN0?.SCR` load on the ACTIVITY failure path, and whether the intended ACTIVITY-specific screen/background choice is ever being selected before that fallback ocean load happens

## 2026-03-28 17:20 PDT

- Added a direct ACTIVITY ADS-entry proof in `adsPlay()`:
  - while testing, if `adsPlay("ACTIVITY.ADS", 1)` is entered, force `night/holiday`

- Validation run:
  - [adsplay-nightproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-adsplay-nightproof/result.json)

- Result:
  - the late `5400–6300` bad band did **not** change at all

- Important conclusion:
  - the stable late ACTIVITY failure is not coming through `adsPlay("ACTIVITY.ADS", 1)` entry itself

## 2026-03-28 17:22 PDT

- Then tested a broader story-level hypothesis:
  - while boot override `ACTIVITY.ADS 1` is still pending,
  - mutate **any** island `storyPrepareSceneState(scene)` call to `night/holiday`

- Validation run:
  - [any-prepare-nightproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-any-prepare-nightproof/result.json)

- Result:
  - the late bad band changed completely

- Important conclusion:
  - the stable late failure is still reaching some island-scene preparation while the ACTIVITY override remains pending
  - but it is not necessarily the ACTIVITY scene itself

## 2026-03-28 17:23 PDT

- Narrowed that branch again:
  - mutate only when:
    - `bootScene == NULL`
    - `storyBootOverrideMatches("ACTIVITY.ADS", 1)`
    - `finalScene->flags & ISLAND`
  - before `storyPrepareSceneState(finalScene)`

- Validation run:
  - [bootscene-null-nightproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-bootscene-null-nightproof/result.json)

- Result:
  - the late bad band did **not** change

- Important read:
  - the stable path is not explained by the simple `bootScene == NULL` random-final-scene branch alone
  - but it also is not entering `adsPlay("ACTIVITY.ADS", 1)` directly
  - and `story ads ...` is confirmed to still route through `storyPlay()`, not `storyPlayBootSceneDirect()`

- Current best read:
  - the remaining control-flow suspect is now the early PS1 title/boot gating in `jc_reborn.c`, especially:
    - `ps1HasBootOverridePending()`
    - override reload/application timing around title startup

- Next target:
  - test whether allowing `story ads` to count as a pending boot override changes the ACTIVITY failure shape, since that is now the cleanest unresolved gating edge

## 2026-03-28 17:29 PDT

- Tested the early PS1 title/boot gating hypothesis directly in [jc_reborn.c](/home/hunter/workspace/jc_reborn/jc_reborn.c):
  - changed `ps1HasBootOverridePending()` so `story ...` overrides count as pending too
  - this suppresses the early title preload gate for `story ads ACTIVITY.ADS 1`

- Validation run:
  - [pendinggate-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-pendinggate-proof/result.json)

- Result:
  - this does change the early shape materially:
    - earlier frames that used to show title now go black sooner
    - the late ocean starts earlier (`04950` instead of `05400`)
  - but the stable late bad band is still the same ocean hash:
    - `59cd749f...`
  - and host compare is still a hard mismatch:
    - [/tmp/activity1-pendinggate-vs-host.json](/tmp/activity1-pendinggate-vs-host.json)
    - verdict still `PIXEL_MISMATCH`

- Comparison to old clean baseline:
  - [/tmp/activity1-old-vs-host.json](/tmp/activity1-old-vs-host.json)
  - host mismatch is unchanged in substance

- Important conclusion:
  - the title/boot gate is a real factor in startup shape
  - but it is not the root fix for `ACTIVITY 1`
  - the remaining bug still lies later, in how the pending ACTIVITY override interacts with subsequent scene preparation / scene selection after startup

- Current best read:
  - `story ads` should probably count as a pending boot override for control-flow correctness
  - but even with that fix, the ACTIVITY path still falls into the same wrong late ocean state

- Next target:
  - keep the pending-gate result in mind, but continue tracing the later handoff between startup and island preparation, because that is still where ACTIVITY correctness is lost

## 2026-03-28 17:36 PDT

- Switched from branch proofs back to family proofs inside `storyPrepareSceneState(scene)` while the pending override is still `ACTIVITY.ADS 1`.
- First tested `WALKSTUF.ADS` only:
  - [walkstuf-prepare-nightproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-walkstuf-prepare-nightproof/result.json)
- Then tested `FISHING.ADS` only:
  - [fishing-prepare-nightproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-fishing-prepare-nightproof/result.json)

- Result:
  - neither changed the stable late ocean band
  - both preserved the same ocean hash `59cd749f...`

- Important conclusion:
  - the earlier “any island prepare while ACTIVITY override is pending” proof is not explained by either:
    - `WALKSTUF.ADS` island prep
    - `FISHING.ADS` island prep

- Current best read:
  - either another island family is being prepared under the still-pending ACTIVITY override
  - or the broader “any prepare” proof is interacting with shared state in a way that is not family-local

- Cleanup:
  - reverted the temporary family-specific proof patch after recording both results

- Next target:
  - move away from heuristic family guesses and identify the exact prepared scene family/tag through a more deterministic state surface, rather than guessing by visual classifier labels

## 2026-03-28 17:44 PDT

- Added exact prepared-scene debug fields in [story.c](/home/hunter/workspace/jc_reborn/story.c):
  - `ps1StoryDbgPreparedSceneAdsSig`
  - `ps1StoryDbgPreparedSceneTag`
- First tried exposing them in the legacy story counter-bar panel in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c) and decoding them via [scripts/decode-ps1-bars.py](/home/hunter/workspace/jc_reborn/scripts/decode-ps1-bars.py).

- Validation run:
  - [preparedtrace](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-preparedtrace/result.json)

- Result:
  - the legacy story panel rows saturated to full width (`90`) on the late frames
  - so that panel is not a trustworthy decode surface for exact prepared-scene identity

- Follow-up:
  - moved the same prepared-scene fields into the compact white-bit scene-marker strip in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c)
  - extended [scripts/decode-ps1-bars.py](/home/hunter/workspace/jc_reborn/scripts/decode-ps1-bars.py) to decode:
    - `prepared_scene_ads_family_estimate`
    - `prepared_scene_tag_estimate`

- Validation rerun:
  - [preparedstrip](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-preparedstrip/result.json)

- Late-frame decode on the compact strip:
  - `frame_06000`: `prepared=0/0`, `requested=63/0`, `boot_pending=1`
  - `frame_06150`: `prepared=0/0`, `requested=63/0`, `boot_pending=1`
  - `frame_06300`: `prepared=0/0`, `requested=63/0`, `boot_pending=1`

- Important conclusion:
  - the new prepared-scene field is consistent with “no prepared scene identity survives into the late ocean band”
  - but the adjacent requested-family bits are still saturating (`63`) on the same compact surface
  - so the compact strip is not yet clean enough to treat the exact family/tag values as final truth

- Current best read:
  - the ACTIVITY failure still looks like “prepared/background state without surviving scene playback”
  - and the next barrier is now the proof surface itself: we need one exact scene-identity signal that is robust under late-frame scaling/filtering, not another branch guess

- Next target:
  - switch from compact multi-bit family/tag encoding to a simpler, one-hot or low-cardinality exact prepared-scene proof surface that cannot saturate into ambiguous `63` values under DuckStation scaling

## 2026-03-28 17:51 PDT

- Replaced the ambiguous compact prepared-scene family/tag decode with one-hot prepared-family rows in the robust scene-marker strip:
  - `prepared_activity1`
  - `prepared_activity`
  - `prepared_building`
  - `prepared_fishing`
  - `prepared_johnny`
  - `prepared_mary`
  - `prepared_miscgag`
  - `prepared_stand`
  - `prepared_suzy`
  - `prepared_visitor`
  - `prepared_walkstuf`

- Validation rerun:
  - [preparedfamily](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-preparedfamily/result.json)

- Late-frame decode:
  - `frame_06000`: all prepared-family one-hots `0`, `boot_pending=1`
  - `frame_06150`: all prepared-family one-hots `0`, `boot_pending=1`
  - `frame_06300`: all prepared-family one-hots `0`, `boot_pending=1`

- Whole sampled run scan:
  - the one-hot family rows only light during the early noisy startup frames (`00300`, `00450`, `00600`), where they all saturate simultaneously and are not trustworthy
  - after that, no prepared-family row lights again
  - `boot_pending` remains asserted from `04950` through `06300`

- Important conclusion:
  - on the stable ACTIVITY failure path, there is no trustworthy evidence that `storyPrepareSceneState()` reaches any concrete island family during the late bad window
  - the bug target moves earlier again:
    - the override remains pending
    - but stable scene preparation is not being reached on the path that leads to the late ocean band

- Current best read:
  - the late ocean band is still real and stable
  - but it is no longer defensible to describe it as “late prepared scene X”
  - instead, the stronger statement is:
    - `boot_pending` survives
    - normal prepared-scene identity does not

- Next target:
  - stop trying to identify a late prepared family
  - instrument the handoff that should consume the pending override and enter `storyPrepareSceneState()` on the stable path, because that handoff is now the narrowest unexplained edge

## 2026-03-28 17:57 PDT

- Added sticky handoff markers for the `ACTIVITY.ADS 1` pending-override path in [story.c](/home/hunter/workspace/jc_reborn/story.c):
  - `pending_boot_resolved`
  - `pending_prepare`
  - `pending_play_prepared`
  - `pending_play_prepared_returned`
- Exposed those as robust white-block rows in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c) and decoded them in [scripts/decode-ps1-bars.py](/home/hunter/workspace/jc_reborn/scripts/decode-ps1-bars.py).

- Validation rerun:
  - [pendinghandoff](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-pendinghandoff/result.json)

- Late-frame decode:
  - `frame_06000`: all handoff markers `0`, `boot_pending=1`
  - `frame_06150`: all handoff markers `0`, `boot_pending=1`
  - `frame_06300`: all handoff markers `0`, `boot_pending=1`

- Whole sampled run scan:
  - all four sticky handoff markers light only during the early noisy startup frames (`00300`, `00450`, `00600`)
  - after that, none of them light again
  - `boot_pending` remains asserted from `04950` through `06300`

- Important conclusion:
  - on the stable ACTIVITY failure path, there is still no trustworthy evidence that the pending override reaches:
    - resolved boot-scene handoff
    - stable prepare entry
    - stable play-prepared entry
    - stable play-prepared return
  - the only late surviving signal is still the pending override itself

- Current best read:
  - the stable failure path is earlier than the normal scene handoff/control-flow surfaces we have been instrumenting
  - the early startup/title window is still polluting these markers, but the late window is now consistent:
    - pending override survives
    - no later stable handoff marker survives with it

- Next target:
  - stop relying on markers that can be polluted by the early startup samples
  - instrument a later-safe state transition that only becomes writable after intro/title flow is complete, so we can distinguish “never consumed” from “consumed then reset”

## 2026-03-28 18:01 PDT

- Added a late-safe gate to the sticky pending-handoff markers in [story.c](/home/hunter/workspace/jc_reborn/story.c):
  - the ACTIVITY handoff latches now only set after `grGetCurrentFrame() >= 1000`

- Validation rerun:
  - [pendinghandoff-late](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-pendinghandoff-late/result.json)

- Result:
  - the sampled late frames still show only:
    - `boot_pending=1`
  - all of these remain `0` at `04950` through `06300`:
    - `pending_boot_resolved`
    - `pending_prepare`
    - `pending_play_prepared`
    - `pending_play_prepared_returned`

- Important conclusion:
  - gating those markers behind a later frame threshold did not change the core read
  - the stable ACTIVITY failure path still preserves only the pending override
  - it does not preserve any trustworthy evidence of the later normal handoff surfaces

- Current best read:
  - the unexplained edge is now even narrower:
    - the override remains pending after startup
    - but the code path that should consume it into normal scene handoff still leaves no late stable trace

- Next target:
  - move one layer lower than the current story markers and find a later-safe “override consumed” state that cannot be confused with startup/title replay

## 2026-03-28 18:06 PDT

- Split the late pending-override source further in [story.c](/home/hunter/workspace/jc_reborn/story.c):
  - `late_boot_ads_exact`
  - `late_boot_lookup_null`
  - `late_boot_lookup_found`
- Exposed those as late sticky rows in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c) and decoded them in [scripts/decode-ps1-bars.py](/home/hunter/workspace/jc_reborn/scripts/decode-ps1-bars.py).

- Validation rerun:
  - [latebootsource](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-latebootsource/result.json)

- Result:
  - after startup, from `04950` through `06300`, the only surviving late signal is still:
    - `boot_pending=1`
  - all of these remain `0` in the late window:
    - `late_boot_ads_exact`
    - `late_boot_lookup_null`
    - `late_boot_lookup_found`
    - `pending_boot_resolved`

- Important conclusion:
  - the new split markers still only light in the polluted early startup samples and do not survive into the late ACTIVITY failure path
  - so they do not change the late-path read:
    - stable late evidence still shows only an unresolved pending override

- Current best read:
  - the late ocean band is downstream of “override still pending”
  - but upstream of every stable story-level resolution/prepare/play marker we have made durable so far

- Next target:
  - stop extending the same story overlay family of markers
  - move to a different proof surface entirely for the next cut, because the current late-safe story markers are no longer adding new separation

## 2026-03-28 18:11 PDT

- Switched from passive markers to a behavioral proof in [story.c](/home/hunter/workspace/jc_reborn/story.c):
  - if the exact `ACTIVITY.ADS 1` override was still pending after frame `1000`,
    clear it immediately via `storyClearBootSceneOverrideOnly()`

- Validation run:
  - [lateclear](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-lateclear/result.json)

- Result:
  - the stable late output did not change at all
  - the late frame hash is byte-identical to the previous late-handoff run:
    - old `frame_06150`: `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
    - new `frame_06150`: `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the lingering ACTIVITY boot override is not the active cause of the late ocean band
  - it is residue
  - the real failure has already happened earlier by the time the late window is reached

- Cleanup:
  - reverted the temporary late-clear proof patch after validating the identical output

- Current best read:
  - the active bug is now clearly upstream of the surviving `boot_pending` residue
  - continuing to chase the late pending-override state itself will not produce the fix

- Next target:
  - shift the proof surface from late residual story state to the earlier point where the ACTIVITY path first diverges into the wrong ocean outcome

## 2026-03-28 18:15 PDT

- Ran a finer-grained ACTIVITY boundary scan with 30-frame sampling:
  - [boundary5100](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-boundary5100/result.json)

- Result:
  - on the current build, the stable wrong-ocean hash first appears at:
    - `frame_04920`
  - before that, the run is still changing through several distinct earlier states

- Important conclusion:
  - the useful instrumentation window is now much tighter
  - there is no need to keep probing the broad `5000+` late band

- Current best read:
  - the ACTIVITY failure becomes stable at `04920`
  - so the next debug cut should target the transition band immediately before that:
    - roughly `04800–04920`

- Next target:
  - instrument the first bad transition window directly around `04920`, rather than chasing residual state after the failure has already stabilized

## 2026-03-28 18:28 PDT

- Tightened the first stable failure onset again with a 10-frame boundary scan:
  - [boundary5000i10](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-boundary5000i10/result.json)

- Result:
  - first stable bad frame is now pinned to:
    - `frame_04910`
  - last frame before it:
    - `frame_04900`

- Useful observation:
  - `frame_04900 -> frame_04910` is the real settled flip
  - `frame_04910 -> frame_04920` is byte-identical
  - so `04910` is the first stable wrong-ocean frame, not just part of a slow drift

- Behavioral proof:
  - temporarily bypassed `adsPlayIntro()` in [story.c](/home/hunter/workspace/jc_reborn/story.c) only for exact `ACTIVITY.ADS 1`
  - validation run:
    - [nointro](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-nointro/result.json)

- Result:
  - the stable late bad ocean disappears entirely
  - replaced by a stable black-screen path
  - final state hash changes from the ocean hash to:
    - `df57118101f4e25e7e8a74ed77aaadb00f9ed9ea28e1cff7236c070d1ffaed5e`
  - visual detect at `frame_04910` and `frame_05000`:
    - `black` / no island / no content

- Important conclusion:
  - the settled bad ocean is directly sourced by the normal `adsPlayIntro()` boot path
  - bypassing intro does not fix ACTIVITY, but it proves the late ocean surface is not some unrelated later fallback
  - it comes from the intro/ocean boot sequence that the story path normally uses before ACTIVITY should take over

- Cleanup:
  - reverted the temporary `adsPlayIntro()` bypass after validating the black-screen replacement path

- Current best read:
  - the active bug is now strongly centered on the handoff from intro/ocean boot into the ACTIVITY scene path
  - not on the residual late pending override

- Next target:
  - instrument the intro-to-scene handoff itself, because that is now the most plausible place where ACTIVITY takeover fails and leaves the boot ocean in place

## 18:32 PDT - Post-intro scrub proof changes failure to stable title, not black or ocean

- Temporary proof:
  - left normal `adsPlayIntro()` in [story.c](/home/hunter/workspace/jc_reborn/story.c)
  - then immediately called `adsNoIsland()` only for exact `ACTIVITY.ADS 1`

- Validation run:
  - [postintroscrub](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-postintroscrub/result.json)

- Result:
  - run exits cleanly with:
    - `state_hash = d9ee0d2e8f8465ea471c9c79223f1177c7225a238274f7f68ab32ed94312c0a0`
  - sampled late frames remain on the title surface, not ocean:
    - `frame_04770`: title
    - `frame_04910`: title
    - `frame_05000`: title

- Important comparison:
  - normal path:
    - stable bad ocean by `04910`
  - no-intro proof:
    - stable black-screen path
  - post-intro scrub proof:
    - stable title path

- Important conclusion:
  - the bad ocean is not explained only by `adsPlayIntro()` being called
  - it depends on intro-side island state surviving after intro returns
  - clearing island state immediately after intro prevents the bad ocean and instead leaves the run stranded on title
  - so the ACTIVITY failure is now most strongly centered on the handoff immediately after intro returns and before ACTIVITY content successfully takes over

- Cleanup:
  - reverted the temporary post-intro `adsNoIsland()` proof patch after validation

- Next target:
  - instrument the immediate post-intro handoff, because that is now the narrowest boundary separating:
    - stable title
    - stable wrong ocean
    - black-screen no-intro failure

## 18:34 PDT - Replacing first exact ACTIVITY prepare with `adsNoIsland()` yields black, not ocean

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), replaced the first exact:
    - `bootScene != NULL`
    - `finalScene == ACTIVITY.ADS 1`
    - `storyPrepareSceneState(finalScene)`
  - with:
    - `adsNoIsland()`

- Validation run:
  - [prepare-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-prepare-noisland/result.json)

- Result:
  - run exits cleanly with:
    - `state_hash = 0f9a705ed4b557e621649636842b051f9311c39535f4fd6159c7b3243c98a54a`
  - sampled late frames are black, not ocean:
    - `frame_04770`: black
    - `frame_04910`: black
    - `frame_05000`: black

- Important comparison:
  - normal path:
    - stable bad ocean by `04910`
  - no-intro proof:
    - stable black-screen path
  - post-intro scrub proof:
    - stable title path
  - exact-prepare `adsNoIsland()` proof:
    - stable black-screen path

- Important conclusion:
  - the settled bad ocean is being established by the exact first `ACTIVITY.ADS 1` prepare path, not merely carried forward from intro
  - intro alone is not enough to explain the stable ocean:
    - keeping intro but scrubbing immediately after it gives title
    - keeping intro but replacing the exact ACTIVITY prepare with `adsNoIsland()` gives black
  - so the active failure boundary is now centered tightly on what `storyPrepareSceneState(finalScene)` does for exact `ACTIVITY.ADS 1`

- Cleanup:
  - reverted the temporary exact-prepare `adsNoIsland()` proof patch after validation

- Next target:
  - inspect the exact `ACTIVITY.ADS 1` prepare path itself:
    - `storyCalculateIslandFromScene(finalScene)`
    - `adsPrimeSceneResources(...)`
    - `adsInitIsland()`
  - because that path now has the strongest causal link to the stable wrong-ocean state

## 18:40 PDT - Skipping `adsPrimeSceneResources()` does not change the stable ocean failure

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), skipped:
    - `adsPrimeSceneResources(scene->adsName, scene->adsTagNo)`
  - only for exact `ACTIVITY.ADS 1`
  - left:
    - `storyCalculateIslandFromScene(scene)`
    - `adsInitIsland()`
    - normal intro flow
    - normal later playback path

- Validation run:
  - [skip-prime](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-skip-prime/result.json)

- Result:
  - run exits cleanly with:
    - `state_hash = fe8a54dc926a6b88be9364720305f44a71ea7d5f511a15ab3c3462defac3996b`
  - sampled late frames are still stable ocean:
    - `frame_04770`: ocean
    - `frame_04910`: ocean
    - `frame_05000`: ocean

- Important conclusion:
  - `adsPrimeSceneResources()` is not the primary cause of the settled wrong-ocean state
  - removing it does not collapse the run to black and does not prevent the ocean failure
  - so the strongest remaining prepare-path suspects are now:
    - `storyCalculateIslandFromScene(finalScene)`
    - `adsInitIsland()`
    - specifically `islandInit()` and the initial `grLoadScreen("OCEAN0?.SCR")` path it performs

- Cleanup:
  - reverted the temporary skip-prime proof patch after validation

- Next target:
  - split the remaining exact prepare surface between:
    - island-state calculation
    - and `adsInitIsland()` / `islandInit()`

## 18:45 PDT - Skipping exact scene island-state calculation perturbs the lead-in, but ocean still settles by `04910`

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), skipped:
    - `storyCalculateIslandFromScene(scene)`
  - only for exact `ACTIVITY.ADS 1`
  - left:
    - `adsPrimeSceneResources(...)`
    - `adsInitIsland()`
    - normal intro flow
    - normal later playback path

- Validation run:
  - [skip-scene-islandcalc](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-skip-scene-islandcalc/result.json)

- Result:
  - run exits cleanly with:
    - `state_hash = 81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`
  - sampled frames:
    - `frame_04770`: black
    - `frame_04910`: ocean
    - `frame_05000`: ocean

- Important conclusion:
  - scene-specific island-state calculation does affect the lead-in before the settled failure
  - but it does not prevent the stable wrong-ocean state from being re-established by `04910`
  - so it is not sufficient to explain the persistent ACTIVITY failure on its own
  - the strongest remaining causal surface is still:
    - `adsInitIsland()`
    - especially `islandInit()` and its initial background load/composition path

- Cleanup:
  - reverted the temporary skip-scene-islandcalc proof patch after validation

- Next target:
  - isolate `adsInitIsland()` / `islandInit()` more directly, because that is now the tightest surviving source of the settled wrong-ocean outcome

## 02:16 PDT - Keeping the first small pack-header buffer alive did not move `ACTIVITY 1`

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c), changed [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) so the first small `PS1_PACK_HEADER_SIZE` allocation stayed alive until the function returned
  - specifically:
    - kept the first `ps1_streamReadFromCdFile(&cdfile, 0, PS1_PACK_HEADER_SIZE)` buffer allocated
    - delayed its `free(...)` until after the later full header read / entry decode completed
  - goal:
    - test whether immediate reuse of that first tiny heap block was the layout-sensitive seam

- Validation run:
  - [activity1-packindex-keep-small-header](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-keep-small-header)

- Result:
  - run exited cleanly with the exact old settled-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - keeping the first small header allocation alive is not sufficient to move the bug
  - so the layout/lifetime sensitivity around [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) is still real, but it is not explained by simple reuse of that first `20`-byte header block alone
  - the stronger remaining suspects stay local to:
    - stack adjacency / local object layout
    - `CdlFILE` lifetime or overwrite
    - interaction between the two `ps1_streamReadFromCdFile(...)` calls beyond just freeing the first tiny buffer

- Cleanup:
  - reverted the temporary `headerDataSmall` lifetime probe in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

- Next target:
  - inspect local-object lifetime and overwrite risk around:
    - `cdfile`
    - `headerData`
    - `entries`
    - the transition between the first and second `ps1_streamReadFromCdFile(...)` calls

## 02:18 PDT - Copying `CdlFILE` immediately after `CdSearchFile` moved `ACTIVITY 1` to the third branch

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c), changed [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) so that:
    - `stableCdfile = cdfile;`
    - both `ps1_streamReadFromCdFile(...)` calls used `stableCdfile`
    - `outPack->cdfile` also stored `stableCdfile`
  - no search logic changed
  - goal:
    - test whether copying `CdlFILE` immediately after `CdSearchFile(...)` is enough to perturb the layout-sensitive failure

- Validation run:
  - [activity1-packindex-stable-cdfile](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-stable-cdfile)

- Result:
  - run exited cleanly with the third stable branch:
    - `state_hash = 81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`
  - late markers:
    - `bmp_ok = true`
    - `bmp_fail = true`
    - `sprite_count_estimate = 0`

- Important conclusion:
  - this is a strong confirmation that the live seam is no longer behaving like a pure `CdSearchFile(...)` logic bug
  - copying `CdlFILE` immediately after search is enough to move the runtime, matching the earlier stack-layout branch
  - the strongest active suspects are now:
    - local `CdlFILE` lifetime / overwrite
    - adjacent stack layout around `cdfile`, `headerData`, and `entries`
    - UB or clobber between the first and second `ps1_streamReadFromCdFile(...)` calls

- Cleanup:
  - reverted the temporary `stableCdfile` probe in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

- Next target:
  - probe memory adjacency around `cdfile` more directly, rather than pack-search logic
  - likely either:
    - duplicate `cdfile` into a far-separated local block
    - or harden / relocate other nearby locals in [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

## 02:21 PDT - Moving `CdlFILE` fully off the local stack also moved `ACTIVITY 1`

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c), changed [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) so the searched pack file record lived in a function-static `CdlFILE`
  - specifically:
    - removed local-stack `CdlFILE cdfile`
    - used `static CdlFILE stableCdfile`
    - passed `stableCdfile` to both `ps1_streamReadFromCdFile(...)` calls
    - stored `stableCdfile` into `outPack->cdfile`

- Validation run:
  - [activity1-packindex-static-cdfile](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-static-cdfile)

- Result:
  - run exited cleanly on the improved branch:
    - `state_hash = 91d3e49ac1712390a97e7588519fa53d29b3a1cf33ae1f1696705a097ddb534d`
  - late markers:
    - `bmp_ok = true`
    - `bmp_fail = false`
    - `sprite_count_estimate = 63`

- Important conclusion:
  - this is stronger than the previous local-copy probe
  - moving `CdlFILE` completely off the hot local stack is enough to change the runtime
  - together, the two `CdlFILE` probes now make the most likely live seam:
    - local-stack adjacency around `CdlFILE cdfile`
    - or a nearby overwrite / UB that depends on where `cdfile` is laid out relative to:
      - `headerData`
      - `packFile`
      - `cdPath`
      - later `packCdFile`

- Cleanup:
  - reverted the temporary function-static `CdlFILE` probe in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

- Next target:
  - probe which nearby local is interacting with `cdfile` layout
  - likely by moving or padding one neighboring object at a time inside [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

## 02:24 PDT - Moving `cdPath` off the local stack also moved `ACTIVITY 1`

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c), changed [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) so the constructed pack search path lived in a function-static buffer
  - specifically:
    - removed local-stack `char cdPath[64]`
    - used `static char stableCdPath[64]`
    - still kept local-stack `CdlFILE cdfile`
    - no search logic changed beyond where the path buffer lived

- Validation run:
  - [activity1-packindex-static-cdpath](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-static-cdpath)

- Result:
  - run exited cleanly on the third branch:
    - `state_hash = 81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`
  - late markers:
    - `bmp_ok = true`
    - `bmp_fail = true`
    - `sprite_count_estimate = 0`

- Important conclusion:
  - this is the strongest adjacent-local clue so far
  - moving only `cdPath` off the local stack is enough to perturb the bug, even while `cdfile` remains local
  - that makes the hottest current seam the local-stack relationship between:
    - `char cdPath[64]`
    - `CdlFILE cdfile`
  - current best read:
    - either `CdSearchFile(...)` or a nearby later operation is clobbering one of those objects
    - or the bug depends on exact stack adjacency between them

- Cleanup:
  - reverted the temporary function-static `cdPath` probe in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

- Next target:
  - probe the `cdPath` / `cdfile` seam directly
  - likely with local padding between them, or by reordering just those two locals inside [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

## 02:27 PDT - Simple local padding between `cdPath` and `cdfile` did not move `ACTIVITY 1`

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c), changed [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) so both:
    - `char cdPath[64]`
    - `CdlFILE cdfile`
  - remained local-stack objects
  - but a local:
    - `volatile uint8_t cdPathPad[64]`
  - was inserted between them and zeroed before `CdSearchFile(...)`

- Validation run:
  - [activity1-packindex-cdpathpad64](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-cdpathpad64)

- Result:
  - run exited cleanly on the original old-ocean branch:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the live seam is more specific than raw stack distance between `cdPath` and `cdfile`
  - simply inserting padding is not enough
  - the earlier moving probes likely depend on:
    - exact object identity / storage class
    - or exact relative ordering/layout, not just added bytes in between

- Cleanup:
  - reverted the temporary `cdPathPad` probe in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

- Next target:
  - test exact local reordering rather than padding
  - especially swapping or separating `cdPath` and `cdfile` by object order inside [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

## 02:31 PDT - Swapping local order of `cdfile` and `cdPath` moved `ACTIVITY 1`

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c), changed [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) so only the declaration order changed:
    - from:
      - `char cdPath[64];`
      - `CdlFILE cdfile;`
    - to:
      - `CdlFILE cdfile;`
      - `char cdPath[64];`
  - no logic changed
  - no storage class changed

- Validation run:
  - [activity1-packindex-swap-cdfile-cdpath](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-swap-cdfile-cdpath)

- Result:
  - run exited cleanly on the improved branch:
    - `state_hash = 91d3e49ac1712390a97e7588519fa53d29b3a1cf33ae1f1696705a097ddb534d`
  - late markers:
    - `bmp_ok = true`
    - `bmp_fail = false`
    - `sprite_count_estimate = 63`

- Important conclusion:
  - exact local order matters here
  - that is stronger than the earlier padding-negative result
  - current best read:
    - the live seam is a layout-sensitive overwrite / UB involving the local-stack placement of:
      - `cdPath`
      - `cdfile`
    - not just raw adjacency distance

- Cleanup:
  - reverted the temporary local-order swap in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

- Next target:
  - identify which operation actually clobbers or depends on that order
  - likely by isolating:
    - `CdSearchFile(&cdfile, cdPath)`
    - the first `ps1_streamReadFromCdFile(&cdfile, ...)`
    - and any later reuse of `cdfile`

## 02:36 PDT - A tiny helper around `CdSearchFile(...)` also moved `ACTIVITY 1`

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c), added a tiny helper:
    - `ps1PilotSearchFile(const char *path, CdlFILE *outFile)`
  - and changed [ps1PilotLoadPackIndex(...)](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) to call that helper instead of invoking:
    - `CdSearchFile(&cdfile, cdPath)`
    - directly
  - no logic changed
  - no storage class changed

- Validation run:
  - [activity1-packindex-search-helper](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-search-helper)

- Result:
  - run exited cleanly on the improved branch:
    - `state_hash = 91d3e49ac1712390a97e7588519fa53d29b3a1cf33ae1f1696705a097ddb534d`
  - late markers:
    - `bmp_ok = true`
    - `bmp_fail = false`
    - `sprite_count_estimate = 63`

- Important conclusion:
  - changing the caller frame around `CdSearchFile(...)` is enough to move the runtime
  - that is the strongest current sign that the live seam is stack-sensitive corruption or UB at/around the `CdSearchFile(&cdfile, cdPath)` call site itself
  - current best read:
    - not a clean logical pack-search bug
    - not just later pack-header parsing
    - most likely a stack-sensitive overwrite interacting with:
      - local `cdPath`
      - local `cdfile`
      - and the exact call frame for `CdSearchFile(...)`

- Cleanup:
  - reverted the temporary `ps1PilotSearchFile(...)` helper probe in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c)

- Next target:
  - isolate the fault surface to:
    - the direct `CdSearchFile(...)` call itself
    - versus the first subsequent `ps1_streamReadFromCdFile(...)`
  - using equally small caller-frame perturbations

## 01:55 PDT - Evaluating the old `ACTIVITY.PAK` conditional alone is enough to hold the improved branch

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - kept the same conditional shape that previously correlated with the improved branch:
      - literal `CdSearchFile(&verifyFile, "\\PACKS\\ACTIVITY.PAK;1")`
      - exact expected metadata checks
      - second literal `CdSearchFile(&cdfile, "\\PACKS\\ACTIVITY.PAK;1")`
      - mismatch test against the same expected metadata
    - but removed the early-return side effect
    - only assigned the boolean result to a `volatile int proof_condition`

- Validation run:
  - [activity1-activitypak-cond-noop](/home/hunter/workspace/jc_reborn/regtest-results/activity1-activitypak-cond-noop)

- Result:
  - run stayed on the improved branch:
    - `91d3e49ac1712390a97e7588519fa53d29b3a1cf33ae1f1696705a097ddb534d`

- Important conclusion:
  - the earlier improvement did not depend on the early return being taken
  - evaluating that exact conditional is enough to perturb the failure
  - so the active seam now looks like a timing / register / stack-layout sensitivity around the exact `ACTIVITY.PAK` lookup path, not a clean logical `CdSearchFile` branch bug

- Cleanup:
  - reverted the temporary conditional-noop proof

- Next target:
  - stop treating this as a pure logic bug in the pack-lookup branch
  - inspect whether tiny local-layout changes around `ps1PilotLoadPackIndex(...)` are masking an adjacent memory clobber or `CdlFILE` lifetime issue

## 02:00 PDT - Pure local stack padding moves the run to a third stable branch

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, added:
    - `volatile uint8_t layoutPad[64];`
    - `memset((void *)layoutPad, 0, sizeof(layoutPad));`
  - only for exact `ACTIVITY.ADS`
  - no `CdSearchFile` logic was changed

- Validation run:
  - [activity1-packindex-layoutpad64](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-layoutpad64)

- Result:
  - run moved to a third stable branch:
    - `81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`
  - markers also changed:
    - `bmp_ok = true`
    - `bmp_fail = true`
    - `sprite_count_estimate = 0`

- Important conclusion:
  - this is the strongest evidence so far that the live seam is a local-memory / UB / layout-sensitive bug around `ps1PilotLoadPackIndex(...)`
  - a pure stack-layout perturbation, with no lookup-logic change, is enough to move the runtime onto a different stable failure branch
  - current best read:
    - adjacent local clobber
    - or lifetime/overwrite involving `CdlFILE`, header buffers, or nearby stack locals

- Cleanup:
  - reverted the temporary stack-padding proof

- Next target:
  - inspect exact local-object lifetime and overwrite risk inside `ps1PilotLoadPackIndex(...)`
  - especially:
    - stack-local adjacency
    - `ps1_streamReadFromCdFile(...)` consumers
    - and whether header/entry decode is reading/writing beyond intended bounds

## 02:05 PDT - Basic decoded-field bounds hardening does not move the run

- Temporary hardening:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, added guards:
    - reject `entryBytes` multiply overflow
    - reject `prefetchCount > PS1_PACK_PREFETCH_MAX`
    - reject `firstResourceOffset > cdfile.size`
  - left all actual lookup and decode logic otherwise unchanged

- Validation run:
  - [activity1-packindex-bounds-guard1](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-bounds-guard1)

- Result:
  - run still fell back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the active bug is still not explained by the most obvious corrupted-header invariants
  - current evidence still points more strongly at:
    - layout-sensitive UB
    - local-memory clobber
    - or object-lifetime misuse around `ps1PilotLoadPackIndex(...)`

- Cleanup:
  - reverted the temporary bounds guards

- Next target:
  - probe object adjacency/lifetime more directly:
    - `headerData`
    - `entries`
    - `cdfile`
    - and the transition between the first and second `ps1_streamReadFromCdFile(...)` results

## 01:24 PDT - Root-file lookup is healthy before `ACTIVITY.PAK` search, but not after it

- Temporary proofs:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - before `CdSearchFile(&cdfile, cdPath)`, checked:
      - `CdSearchFile(&verifyFile, "\\RESOURCE.MAP;1")`
      - expected:
        - size `1461`
        - extent `416`
    - and temporarily returned `0` if that lookup matched exactly
  - separate run:
    - after the normal `ACTIVITY.PAK` `CdSearchFile(...)`, checked the same `\\RESOURCE.MAP;1` metadata and returned `0` only if it still matched

- Validation runs:
  - [activity1-rootmap-presearch-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-rootmap-presearch-proof)
  - [activity1-rootmap-control-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-rootmap-control-proof)

- Result:
  - presearch proof snapped back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - postsearch proof stayed on the improved branch:
    - `91d3e49ac1712390a97e7588519fa53d29b3a1cf33ae1f1696705a097ddb534d`

- Important conclusion:
  - `CdSearchFile` is healthy on entry to `ps1PilotLoadPackIndex(...)`
  - but after the exact `ACTIVITY.PAK` lookup, even a known-good root file no longer resolves with the expected metadata
  - so the `ACTIVITY.PAK` search itself is poisoning later `CdSearchFile` state

- Cleanup:
  - reverted the temporary `RESOURCE.MAP` proofs

- Next target:
  - determine whether pack-directory lookup is also healthy before the exact `ACTIVITY.PAK` search

## 01:31 PDT - Another pack file resolves correctly before `ACTIVITY.PAK` search

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - before the normal `ACTIVITY.PAK` search, checked:
      - `CdSearchFile(&verifyFile, "\\PACKS\\BUILDING.PAK;1")`
      - expected:
        - size `2441216`
        - extent `5037`
    - and temporarily returned `0` if that lookup matched exactly

- Validation run:
  - [activity1-building-presearch-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-building-presearch-proof)

- Result:
  - run snapped back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - pack-directory lookup is also healthy on entry
  - so the failure is not a general inability to resolve `PACKS\\*.PAK;1`
  - the live seam is now narrower: the exact `ACTIVITY.PAK` lookup itself

- Cleanup:
  - reverted the temporary `BUILDING.PAK` presearch proof

- Next target:
  - rule out any remaining path-string construction issue by testing a literal `\\PACKS\\ACTIVITY.PAK;1` lookup

## 01:32 PDT - Literal `\\PACKS\\ACTIVITY.PAK;1` lookup fails too

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - before the normal `cdPath` search, checked a literal:
      - `CdSearchFile(&verifyFile, "\\PACKS\\ACTIVITY.PAK;1")`
      - expected:
        - size `2836480`
        - extent `3652`
    - and temporarily returned `0` if that literal lookup matched exactly

- Validation run:
  - [activity1-activitypak-literal-presearch-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-activitypak-literal-presearch-proof)

- Result:
  - run stayed on the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - this is not a `cdPath` construction or `ps1PilotBuildPackFile(...)` string-format issue
  - on current `HEAD`, exact `ACTIVITY.PAK` resolution is already wrong even when searched via the literal correct path, while:
    - `\\RESOURCE.MAP;1` resolves correctly before it
    - `\\PACKS\\BUILDING.PAK;1` resolves correctly before it
  - the active root-cause surface is now the exact `CdSearchFile("\\PACKS\\ACTIVITY.PAK;1")` lookup behavior itself

- Cleanup:
  - reverted the temporary literal-lookup proof

- Next target:
  - inspect why exact `ACTIVITY.PAK` lookup is special:
    - possible duplicate/conflicting directory record
    - path aliasing
    - or pack-name-specific disc lookup behavior under DuckStation/PSn00b CD APIs

## 01:35 PDT - The image has exactly one `ACTIVITY.PAK` record, and repeated literal lookups stay on the improved branch

- Built-image inspection:
  - parsed [jcreborn.bin](/home/hunter/workspace/jc_reborn/jcreborn.bin) directly
  - `PACKS` directory listing is clean:
    - `ACTIVITY.PAK;1` extent `3652`, size `2836480`
    - `BUILDING.PAK;1` extent `5037`, size `2441216`
    - no duplicate `ACTIVITY.PAK;1` directory records

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - performed a first literal:
      - `CdSearchFile(&verifyFile, "\\PACKS\\ACTIVITY.PAK;1")`
    - required it to match:
      - size `2836480`
      - extent `3652`
    - then immediately performed a second identical literal:
      - `CdSearchFile(&cdfile, "\\PACKS\\ACTIVITY.PAK;1")`
    - and would temporarily return `0` only if that second lookup came back wrong

- Validation run:
  - [activity1-activitypak-doublelookup-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-activitypak-doublelookup-proof)

- Result:
  - run stayed on the improved branch:
    - `91d3e49ac1712390a97e7588519fa53d29b3a1cf33ae1f1696705a097ddb534d`

- Important conclusion:
  - the disc image itself is not the issue:
    - there is a single clean `ACTIVITY.PAK;1` record
  - two consecutive literal `CdSearchFile("\\PACKS\\ACTIVITY.PAK;1")` calls do not reproduce the old failure
  - so the live seam is now narrower than “ACTIVITY record is bad”
  - current best read:
    - the normal runtime path can be destabilized by how it reaches the later `CdSearchFile(&cdfile, cdPath)` call
    - but a literal ACTIVITY lookup can also act as a stabilizing/priming step before the normal path continues

- Cleanup:
  - reverted the temporary repeated-literal proof

- Next target:
  - test whether a single literal `ACTIVITY.PAK` prelookup, with no mismatch check and no early return, is enough to stabilize the normal runtime path

## 01:40 PDT - A single literal `ACTIVITY.PAK` prelookup is not enough

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - inserted a no-op prelookup:
      - `CdSearchFile(&primeFile, "\\PACKS\\ACTIVITY.PAK;1")`
    - then continued into the normal:
      - `CdSearchFile(&cdfile, cdPath)`
    - no early return, no mismatch branch

- Validation run:
  - [activity1-activitypak-prime-only](/home/hunter/workspace/jc_reborn/regtest-results/activity1-activitypak-prime-only)

- Result:
  - run fell back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the earlier repeated-literal stabilization effect is real, but it is not explained by simple one-shot priming alone
  - the live seam is now narrower:
    - something about the exact repeated lookup/use pattern matters
    - not just “search ACTIVITY once before the normal path”

- Cleanup:
  - reverted the temporary prime-only prelookup

- Next target:
  - compare the successful double-literal pattern against the failing prime-only pattern more directly
  - likely focus on:
    - which `CdlFILE` instance is later reused
    - and whether avoiding the final `CdSearchFile(&cdfile, cdPath)` entirely changes the path

## 01:45 PDT - Reusing a known-good literal `CdlFILE` still does not stabilize the normal path

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - performed:
      - `CdSearchFile(&primeFile, "\\PACKS\\ACTIVITY.PAK;1")`
    - then skipped the normal `CdSearchFile(&cdfile, cdPath)` by copying:
      - `cdfile = primeFile`
    - the rest of the pack-index path was unchanged

- Validation run:
  - [activity1-activitypak-reuse-primefile](/home/hunter/workspace/jc_reborn/regtest-results/activity1-activitypak-reuse-primefile)

- Result:
  - run still fell back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the earlier repeated-literal stabilization effect is narrower than:
    - one-shot priming
    - or reusing a known-good `CdlFILE` directly
  - so the remaining seam is likely in the exact lookup/use sequence of the successful double-literal proof, not just in the final normal `CdSearchFile(...)` call or the `CdlFILE` value alone

- Cleanup:
  - reverted the temporary `cdfile = primeFile` proof

- Next target:
  - compare the exact successful double-literal proof flow against the failing reuse flow
  - likely inspect whether the success depended on:
    - performing the second literal `CdSearchFile(...)` into the same destination variable later used by the rest of the function

## 01:50 PDT - Double literal lookup without the old conditional still falls back

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - performed:
      - `CdSearchFile(&primeFile, "\\PACKS\\ACTIVITY.PAK;1")`
      - `CdSearchFile(&cdfile, "\\PACKS\\ACTIVITY.PAK;1")`
    - then continued normally
    - no early return, no mismatch check

- Validation run:
  - [activity1-activitypak-doubleliteral-noearly](/home/hunter/workspace/jc_reborn/regtest-results/activity1-activitypak-doubleliteral-noearly)

- Result:
  - run still fell back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the earlier “successful” double-literal result was not a practical stabilizer sequence by itself
  - it depended on the exact conditional proof structure around it, not just on executing two literal lookups
  - that pushes the live seam back toward:
    - subtle branch/control-flow perturbation from the proof itself
    - rather than a reusable `CdSearchFile` workaround

- Cleanup:
  - reverted the temporary double-literal-noearly probe

- Next target:
  - compare the original successful conditional proof shape against the clean negative variants
  - specifically isolate whether the old success came from:
    - skipping later code via the conditional structure
    - or changing register/stack/timing enough to mask the underlying fault

## 00:36 PDT - ACTIVITY ADS metadata is already wrong immediately after `ps1_loadAdsData()`

- Active scene/debug base:
  - current `HEAD`
  - exact scene:
    - `ACTIVITY.ADS 1`

- Strong parser-side results established this session:
  - exact ACTIVITY entry offset is still correct after `adsLoad(...)`:
    - `offset == 2`
  - but these invariants do not hold on the PS1 runtime path:
    - `adsNumTags == 10`
    - `numAdsChunks == 6`
    - `adsFindTag(12) == 176`

- New earlier proof:
  - in [ads.c](/home/hunter/workspace/jc_reborn/ads.c), immediately after the PS1 lazy ADS load block and before:
    - pilot priming
    - `pinResource(...)`
    - `checkMemoryBudget()`
    - `adsLoad(...)`
  - for exact `ACTIVITY.ADS 1`, added a guard:
    - if `adsResource->uncompressedSize != 2558`
    - call:
      - `adsReleaseIsland()`
      - `adsNoIsland()`
      - `return`

- Validation run:
  - [activity1-ads-size-early-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads-size-early-proof)

- Result:
  - `frame_04920` stayed on the improved black lead-in:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - late frames changed to the same alternate persistent state seen in the earlier byte-proof:
    - `frame_06000`: `81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`
    - `frame_06150`: `81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`
    - `frame_06300`: `81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`

- Important conclusion:
  - the ACTIVITY ADS corruption is earlier than:
    - `pinResource(...)`
    - `checkMemoryBudget()`
    - `adsLoad(...)`
  - by the time `ps1_loadAdsData()` returns, exact `ACTIVITY.ADS 1` already has the wrong `uncompressedSize`
  - that shifts the live root-cause surface into:
    - PS1 ADS load path
    - or ADS resource metadata integrity before/inside `ps1_loadAdsData()`

- Cleanup:
  - reverted the temporary early-size proof patch after validation

- Next target:
  - inspect:
    - `ps1_loadAdsData(...)`
    - ADS metadata parse path in `resource.c`
    - any mismatch between authored ACTIVITY metadata (`uncompressedSize = 2558`) and runtime PS1 metadata/load behavior

## 23:55 PDT - Older `6dae8410` base is not a trustworthy post-boot PS1 scene-debug target

- Active validation base:
  - detached worktree at [/tmp/jc_reborn_6dae](/tmp/jc_reborn_6dae)
  - commit:
    - `6dae8410`
    - `Fix PS1 first-handoff blackscreen`

- What I validated:
  - the old-base regtest harness really is staging the requested boot override into the snapshot disc:
    - `strings -a regtest-results/activity4-6dae-directads4/disc/jcreborn.bin`
    - contains:
      - `island ads ACTIVITY.ADS 4`
  - the old-base PS1 executable really is changing between proof runs:
    - `activity4-6dae-ps1load-entry-trap2`:
      - `Hash for 'JCREBORN.EXE' - 6A2E4F25CCD7CF0E`
    - `activity4-6dae-main-entry-trap`:
      - `Hash for 'JCREBORN.EXE' - 6880449D0B51BA34`

- Strong proofs that failed to fire despite different executables:
  - temporary non-optimizable infinite-loop trap at the top of:
    - `ps1LoadBootOverride()`
  - validation run:
    - [/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-ps1load-entry-trap2](/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-ps1load-entry-trap2)
  - result:
    - exited with success
    - `Frames dumped: 96`
    - `Total execution time: 2464.34ms`
  - temporary non-optimizable infinite-loop trap at the very top of:
    - `main()`
  - validation run:
    - [/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-main-entry-trap](/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-main-entry-trap)
  - result:
    - exited with success
    - `Frames dumped: 96`
    - `Total execution time: 3811.94ms`

- Important conclusion:
  - on `6dae8410`, the captured PS1 run is not behaving like a trustworthy post-`main()` game execution target
  - the disc snapshot does contain the requested `BOOTMODE.TXT` override
  - but even hard traps at `ps1LoadBootOverride()` entry and `main()` entry do not stall execution
  - therefore this older base is not a defensible target for deeper scene-by-scene ADS debugging above pre-`main()` startup

- Cleanup:
  - reverted the temporary `ps1LoadBootOverride()` and `main()` trap probes after validation

- Next target:
  - stop using `6dae8410` as the primary scene-debug base
  - choose a newer PS1 snapshot that actually reaches normal runtime code paths, then continue one-scene-at-a-time from there

## 00:05 PDT - Returned to current `HEAD`; `ACTIVITY 1` remains the live scene target

- What I validated:
  - the real PS1-side `adsTags` lifetime fix is still present in [ads.c](/home/hunter/workspace/jc_reborn/ads.c):
    - PS1 path uses:
      - `ps1StaticAdsTags`
    - instead of per-play heap allocation
  - the earlier improved ACTIVITY result artifacts are still the right baseline:
    - [activity1-ads1-seed1-static-adstags-fix](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-static-adstags-fix/result.json)
    - [activity1-ads1-seed1-static-adstags-fix-6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-static-adstags-fix-6300/result.json)

- Current ACTIVITY 1 read on `HEAD`:
  - the old settled bootstrap collapse is gone
  - but the scene is still not correct
  - by `6300` frames the run settles to:
    - `screen_type = ocean`
    - `island_present = false`
    - `johnny_present = false`
    - `launched = false`
    - `bmp_ok = true`
    - `sprite_count_estimate = 63`
  - strongest artifact:
    - [frame_06300.png](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-static-adstags-fix-6300/20260328-215318/frames/jcreborn/frame_06300.png)

- Harness note:
  - a fresh compare-wrapper rerun for `ACTIVITY 1` on current `HEAD` completed the PS1 capture phase but hung in post-capture compare generation
  - raw scene artifacts were produced under:
    - [/home/hunter/workspace/jc_reborn/regtest-results/reference-compare-current-activity1/activity-1](/home/hunter/workspace/jc_reborn/regtest-results/reference-compare-current-activity1/activity-1)
  - but no final `compare.json` / `compare.html` was emitted before the wrapper was stopped
  - that is a secondary harness issue, not the active scene root cause

- Important conclusion:
  - `6dae8410` is out
  - current `HEAD` is back to being the only defensible live PS1 scene-debug base
  - active scene remains:
    - `ACTIVITY 1`
  - active bug surface remains:
    - exact ACTIVITY bootstrap no longer fully collapses
    - but later live playback still fails to take ownership of the scene, leaving a bad late ocean state

- Next target:
  - continue on `ACTIVITY 1` from current `HEAD`
  - debug the post-bootstrap playback path after the static-`adsTags` fix, not the older pre-`main()` validation base

## 00:12 PDT - Current `ACTIVITY 1` still crosses the old pre-chunk slot-1 corruption seam

- Temporary proof:
  - in [ads.c](/home/hunter/workspace/jc_reborn/ads.c), immediately after the exact `ACTIVITY.ADS 1` TTM load loop and before the first `adsPlayChunk(...)`, added a temporary guard:
    - if `ttmSlots[1].ttmResource` was not `GJDIVE.TTM`
    - call:
      - `adsReleaseIsland()`
      - `adsNoIsland()`
      - `unpinResource(adsResource, "ADS")`
      - `return`

- Validation run:
  - [activity1-slot1-prechunk-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-slot1-prechunk-proof)

- Result:
  - `frame_04920` stayed on the post-`adsTags` improved black lead-in:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - but late frames regressed back to the old settled-ocean hash:
    - `frame_06000`: `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
    - `frame_06150`: `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
    - `frame_06300`: `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the static `adsTags` fix did move the bootstrap behavior
  - but current `ACTIVITY 1` still crosses the earlier pre-chunk slot/resource corruption seam
  - the strongest live suspect remains:
    - slot `1` resource integrity between:
      - the `ttmLoadTtm()` loop
      - and the first exact `adsPlayChunk(...)`

- Cleanup:
  - reverted the temporary slot-1 prechunk proof patch after validation

- Next target:
  - inspect what mutates or invalidates `ttmSlots[1]` in that narrow pre-chunk window on current `HEAD`

## 00:18 PDT - `ACTIVITY 1` slot-1 corruption is already present by the time `adsLoad()` returns

- Temporary proof:
  - in [ads.c](/home/hunter/workspace/jc_reborn/ads.c), immediately after:
    - `adsLoad(data, dataSize, adsResource->numTags, adsTag, &offset);`
  - for exact `ACTIVITY.ADS 1`, added a temporary guard:
    - if `ttmSlots[1].ttmResource` is not `GJDIVE.TTM`
    - call:
      - `adsReleaseIsland()`
      - `adsNoIsland()`
      - `unpinResource(adsResource, "ADS")`
      - `return`

- Validation run:
  - [activity1-post-adsload-slot1-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-post-adsload-slot1-proof)

- Result:
  - `frame_04920` stayed on the improved black lead-in:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - late frames changed to a different persistent state:
    - `frame_06000`: `81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`
    - `frame_06150`: `81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`
    - `frame_06300`: `81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`

- Important conclusion:
  - this guard still perturbs the long-run ACTIVITY outcome
  - therefore the slot/resource corruption seam is already crossed by the time `adsLoad()` returns
  - that moves the active root-cause surface earlier than:
    - first exact `adsPlayChunk(...)`
  - strongest live suspect now:
    - `adsLoad(...)` itself, or state it mutates while parsing tags/chunks

- Cleanup:
  - reverted the temporary post-`adsLoad()` proof patch after validation

- Next target:
  - inspect `adsLoad(...)` for side effects that can invalidate slot/resource state on current `HEAD`

## 00:25 PDT - `adsLoad()` runtime tag table no longer preserves authored ACTIVITY startup tag `12 -> 176`

- Offline authored fact:
  - extracted [ACTIVITY.ADS](/home/hunter/workspace/jc_reborn/jc_resources/extracted/ads/ACTIVITY.ADS) parses to:
    - `10` tags total
    - tag `1` offset `2`
    - first-chunk bookmarked globals `= 6`
    - tag `12` offset `176`
    - tag `11` offset `364`

- Temporary proof:
  - in [ads.c](/home/hunter/workspace/jc_reborn/ads.c), immediately after `adsLoad(...)`, for exact `ACTIVITY.ADS 1`, added a guard:
    - if `adsFindTag(12) != 176`
    - call:
      - `adsReleaseIsland()`
      - `adsNoIsland()`
      - `unpinResource(adsResource, "ADS")`
      - `return`

- Validation run:
  - [activity1-tag12-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-tag12-proof)

- Result:
  - `frame_04920` stayed on the improved black lead-in:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - late frames regressed to the old settled-ocean hash:
    - `frame_06000`: `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
    - `frame_06150`: `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
    - `frame_06300`: `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - this guard clearly fired
  - so after runtime `adsLoad()` on current `HEAD`, the exact ACTIVITY startup tag table is already wrong enough that:
    - `adsFindTag(12) != 176`
  - this is stronger than the earlier bundled parse proof:
    - the corruption is not just “some parse invariant”
    - it directly affects the authored startup tag lookup needed to bootstrap ACTIVITY

- Cleanup:
  - reverted the temporary `adsFindTag(12)` proof patch after validation

- Next target:
  - distinguish whether runtime `adsFindTag(12)` is:
    - missing entirely (`0`)
    - or present but wrong offset

## 00:31 PDT - `ACTIVITY.ADS` parse-time metadata is still correct; corruption happens later

- Temporary proof:
  - in [ads.c](/home/hunter/workspace/jc_reborn/ads.c), moved the exact-size guard earlier so it checked:
    - `adsResource->uncompressedSize != 2558`
  - before calling `ps1_loadAdsData(...)`
  - then moved the same proof one step earlier again into [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1_parseAdsResource(...)`:
    - if exact `ACTIVITY.ADS` parsed with `uncompressedSize != 2558`
    - return `NULL`

- Validation runs:
  - [activity1-preload-metadata-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-preload-metadata-proof)
  - [activity1-parse-size-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-parse-size-proof)

- Result:
  - pre-load metadata proof changed the run back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - parse-time proof did not fire; run stayed on the improved post-`adsTags` branch:
    - `91d3e49ac1712390a97e7588519fa53d29b3a1cf33ae1f1696705a097ddb534d`

- Important conclusion:
  - `ACTIVITY.ADS` resource metadata parses correctly from the resource table
  - the bad `uncompressedSize` appears later, after resource-table build but before first visible ACTIVITY playback

- Cleanup:
  - reverted the temporary parse-time proof after validation

- Next target:
  - split the lazy-load path:
    - pilot-pack ADS load
    - raw-file fallback

## 00:39 PDT - The bad size comes from the pilot-pack ADS load path, not from authored pack content

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1_loadAdsData(...)`, for exact `ACTIVITY.ADS`:
    - if `ps1PilotLoadResource("ads", ...)` succeeds but returns `readSize != 2558`
    - drop the loaded pointer and return

- Validation run:
  - [activity1-pilotload-size-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-pilotload-size-proof)

- Result:
  - run changed back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the proof fired on the pilot-pack success path
  - so the bad ACTIVITY size is being sourced by `ps1PilotLoadResource(...)`
  - it is not explained by parse-time metadata and does not require the raw-file fallback path

- Additional validation:
  - the authored on-disk pack is clean:
    - `/home/hunter/workspace/jc_reborn/jc_resources/packs/ACTIVITY.PAK`
  - offline header parse shows:
    - `ACTIVITY.ADS` entry size `2558`

- Cleanup:
  - reverted the temporary pilot-load proof after validation

- Next target:
  - determine whether the in-memory pack index is already wrong when loaded, or only becomes wrong later in pack-entry lifetime

## 00:45 PDT - Runtime `ACTIVITY.PAK` index handling is still the live seam

- Temporary proofs:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`:
    - if exact `ACTIVITY.ADS` pack index decodes an `ads/ACTIVITY.ADS` entry with `sizeBytes != 2558`
    - return `0`
  - then tried two read-path variants for exact `ACTIVITY.ADS` pack index load:
    - whole-file read for the larger second header/index read only
    - whole-file read for both pack-header reads

- Validation runs:
  - [activity1-packindex-size-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-size-proof)
  - [activity1-packindex-wholeproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packindex-wholeproof)
  - [activity1-packheader-wholeproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packheader-wholeproof)

- Result:
  - all three cuts still land on the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the on-disk pack header is correct, but the runtime pack-index path for `ACTIVITY.PAK` is still the live seam
  - switching the larger index read, or both header reads, to the whole-file path did not recover the improved ACTIVITY branch
  - so the remaining suspect space is:
    - corrupted bytes returned by the runtime pack-index read path
    - or later corruption of the active pack entry table after a correct decode

- Cleanup:
  - reverted the temporary whole-read pack-index proof patches after validation

- Next target:
  - inspect active-pack table lifetime and ownership after `ps1PilotLoadPackIndex(...)`
  - specifically whether `ps1PilotActivePack.entries` is being overwritten after pack activation for `ACTIVITY.ADS`

## 00:50 PDT - `ps1PilotActivePack.entries` is already wrong immediately after pack activation

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1_loadAdsData(...)`, for exact `ACTIVITY.ADS`:
    - immediately after:
      - `ps1PilotSetActivePackForAds(adsResource->resName);`
    - looked up:
      - `ps1PilotFindEntry("ads", adsResource->resName)`
    - if the entry existed and `entry->sizeBytes != 2558`
      - returned immediately before `ps1PilotLoadResource(...)`

- Validation run:
  - [activity1-activepack-entry-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-activepack-entry-proof)

- Result:
  - run changed back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the proof fired before `ps1PilotLoadResource(...)` even ran
  - so the active-pack table is already wrong as soon as `ACTIVITY.ADS` pack activation completes
  - the live seam is now earlier than ADS byte loading itself:
    - `ps1PilotSetActivePackForAds(...)`
    - `ps1PilotLoadPackIndex(...)`
    - or stale/corrupted lifetime of `ps1PilotActivePack.entries` before this call returns

- Cleanup:
  - reverted the temporary active-pack-entry proof after validation

- Next target:
  - inspect pack activation and active-pack cache lifetime directly
  - specifically whether:
    - `ps1PilotSetActivePackForAds(...)` is reusing a stale `ps1PilotActivePack`
    - or `ps1PilotLoadPackIndex(...)` populates a bad in-memory table even though the on-disk pack is correct

## 00:55 PDT - Cold-resetting active/prefetch packs does not recover `ACTIVITY 1`

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1_loadAdsData(...)`, for exact `ACTIVITY.ADS`:
    - before `ps1PilotSetActivePackForAds(...)`, forcibly:
      - `ps1PilotResetActivePack();`
      - `ps1PilotResetPack(&ps1PilotPrefetchPack);`

- Validation run:
  - [activity1-coldpack-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-coldpack-proof)

- Result:
  - run still changed back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - stale active/prefetch cache reuse is not sufficient to explain the bad ACTIVITY pack entry
  - even a cold activation path still produces the wrong in-memory `ACTIVITY.PAK` table on current `HEAD`
  - the remaining live seam is now tighter:
    - fresh `ps1PilotLoadPackIndex("ACTIVITY.ADS", ...)`
    - or the CD read bytes feeding that fresh decode

- Cleanup:
  - reverted the temporary cold-pack proof after validation

- Next target:
  - instrument fresh `ps1PilotLoadPackIndex(...)` more directly
  - especially the decoded `entryCount` / `ACTIVITY.ADS` entry fields inside the newly allocated `entries[]` table before it is installed as the active pack

## 01:00 PDT - Fresh `ACTIVITY.PAK` header parse is already wrong on the first 20-byte read

- Authored on-disk constants:
  - `ACTIVITY.PAK` header decodes to:
    - `entryCount = 71`
    - `firstResourceOffset = 2048`
    - `prefetchCount = 1`

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - after the first `PS1_PACK_HEADER_SIZE` read and field decode
    - if any of:
      - `entryCount != 71`
      - `firstResourceOffset != 2048`
      - `prefetchCount != 1`
    - return `0`

- Validation run:
  - [activity1-packheader-fields-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packheader-fields-proof)

- Result:
  - run changed back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - this proof fired before any entry-table decode
  - so the very first 20-byte `ACTIVITY.PAK` header read is already wrong on the fresh runtime path
  - that rules out:
    - stale pack reuse
    - late active-pack entry corruption as the first cause
    - entry-table decode as the earliest fault
  - the live seam is now the pack-header CD read itself:
    - `ps1_streamReadFromCdFile(&cdfile, 0, PS1_PACK_HEADER_SIZE)`
    - or the `CdSearchFile` / `cdfile` basis feeding it

- Cleanup:
  - reverted the temporary pack-header-fields proof after validation

- Next target:
  - debug the `ACTIVITY.PAK` header CD read path directly
  - compare:
    - `ps1_streamReadFromCdFile(...)`
    - `ps1_streamReadFromCdFileWhole(...)`
    - and raw whole-file CD reads for the first 20-byte pack header only

## 01:05 PDT - Raw whole-file CD read for the first `ACTIVITY.PAK` header still fails the same way

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - replaced the first `PS1_PACK_HEADER_SIZE` read with:
      - `ps1_loadRawFile("\\PACKS\\ACTIVITY.PAK;1", &rawPackSize)`
    - copied only the first 20 bytes into `headerData`
    - then let normal header field decode continue

- Validation run:
  - [activity1-packheader-rawproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packheader-rawproof)

- Result:
  - run still changed back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - switching the first pack-header read from the range-read helper to the raw whole-file CD path did not recover the improved ACTIVITY branch
  - so the earliest fault is now above the simple choice of:
    - range read
    - whole-file slice
    - raw whole-file read
  - the live seam is now likely:
    - `CdSearchFile` / file-info basis for `ACTIVITY.PAK`
    - or broader CD state/path handling before the first header bytes are interpreted

- Cleanup:
  - reverted the temporary raw-header proof after validation

- Next target:
  - inspect the `CdlFILE` / path / file-size basis for `ACTIVITY.PAK` at pack activation time
  - specifically whether `CdSearchFile` is resolving the expected pack file metadata before any read occurs

## 01:10 PDT - `CdSearchFile` metadata for `ACTIVITY.PAK` is already wrong before any header bytes are read

- Authored on-disk fact:
  - `/home/hunter/workspace/jc_reborn/jc_resources/packs/ACTIVITY.PAK`
  - size:
    - `2836480` bytes

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - immediately after:
      - `CdSearchFile(&cdfile, "\\PACKS\\ACTIVITY.PAK;1")`
    - if:
      - `cdfile.size != 2836480`
    - return `0`

- Validation run:
  - [activity1-pack-cdfile-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-pack-cdfile-proof)

- Result:
  - run changed back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - this proof fired before any header bytes were read
  - so the earliest fault now exposed is `CdSearchFile` metadata for `ACTIVITY.PAK`
  - that moves the live seam above:
    - pack-header reads
    - entry-table decode
    - active-pack table lifetime
  - strongest current suspect:
    - path/file-resolution or disc-image lookup state for `PACKS\\ACTIVITY.PAK;1`

- Cleanup:
  - reverted the temporary `cdfile.size` proof after validation

- Next target:
  - inspect why `CdSearchFile` resolves the wrong `ACTIVITY.PAK` metadata on current `HEAD`
  - likely compare:
    - exact path string
    - disc image contents
    - and any conflicting duplicate files or naming/case issues in the PS1 image build

## 01:16 PDT - `CdSearchFile` is not just returning the wrong size; the resolved `ACTIVITY.PAK` start sector is wrong too

- Built image fact:
  - parsed directly from `/home/hunter/workspace/jc_reborn/jcreborn.bin`
  - ISO entry:
    - `PACKS/ACTIVITY.PAK;1`
    - extent `3652`
    - size `2836480`

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - immediately after `CdSearchFile(&cdfile, "\\PACKS\\ACTIVITY.PAK;1")`
    - required both:
      - `cdfile.size == 2836480`
      - `CdPosToInt(&cdfile.pos) == 3652`
    - else returned `0`

- Validation run:
  - [activity1-pack-cdfile-pos-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-pack-cdfile-pos-proof)

- Result:
  - run still changed back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the earliest exposed runtime fault is now stronger than “wrong size” alone
  - `CdSearchFile` is resolving `ACTIVITY.PAK` with metadata that does not match the built image’s actual ISO entry
  - that points the live seam at:
    - path/file resolution state inside the PS1 runtime / emulator path
    - or conflicting disc-image lookup behavior for `PACKS\\ACTIVITY.PAK;1`

- Cleanup:
  - reverted the temporary `cdfile.size + CdPosToInt(...)` proof after validation

- Next target:
  - inspect path-resolution behavior for `PACKS\\ACTIVITY.PAK;1` specifically
  - and compare against nearby known-good lookups to determine whether this is:
    - a pack-path-specific lookup fault
    - or a broader CD file-resolution problem

## 01:20 PDT - Resetting CD state immediately before `ACTIVITY.PAK` lookup does not recover the scene

- Temporary proof:
  - in [cdrom_ps1.c](/home/hunter/workspace/jc_reborn/cdrom_ps1.c) `ps1PilotLoadPackIndex(...)`, for exact `ACTIVITY.ADS`:
    - called:
      - `cdromResetState()`
    - immediately before:
      - `CdSearchFile(&cdfile, "\\PACKS\\ACTIVITY.PAK;1")`

- Validation run:
  - [activity1-packsearch-reset-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-packsearch-reset-proof)

- Result:
  - run still changed back to the old settled-ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - stale CD state alone is not sufficient to explain the bad `ACTIVITY.PAK` lookup
  - the live seam remains at or above the pack-specific `CdSearchFile` resolution path itself

- Cleanup:
  - reverted the temporary reset-before-search proof after validation

- Next target:
  - compare exact `CdSearchFile` resolution behavior for `ACTIVITY.PAK` against another known-good pack in the same image
  - to separate:
    - ACTIVITY-pack-specific lookup failure
    - from broader pack-directory/file-resolution corruption

## 22:58 PDT - ACTIVITY 4 on `6dae8410`: `story single` was broken on PS1; now narrowed to exact-scene no-launch

- Goal:
  - continue scene-by-scene PS1 validation from the older `6dae8410` base and prove whether `ACTIVITY 4` was actually honoring the canonical `story single 4` boot path

- Context:
  - older validation base:
    - `/tmp/jc_reborn_6dae`
  - target scene:
    - `ACTIVITY 4`
  - canonical boot string:
    - `story single 4`

- What I found:
  - in `/tmp/jc_reborn_6dae/jc_reborn.c`, the PS1 `BOOTMODE.TXT` parser only handled:
    - `story ads ...`
  - it silently ignored:
    - `story single ...`
  - that explains the earlier PS1 drift into unrelated `WALKSTUF` / `FISHING`-like content on `ACTIVITY 4`

- Fix applied in old worktree:
  - added:
    - `storySetBootSceneByIndex(int)` in `/tmp/jc_reborn_6dae/story.c`
  - declared it in:
    - `/tmp/jc_reborn_6dae/story.h`
  - taught PS1 boot parsing to accept:
    - `story single <index>`
    - in `/tmp/jc_reborn_6dae/jc_reborn.c`

- Validation run:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-singlefix`

- Result:
  - the scene shape changed immediately and materially
  - before fix:
    - long drift into `WALKSTUF`-looking ocean and then stable `FISHING`-like island content
  - after fix:
    - no unrelated later scene
    - run stays in title / black
    - `launched = false`
    - `likely_scene_not_started = true`
    - `likely_visual_broken = true`
  - this is useful progress:
    - the old random-scene misroute was real
    - the PS1 run is now exercising the intended exact `ACTIVITY 4` path

- Follow-up check:
  - ported the static `adsTags` lifetime fix into the same old base:
    - `/tmp/jc_reborn_6dae/ads.c`
  - validation run:
    - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-singlefix-staticadstags`

- A/B result:
  - representative frame hashes were byte-identical before / after:
    - `frame_00250`
    - `frame_00310`
    - `frame_00660`
    - `frame_01200`
  - so the `adsTags` heap-lifetime bug that moved `ACTIVITY 1` on current `HEAD` is not what blocks `ACTIVITY 4` on `6dae8410`

- Current read:
  - `6dae8410` remains the better PS1 validation base
  - `ACTIVITY 4` no longer looks like a random story-family reroute
  - the active bug is now narrower:
    - exact `ACTIVITY 4` canonical boot path is requested
    - but it still fails as a no-launch / title-black bootstrap path on this older base

- Next target:
  - debug the exact-scene bootstrap for `ACTIVITY 4` after the now-correct `story single 4` handoff, instead of chasing random later scene families

## 23:07 PDT - Direct `ads ACTIVITY.ADS 4` matches `story single 4` byte-for-byte on key frames

- Goal:
  - determine whether the remaining `ACTIVITY 4` failure on `6dae8410` is still in top-level story handoff, or already inside exact `ACTIVITY.ADS 4` playback/bootstrap

- Validation run:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-directads4`
  - boot string:
    - `island ads ACTIVITY.ADS 4`

- Comparison target:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-singlefix`
  - boot string:
    - `story single 4`

- Result:
  - representative captured frames are byte-identical between the two runs:
    - `frame_00310`
    - `frame_00660`
    - `frame_01200`
  - so the remaining `ACTIVITY 4` failure is not in the `story single` wrapper anymore
  - it reproduces even when we bypass story selection and boot exact `ACTIVITY.ADS 4` directly

- Important conclusion:
  - `story single` parser bug was real and fixed
  - but the current blocker is now deeper:
    - exact `ACTIVITY.ADS 4` bootstrap / playback itself
  - this is the right kind of narrowing for scene-by-scene PS1 debugging

- Next target:
  - inspect the first exact `ACTIVITY.ADS 4` bootstrap path directly:
    - `adsLoad(...)`
    - first `adsPlayChunk(...)`
    - startup `ADD_SCENE(...)` thread creation / survival

## 23:14 PDT - `ACTIVITY.ADS 4` is not the same first-chunk zero-thread failure shape as `ACTIVITY 1`

- Goal:
  - test whether exact `ACTIVITY.ADS 4` on `6dae8410` is failing in the same way `ACTIVITY 1` did earlier:
    - first `adsPlayChunk(...)` returns with `numThreads == 0`

- Temporary proof:
  - in `/tmp/jc_reborn_6dae/ads.c`
  - immediately after the first:
    - `adsPlayChunk(data, dataSize, offset);`
  - for exact:
    - `ACTIVITY.ADS`
    - tag `4`
  - if:
    - `numThreads == 0`
    - and `!adsStopRequested`
  - then:
    - `adsNoIsland();`
    - `return;`

- Validation run:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-firstchunk-nolaunch`

- Comparison target:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-directads4`

- Result:
  - representative frames remained byte-identical:
    - `frame_00310`
    - `frame_00660`
    - `frame_01200`
  - so the temporary first-chunk no-launch hook did not fire in any way that changed visible behavior

- Important conclusion:
  - `ACTIVITY 4` on `6dae8410` is not currently failing with the same exact first-chunk `numThreads == 0` signature that previously helped pin `ACTIVITY 1`
  - the remaining exact-scene bootstrap failure is subtler than that:
    - either startup threads exist briefly and die later
    - or the failure is happening on a different branch than the one just tested

- Cleanup:
  - reverted the temporary first-chunk no-launch proof patch

- Next target:
  - move one step deeper into exact `ACTIVITY.ADS 4` startup:
    - inspect which startup `ADD_SCENE(...)` calls actually execute
    - and whether those startup threads survive into the first main ADS loop iteration

## 23:20 PDT - `ACTIVITY.ADS 4` startup-chain proof is also a clean negative

- Goal:
  - determine whether exact `ACTIVITY.ADS 4` on `6dae8410` is reaching its authored startup chain in a way that affects the visible PS1 run

- Authored startup sequence extracted from `ACTIVITY.ADS` tag `4`:
  - `ADD_SCENE(2,1)`
  - `IF_LASTPLAYED 2,1 -> ADD_SCENE(2,3)`
  - `IF_LASTPLAYED 2,3 -> ADD_SCENE(2,2)`

- Temporary proof:
  - in `/tmp/jc_reborn_6dae/ads.c`
  - added a trap in `adsAddScene(...)`
  - for exact:
    - `ACTIVITY.ADS`
    - tag `4`
  - if startup scenes:
    - `(2,1)`
    - `(2,2)`
    - `(2,3)`
    - were being added
  - then:
    - `adsNoIsland();`
    - `return;`

- Validation run:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-startupchain-proof`

- Comparison target:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-directads4`

- Result:
  - representative frames remained byte-identical:
    - `frame_00310`
    - `frame_00660`
    - `frame_01200`
  - so this authored startup-chain trap did not move the visible run

- Important conclusion:
  - on the current exact `ACTIVITY.ADS 4` PS1 path, we are not reaching the authored `(2,1) -> (2,3) -> (2,2)` startup chain in any way that affects the visible outcome
  - combined with the earlier first-chunk no-launch negative, the live boundary moves earlier again:
    - before visible startup thread creation
    - likely in exact bootstrap resource/tag resolution or very-early chunk/control flow

- Cleanup:
  - reverted the temporary startup-chain trap after validation

- Next target:
  - inspect the exact bootstrap state before visible startup `ADD_SCENE(...)` activity:
    - tag resolution
    - first-chunk control flow
    - resource/slot binding for `ACTIVITY.ADS 4`

## 23:31 PDT - Post-`ttmLoadTtm()` slot-2 visual proofs are also negative for `ACTIVITY.ADS 4`

- Goal:
  - test whether exact `ACTIVITY.ADS 4` on `6dae8410` has an obviously wrong slot-2 resource binding immediately after the TTM load loop

- Context:
  - authored startup for `ACTIVITY.ADS` tag `4` only touches slot `2`
  - so slot `2` is the highest-value binding to probe first

- Temporary proofs tried in `/tmp/jc_reborn_6dae/ads.c` immediately after:
  - `ttmLoadTtm(&ttmSlots[adsResource->res[i].id], adsResource->res[i].name);`

- Proof variants:
  - if slot `2` is correctly bound to `MJDIVE.TTM`, force a visible return
  - if slot `2` is null / wrong, force a visible `NIGHT.SCR` load plus `grUpdateDisplay(...)` before return

- Validation runs:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-slot2-bind-proof`
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-slot2-nightproof`
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-slot2-nullproof`
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-slot2-nullproof2`

- Comparison target:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-directads4`

- Result:
  - representative frames remained byte-identical across all variants:
    - `frame_00310`
    - `frame_00660`
    - `frame_01200`

- Important conclusion:
  - the remaining exact `ACTIVITY.ADS 4` failure is earlier than every post-`ttmLoadTtm()` visual proof tried so far
  - these probes did not produce a visible state change, so they are not good enough as the next proof surface
  - current best boundary remains:
    - exact ACTIVITY path is requested
    - direct `ads ACTIVITY.ADS 4` matches `story single 4`
    - but the visible failure persists before any startup-thread or post-load resource proof has produced a perturbation

- Cleanup:
  - reverted the temporary slot-2 visual proof patches

- Next target:
  - move earlier than the post-load visual probes:
    - exact tag-offset resolution in `adsLoad(...)`
    - or pre-display bootstrap control flow that still leaves the title surface intact

## 23:39 PDT - Even a top-of-`adsPlay(ACTIVITY.ADS, 4)` visible return is inert on the captured output

- Goal:
  - verify whether the current title-like captured output is even a trustworthy proof surface for exact `ACTIVITY.ADS 4` playback on `6dae8410`

- Strong sanity-check proof:
  - in `/tmp/jc_reborn_6dae/ads.c`
  - at the top of exact:
    - `adsPlay("ACTIVITY.ADS", 4)`
  - forced:
    - `grLoadScreen("NIGHT.SCR")`
    - `grUpdateDelay = 100`
    - `grUpdateDisplay(NULL, ttmThreads, NULL)`
    - immediate `return`

- Validation run:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-adsplay-nightproof`

- Comparison target:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-directads4`

- Result:
  - representative frames remained byte-identical:
    - `frame_00310`
    - `frame_00660`
    - `frame_01200`

- Important conclusion:
  - the current title-like captured output is not a trustworthy proof surface for exact `ACTIVITY.ADS 4` playback on this older base
  - after this many inert visible proofs, continuing to infer control flow from the stable title surface is no longer defensible
  - the debugging strategy needs to change:
    - stop relying on visible-frame perturbations at this boundary
    - move to a different runtime-state proof surface

- Cleanup:
  - reverted the temporary top-of-`adsPlay()` proof patch
  - reverted the temporary complementary tag-offset proof patch

- Next target:
  - switch `ACTIVITY 4` debugging to a non-visual state surface on `6dae8410`
  - likely candidates:
    - runtime state hash / RAM hash transitions
    - durable story / ADS state persisted into telemetry in a form that actually survives on this branch

## 23:45 PDT - Non-visual validation confirmed: exact `adsPlay(ACTIVITY.ADS, 4)` proof does execute even though frames do not move

- Goal:
  - verify that the inert frame proofs on `ACTIVITY 4` were not simply dead code paths

- Comparison:
  - baseline direct exact ADS run:
    - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-directads4/printf.log`
  - top-of-`adsPlay(ACTIVITY.ADS, 4)` proof run:
    - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-adsplay-nightproof/printf.log`

- Result:
  - captured frames stayed byte-identical, but the non-visual runtime surface changed substantially
  - baseline:
    - `Save State Hash = 048bcd0b774df0f7099975e33e26364b19cb2c6e3b6010e3a838b899b241d7c4`
    - `RAM Hash = c492897409507dcb852cab1f8886ed64cc3efe94fa7341fd663ca207c1eb781a`
    - `Total execution time = 2545.77 ms`
  - proof run:
    - `Save State Hash = de10700000100da320b4b3366e475d78a1b95c1571c864067f169d3b532a602c`
    - `RAM Hash = 5c0939e6a6c10e1b2e8fa6e54f8de2973a651810f44b112e8569c172a49c3d91`
    - `Total execution time = 3782.95 ms`
  - `VRAM Hash` stayed the same

- Important conclusion:
  - exact `ACTIVITY.ADS 4` playback is executing on `6dae8410`
  - the current captured frame surface is simply not trustworthy at this boundary
  - `RAM Hash` / `Save State Hash` / execution-time changes are a better proof surface here than the PNGs

- Next target:
  - continue `ACTIVITY 4` using non-visual runtime-state proofs first
  - only use frames secondarily, once a proof is known to touch VRAM or display ownership

## 23:46 PDT - Fresh-build timeout proof shows `island ads ACTIVITY.ADS 4` is not reaching `main()`'s `argAds` branch on `6dae8410`

- Goal:
  - verify the actual entrypoint for the old-base “direct ADS” runs, because earlier ADS-level timeout proofs were exiting cleanly even after fresh rebuilds

- Fresh-build check:
  - touched `/tmp/jc_reborn_6dae/jc_reborn.c`
  - confirmed rebuild picked it up:
    - `jc_reborn.c.obj`
    - `jcreborn.exe`
    - all rebuilt at `23:34`

- Temporary proof:
  - in `/tmp/jc_reborn_6dae/jc_reborn.c`
  - at the top of:
    - `else if (argAds && numArgs >= 2)`
  - inserted:
    - `while (1) { }`

- Validation run:
  - `/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-main-anyargads-timeoutproof`
  - boot string:
    - `island ads ACTIVITY.ADS 4`
  - timeout budget:
    - `15s`

- Result:
  - run still exited cleanly
  - it did not time out
  - so the old-base direct-ADS boot string is not reaching `main()`'s `argAds` branch

- Important conclusion:
  - previous “direct ADS” runs on `6dae8410` were not actually proving ADS-branch entry
  - that invalidates the earlier assumption that `island ads ACTIVITY.ADS 4` was already inside exact ADS playback on this branch
  - the active root-cause surface moves up:
    - PS1 boot-override parsing / retention / dispatch for `island ads ...`

- Cleanup:
  - reverted the temporary `argAds` timeout proof in `/tmp/jc_reborn_6dae/jc_reborn.c`
  - reverted the temporary `adsLoad(...)` timeout proof in `/tmp/jc_reborn_6dae/ads.c`

- Next target:
  - inspect why `island ads ...` is not reaching `argAds` on `6dae8410`
  - specifically:
    - `ps1LoadBootOverride()`
    - `ps1ApplyBootOverride()`
    - `argPlayAll` / `argAds` / `numArgs` state after parsing

## 21:54 PDT - `adsLoad(...)` heap allocation was corrupting ACTIVITY bootstrap; static ADS tag storage removes the old ocean failure

- First exact-bytes proof:
  - temporary proof in [ads.c](/home/hunter/workspace/jc_reborn/ads.c) blacked only if the first exact `ACTIVITY.ADS 1` load saw:
    - `dataSize == 2558`
    - the expected extracted byte prefix
  - validation:
    - [ads-bytes-firstproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-ads-bytes-firstproof/20260328-214718/frames/jcreborn/frame_04910.png)

- Result:
  - boundary frames blacked:
    - `frame_04900 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
    - `frame_04910 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
    - `frame_05000 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - so the first exact PS1 `ACTIVITY.ADS` payload bytes are already correct before `adsLoad(...)`

- Allocation-vs-scan split:
  - replaced the temporary `adsTags = safe_malloc(...)` allocation with a static scratch buffer only for the first exact `ACTIVITY.ADS 1` load
  - blacked only if `ttmSlots[1].ttmResource == GJDIVE.TTM` still held immediately after `adsLoad(...)`
  - validation:
    - [adsload-static-tags](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-adsload-static-tags/20260328-215017/frames/jcreborn/frame_04910.png)

- Result:
  - the boundary frames blacked again
  - so the active corruption surface was the `adsLoad(...)` heap allocation path, not ACTIVITY byte delivery and not the ADS scan logic alone

- Current code change:
  - converted PS1 `adsTags` storage in [ads.c](/home/hunter/workspace/jc_reborn/ads.c) from per-play heap allocation to a persistent static buffer with capacity guard:
    - `PS1_STATIC_ADS_TAG_CAPACITY = 1024`

- Clean validation run with the real change:
  - [static-adstags-fix](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-static-adstags-fix/20260328-215232/frames/jcreborn/frame_04910.png)

- Clean longer validation:
  - [static-adstags-fix-6300](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-static-adstags-fix-6300/20260328-215318/frames/jcreborn/frame_06000.png)

- Important result:
  - the old settled ACTIVITY ocean hash is gone
  - boundary frames now stay black through the old failure onset:
    - `frame_04920 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - later frames no longer match the old ocean either:
    - `frame_06000 = 91d3e49ac1712390a97e7588519fa53d29b3a1cf33ae1f1696705a097ddb534d`
    - `frame_06150 = 91d3e49ac1712390a97e7588519fa53d29b3a1cf33ae1f1696705a097ddb534d`
    - `frame_06300 = 91d3e49ac1712390a97e7588519fa53d29b3a1cf33ae1f1696705a097ddb534d`

- Current conclusion:
  - this is the first material PS1 runtime fix on `ACTIVITY 1`
  - the `adsLoad(...)` heap allocation really was corrupting bootstrap state
  - but `ACTIVITY 1` is not fully fixed yet; it has moved from the old settled-ocean failure to a new later failure surface

- Next target:
  - characterize the new `06000+` stable state after the static-`adsTags` fix
  - then continue scene-by-scene debugging from that cleaner post-bootstrap runtime

## 22:36 PDT - Step-back validation: `6dae8410` is a materially better PS1 base than current HEAD for `ACTIVITY 4`

- Reason for step-back:
  - current `ps1` HEAD had regressed too far for validation sweeps:
    - `ACTIVITY 1` and `ACTIVITY 4` were collapsing to black-screen/fallback behavior
  - that made the current tree unsuitable as a “closest working” PS1 candidate

- Validation base:
  - older detached worktree:
    - [/tmp/jc_reborn_6dae](/tmp/jc_reborn_6dae)
  - commit:
    - `6dae8410`
    - `2026-03-21 09:02:06 -0700`
    - `Fix PS1 first-handoff blackscreen`

- Harness bridge:
  - copied current regtest harness scripts into the older worktree just for validation
  - fixed that older snapshot’s stale [build-ps1.sh](/tmp/jc_reborn_6dae/scripts/build-ps1.sh), which was still calling:
    - `make jcreborn`
  - updated it to use the existing CMake-based `build-ps1/` tree

- One-scene validation:
  - ran:
    - `ACTIVITY 4`
    - `4200` frames
    - `Interpreter`
  - output:
    - [/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-check/result.json](/tmp/jc_reborn_6dae/regtest-results/activity4-6dae-check/result.json)

- Result:
  - older `6dae8410` snapshot reaches real non-black scene content
  - last visual scene on that run:
    - `screen_type = island`
    - `scene_family = FISHING`
    - `johnny_present = true`
    - `sand_present = true`
  - hashes:
    - `state_hash = c979c7cb2a17e672791991117ed14777f3441fe6b074cd69f5683189d7435167`
    - best scene frame:
      - `frame_03150.png`
      - `frame_scene_pixel_sha256 = 76de45bc2265ba5180709b2273f5a5f655459a3072d30b27d45c23de9f195426`
    - last scene frame:
      - `frame_04200.png`
      - `frame_scene_pixel_sha256 = 54a1f25fab3e3fa0ae236af60b06c022d556d8bdd686cfedff8104e1f860b9bf`

- Important conclusion:
  - even before the exact compare JSON is finalized, this is already enough to choose direction:
  - `6dae8410` is materially healthier than current HEAD for one-scene validation
  - current HEAD should not be used as the candidate PS1 sweep/debug base

- Next target:
  - keep validating from `6dae8410` one scene at a time
  - confirm the exact `ACTIVITY 4` compare verdict when the compare tool completes
  - if that remains the best behavior, continue scene debugging from the older base instead of current HEAD

## 21:20 PDT - ACTIVITY startup failure is now narrowed to missing slot-1 resource binding before first chunk playback

- Temporary proof family in [ads.c](/home/hunter/workspace/jc_reborn/ads.c):
  - black-and-return only for exact `ACTIVITY.ADS 1` under progressively narrower conditions
  - all probes were run under `--cpu Interpreter` to avoid DuckStation recompiler crashes

- Validation runs:
  - [startup-survival-proof-interp](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-startup-survival-proof-interp/20260328-211458/frames/jcreborn/frame_04910.png)
  - [startup-zeroip-only](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-startup-zeroip-only/20260328-211613/frames/jcreborn/frame_04910.png)
  - [startup-slot1-ttmproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-startup-slot1-ttmproof/20260328-211819/frames/jcreborn/frame_04910.png)
  - [startup-slot1-nullproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-startup-slot1-nullproof/20260328-211918/frames/jcreborn/frame_04910.png)
  - [slot1-prechunk-nullproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-slot1-prechunk-nullproof/20260328-212046/frames/jcreborn/frame_04910.png)
  - [slot1-resource-list-proof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-slot1-resource-list-proof/20260328-212156/frames/jcreborn/frame_04910.png)

- Result:
  - every proof above drove `frame_04900`, `frame_04910`, and `frame_05000` to the same black hash:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`

- Important conclusions:
  - the first exact ACTIVITY chunk really does reach authored startup `ADD_SCENE(1,12)` / `ADD_SCENE(1,13)`
  - a zero-`ip` birth proof alone is sufficient to black out the stable ocean band
  - by `adsAddScene(1,12/13)`, slot `1` is not bound to live `GJDIVE.TTM`
  - a null-only slot-1 proof also blacks the band, so this is not just “wrong non-null TTM”; slot `1` is effectively unbound
  - moving the proof earlier, immediately after the `ttmLoadTtm()` loop and before first-chunk playback, still blacks the band
  - finally, proving only that the parsed ACTIVITY resource list lacks `slot 1 -> GJDIVE.TTM` also blacks the band

- Current best read:
  - the ACTIVITY failure is now upstream of scene-thread lifetime
  - on PS1, the parsed `ACTIVITY.ADS` resource mapping reaching runtime does not preserve the expected `slot 1 -> GJDIVE.TTM` binding
  - that explains why authored startup tags `12/13` become `ip == 0` at runtime and no ACTIVITY startup thread survives first-chunk bootstrap

- Cleanup:
  - reverted the temporary proof patches after validation; only the worklog entry remains

- Next target:
  - inspect why PS1 `ACTIVITY.ADS` resource metadata loses `slot 1 -> GJDIVE.TTM`
  - likely surfaces:
    - PS1 ADS resource parse/list integrity
    - ACTIVITY resource-list contents at runtime versus expected authored mapping
    - any slot reset/overwrite before first chunk playback

## 21:35 PDT - Raw `RESOURCE.001` parse confirms ACTIVITY source asset is correct

- Parsed the on-disk `ACTIVITY.ADS` entry directly from:
  - [RESOURCE.MAP](/home/hunter/workspace/jc_reborn/jc_resources/RESOURCE.MAP)
  - [RESOURCE.001](/home/hunter/workspace/jc_reborn/jc_resources/RESOURCE.001)

- Important detail:
  - the ADS `RES:` table uses variable-length NUL-terminated names, matching `getString(..., 40)` / `ps1_getString(..., 40)`, not fixed 40-byte records

- Direct raw result for `ACTIVITY.ADS`:
  - `numRes = 6`
  - `slot 1 -> GJDIVE.TTM`
  - `slot 2 -> MJDIVE.TTM`
  - `slot 4 -> MJREAD.TTM`
  - `slot 5 -> MJBATH.TTM`
  - `slot 6 -> GJNAT1.TTM`
  - `slot 7 -> GJNAT3.TTM`

- Important conclusion:
  - the source asset on disk is correct
  - the failure is not in authored ACTIVITY metadata
  - the remaining bug surface is after bytes leave disk:
    - PS1 runtime parse/in-memory ADS resource state
    - or later slot reset/overwrite before first chunk playback

- Cleanup:
  - reverted an unstable first-entry positive proof patch after it failed to produce a clean capture window

- Next target:
  - inspect live in-memory `adsResource->numRes/res[]` state on the PS1 path without relying on repeated-entry proofs
  - then inspect whether any slot reset occurs between `ttmLoadTtm()` and the first startup `adsAddScene(1,12/13)`

## 21:45 PDT - First exact ACTIVITY in-memory ADS list and slot-1 TTM load are both correct

- Temporary first-entry proofs in [ads.c](/home/hunter/workspace/jc_reborn/ads.c), all under `--cpu Interpreter`:
  - black-and-return only if the first exact `adsPlay("ACTIVITY.ADS", 1)` sees the full expected six-entry in-memory `adsResource->res[]` mapping
  - then black-and-return only if that same first exact path sees `ttmSlots[1].ttmResource == GJDIVE.TTM` immediately after the `ttmLoadTtm()` loop

- Validation runs:
  - [reslist-firstproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-reslist-firstproof/20260328-213640/frames/jcreborn/frame_04910.png)
  - [slot1-load-firstproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-slot1-load-firstproof/20260328-213740/frames/jcreborn/frame_04910.png)

- Result:
  - both proofs turned `frame_04900`, `frame_04910`, and `frame_05000` black:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`

- Important conclusion:
  - on the first exact `ACTIVITY.ADS 1` playback:
    - the in-memory ADS resource list is already correct
    - and `ttmSlots[1]` is correctly bound to `GJDIVE.TTM` immediately after the load loop
  - so the slot-1 loss does **not** happen in:
    - source asset metadata
    - PS1 ADS resource parsing
    - or the initial `ttmLoadTtm()` binding step
  - the remaining live boundary is now very tight:
    - between the end of the `ttmLoadTtm()` loop and the first startup `adsAddScene(1,12/13)`

- Cleanup:
  - reverted the temporary first-entry proof patches after validation

- Next target:
  - inspect what mutates or invalidates slot `1` in that narrow pre-chunk bootstrap span
  - likely surfaces:
    - `adsLoad(...)`
    - `adsPlayChunk(...)`
    - or a slot reset/overwrite side effect before `adsAddScene(1,12/13)` runs

## 21:55 PDT - `adsLoad(...)` is the active boundary, but not via simple tag/chunk table overflow

- Temporary boundary proof in [ads.c](/home/hunter/workspace/jc_reborn/ads.c):
  - on the first exact `ACTIVITY.ADS 1` playback only
  - black-and-return immediately after `adsLoad(...)` if `ttmSlots[1]` is already not bound to `GJDIVE.TTM`

- Validation run:
  - [prechunk-slot1-loss](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-prechunk-slot1-loss/20260328-214146/frames/jcreborn/frame_04910.png)

- Result:
  - `frame_04900`, `frame_04910`, `frame_05000` all black:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`

- Important conclusion:
  - the slot-1 loss is already present immediately after `adsLoad(...)`
  - so the current corruption boundary is:
    - after `ttmLoadTtm()` succeeds for `slot 1 -> GJDIVE.TTM`
    - but before the first `adsPlayChunk(...)` executes ACTIVITY bytecode

- Follow-up static validation using [jc_resources/extracted/ads/ACTIVITY.ADS](/home/hunter/workspace/jc_reborn/jc_resources/extracted/ads/ACTIVITY.ADS):
  - simulated the exact `adsLoad(...)` scanner on the real decompressed ACTIVITY bytecode
  - observed:
    - `adsNumTags = 10`, matching metadata from `RESOURCE.001`
    - bookmarked chunk counts stay well below `MAX_ADS_CHUNKS`

- Important refinement:
  - this is not explained by a simple `adsTags` overflow
  - and not by a simple `adsChunks` overflow
  - `adsLoad(...)` remains the active boundary, but the failure is subtler than straightforward table overrun from ACTIVITY bytecode shape

- Cleanup:
  - reverted the temporary post-`adsLoad(...)` proof patch after validation

- Next target:
  - inspect heap/object lifetime side effects inside `adsLoad(...)`
  - especially:
    - `adsTags = safe_malloc(...)`
    - `adsReleaseAds()/free(adsTags)` lifetime assumptions
    - any corruption caused by repeated ACTIVITY exact-entry bookkeeping versus allocator reuse

## 20:16 PDT - Quadrant-color upload proof collapsed the late ACTIVITY frames to near-black, but did not identify tile ownership

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), replaced the normal late `grDrawBackground()` upload source for exact `ACTIVITY.ADS 1` during `04900..04910`
  - each quadrant tile uploaded from a different solid-color buffer:
    - top-left: red
    - top-right: green
    - bottom-left: blue
    - bottom-right: white

- Validation run:
  - [quadrant-proof-4900-4910](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-quadrant-proof-4900-4910)

- Result:
  - late sampled frames all collapsed to the same new hash:
    - `frame_04900 = 1c7b028eeb1335dc37111468c89ff4c9f2194efe3645fae8656d5114bebf52b5`
    - `frame_04910 = 1c7b028eeb1335dc37111468c89ff4c9f2194efe3645fae8656d5114bebf52b5`
    - `frame_05000 = 1c7b028eeb1335dc37111468c89ff4c9f2194efe3645fae8656d5114bebf52b5`
  - simple quadrant averages on `frame_04910` were all essentially black:
    - top-left: `(0, 0, 1)`
    - top-right: `(0, 0, 0)`
    - bottom-left: `(0, 0, 0)`
    - bottom-right: `(0, 0, 0)`

- Important conclusion:
  - this was not informative enough to identify which tile/quadrant owns the settled ACTIVITY ocean
  - the proof only showed that substituting the late upload source in this way can collapse the frame to near-black
  - the next useful surface remains the real normal upload-source content at `04910`, not another quadrant substitution

- Cleanup:
  - reverted the temporary quadrant-color proof patch after validation

- Next target:
  - inspect or checksum the real `grDrawBackground()` tile-upload source content at the `04910` handoff window

## 20:28 PDT - By `04900..05000`, no normal background-tile uploads are active anymore on the stable ACTIVITY failure path

- Temporary telemetry:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), added compact-strip values for:
    - per-tile `grDrawBackground()` upload source signatures
    - an upload-active bitmask showing which of the four background tiles actually reached `LoadImage(...)` this frame
  - decoded them in [decode-ps1-bars.py](/home/hunter/workspace/jc_reborn/scripts/decode-ps1-bars.py)

- Validation run:
  - [uploadsig2](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-uploadsig2)

- Decoded boundary frames:
  - `frame_04900`:
    - `upload_active_mask_estimate = 0`
    - all four `upload_sig_tile* = 0`
  - `frame_04910`:
    - `upload_active_mask_estimate = 0`
    - all four `upload_sig_tile* = 0`
  - `frame_05000`:
    - `upload_active_mask_estimate = 0`
    - all four `upload_sig_tile* = 0`

- Important conclusion:
  - by the settled bad-ocean band, `grDrawBackground()` is no longer performing any normal per-tile uploads at all
  - that reconciles the earlier renderer proofs:
    - forcing a black upload source only changes the result when uploads are explicitly forced into the band
    - otherwise the visible ACTIVITY ocean is already persistent before `04900`
  - so the active boundary is now earlier than the late renderer upload path
  - the next useful target is no longer “what gets uploaded at `04910`,” but:
    - where the last successful background upload happens before the ocean becomes persistent
    - and why no later ACTIVITY scene content dirties/uploads the framebuffer after that point

- Next target:
  - move the renderer proof surface earlier than `04900`
  - identify the final frame where any background tile upload is still active, then debug the transition from that last upload into the settled persistent ocean

## 20:33 PDT - The last decoded normal background upload in the ACTIVITY run is back at `frame_0610`

- Follow-up on [uploadsig2](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-uploadsig2):
  - scanned the full decoded telemetry timeline for the new:
    - `upload_active_mask_estimate`
    - `upload_sig_tile*`

- Result:
  - the last frame with any nonzero decoded upload activity is:
    - `frame_0610`
  - from `frame_0620` onward, `upload_active_mask_estimate = 0`
  - that includes the entire settled failure band:
    - `frame_04900`
    - `frame_04910`
    - `frame_05000`

- Important conclusion:
  - the stable ACTIVITY ocean is not being maintained by normal `grDrawBackground()` uploads anywhere near the failure onset
  - by the time the run reaches the long black lead-in and then the settled `04910+` ocean, the normal background upload path is already dormant
  - that is consistent with the earlier blackproofs:
    - forced uploads can overwrite the persistent framebuffer
    - but the normal path is no longer uploading there

- Current read:
  - the active bug is now better described as:
    - normal background upload activity stops very early
    - then some other path leaves or restores a persistent ocean/title-derived visible framebuffer state
    - and no later ACTIVITY scene content ever dirties/uploads over it

- Next target:
  - stop treating `04910` as an active upload site
  - debug the direct framebuffer/background ownership path after uploads stop:
    - title/intro display persistence
    - direct screen loads
    - any display-page reuse that survives while the ACTIVITY scene never reclaims the framebuffer

## 20:43 PDT - One-shot post-init framebuffer clear removes the stale `04900` title, but not the settled `04910+` ocean

- Temporary proof:
  - in [jc_reborn.c](/home/hunter/workspace/jc_reborn/jc_reborn.c), for exact `ACTIVITY.ADS 1` only:
    - immediately after `graphicsInit()` and palette setup
    - called:
      - `grInitEmptyBackground()`
      - `grDrawBackground()`
      - `VSync(0)`
  - this performs a one-shot black framebuffer handoff after the early title path, before `storyPlay()`

- Validation run:
  - [postinit-clear2](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-postinit-clear2)

- Result:
  - `frame_04900` changed from the old late-title frame to black:
    - new hash: `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - but the settled ACTIVITY ocean did not move:
    - `frame_04910 = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
    - `frame_05000 = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - there really is stale pre-story framebuffer/title persistence contributing to the late `04900` frame
  - but that is not the root cause of the settled ACTIVITY failure
  - the later `04910+` ocean is being established independently of that stale title residue

- Cleanup:
  - reverted the temporary post-init framebuffer-clear proof patch after validation

- Next target:
  - treat `04900` title persistence and `04910+` settled ocean as two separate phenomena
  - continue debugging the first point where the persistent ACTIVITY ocean itself is established

## 20:46 PDT - The settled `04910+` ACTIVITY ocean comes directly from the exact `OCEAN0?.SCR` bootstrap load in `islandInit()`

- Temporary proof:
  - in [island.c](/home/hunter/workspace/jc_reborn/island.c), for exact `ACTIVITY.ADS 1` only:
    - replaced only the initial:
      - `grLoadScreen("OCEAN0?.SCR")`
    - with:
      - `grInitEmptyBackground()`
  - left the rest of `islandInit()` intact:
    - raft/cloud/background BMP loads
    - island sprite composition
    - initial wave draws
    - later ACTIVITY flow unchanged

- Validation run:
  - [skip-ocean-load](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-skip-ocean-load)

- Result:
  - all three boundary frames turned black:
    - `frame_04900 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
    - `frame_04910 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
    - `frame_05000 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`

- Important conclusion:
  - this is the strongest causal proof so far:
    - the settled `04910+` ACTIVITY ocean is coming directly from the exact bootstrap `OCEAN0?.SCR` load in `islandInit()`
    - it is not being independently recreated later by other island composition or fallback logic
  - that means the remaining ACTIVITY failure is now best described as:
    - the bootstrap ocean background loads successfully
    - but the intended ACTIVITY scene content never later reclaims or replaces it

- Cleanup:
  - reverted the temporary skip-ocean-load proof patch after validation

- Next target:
  - focus directly on why exact ACTIVITY playback never replaces the already-correctly-loaded bootstrap ocean background

## 20:52 PDT - Direct black-and-return at the top of `storyPlayPreparedScene(ACTIVITY.ADS, 1)` still does not move the settled `04910+` ocean

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for exact `ACTIVITY.ADS 1` only:
    - at the top of `storyPlayPreparedScene(scene, prevSpot, prevHdg)`
    - called:
      - `grInitEmptyBackground()`
      - `grDrawBackground()`
    - then returned `0` immediately

- Validation run:
  - [playprepared-blackreturn](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-playprepared-blackreturn)

- Result:
  - lead-in changed:
    - `frame_04900 = d9ee0d2e8f8465ea471c9c79223f1177c7225a238274f7f68ab32ed94312c0a0`
  - but the settled ACTIVITY ocean remained unchanged:
    - `frame_04910 = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
    - `frame_05000 = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - this is another strong negative on the straightforward top-level playback path
  - the settled `04910+` ocean is still not being controlled by the obvious exact `storyPlayPreparedScene(ACTIVITY.ADS, 1)` hook point
  - so the strongest remaining read is unchanged:
    - exact bootstrap ocean background is loaded correctly
    - but the later stable failure path that leaves it on screen is bypassing, outliving, or otherwise decoupling from the straightforward exact prepared-scene playback hook

- Cleanup:
  - reverted the temporary `storyPlayPreparedScene()` black-return proof patch after validation

- Next target:
  - move below top-level story hooks again
  - identify the alternate ACTIVITY path that preserves the bootstrap ocean while the obvious exact prepared-scene playback hook remains behaviorally negative

## 20:56 PDT - Even returning from `storyPlay()` immediately after an exact no-launch still does not move the settled `04910+` ocean

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), wrapped:
    - `storyPlayPreparedScene(finalScene, prevSpot, prevHdg)`
  - if it returned false for exact `ACTIVITY.ADS 1`, immediately:
    - `grInitEmptyBackground()`
    - `grDrawBackground()`
    - `return`
  - this bypassed the obvious retry loop after an exact prepared-scene no-launch

- Validation run:
  - [no-launch-return](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-no-launch-return)

- Result:
  - lead-in changed:
    - `frame_04900 = d9ee0d2e8f8465ea471c9c79223f1177c7225a238274f7f68ab32ed94312c0a0`
  - but the settled ACTIVITY ocean still did not move:
    - `frame_04910 = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
    - `frame_05000 = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled `04910+` ACTIVITY ocean is still not explained by the obvious exact no-launch retry path in `storyPlay()`
  - this is another strong negative on the straightforward top-level story control flow
  - the remaining failure path is still behaving as if it bypasses, outlives, or otherwise decouples from the obvious exact prepared-scene playback/no-launch branch

- Cleanup:
  - reverted the temporary no-launch-return proof patch after validation

- Next target:
  - keep the focus below top-level story control flow
  - the strongest remaining suspect surface is now the ADS bootstrap/scene-thread lifecycle itself, after the bootstrap ocean background has been loaded

## 21:01 PDT - If exact ACTIVITY still has `numThreads == 0` immediately after the first `adsPlayChunk()`, the whole late boundary turns black

- Temporary proof:
  - in [ads.c](/home/hunter/workspace/jc_reborn/ads.c), for exact `ACTIVITY.ADS 1` only:
    - immediately after the first:
      - `adsPlayChunk(data, dataSize, offset)`
    - if `numThreads == 0`, then:
      - `grInitEmptyBackground()`
      - `grDrawBackground()`
      - `return`

- Validation run:
  - [firstchunk-nolaunch-return](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-firstchunk-nolaunch-return)

- Result:
  - all three boundary frames turned black:
    - `frame_04900 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
    - `frame_04910 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
    - `frame_05000 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`

- Important conclusion:
  - this is the strongest ADS bootstrap proof so far
  - the settled ACTIVITY ocean is consistent with the exact first ADS chunk returning with no launched scene threads
  - in other words:
    - bootstrap ocean background loads
    - first exact `adsPlayChunk()` does not produce any running ACTIVITY scene thread
    - and later nothing reclaims the framebuffer from that bootstrap state

- Cleanup:
  - reverted the temporary firstchunk-nolaunch-return proof patch after validation

- Next target:
  - debug why the exact first `adsPlayChunk()` path for `ACTIVITY.ADS 1` leaves `numThreads == 0`
  - that is now the narrowest live root-cause surface

## 21:06 PDT - The first exact ACTIVITY chunk definitely reaches authored startup `ADD_SCENE(1,12)` / `ADD_SCENE(1,13)`

- Temporary proof:
  - in [ads.c](/home/hunter/workspace/jc_reborn/ads.c), for exact `ACTIVITY.ADS 1` only:
    - inside `adsAddScene(ttmSlotNo, ttmTag, arg3)`
    - if the scene being added was authored startup slot/tag:
      - `1:12` or `1:13`
    - then:
      - `grInitEmptyBackground()`
      - `grDrawBackground()`
      - `return`

- Validation run:
  - [addscene-init12-13-return](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-addscene-init12-13-return)

- Result:
  - all three boundary frames turned black:
    - `frame_04900 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
    - `frame_04910 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
    - `frame_05000 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`

- Important conclusion:
  - this proves the first exact ACTIVITY chunk really is reaching the authored startup `ADD_SCENE` calls for:
    - `slot 1 / tag 12`
    - `slot 1 / tag 13`
  - combined with the earlier first-chunk no-launch proof, the remaining root-cause surface is now extremely tight:
    - authored startup `ADD_SCENE` is reached
    - but by the time `adsPlayChunk()` returns, no running scene thread survives (`numThreads == 0`)
  - so the bug is no longer “wrong chunk” or “startup `ADD_SCENE` never hit”
  - it is now in the immediate lifetime of those just-added startup threads

- Cleanup:
  - reverted the temporary `adsAddScene(1:12/1:13)` proof patch after validation

- Next target:
  - debug why the just-added ACTIVITY startup threads do not survive past the first-chunk bootstrap
  - the narrowest live candidates are:
    - duplicate/running suppression
    - immediate stop/termination in the same chunk
    - zero/invalid TTM entrypoint after add

## 19:39 PDT - Late ACTIVITY background-thread suppression does not move the settled ocean

- Temporary proof:
  - in [ads.c](/home/hunter/workspace/jc_reborn/ads.c), suppressed only:
    - `islandAnimate(&ttmBackgroundThread)`
    - `islandRedrawWave(&ttmBackgroundThread)`
  - only for exact `ACTIVITY.ADS 1` after `grGetCurrentFrame() >= 1000`
  - left all `ttmPlay(...)` scene-thread playback intact

- Validation run:
  - direct frame capture under:
    - [bgthread-suppress](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-bgthread-suppress)

- Result:
  - `frame_04910` stays byte-identical to the original settled ocean:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - `frame_05000` is also byte-identical to the same hash
  - full sampled-run concatenated PNG hash:
    - `31354944eee50c711b70eecc308bb70028cd04247471e2646de8b0ad45fa5002`

- Important conclusion:
  - the late stable ACTIVITY ocean is not being sustained by the background thread
  - neither late wave redraw nor late background animation is the repopulation path

- Cleanup:
  - reverted the temporary late background-thread suppression proof patch

## 19:41 PDT - Late `ttmPlay()` suppression also does not move the settled ocean

- Temporary proof:
  - in [ads.c](/home/hunter/workspace/jc_reborn/ads.c), suppressed:
    - `ttmPlay(&ttmThreads[i])`
  - only for exact `ACTIVITY.ADS 1` after `grGetCurrentFrame() >= 1000`
  - left background-thread work intact

- Validation run:
  - direct frame capture under:
    - [ttm-suppress](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-ttm-suppress)

- Result:
  - `frame_04910` stays byte-identical to the original settled ocean:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - `frame_05000` is also identical
  - early lead-in still differs:
    - `frame_04770 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - full sampled-run concatenated PNG hash is unchanged from the background-thread proof:
    - `31354944eee50c711b70eecc308bb70028cd04247471e2646de8b0ad45fa5002`

- Important conclusion:
  - by the settled `04910+` failure band, the stable bad ocean no longer depends on late `ttmPlay()` execution either
  - so the image is surviving after both:
    - late background-thread work
    - late scene-thread opcode playback

- Cleanup:
  - reverted the temporary late `ttmPlay()` suppression proof patch

## 19:43 PDT - Zeroing all background tiles immediately before `LoadImage()` still does not move the settled ocean

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grDrawBackground()`:
    - zeroed all four `bgTile*->pixels`
    - forced their dirty ranges full-height
  - only for exact `ACTIVITY.ADS 1` after `grCurrentFrame >= 1000`
  - this is the last shared upload boundary before the frame reaches VRAM

- Validation run:
  - direct frame capture under:
    - [upload-blackproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-upload-blackproof)

- Result:
  - `frame_04910` still matches the exact original settled ocean hash:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - `frame_05000` also stays identical
  - early lead-in still differs:
    - `frame_04770 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - full sampled-run concatenated PNG hash remains:
    - `31354944eee50c711b70eecc308bb70028cd04247471e2646de8b0ad45fa5002`

- Important conclusion:
  - the settled ACTIVITY ocean is surviving even past the normal tile-upload boundary
  - it is not explained by:
    - late background-thread redraw
    - late `ttmPlay()` scene-thread execution
    - or even the current contents of the four background tile RAM surfaces at `grDrawBackground()` upload time

- Cleanup:
  - reverted the temporary upload-blackproof patch after validation

- Next target:
  - move the proof surface to framebuffer/display ownership itself:
    - determine whether the visible settled ocean is coming from a different renderer surface/path than the tile upload path

## 19:47 PDT - Direct late framebuffer black upload does change the settled ACTIVITY image

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), immediately after `grDrawBackground()` inside `grUpdateDisplay()`:
    - uploaded a static all-zero `320x240` tile into all four framebuffer quadrants
  - only for exact `ACTIVITY.ADS 1` after `grCurrentFrame >= 1000`

- Validation run:
  - direct frame capture under:
    - [framebuffer-blackproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-framebuffer-blackproof)

- Result:
  - the settled late ocean disappears
  - `frame_04910`, `frame_04770`, and `frame_05000` all become:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - full sampled-run concatenated PNG hash changes to:
    - `8d305707d22f695b403e105f5ca11807281cfb1799cb6074b4a0e96889767e56`

- Important conclusion:
  - the visible settled ACTIVITY image is absolutely on the normal displayed framebuffer
  - so the earlier negative `grDrawBackground()` tile-scrub proof does **not** mean “different renderer/output surface”
  - instead, it means the settled ocean is surviving into the visible framebuffer in a way that was not affected by the earlier tile-RAM/upload scrubs
  - the likely next target is now the exact interaction between:
    - dirty-row tracking / upload eligibility
    - framebuffer persistence across frames
    - any direct framebuffer writes outside the tile upload proof

- Cleanup:
  - reverted the temporary framebuffer-blackproof patch after validation

- Next target:
  - instrument why scrubbing the tile-upload source did not move the framebuffer, while a direct post-upload framebuffer write did

## 19:51 PDT - Replacing the `grDrawBackground()` upload source with black does change the settled ACTIVITY image

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grDrawBackground()`:
    - replaced the normal `LoadImage(..., tiles[i]->pixels + minY * w)` source with a static all-zero upload buffer
  - only for exact `ACTIVITY.ADS 1` after `grCurrentFrame >= 1000`

- Validation run:
  - direct frame capture under:
    - [upload-source-blackproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-upload-source-blackproof)

- Result:
  - the settled late ocean disappears
  - `frame_04910`, `frame_04770`, and `frame_05000` all become:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - full sampled-run concatenated PNG hash changes to:
    - `8d305707d22f695b403e105f5ca11807281cfb1799cb6074b4a0e96889767e56`

- Important conclusion:
  - the settled ACTIVITY image is still sourced through the normal `grDrawBackground()` upload path
  - so the earlier negative “tile memset + mark dirty” proof was not evidence of an alternate renderer/output surface
  - it only means that forcing the tile RAM contents to zero in that earlier proof did not actually alter the uploaded source in the way expected

- Cleanup:
  - reverted the temporary upload-source-blackproof patch after validation

- Next target:
  - debug why the earlier tile-RAM scrub failed while replacing the actual `LoadImage` source succeeded
  - most likely surfaces now:
    - dirty-row/source-pointer semantics in `grDrawBackground()`
    - tile RAM contents vs. expected upload window

## 19:56 PDT - Overwriting the exact heap upload slice still does not move the settled ocean

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grDrawBackground()`:
    - for exact `ACTIVITY.ADS 1`, overwrote the exact heap slice
      - `tiles[i]->pixels + minY * w`
    - with zeroes immediately before the normal `LoadImage(...)` call
  - this kept:
    - the same rect
    - the same source pointer
    - the same upload path

- Validation run:
  - direct frame capture under:
    - [upload-slice-blackproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-upload-slice-blackproof)

- Result:
  - `frame_04910` still matches the original settled ocean:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - `frame_05000` also stays identical
  - earlier lead-in still differs:
    - `frame_04770 = 0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - full sampled-run concatenated PNG hash remains the old one:
    - `31354944eee50c711b70eecc308bb70028cd04247471e2646de8b0ad45fa5002`

- Important conclusion:
  - replacing the `LoadImage` source pointer works
  - but mutating the exact heap source slice in place does not
  - so the remaining renderer bug is now strongly pointing at source-memory behavior, not upload-path selection

- Cleanup:
  - reverted the temporary upload-slice-blackproof patch after validation

## 20:00 PDT - Fresh-address upload only perturbs the lead-in, not the settled `04910` ocean

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grDrawBackground()`:
    - for exact `ACTIVITY.ADS 1` only when `grCurrentFrame` was in the tight `4909..4910` band
    - replaced the normal source with a freshly allocated zeroed upload buffer

- Validation run:
  - direct frame capture under:
    - [upload-freshaddr-4910](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-upload-freshaddr-4910)

- Result:
  - `frame_04910` still matches the original settled ocean:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - `frame_05000` also stays identical
  - but the immediate pre-failure frame does change:
    - `frame_04900 = d9ee0d2e8f8465ea471c9c79223f1177c7225a238274f7f68ab32ed94312c0a0`

- Important conclusion:
  - this is not clean enough to claim the “fresh unique source address fixes late uploads” theory
  - but it does show the renderer is sensitive to source-buffer identity/timing right at the failure boundary

- Cleanup:
  - reverted the temporary upload-freshaddr-4910 patch after validation

- Next target:
  - keep the bug class as renderer-side `LoadImage` source-memory behavior
  - but move the proof surface to a slightly wider `04900–04910` upload window, since the exact-frame gate is too narrow to be definitive

## 20:01 PDT - Widened fresh-address upload window is cleanly negative

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grDrawBackground()`:
    - for exact `ACTIVITY.ADS 1`, replaced the normal upload source with a freshly allocated zeroed buffer
    - across the full `grCurrentFrame` window `4900..4910`

- Validation run:
  - direct frame capture under:
    - [upload-freshaddr-4900-4910](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-upload-freshaddr-4900-4910)

- Result:
  - `frame_04900` is unchanged:
    - `d9ee0d2e8f8465ea471c9c79223f1177c7225a238274f7f68ab32ed94312c0a0`
  - `frame_04910` is unchanged:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - `frame_05000` is unchanged:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - full sampled-run concatenated PNG hash also stays unchanged:
    - `31354944eee50c711b70eecc308bb70028cd04247471e2646de8b0ad45fa5002`

- Important conclusion:
  - the simple “fresh unique source address in the failure band fixes the late renderer state” theory is now ruled out
  - current renderer-side read is narrower:
    - replacing the upload source globally with a static black buffer works
    - mutating the in-place heap slice does not
    - swapping to fresh zeroed buffers only in the handoff band also does not

- Cleanup:
  - reverted the widened fresh-address proof patch after validation

- Next target:
  - stop treating this as a generic source-address problem
  - move to exact upload-window geometry / dirty-range behavior at `04910`

## 20:05 PDT - Forcing full-height uploads across `04900–04910` makes the settled ocean disappear

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grDrawBackground()`:
    - for exact `ACTIVITY.ADS 1` and only during `grCurrentFrame` `4900..4910`
    - forced every background tile upload window to full height:
      - `minY = 0`
      - `maxY = tile->height - 1`
    - and replaced the upload source with a static all-zero black buffer

- Validation run:
  - direct frame capture under:
    - [force-dirty-black-4900-4910](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-force-dirty-black-4900-4910)

- Result:
  - `frame_04900` changes to black:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - `frame_04910` changes to black:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - `frame_05000` also stays black:
    - `0092914af727e5d656269735cf9d7cfffa0cb120f2966b57c23fa9e0fe1095e9`
  - full sampled-run concatenated PNG hash:
    - `8d305707d22f695b403e105f5ca11807281cfb1799cb6074b4a0e96889767e56`

- Important conclusion:
  - this is the sharpest renderer cut so far
  - the settled ACTIVITY ocean is still entirely controlled by `grDrawBackground()` uploads
  - and the key remaining bug class is now dirty-range/upload eligibility at the handoff window
  - i.e. the normal upload window at `04910` is not covering or replacing the visible framebuffer the way it should

- Cleanup:
  - reverted the temporary force-dirty-black proof patch after validation

- Next target:
  - inspect the exact `minY/maxY` dirty-row state leading into `04910`
  - determine why the normal dirty window is insufficient while forced full-height upload fixes the frame

## 20:10 PDT - Forcing full-height uploads with the normal source does nothing

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grDrawBackground()`:
    - for exact `ACTIVITY.ADS 1` during `grCurrentFrame` `4900..4910`
    - forced:
      - `minY = 0`
      - `maxY = tile->height - 1`
    - but kept the normal upload source:
      - `tiles[i]->pixels + minY * w`

- Validation run:
  - direct frame capture under:
    - [force-dirty-normal-4900-4910](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-force-dirty-normal-4900-4910)

- Result:
  - `frame_04900` is unchanged:
    - `d9ee0d2e8f8465ea471c9c79223f1177c7225a238274f7f68ab32ed94312c0a0`
  - `frame_04910` is unchanged:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - `frame_05000` is unchanged:
    - `59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`
  - full sampled-run concatenated PNG hash also remains unchanged:
    - `31354944eee50c711b70eecc308bb70028cd04247471e2646de8b0ad45fa5002`

- Important conclusion:
  - this cleanly rules out “dirty upload window is too small” as a sufficient explanation by itself
  - widening the upload window alone does not move the settled ocean
  - the current renderer-side read is now:
    - replacing the upload source with black works
    - widening the upload window alone does not
    - so the decisive remaining issue is the content of the normal upload source at the handoff, not just row eligibility

- Cleanup:
  - reverted the temporary force-dirty-normal proof patch after validation

- Next target:
  - inspect what is actually in the normal tile upload source at `04910`
  - and why it remains ocean while black-source substitution succeeds

## 18:55 PDT - Returning from `islandInit()` immediately after `grLoadScreen(...)` reproduces the exact original ocean hash

- Temporary proof:
  - in [island.c](/home/hunter/workspace/jc_reborn/island.c), for exact `ACTIVITY.ADS 1` boot path:
    - kept the normal initial:
      - `grLoadScreen("NIGHT.SCR")` or `grLoadScreen("OCEAN0?.SCR")`
    - then returned immediately from `islandInit()`
  - this bypassed all later island composition:
    - raft
    - `BACKGRND.BMP`
    - clouds
    - island sprites
    - initial wave draws

- Validation run:
  - [island-loadonly](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-loadonly/result.json)

- Result:
  - run exits cleanly with the exact original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - later island composition is not required at all for the persistent ACTIVITY failure
  - the stable bad ocean is already fully established by the initial `grLoadScreen(...)` path inside `islandInit()`
  - this collapses the remaining causal surface tightly onto:
    - `adsInitIsland()`
    - `islandInit()`
    - specifically the initial `OCEAN0?.SCR` / `NIGHT.SCR` background load and the state path that leads there

- Cleanup:
  - reverted the temporary early return after `grLoadScreen(...)`

## 19:00 PDT - Scrubbing to black immediately after exact `adsPlay(ACTIVITY.ADS, 1)` no-launch return does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), immediately after:
    - `adsPlay(scene->adsName, scene->adsTagNo)`
  - for exact `ACTIVITY.ADS 1`, if:
    - `!ps1AdsLastPlayLaunched`
  - then called:
    - `adsNoIsland()`

- Validation run:
  - [post-adsplay-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-post-adsplay-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the stable ACTIVITY ocean path is not being explained by the exact post-`adsPlay(ACTIVITY.ADS, 1)` no-launch return hook
  - either:
    - that exact playback hook is not reached on the stable failure path
    - or the late ocean path is being re-established elsewhere after it
  - combined with the exact prepare proofs, this keeps the strongest remaining live suspect centered on the prepare/intro-to-scene handoff rather than the exact post-playback failure branch

- Cleanup:
  - reverted the temporary post-`adsPlay()` `adsNoIsland()` proof patch

## 19:05 PDT - Scrubbing to black immediately before `storyPlayPreparedScene(finalScene, ...)` also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for:
    - `bootScene != NULL`
    - exact `finalScene == ACTIVITY.ADS 1`
  - called:
    - `adsNoIsland()`
  - immediately before:
    - `storyPlayPreparedScene(finalScene, prevSpot, prevHdg)`

- Validation run:
  - [pre-playprepared-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-pre-playprepared-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ACTIVITY ocean path is bypassing or outliving even the top-level exact pre-`storyPlayPreparedScene(...)` handoff hook in `storyPlay()`
  - that makes the current failure shape more explicit:
    - exact ACTIVITY prepare has strong causal linkage to the ocean
    - but the stable bad path is not behaving like the straightforward top-level exact-playback branch we expected

- Cleanup:
  - reverted the temporary pre-`storyPlayPreparedScene()` `adsNoIsland()` proof patch

## 19:10 PDT - Forcing the obvious `bootScene == NULL` fallback prepare path to black also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), before `storyPrepareSceneState(finalScene)`:
    - if `bootScene == NULL`
    - and exact `ACTIVITY.ADS 1` override was still pending
    - called `adsNoIsland()` instead of `storyPrepareSceneState(finalScene)`

- Validation run:
  - [fallback-bootscene-null](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-fallback-bootscene-null/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ocean is not being explained by the obvious `bootScene == NULL` fallback prepare branch either
  - so the remaining bad path is more specific than:
    - exact top-level playprepared branch
    - exact post-adsPlay no-launch branch
    - obvious `bootScene == NULL` fallback prepare

- Cleanup:
  - reverted the temporary fallback proof patch

## 19:16 PDT - Even `adsReleaseIsland(); adsNoIsland();` before exact playback leaves the original ocean hash untouched

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for:
    - `bootScene != NULL`
    - exact `finalScene == ACTIVITY.ADS 1`
  - immediately before `storyPlayPreparedScene(...)`, called:
    - `adsReleaseIsland()`
    - `adsNoIsland()`

- Validation run:
  - [release-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-release-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - this is not just island/background state persisting because `adsNoIsland()` is too weak after prepare
  - even explicit island release plus black-background init at that top-level handoff point does not move the result
  - that strengthens the read that the stable bad-ocean path is bypassing or outliving the obvious top-level exact handoff hooks entirely

- Cleanup:
  - reverted the temporary `adsReleaseIsland(); adsNoIsland();` proof patch

## 19:21 PDT - Forcing late `grLoadScreen(\"OCEAN0?.SCR\")` loads to black also does nothing

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grLoadScreen(char *strArg)`:
    - if exact `ACTIVITY.ADS 1` override is pending
    - and `grGetCurrentFrame() >= 1000`
    - and `strArg` matches `OCEAN0?.SCR`
    - then call `grInitEmptyBackground()` and return

- Validation run:
  - [late-ocean-screen-blackproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-late-ocean-screen-blackproof/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled `04910+` ocean is not being actively re-established by a late `grLoadScreen(\"OCEAN0?.SCR\")` call on the failure path
  - that strengthens the read that the bad ocean is coming from an earlier background load that survives into the late window, not a later screen reload

- Cleanup:
  - reverted the temporary late-`grLoadScreen()` blackproof patch

## 19:26 PDT - Zeroing restored background tiles every frame still leaves the exact original ocean hash

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grRestoreBgTiles()`:
    - after restoring clean copies
    - if exact `ACTIVITY.ADS 1` override is pending
    - and `grCurrentFrame >= 1000`
    - memset the active restored background tiles to black

- Validation run:
  - [restore-blackproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-restore-blackproof/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ocean is being repopulated after `grRestoreBgTiles()`, before upload
  - so it is not merely a stale clean-background restore artifact surviving untouched through the late window
  - this moves the remaining live suspect surface to late per-frame compositing/drawing, not background restore

- Cleanup:
  - reverted the temporary restore-blackproof patch

## 19:30 PDT - Disabling saved-rect replay still leaves the exact original ocean hash

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grApplySavedRects()`:
    - if exact `ACTIVITY.ADS 1` override is pending
    - and `grCurrentFrame >= 1000`
    - return immediately without replaying any saved rects

- Validation run:
  - [savedrect-blackproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-savedrect-blackproof/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - saved-rect replay is not the repopulation path either
  - the settled ocean is still being drawn later in the frame by live compositing/drawing after:
    - `grRestoreBgTiles()`
    - and after `grApplySavedRects()`

- Cleanup:
  - reverted the temporary saved-rect blackproof patch

- Next target:
  - move directly into late live compositing between restore and upload:
    - `ttmPlay()` / background-thread draws / holiday-thread draws / sprite compositing

## 18:55 PDT - Returning from `islandInit()` immediately after `grLoadScreen(...)` reproduces the exact original ocean hash

- Temporary proof:
  - in [island.c](/home/hunter/workspace/jc_reborn/island.c), for exact `ACTIVITY.ADS 1` boot path:
    - kept the normal initial:
      - `grLoadScreen("NIGHT.SCR")` or `grLoadScreen("OCEAN0?.SCR")`
    - then returned immediately from `islandInit()`
  - this bypassed all later island composition:
    - raft
    - `BACKGRND.BMP`
    - clouds
    - island sprites
    - initial wave draws

- Validation run:
  - [island-loadonly](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-loadonly/result.json)

- Result:
  - run exits cleanly with the exact original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - later island composition is not required at all for the persistent ACTIVITY failure
  - the stable bad ocean is already fully established by the initial `grLoadScreen(...)` path inside `islandInit()`
  - this collapses the remaining causal surface tightly onto:
    - `adsInitIsland()`
    - `islandInit()`
    - specifically the initial `OCEAN0?.SCR` / `NIGHT.SCR` background load and the state path that leads there

- Cleanup:
  - reverted the temporary early return after `grLoadScreen(...)`

## 19:00 PDT - Scrubbing to black immediately after exact `adsPlay(ACTIVITY.ADS, 1)` no-launch return does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), immediately after:
    - `adsPlay(scene->adsName, scene->adsTagNo)`
  - for exact `ACTIVITY.ADS 1`, if:
    - `!ps1AdsLastPlayLaunched`
  - then called:
    - `adsNoIsland()`

- Validation run:
  - [post-adsplay-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-post-adsplay-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the stable ACTIVITY ocean path is not being explained by the exact post-`adsPlay(ACTIVITY.ADS, 1)` no-launch return hook
  - either:
    - that exact playback hook is not reached on the stable failure path
    - or the late ocean path is being re-established elsewhere after it
  - combined with the exact prepare proofs, this keeps the strongest remaining live suspect centered on the prepare/intro-to-scene handoff rather than the exact post-playback failure branch

- Cleanup:
  - reverted the temporary post-`adsPlay()` `adsNoIsland()` proof patch

## 19:05 PDT - Scrubbing to black immediately before `storyPlayPreparedScene(finalScene, ...)` also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for:
    - `bootScene != NULL`
    - exact `finalScene == ACTIVITY.ADS 1`
  - called:
    - `adsNoIsland()`
  - immediately before:
    - `storyPlayPreparedScene(finalScene, prevSpot, prevHdg)`

- Validation run:
  - [pre-playprepared-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-pre-playprepared-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ACTIVITY ocean path is bypassing or outliving even the top-level exact pre-`storyPlayPreparedScene(...)` handoff hook in `storyPlay()`
  - that makes the current failure shape more explicit:
    - exact ACTIVITY prepare has strong causal linkage to the ocean
    - but the stable bad path is not behaving like the straightforward top-level exact-playback branch we expected

- Cleanup:
  - reverted the temporary pre-`storyPlayPreparedScene()` `adsNoIsland()` proof patch

## 19:10 PDT - Forcing the obvious `bootScene == NULL` fallback prepare path to black also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), before `storyPrepareSceneState(finalScene)`:
    - if `bootScene == NULL`
    - and exact `ACTIVITY.ADS 1` override was still pending
    - called `adsNoIsland()` instead of `storyPrepareSceneState(finalScene)`

- Validation run:
  - [fallback-bootscene-null](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-fallback-bootscene-null/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ocean is not being explained by the obvious `bootScene == NULL` fallback prepare branch either
  - so the remaining bad path is more specific than:
    - exact top-level playprepared branch
    - exact post-adsPlay no-launch branch
    - obvious `bootScene == NULL` fallback prepare

- Cleanup:
  - reverted the temporary fallback proof patch

## 19:16 PDT - Even `adsReleaseIsland(); adsNoIsland();` before exact playback leaves the original ocean hash untouched

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for:
    - `bootScene != NULL`
    - exact `finalScene == ACTIVITY.ADS 1`
  - immediately before `storyPlayPreparedScene(...)`, called:
    - `adsReleaseIsland()`
    - `adsNoIsland()`

- Validation run:
  - [release-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-release-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - this is not just island/background state persisting because `adsNoIsland()` is too weak after prepare
  - even explicit island release plus black-background init at that top-level handoff point does not move the result
  - that strengthens the read that the stable bad-ocean path is bypassing or outliving the obvious top-level exact handoff hooks entirely

- Cleanup:
  - reverted the temporary `adsReleaseIsland(); adsNoIsland();` proof patch

## 19:21 PDT - Forcing late `grLoadScreen(\"OCEAN0?.SCR\")` loads to black also does nothing

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grLoadScreen(char *strArg)`:
    - if exact `ACTIVITY.ADS 1` override is pending
    - and `grGetCurrentFrame() >= 1000`
    - and `strArg` matches `OCEAN0?.SCR`
    - then call `grInitEmptyBackground()` and return

- Validation run:
  - [late-ocean-screen-blackproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-late-ocean-screen-blackproof/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled `04910+` ocean is not being actively re-established by a late `grLoadScreen(\"OCEAN0?.SCR\")` call on the failure path
  - that strengthens the read that the bad ocean is coming from an earlier background load that survives into the late window, not a later screen reload

- Cleanup:
  - reverted the temporary late-`grLoadScreen()` blackproof patch

## 19:26 PDT - Zeroing restored background tiles every frame still leaves the exact original ocean hash

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grRestoreBgTiles()`:
    - after restoring clean copies
    - if exact `ACTIVITY.ADS 1` override is pending
    - and `grCurrentFrame >= 1000`
    - memset the active restored background tiles to black

- Validation run:
  - [restore-blackproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-restore-blackproof/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ocean is being repopulated after `grRestoreBgTiles()`, before upload
  - so it is not merely a stale clean-background restore artifact surviving untouched through the late window
  - this moves the remaining live suspect surface to late per-frame compositing/drawing, not background restore

- Cleanup:
  - reverted the temporary restore-blackproof patch

- Next target:
  - instrument or suppress late per-frame compositing paths between `grRestoreBgTiles()` and `grDrawBackground()` to identify what is repopulating the ocean after restore

## 18:55 PDT - Returning from `islandInit()` immediately after `grLoadScreen(...)` reproduces the exact original ocean hash

- Temporary proof:
  - in [island.c](/home/hunter/workspace/jc_reborn/island.c), for exact `ACTIVITY.ADS 1` boot path:
    - kept the normal initial:
      - `grLoadScreen("NIGHT.SCR")` or `grLoadScreen("OCEAN0?.SCR")`
    - then returned immediately from `islandInit()`
  - this bypassed all later island composition:
    - raft
    - `BACKGRND.BMP`
    - clouds
    - island sprites
    - initial wave draws

- Validation run:
  - [island-loadonly](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-loadonly/result.json)

- Result:
  - run exits cleanly with the exact original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - later island composition is not required at all for the persistent ACTIVITY failure
  - the stable bad ocean is already fully established by the initial `grLoadScreen(...)` path inside `islandInit()`
  - this collapses the remaining causal surface tightly onto:
    - `adsInitIsland()`
    - `islandInit()`
    - specifically the initial `OCEAN0?.SCR` / `NIGHT.SCR` background load and the state path that leads there

- Cleanup:
  - reverted the temporary early return after `grLoadScreen(...)`

## 19:00 PDT - Scrubbing to black immediately after exact `adsPlay(ACTIVITY.ADS, 1)` no-launch return does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), immediately after:
    - `adsPlay(scene->adsName, scene->adsTagNo)`
  - for exact `ACTIVITY.ADS 1`, if:
    - `!ps1AdsLastPlayLaunched`
  - then called:
    - `adsNoIsland()`

- Validation run:
  - [post-adsplay-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-post-adsplay-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the stable ACTIVITY ocean path is not being explained by the exact post-`adsPlay(ACTIVITY.ADS, 1)` no-launch return hook
  - either:
    - that exact playback hook is not reached on the stable failure path
    - or the late ocean path is being re-established elsewhere after it
  - combined with the exact prepare proofs, this keeps the strongest remaining live suspect centered on the prepare/intro-to-scene handoff rather than the exact post-playback failure branch

- Cleanup:
  - reverted the temporary post-`adsPlay()` `adsNoIsland()` proof patch

## 19:05 PDT - Scrubbing to black immediately before `storyPlayPreparedScene(finalScene, ...)` also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for:
    - `bootScene != NULL`
    - exact `finalScene == ACTIVITY.ADS 1`
  - called:
    - `adsNoIsland()`
  - immediately before:
    - `storyPlayPreparedScene(finalScene, prevSpot, prevHdg)`

- Validation run:
  - [pre-playprepared-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-pre-playprepared-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ACTIVITY ocean path is bypassing or outliving even the top-level exact pre-`storyPlayPreparedScene(...)` handoff hook in `storyPlay()`
  - that makes the current failure shape more explicit:
    - exact ACTIVITY prepare has strong causal linkage to the ocean
    - but the stable bad path is not behaving like the straightforward top-level exact-playback branch we expected

- Cleanup:
  - reverted the temporary pre-`storyPlayPreparedScene()` `adsNoIsland()` proof patch

## 19:10 PDT - Forcing the obvious `bootScene == NULL` fallback prepare path to black also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), before `storyPrepareSceneState(finalScene)`:
    - if `bootScene == NULL`
    - and exact `ACTIVITY.ADS 1` override was still pending
    - called `adsNoIsland()` instead of `storyPrepareSceneState(finalScene)`

- Validation run:
  - [fallback-bootscene-null](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-fallback-bootscene-null/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ocean is not being explained by the obvious `bootScene == NULL` fallback prepare branch either
  - so the remaining bad path is more specific than:
    - exact top-level playprepared branch
    - exact post-adsPlay no-launch branch
    - obvious `bootScene == NULL` fallback prepare

- Cleanup:
  - reverted the temporary fallback proof patch

## 19:16 PDT - Even `adsReleaseIsland(); adsNoIsland();` before exact playback leaves the original ocean hash untouched

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for:
    - `bootScene != NULL`
    - exact `finalScene == ACTIVITY.ADS 1`
  - immediately before `storyPlayPreparedScene(...)`, called:
    - `adsReleaseIsland()`
    - `adsNoIsland()`

- Validation run:
  - [release-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-release-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - this is not just island/background state persisting because `adsNoIsland()` is too weak after prepare
  - even explicit island release plus black-background init at that top-level handoff point does not move the result
  - that strengthens the read that the stable bad-ocean path is bypassing or outliving the obvious top-level exact handoff hooks entirely

- Cleanup:
  - reverted the temporary `adsReleaseIsland(); adsNoIsland();` proof patch

## 19:21 PDT - Forcing late `grLoadScreen(\"OCEAN0?.SCR\")` loads to black also does nothing

- Temporary proof:
  - in [graphics_ps1.c](/home/hunter/workspace/jc_reborn/graphics_ps1.c), inside `grLoadScreen(char *strArg)`:
    - if exact `ACTIVITY.ADS 1` override is pending
    - and `grGetCurrentFrame() >= 1000`
    - and `strArg` matches `OCEAN0?.SCR`
    - then call `grInitEmptyBackground()` and return

- Validation run:
  - [late-ocean-screen-blackproof](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-late-ocean-screen-blackproof/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled `04910+` ocean is not being actively re-established by a late `grLoadScreen(\"OCEAN0?.SCR\")` call on the failure path
  - that strengthens the read that the bad ocean is coming from an earlier background load that survives into the late window, not a later screen reload

- Cleanup:
  - reverted the temporary late-`grLoadScreen()` blackproof patch

- Next target:
  - move the proof surface to background persistence and framebuffer ownership itself, because both:
    - obvious story-level branches
    - late renderer reload hypothesis
  - are now ruled out directly

## 18:55 PDT - Returning from `islandInit()` immediately after `grLoadScreen(...)` reproduces the exact original ocean hash

- Temporary proof:
  - in [island.c](/home/hunter/workspace/jc_reborn/island.c), for exact `ACTIVITY.ADS 1` boot path:
    - kept the normal initial:
      - `grLoadScreen("NIGHT.SCR")` or `grLoadScreen("OCEAN0?.SCR")`
    - then returned immediately from `islandInit()`
  - this bypassed all later island composition:
    - raft
    - `BACKGRND.BMP`
    - clouds
    - island sprites
    - initial wave draws

- Validation run:
  - [island-loadonly](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-loadonly/result.json)

- Result:
  - run exits cleanly with the exact original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - later island composition is not required at all for the persistent ACTIVITY failure
  - the stable bad ocean is already fully established by the initial `grLoadScreen(...)` path inside `islandInit()`
  - this collapses the remaining causal surface tightly onto:
    - `adsInitIsland()`
    - `islandInit()`
    - specifically the initial `OCEAN0?.SCR` / `NIGHT.SCR` background load and the state path that leads there

- Cleanup:
  - reverted the temporary early return after `grLoadScreen(...)`

## 19:00 PDT - Scrubbing to black immediately after exact `adsPlay(ACTIVITY.ADS, 1)` no-launch return does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), immediately after:
    - `adsPlay(scene->adsName, scene->adsTagNo)`
  - for exact `ACTIVITY.ADS 1`, if:
    - `!ps1AdsLastPlayLaunched`
  - then called:
    - `adsNoIsland()`

- Validation run:
  - [post-adsplay-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-post-adsplay-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the stable ACTIVITY ocean path is not being explained by the exact post-`adsPlay(ACTIVITY.ADS, 1)` no-launch return hook
  - either:
    - that exact playback hook is not reached on the stable failure path
    - or the late ocean path is being re-established elsewhere after it
  - combined with the exact prepare proofs, this keeps the strongest remaining live suspect centered on the prepare/intro-to-scene handoff rather than the exact post-playback failure branch

- Cleanup:
  - reverted the temporary post-`adsPlay()` `adsNoIsland()` proof patch

## 19:05 PDT - Scrubbing to black immediately before `storyPlayPreparedScene(finalScene, ...)` also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for:
    - `bootScene != NULL`
    - exact `finalScene == ACTIVITY.ADS 1`
  - called:
    - `adsNoIsland()`
  - immediately before:
    - `storyPlayPreparedScene(finalScene, prevSpot, prevHdg)`

- Validation run:
  - [pre-playprepared-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-pre-playprepared-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ACTIVITY ocean path is bypassing or outliving even the top-level exact pre-`storyPlayPreparedScene(...)` handoff hook in `storyPlay()`
  - that makes the current failure shape more explicit:
    - exact ACTIVITY prepare has strong causal linkage to the ocean
    - but the stable bad path is not behaving like the straightforward top-level exact-playback branch we expected

- Cleanup:
  - reverted the temporary pre-`storyPlayPreparedScene()` `adsNoIsland()` proof patch

## 19:10 PDT - Forcing the obvious `bootScene == NULL` fallback prepare path to black also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), before `storyPrepareSceneState(finalScene)`:
    - if `bootScene == NULL`
    - and exact `ACTIVITY.ADS 1` override was still pending
    - called `adsNoIsland()` instead of `storyPrepareSceneState(finalScene)`

- Validation run:
  - [fallback-bootscene-null](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-fallback-bootscene-null/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ocean is not being explained by the obvious `bootScene == NULL` fallback prepare branch either
  - so the remaining bad path is more specific than:
    - exact top-level playprepared branch
    - exact post-adsPlay no-launch branch
    - obvious `bootScene == NULL` fallback prepare

- Cleanup:
  - reverted the temporary fallback proof patch

## 19:16 PDT - Even `adsReleaseIsland(); adsNoIsland();` before exact playback leaves the original ocean hash untouched

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for:
    - `bootScene != NULL`
    - exact `finalScene == ACTIVITY.ADS 1`
  - immediately before `storyPlayPreparedScene(...)`, called:
    - `adsReleaseIsland()`
    - `adsNoIsland()`

- Validation run:
  - [release-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-release-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - this is not just island/background state persisting because `adsNoIsland()` is too weak after prepare
  - even explicit island release plus black-background init at that top-level handoff point does not move the result
  - that strengthens the read that the stable bad-ocean path is bypassing or outliving the obvious top-level exact handoff hooks entirely

- Cleanup:
  - reverted the temporary `adsReleaseIsland(); adsNoIsland();` proof patch

- Next target:
  - switch from behavioral scrubs to a more direct runtime identity proof for the code path active at the `04910` settled-ocean window, because the obvious top-level hooks and fallback branches are now directly ruled out

## 18:55 PDT - Returning from `islandInit()` immediately after `grLoadScreen(...)` reproduces the exact original ocean hash

- Temporary proof:
  - in [island.c](/home/hunter/workspace/jc_reborn/island.c), for exact `ACTIVITY.ADS 1` boot path:
    - kept the normal initial:
      - `grLoadScreen("NIGHT.SCR")` or `grLoadScreen("OCEAN0?.SCR")`
    - then returned immediately from `islandInit()`
  - this bypassed all later island composition:
    - raft
    - `BACKGRND.BMP`
    - clouds
    - island sprites
    - initial wave draws

- Validation run:
  - [island-loadonly](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-loadonly/result.json)

- Result:
  - run exits cleanly with the exact original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - later island composition is not required at all for the persistent ACTIVITY failure
  - the stable bad ocean is already fully established by the initial `grLoadScreen(...)` path inside `islandInit()`
  - this collapses the remaining causal surface tightly onto:
    - `adsInitIsland()`
    - `islandInit()`
    - specifically the initial `OCEAN0?.SCR` / `NIGHT.SCR` background load and the state path that leads there

- Cleanup:
  - reverted the temporary early return after `grLoadScreen(...)`

## 19:00 PDT - Scrubbing to black immediately after exact `adsPlay(ACTIVITY.ADS, 1)` no-launch return does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), immediately after:
    - `adsPlay(scene->adsName, scene->adsTagNo)`
  - for exact `ACTIVITY.ADS 1`, if:
    - `!ps1AdsLastPlayLaunched`
  - then called:
    - `adsNoIsland()`

- Validation run:
  - [post-adsplay-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-post-adsplay-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the stable ACTIVITY ocean path is not being explained by the exact post-`adsPlay(ACTIVITY.ADS, 1)` no-launch return hook
  - either:
    - that exact playback hook is not reached on the stable failure path
    - or the late ocean path is being re-established elsewhere after it
  - combined with the exact prepare proofs, this keeps the strongest remaining live suspect centered on the prepare/intro-to-scene handoff rather than the exact post-playback failure branch

- Cleanup:
  - reverted the temporary post-`adsPlay()` `adsNoIsland()` proof patch

## 19:05 PDT - Scrubbing to black immediately before `storyPlayPreparedScene(finalScene, ...)` also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for:
    - `bootScene != NULL`
    - exact `finalScene == ACTIVITY.ADS 1`
  - called:
    - `adsNoIsland()`
  - immediately before:
    - `storyPlayPreparedScene(finalScene, prevSpot, prevHdg)`

- Validation run:
  - [pre-playprepared-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-pre-playprepared-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ACTIVITY ocean path is bypassing or outliving even the top-level exact pre-`storyPlayPreparedScene(...)` handoff hook in `storyPlay()`
  - that makes the current failure shape more explicit:
    - exact ACTIVITY prepare has strong causal linkage to the ocean
    - but the stable bad path is not behaving like the straightforward top-level exact-playback branch we expected

- Cleanup:
  - reverted the temporary pre-`storyPlayPreparedScene()` `adsNoIsland()` proof patch

## 19:10 PDT - Forcing the obvious `bootScene == NULL` fallback prepare path to black also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), before `storyPrepareSceneState(finalScene)`:
    - if `bootScene == NULL`
    - and exact `ACTIVITY.ADS 1` override was still pending
    - called `adsNoIsland()` instead of `storyPrepareSceneState(finalScene)`

- Validation run:
  - [fallback-bootscene-null](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-fallback-bootscene-null/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ocean is not being explained by the obvious `bootScene == NULL` fallback prepare branch either
  - so the remaining bad path is more specific than:
    - exact top-level playprepared branch
    - exact post-adsPlay no-launch branch
    - obvious `bootScene == NULL` fallback prepare

- Cleanup:
  - reverted the temporary fallback proof patch

- Next target:
  - move to a more direct runtime identity proof surface for the actual branch executing at the `04910` settled-ocean window, because the obvious story-level branches are now mostly ruled out by direct behavioral proofs

## 18:55 PDT - Returning from `islandInit()` immediately after `grLoadScreen(...)` reproduces the exact original ocean hash

- Temporary proof:
  - in [island.c](/home/hunter/workspace/jc_reborn/island.c), for exact `ACTIVITY.ADS 1` boot path:
    - kept the normal initial:
      - `grLoadScreen("NIGHT.SCR")` or `grLoadScreen("OCEAN0?.SCR")`
    - then returned immediately from `islandInit()`
  - this bypassed all later island composition:
    - raft
    - `BACKGRND.BMP`
    - clouds
    - island sprites
    - initial wave draws

- Validation run:
  - [island-loadonly](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-loadonly/result.json)

- Result:
  - run exits cleanly with the exact original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - later island composition is not required at all for the persistent ACTIVITY failure
  - the stable bad ocean is already fully established by the initial `grLoadScreen(...)` path inside `islandInit()`
  - this collapses the remaining causal surface tightly onto:
    - `adsInitIsland()`
    - `islandInit()`
    - specifically the initial `OCEAN0?.SCR` / `NIGHT.SCR` background load and the state path that leads there

- Cleanup:
  - reverted the temporary early return after `grLoadScreen(...)`

## 19:00 PDT - Scrubbing to black immediately after exact `adsPlay(ACTIVITY.ADS, 1)` no-launch return does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), immediately after:
    - `adsPlay(scene->adsName, scene->adsTagNo)`
  - for exact `ACTIVITY.ADS 1`, if:
    - `!ps1AdsLastPlayLaunched`
  - then called:
    - `adsNoIsland()`

- Validation run:
  - [post-adsplay-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-post-adsplay-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the stable ACTIVITY ocean path is not being explained by the exact post-`adsPlay(ACTIVITY.ADS, 1)` no-launch return hook
  - either:
    - that exact playback hook is not reached on the stable failure path
    - or the late ocean path is being re-established elsewhere after it
  - combined with the exact prepare proofs, this keeps the strongest remaining live suspect centered on the prepare/intro-to-scene handoff rather than the exact post-playback failure branch

- Cleanup:
  - reverted the temporary post-`adsPlay()` `adsNoIsland()` proof patch

## 19:05 PDT - Scrubbing to black immediately before `storyPlayPreparedScene(finalScene, ...)` also does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), for:
    - `bootScene != NULL`
    - exact `finalScene == ACTIVITY.ADS 1`
  - called:
    - `adsNoIsland()`
  - immediately before:
    - `storyPlayPreparedScene(finalScene, prevSpot, prevHdg)`

- Validation run:
  - [pre-playprepared-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-pre-playprepared-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the settled ACTIVITY ocean path is bypassing or outliving even the top-level exact pre-`storyPlayPreparedScene(...)` handoff hook in `storyPlay()`
  - that makes the current failure shape more explicit:
    - exact ACTIVITY prepare has strong causal linkage to the ocean
    - but the stable bad path is not behaving like the straightforward top-level exact-playback branch we expected

- Cleanup:
  - reverted the temporary pre-`storyPlayPreparedScene()` `adsNoIsland()` proof patch

- Next target:
  - identify the alternate path or retry/re-entry path that preserves or re-establishes the prepared ocean while bypassing both:
    - exact pre-`storyPlayPreparedScene(...)`
    - exact post-`adsPlay(ACTIVITY.ADS, 1)` no-launch cleanup

## 18:55 PDT - Returning from `islandInit()` immediately after `grLoadScreen(...)` reproduces the exact original ocean hash

- Temporary proof:
  - in [island.c](/home/hunter/workspace/jc_reborn/island.c), for exact `ACTIVITY.ADS 1` boot path:
    - kept the normal initial:
      - `grLoadScreen("NIGHT.SCR")` or `grLoadScreen("OCEAN0?.SCR")`
    - then returned immediately from `islandInit()`
  - this bypassed all later island composition:
    - raft
    - `BACKGRND.BMP`
    - clouds
    - island sprites
    - initial wave draws

- Validation run:
  - [island-loadonly](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-loadonly/result.json)

- Result:
  - run exits cleanly with the exact original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - later island composition is not required at all for the persistent ACTIVITY failure
  - the stable bad ocean is already fully established by the initial `grLoadScreen(...)` path inside `islandInit()`
  - this collapses the remaining causal surface tightly onto:
    - `adsInitIsland()`
    - `islandInit()`
    - specifically the initial `OCEAN0?.SCR` / `NIGHT.SCR` background load and the state path that leads there

- Cleanup:
  - reverted the temporary early return after `grLoadScreen(...)`

## 19:00 PDT - Scrubbing to black immediately after exact `adsPlay(ACTIVITY.ADS, 1)` no-launch return does nothing

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), immediately after:
    - `adsPlay(scene->adsName, scene->adsTagNo)`
  - for exact `ACTIVITY.ADS 1`, if:
    - `!ps1AdsLastPlayLaunched`
  - then called:
    - `adsNoIsland()`

- Validation run:
  - [post-adsplay-noisland](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-post-adsplay-noisland/result.json)

- Result:
  - run exits cleanly with the unchanged original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - the stable ACTIVITY ocean path is not being explained by the exact post-`adsPlay(ACTIVITY.ADS, 1)` no-launch return hook
  - either:
    - that exact playback hook is not reached on the stable failure path
    - or the late ocean path is being re-established elsewhere after it
  - combined with the exact prepare proofs, this keeps the strongest remaining live suspect centered on the prepare/intro-to-scene handoff rather than the exact post-playback failure branch

- Cleanup:
  - reverted the temporary post-`adsPlay()` `adsNoIsland()` proof patch

- Next target:
  - identify the path that reaches the settled ocean after exact ACTIVITY prepare while bypassing the exact post-`adsPlay(ACTIVITY.ADS, 1)` no-launch cleanup hook

## 18:55 PDT - Returning from `islandInit()` immediately after `grLoadScreen(...)` reproduces the exact original ocean hash

- Temporary proof:
  - in [island.c](/home/hunter/workspace/jc_reborn/island.c), for exact `ACTIVITY.ADS 1` boot path:
    - kept the normal initial:
      - `grLoadScreen("NIGHT.SCR")` or `grLoadScreen("OCEAN0?.SCR")`
    - then returned immediately from `islandInit()`
  - this bypassed all later island composition:
    - raft
    - `BACKGRND.BMP`
    - clouds
    - island sprites
    - initial wave draws

- Validation run:
  - [island-loadonly](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-island-loadonly/result.json)

- Result:
  - run exits cleanly with the exact original stable bad-ocean hash:
    - `state_hash = 59cd749fc0952552db0f9748ec7421df288292773be8ee82f008bfb3f2fbf6fc`

- Important conclusion:
  - later island composition is not required at all for the persistent ACTIVITY failure
  - the stable bad ocean is already fully established by the initial `grLoadScreen(...)` path inside `islandInit()`
  - this collapses the remaining causal surface tightly onto:
    - `adsInitIsland()`
    - `islandInit()`
    - specifically the initial `OCEAN0?.SCR` / `NIGHT.SCR` background load and the state path that leads there

- Cleanup:
  - reverted the temporary early return after `grLoadScreen(...)`

- Next target:
  - determine why exact `ACTIVITY.ADS 1` is reaching that background-load-only island path and never replacing it with live ACTIVITY scene content

## 18:45 PDT - Skipping exact scene island-state calculation perturbs the lead-in, but ocean still settles by `04910`

- Temporary proof:
  - in [story.c](/home/hunter/workspace/jc_reborn/story.c), skipped:
    - `storyCalculateIslandFromScene(scene)`
  - only for exact `ACTIVITY.ADS 1`
  - left:
    - `adsPrimeSceneResources(...)`
    - `adsInitIsland()`
    - normal intro flow
    - normal later playback path

- Validation run:
  - [skip-scene-islandcalc](/home/hunter/workspace/jc_reborn/regtest-results/activity1-ads1-seed1-skip-scene-islandcalc/result.json)

- Result:
  - run exits cleanly with:
    - `state_hash = 81b56aa4b51befe5badf1bdfff881504bc469fc5a829e1924a706c359620cf03`
  - sampled frames:
    - `frame_04770`: black
    - `frame_04910`: ocean
    - `frame_05000`: ocean

- Important conclusion:
  - scene-specific island-state calculation does affect the lead-in before the settled failure
  - but it does not prevent the stable wrong-ocean state from being re-established by `04910`
  - so it is not sufficient to explain the persistent ACTIVITY failure on its own
  - the strongest remaining causal surface is still:
    - `adsInitIsland()`
    - especially `islandInit()` and its initial background load/composition path

- Cleanup:
  - reverted the temporary skip-scene-islandcalc proof patch after validation

- Next target:
  - isolate `adsInitIsland()` / `islandInit()` more directly, because that is now the tightest surviving source of the settled wrong-ocean outcome
