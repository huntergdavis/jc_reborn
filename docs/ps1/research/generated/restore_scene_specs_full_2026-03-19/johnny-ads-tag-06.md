# Restore Pilot Spec

Date: 2026-03-18

Selected pilot: `JOHNNY.ADS tag 6` from `johnny-ads`

## Scene envelope

- scene index: `30`
- restore score: `111`
- peak memory: `223606`
- union rect: `{'x': 0, 'y': 87, 'width': 528, 'height': 263}`
- unique rects: `5`
- TTM owners: `MEANWHIL.TTM, SJMSSGE.TTM, SJWORK.TTM, THEEND.TTM`

## Scene resources

- BMPs: `SJWORK.BMP, THNKBUBL.BMP, MEANWHIL.BMP, THEEND1.BMP, ENDCRDTS.BMP, JOHNWALK.BMP, MJBOTTLE.BMP, MJBTL2.BMP, MJFISH3.BMP, LITEBULB.BMP`
- SCRs: `JOFFICE.SCR`
- TTMs: `THEEND.TTM, SJMSSGE.TTM, SJWORK.TTM, MEANWHIL.TTM`

## Runtime scope

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.

## TTM details

- `MEANWHIL.TTM`: rect `{'x': 224, 'y': 90, 'width': 160, 'height': 134}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 1, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 49}`
- `SJMSSGE.TTM`: rect `{'x': 0, 'y': 147, 'width': 448, 'height': 203}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 142}`
- `SJWORK.TTM`: rect `{'x': 192, 'y': 199, 'width': 264, 'height': 142}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 25}`
- `THEEND.TTM`: rect `{'x': 144, 'y': 87, 'width': 384, 'height': 152}`, region ids `[0]`, clear regions `[0]`, op counts `{'region_id': 1, 'copy_zone_to_bg': 0, 'save_image1': 1, 'save_zone': 0, 'restore_zone': 0, 'clear_screen': 73}`
