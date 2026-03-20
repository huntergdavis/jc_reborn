# Restore Pilot Spec

Date: 2026-03-18

Selected pilot: `STAND.ADS tag 9` from `stand-ads`

## Scene envelope

- scene index: `46`
- restore score: `118`
- peak memory: `166164`
- union rect: `{'x': 248, 'y': 196, 'width': 352, 'height': 140}`
- unique rects: `2`
- TTM owners: `MJAMBWLK.TTM, MJTELE.TTM`

## Scene resources

- BMPs: `MJ_AMB.BMP`
- SCRs: `ISLETEMP.SCR`
- TTMs: `MJAMBWLK.TTM, MJTELE.TTM`

## Runtime scope

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.

## TTM details

- `MJAMBWLK.TTM`: rect `{'x': 248, 'y': 196, 'width': 344, 'height': 139}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 273}`
- `MJTELE.TTM`: rect `{'x': 256, 'y': 197, 'width': 344, 'height': 139}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 121}`
