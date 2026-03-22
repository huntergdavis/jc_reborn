# Restore Pilot Spec

Date: 2026-03-18

Selected pilot: `ACTIVITY.ADS tag 11` from `activity-ads`

## Scene envelope

- scene index: `2`
- restore score: `93`
- peak memory: `297594`
- union rect: `{'x': 0, 'y': 43, 'width': 640, 'height': 307}`
- unique rects: `9`
- TTM owners: `GJDIVE.TTM, GJNAT1.TTM, GJNAT3.TTM, MJBATH.TTM, MJDIVE.TTM, MJREAD.TTM`

## Scene resources

- BMPs: `TRUNK.BMP, MJBATH.BMP, JCHANGE.BMP, GJGULL1.BMP, GJGULL2.BMP, GJGULL2A.BMP, MJDIVE.BMP, GJDIVE.BMP, JOHNWALK.BMP, COCONUTS.BMP, MJREAD.BMP, ZZZZS.BMP, LITEBULB.BMP, GJGULL1A.BMP, GJGULL3.BMP, GJGULL3A.BMP, GJHOT.BMP, GJNAT1LI.BMP, MEXCWALK.BMP, GJNAT1.BMP, GJNAT3.BMP, BOAT.BMP`
- SCRs: `ISLETEMP.SCR`
- TTMs: `GJDIVE.TTM, MJDIVE.TTM, MJREAD.TTM, MJBATH.TTM, GJNAT1.TTM, GJNAT3.TTM`

## Runtime scope

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.

## TTM details

- `GJDIVE.TTM`: rect `{'x': 96, 'y': 43, 'width': 400, 'height': 295}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 115}`
- `GJNAT1.TTM`: rect `None`, region ids `[]`, clear regions `[0]`, op counts `{'region_id': 0, 'copy_zone_to_bg': 0, 'save_image1': 0, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 177}`
- `GJNAT3.TTM`: rect `{'x': 0, 'y': 128, 'width': 640, 'height': 180}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 206}`
- `MJBATH.TTM`: rect `{'x': 144, 'y': 77, 'width': 496, 'height': 273}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 0, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 190}`
- `MJDIVE.TTM`: rect `{'x': 155, 'y': 50, 'width': 336, 'height': 288}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 93}`
- `MJREAD.TTM`: rect `{'x': 160, 'y': 60, 'width': 480, 'height': 290}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 5, 'copy_zone_to_bg': 0, 'save_image1': 5, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 295}`
