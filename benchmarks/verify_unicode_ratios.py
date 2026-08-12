#!/usr/bin/env python3
"""Consistency check for docs/BENCHMARKS.md's §Unicode tables: every displayed ratio must equal
engine_ns_per_byte / REAL_ns_per_byte (the document's stated convention, matching §A/§E — see the
note right after the bench_engines.cpp table). Exists because that section shipped with hand-typed
ratios that drifted from this rule (a copy-pasted cell, a sign flip on \\p{L}+ read as a "win" when
it was REAL's worst row) — this script makes a repeat of that class of error a caught regression
instead of a silent one. Dev-tooling; not part of the benchmark run itself, just its arithmetic.

  python benchmarks/verify_unicode_ratios.py [path/to/BENCHMARKS.md]
"""
import re
import sys

ROW_RE = re.compile(
    r"^\|\s*(?P<name>`[^`]+`(?:\s*\([^)]*\))?)\s*\|\s*(?P<real>[\d.]+)\s*\|\s*(?P<rest>.+?)\s*\|\s*"
    r"(?P<counts>[\d—/]+(?:\s*⚠)?)\s*\|\s*$"
)
CELL_RE = re.compile(r"(unsupported|[\d.]+)\s*(?:\(\*{0,2}([\d.]+)×\*{0,2}\))?")


def parse_table(lines, start_idx, ncols):
    """Parse a markdown table starting at the header row; returns a list of row dicts."""
    rows = []
    i = start_idx + 2  # skip header + separator
    while i < len(lines) and lines[i].startswith("|"):
        cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
        if len(cells) >= ncols:
            rows.append(cells)
        i += 1
    return rows


def duplicate_rows(names, where):
    """A repeated row LABEL, which cell arithmetic cannot see -- and the defect this table HAD: `\\w+`
    appeared twice (1.946 and 2.239 ns/B) after a row replacement, each copy consistent with its own
    pair, so the ratio check was satisfied by both while the section published one pattern at two
    different speeds."""
    seen, dups = set(), []
    for n in names:
        (dups.append(n) if n in seen else seen.add(n))
    return [f"{where}: row {n!r} appears more than once -- cell arithmetic cannot see a duplicate"
            for n in dict.fromkeys(dups)]


def check_bench_engines(lines):
    errors = []
    for i, line in enumerate(lines):
        if line.startswith("| case | REAL ns/B | std::regex |"):
            parsed = parse_table(lines, i, 5)
            errors += duplicate_rows([c[0] for c in parsed], "§Unicode engines")
            for cells in parsed:
                name, real_s, std_s, pcre2_s, re2_s = cells[0], cells[1], cells[2], cells[3], cells[4]
                real = float(real_s)
                for label, cell in (("std", std_s), ("pcre2", pcre2_s), ("re2", re2_s)):
                    if cell == "unsupported":
                        continue
                    m = CELL_RE.match(cell)
                    if not m or m.group(2) is None:
                        errors.append(f"{name} [{label}]: cannot parse cell {cell!r}")
                        continue
                    eng_ns = float(m.group(1))
                    shown_ratio = float(m.group(2))
                    expect_ratio = eng_ns / real
                    if abs(shown_ratio - expect_ratio) > 0.015:
                        errors.append(
                            f"{name} [{label}]: shown {shown_ratio:.2f}x != "
                            f"engine/REAL {expect_ratio:.2f}x (REAL={real}, {label}={eng_ns})"
                        )
            return errors
    errors.append("bench_engines.cpp table not found")
    return errors


def check_duel(lines):
    errors = []
    for i, line in enumerate(lines):
        if line.startswith("| case | REAL ns/B | rust ns/B | winner |"):
            parsed = parse_table(lines, i, 4)
            errors += duplicate_rows([c[0] for c in parsed], "§Unicode duel")
            for cells in parsed:
                name, real_s, rust_s, winner = cells[0], cells[1], cells[2], cells[3]
                real, rust = float(real_s), float(rust_s)
                ratio = rust / real
                m = re.search(r"([\d.]+)×", winner)
                if not m:
                    errors.append(f"{name}: cannot parse winner cell {winner!r}")
                    continue
                shown = float(m.group(1))
                real_faster = "REAL" in winner
                expect_shown = ratio if real_faster else 1.0 / ratio
                if abs(shown - expect_shown) > 0.05:
                    errors.append(
                        f"{name}: shown {winner!r} ({shown:.2f}x) != computed "
                        f"{'REAL' if ratio > 1 else 'rust'} {expect_shown:.2f}x (REAL={real}, rust={rust})"
                    )
                if (ratio > 1) != real_faster:
                    errors.append(f"{name}: winner label {winner!r} disagrees with raw ns/B (REAL={real}, rust={rust})")
            return errors
    errors.append("duel table not found")
    return errors


def unicode_section(lines):
    """Slice to the '## Unicode — comparative' section only -- §A and §E have same-shaped tables
    (a plain REAL/std/PCRE2/RE2 header, a REAL/rust/winner header) that would otherwise false-match."""
    start = next(i for i, line in enumerate(lines) if line.startswith("## Unicode"))
    end = next(i for i, line in enumerate(lines) if line.startswith("## Methodology"))
    return lines[start:end]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "docs/BENCHMARKS.md"
    with open(path, encoding="utf-8") as f:
        lines = f.readlines()
    lines = unicode_section([line.rstrip("\n") for line in lines])

    errors = check_bench_engines(lines) + check_duel(lines)
    if errors:
        print(f"FAIL: {len(errors)} ratio inconsistency(ies) in {path}")
        for e in errors:
            print(f"  - {e}")
        return 1
    print(f"OK: all §Unicode ratios in {path} match engine_ns/REAL_ns")
    return 0


if __name__ == "__main__":
    sys.exit(main())
