#!/usr/bin/env python3
"""Generate include/real/unicode/unicode_property.hpp: the Unicode General_Category property ranges for `\\p{}`.

DISTINCT from gen_unicode_props.py, on purpose. That generator's contract is CPython's `re` (the `\\w \\d \\s`
shorthands must equal `re` for the Python drop-in); THIS one's contract is the UCD (the `\\p{...}` property
classes must equal the Unicode Character Database, matching the `regex` crate). The two sources of truth never
mix: separate generator, separate header. Here the oracle is `unicodedata.category` — for every non-surrogate
code point we ask its General_Category and coalesce each category (and each top-level group) into ranges.

Two guarantees, the same as the sibling generator:
  1. the construction IS the oracle (unicodedata.category), and
  2. a second pass re-checks every code point (`in_ranges(cp) == (category matches)`), aborting on any
     mismatch, so a range-building bug cannot slip through. Only the public `unicodedata` is used, so this is
     robust across Unicode versions; the emitted `..._unidata_version` constant pins the data version and the
     regen guard skips when the running Python's version differs.

Emits, per category and per group, a sorted non-overlapping `code_range` table and a dispatch by `gc_property`.
Ranges-first (no speculative two-level table). Wired into the parser at ast.hpp::resolve_property
(`\\p{gc=...}` / a bare name), via the generated, loose-keyed `resolve_gc`.
"""
import sys
import unicodedata

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
import _gen_common as common  # noqa: E402

_MAX_CP = 0x10FFFF
_SURROGATE_LO = 0xD800
_SURROGATE_HI = 0xDFFF

# The 29 assignable General_Category values, in UCD order. Cs (surrogate) is omitted: a surrogate is not a
# scalar value, so it never reaches the matcher, and unicodedata cannot classify chr(surrogate).
_CATEGORIES = ["Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No",
               "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So",
               "Zs", "Zl", "Zp", "Cc", "Cf", "Co", "Cn"]
# The seven top-level groups: a group is every category whose first letter matches (C excludes Cs, as above).
_GROUPS = ["L", "M", "N", "P", "S", "Z", "C"]

# Long-name aliases (UCD PropertyValueAliases for General_Category) — the parser accepts both the short code
# (`\p{Lu}`) and the long alias (`\p{Uppercase_Letter}`), loose-matched (case- and _/-/space-insensitive).
_GC_LONG = {
    "Lu": "Uppercase_Letter", "Ll": "Lowercase_Letter", "Lt": "Titlecase_Letter", "Lm": "Modifier_Letter",
    "Lo": "Other_Letter", "L": "Letter", "Mn": "Nonspacing_Mark", "Mc": "Spacing_Mark", "Me": "Enclosing_Mark",
    "M": "Mark", "Nd": "Decimal_Number", "Nl": "Letter_Number", "No": "Other_Number", "N": "Number",
    "Pc": "Connector_Punctuation", "Pd": "Dash_Punctuation", "Ps": "Open_Punctuation", "Pe": "Close_Punctuation",
    "Pi": "Initial_Punctuation", "Pf": "Final_Punctuation", "Po": "Other_Punctuation", "P": "Punctuation",
    "Sm": "Math_Symbol", "Sc": "Currency_Symbol", "Sk": "Modifier_Symbol", "So": "Other_Symbol", "S": "Symbol",
    "Zs": "Space_Separator", "Zl": "Line_Separator", "Zp": "Paragraph_Separator", "Z": "Separator",
    "Cc": "Control", "Cf": "Format", "Co": "Private_Use", "Cn": "Unassigned", "C": "Other",
}


def _loose(name):
    """UAX44-LM3-ish loose match key: lowercase, drop spaces / underscores / hyphens."""
    return name.lower().replace("_", "").replace("-", "").replace(" ", "")


def _category(cp):
    return unicodedata.category(chr(cp))


def _build_ranges(pred):
    """Coalesce the code points satisfying `pred(cp)` into sorted inclusive [lo, hi] ranges (surrogates skipped)."""
    ranges = []
    cp = 0
    while cp <= _MAX_CP:
        if _SURROGATE_LO <= cp <= _SURROGATE_HI:
            cp = _SURROGATE_HI + 1
            continue
        if pred(cp):
            lo = cp
            while cp <= _MAX_CP and not (_SURROGATE_LO <= cp <= _SURROGATE_HI) and pred(cp):
                cp += 1
            ranges.append((lo, cp - 1))
        else:
            cp += 1
    return ranges


def _in_ranges(ranges, cp):
    lo, hi = 0, len(ranges)
    while lo < hi:
        mid = (lo + hi) // 2
        if ranges[mid][1] < cp:
            lo = mid + 1
        else:
            hi = mid
    return lo < len(ranges) and ranges[lo][0] <= cp <= ranges[lo][1]


def _non_surrogate_cps():
    for cp in range(0, _MAX_CP + 1):
        if not (_SURROGATE_LO <= cp <= _SURROGATE_HI):
            yield cp


def _validate(name, ranges, pred):
    common.validate_exhaustive(
        _non_surrogate_cps(),
        lambda cp: f"{name} U+{cp:04X}" if _in_ranges(ranges, cp) != pred(cp) else None,
        lambda n: sys.exit(f"ABORT {name}: {n} code point(s) disagree with unicodedata"))


def _emit_array(name, ranges):
    cps = sum(hi - lo + 1 for lo, hi in ranges)
    lines = [f"  //! \\brief `\\p{{{name}}}` — {len(ranges)} ranges, {cps} code points.",
             f"  inline constexpr code_range gc_{name}_ranges[] {{"]
    lines += [f"    {{0x{lo:04X}, 0x{hi:04X}}}," for lo, hi in ranges]
    lines += ["  };", ""]
    return lines


def _emit(tables, path):
    ver = unicodedata.unidata_version
    props = _CATEGORIES + _GROUPS
    out = common.file_header(
        filename="unicode_property.hpp",
        brief="Unicode General_Category ranges for `\\p{...}` (UCD contract; distinct from the re-contract shorthands).",
        generator="gen_unicode_property_tables.py",
        doc_lines=[
            "Built from, and exhaustively validated against, the Unicode Character Database via",
            f"`unicodedata.category`, so REAL's `\\p{{Gc}}` classes equal the UCD by construction (matching the",
            f"`regex` crate). SEPARATE from unicode_props.hpp, whose contract is CPython `re`. Unicode data",
            f"version: {ver} (asserted). Wired at ast.hpp::resolve_property (`\\p{{gc=...}}` / a bare name).",
        ],
        guard="REAL_UNICODE_PROPERTY_HPP",
        includes=[
            "#include <cstddef>",
            "#include <cstdint>",
            "#include <span>",
            "#include <string_view>",
            "",
            '#include "real/core/program.hpp"          // code_range',
            '#include "real/unicode/unicode_props.hpp" // cp_in_ranges (contract-neutral binary search)',
        ],
        version_kind="tables",
        version_const="unicode_property_unidata_version")
    for name in props:
        out += _emit_array(name, tables[name])
    # the dispatch enum + table + lookup
    out += ["  //! \\brief A Unicode General_Category property: the 29 assignable categories then the 7 groups."]
    out += ["  enum class gc_property : std::uint8_t {"]
    out += [f"    {name}," for name in props]
    out += ["    count", "  };", ""]
    out += ["  //! \\brief Range table indexed by \\ref gc_property (parallel to the enum order)."]
    out += ["  inline constexpr std::span<const code_range> gc_property_ranges[] {"]
    out += [f"    gc_{name}_ranges," for name in props]
    out += ["  };", ""]
    out += ["  //! \\brief Whether \\p cp is in the General_Category property \\p prop (== the UCD)."]
    out += ["  //! \\param[in] prop The General_Category to test for."]
    out += ["  //! \\param[in] cp   The code point to test."]
    out += ["  //! \\return Whether \\p cp is in \\p prop."]
    out += ["  constexpr bool is_gc_cp(gc_property prop, char32_t cp)"]
    out += ["  {"]
    out += ["    return cp_in_ranges(gc_property_ranges[static_cast<std::size_t>(prop)], cp);"]
    out += ["  }", ""]
    # loose-key -> property alias table (short code + long name), for the \p{...} parser. No std::hash: a small
    # sorted array with a linear resolve at parse time (never a hot path).
    aliases = []
    for name in _CATEGORIES + _GROUPS:
        aliases.append((_loose(name), name))
        if name in _GC_LONG:
            aliases.append((_loose(_GC_LONG[name]), name))
    aliases.sort()
    out += ["  //! \\brief A loose-normalized (lowercase, no _/-/space) General_Category name and its property."]
    out += ["  struct gc_alias_entry"]
    out += ["  {"]
    out += ["    std::string_view name; //!< The loose-normalized name (short code or long name)."]
    out += ["    gc_property      prop; //!< The property it names."]
    out += ["  };", ""]
    out += ["  //! \\brief Short codes (`Lu`) and long names (`Uppercase_Letter`), loose-keyed; for the `\\p{...}` parser."]
    out += ["  inline constexpr gc_alias_entry gc_aliases[] {"]
    out += [f'    {{"{key}", gc_property::{name}}},' for key, name in aliases]
    out += ["  };", ""]
    out += ["  //! \\brief Resolve a loose-normalized General_Category name to its property, or `count` if unknown."]
    out += ["  //! \\param[in] loose A name already put through the parser's loose normalization."]
    out += ["  //! \\return The property, or `gc_property::count` when no alias matches."]
    out += ["  constexpr gc_property resolve_gc(std::string_view loose)"]
    out += ["  {"]
    out += ["    for (const gc_alias_entry& a : gc_aliases) {"]
    out += ["      if (a.name == loose) {"]
    out += ["        return a.prop;"]
    out += ["      }"]
    out += ["    }"]
    out += ["    return gc_property::count;"]
    out += ["  }", ""]
    out += common.file_footer("REAL_UNICODE_PROPERTY_HPP")
    total_ranges = sum(len(tables[n]) for n in props)
    common.write_lines(path, out, f"wrote {path}: {len(props)} properties, {total_ranges} ranges, Unicode {ver}")


def generate(path):
    """Build every category/group range table, exhaustively re-validate it against unicodedata, and emit the
    header at `path`. Shared by `main` and the regen guard so both drive the identical, self-checking path."""
    tables = {}
    try:
        for cat in _CATEGORIES:
            tables[cat] = _build_ranges(lambda cp, c=cat: _category(cp) == c)
        for grp in _GROUPS:
            tables[grp] = _build_ranges(lambda cp, g=grp: _category(cp)[0] == g)
    except Exception as exc:  # noqa: BLE001 - surface any unicodedata API change clearly
        sys.exit(f"gen_unicode_property_tables: failed building ranges from unicodedata -- API change? ({exc})")
    for cat in _CATEGORIES:
        _validate(cat, tables[cat], lambda cp, c=cat: _category(cp) == c)
    for grp in _GROUPS:
        _validate(grp, tables[grp], lambda cp, g=grp: _category(cp)[0] == g)
    _emit(tables, path)


def main():
    generate(sys.argv[1] if len(sys.argv) > 1 else "include/real/unicode/unicode_property.hpp")


if __name__ == "__main__":
    main()
