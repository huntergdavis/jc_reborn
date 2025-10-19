
# jc_reborn — Memory-lite refactor

This branch replaces eager, whole-blob resource decompression with **lazy, on-demand streaming + a tiny LRU cache**, holding at most a small working set in RAM.

## What changed (high level)

- **No more up-front decompression.** `RESOURCE.001` is parsed, but for each ADS/BMP/SCR/TTM the engine now records the file offset of the compressed payload and _does not_ inflate it immediately.
- New getters in `resource.[ch]`: `res_get_*()` **decompress on first use** and reuse a small global buffer pool tracked by an LRU. When memory exceeds a budget, the least-recently used unpinned buffer is freed.
- **Pin/unpin**: TTMs that are actively running and ADS scripts being played are pinned (won’t be evicted). BMP/SCR payloads are explicitly released after conversion to SDL surfaces or background buffers.
- **Budget control**: default is **4 MiB**. Override with env var:  
  `JC_MEM_BUDGET_MB=4` (or another integer value).
- Zero feature loss: same content, same timing/logic; only the loading strategy changed.

## How it works

- In `resource.c`, `parse*Resource()` functions now store:
  - `compressedSize`, `compressionMethod`, `uncompressedSize`, and a new `dataOffset` (ftell) for the compressed blob.
  - `uncompressedData` starts as `NULL` and is filled on demand.
- `res_get_*()` opens `RESOURCE.001`, seeks to `dataOffset` and calls existing `uncompress()` into a freshly allocated buffer. The cache accounts the size, bumps an access tick, and evicts the oldest unpinned blobs while above budget.
- The cache is deliberately simple (no extra files, fully portable, O(N) scan) because there are only a few hundred resources.

## Touch points in code

- `resource.h/.c`: new fields (`dataOffset`, `lastUsedTick`, `pinCount`), new API, and lazy load path.  
- `ads.c`: ADS data now fetched via `res_get_ads_data()`; pinned during playback.  
- `ttm.c` + `graphics.h`: `TTtmSlot` now keeps a `resourceRef` so we can unpin when the slot resets; TTM data fetched via `res_get_ttm_data()`.  
- `graphics.c`: SCR/BMP load paths now call `res_get_*()` and explicitly `res_release_*()` once converted.  
- `dump.c`: switched to getters so dev tools still work.

## Building

Same as before, e.g. on Linux:

```
make -f Makefile.linux
```

or Windows/MinGW:

```
make -f Makefile.MinGW
```

No new libs, no platform-specific APIs; everything uses stdio + existing `uncompress()`.

## Runtime controls

- **Budget**: `JC_MEM_BUDGET_MB` (integer, default 4). Example:
  ```
  JC_MEM_BUDGET_MB=4 ./jc_reborn window
  ```
- Use `debug` to see loading/eviction traces if you add prints (left minimal to avoid noise).

## Expected footprint

- Previously: up-front inflate of ADS/BMP/SCR/TTM could push >20 MiB peak.
- Now: steady-state ~2–4 MiB for the working set on typical scenes (TTM pinned + a transient BMP/SCR), with spikes smoothed by eviction.

## Notes / edge cases

- If **all live resources are pinned** (extremely rare), the engine may temporarily exceed the budget to remain correct. When something unpins, eviction will bring RAM back down.
- Threading: ttm background animation is light; the cache is intentionally coarse-grained (no explicit locks). If you later add multi-threaded resource consumers, protect `res_get_*()` with an SDL mutex.
- Disk I/O: seeking within `RESOURCE.001` is cheap; the data are small and decompression is fast. If you want _zero_ seeks, a future step would be to pre-extract to per-resource files; the current refactor already makes that optional, not required.

## File list (changed)

- `resource.h`
- `resource.c`
- `ads.c`
- `graphics.h`
- `graphics.c`
- `ttm.c`
- `dump.c`

## License

Retains original GPLv3 license headers.

— Memory-lite refactor by ChatGPT
