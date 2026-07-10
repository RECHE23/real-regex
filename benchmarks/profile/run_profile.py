#!/usr/bin/env python3
"""P0 profile harness: 2-pass (clean timing + instrumented attribution) → JSONL + markdown grid.

Protocol (non-negotiable):
  * ns/B only from the CLEAN binary (profile OFF)
  * routes only from the INSTRUMENTED binary (-DREAL_PROFILE)
  * joined by (pattern, corpus_tag, surface, force)
"""
from __future__ import annotations

import json
import os
import platform
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BUILD = ROOT / "build" / "profile"
CLEAN = BUILD / "profile_runner_clean"
INST = BUILD / "profile_runner_inst"
OUT_JSONL = BUILD / "run.jsonl"
OUT_MD = BUILD / "grid.md"

N_REP = 8000  # corpus repetitions (smaller than duel 20k — profile sample, not §E stamp)

# --- corpora (reuse duel/mp shapes; not a second world) ---
def corpus_prose_ascii() -> str:
    return "the quick brown fox jumps over the lazy dog " * N_REP


def corpus_log_dense() -> str:
    return "2026-07-09T12:00:00 INFO user=alice action=login id=42 host=web\n" * N_REP


def corpus_sparse() -> str:
    filler = "plain english words with no special token on this line at all " * 15
    hit = "then dog appears once "
    return (filler + hit) * (N_REP // 15)


def corpus_utf8_mixed() -> str:
    return "café résumé naïve 😀 ok_9 john@x.io café " * N_REP


def corpus_ident_dense() -> str:
    # §E MIXED shape: every token is id_42-style — IL memmem on `_` does not skip filler.
    return "id_42 name_foo val_7 key_bar ok_9 " * N_REP


CORPORA = {
    "prose_ascii": corpus_prose_ascii,
    "log_dense": corpus_log_dense,
    "sparse_generic": corpus_sparse,
    "utf8_mixed": corpus_utf8_mixed,
    "ident_dense": corpus_ident_dense,
}

# (label, pattern, intended_shape, intended_axes, default_corpus, surfaces, forces)
# forces: none always; class_off / ldfa_off where seam audit is informative
CELLS = [
    ("w_plus", r"\w+", "cp_class", {"wb": "none", "captures": 0, "unicode": True},
     "prose_ascii", ["count"], ["none", "class_off"]),
    ("az_plus", r"[a-z]+", "class_plus", {"wb": "none", "captures": 0, "unicode": False},
     "prose_ascii", ["count"], ["none", "class_off"]),
    ("digits", r"[0-9]+", "class_plus", {"wb": "none", "captures": 0, "unicode": False},
     "log_dense", ["count"], ["none"]),
    ("bw_word", r"\b\w+\b", "wb_wrap", {"wb": "exact", "captures": 0, "unicode": True},
     "prose_ascii", ["count"], ["none"]),
    ("baz", r"\b[a-z]+\b", "wb_wrap", {"wb": "subset", "captures": 0, "unicode": False},
     "prose_ascii", ["count"], ["none"]),
    ("ident_cap", r"(\w+)_(\w+)", "capture2", {"wb": "none", "captures": 2, "unicode": True},
     "ident_dense", ["count", "find"], ["none", "ldfa_off", "il_off"]),
    ("ident_ncap", r"(?:\w+)_(?:\w+)", "capture0_ident", {"wb": "none", "captures": 0, "unicode": True},
     "ident_dense", ["count"], ["none", "ldfa_off", "il_off"]),
    ("ident_cap_log", r"(\w+)_(\w+)", "capture2", {"wb": "none", "captures": 2, "unicode": True},
     "log_dense", ["count"], ["none"]),  # sparse `_` → IL wins (contrast with ident_dense)
    ("email", r"(\w+)@(\w+)", "capture2", {"wb": "none", "captures": 2, "unicode": True},
     "prose_ascii", ["count", "find"], ["none", "ldfa_off"]),
    ("alt", r"dog|fox|cat", "alt", {"wb": "none", "captures": 0, "unicode": False},
     "sparse_generic", ["count"], ["none"]),
    ("lit_dog", r"dog", "literal", {"wb": "none", "captures": 0, "unicode": False},
     "sparse_generic", ["count"], ["none"]),
    ("date_fixed", r"\d{4}-\d{2}-\d{2}", "fixed_shape", {"wb": "none", "captures": 0, "unicode": True},
     "log_dense", ["count"], ["none"]),
    ("superset_emoji", "\\b[\\w\U0001f600]+\\b", "superset",
     {"wb": "superset", "captures": 0, "unicode": True},
     "utf8_mixed", ["count"], ["none"]),
]


def git_commit() -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"], text=True
        ).strip()
    except Exception:
        return "unknown"


def run_bin(binary: Path, mode: str, pattern: str, text_path: str, surface: str, force: str) -> dict:
    out = subprocess.check_output(
        [str(binary), mode, pattern, text_path, surface, force], text=True
    )
    return json.loads(out)


def dominant_route(routes: dict) -> str:
    if not routes:
        return "none"
    return max(routes.items(), key=lambda kv: kv[1])[0]


def main() -> int:
    BUILD.mkdir(parents=True, exist_ok=True)
    if not CLEAN.is_file() or not INST.is_file():
        print("missing binaries — run: make profile-sample-build", file=sys.stderr)
        return 2

    meta_base = {
        "host": platform.node(),
        "isa": platform.machine(),
        "commit": git_commit(),
        "date": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "engine": {"name": "real", "version": None},
        "schema_version": 1,
    }

    rows = []
    with tempfile.TemporaryDirectory() as td:
        for label, pat, shape, axes, corp_tag, surfaces, forces in CELLS:
            text = CORPORA[corp_tag]()
            tpath = os.path.join(td, f"{label}.txt")
            with open(tpath, "w", encoding="utf-8") as f:
                f.write(text)
            density = None  # optional; skip for P0
            for surface in surfaces:
                for force in forces:
                    key = f"{label}/{corp_tag}/{surface}/{force}"
                    print(f"… {key}", flush=True)
                    try:
                        t = run_bin(CLEAN, "time", pat, tpath, surface, force)
                        a = run_bin(INST, "attr", pat, tpath, surface, force)
                    except subprocess.CalledProcessError as e:
                        print(f"FAIL {key}: {e}", file=sys.stderr)
                        continue
                    if t.get("instrumented"):
                        print(f"PROTOCOL VIOLATION: clean binary is instrumented", file=sys.stderr)
                        return 3
                    if not a.get("instrumented"):
                        print(f"PROTOCOL VIOLATION: attr binary not instrumented", file=sys.stderr)
                        return 3

                    timing = t.get("timing") or {}
                    routes = a.get("routes") or {}
                    rec = t.get("recognized") or a.get("recognized") or {}
                    predicted = rec.get("route_predicted", "")
                    dom = dominant_route(routes)
                    # recognition gap: intended shape vs predicted when we know they should match
                    gap = False
                    if shape == "cp_class" and predicted != "cp_class_loop":
                        gap = True
                    if shape == "class_plus" and predicted != "class_loop":
                        gap = True
                    if shape == "literal" and predicted != "exact_literal":
                        gap = True
                    if shape == "superset" and predicted == "cp_class_loop":
                        gap = True  # must NOT recognize as full \w

                    rec_hints = rec.get("hints") or []
                    if axes.get("wb") == "exact" and "greedy_cp" in rec_hints and "wb_wrap" not in rec_hints:
                        # B-1 drop is intended recognition
                        pass

                    row = {
                        "schema_version": 1,
                        "meta": {
                            **meta_base,
                            "engine": {
                                "name": "real",
                                "version": t.get("engine_version") or a.get("engine_version"),
                            },
                            "build": {
                                "flags": "-O3",
                                "instrumented": False,  # timing half
                            },
                        },
                        "label": label,
                        "pattern": pat,
                        "flags": "",
                        "intended": {"shape": shape, "axes": axes},
                        "recognized": rec,
                        "recognition_gap": gap,
                        "corpus": {
                            "tag": corp_tag,
                            "bytes": len(text.encode("utf-8")),
                            "density_measured": density,
                        },
                        "surface": "count_matches" if surface == "count" else "find_iter_span",
                        "timing": {
                            "ns_per_b_p50": timing.get("ns_per_b_p50"),
                            "p95": timing.get("p95"),
                            "n": timing.get("n"),
                        },
                        "routes": routes,
                        "events": a.get("events") or {},
                        "route_dominant": dom,
                        "matches": t.get("matches"),
                        "forced": {"route": force, "ns_per_b_p50": timing.get("ns_per_b_p50")},
                    }
                    rows.append(row)

    # Seam audit: mark dispatch-dominated cells (forced faster by ≥20%)
    by_base = {}
    for r in rows:
        k = (r["label"], r["corpus"]["tag"], r["surface"])
        by_base.setdefault(k, {})[r["forced"]["route"]] = r

    for k, fmap in by_base.items():
        base = fmap.get("none")
        if not base or base["timing"]["ns_per_b_p50"] is None:
            continue
        base_ns = base["timing"]["ns_per_b_p50"]
        for force, r in fmap.items():
            if force == "none":
                r["dispatch_dominated"] = False
                r["dominated_by"] = None
                continue
            fns = r["timing"]["ns_per_b_p50"]
            if fns is None or base_ns <= 0:
                continue
            # forced wins if lower ns/B by ≥20%
            if fns < base_ns * 0.80:
                base["dispatch_dominated"] = True
                base["dominated_by"] = force
                base["dominate_ratio"] = base_ns / fns
            r["dispatch_dominated"] = False

    with OUT_JSONL.open("w", encoding="utf-8") as f:
        for r in rows:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")

    # Markdown grid (default force=none rows first)
    lines = [
        f"# P0 profile grid — {meta_base['date']} · {meta_base['commit']} · {meta_base['isa']}",
        "",
        "Timing = clean build; routes = `-DREAL_PROFILE` build. ns/B never from instrumented.",
        "",
        "| cell | ns/B p50 | route dom | predicted | gap? | dominated? | force |",
        "| --- | ---: | --- | --- | --- | --- | --- |",
    ]
    for r in rows:
        t = r["timing"]["ns_per_b_p50"]
        t_s = f"{t:.3f}" if t is not None else "?"
        dom = r.get("dispatch_dominated")
        dom_s = (
            f"YES by {r.get('dominated_by')} ({r.get('dominate_ratio', 0):.2f}×)"
            if dom
            else "no"
        )
        lines.append(
            f"| {r['label']}/{r['corpus']['tag']}/{r['surface']}/{r['forced']['route']} "
            f"| {t_s} | `{r.get('route_dominant')}` | `{r['recognized'].get('route_predicted')}` "
            f"| {'GAP' if r.get('recognition_gap') else ''} | {dom_s} | {r['forced']['route']} |"
        )

    # Opportunities stub section (filled by human/verdict from data)
    lines += [
        "",
        "## Recognition gaps",
    ]
    gaps = [r for r in rows if r.get("recognition_gap")]
    if not gaps:
        lines.append("_none on default cells_")
    else:
        for r in gaps:
            lines.append(f"- `{r['label']}` intended={r['intended']['shape']} predicted={r['recognized'].get('route_predicted')}")

    lines += [
        "",
        "## Dispatch-dominated (forced ≥20% faster)",
    ]
    doms = [r for r in rows if r.get("dispatch_dominated")]
    if not doms:
        lines.append("_none ≥20%_")
    else:
        for r in doms:
            lines.append(
                f"- `{r['label']}` default {r['timing']['ns_per_b_p50']:.3f} → "
                f"{r.get('dominated_by')} wins {r.get('dominate_ratio', 0):.2f}×"
            )

    OUT_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT_JSONL} ({len(rows)} rows)")
    print(f"wrote {OUT_MD}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
