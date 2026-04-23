# Retired Scripts — Binary Library Archaeology

These scripts belonged to the 2026-03-29 .. 2026-04-11 binary-library
regression era (see `../timeline.yaml` and `../tools.yaml`). They are
preserved here as historical archaeology per
`../../ps1-branch-cleanup-plan.yaml` phase_04 archive_candidates.
They are **not** invoked by the current operator workflow.

## Inventory

| Script | Purpose | Status at archival |
|---|---|---|
| `build-binary-library.sh` | Build a PS1 exe + CD image for every code-changing commit since the port began. Produced the ~118 GB `binary-library/` corpus. | Last touched 2026-04-12 (commit 0c6387a9) |
| `regtest-binary-library-scene.sh` | Run regtest against a specific historical entry from the binary library. | Last touched 2026-04-12 (commit fa5be68e) |
| `scan-fishing1-binary-regression.sh` | Sweep the binary library to find when FISHING 1 regressed. | Last touched 2026-04-11 (commit 8d2e6d90) |
| `find-fishing1-regression-boundary.py` | Binary-search the library for a regression boundary. | Same era |
| `find-fishing1-startup-onset.sh` | Locate the commit where FISHING 1 startup first appeared. | Last touched 2026-04-11 (commit 8d2e6d90) |
| `find-fishing1-visibility-onset.sh` | Locate when FISHING 1 became visible on-screen. | Last touched 2026-04-11 (commit 8d2e6d90) |
| `continue-fishing1-startup-onset.sh` | Resume an interrupted onset scan. | Same era |
| `refine-fishing1-startup-onset.sh` | Narrow a previously-found onset range. | Same era |
| `refine-fishing1-visibility-onset.sh` | Narrow a previously-found visibility-onset range. | Same era |
| `report-binary-library-epochs.py` | Summarise library coverage by epoch. | Last touched 2026-04-11 (commit 5d374a46) |
| `report-binary-library-gaps.py` | Report coverage gaps in the library. | Same era |
| `report-binary-library-sequence-resets.py` | Detect sequence discontinuities. | Same era |
| `verify-fishing1-head-clean.sh` | Verify a clean HEAD against the binary-library baseline. | Last touched 2026-04-11 (commit 047c1685) |

## Why archived, not deleted

- Git history keeps the source, but having these grepped in one place
  (here) makes the "what existed during the binary-library era" question
  findable without commit archaeology.
- They reference a workflow that the current scene-playback path has
  replaced — re-running them against today's HEAD is not expected to work
  without adaptation.

## If you need one of them again

1. Copy out of this directory (do not symlink — they expect to live under
   `scripts/`).
2. Check whether the Docker image `jc-reborn-ps1-dev:amd64` still builds
   the target commit range.
3. Check whether the helpers' expected paths still exist (e.g. the
   `binary-library/` tree itself, which the cleanup plan archives out of
   the repo in phase_04).
