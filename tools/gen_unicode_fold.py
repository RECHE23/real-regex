#!/usr/bin/env python3
"""Generate include/real/unicode_fold.hpp: the Unicode simple case-folding orbit table.

Source of truth: CPython's own case-insensitive matching, so REAL's `flags::icase` in text mode
is identical to `re.IGNORECASE` BY CONSTRUCTION (same data, same relation) -- not merely derived
from CaseFolding.txt and hoped to agree. The orbit of a code point is exactly the set that
`re.compile(re.escape(chr(cp)), re.IGNORECASE)` matches. We build orbits with a union-find over
`str.lower/upper/casefold` plus `re._compiler._EXTRA_CASES`, then REPAIR any code point with a
multi-character case mapping (e.g. U+0130 LATIN CAPITAL I WITH DOT ABOVE, which `re` folds to `i`
but `str.lower` expands to 'i' + combining dot) using `re` as the oracle, and finally VALIDATE
every cased code point against `re` exhaustively. The script aborts on any mismatch.

Deterministic: for a fixed CPython Unicode version the output is byte-identical. Regenerate after a
CPython Unicode-data bump (see tools/REGEN.md); the emitted `unicode_fold_unidata_version` is
asserted at compile time against the build's unicodedata, and the contract tests are a second net.
"""
import re
import re._compiler as _compiler
import sys
import unicodedata

import _gen_common as common

try:
    _EXTRA_CASES = _compiler._EXTRA_CASES
except AttributeError as exc:  # pragma: no cover - CPython-internal, can move between versions
    sys.exit(
        "gen_unicode_fold: re._compiler._EXTRA_CASES is gone (moved/removed in this CPython) -- "
        "check CPython's re internals (Lib/re/_compiler.py / _casefix.py) for the fold extra-cases "
        f"table and update this import. ({exc})")


def cased_codepoints():
    """Every code point with a non-identity case mapping or an extra-cases entry."""
    cased = set()
    for cp in range(0x110000):
        ch = chr(cp)
        if ch.lower() != ch or ch.upper() != ch or cp in _EXTRA_CASES:
            cased.add(cp)
    for lo, extras in _EXTRA_CASES.items():
        cased.add(lo)
        cased.update(extras)
    return cased


class UnionFind:
    def __init__(self, items):
        self.parent = {x: x for x in items}

    def find(self, x):
        root = x
        while self.parent[root] != root:
            root = self.parent[root]
        while self.parent[x] != root:
            self.parent[x], x = root, self.parent[x]
        return root

    def union(self, a, b):
        if a in self.parent and b in self.parent:
            ra, rb = self.find(a), self.find(b)
            if ra != rb:
                self.parent[ra] = rb


def build_orbits(cased):
    uf = UnionFind(cased)
    multichar = []
    for cp in cased:
        ch = chr(cp)
        for mapped in (ch.lower(), ch.upper(), ch.casefold()):
            if len(mapped) == 1:
                uf.union(cp, ord(mapped))
        if any(len(m) > 1 for m in (ch.lower(), ch.upper(), ch.casefold())):
            multichar.append(cp)
    for lo, extras in _EXTRA_CASES.items():
        for e in extras:
            uf.union(lo, e)
    # Repair code points whose str case mapping is multi-character (re single-folds them): use re.
    cased_sorted = sorted(cased)
    for cp in multichar:
        pat = re.compile(re.escape(chr(cp)), re.IGNORECASE)
        for d in cased_sorted:
            if pat.fullmatch(chr(d)):
                uf.union(cp, d)
    groups = {}
    for cp in cased:
        groups.setdefault(uf.find(cp), set()).add(cp)
    return [tuple(sorted(g)) for g in groups.values() if len(g) >= 2]


def validate(orbits, cased):
    """Every cased code point's re match set must equal its orbit. Abort on any mismatch."""
    orbit_of = {}
    for o in orbits:
        for cp in o:
            orbit_of[cp] = set(o)
    cased_sorted = sorted(cased)

    def check(cp):
        pat = re.compile(re.escape(chr(cp)), re.IGNORECASE)
        got = {d for d in cased_sorted if pat.fullmatch(chr(d))}
        got.discard(cp)
        expected = orbit_of.get(cp, set()) - {cp}
        if got != expected:
            return (f"U+{cp:04X} {chr(cp)!r}: re={sorted(hex(x) for x in got)} "
                    f"table={sorted(hex(x) for x in expected)}")
        return None

    common.validate_exhaustive(cased_sorted, check,
                               lambda n: f"ABORT: {n} orbit(s) disagree with re.IGNORECASE")


def emit(orbits, path):
    # One entry per cased code point, sorted by code point, carrying its (<= 3) fold partners.
    entries = []
    for o in orbits:
        for cp in o:
            partners = [p for p in o if p != cp]
            entries.append((cp, partners))
    entries.sort()
    max_partners = max(len(p) for _, p in entries)
    assert max_partners <= 3, f"orbit larger than 4 members ({max_partners + 1}); widen fold_entry"
    ver = unicodedata.unidata_version
    lines = common.file_header(
        filename="unicode_fold.hpp",
        brief="Unicode simple case-folding orbits for text-mode `flags::icase`.",
        generator="gen_unicode_fold.py",
        doc_lines=[
            "Each entry maps a code point to the other members of its case-fold orbit (the set that",
            "matches it under IGNORECASE). Built from, and exhaustively validated against, CPython's",
            "own case-insensitive matching, so REAL's text-mode icase is identical to re.IGNORECASE",
            f"by construction. Unicode data version: {ver} (asserted at compile time).",
        ],
        guard="REAL_UNICODE_FOLD_HPP",
        includes=[
            "#include <cstddef>",
            "#include <cstdint>",
        ],
        version_kind="orbits",
        version_const="unicode_fold_unidata_version")
    lines += [
        "  //! \\brief A code point and the other members of its case-fold orbit (up to 3; orbits <= 4).",
        "  struct fold_entry",
        "  {",
        "    std::uint32_t cp;         //!< The code point.",
        "    std::uint32_t partner[3]; //!< Fold partners (only the first \\ref count are meaningful).",
        "    std::uint8_t  count;      //!< Number of valid partners (orbit size - 1).",
        "  };",
        "",
        "  //! \\brief Fold orbits, sorted by \\ref fold_entry::cp for binary search.",
        "  inline constexpr fold_entry unicode_fold_table[] {",
    ]
    for cp, partners in entries:
        p = partners + [0] * (3 - len(partners))
        lines.append(f"    {{0x{cp:04X}, {{0x{p[0]:04X}, 0x{p[1]:04X}, 0x{p[2]:04X}}}, {len(partners)}}},")
    lines += [
        "  };",
        "",
        f"  inline constexpr std::size_t unicode_fold_table_size {{{len(entries)}}}; "
        f"//!< Number of entries in \\ref unicode_fold_table.",
        "",
        "  /*!",
        "   * \\brief Index of the first entry whose code point is at or after \\p cp, or",
        "   *        \\ref unicode_fold_table_size if none is. The seek half of \\ref find_fold_index,",
        "   *        exposed on its own so a caller holding a RANGE enters the table once and walks",
        "   *        forward instead of scanning it whole -- see \\c real::detail::unicode_casefold.",
        "   * \\param[in] cp The code point to seek.",
        "   * \\return The index of the first entry at or after \\p cp; \\ref unicode_fold_table_size when none is.",
        "   */",
        "  constexpr std::size_t find_fold_lower_bound(std::uint32_t cp)",
        "  {",
        "    std::size_t lo {0};",
        "    std::size_t hi {unicode_fold_table_size};",
        "    while (lo < hi) {",
        "      const std::size_t mid {lo + ((hi - lo) / 2)};",
        "      if (unicode_fold_table[mid].cp < cp) {",
        "        lo = mid + 1;",
        "      }",
        "      else {",
        "        hi = mid;",
        "      }",
        "    }",
        "    return lo;",
        "  }",
        "",
        "  /*!",
        "   * \\brief Binary-searches \\ref unicode_fold_table for \\p cp; returns its index, or",
        "   *        \\ref unicode_fold_table_size if \\p cp is not cased. An index (not a pointer into",
        "   *        the table) keeps this usable in a constant expression on every compiler — g++",
        "   *        rejects a `&table[i] != nullptr` comparison inside a `static_regex`.",
        "   *        Shared by the parser (is a literal cased?) and the compiler (its fold partners).",
        "   * \\param[in] cp The code point to look up.",
        "   * \\return Its index in \\ref unicode_fold_table, or \\ref unicode_fold_table_size when uncased.",
        "   */",
        "  constexpr std::size_t find_fold_index(std::uint32_t cp)",
        "  {",
        "    std::size_t lo {0};",
        "    std::size_t hi {unicode_fold_table_size};",
        "    while (lo < hi) {",
        "      const std::size_t mid {lo + ((hi - lo) / 2)};",
        "      if (unicode_fold_table[mid].cp < cp) {",
        "        lo = mid + 1;",
        "      }",
        "      else {",
        "        hi = mid;",
        "      }",
        "    }",
        "    if (lo < unicode_fold_table_size && unicode_fold_table[lo].cp == cp) {",
        "      return lo;",
        "    }",
        "    return unicode_fold_table_size;",
        "  }",
        "",
    ]
    lines += common.file_footer("REAL_UNICODE_FOLD_HPP")
    common.write_lines(path, lines,
                       f"wrote {path}: {len(entries)} entries, {len(orbits)} orbits, Unicode {ver}")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "include/real/unicode/unicode_fold.hpp"
    cased = cased_codepoints()
    orbits = build_orbits(cased)
    validate(orbits, cased)
    emit(orbits, out)


if __name__ == "__main__":
    main()
