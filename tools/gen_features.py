#!/usr/bin/env python3
r"""Generate TWO artifacts from the single source docs/site/data/features.yaml (also
the data behind the {features} site directive):

1. tests/frontend/features_probe_generated.inc -- the executable-claims CI probe.
   Every features.yaml row with a non-null `pattern` becomes one assertion:

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

2. docs/COMPATIBILITY.md's "## Feature scorecard" table -- the GitHub-reader-
   facing status view, GENERATED and injected between two markers already present in the
   file (never invented by this script; the hand-written prose around them is untouched).
   Only categories NOT marked `scorecard: false` (see features.yaml's own schema comment)
   contribute rows, in features.yaml's own order. A row's `link:` (the site's MyST
   "PAGE#target" form) is rewritten to a Doxygen `([why|more](@ref target))` cross-ref --
   COMPATIBILITY.md feeds Doxygen (`INPUT = ... docs/`), not Sphinx, so the site's `:ref:`
   mechanism (conf.py's FeaturesDirective) does not apply here.

Usage:
  python3 tools/gen_features.py              # write both generated artifacts
  python3 tools/gen_features.py --stdout     # print the .inc fragment to stdout (probe only)
  python3 tools/gen_features.py --check      # exit 1 if EITHER committed artifact is stale
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
COMPATIBILITY_MD = ROOT / "docs" / "COMPATIBILITY.md"

# FIGE: matches features.yaml's own schema comment.
COMPILE_STATUSES = {"supported", "extension"}
THROW_STATUSES = {"excluded-by-design"}
SKIP_STATUSES = {"planned"}
KNOWN_STATUSES = COMPILE_STATUSES | THROW_STATUSES | SKIP_STATUSES

# The canonical status-slug -> human-label table, shared by generate_scorecard() and
# conf.py's {features} directive (conf.py imports this dict). This module must stay
# import-safe with only yaml/stdlib: conf.py is venv-only, the dependency direction is
# conf.py -> tools/, never the reverse. Values are the human labels ("excluded by
# design" is spaced, matching the scorecard prose).
STATUS_LABELS = {
    "supported": "supported",
    "extension": "extension",
    "excluded-by-design": "excluded by design",
    "planned": "planned",
}

# The scorecard table lives between these two exact marker lines in COMPATIBILITY.md
# (never created by this script -- a missing marker is a hard error, not a silent
# no-op). Link-text convention: "why" for an excluded-by-design row, "more" otherwise.
SCORECARD_BEGIN = (
    "<!-- BEGIN GENERATED features scorecard — DO NOT EDIT — "
    "regenerate: python3 tools/gen_features.py -->"
)
SCORECARD_END = "<!-- END GENERATED -->"

# features.yaml `link:` values are always "differences-from-re#div_X" (the site's MyST
# cross-ref target, see features.yaml's own schema comment) -- the only page this repo's
# scorecard rows have ever linked to. Stripping this fixed prefix and keeping "div_X" is
# what turns the link into a Doxygen `@ref div_X` (COMPATIBILITY.md's page, divergences.dox's
# anchor -- both Doxygen documents, not Sphinx/MyST).
_SCORECARD_LINK_PAGE_PREFIX = "differences-from-re#"

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


def generate_scorecard() -> str:
    """Render docs/COMPATIBILITY.md's "## Feature scorecard" TABLE ONLY (header + separator
    + rows -- no surrounding markers, no prose) from features.yaml. See this module's own
    docstring, item 2, for the category-filter and link-mapping rules.
    """
    with FEATURES_YAML.open(encoding="utf-8") as fh:
        data = yaml.safe_load(fh)

    categories = data.get("categories") if isinstance(data, dict) else None
    if not categories:
        raise SystemExit("gen_features: features.yaml has no 'categories'")

    lines: list[str] = [
        "| Feature | Status | Rationale | Since / target |",
        "| --- | --- | --- | --- |",
    ]

    for category in categories:
        # `scorecard: false` (default true, per-category): opts a category OUT of the
        # GENERATED COMPATIBILITY.md table while it still renders in full on the site's
        # {features} page. "Core" is the one category so far that sets this -- it is
        # already covered, in prose, by COMPATIBILITY.md's own "## What runs on real"
        # section (hand-written, untouched by this script); the scorecard's own intro
        # ("the lines where REAL says something notable against a full feature matrix")
        # would stop being true if every baseline "of course it's supported" Core row
        # joined it too.
        if not category.get("scorecard", True):
            continue
        cat_name = category["name"]
        for feature in category.get("features") or []:
            construct = feature["construct"]
            status = feature["status"]
            if status not in STATUS_LABELS:
                raise SystemExit(
                    f"gen_features: {cat_name!r} / {construct!r} has status {status!r}, "
                    f"not one of {sorted(STATUS_LABELS)}"
                )
            note = feature.get("note", "")
            link = feature.get("link")
            since = feature.get("since") or "—"

            rationale = note
            if link:
                _, sep, target = link.partition("#")
                if not link.startswith(_SCORECARD_LINK_PAGE_PREFIX) or not sep or not target:
                    raise SystemExit(
                        f"gen_features: {cat_name!r} / {construct!r} scorecard link {link!r} "
                        f"does not start with {_SCORECARD_LINK_PAGE_PREFIX!r} + '#target' -- "
                        "only differences-from-re#div_* links are mapped to a Doxygen @ref"
                    )
                # "why" for a closed door (excluded-by-design), "more" for everything else
                # that links out (an extension or a supported row with extra context) --
                # reproduces the two link texts the hand-written scorecard actually used.
                link_text = "why" if status == "excluded-by-design" else "more"
                rationale = f"{rationale} ([{link_text}](@ref {target}))"

            lines.append(f"| {construct} | **{STATUS_LABELS[status]}** | {rationale} | {since} |")

    lines.append("")
    return "\n".join(lines)


def _inject_scorecard(markdown: str, table: str) -> str:
    """Splice TABLE between COMPATIBILITY.md's two scorecard markers. The markers must
    already exist in MARKDOWN (a missing marker is a hard error -- this function never
    invents one); the hand-written prose outside them passes through untouched."""
    try:
        start = markdown.index(SCORECARD_BEGIN)
    except ValueError:
        raise SystemExit(
            f"gen_features: {COMPATIBILITY_MD.relative_to(ROOT)} is missing the marker "
            f"{SCORECARD_BEGIN!r}"
        )
    try:
        end = markdown.index(SCORECARD_END, start)
    except ValueError:
        raise SystemExit(
            f"gen_features: {COMPATIBILITY_MD.relative_to(ROOT)} is missing the marker "
            f"{SCORECARD_END!r} (after the BEGIN marker)"
        )
    end += len(SCORECARD_END)
    return markdown[:start] + SCORECARD_BEGIN + "\n\n" + table + "\n" + SCORECARD_END + markdown[end:]


def generate_compatibility_md() -> str:
    """Return docs/COMPATIBILITY.md's full text with the scorecard block regenerated from
    features.yaml (everything outside the two markers is the committed file, unchanged)."""
    if not COMPATIBILITY_MD.is_file():
        raise SystemExit(f"gen_features: missing {COMPATIBILITY_MD.relative_to(ROOT)}")
    markdown = COMPATIBILITY_MD.read_text(encoding="utf-8")
    return _inject_scorecard(markdown, generate_scorecard())


def _report_stale(path: Path, existing: str, generated: str) -> None:
    print(
        f"gen_features: FAIL — {path.relative_to(ROOT)} stale vs docs/site/data/features.yaml\n"
        "  regenerate: python3 tools/gen_features.py",
        file=sys.stderr,
    )
    old_lines = existing.splitlines()
    new_lines = generated.splitlines()
    for i, (a, b) in enumerate(zip(old_lines, new_lines)):
        if a != b:
            print(f"  first mismatch at line {i + 1}:", file=sys.stderr)
            print(f"    committed:   {a}", file=sys.stderr)
            print(f"    regenerated: {b}", file=sys.stderr)
            break
    else:
        if len(old_lines) != len(new_lines):
            print(
                f"  length committed={len(old_lines)} regenerated={len(new_lines)}",
                file=sys.stderr,
            )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stdout", action="store_true", help="print the .inc probe fragment to stdout (probe only)")
    ap.add_argument("--check", action="store_true", help="fail if EITHER committed artifact is stale")
    args = ap.parse_args()

    fragment = generate()

    if args.stdout:
        sys.stdout.write(fragment)
        return 0

    if args.check:
        ok = True

        if not OUT.is_file():
            print(f"gen_features: FAIL — missing {OUT}", file=sys.stderr)
            ok = False
        else:
            existing = OUT.read_text(encoding="utf-8")
            if existing != fragment:
                _report_stale(OUT, existing, fragment)
                ok = False
            else:
                print(f"gen_features: OK — {OUT.relative_to(ROOT)} matches features.yaml")

        if not COMPATIBILITY_MD.is_file():
            print(f"gen_features: FAIL — missing {COMPATIBILITY_MD}", file=sys.stderr)
            ok = False
        else:
            existing_md = COMPATIBILITY_MD.read_text(encoding="utf-8")
            generated_md = _inject_scorecard(existing_md, generate_scorecard())
            if existing_md != generated_md:
                _report_stale(COMPATIBILITY_MD, existing_md, generated_md)
                ok = False
            else:
                print(
                    f"gen_features: OK — {COMPATIBILITY_MD.relative_to(ROOT)} "
                    "scorecard matches features.yaml"
                )

        return 0 if ok else 1

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(fragment, encoding="utf-8")
    n_tests = fragment.count("\nTEST(")
    print(f"gen_features: wrote {OUT.relative_to(ROOT)} ({n_tests} assertions)")

    table = generate_scorecard()
    COMPATIBILITY_MD.write_text(_inject_scorecard(COMPATIBILITY_MD.read_text(encoding="utf-8"), table), encoding="utf-8")
    n_rows = max(len(table.strip("\n").splitlines()) - 2, 0)  # minus the header + separator lines
    print(f"gen_features: wrote scorecard into {COMPATIBILITY_MD.relative_to(ROOT)} ({n_rows} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
