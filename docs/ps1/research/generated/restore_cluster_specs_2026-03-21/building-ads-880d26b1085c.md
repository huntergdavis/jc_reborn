# Restore Cluster Spec

Date: 2026-03-21

- ads: `BUILDING.ADS`
- selected tag: `5`
- cluster tags: `[5, 7]`
- cluster scenes: `[14, 15]`
- cluster status: `blocked_entry_path_or_unreliable_route`
- cluster size: `2`

## Notes

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.
- This spec was lifted from a shared restore-contract cluster.
- Promote the whole tag list together when runtime validation confirms the contract.
