#!/usr/bin/env python3
"""Consistency check for docs/BENCHMARKS.md's §A and §E tables: every displayed ratio must equal
engine_ns_per_byte / REAL_ns_per_byte (>1 means REAL is faster — the document's stated convention
throughout). Twin of verify_unicode_ratios.py (same rule, same reason: §Unicode shipped with a
hand-typed ratio that drifted from this formula — a copy-pasted cell, a sign flip read as a "win"
that was REAL's worst row). §A has two 4-engine tables (x86-64, arm64); §E has two 2-engine tables
(arm64, x86-64) with a textual "winner" cell instead of a bare ratio.

**It also checks §A's PROSE against §A's cells, and that half exists because the tables were right
while the prose was wrong for three consecutive stamps.** Checking cell arithmetic alone let all of
this through: two std::regex bounds and two RE2 bounds that no row supported, a per-row list carrying
pre-v2026.8.11 ratios (`alternation` filed as a PCRE2 win the table had already crossed), a count of
"five rows" that ignored three rows the same train added, a "within 3 %" claim on a row the release
notes had published at 18 % behind, and a drift-witness list of seven values absent from the table. A
checker that reads only the tables reports OK on every one of them, which is how they survived.

  python benchmarks/verify_bench_ratios.py [path/to/BENCHMARKS.md]
"""
import re
import sys
from decimal import Decimal

CELL_RE = re.compile(r"(unsupported|~tie|[\d.]+)\s*(?:\(\*{0,2}([\d.]+)×\*{0,2}\))?")

# Prose claims in §A's reading bullets, each checked against the cells above them.
RANGE_RE = re.compile(r"\*{0,2}([\d.]+)–([\d.]+)×\*{0,2}\s+on\s+(x86-64|arm64)")
PAIR_RE = re.compile(
    r"`([^`]+)`\s*\(\*{0,2}([\d.]+)×\*{0,2}(?:\s*x86-64)?\s*/\s*\*{0,2}([\d.]+)×\*{0,2}(?:\s*arm64)?\)"
)
COUNT_RE = re.compile(r"REAL vs PCRE2-JIT: (\w+) of (\w+) rows are REAL's on BOTH ISAs")
WORDS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
}


def displayed_interval(text):
    """Half-open interval a displayed decimal stands for: '0.21' -> [0.205, 0.215), '0.205' ->
    [0.2045, 0.2055). The number of printed decimals IS the precision claim, so it is what bounds
    the raw value -- and for a cell like 0.21 one unit in the last place is 4.8 % of the value, far
    wider than any fixed relative tolerance would model."""
    d = Decimal(text)
    half = Decimal(1).scaleb(d.as_tuple().exponent) / 2
    return float(d - half), float(d + half)


def ratio_is_consistent(eng_s, real_s, shown_s):
    """A shown ratio is consistent when it can be produced by SOME pair of raw values the two
    displayed operands stand for. Dividing rounded operands does not commute with rounding the
    ratio of raw ones, so an equality check on the rounded values is simply the wrong test: it
    fires on correct rows (0.205 ns/B against 31.12 is one) and, being fixed-relative, stays blind
    on others. The interval test is exact -- it fails only when NO admissible raw pair yields the
    printed ratio."""
    e_lo, e_hi = displayed_interval(eng_s)
    r_lo, r_hi = displayed_interval(real_s)
    s_lo, s_hi = displayed_interval(shown_s)
    return s_hi > e_lo / r_hi and s_lo < e_hi / r_lo, (e_lo / r_hi, e_hi / r_lo)


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


def duplicate_rows(names, where):
    """A repeated row LABEL, which cell arithmetic cannot see. §Unicode once carried `\\w+` twice
    (1.946 and 2.239 ns/B, a leftover from replacing rows) and both copies were internally consistent,
    so every ratio check passed on a table that published one pattern under two different speeds."""
    seen, dups = set(), []
    for n in names:
        (dups.append(n) if n in seen else seen.add(n))
    return [f"{where}: row {n!r} appears more than once -- cell arithmetic cannot see a duplicate"
            for n in dict.fromkeys(dups)]


def normalise_case(name):
    """'words `[a-z]+`' and a prose '`words [a-z]+`' are the same row; backticks are formatting."""
    return " ".join(name.replace("`", " ").split())


def section_a_tables(lines):
    """§A: '| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |' — appears twice (x86-64, arm64).
    Each non-REAL cell is 'ns/B (ratio×)', e.g. '6.41 (**1.72×**)' — same shape as §Unicode's table.
    Returns (errors, checks, cells) where cells is {isa: {case: {engine: shown_ratio}}} for the prose
    pass: the bullets below the tables must be checkable against the same numbers, not re-typed."""
    errors, checks = [], 0
    table = {}
    for i, line in enumerate(lines):
        if not line.startswith("| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |"):
            continue
        isa = next(
            (
                "x86-64" if lines[k].startswith("**x86-64**") else "arm64"
                for k in range(i - 1, max(i - 6, -1), -1)
                if lines[k].startswith("**x86-64**") or lines[k].startswith("**arm64**")
            ),
            None,
        )
        if isa is None:
            errors.append(f"§A: table at line {i} has no '**x86-64**' / '**arm64**' heading above it")
            continue
        table[isa] = {}
        parsed = parse_table(lines, i, 5)
        errors += duplicate_rows([normalise_case(c[0]) for c in parsed], f"§A/{isa}")
        for cells in parsed:
            name, real_s = normalise_case(cells[0]), cells[1]
            table[isa][name] = {}
            for label, cell in zip(("std", "pcre2", "re2"), cells[2:5]):
                if cell == "unsupported":
                    continue
                m = CELL_RE.match(cell)
                if not m or m.group(2) is None:
                    errors.append(f"§A [{label}] {name}: cannot parse cell {cell!r}")
                    continue
                eng_s, shown_s = m.group(1), m.group(2)
                table[isa][name][label] = float(shown_s)
                checks += 1
                ok, (lo, hi) = ratio_is_consistent(eng_s, real_s, shown_s)
                if not ok:
                    errors.append(
                        f"§A/{isa} [{label}] {name}: shown {shown_s}x outside [{lo:.2f}, {hi:.2f}]x, "
                        f"the only ratios REAL={real_s} and {label}={eng_s} can produce"
                    )
    for isa in ("x86-64", "arm64"):
        if isa not in table:
            errors.append(f"§A: no '{isa}' 'REAL ns/B | std::regex | PCRE2-JIT | RE2' table found")
    return errors, checks, table


def lookup_case(table_isa, key):
    """Exact row name, else a unique prefix. An AMBIGUOUS prefix is an error rather than a first
    match: §A has three `words` rows, so a bullet citing a bare `words` cannot be checked at all --
    and one that cannot be checked is exactly what this pass exists to refuse."""
    if key in table_isa:
        return key, None
    hits = [n for n in table_isa if n.startswith(key)]
    if len(hits) == 1:
        return hits[0], None
    if not hits:
        return None, f"no §A row matches {key!r}"
    return None, f"{key!r} matches {len(hits)} §A rows ({', '.join(sorted(hits))}) -- qualify it"


def section_a_prose(lines, table):
    """§A's reading bullets, checked against §A's cells. Three claim shapes are recognised: a range
    ('8.20-41.97x on x86-64'), a per-row pair ('`digits` (**2.76x** / **1.64x**)') and the both-ISA
    win count. ZERO recognised claims is a FAILURE -- a pass that matched nothing reads identically
    to a pass that verified everything, and this file's whole reason for existing is that the second
    reading was assumed for three stamps while the first was true."""
    errors, checks = [], 0
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("**Reading"))
    except StopIteration:
        return ["§A: no '**Reading' block found -- prose claims unchecked"], 0

    bullets, cur = [], None
    for line in lines[start:]:
        if line.startswith("- "):
            if cur is not None:
                bullets.append(" ".join(cur))
            cur = [line[2:].strip()]
        elif cur is not None and line.strip():
            cur.append(line.strip())
        elif cur is not None:
            bullets.append(" ".join(cur))
            cur = None
    if cur is not None:
        bullets.append(" ".join(cur))

    for b in bullets:
        for lo_s, hi_s, isa in RANGE_RE.findall(b):
            names = {"std": "std::regex" in b, "re2": "RE2" in b}
            engines = [k for k, present in names.items() if present]
            if len(engines) != 1:
                errors.append(
                    f"§A prose: range {lo_s}-{hi_s}x on {isa} names "
                    f"{'both engines' if engines else 'no engine'} -- cannot be checked"
                )
                continue
            eng = engines[0]
            vals = [r[eng] for r in table.get(isa, {}).values() if eng in r]
            if not vals:
                errors.append(f"§A prose: range {lo_s}-{hi_s}x cites {isa}/{eng}, absent from the tables")
                continue
            checks += 1
            for claimed, actual, which in ((lo_s, min(vals), "low"), (hi_s, max(vals), "high")):
                c_lo, c_hi = displayed_interval(claimed)
                if not c_lo <= actual < c_hi:
                    errors.append(
                        f"§A prose [{eng}/{isa}]: {which} bound {claimed}x, table says {actual:.2f}x"
                    )
        for key, a_s, b_s in PAIR_RE.findall(b):
            key = normalise_case(key)
            resolved, err = lookup_case(table.get("x86-64", {}), key)
            if err:
                errors.append(f"§A prose: pair ({a_s}x / {b_s}x) -- {err}")
                continue
            checks += 1
            per_isa = (table["x86-64"].get(resolved, {}), table["arm64"].get(resolved, {}))
            shared = set(per_isa[0]) & set(per_isa[1])
            claimed = (displayed_interval(a_s), displayed_interval(b_s))
            if not any(
                claimed[0][0] <= per_isa[0][e] < claimed[0][1] and claimed[1][0] <= per_isa[1][e] < claimed[1][1]
                for e in shared
            ):
                reads = "; ".join(f"{e} {per_isa[0][e]:.2f}/{per_isa[1][e]:.2f}" for e in sorted(shared))
                errors.append(
                    f"§A prose [{resolved}]: pair ({a_s}x x86-64 / {b_s}x arm64) matches no engine -- "
                    f"the table reads {reads}"
                )
        for n_s, of_s in COUNT_RE.findall(b):
            checks += 1
            both = [
                n for n in table.get("x86-64", {})
                if "pcre2" in table["x86-64"][n] and "pcre2" in table.get("arm64", {}).get(n, {})
            ]
            wins = [n for n in both if table["x86-64"][n]["pcre2"] > 1 and table["arm64"][n]["pcre2"] > 1]
            for word, actual, which in ((n_s, len(wins), "win count"), (of_s, len(both), "row total")):
                if WORDS.get(word.lower(), word) != actual:
                    errors.append(f"§A prose: PCRE2 {which} reads {word!r}, table says {actual}")

    if checks == 0:
        errors.append("§A prose: no range, pair or count claim recognised -- the bullets went unchecked")
    return errors, checks


def section_e_tables(lines):
    """§E: '| case | REAL ns/B | rust ns/B | winner |' — appears twice (arm64, x86-64)."""
    errors = []
    found = 0
    for i, line in enumerate(lines):
        if not line.startswith("| case | REAL ns/B | rust ns/B | winner |"):
            continue
        found += 1
        parsed = parse_table(lines, i, 4)
        errors += duplicate_rows([normalise_case(c[0]) for c in parsed], f"§E table {found}")
        for cells in parsed:
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
    table_errors, table_checks, table = section_a_tables(a_lines)
    prose_errors, prose_checks = section_a_prose(a_lines, table)
    errors = table_errors + prose_errors + section_e_tables(e_lines)
    if errors:
        print(f"FAIL: {len(errors)} ratio inconsistency(ies) in {path}")
        for e in errors:
            print(f"  - {e}")
        return 1
    # The counts are printed, not just the verdict: this check reported OK for three stamps while
    # every prose claim in §A was stale, because it was reading none of them.
    print(
        f"OK: {path} -- {table_checks} §A cell ratios and {prose_checks} §A prose claims consistent, "
        f"§E winners agree with their ns/B"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
