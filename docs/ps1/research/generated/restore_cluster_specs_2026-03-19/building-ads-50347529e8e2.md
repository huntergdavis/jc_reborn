# Restore Cluster Spec

Date: 2026-03-19

- ads: `BUILDING.ADS`
- selected tag: `3`
- cluster tags: `[3, 4, 6]`
- cluster scenes: `[11, 12, 16]`
- cluster status: `offline_ready`
- cluster size: `3`

## Notes

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.
- This spec was lifted from a shared restore-contract cluster.
- Promote the whole tag list together when runtime validation confirms the contract.
