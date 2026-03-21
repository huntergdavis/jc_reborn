# Restore Pilot Spec

Date: 2026-03-18

Selected pilot: `VISITOR.ADS tag 7` from `visitor-ads`

## Scene envelope

- scene index: `58`
- restore score: `76`
- peak memory: `335648`
- union rect: `{'x': 0, 'y': 35, 'width': 640, 'height': 315}`
- unique rects: `36`
- TTM owners: `GJLILIPU.TTM, GJVIS3.TTM, GJVIS5.TTM, GJVIS6.TTM, MJCOCO.TTM`

## Scene resources

- BMPs: `MJCOCO.BMP, JOHNWALK.BMP, COCONUTS.BMP, SPLASH.BMP, TRUNK.BMP, COCOHEAD.BMP, MJREAD.BMP, MJTELE.BMP, MJTELE2.BMP, GJVIS3.BMP, GJCASTLE.BMP, SHIPS.BMP, GJVIS6.BMP, TANKER.BMP, GJPROW.BMP, GJVIS5.BMP, GJVIS52.BMP`
- SCRs: `ISLETEMP.SCR`
- TTMs: `GJVIS3.TTM, GJLILIPU.TTM, GJVIS6.TTM, MJCOCO.TTM, GJVIS5.TTM`

## Runtime scope

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.

## TTM details

- `GJLILIPU.TTM`: rect `{'x': 264, 'y': 40, 'width': 376, 'height': 274}`, clear regions `[0, 1]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 100}`
- `GJVIS3.TTM`: rect `{'x': 0, 'y': 35, 'width': 640, 'height': 315}`, clear regions `[0, 1]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 0, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 329}`
- `GJVIS5.TTM`: rect `{'x': 0, 'y': 73, 'width': 640, 'height': 252}`, clear regions `[0, 1]`, op counts `{'region_id': 2, 'copy_zone_to_bg': 0, 'save_image1': 2, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 250}`
- `GJVIS6.TTM`: rect `{'x': 0, 'y': 43, 'width': 640, 'height': 307}`, clear regions `[0, 1, 2]`, op counts `{'region_id': 4, 'copy_zone_to_bg': 28, 'save_image1': 3, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 157}`
- `MJCOCO.TTM`: rect `{'x': 232, 'y': 117, 'width': 408, 'height': 233}`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 143}`
