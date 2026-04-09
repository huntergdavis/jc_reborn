#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_ROOT="$ROOT"
REFDIR="$PROJECT_ROOT/regtest-references"
OUTROOT_DEFAULT="$PROJECT_ROOT/vision-artifacts/vision-reference-pipeline-$(date +%Y%m%d-%H%M%S)"
OUTROOT="${1:-$OUTROOT_DEFAULT}"
BANKDIR="$OUTROOT/reference-bank"
SELFCHECKDIR="$OUTROOT/reference-selfcheck"

mkdir -p "$OUTROOT"

echo "Building reference bank into $BANKDIR"
python3 "$ROOT/scripts/vision_classifier.py" \
  build-reference-bank \
  --refdir "$REFDIR" \
  --outdir "$BANKDIR"

echo "Running reference self-check into $SELFCHECKDIR"
python3 "$ROOT/scripts/vision_classifier.py" \
  analyze-reference-set \
  --refdir "$REFDIR" \
  --bank-dir "$BANKDIR" \
  --outdir "$SELFCHECKDIR"

python3 "$ROOT/scripts/publish-vision-pipeline.py" \
  --bankdir "$BANKDIR" \
  --selfcheckdir "$SELFCHECKDIR" \
  --outroot "$OUTROOT" >/dev/null

echo "Pipeline complete:"
echo "  $OUTROOT/index.html"
