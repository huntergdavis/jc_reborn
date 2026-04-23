#!/usr/bin/env python3
import argparse
import json
from datetime import datetime
from pathlib import Path


def parse_date(value):
    if not value:
        return None
    return datetime.fromisoformat(value)


def load_successful_entries(index_path: Path):
    entries = json.loads(index_path.read_text(encoding="utf-8"))
    rows = []
    for entry in entries:
        build = entry.get("build") or {}
        commit = entry.get("commit") or {}
        seq = entry.get("sequence")
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
    rows.sort(key=lambda row: (row["date_obj"] or datetime.min, row["dir_name"] or ""))
    return rows


def build_epochs(rows):
    epochs = []
    current = None
    previous = None
    for row in rows:
        reset = previous is not None and row["sequence"] <= previous["sequence"]
        if current is None or reset:
            if current is not None:
                epochs.append(current)
            current = {
                "epoch_index": len(epochs) + 1,
                "start_sequence": row["sequence"],
                "end_sequence": row["sequence"],
                "entry_count": 0,
                "first_commit_date": row["commit_date"],
                "last_commit_date": row["commit_date"],
                "first_dir_name": row["dir_name"],
                "last_dir_name": row["dir_name"],
                "first_commit_short_hash": row["commit_short_hash"],
                "last_commit_short_hash": row["commit_short_hash"],
                "first_commit_message": row["commit_message"],
                "last_commit_message": row["commit_message"],
            }
        current["entry_count"] += 1
        current["end_sequence"] = max(current["end_sequence"], row["sequence"])
        current["last_commit_date"] = row["commit_date"]
        current["last_dir_name"] = row["dir_name"]
        current["last_commit_short_hash"] = row["commit_short_hash"]
        current["last_commit_message"] = row["commit_message"]
        previous = row
    if current is not None:
        epochs.append(current)
    return epochs


def main():
    parser = argparse.ArgumentParser(
        description="Report successful binary-library sequence epochs split by sequence resets."
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
        default="tmp-regtests/binary-library-epochs.json",
        help="Output JSON path when not using --stdout",
    )
    args = parser.parse_args()

    rows = load_successful_entries(Path(args.index))
    epochs = build_epochs(rows)
    report = {
        "index_path": str(Path(args.index).resolve()),
        "successful_entry_count": len(rows),
        "epoch_count": len(epochs),
        "epochs": epochs,
    }

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
