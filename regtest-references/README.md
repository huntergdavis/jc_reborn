Host reference corpus for the PS1 regtest workflow.

Contents
- 63 canonical host scene captures
- one directory per scene, named `<ADS>-<TAG>` like `BUILDING-1`
- per-scene `frames/`, `frames-png/`, `metadata.json`, `result.json`, and `review.html`
- top-level `index.html`, `mega.html`, `manifest.json`, and `manifest.csv`

Capture policy
- source: PC/host executable
- boot route: scene-authored default route from `config/ps1/regtest-scenes.txt`
- capture mode: engine-truth scene-end
- stop rule: capture until the authored scene returns, with loop-aware scheduler-state stop for deterministic tail loops

Notes
- This corpus replaces the earlier invalid PS1-vs-PS1 reference set.
- Absolute paths inside generated artifacts were rewritten to this canonical root after capture so the review pages and manifests stay stable.
