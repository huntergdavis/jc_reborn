# Restore Cluster Spec

Date: 2026-03-21

- ads: `STAND.ADS`
- selected tag: `1`
- cluster tags: `[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]`
- cluster scenes: `[38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49]`
- cluster status: `verified_live`
- cluster size: `12`

## Notes

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.
- This spec was lifted from a shared restore-contract cluster.
- Promote the whole tag list together when runtime validation confirms the contract.
