#!/usr/bin/env python3
import argparse
import json
from collections import defaultdict
from datetime import datetime
from pathlib import Path


def parse_date(value):
    if not value:
        return None
    return datetime.fromisoformat(value)


def load_entries(index_path: Path):
    return json.loads(index_path.read_text(encoding="utf-8"))


def successful_entries(entries):
    rows = []
    for entry in entries:
        build = entry.get("build") or {}
        seq = entry.get("sequence")
        commit = entry.get("commit") or {}
        if build.get("status") != "success" or not isinstance(seq, int):
            continue
        rows.append(
            {
                "sequence": seq,
                "dir_name": entry.get("dir_name"),
                "commit_hash": commit.get("hash"),
                "commit_short_hash": commit.get("short_hash"),
                "commit_date": commit.get("date"),
                "commit_message": commit.get("message"),
                "date_obj": parse_date(commit.get("date")),
            }
        )
    return rows


def sequence_reuse(rows):
    by_seq = defaultdict(list)
    for row in rows:
        by_seq[row["sequence"]].append(row)
    reused = []
    for seq, entries in sorted(by_seq.items()):
        if len(entries) < 2:
            continue
        entries = sorted(entries, key=lambda row: (row["date_obj"] or datetime.min, row["dir_name"] or ""))
        reused.append(
            {
                "sequence": seq,
                "count": len(entries),
                "entries": [
                    {
                        "dir_name": row["dir_name"],
                        "commit_short_hash": row["commit_short_hash"],
                        "commit_date": row["commit_date"],
                        "commit_message": row["commit_message"],
                    }
                    for row in entries
                ],
            }
        )
    return reused


def sequence_resets(rows):
    ordered = sorted(rows, key=lambda row: (row["date_obj"] or datetime.min, row["dir_name"] or ""))
    resets = []
    previous = None
    for row in ordered:
        if previous is not None and row["sequence"] <= previous["sequence"]:
            resets.append(
                {
                    "previous": {
                        "sequence": previous["sequence"],
                        "dir_name": previous["dir_name"],
                        "commit_short_hash": previous["commit_short_hash"],
                        "commit_date": previous["commit_date"],
                    },
                    "current": {
                        "sequence": row["sequence"],
                        "dir_name": row["dir_name"],
                        "commit_short_hash": row["commit_short_hash"],
                        "commit_date": row["commit_date"],
                    },
                }
            )
        previous = row
    return resets


def main():
    parser = argparse.ArgumentParser(
        description="Report successful binary-library sequence reuse and resets."
    )
    parser.add_argument(
        "--index",
        default="binary-library/index.json",
        help="Path to binary-library index.json",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="Write JSON report to stdout instead of a file",
    )
    parser.add_argument(
        "--output",
        default="tmp-regtests/binary-library-sequence-resets.json",
        help="Output JSON path when not using --stdout",
    )
    args = parser.parse_args()

    rows = successful_entries(load_entries(Path(args.index)))
    report = {
        "index_path": str(Path(args.index).resolve()),
        "successful_entry_count": len(rows),
        "reused_sequence_count": 0,
        "reused_sequences": [],
        "reset_count": 0,
        "resets": [],
    }
    reused = sequence_reuse(rows)
    resets = sequence_resets(rows)
    report["reused_sequence_count"] = len(reused)
    report["reused_sequences"] = reused
    report["reset_count"] = len(resets)
    report["resets"] = resets

    payload = json.dumps(report, indent=2) + "\n"
    if args.stdout:
        print(payload, end="")
        return

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(payload, encoding="utf-8")
    print(output_path.resolve())


if __name__ == "__main__":
    main()
