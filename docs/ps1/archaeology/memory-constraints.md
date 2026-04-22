# PS1 Archaeology: Memory Constraints

Source plan: [../ps1-branch-cleanup-plan.yaml](../ps1-branch-cleanup-plan.yaml)  
Related: [timeline.yaml](./timeline.yaml), [tools.yaml](./tools.yaml), [status-surfaces.yaml](./status-surfaces.yaml), [team-perspective.yaml](./team-perspective.yaml), [assumptions.yaml](./assumptions.yaml), [blog-source-map.md](./blog-source-map.md), [reporter-notes.md](./reporter-notes.md)

The PS1 branch was never choosing among equally comfortable designs. Two hard facts shaped nearly every decision: the target lives inside 2 MB of main RAM and 1 MB of VRAM, and the port insisted on native 640x480 output. That combination made generic, high-overhead solutions look attractive on paper and fragile at runtime.

The most obvious dead end was full-screen prerender for every scene. A single 640x480 16-bit frame is about 614400 bytes. Even indexed, a full frame is about 307200 bytes. At 63 scenes and multiple states, naive video-style coverage turns into multi-gigabyte storage quickly. That ruled out any serious plan that depended on checking large frame payloads into the branch as the steady-state answer.

Static fit numbers also proved weaker than they looked. March analysis could show zero budget violations and a heaviest scene around 555.3 KB, but April runtime work still hit allocation failures once real load order and transient heap shape entered the picture. The lesson was simple: memory totals mattered, but allocation timing and object lifetime mattered more.

That pressure made the branch increasingly specific. Full background-tile clean backups were about 614 KB, which was expensive enough to force tighter scoping. The FISHING 1 rect-based clean backup brought the dynamic region down to about 181 KB, which is the kind of reduction that changes a method from theoretical to usable.

The wave path told the same story in miniature. `BACKGRND.PSB` was only about 93 KB as an asset, but the read path could peak around 186 KB because it allocated twice. The raw BMP fallback was about 150 KB. Loading that asset before bg-tile allocation solved the practical problem, which is why preload order became part of the method rather than an implementation detail.

Taken together, these constraints explain the winning line. The branch moved away from generic reconstruction and toward a lower-overhead hybrid: exact host-captured foreground pixels, narrow runtime scene-base work, scene-relative packs, targeted clean backups, careful preload order, and finally captured SFX replay. Memory pressure did not merely constrain the solution. It selected it.
