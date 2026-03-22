# Restore Pilot Spec

Date: 2026-03-18

Selected pilot: `WALKSTUF.ADS tag 1` from `walkstuf-ads`

## Scene envelope

- scene index: `60`
- restore score: `99`
- peak memory: `341368`
- union rect: `{'x': 0, 'y': 91, 'width': 640, 'height': 259}`
- unique rects: `4`
- TTM owners: `MJJOG.TTM, MJRAFT.TTM, WOULDBE.TTM`

## Scene resources

- BMPs: `JOHNWALK.BMP, WOULDBE.BMP, BOAT.BMP, TRUNK.BMP, LITEBULB.BMP, JOHNWOUL.BMP, DRUNKJON.BMP, MJRAFT2.BMP, SJRAFT1.BMP, MJJOG2.BMP, JCHANGE.BMP, MJJOG1.BMP`
- SCRs: `ISLETEMP.SCR`
- TTMs: `WOULDBE.TTM, MJRAFT.TTM, MJJOG.TTM`

## Runtime scope

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.

## TTM details

- `MJJOG.TTM`: rect `{'x': 280, 'y': 200, 'width': 304, 'height': 150}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 224}`
- `MJRAFT.TTM`: rect `{'x': 232, 'y': 125, 'width': 408, 'height': 225}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 34}`
- `WOULDBE.TTM`: rect `{'x': 0, 'y': 91, 'width': 640, 'height': 228}`, region ids `[0, 1]`, clear regions `[0, 1]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 0, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 278}`
