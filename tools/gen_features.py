#!/usr/bin/env python3
r"""Generate the Features-matrix CI probe from docs/site/data/features.yaml (single
source of truth for the {features} directive AND this probe -- doc-site P3a).

Every features.yaml row with a non-null `pattern` becomes one executable assertion,
emitted as a GENERATED + committed C++ fragment (tests/frontend/features_probe_generated.inc):

  - status: supported / extension   -> real::regex(pattern) must COMPILE (not throw).
  - status: excluded-by-design      -> real::regex(pattern) must THROW real::regex_error
                                        (the moat, proven executable -- not just documented).
  - status: planned, or pattern: null -> SKIPPED (a null pattern has an honest reason,
                                        recorded in features.yaml itself; no pattern is
                                        fabricated here to fill the gap).

Patterns are emitted as C++ raw strings (R"delim(...)delim", delimiter chosen per-pattern
to avoid colliding with the pattern's own text) so the yaml's already-unescaped pattern
text (e.g. yaml `"\\p{L}+"` -> Python str `\p{L}+`) reaches the compiler unchanged --
re-escaping it through a normal C++ string literal would risk testing the wrong pattern.

tests/frontend/test_features_probe.cpp (hand-written, stable) provides the includes and
`#include`s the generated fragment; this script owns only the fragment.

Usage:
  python3 tools/gen_features.py              # write tests/frontend/features_probe_generated.inc
  python3 tools/gen_features.py --stdout     # print the generated fragment to stdout
  python3 tools/gen_features.py --check      # exit 1 if the committed fragment is stale
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
FEATURES_YAML = ROOT / "docs" / "site" / "data" / "features.yaml"
OUT = ROOT / "tests" / "frontend" / "features_probe_generated.inc"

# FIGE: matches features.yaml's own schema comment and conf.py's _FEATURE_STATUS_LABELS.
COMPILE_STATUSES = {"supported", "extension"}
THROW_STATUSES = {"excluded-by-design"}
SKIP_STATUSES = {"planned"}
KNOWN_STATUSES = COMPILE_STATUSES | THROW_STATUSES | SKIP_STATUSES

# Candidate raw-string delimiters, tried in order; the first that cannot collide with a
# given pattern's own text is used for that pattern. Patterns here are short regex
# snippets, so any of these is virtually certain to be safe -- collision is still
# checked explicitly (never assumed) and this script errors out rather than emit an
# ambiguous raw string if every candidate somehow collides.
_RAW_DELIMS = ["RRX", "RGX", "PAT", "FRX", "QRX", "ZRX"]


def _slugify(text: str) -> str:
    """Turn a construct/category label into a valid, readable C++ identifier fragment."""
    text = text.replace("`", "")
    text = re.sub(r"[^A-Za-z0-9]+", "_", text)
    text = re.sub(r"_+", "_", text).strip("_").lower()
    return text or "row"


def _raw_string(pattern: str) -> str:
    """Render PATTERN as a C++ raw string literal, picking a delimiter that cannot
    collide with the pattern's own text (the closing sequence is `)delim"`)."""
    for delim in _RAW_DELIMS:
        if f"){delim}\"" not in pattern and delim not in pattern:
            return f'R"{delim}({pattern}){delim}"'
    raise SystemExit(f"gen_features: no safe raw-string delimiter for pattern {pattern!r}")


def generate() -> str:
    with FEATURES_YAML.open(encoding="utf-8") as fh:
        data = yaml.safe_load(fh)

    categories = data.get("categories") if isinstance(data, dict) else None
    if not categories:
        raise SystemExit("gen_features: features.yaml has no 'categories'")

    lines: list[str] = [
        "// GENERATED from docs/site/data/features.yaml — DO NOT EDIT — regenerate: python3 tools/gen_features.py",
        "//",
        "// One TEST() per features.yaml row with a non-null `pattern`: supported/extension rows",
        "// assert real::regex(pattern) compiles without throwing; excluded-by-design rows assert it",
        "// throws real::regex_error (the moat, proven executable). `planned` rows and rows with",
        "// `pattern: null` are skipped — see features.yaml's own header for why no pattern is",
        "// fabricated to fill those in. #include this fragment from test_features_probe.cpp only.",
    ]

    n_compile = 0
    n_throw = 0
    n_skip = 0
    seen_names: set[str] = set()

    for cat_idx, category in enumerate(categories):
        cat_name = category["name"]
        cat_slug = _slugify(cat_name)
        features = category.get("features") or []
        lines.append("")
        lines.append(f"// --- category {cat_idx}: {cat_name} ---")
        for feat_idx, feature in enumerate(features):
            construct = feature["construct"]
            status = feature["status"]
            pattern = feature.get("pattern")

            if status not in KNOWN_STATUSES:
                raise SystemExit(
                    f"gen_features: {cat_name!r} / {construct!r} has status {status!r}, "
                    f"not one of {sorted(KNOWN_STATUSES)}"
                )

            if status in SKIP_STATUSES or pattern is None:
                reason = "planned" if status in SKIP_STATUSES else "pattern: null"
                lines.append(f"// skipped ({reason}): {construct}")
                n_skip += 1
                continue

            base_name = f"features_probe_{cat_idx:02d}_{feat_idx:02d}_{cat_slug}_{_slugify(construct)}"
            name = base_name[:120]  # generous cap; readability over a hard identifier limit
            suffix = 1
            while name in seen_names:
                suffix += 1
                name = f"{base_name}_{suffix}"[:120]
            seen_names.add(name)

            raw = _raw_string(pattern)
            lines.append("")
            if status in COMPILE_STATUSES:
                lines.append(f"TEST({name})")
                lines.append("{")
                lines.append(f"  // {cat_name} / {construct} — status: {status} (must compile)")
                lines.append(f"  const real::regex re({raw});")
                lines.append(f"  EXPECT_EQ(re.pattern(), std::string_view({raw}));")
                lines.append("}")
                n_compile += 1
            else:  # THROW_STATUSES
                lines.append(f"TEST({name})")
                lines.append("{")
                lines.append(f"  // {cat_name} / {construct} — status: {status} (must throw real::regex_error)")
                lines.append(f"  EXPECT_THROWS(real::regex({raw}), real::regex_error);")
                lines.append("}")
                n_throw += 1

    lines.append("")
    lines.append(
        f"// {n_compile} compile-assertions, {n_throw} throw-assertions, {n_skip} skipped "
        f"(planned / pattern: null)."
    )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stdout", action="store_true", help="print the generated fragment to stdout")
    ap.add_argument("--check", action="store_true", help="fail if the committed fragment is stale")
    args = ap.parse_args()

    fragment = generate()

    if args.stdout:
        sys.stdout.write(fragment)
        return 0

    if args.check:
        if not OUT.is_file():
            print(f"gen_features: FAIL — missing {OUT}", file=sys.stderr)
            return 1
        existing = OUT.read_text(encoding="utf-8")
        if existing != fragment:
            print(
                "gen_features: FAIL — generated fragment stale vs docs/site/data/features.yaml\n"
                "  regenerate: python3 tools/gen_features.py",
                file=sys.stderr,
            )
            old_lines = existing.splitlines()
            new_lines = fragment.splitlines()
            for i, (a, b) in enumerate(zip(old_lines, new_lines)):
                if a != b:
                    print(f"  first mismatch at line {i + 1}:", file=sys.stderr)
                    print(f"    committed:  {a}", file=sys.stderr)
                    print(f"    regenerated: {b}", file=sys.stderr)
                    break
            else:
                if len(old_lines) != len(new_lines):
                    print(
                        f"  length committed={len(old_lines)} regenerated={len(new_lines)}",
                        file=sys.stderr,
                    )
            return 1
        print(f"gen_features: OK — {OUT.relative_to(ROOT)} matches features.yaml")
        return 0

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(fragment, encoding="utf-8")
    n_tests = fragment.count("\nTEST(")
    print(f"gen_features: wrote {OUT.relative_to(ROOT)} ({n_tests} assertions)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
