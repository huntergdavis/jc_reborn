# Scene Transition and Prefetch Report

Generated from `scene_analysis_output_2026-03-17.json`.

## Caveats
- Scene ordering comes from the analyzer's story order, not from instrumented runtime control flow.
- Prefetch hints are based on resource churn between adjacent analyzer scenes.
- Pack candidates are ADS-family groups with unioned resource sets; they are not yet validated CD layouts.

## Pack Candidates
- `MARY.ADS`: 5 scenes, union 1240.3 KB, peak 555.3 KB, exceeds PS1 budget
- `ACTIVITY.ADS`: 10 scenes, union 845.1 KB, peak 426.4 KB, exceeds PS1 budget
- `FISHING.ADS`: 8 scenes, union 819.2 KB, peak 550.0 KB, exceeds PS1 budget
- `VISITOR.ADS`: 6 scenes, union 648.2 KB, peak 343.0 KB, exceeds PS1 budget
- `BUILDING.ADS`: 7 scenes, union 595.2 KB, peak 431.6 KB, fits PS1 budget
- `JOHNNY.ADS`: 6 scenes, union 560.1 KB, peak 326.5 KB, fits PS1 budget
- `SUZY.ADS`: 2 scenes, union 465.3 KB, peak 449.7 KB, fits PS1 budget
- `WALKSTUF.ADS`: 3 scenes, union 422.1 KB, peak 333.4 KB, fits PS1 budget
- `MISCGAG.ADS`: 2 scenes, union 300.7 KB, peak 287.6 KB, fits PS1 budget
- `STAND.ADS`: 14 scenes, union 178.0 KB, peak 162.3 KB, fits PS1 budget

## Top Prefetch Edges
- 30 -> 31 (`JOHNNY.ADS` -> `MARY.ADS`): add 539.7 KB, shared 0.0 KB, class `heavy`
- 51 -> 52 (`STAND.ADS` -> `SUZY.ADS`): add 325.8 KB, shared 0.0 KB, class `heavy`
- 9 -> 10 (`ACTIVITY.ADS` -> `BUILDING.ADS`): add 319.1 KB, shared 109.4 KB, class `heavy`
- 25 -> 26 (`JOHNNY.ADS` -> `JOHNNY.ADS`): add 287.7 KB, shared 9.8 KB, class `heavy`
- 34 -> 35 (`MARY.ADS` -> `MARY.ADS`): add 267.6 KB, shared 88.1 KB, class `heavy`
- 59 -> 60 (`VISITOR.ADS` -> `WALKSTUF.ADS`): add 266.7 KB, shared 47.3 KB, class `heavy`
- 20 -> 21 (`FISHING.ADS` -> `FISHING.ADS`): add 244.7 KB, shared 159.8 KB, class `heavy`
- 5 -> 6 (`ACTIVITY.ADS` -> `ACTIVITY.ADS`): add 244.6 KB, shared 160.9 KB, class `heavy`
- 2 -> 3 (`ACTIVITY.ADS` -> `ACTIVITY.ADS`): add 233.1 KB, shared 172.4 KB, class `heavy`
- 8 -> 9 (`ACTIVITY.ADS` -> `ACTIVITY.ADS`): add 222.9 KB, shared 163.0 KB, class `heavy`
- 0 -> 1 (`ACTIVITY.ADS` -> `ACTIVITY.ADS`): add 209.6 KB, shared 195.8 KB, class `heavy`
- 53 -> 54 (`SUZY.ADS` -> `VISITOR.ADS`): add 205.4 KB, shared 0.0 KB, class `heavy`

## Top Pack Boundaries
- 31 -> 32 working set 739.4 KB, added 199.8 KB, class `heavy`
- 30 -> 31 working set 732.0 KB, added 539.7 KB, class `heavy`
- 9 -> 10 working set 705.1 KB, added 319.1 KB, class `heavy`
- 19 -> 20 working set 683.9 KB, added 164.4 KB, class `heavy`
- 34 -> 35 working set 641.4 KB, added 267.6 KB, class `heavy`
- 53 -> 54 working set 634.9 KB, added 205.4 KB, class `heavy`
- 20 -> 21 working set 627.3 KB, added 244.7 KB, class `heavy`
- 59 -> 60 working set 588.3 KB, added 266.7 KB, class `heavy`
- 32 -> 33 working set 566.2 KB, added 199.2 KB, class `heavy`
- 5 -> 6 working set 545.1 KB, added 244.6 KB, class `heavy`
- 24 -> 25 working set 533.1 KB, added 150.5 KB, class `heavy`
- 0 -> 1 working set 526.0 KB, added 209.6 KB, class `heavy`

