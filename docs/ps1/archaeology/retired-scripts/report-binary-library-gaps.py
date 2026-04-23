#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def load_entries(index_path: Path):
    return json.loads(index_path.read_text(encoding="utf-8"))


def successful_sequences(entries):
    seqs = {
        entry.get("sequence")
        for entry in entries
        if isinstance(entry.get("sequence"), int)
        and (entry.get("build") or {}).get("status") == "success"
    }
    return sorted(seqs)


def compute_gaps(seqs):
    gaps = []
    prev = None
    for seq in seqs:
        if prev is not None and seq > prev + 1:
            gaps.append(
                {
                    "start_sequence": prev + 1,
                    "end_sequence": seq - 1,
                    "missing_count": seq - prev - 1,
                    "previous_success_sequence": prev,
                    "next_success_sequence": seq,
                }
            )
        prev = seq
    return gaps


def main():
    parser = argparse.ArgumentParser(
        description="Report gaps between successful binary-library sequences."
    )
    parser.add_argument(
        "--index",
        default="binary-library/index.json",
        help="Path to binary-library index.json",
    )
    parser.add_argument(
        "--min-gap-size",
        type=int,
        default=1,
        help="Only emit gaps with at least this many missing sequences",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="Write JSON report to stdout instead of a file",
    )
    parser.add_argument(
        "--output",
        default="tmp-regtests/binary-library-gaps.json",
        help="Output JSON path when not using --stdout",
    )
    args = parser.parse_args()

    entries = load_entries(Path(args.index))
    seqs = successful_sequences(entries)
    gaps = [gap for gap in compute_gaps(seqs) if gap["missing_count"] >= args.min_gap_size]

    report = {
        "index_path": str(Path(args.index).resolve()),
        "successful_sequence_count": len(seqs),
        "first_success_sequence": seqs[0] if seqs else None,
        "last_success_sequence": seqs[-1] if seqs else None,
        "gap_count": len(gaps),
        "gaps": gaps,
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
