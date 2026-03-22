# Restore Cluster Spec

Date: 2026-03-21

- ads: `ACTIVITY.ADS`
- selected tag: `6`
- cluster tags: `[6, 7, 10, 12]`
- cluster scenes: `[1, 3, 6, 7]`
- cluster status: `artifact_ready_unverified`
- cluster size: `4`

## Notes

- Keep pack path authoritative; do not reintroduce extracted fallback.
- Scope runtime attempts to the compatible scene cluster only, not the whole ADS family.
- Prefer rect-restore validation around the listed TTM names before touching replay carry logic.
- This spec was lifted from a shared restore-contract cluster.
- Promote the whole tag list together when runtime validation confirms the contract.
