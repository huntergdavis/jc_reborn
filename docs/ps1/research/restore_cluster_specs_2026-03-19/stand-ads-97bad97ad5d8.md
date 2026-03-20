# Restore Cluster Spec

Date: 2026-03-19

- ads: `STAND.ADS`
- selected tag: `15`
- cluster tags: `[15, 16]`
- cluster scenes: `[50, 51]`
- cluster status: `offline_ready`
- cluster size: `2`

## Notes

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.
- This spec was lifted from a shared restore-contract cluster.
- Promote the whole tag list together when runtime validation confirms the contract.
