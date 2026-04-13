#!/bin/bash
# Run the exact visible-window fishing fgpilot check for multiple fgpilot scenes
# and collect the per-scene summary results in one place.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

OUT_DIR="${1:-/tmp/fgpilot-fishing1-scene-matrix}"
SCENES="${SCENES:-testcard fishing1 fishing1raw adsfishing1}"

mkdir -p "$OUT_DIR"

for scene in $SCENES; do
    FGOVERLAY_SCENE="$scene" "$SCRIPT_DIR/regtest-fgpilot-fishing1-exact.sh" "$OUT_DIR/$scene" >/dev/null
done

python3 - <<'PY' "$OUT_DIR" $SCENES
import json
import sys
from pathlib import Path

out_dir = Path(sys.argv[1])
scenes = sys.argv[2:]
rows = []
for scene in scenes:
    summary_path = out_dir / scene / "summary.json"
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    rows.append({
        "scene": scene,
        "summary_path": str(summary_path),
        "current_hard_read": summary["current_hard_read"],
        "fgpilot_vs_overlay": summary["fgpilot_vs_overlay"],
    })

payload = {
    "scenes": rows,
}
print(json.dumps(payload, indent=2))
PY
