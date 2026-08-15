#!/usr/bin/env python3
"""Generate include/real/unicode/unicode_binprop.hpp: the Unicode binary-property ranges for `\\p{...}`.

Its contract is the UCD (like gen_unicode_property_tables.py / gen_unicode_script_tables.py, and unlike
gen_unicode_props.py whose contract is CPython `re`). `unicodedata` exposes General_Category and (via
`unicodedata.category`) nothing else usable here, so the SOURCE is three committed UCD data files,
parsed directly like tools/ucd/Scripts.txt: tools/ucd/DerivedCoreProperties.txt, tools/ucd/PropList.txt
and tools/ucd/emoji-data.txt (version-pinned, same UCD 16.0.0 as Scripts.txt).

Unlike General_Category (a partition -- every code point has exactly one) or Script (likewise), binary
properties are NOT a partition: a code point can be Alphabetic AND ID_Start AND White_Space at once, or
none of them. So the shape here is per-property (like gc_property's per-category tables), not a single
partitioning table (like script_ranges) -- one sorted, coalesced `code_range[]` per property, an enum,
a parallel dispatch table, and a loose-keyed `resolve_binprop` (the `\\p{...}` parser's contract, same
style as `resolve_gc` / `resolve_script`).

`InCB` (Indic_Conjunct_Break) in DerivedCoreProperties.txt is excluded: it is an ENUMERATED property
(Linker / Consonant / Extend / None -- a third field, not a bare yes/no), the same shape as
General_Category or Script, not a binary property -- out of scope for this generator by construction.

Two checks, matching the sibling generators: the parse is self-consistent (each property's own ranges
sorted, coalesced, disjoint); and, when the third-party `regex` module is importable (a gen-time
cross-oracle -- `pip install regex` in a venv), every code point's parsed membership in a sampled binary
property is re-verified against `regex`'s own `\\p{Name}`, aborting on any disagreement. The `regex`
check does not affect the emitted bytes (the parse is the source), so the regen guard stays byte-exact
without it.

Anti-collision guard: a binary property's loose key must not collide with any General_Category or
Script loose alias (`resolve_property`, ast.hpp, tries `gc=`/`sc=` before a bare name falls through to
`resolve_binprop` -- a collision would silently shadow the binary property with the wrong semantics,
e.g. `\\p{Lowercase}` the binary property vs `\\p{Ll}`'s "Lowercase_Letter" GC long alias). Checked by
importing the sibling generators' own alias-key logic directly (not a hand-copied approximation, so it
can never drift from what they actually emit) and aborting the regen on any overlap.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _gen_common as common  # noqa: E402
import gen_unicode_property_tables as gc_gen  # noqa: E402 - reused for the anti-collision guard
import gen_unicode_script_tables as script_gen  # noqa: E402 - reused for the anti-collision guard

_MAX_CP = 0x10FFFF
_SURROGATE_LO = 0xD800
_SURROGATE_HI = 0xDFFF
_UCD_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ucd")

# (filename, version-header regex, human label for messages)
_SOURCES = [
    ("DerivedCoreProperties.txt", re.compile(r"^#\s*DerivedCoreProperties-([0-9.]+)\.txt"), "DerivedCoreProperties"),
    ("PropList.txt", re.compile(r"^#\s*PropList-([0-9.]+)\.txt"), "PropList"),
    ("emoji-data.txt", re.compile(r"^#\s*Used with Emoji Version ([0-9.]+)"), "emoji-data"),
]

# DerivedCoreProperties.txt's InCB is enumerated (a third `; Linker`/`; Consonant`/`; Extend` field), not
# a bare binary yes/no -- excluded by construction, not a cherry-pick (see module docstring).
_NOT_BINARY = {"InCB"}

_LINE = re.compile(r"^([0-9A-Fa-f]{4,6})(?:\.\.([0-9A-Fa-f]{4,6}))?\s*;\s*([A-Za-z_]+)\s*$")


def _norm_version(v):
    """Pad a dotted version to 3 components for comparison -- emoji-data.txt states "16.0" (Emoji spec
    versions are major.minor only), the other two sources state "16.0.0"; both mean the same UCD 16.0.0."""
    parts = v.split(".")
    while len(parts) < 3:
        parts.append("0")
    return tuple(int(p) for p in parts)


def _loose(name):
    """UAX44-LM3-ish loose match key: lowercase, drop spaces / underscores / hyphens."""
    return name.lower().replace("_", "").replace("-", "").replace(" ", "")


def _parse_one(filename, version_re, label):
    """Parse one committed UCD source file into {property_name: [(lo, hi), ...]} (unsorted) + its version."""
    path = os.path.join(_UCD_DIR, filename)
    version = None
    per_prop = {}
    with open(path, encoding="utf-8") as f:
        for raw in f:
            if version is None:
                m = version_re.match(raw)
                if m:
                    version = m.group(1)
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [p.strip() for p in line.split(";")]
            if len(fields) != 2:
                continue  # a 3-field line (InCB's values) or anything not this generator's binary shape
            cps, name = fields
            if name in _NOT_BINARY:
                continue
            m = _LINE.match(f"{cps} ; {name}")
            if not m:
                sys.exit(f"gen_unicode_binprop_tables: unparsable {label} line: {raw!r}")
            lo = int(m.group(1), 16)
            hi = int(m.group(2), 16) if m.group(2) else lo
            per_prop.setdefault(m.group(3), []).append((lo, hi))
    if version is None:
        sys.exit(f"gen_unicode_binprop_tables: {label} is missing its version header")
    return per_prop, version


def _coalesce(ranges):
    """Sort and merge adjacent/overlapping [lo, hi] ranges into the minimal sorted, disjoint form."""
    ranges = sorted(ranges)
    merged = []
    for lo, hi in ranges:
        if merged and lo <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], hi))
        else:
            merged.append((lo, hi))
    return merged


def _validate_structure(name, ranges):
    """Sorted, disjoint, no surrogate overlap -- the invariant a single property's own table must hold."""
    prev_hi = -1
    for lo, hi in ranges:
        if lo > hi or lo <= prev_hi:
            sys.exit(f"gen_unicode_binprop_tables: {name} table not sorted/disjoint near U+{lo:04X}")
        if lo <= _SURROGATE_HI and hi >= _SURROGATE_LO:
            sys.exit(f"gen_unicode_binprop_tables: {name} range U+{lo:04X}..U+{hi:04X} overlaps the surrogate block")
        prev_hi = hi


def _in_ranges(ranges, cp):
    lo, hi = 0, len(ranges)
    while lo < hi:
        mid = (lo + hi) // 2
        if ranges[mid][1] < cp:
            lo = mid + 1
        else:
            hi = mid
    return lo < len(ranges) and ranges[lo][0] <= cp <= ranges[lo][1]


def _cross_check_regex(tables, version):
    """Gen-time cross-oracle: every code point's parsed membership must equal the `regex` module's, per
    property, if importable. Optional -- absence does not affect the emitted bytes."""
    skew = common.cross_oracle_skew(version)
    if skew is not None:
        print(f"gen_unicode_binprop_tables: cross-oracle SKIPPED -- {skew}.", file=sys.stderr)
        return
    try:
        import regex  # noqa: PLC0415 - optional, gen-time only
    except ImportError:
        print("gen_unicode_binprop_tables: `regex` module absent, skipping the cross-oracle (parse-only).",
              file=sys.stderr)
        return
    skew = common.regex_version_skew(regex)
    if skew is not None:
        print(f"gen_unicode_binprop_tables: cross-oracle SKIPPED -- {skew}.", file=sys.stderr)
        return
    for name, ranges in tables.items():
        try:
            matcher = regex.compile(rf"\p{{{name}}}")
        except regex.error:
            print(f"gen_unicode_binprop_tables: `regex` module does not know \\p{{{name}}}, skipping it.",
                  file=sys.stderr)
            continue
        common.validate_exhaustive(
            (cp for cp in range(0, _MAX_CP + 1) if not (_SURROGATE_LO <= cp <= _SURROGATE_HI)),
            lambda cp, r=ranges, m=matcher: (
                f"{name} U+{cp:04X}" if _in_ranges(r, cp) != bool(m.match(chr(cp))) else None),
            lambda n, nm=name: sys.exit(f"ABORT {nm}: {n} code point(s) disagree with the regex module"))


def _gc_and_script_loose_keys():
    """The GC and Script loose alias keys, via the sibling generators' own logic (not a hand-copied
    approximation -- see the anti-collision guard note in the module docstring)."""
    gc_keys = set()
    for name in gc_gen._CATEGORIES + gc_gen._GROUPS:  # noqa: SLF001 - intentional tooling-internal reuse
        gc_keys.add(gc_gen._loose(name))
        if name in gc_gen._GC_LONG:
            gc_keys.add(gc_gen._loose(gc_gen._GC_LONG[name]))
    script_entries, _ = script_gen._parse_scripts_txt(script_gen._SCRIPTS_TXT)  # noqa: SLF001
    script_keys = {script_gen._loose(name) for _, _, name in script_entries}
    return gc_keys, script_keys


def _check_no_collisions(names):
    """Abort the regen if any binary-property loose key collides with a GC or Script loose alias --
    ast.hpp's resolve_property tries gc=/sc= before falling through to resolve_binprop for a bare name,
    so a collision would silently shadow the binary property with the wrong semantics."""
    gc_keys, script_keys = _gc_and_script_loose_keys()
    collisions = []
    for name in names:
        key = _loose(name)
        if key in gc_keys:
            collisions.append(f"{name} (loose {key!r}) collides with a General_Category alias")
        if key in script_keys:
            collisions.append(f"{name} (loose {key!r}) collides with a Script alias")
    if collisions:
        sys.exit("gen_unicode_binprop_tables: ABORT -- binary-property / GC-or-Script alias collision:\n  "
                  + "\n  ".join(collisions))


def _emit(tables, version, path):
    names = sorted(tables)
    out = common.file_header(
        filename="unicode_binprop.hpp",
        brief="Unicode binary-property ranges for `\\p{...}` (UCD contract; PCRE2-parity properties).",
        generator="gen_unicode_binprop_tables.py",
        doc_lines=[
            "Parsed directly from the committed tools/ucd/{DerivedCoreProperties,PropList,emoji-data}.txt",
            f"(UCD {version}, same version as Scripts.txt) -- not derived from `unicodedata`, which exposes",
            "General_Category only. Each property gets its own sorted, disjoint range table (NOT a",
            "partition like General_Category/Script: a code point can satisfy several, or none). Checked at",
            "generation time to have zero loose-key collision with the General_Category / Script aliases",
            "(ast.hpp::resolve_property tries gc=/sc= first for a bare name).",
        ],
        guard="REAL_UNICODE_BINPROP_HPP",
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
        version_const="unicode_binprop_unidata_version")
    for name in names:
        ranges = tables[name]
        cps = sum(hi - lo + 1 for lo, hi in ranges)
        out += [f"  //! \\brief `\\p{{{name}}}` — {len(ranges)} ranges, {cps} code points.",
                f"  inline constexpr code_range binprop_{name}_ranges[] {{"]
        out += [f"    {{0x{lo:04X}, 0x{hi:04X}}}," for lo, hi in ranges]
        out += ["  };", ""]
    out += ["  /*!",
            f"   * \\brief A Unicode binary property: the {len(names)} standard yes/no properties this build knows.",
            "   */"]
    out += ["  enum class binprop : std::uint8_t {"]
    out += [f"    {name}," for name in names]
    out += ["    count", "  };", ""]
    out += ["  //! \\brief Range table indexed by \\ref binprop (parallel to the enum order)."]
    out += ["  inline constexpr std::span<const code_range> binprop_ranges[] {"]
    out += [f"    binprop_{name}_ranges," for name in names]
    out += ["  };", ""]
    out += ["  /*!"]
    out += ["   * \\brief Whether \\p cp has the binary property \\p prop (== the UCD)."]
    out += ["   * \\param[in] prop The property to test for."]
    out += ["   * \\param[in] cp   The code point to test."]
    out += ["   * \\return Whether \\p cp has \\p prop."]
    out += ["   */"]
    out += ["  constexpr bool is_binprop_cp(binprop prop, char32_t cp)"]
    out += ["  {"]
    out += ["    return cp_in_ranges(binprop_ranges[static_cast<std::size_t>(prop)], cp);"]
    out += ["  }", ""]
    out += ["  /*! \\brief A loose-normalized (lowercase, no _/-/space) binary-property name and its value. */"]
    out += ["  struct binprop_alias_entry"]
    out += ["  {"]
    out += ["    std::string_view name; //!< The loose-normalized name."]
    out += ["    binprop          prop; //!< The property it names."]
    out += ["  };", ""]
    out += ["  //! \\brief Binary-property names, loose-keyed; for the `\\p{...}` parser (no namespace prefix,"]
    out += ["  //!        same as PCRE2: `\\p{Alphabetic}`, not `\\p{bp=Alphabetic}`)."]
    out += ["  inline constexpr binprop_alias_entry binprop_aliases[] {"]
    out += [f'    {{"{_loose(n)}", binprop::{n}}},' for n in names]
    out += ["  };", ""]
    out += ["  /*!"]
    out += ["   * \\brief Resolve a loose-normalized binary-property name to its value, or `count` if unknown."]
    out += ["   * \\param[in] loose A name already put through the parser's loose normalization."]
    out += ["   * \\return The property, or `binprop::count` when no alias matches."]
    out += ["   */"]
    out += ["  constexpr binprop resolve_binprop(std::string_view loose)"]
    out += ["  {"]
    out += ["    for (const binprop_alias_entry& a : binprop_aliases) {"]
    out += ["      if (a.name == loose) {"]
    out += ["        return a.prop;"]
    out += ["      }"]
    out += ["    }"]
    out += ["    return binprop::count;"]
    out += ["  }", ""]
    out += common.file_footer("REAL_UNICODE_BINPROP_HPP")
    total_ranges = sum(len(tables[n]) for n in names)
    common.write_lines(path, out, f"wrote {path}: {len(names)} binary properties, {total_ranges} ranges, UCD {version}")


def generate(path):
    """Parse the three committed UCD sources, coalesce+validate each property's ranges, guard against a
    GC/Script alias collision, and emit the header at `path`. Shared by `main` and the regen guard."""
    tables = {}
    versions = {}
    for filename, version_re, label in _SOURCES:
        per_prop, version = _parse_one(filename, version_re, label)
        versions[label] = version
        for name, ranges in per_prop.items():
            if name in tables:
                sys.exit(f"gen_unicode_binprop_tables: {name} defined in both {label} and another source "
                          f"-- unexpected cross-file collision, investigate before regenerating")
            tables[name] = _coalesce(ranges)
    distinct_versions = {_norm_version(v) for v in versions.values()}
    if len(distinct_versions) != 1:
        sys.exit(f"gen_unicode_binprop_tables: source version mismatch across files: {versions}")
    version = max(versions.values(), key=_norm_version)  # the longest/most-precise form ("16.0.0" over "16.0")
    for name, ranges in tables.items():
        _validate_structure(name, ranges)
    _check_no_collisions(tables.keys())
    _cross_check_regex(tables, version)
    _emit(tables, version, path)


def main():
    generate(sys.argv[1] if len(sys.argv) > 1 else "include/real/unicode/unicode_binprop.hpp")


if __name__ == "__main__":
    main()
