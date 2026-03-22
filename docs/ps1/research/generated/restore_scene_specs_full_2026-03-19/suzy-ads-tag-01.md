# Restore Pilot Spec

Date: 2026-03-18

Selected pilot: `SUZY.ADS tag 1` from `suzy-ads`

## Scene envelope

- scene index: `52`
- restore score: `54`
- peak memory: `355508`
- union rect: `{'x': 128, 'y': 77, 'width': 384, 'height': 268}`
- unique rects: `3`
- TTM owners: `MEANWHIL.TTM, SJMSUZY.TTM, SUZYCITY.TTM`

## Scene resources

- BMPs: `SSUZY1.BMP, SSUZY2.BMP, SSUZY3.BMP, LITEBULB.BMP, MEANWHIL.BMP, SJMSUZY1.BMP, MRAFT.BMP, SJMSUZY2.BMP, SJMSUZY3.BMP`
- SCRs: `SUZBEACH.SCR`
- TTMs: `SUZYCITY.TTM, SJMSUZY.TTM, MEANWHIL.TTM`

## Runtime scope

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.

## TTM details

- `MEANWHIL.TTM`: rect `{'x': 224, 'y': 90, 'width': 160, 'height': 134}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 1, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 49}`
- `SJMSUZY.TTM`: rect `{'x': 128, 'y': 77, 'width': 384, 'height': 268}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 85}`
- `SUZYCITY.TTM`: rect `{'x': 128, 'y': 77, 'width': 384, 'height': 268}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 97}`
