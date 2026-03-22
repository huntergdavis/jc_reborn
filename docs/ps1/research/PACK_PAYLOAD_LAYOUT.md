# Pack Payload Layout

Date: 2026-03-17
Status: Draft compiler format

## Purpose

Describe the first concrete compiled-pack artifact produced from a scene-pack
manifest.

This is the current payload layout for compiler and loader work. It is not yet
the final on-disc binary format.

## Artifact shape

The compiler currently emits two files per compiled pack:

- `pack_payload.bin`
- `pack_index.json`
- staged CD payload copies: `jc_resources/packs/*.PAK`

Current outputs:

- [compiled pack directory](/home/hunter/workspace/jc_reborn/docs/ps1/research/generated/compiled_packs_2026-03-17)

## `pack_payload.bin`

This is a flat binary blob containing:

1. a compact binary header
2. a compact resource table of contents
3. raw extracted resources concatenated in a deterministic order

Current resource order:

1. ADS
2. SCR
3. TTM
4. BMP

The current header includes:

- magic
- version
- entry count
- first-resource offset

Each TOC entry includes:

- resource-type code
- offset
- size
- fixed-width resource name

Each resource starts on a `2048`-byte boundary so the payload can already be
reasoned about in CD-sector terms. The first resource currently begins at sector 1,
after the header/TOC block is padded up to `2048` bytes.

## `pack_index.json`

This is the loader-facing metadata sidecar for the current compiler format.

Top-level fields:

- `schema_version`
- `artifact_kind`
- `pack_id`
- `source_manifest`
- `pack_strategy`
- `cluster_key`
- `scene_indices`
- `runtime_requirements`
- `transition_hints`
- `prefetch_hints`
- `dirty_region_templates`
- `payload`
- `entries`

`dirty_region_templates` is an offline sidecar reference, not a runtime-on-disc
format field. When a matching extracted template exists, the compiler now copies
it into the compiled pack directory as `dirty_region_templates.json` and records:

- sidecar path
- source template path
- availability flag
- candidate scene indices
- restore-candidate TTM count

Each entry includes:

- `resource_type`
- `name`
- `source_path`
- `offset_bytes`
- `size_bytes`
- `aligned_size_bytes`
- `sector_index`
- `sector_count`
- `sha256`

## Why this is enough for the next step

This layout is intentionally simple, but it already gives the runtime-loader work:

- deterministic payload order
- deterministic sector alignment
- explicit offsets and sizes
- integrity checks for debugging
- direct access to transition and prefetch hints from the source manifest
- a self-describing on-disc TOC that the runtime can read directly from the pack

## Known limitations

- resource bytes are still original extracted assets, not transcoded PS1-native
  sprite banks
- the research sidecar index is still JSON
- no compression is applied yet
- no shared-bank deduplication is applied across packs yet
- no checksum validation is performed on-console yet
- dirty-region templates are compiler sidecars only; runtime consumption remains
  intentionally gated until per-scene restore policy is validated
