# Restore Cluster Spec

Date: 2026-03-19

- ads: `FISHING.ADS`
- selected tag: `1`
- cluster tags: `[1, 2, 6]`
- cluster scenes: `[17, 18, 22]`
- cluster status: `blocked_entry_path`
- cluster size: `3`

## Notes

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.
- This spec was lifted from a shared restore-contract cluster.
- Promote the whole tag list together when runtime validation confirms the contract.
