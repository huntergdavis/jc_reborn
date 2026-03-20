# Restore Pilot Spec

Date: 2026-03-18

Selected pilot: `BUILDING.ADS tag 4` from `building-ads`

## Scene envelope

- scene index: `11`
- restore score: `84`
- peak memory: `362524`
- union rect: `{'x': 40, 'y': 56, 'width': 600, 'height': 294}`
- unique rects: `11`
- TTM owners: `GJGULIVR.TTM, MJFIRE.TTM, MJSAND.TTM`

## Scene resources

- BMPs: `JOHNWALK.BMP, STNDLAY.BMP, ZZZZS.BMP, SLEEP.BMP, SHIPS.BMP, GJBIPLAN.BMP, LILIPUTS.BMP, TRUNK.BMP, GJGULL1.BMP`
- SCRs: `ISLETEMP.SCR`
- TTMs: `MJSAND.TTM, GJGULIVR.TTM, MJFIRE.TTM`

## Runtime scope

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.

## TTM details

- `GJGULIVR.TTM`: rect `{'x': 120, 'y': 90, 'width': 520, 'height': 260}`, clear regions `[0, 1]`, op counts `{'region_id': 3, 'copy_zone_to_bg': 2, 'save_image1': 3, 'save_zone': 1, 'restore_zone': 1, 'clear_screen': 470}`
- `MJFIRE.TTM`: rect `{'x': 240, 'y': 134, 'width': 352, 'height': 204}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 324}`
- `MJSAND.TTM`: rect `{'x': 40, 'y': 56, 'width': 568, 'height': 294}`, clear regions `[0, 1]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 3, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 294}`
