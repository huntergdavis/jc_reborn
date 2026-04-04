#!/usr/bin/env python3
"""Estimate per-scene durations by simulating ADS + TTM scheduling."""

from __future__ import annotations

import argparse
import csv
import ctypes
import json
from dataclasses import dataclass
from pathlib import Path


DEFAULT_SCENE_ANALYSIS = Path(
    "docs/ps1/research/generated/scene_analysis_output_2026-03-21.json"
)
DEFAULT_EXTRACTED_ADS = Path("jc_resources/extracted/ads")
DEFAULT_EXTRACTED_TTM = Path("jc_resources/extracted/ttm")
DEFAULT_OUTPUT = Path("docs/ps1/research/generated/scene_duration_estimates_2026-03-27.json")
DEFAULT_CSV_OUTPUT = Path("docs/ps1/research/generated/scene_duration_estimates_2026-03-27.csv")

ADS_THREAD_RUNNING = 1
ADS_THREAD_TERMINATED = 2
BG_DELAY = 40
MAX_SIM_FRAMES = 200000


def u16(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def s16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def skip_ttm_string(data: bytes, offset: int) -> int:
    while data[offset] != 0:
        offset += 1
    offset += 1
    if offset & 1:
        offset += 1
    return offset


@dataclass
class ThreadState:
    slot: int
    tag: int
    ip: int
    delay: int = 4
    timer: int = 0
    scene_timer: int = 0
    scene_iterations: int = 0
    next_goto_offset: int = 0
    is_running: int = ADS_THREAD_RUNNING


@dataclass
class AdsChunk:
    slot: int
    tag: int
    offset: int


class CRand:
    def __init__(self, seed: int) -> None:
        self.libc = ctypes.CDLL(None)
        self.libc.srand.argtypes = [ctypes.c_uint]
        self.libc.rand.restype = ctypes.c_int
        self.libc.srand(ctypes.c_uint(seed))

    def randrange(self, total: int) -> int:
        if total <= 0:
            raise ValueError("randrange total must be positive")
        return int(self.libc.rand()) % total


class TtmProgram:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        self.tags: list[tuple[int, int]] = []
        self.tag_to_offset: dict[int, int] = {}
        self._scan_tags()

    def _scan_tags(self) -> None:
        offset = 0
        data = self.data
        size = len(data)
        while offset < size:
            opcode = u16(data, offset)
            offset += 2
            if opcode in (0x1111, 0x1101):
                tag = u16(data, offset)
                offset += 2
                self.tags.append((tag, offset))
                self.tag_to_offset[tag] = offset
                continue

            num_args = opcode & 0x000F
            if num_args == 0x0F:
                offset = skip_ttm_string(data, offset)
            else:
                offset += num_args * 2

    def find_tag(self, tag: int) -> int:
        return self.tag_to_offset.get(tag, 0)

    def find_previous_tag(self, offset: int) -> int:
        result = 0
        for _, tag_offset in self.tags:
            if tag_offset < offset:
                result = tag_offset
            else:
                break
        return result


class AdsProgram:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        self.tags: list[tuple[int, int]] = []
        self.tag_to_offset: dict[int, int] = {}

    def load(self, target_tag: int) -> tuple[int, list[AdsChunk], list[AdsChunk]]:
        data = self.data
        size = len(data)
        ads_chunks: list[AdsChunk] = []
        ads_chunks_local: list[AdsChunk] = []
        self.tags = []
        self.tag_to_offset = {}
        tag_offset = 0
        bookmarking_chunks = False
        bookmarking_if_not_runnings = False

        offset = 0
        while offset < size:
            opcode = u16(data, offset)
            offset += 2

            if opcode == 0x1350:
                if bookmarking_chunks:
                    bookmarking_if_not_runnings = False
                    slot = u16(data, offset)
                    tag = u16(data, offset + 2)
                    offset += 4
                    ads_chunks.append(AdsChunk(slot, tag, offset))
                else:
                    offset += 4
                continue

            if opcode == 0x1360:
                if bookmarking_chunks and bookmarking_if_not_runnings:
                    slot = u16(data, offset)
                    tag = u16(data, offset + 2)
                    offset += 4
                    ads_chunks.append(AdsChunk(slot, tag, offset))
                else:
                    offset += 4
                continue

            if opcode == 0x1370:
                bookmarking_if_not_runnings = False
                offset += 4
                continue

            known_sizes = {
                0x1070: 4,
                0x1330: 4,
                0x1420: 0,
                0x1430: 0,
                0x1510: 0,
                0x1520: 10,
                0x2005: 8,
                0x2010: 6,
                0x2014: 0,
                0x3010: 0,
                0x3020: 2,
                0x30FF: 0,
                0x4000: 6,
                0xF010: 0,
                0xF200: 2,
                0xFFFF: 0,
                0xFFF0: 0,
            }
            if opcode in known_sizes:
                offset += known_sizes[opcode]
                continue

            self.tags.append((opcode, offset))
            self.tag_to_offset[opcode] = offset
            if opcode == target_tag:
                tag_offset = offset
                bookmarking_chunks = True
                bookmarking_if_not_runnings = True
            else:
                bookmarking_chunks = False
                bookmarking_if_not_runnings = False

        return tag_offset, ads_chunks, ads_chunks_local

    def find_tag(self, tag: int) -> int:
        return self.tag_to_offset.get(tag, 0)


class SceneSimulator:
    def __init__(self, scene: dict, ads_dir: Path, ttm_dir: Path, seed: int) -> None:
        self.scene = scene
        self.scene_index = int(scene["scene_index"])
        self.ads_name = scene["ads_name"]
        self.ads_tag = int(scene["ads_tag"])
        self.flags = set(scene.get("flags", []))
        self.rng = CRand(seed)
        self.ads = AdsProgram(ads_dir / self.ads_name)
        self.slot_to_ttm: dict[int, TtmProgram] = {}
        for row in scene["resources"]["ttms"]:
            slot_id = int(row["slot_id"])
            self.slot_to_ttm[slot_id] = TtmProgram(ttm_dir / row["name"])

        self.threads: list[ThreadState] = []
        self.ads_chunks: list[AdsChunk] = []
        self.ads_chunks_local: list[AdsChunk] = []
        self.ads_stop_requested = False
        self.bg_timer = 0 if "ISLAND" in self.flags else None
        self.total_ticks = 0
        self.frames = 0
        self.max_active_threads = 0
        self.seen_states: dict[tuple, int] = {}

    def is_building_mjsand_thread(self, thread: ThreadState) -> bool:
        ttm = self.slot_to_ttm.get(thread.slot)
        return (
            self.ads_name == "BUILDING.ADS"
            and ttm is not None
            and ttm.path.name == "MJSAND.TTM"
        )

    def is_scene_running(self, slot: int, tag: int) -> bool:
        return any(
            thread.is_running == ADS_THREAD_RUNNING
            and thread.slot == slot
            and thread.tag == tag
            for thread in self.threads
        )

    def stop_scene_index(self, idx: int) -> None:
        self.threads[idx].is_running = 0

    def stop_scene_by_tag(self, slot: int, tag: int) -> None:
        for idx, thread in enumerate(self.threads):
            if thread.is_running and thread.slot == slot and thread.tag == tag:
                self.stop_scene_index(idx)

    def reap_terminated(self) -> None:
        for idx, thread in enumerate(self.threads):
            if thread.is_running == ADS_THREAD_TERMINATED:
                self.stop_scene_index(idx)

    def add_scene(self, slot: int, tag: int, arg3: int) -> None:
        self.reap_terminated()
        ttm = self.slot_to_ttm.get(slot)
        ip = 0 if slot == 0 or ttm is None else ttm.find_tag(tag)
        thread = ThreadState(slot=slot, tag=tag, ip=ip)
        signed = s16(arg3)
        if signed < 0:
            thread.scene_timer = -signed
        elif signed > 0:
            thread.scene_iterations = arg3 - 1
        self.threads.append(thread)

    def random_end(self, ops: list[tuple[str, int, int, int, int]]) -> None:
        if not ops:
            return
        total = sum(weight for _, _, _, _, weight in ops)
        pick = self.rng.randrange(total)
        partial = 0
        chosen = ops[-1]
        for op in ops:
            partial += op[4]
            if pick < partial:
                chosen = op
                break
        op_type, slot, tag, num_plays, _ = chosen
        if op_type == "add":
            self.add_scene(slot, tag, num_plays)
        elif op_type == "stop":
            self.stop_scene_by_tag(slot, tag)

    def play_chunk(self, offset: int) -> None:
        data = self.ads.data
        size = len(data)
        in_rand_block = False
        in_or_block = False
        in_skip_block = False
        in_if_lastplayed_local = False
        rand_ops: list[tuple[str, int, int, int, int]] = []
        continue_loop = True

        while continue_loop and offset < size:
            opcode = u16(data, offset)
            offset += 2

            if opcode == 0x1070:
                slot = u16(data, offset)
                tag = u16(data, offset + 2)
                offset += 4
                in_if_lastplayed_local = True
                self.ads_chunks_local.append(AdsChunk(slot, tag, offset))
            elif opcode == 0x1330:
                offset += 4
            elif opcode == 0x1350:
                offset += 4
                if not in_or_block:
                    continue_loop = False
                in_or_block = False
            elif opcode == 0x1360:
                slot = u16(data, offset)
                tag = u16(data, offset + 2)
                offset += 4
                if self.is_scene_running(slot, tag):
                    in_skip_block = True
            elif opcode == 0x1370:
                slot = u16(data, offset)
                tag = u16(data, offset + 2)
                offset += 4
                in_skip_block = not self.is_scene_running(slot, tag)
            elif opcode == 0x1420:
                pass
            elif opcode == 0x1430:
                in_or_block = True
            elif opcode == 0x1510:
                if in_skip_block:
                    in_skip_block = False
                else:
                    continue_loop = False
            elif opcode == 0x1520:
                args = [u16(data, offset + i * 2) for i in range(5)]
                offset += 10
                if in_if_lastplayed_local:
                    in_if_lastplayed_local = False
                else:
                    self.add_scene(args[1], args[2], args[3])
            elif opcode == 0x2005:
                args = [u16(data, offset + i * 2) for i in range(4)]
                offset += 8
                if not in_skip_block:
                    if in_rand_block:
                        rand_ops.append(("add", args[0], args[1], args[2], args[3]))
                    else:
                        self.add_scene(args[0], args[1], args[2])
            elif opcode == 0x2010:
                args = [u16(data, offset + i * 2) for i in range(3)]
                offset += 6
                if not in_skip_block:
                    if in_rand_block:
                        rand_ops.append(("stop", args[0], args[1], 0, args[2]))
                    else:
                        self.stop_scene_by_tag(args[0], args[1])
            elif opcode == 0x3010:
                rand_ops = []
                in_rand_block = True
            elif opcode == 0x3020:
                weight = u16(data, offset)
                offset += 2
                if in_rand_block:
                    rand_ops.append(("nop", 0, 0, 0, weight))
            elif opcode == 0x30FF:
                self.random_end(rand_ops)
                rand_ops = []
                in_rand_block = False
            elif opcode == 0x4000:
                offset += 6
            elif opcode == 0xF010:
                pass
            elif opcode == 0xF200:
                tag = u16(data, offset)
                offset += 2
                self.play_chunk(self.ads.find_tag(tag))
            elif opcode == 0xFFFF:
                if in_skip_block:
                    in_skip_block = False
                else:
                    self.ads_stop_requested = True
            elif opcode == 0xFFF0:
                pass
            else:
                # another tag begins
                pass

    def play_triggered_chunks(self, slot: int, tag: int) -> None:
        handled_local = False
        if self.ads_chunks_local:
            matched_local: list[AdsChunk] = []
            remaining_local: list[AdsChunk] = []
            for chunk in self.ads_chunks_local:
                if chunk.slot == slot and chunk.tag == tag:
                    matched_local.append(chunk)
                    handled_local = True
                else:
                    remaining_local.append(chunk)
            self.ads_chunks_local = remaining_local
            for chunk in matched_local:
                self.play_chunk(chunk.offset)
        if not handled_local:
            for chunk in self.ads_chunks:
                if chunk.slot == slot and chunk.tag == tag:
                    self.play_chunk(chunk.offset)

    def ttm_play(self, thread: ThreadState) -> None:
        ttm = self.slot_to_ttm.get(thread.slot)
        if ttm is None:
            thread.is_running = ADS_THREAD_TERMINATED
            return

        data = ttm.data
        size = len(data)
        offset = thread.ip
        continue_loop = True

        while continue_loop:
            if offset + 1 >= size:
                thread.is_running = ADS_THREAD_TERMINATED
                break

            opcode = u16(data, offset)
            offset += 2
            num_args = opcode & 0x000F
            if num_args == 0x0F:
                offset = skip_ttm_string(data, offset)
                args: list[int] = []
            else:
                args = [u16(data, offset + i * 2) for i in range(num_args)]
                offset += num_args * 2

            if opcode == 0x0110:
                if thread.scene_timer:
                    thread.next_goto_offset = ttm.find_previous_tag(offset)
                    continue_loop = False
                else:
                    thread.is_running = ADS_THREAD_TERMINATED
            elif opcode == 0x0FF0:
                continue_loop = False
            elif opcode == 0x1021:
                # Host runtime special-cases BUILDING/MJSAND and keeps the raw
                # delay value instead of clamping it to 4. Without this, the
                # late BUILDING 1 continuation undercounts badly.
                delay = args[0] if self.is_building_mjsand_thread(thread) else max(args[0], 4)
                thread.timer = thread.delay = delay
            elif opcode == 0x1201:
                thread.next_goto_offset = ttm.find_tag(args[0])
                continue_loop = False
            elif opcode == 0x2022:
                delay = (args[0] + args[1]) // 2
                thread.delay = max(delay, 1)
                thread.timer = max(delay, 1)

            if offset >= size:
                thread.is_running = ADS_THREAD_TERMINATED
                continue_loop = False

        thread.ip = offset

    def active_threads(self) -> list[int]:
        return [
            idx
            for idx, thread in enumerate(self.threads)
            if thread.is_running in (ADS_THREAD_RUNNING, ADS_THREAD_TERMINATED)
        ]

    def snapshot_state(self) -> tuple:
        active = []
        for thread in self.threads:
            if thread.is_running in (ADS_THREAD_RUNNING, ADS_THREAD_TERMINATED):
                active.append(
                    (
                        thread.slot,
                        thread.tag,
                        thread.ip,
                        thread.delay,
                        thread.timer,
                        thread.scene_timer,
                        thread.scene_iterations,
                        thread.next_goto_offset,
                        thread.is_running,
                    )
                )
        active.sort()
        return (
            tuple(active),
            self.bg_timer,
            self.ads_stop_requested,
        )

    def simulate(self) -> dict:
        tag_offset, self.ads_chunks, self.ads_chunks_local = self.ads.load(self.ads_tag)
        self.ads_stop_requested = False
        self.play_chunk(tag_offset)

        while self.active_threads():
            state = self.snapshot_state()
            if state in self.seen_states:
                first_seen = self.seen_states[state]
                return {
                    "scene_index": self.scene_index,
                    "ads_name": self.ads_name,
                    "ads_tag": self.ads_tag,
                    "estimated_frames": first_seen,
                    "steady_state_cycle_frames": self.frames - first_seen,
                    "estimated_ticks": self.total_ticks,
                    "max_active_threads": self.max_active_threads,
                    "status": "steady_state_cycle",
                }
            self.seen_states[state] = self.frames

            self.frames += 1
            active = self.active_threads()
            self.max_active_threads = max(self.max_active_threads, len(active))
            if self.frames > MAX_SIM_FRAMES:
                return {
                    "scene_index": self.scene_index,
                    "ads_name": self.ads_name,
                    "ads_tag": self.ads_tag,
                    "estimated_frames": None,
                    "steady_state_cycle_frames": None,
                    "estimated_ticks": self.total_ticks,
                    "status": "frame_limit_exceeded",
                }

            if self.bg_timer is not None and self.bg_timer == 0:
                self.bg_timer = BG_DELAY

            for idx in active:
                thread = self.threads[idx]
                if thread.is_running == ADS_THREAD_RUNNING and thread.timer == 0:
                    thread.timer = thread.delay
                    self.ttm_play(thread)

            mini = 300
            if self.bg_timer is not None:
                mini = self.bg_timer

            for idx in self.active_threads():
                thread = self.threads[idx]
                if thread.is_running == ADS_THREAD_RUNNING:
                    mini = min(mini, thread.delay, thread.timer)

            if mini == 0:
                mini = 1

            self.total_ticks += mini

            if self.bg_timer is not None:
                self.bg_timer = max(0, self.bg_timer - mini)

            for idx in self.active_threads():
                thread = self.threads[idx]
                if thread.timer > mini:
                    thread.timer -= mini
                else:
                    thread.timer = 0

            for idx in self.active_threads():
                thread = self.threads[idx]
                if thread.is_running == ADS_THREAD_RUNNING and thread.timer == 0:
                    if thread.next_goto_offset:
                        thread.ip = thread.next_goto_offset
                        thread.next_goto_offset = 0
                    if thread.scene_timer > 0:
                        thread.scene_timer -= thread.delay
                        if thread.scene_timer <= 0:
                            thread.is_running = ADS_THREAD_TERMINATED

                if thread.is_running == ADS_THREAD_TERMINATED:
                    if thread.scene_iterations:
                        thread.scene_iterations -= 1
                        thread.is_running = ADS_THREAD_RUNNING
                        thread.ip = self.slot_to_ttm[thread.slot].find_tag(thread.tag)
                        thread.timer = 0
                    else:
                        ended_slot = thread.slot
                        ended_tag = thread.tag
                        self.stop_scene_index(idx)
                        if not self.ads_stop_requested:
                            self.play_triggered_chunks(ended_slot, ended_tag)

        return {
            "scene_index": self.scene_index,
            "ads_name": self.ads_name,
            "ads_tag": self.ads_tag,
            "estimated_frames": self.frames,
            "steady_state_cycle_frames": None,
            "estimated_ticks": self.total_ticks,
            "max_active_threads": self.max_active_threads,
            "status": "ok",
        }


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--scene-analysis", type=Path, default=DEFAULT_SCENE_ANALYSIS)
    ap.add_argument("--ads-dir", type=Path, default=DEFAULT_EXTRACTED_ADS)
    ap.add_argument("--ttm-dir", type=Path, default=DEFAULT_EXTRACTED_TTM)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--scene-index", type=int)
    ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--csv-output", type=Path, default=DEFAULT_CSV_OUTPUT)
    return ap


def main() -> int:
    args = build_parser().parse_args()
    analysis = json.loads(args.scene_analysis.read_text())
    scenes = analysis["scenes"]
    if args.scene_index is not None:
        scenes = [scene for scene in scenes if int(scene["scene_index"]) == args.scene_index]

    rows = []
    for scene in scenes:
        sim = SceneSimulator(scene, args.ads_dir, args.ttm_dir, args.seed)
        rows.append(sim.simulate())

    rows.sort(key=lambda row: row["scene_index"])
    payload = {
        "schema_version": 1,
        "artifact_kind": "scene_duration_estimates",
        "seed": args.seed,
        "scene_count": len(rows),
        "rows": rows,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n")
    args.csv_output.parent.mkdir(parents=True, exist_ok=True)
    with args.csv_output.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(
            fh,
            fieldnames=[
                "scene_index",
                "ads_name",
                "ads_tag",
                "boot_string",
                "estimated_frames",
                "estimated_ticks",
                "steady_state_cycle_frames",
                "max_active_threads",
                "status",
            ],
        )
        writer.writeheader()
        for row in rows:
            writer.writerow(
                {
                    **row,
                    "boot_string": f"window nosound story direct {row['scene_index']} seed {args.seed}",
                }
            )
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
