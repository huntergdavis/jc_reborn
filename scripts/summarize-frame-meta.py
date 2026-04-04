#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


EXACT_EXCLUDES = {
    "BACKGRND.BMP",
    "MRAFT.BMP",
    "MJRAFT.BMP",
    "MJRAFT2.BMP",
    "MJSAND.BMP",
    "MJSANDC.BMP",
    "SPLASH.BMP",
    "LILFISH.BMP",
    "COCONUTS.BMP",
    "COCOHEAD.BMP",
}

PREFIX_EXCLUDES = (
    "BACKGRND",
    "MJSAND",
    "MRAFT",
    "MJRAFT",
)

ACTOR_PREFIXES = (
    "MJ",
    "SJ",
    "SM",
    "GJ",
    "JOHN",
    "MARY",
    "SUZY",
)

JOHNNY_EXACT = {
    "JOHNWALK.BMP",
    "JOHNWOUL.BMP",
    "MJ_AMB.BMP",
    "MJBATH.BMP",
    "MJDIVE.BMP",
    "MJFISH1.BMP",
    "MJFISH2.BMP",
    "MJFISH3.BMP",
    "MJJOG1.BMP",
    "MJJOG2.BMP",
    "MJREAD.BMP",
    "MJTELE.BMP",
    "MJTELE2.BMP",
    "GJANGRY.BMP",
    "GJBIPLAN.BMP",
    "GJCATCH1.BMP",
    "GJCATCH2.BMP",
    "GJCATCH3.BMP",
    "GJDIVE.BMP",
    "GJFFFOOD.BMP",
    "GJHOT.BMP",
    "GJKINGKO.BMP",
    "GJPROW.BMP",
    "GJRUNAWA.BMP",
}

MARY_EXACT = {
    "SASKDATE.BMP",
    "SBREAKUP.BMP",
    "SJBRAKUP.BMP",
    "SMDATE1.BMP",
    "SMDATE2.BMP",
    "SMDATE3.BMP",
    "SMDATE4.BMP",
    "SMDATE5.BMP",
    "SMDATE6.BMP",
    "SMDATE7.BMP",
    "SMDATE8.BMP",
    "SMDATE9.BMP",
    "SMDATE10.BMP",
    "SMDATE11.BMP",
    "SMDATE12.BMP",
    "SMGIFT.BMP",
    "SMGLIMSE.BMP",
    "SMGFTWAV.BMP",
    "SLEVEJM1.BMP",
    "SLEVEJM2.BMP",
    "SLEVEJM3.BMP",
}

SUZY_EXACT = {
    "SJMSUZY1.BMP",
    "SJMSUZY2.BMP",
    "SJMSUZY3.BMP",
    "SSUZY1.BMP",
    "SSUZY2.BMP",
    "SSUZY3.BMP",
}

OTHER_ACTOR_EXACT = {
    "FISHMAN.BMP",
}

AMBIGUOUS_ACTORISH = {
    "MJCOCO.BMP",
    "MJCOCO1.BMP",
    "MJBOTTLE.BMP",
    "SJGFTASK.BMP",
    "SJGFTJMP.BMP",
    "SJGFTSHY.BMP",
    "SJGFTXCH.BMP",
    "SJWORK.BMP",
}

NON_ACTOR_PREFIXES = (
    "GJGULL",
    "GJVIS",
    "GJNAT",
    "BACKGRND",
    "MJSAND",
    "MRAFT",
    "MJRAFT",
    "SHARK",
    "COCO",
    "LITE",
    "TRUNK",
    "SRAFT",
)


def is_actor_candidate(name: str) -> bool:
    upper = (name or "").upper()
    if not upper or upper in EXACT_EXCLUDES or upper in AMBIGUOUS_ACTORISH:
        return False
    if any(upper.startswith(prefix) for prefix in PREFIX_EXCLUDES):
        return False
    if any(upper.startswith(prefix) for prefix in NON_ACTOR_PREFIXES):
        return False
    return upper in JOHNNY_EXACT or upper in MARY_EXACT or upper in SUZY_EXACT or upper in OTHER_ACTOR_EXACT


def classify_entity(name: str) -> str:
    upper = (name or "").upper()
    if not upper:
        return "unknown"
    if upper in EXACT_EXCLUDES or any(upper.startswith(prefix) for prefix in NON_ACTOR_PREFIXES):
        return "background_or_prop"
    if upper in AMBIGUOUS_ACTORISH:
        return "ambiguous"
    if upper in SUZY_EXACT:
        return "suzy"
    if upper in MARY_EXACT:
        return "mary"
    if upper in JOHNNY_EXACT:
        return "johnny"
    if upper in OTHER_ACTOR_EXACT:
        return "other_actor"
    return "unknown"


def dedupe(draws: list[dict]) -> list[dict]:
    grouped: dict[tuple, dict] = {}
    order: list[tuple] = []
    for draw in draws:
        key = (
            draw.get("surface_role"),
            draw.get("bmp_name"),
            draw.get("image_no"),
            draw.get("sprite_no"),
            draw.get("x"),
            draw.get("y"),
            draw.get("width"),
            draw.get("height"),
            bool(draw.get("flipped")),
        )
        if key not in grouped:
            entry = dict(draw)
            entry["occurrences"] = 1
            grouped[key] = entry
            order.append(key)
        else:
            grouped[key]["occurrences"] += 1
    return [grouped[key] for key in order]


def summarize(path: Path) -> dict:
    obj = json.loads(path.read_text(encoding="utf-8"))
    visible = obj.get("visible_draws") or obj.get("draws") or []
    unique = dedupe(visible)
    for row in unique:
        row["entity"] = classify_entity(row.get("bmp_name", ""))
        row["actor_candidate"] = is_actor_candidate(row.get("bmp_name", ""))
    actor_candidates = [row for row in unique if row["actor_candidate"]]
    actor_summary: dict[str, int] = {}
    for row in actor_candidates:
        entity = row["entity"]
        actor_summary[entity] = actor_summary.get(entity, 0) + 1
    return {
        "frame_number": obj.get("frame_number"),
        "scene_label": obj.get("scene_label"),
        "draw_count": obj.get("draw_count"),
        "visible_draw_count": obj.get("visible_draw_count", len(visible)),
        "visible_unique_draw_count": len(unique),
        "actor_candidate_draw_count": len(actor_candidates),
        "actor_summary": actor_summary,
        "actor_candidates": actor_candidates,
        "visible_unique_draws": unique,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize host frame metadata into deduped visible draws and actor candidates")
    parser.add_argument("meta_json", help="Path to frame metadata JSON")
    parser.add_argument("--out", help="Optional output JSON path")
    args = parser.parse_args()

    summary = summarize(Path(args.meta_json))
    payload = json.dumps(summary, indent=2) + "\n"
    if args.out:
        Path(args.out).write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
