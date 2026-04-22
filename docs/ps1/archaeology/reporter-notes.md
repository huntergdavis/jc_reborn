# PS1 Archaeology: Reporter Notes

Source plan: [../ps1-branch-cleanup-plan.yaml](../ps1-branch-cleanup-plan.yaml)  
Related: [timeline.yaml](./timeline.yaml), [tools.yaml](./tools.yaml), [status-surfaces.yaml](./status-surfaces.yaml), [team-perspective.yaml](./team-perspective.yaml), [assumptions.yaml](./assumptions.yaml), [memory-constraints.md](./memory-constraints.md), [blog-source-map.md](./blog-source-map.md)

The cleanest read is that the PS1 branch kept changing jobs, not standards. Early on, the agent-facing material assumes a hard embedded port: exact commands, SDK quirks, missing libc, draw-order bugs, and memory maps. By March and early April, the same branch is acting like a harness builder and validation skeptic, producing dashboards, compare reports, semantic truth manifests, VLM packs, and regression surfaces because nobody wanted to count the wrong thing as success.

Early April then turns into archaeology at scale. The branch builds corpora, onset scans, and boundary tools to understand when FISHING 1 broke and when it recovered. That phase matters because it explains why the repo accumulated so many scripts, manifests, binary artifacts, and competing status surfaces. Those were not random detours. They were instruments built to answer real uncertainty.

The decisive turn is April 12 through April 22. The work stops trying to prove a universal replay theory and starts tightening a bespoke line that actually closes: offline foreground capture, scene-relative packs, restored ocean base, waves, draw-event capture, validated state variants, then captured sound events replayed through the PS1 path. That is the chapter where the repo stops searching for an answer and starts shipping one.

Several story points should remain explicit in any future writeup. The branch did not begin with `fgpilot`; it began with general portability and low-memory optimism. The repo owner was already acting as a multi-platform porter before PS1 became the central story. The meaning of `verified` changed multiple times, and that should be preserved as part of the learning process rather than edited away. March and early April contain false summits that make the final bespoke method more convincing, not less. And the latest chapter is small but important: the first scene is no longer only pixel-perfect, it is fully working with sound.
