#!/usr/bin/env python3
"""Generate include/real/unicode_props.hpp: the Unicode \\w \\d \\s property ranges.

Source of truth: CPython's own `re`, so REAL's Unicode-shorthand semantics are identical to
`re`'s by construction (same relation) -- not derived from UCD files and hoped to agree. For each
code point in [0, 0x10FFFF] minus the surrogate block we ask `re.fullmatch(r'\\w'/'\\d'/'\\s', chr(cp))`
and coalesce the matches into sorted inclusive ranges.

Two safety nets, like scripts/gen_unicode_fold.py:
  1. the construction IS the oracle;
  2. a full second pass re-checks every code point: `in_ranges(cp) == bool(re.fullmatch(...))`,
     and the script ABORTS on any mismatch (a range-building bug can't slip through);
  3. the total code-point counts are asserted against the known Unicode-16 values.

Deterministic per CPython Unicode version; regenerate after a data bump (see scripts/REGEN.md). Only
the public `re` and `unicodedata` are used (no private internals), so this is robust across versions.
"""
import re
import sys
import unicodedata

import _gen_common as common

# The property matchers (str patterns default to re.UNICODE).
_PATTERNS = {"word": re.compile(r"\w"), "digit": re.compile(r"\d"), "space": re.compile(r"\s")}

# Known Unicode-16.0.0 totals; the version pin catches a bump before these would.
_EXPECTED = {"word": (142940, 771), "digit": (760, 71), "space": (29, 10)}

_SURROGATE_LO = 0xD800
_SURROGATE_HI = 0xDFFF
_MAX_CP = 0x10FFFF


def _matches(pat, cp):
    return pat.fullmatch(chr(cp)) is not None


def build_ranges(pat):
    """Coalesce the code points matching \\p{pat} into sorted inclusive [lo, hi] ranges."""
    ranges = []
    cp = 0
    while cp <= _MAX_CP:
        if _SURROGATE_LO <= cp <= _SURROGATE_HI:
            cp = _SURROGATE_HI + 1
            continue
        if _matches(pat, cp):
            lo = cp
            while cp <= _MAX_CP and not (_SURROGATE_LO <= cp <= _SURROGATE_HI) and _matches(pat, cp):
                cp += 1
            ranges.append((lo, cp - 1))
        else:
            cp += 1
    return ranges


def in_ranges(ranges, cp):
    lo, hi = 0, len(ranges)
    while lo < hi:
        mid = (lo + hi) // 2
        if ranges[mid][1] < cp:
            lo = mid + 1
        else:
            hi = mid
    return lo < len(ranges) and ranges[lo][0] <= cp <= ranges[lo][1]


def _non_surrogate_cps():
    """All code points in [0, _MAX_CP] except the surrogate block."""
    for cp in range(0, _MAX_CP + 1):
        if not (_SURROGATE_LO <= cp <= _SURROGATE_HI):
            yield cp


def validate(name, ranges, pat):
    """Second pass: every code point's range membership must equal re.fullmatch. Abort on mismatch."""
    total = sum(hi - lo + 1 for lo, hi in ranges)
    exp_cp, exp_ranges = _EXPECTED[name]
    if (total, len(ranges)) != (exp_cp, exp_ranges):
        sys.exit(f"ABORT {name}: got {total} cp / {len(ranges)} ranges, expected {exp_cp} / {exp_ranges} "
                 f"(Unicode {unicodedata.unidata_version}; update _EXPECTED if a data bump is intended)")
    common.validate_exhaustive(
        _non_surrogate_cps(),
        lambda cp: f"{name} U+{cp:04X}" if in_ranges(ranges, cp) != _matches(pat, cp) else None,
        lambda n: f"ABORT {name}: {n} code point(s) disagree with re")


def emit_array(name, ranges):
    lines = [f"  //! \\brief Code-point ranges matched by `\\{name[0]}` ({len(ranges)} ranges, "
             f"{sum(hi - lo + 1 for lo, hi in ranges)} code points).",
             f"  inline constexpr code_range {name}_ranges[] {{"]
    for lo, hi in ranges:
        lines.append(f"    {{0x{lo:04X}, 0x{hi:04X}}},")
    lines.append("  };")
    lines.append(f"  inline constexpr std::size_t {name}_ranges_size {{{len(ranges)}}};")
    lines.append("")
    return lines


def emit(tables, path):
    ver = unicodedata.unidata_version
    out = common.file_header(
        filename="unicode_props.hpp",
        brief="Unicode `\\w` / `\\d` / `\\s` property ranges and their lookups.",
        generator="gen_unicode_props.py",
        doc_lines=[
            "Built from, and exhaustively validated against, CPython's own `re`, so REAL's Unicode",
            f"shorthand semantics equal re by construction. Unicode data version: {ver} (asserted).",
        ],
        guard="REAL_UNICODE_PROPS_HPP",
        includes=[
            "#include <cstddef>",
            "#include <cstdint>",
            "#include <span>",
            "",
            '#include "program.hpp" // code_range',
        ],
        version_kind="tables",
        version_const="unicode_props_unidata_version")
    for name in ("word", "digit", "space"):
        out += emit_array(name, tables[name])
    out += [
        "  //! \\brief Binary-searches a sorted, non-overlapping range table for \\p cp. Returns a bool",
        "  //!        (not a pointer into the table) so it stays constant-evaluable on every compiler.",
        "  constexpr bool cp_in_ranges(std::span<const code_range> ranges,",
        "                              char32_t                    cp)",
        "  {",
        "    std::size_t lo {0};",
        "    std::size_t hi {ranges.size()};",
        "    while (lo < hi) {",
        "      const std::size_t mid {lo + ((hi - lo) / 2)};",
        "      if (ranges[mid].hi < cp) {",
        "        lo = mid + 1;",
        "      }",
        "      else {",
        "        hi = mid;",
        "      }",
        "    }",
        "    return lo < ranges.size() && cp >= ranges[lo].lo && cp <= ranges[lo].hi;",
        "  }",
        "",
        "  //! \\brief Whether \\p cp is a Unicode word / digit / whitespace code point (== re `\\w`/`\\d`/`\\s`).",
        "  constexpr bool is_word_cp(char32_t cp) { return cp_in_ranges(word_ranges, cp); }",
        "  constexpr bool is_digit_cp(char32_t cp) { return cp_in_ranges(digit_ranges, cp); }",
        "  constexpr bool is_space_cp(char32_t cp) { return cp_in_ranges(space_ranges, cp); }",
        "",
    ]
    out += common.file_footer("REAL_UNICODE_PROPS_HPP")
    common.write_lines(
        path, out,
        f"wrote {path}: word {len(tables['word'])} / digit {len(tables['digit'])} / "
        f"space {len(tables['space'])} ranges, Unicode {ver}")


def main():
    dest = sys.argv[1] if len(sys.argv) > 1 else "include/real/unicode_props.hpp"
    try:
        tables = {name: build_ranges(pat) for name, pat in _PATTERNS.items()}
    except Exception as exc:  # noqa: BLE001 - surface any re/unicodedata API change clearly
        sys.exit(f"gen_unicode_props: failed building ranges from re/unicodedata -- API change? ({exc})")
    for name, pat in _PATTERNS.items():
        validate(name, tables[name], pat)
    emit(tables, dest)


if __name__ == "__main__":
    main()
