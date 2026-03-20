# Restore Pilot Spec

Date: 2026-03-18

Selected pilot: `MARY.ADS tag 3` from `mary-ads`

## Scene envelope

- scene index: `32`
- restore score: `83`
- peak memory: `390512`
- union rect: `{'x': 0, 'y': 124, 'width': 640, 'height': 226}`
- unique rects: `12`
- TTM owners: `SASKDATE.TTM, SBREAKUP.TTM, SJGLIMPS.TTM, SJLEAVES.TTM, SMDATE.TTM`

## Scene resources

- BMPs: `SMDATE1.BMP, SMGLIMSE.BMP, SMGIFT.BMP, MJREAD.BMP, SJGFTJMP.BMP, LITEBULB.BMP, SJGFTSHY.BMP, SJGFTXCH.BMP, SJGFTASK.BMP, SMGFTWAV.BMP`
- SCRs: `ISLETEMP.SCR`
- TTMs: `SJGLIMPS.TTM, SASKDATE.TTM, SMDATE.TTM, SBREAKUP.TTM, SJLEAVES.TTM`

## Runtime scope

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.

## TTM details

- `SASKDATE.TTM`: rect `{'x': 0, 'y': 174, 'width': 488, 'height': 176}`, clear regions `[0]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 1, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 207}`
- `SBREAKUP.TTM`: rect `{'x': 224, 'y': 125, 'width': 416, 'height': 225}`, clear regions `[0]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 1, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 138}`
- `SJGLIMPS.TTM`: rect `{'x': 0, 'y': 151, 'width': 640, 'height': 199}`, clear regions `[0]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 0, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 201}`
- `SJLEAVES.TTM`: rect `{'x': 88, 'y': 208, 'width': 544, 'height': 141}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 150}`
- `SMDATE.TTM`: rect `{'x': 8, 'y': 124, 'width': 560, 'height': 211}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 2, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 194}`
