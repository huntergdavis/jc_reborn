# Restore Pilot Spec

Date: 2026-03-18

Selected pilot: `MISCGAG.ADS tag 2` from `miscgag-ads`

## Scene envelope

- scene index: `37`
- restore score: `57`
- peak memory: `294508`
- union rect: `{'x': 0, 'y': 61, 'width': 640, 'height': 289}`
- unique rects: `4`
- TTM owners: `GJGULL1.TTM, GJHOT.TTM, SHARK1.TTM`

## Scene resources

- BMPs: `SHARKWLK.BMP, TRUNK.BMP, SHARK.BMP, JOHNWALK.BMP, GJHOT.BMP, MJREAD.BMP, GJGULL1.BMP, GJGULL1A.BMP`
- SCRs: `ISLETEMP.SCR`
- TTMs: `GJHOT.TTM, SHARK1.TTM, GJGULL1.TTM`

## Runtime scope

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.

## TTM details

- `GJGULL1.TTM`: rect `{'x': 160, 'y': 61, 'width': 480, 'height': 289}`, clear regions `[0]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 0, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 107}`
- `GJHOT.TTM`: rect `{'x': 280, 'y': 200, 'width': 304, 'height': 150}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 89}`
- `SHARK1.TTM`: rect `{'x': 0, 'y': 171, 'width': 584, 'height': 179}`, clear regions `[0, 1]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 0, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 88}`
