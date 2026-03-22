# Restore Pilot Spec

Date: 2026-03-18

Selected pilot: `FISHING.ADS tag 7` from `fishing-ads`

## Scene envelope

- scene index: `23`
- restore score: `94`
- peak memory: `414002`
- union rect: `{'x': 0, 'y': 111, 'width': 640, 'height': 239}`
- unique rects: `4`
- TTM owners: `FISHWALK.TTM, GFFFOOD.TTM, GJCATCH2.TTM, MJFISH.TTM, MJFISHC.TTM`

## Scene resources

- BMPs: `TRUNK.BMP, MJFISH1.BMP, MJFISH2.BMP, MJFISH3.BMP, JOHNWALK.BMP, GJCATCH1.BMP, LILFISH.BMP, GJCATCH3.BMP, GJCATCH2.BMP, SPLASH.BMP, FISHMAN.BMP, SHKNFIST.BMP, MJDIVE.BMP, GJFFFOOD.BMP`
- SCRs: `ISLAND2.SCR`
- TTMs: `MJFISH.TTM, GJCATCH2.TTM, GFFFOOD.TTM, MJFISHC.TTM, FISHWALK.TTM`

## Runtime scope

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.

## TTM details

- `FISHWALK.TTM`: rect `{'x': 88, 'y': 142, 'width': 552, 'height': 208}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 22}`
- `GFFFOOD.TTM`: rect `{'x': 136, 'y': 111, 'width': 504, 'height': 239}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 47}`
- `GJCATCH2.TTM`: rect `{'x': 0, 'y': 157, 'width': 640, 'height': 193}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 61}`
- `MJFISH.TTM`: rect `{'x': 0, 'y': 157, 'width': 640, 'height': 193}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 370}`
- `MJFISHC.TTM`: rect `{'x': 0, 'y': 138, 'width': 640, 'height': 212}`, clear regions `[0]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 0, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 256}`
