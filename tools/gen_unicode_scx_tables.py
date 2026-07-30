#!/usr/bin/env python3
"""Generate include/real/unicode/unicode_scx.hpp: the Unicode Script_Extensions ranges for `\\p{scx=...}`.

Its contract is the UCD (like the sibling gen_unicode_*_tables.py generators). Script_Extensions is NOT a
partition (unlike Script): scx(cp) is a SET of scripts, defaulting to {Script(cp)} for any code point
without an explicit override, and replaced entirely by the override set for a code point that HAS one
(tools/ucd/ScriptExtensions.txt, in the short UAX24 codes -- Latn, Grek, ... -- mapped to the same `script`
enum gen_unicode_script_tables.py already builds, via that generator's own `parse_sc_short_codes`). A
combining mark shared across scripts is the common case: U+0301 COMBINING ACUTE ACCENT has base
Script=Inherited, but scx={Cher, Cyrl, Grek, Latn, Osge, Sunu, Tale, Todr} -- Inherited is not even in its
own scx, since the override REPLACES the default, it does not extend it.

Shape: one sorted, disjoint `code_range[]` per script (same non-partition style as
gen_unicode_binprop_tables.py -- a code point can be in several scripts' scx, or, for Common/Inherited-only
code points with no override, just the one). No new enum: `scx_ranges[]` is indexed by the EXISTING
`real::detail::script` enum, so `\\p{sc=X}` and `\\p{scx=X}` share one name resolver (`resolve_script`) --
only the range TABLE differs (`script_ranges`, the partition, vs `scx_ranges`, per-script and overlapping).

Two checks: the parse is self-consistent (each script's own table sorted/disjoint); and, when the
third-party `regex` module is importable (a gen-time cross-oracle -- same convention as the sibling
generators), a sample of code points' parsed scx membership is re-verified against `regex`'s own
`\\p{scx=Name}`.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _gen_common as common  # noqa: E402
import gen_unicode_script_tables as script_gen  # noqa: E402 - reused: script parse, enum names, short codes

_MAX_CP = 0x10FFFF
_SURROGATE_LO = 0xD800
_SURROGATE_HI = 0xDFFF
_SCRIPT_EXTENSIONS_TXT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ucd", "ScriptExtensions.txt")
_LINE = re.compile(r"^([0-9A-Fa-f]{4,6})(?:\.\.([0-9A-Fa-f]{4,6}))?\s*;\s*(.+)$")
_VERSION = re.compile(r"^#\s*ScriptExtensions-([0-9.]+)\.txt")


def _script_of_factory(entries):
    """A closure over Scripts.txt's sorted, disjoint (lo, hi, name) partition -- binary search, `Unknown`
    for a gap (mirrors unicode_script.hpp's own script_of, kept in Python for the override pass below)."""
    def script_of(cp):
        lo_i, hi_i = 0, len(entries)
        while lo_i < hi_i:
            mid = (lo_i + hi_i) // 2
            if entries[mid][1] < cp:
                lo_i = mid + 1
            else:
                hi_i = mid
        if lo_i < len(entries) and entries[lo_i][0] <= cp <= entries[lo_i][1]:
            return entries[lo_i][2]
        return "Unknown"
    return script_of


def _parse_script_extensions(path, long_of):
    """Parse ScriptExtensions.txt into {cp: [long_script_name, ...]} (one entry per code point that HAS an
    override -- the file only lists exceptions) and the data version."""
    version = None
    overrides = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if version is None:
                m = _VERSION.match(line)
                if m:
                    version = m.group(1)
            data = line.split("#", 1)[0].strip()
            if not data:
                continue
            lm = _LINE.match(data)
            if not lm:
                sys.exit(f"gen_unicode_scx_tables: unparsable ScriptExtensions.txt line: {line!r}")
            lo = int(lm.group(1), 16)
            hi = int(lm.group(2), 16) if lm.group(2) else lo
            shorts = lm.group(3).split()
            longs = []
            for short in shorts:
                long_name = long_of.get(short)
                if long_name is None:
                    sys.exit(f"gen_unicode_scx_tables: unknown short script code {short!r} in "
                             f"ScriptExtensions.txt (PropertyValueAliases.txt out of sync?)")
                if long_name not in longs:  # a line can repeat a code (seen in the wild); keep it a set
                    longs.append(long_name)
            for cp in range(lo, hi + 1):
                overrides[cp] = longs
    if version is None:
        sys.exit("gen_unicode_scx_tables: ScriptExtensions.txt is missing its version header")
    return overrides, version


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


def _ranges_minus_point(ranges, cp):
    """Remove a single code point from a sorted, disjoint range list (splits the containing range)."""
    out = []
    for lo, hi in ranges:
        if not (lo <= cp <= hi):
            out.append((lo, hi))
            continue
        if lo < cp:
            out.append((lo, cp - 1))
        if cp < hi:
            out.append((cp + 1, hi))
    return out


def _in_ranges(ranges, cp):
    lo, hi = 0, len(ranges)
    while lo < hi:
        mid = (lo + hi) // 2
        if ranges[mid][1] < cp:
            lo = mid + 1
        else:
            hi = mid
    return lo < len(ranges) and ranges[lo][0] <= cp <= ranges[lo][1]


def _validate_structure(name, ranges):
    prev_hi = -1
    for lo, hi in ranges:
        if lo > hi or lo <= prev_hi:
            sys.exit(f"gen_unicode_scx_tables: {name} table not sorted/disjoint near U+{lo:04X}")
        if lo <= _SURROGATE_HI and hi >= _SURROGATE_LO:
            sys.exit(f"gen_unicode_scx_tables: {name} range U+{lo:04X}..U+{hi:04X} overlaps the surrogate block")
        prev_hi = hi


def _cross_check_regex(scx_ranges, names):
    """Gen-time cross-oracle: a sample of code points' parsed scx membership must equal the `regex`
    module's `\\p{scx=Name}`, if importable. Optional -- absence does not affect the emitted bytes. Full
    exhaustive per-script check like the sibling generators would be O(170 x 0x110000); instead every
    OVERRIDDEN code point (where a mistake would actually show up) plus a coarse per-script sample."""
    try:
        import regex  # noqa: PLC0415 - optional, gen-time only
    except ImportError:
        print("gen_unicode_scx_tables: `regex` module absent, skipping the cross-oracle (parse-only).",
              file=sys.stderr)
        return
    mismatches = 0
    for name in names:
        ranges = scx_ranges[name]
        try:
            matcher = regex.compile(rf"\p{{scx={name}}}")
        except regex.error:
            print(f"gen_unicode_scx_tables: `regex` module does not know \\p{{scx={name}}}, skipping it.",
                  file=sys.stderr)
            continue
        # Sample: every range boundary (lo, hi) plus lo-1/hi+1 where in-bounds -- catches off-by-one range
        # errors, the actual failure mode of this generator's range algebra.
        sample = set()
        for lo, hi in ranges:
            for cp in (lo - 1, lo, hi, hi + 1):
                if 0 <= cp <= _MAX_CP and not (_SURROGATE_LO <= cp <= _SURROGATE_HI):
                    sample.add(cp)
        for cp in sample:
            expected = bool(matcher.match(chr(cp)))
            got = _in_ranges(ranges, cp)
            if expected != got:
                mismatches += 1
                if mismatches <= 8:
                    print(f"  MISMATCH scx={name} U+{cp:04X}: parsed {got}, regex {expected}", file=sys.stderr)
    if mismatches:
        sys.exit(f"ABORT: {mismatches} code point(s) disagree with the regex module's \\p{{scx=...}}")


def _emit(scx_ranges, names, version, path):
    out = common.file_header(
        filename="unicode_scx.hpp",
        brief="Unicode Script_Extensions ranges for `\\p{scx=...}` (UCD contract; NOT a partition).",
        generator="gen_unicode_scx_tables.py",
        doc_lines=[
            "Parsed from the committed tools/ucd/Scripts.txt (base) + ScriptExtensions.txt (overrides),",
            f"UCD {version} (asserted, same version as Scripts.txt). Script_Extensions defaults to a code",
            "point's own Script, EXCEPT the code points ScriptExtensions.txt overrides (a combining mark",
            "shared across scripts, typically), whose scx REPLACES the default with an explicit set --",
            "so, unlike script_ranges (a partition), a code point can be in several scripts' scx_ranges at",
            "once, or none extra beyond its own Script. Indexed by the EXISTING real::detail::script enum",
            "(unicode_script.hpp) -- no new enum, no new name resolver: `\\p{sc=X}` and `\\p{scx=X}` share",
            "`resolve_script` (long name AND short UAX24 code), only the range table differs.",
        ],
        guard="REAL_UNICODE_SCX_HPP",
        includes=[
            "#include <cstddef>",
            "#include <cstdint>",
            "#include <span>",
            "",
            '#include "real/core/program.hpp"           // code_range',
            '#include "real/unicode/unicode_props.hpp"  // cp_in_ranges (contract-neutral binary search)',
            '#include "real/unicode/unicode_script.hpp" // script (reused, not redefined)',
        ],
        version_kind="tables",
        version_const="unicode_scx_unidata_version")
    for name in names:
        ranges = scx_ranges[name]
        cps = sum(hi - lo + 1 for lo, hi in ranges)
        out += [f"  //! \\brief `\\p{{scx={name}}}` — {len(ranges)} ranges, {cps} code points.",
                f"  inline constexpr code_range scx_{name}_ranges[] {{"]
        out += [f"    {{0x{lo:04X}, 0x{hi:04X}}}," for lo, hi in ranges]
        out += ["  };", ""]
    out += ["  //! \\brief Range table indexed by \\ref script (parallel to the enum order, `Unknown` empty --"]
    out += ["  //!        no code point's scx is ever explicitly Unknown, see the generator)."]
    out += ["  inline constexpr std::span<const code_range> scx_ranges[] {"]
    out += ["    {}, // script::Unknown -- never a member of any scx (ScriptExtensions.txt never lists it)"]
    out += [f"    scx_{name}_ranges," for name in names]
    out += ["  };", ""]
    out += ["  /*!"]
    out += ["   * \\brief Whether \\p cp is in the Script_Extensions of \\p sc (== the UCD). NOT exclusive:"]
    out += ["   *        a code point can satisfy this for several \\ref script values at once."]
    out += ["   * \\param[in] sc The Script to test for."]
    out += ["   * \\param[in] cp The code point to test."]
    out += ["   * \\return Whether \\p sc is in \\p cp's Script_Extensions set."]
    out += ["   */"]
    out += ["  constexpr bool is_scx_cp(script sc, char32_t cp)"]
    out += ["  {"]
    out += ["    return cp_in_ranges(scx_ranges[static_cast<std::size_t>(sc)], cp);"]
    out += ["  }", ""]
    out += common.file_footer("REAL_UNICODE_SCX_HPP")
    total_ranges = sum(len(scx_ranges[n]) for n in names)
    common.write_lines(path, out, f"wrote {path}: {len(names)} scripts, {total_ranges} ranges, UCD {version}")


def generate(path):
    """Build every script's scx range table, validate, cross-check, and emit the header at `path`. Shared
    by `main` and the regen guard."""
    entries, scripts_version = script_gen._parse_scripts_txt(script_gen._SCRIPTS_TXT)  # noqa: SLF001
    short_of, pva_version = script_gen.parse_sc_short_codes()
    long_of = {short: long for long, short in short_of.items()}
    names = sorted({name for _, _, name in entries})

    overrides, scx_version = _parse_script_extensions(_SCRIPT_EXTENSIONS_TXT, long_of)
    distinct_versions = {scripts_version, pva_version, scx_version}
    if len(distinct_versions) != 1:
        sys.exit(f"gen_unicode_scx_tables: source version mismatch: Scripts.txt={scripts_version} "
                  f"PropertyValueAliases.txt={pva_version} ScriptExtensions.txt={scx_version}")

    # Base: scx defaults to Script -- start each script's scx table as its own Script partition ranges.
    base_ranges = {name: [] for name in names}
    for lo, hi, name in entries:
        base_ranges[name].append((lo, hi))

    script_of = _script_of_factory(entries)
    removals = {name: [] for name in names}   # code points pulled OUT of their base script's scx
    additions = {name: [] for name in names}  # code points added to a DIFFERENT script's scx

    for cp, override_names in overrides.items():
        base = script_of(cp)
        if base != "Unknown":  # Unknown has no range table (not a real script; see _emit)
            removals[base].append(cp)
        for name in override_names:
            additions[name].append(cp)

    scx_ranges = {}
    for name in names:
        ranges = base_ranges[name]
        for cp in removals[name]:
            ranges = _ranges_minus_point(ranges, cp)
        ranges = _coalesce(ranges + [(cp, cp) for cp in additions[name]])
        _validate_structure(name, ranges)
        scx_ranges[name] = ranges

    _cross_check_regex(scx_ranges, names)
    _emit(scx_ranges, names, scripts_version, path)


def main():
    generate(sys.argv[1] if len(sys.argv) > 1 else "include/real/unicode/unicode_scx.hpp")


if __name__ == "__main__":
    main()
