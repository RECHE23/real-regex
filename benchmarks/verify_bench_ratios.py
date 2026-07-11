#!/usr/bin/env python3
"""Consistency check for docs/BENCHMARKS.md's §A and §E tables: every displayed ratio must equal
engine_ns_per_byte / REAL_ns_per_byte (>1 means REAL is faster — the document's stated convention
throughout). Twin of verify_unicode_ratios.py (same rule, same reason: §Unicode shipped with a
hand-typed ratio that drifted from this formula — a copy-pasted cell, a sign flip read as a "win"
that was REAL's worst row). §A has two 4-engine tables (x86-64, arm64); §E has two 2-engine tables
(arm64, x86-64) with a textual "winner" cell instead of a bare ratio. Dev-tooling; not part of the
benchmark run itself, just its arithmetic.

  python benchmarks/verify_bench_ratios.py [path/to/BENCHMARKS.md]
"""
import re
import sys

CELL_RE = re.compile(r"(unsupported|~tie|[\d.]+)\s*(?:\(\*{0,2}([\d.]+)×\*{0,2}\))?")


_ESCAPED_PIPE = "\x00PIPE\x00"  # placeholder so a literal `\|` inside a cell (e.g. `the\|fox\|dog`)
                                 # is not mistaken for a column delimiter


def parse_table(lines, start_idx, ncols):
    rows = []
    i = start_idx + 2  # skip header + separator
    while i < len(lines) and lines[i].startswith("|"):
        protected = lines[i].replace("\\|", _ESCAPED_PIPE)
        cells = [c.strip().replace(_ESCAPED_PIPE, "|") for c in protected.strip().strip("|").split("|")]
        if len(cells) >= ncols:
            rows.append(cells)
        i += 1
    return rows


def section_a_tables(lines):
    """§A: '| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |' — appears twice (x86-64, arm64).
    Each non-REAL cell is 'ns/B (ratio×)', e.g. '6.41 (**1.72×**)' — same shape as §Unicode's table."""
    errors = []
    found = 0
    for i, line in enumerate(lines):
        if not line.startswith("| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |"):
            continue
        found += 1
        for cells in parse_table(lines, i, 5):
            name, real_s = cells[0], cells[1]
            real = float(real_s)
            for label, cell in zip(("std", "pcre2", "re2"), cells[2:5]):
                if cell == "unsupported":
                    continue
                m = CELL_RE.match(cell)
                if not m or m.group(2) is None:
                    errors.append(f"§A [{label}] {name}: cannot parse cell {cell!r}")
                    continue
                eng_ns = float(m.group(1))
                shown = float(m.group(2))
                expect = eng_ns / real
                # Relative, not fixed: both operands are themselves rounded to 2 decimals for
                # display, and rounding each independently before dividing doesn't commute with
                # dividing at full precision then rounding the ratio -- a small gap here is that
                # chain, not a wrong ratio (worst observed on real data: 0.73% relative).
                if abs(shown - expect) > max(0.03, 0.02 * expect):
                    errors.append(
                        f"§A [{label}] {name}: shown {shown:.2f}x != engine/REAL {expect:.2f}x "
                        f"(REAL={real}, {label}={eng_ns})"
                    )
    if found == 0:
        errors.append("§A: no 'REAL ns/B | std::regex | PCRE2-JIT | RE2' table found")
    return errors


def section_e_tables(lines):
    """§E: '| case | REAL ns/B | rust ns/B | winner |' — appears twice (arm64, x86-64)."""
    errors = []
    found = 0
    for i, line in enumerate(lines):
        if not line.startswith("| case | REAL ns/B | rust ns/B | winner |"):
            continue
        found += 1
        for cells in parse_table(lines, i, 4):
            name, real_s, rust_s, winner = cells[0], cells[1], cells[2], cells[3]
            real, rust = float(real_s), float(rust_s)
            ratio = rust / real  # >1 => REAL faster
            m = re.search(r"([\d.]+)×", winner)
            if not m:
                errors.append(f"§E {name}: cannot parse winner cell {winner!r}")
                continue
            shown = float(m.group(1))
            expect_shown = ratio if ratio >= 1 else 1.0 / ratio
            # Tolerance is relative, not fixed: the table's ns/B cells are themselves rounded to 3
            # decimals, and rounding-then-dividing doesn't commute with dividing-then-rounding, so a
            # tiny last-digit gap here is an artifact of that chain, not a wrong ratio (verified: the
            # one case this fires on matches exactly once computed from the unrounded JSON sample).
            # 0.055 floor: the winner cell rounds to 1 decimal place, an inherent +/-0.05 step no
            # relative term can shrink below.
            if abs(shown - expect_shown) > max(0.055, 0.025 * expect_shown):
                errors.append(
                    f"§E {name}: shown {winner!r} ({shown:.2f}x) != computed "
                    f"{'REAL' if ratio >= 1 else 'rust'} {expect_shown:.2f}x (REAL={real}, rust={rust})"
                )
            label_says_real = "REAL" in winner or "tie" in winner.lower()
            if (ratio >= 1) != label_says_real:
                errors.append(f"§E {name}: winner label {winner!r} disagrees with raw ns/B (REAL={real}, rust={rust})")
    if found == 0:
        errors.append("§E: no 'REAL ns/B | rust ns/B | winner' table found")
    return errors


def bench_section(lines):
    """Slice to '## A.' .. '## Multi-pattern' and '## E.' .. '### E.1' -- keeps §Unicode/§B untouched
    and out of scope (verify_unicode_ratios.py already covers §Unicode)."""
    def idx(prefix):
        for i, line in enumerate(lines):
            if line.startswith(prefix):
                return i
        raise ValueError(f"section not found: {prefix!r}")

    a_start = idx("## A.")
    a_end = idx("## Multi-pattern")
    e_start = idx("## E.")
    e_end = idx("### E.1")
    return lines[a_start:a_end], lines[e_start:e_end]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "docs/BENCHMARKS.md"
    with open(path, encoding="utf-8") as f:
        lines = [line.rstrip("\n") for line in f.readlines()]

    a_lines, e_lines = bench_section(lines)
    errors = section_a_tables(a_lines) + section_e_tables(e_lines)
    if errors:
        print(f"FAIL: {len(errors)} ratio inconsistency(ies) in {path}")
        for e in errors:
            print(f"  - {e}")
        return 1
    print(f"OK: all §A/§E ratios in {path} match engine_ns/REAL_ns")
    return 0


if __name__ == "__main__":
    sys.exit(main())
