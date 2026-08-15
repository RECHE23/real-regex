#!/usr/bin/env python3
"""Generate include/real/unicode/unicode_script.hpp: the Unicode Script property ranges for `\\p{sc=...}`.

Its contract is the UCD (like gen_unicode_property_tables.py, and unlike gen_unicode_props.py whose contract is
CPython `re`). `unicodedata` has no `script()`, so the SOURCE is the committed UCD data file tools/ucd/Scripts.txt
(version-pinned, parsed directly). Every assigned code point has exactly one Script, so the natural, compact
representation is a single sorted table of `{lo, hi, script}` ranges partitioning the code space (gaps are the
`Unknown` script) — ranges-first, one binary search, not 170 separate arrays.

Two checks: the parse is self-consistent (sorted, disjoint, coalesced); and, when the third-party `regex` module
is importable (a gen-time cross-oracle — `pip install regex` in a venv), every code point's parsed script is
re-verified against `regex`'s own `\\p{sc=Name}` over the whole code space, aborting on any disagreement. The
`regex` check does not affect the emitted bytes (the parse is the source), so the regen guard stays byte-exact
without it. The emitted `..._unidata_version` pins the Scripts.txt version.

`script_aliases` carries BOTH the long name (`Latin`, from Scripts.txt itself) and the short UAX24/ISO 15924
code (`Latn`, from the committed tools/ucd/PropertyValueAliases.txt `sc ;` lines) for every script, loose-
keyed, resolving to the same `script` value -- so `\\p{sc=Latn}` and `\\p{sc=Latin}` are the same lookup.
Short codes are required by `\\p{scx=...}` (gen_unicode_scx_tables.py; ScriptExtensions.txt states its
overrides in short codes only) and, since the table is shared, `\\p{sc=...}` gains PCRE2/rust-crate parity
on the short form as a direct consequence, not a separate change.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _gen_common as common  # noqa: E402

_MAX_CP = 0x10FFFF
_SURROGATE_LO = 0xD800
_SURROGATE_HI = 0xDFFF
_SCRIPTS_TXT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ucd", "Scripts.txt")
_PROPERTY_VALUE_ALIASES_TXT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ucd",
                                           "PropertyValueAliases.txt")
_LINE = re.compile(r"^([0-9A-Fa-f]{4,6})(?:\.\.([0-9A-Fa-f]{4,6}))?\s*;\s*(\w+)")
_VERSION = re.compile(r"^#\s*Scripts-([0-9.]+)\.txt")
_PVA_VERSION = re.compile(r"^#\s*PropertyValueAliases-([0-9.]+)\.txt")


def parse_sc_short_codes(path=_PROPERTY_VALUE_ALIASES_TXT):
    """Parse PropertyValueAliases.txt's `sc ; Short ; Long` lines into {long_name: short_code} and the data
    version. Public (no leading underscore): gen_unicode_scx_tables.py imports this directly rather than
    re-parsing the file, so the two generators can never disagree on the mapping."""
    version = None
    short_of = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if version is None:
                m = _PVA_VERSION.match(line)
                if m:
                    version = m.group(1)
            data = line.split("#", 1)[0].strip()
            if not data.startswith("sc ;"):
                continue
            fields = [p.strip() for p in data.split(";")]
            if len(fields) < 3:
                sys.exit(f"gen_unicode_script_tables: unparsable PropertyValueAliases.txt sc line: {line!r}")
            short_of[fields[2]] = fields[1]  # long -> short (ignore any further legacy aliases, e.g. Qaac)
    if version is None:
        sys.exit("gen_unicode_script_tables: PropertyValueAliases.txt is missing its version header")
    return short_of, version


def _loose(name):
    """UAX44-LM3-ish loose match key: lowercase, drop spaces / underscores / hyphens."""
    return name.lower().replace("_", "").replace("-", "").replace(" ", "")


def _parse_scripts_txt(path):
    """Parse Scripts.txt into a sorted, coalesced list of (lo, hi, script_name), and the data version string."""
    version = None
    entries = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if version is None:
                m = _VERSION.match(line)
                if m:
                    version = m.group(1)
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            m = _LINE.match(line)
            if not m:
                sys.exit(f"gen_unicode_script_tables: unparsable Scripts.txt line: {line!r}")
            lo = int(m.group(1), 16)
            hi = int(m.group(2), 16) if m.group(2) else lo
            entries.append((lo, hi, m.group(3)))
    if version is None:
        sys.exit("gen_unicode_script_tables: Scripts.txt is missing its `# Scripts-<version>.txt` header")
    entries.sort()
    # coalesce adjacent ranges of the same script (Scripts.txt sometimes splits them)
    merged = []
    for lo, hi, name in entries:
        if merged and merged[-1][2] == name and merged[-1][1] + 1 == lo:
            merged[-1] = (merged[-1][0], hi, name)
        else:
            merged.append((lo, hi, name))
    return merged, version


def _validate_structure(entries):
    """Sorted, disjoint, and no surrogate overlap — the invariants a partition must hold."""
    prev_hi = -1
    for lo, hi, name in entries:
        if lo > hi or lo <= prev_hi:
            sys.exit(f"gen_unicode_script_tables: table not sorted/disjoint near U+{lo:04X} ({name})")
        if lo <= _SURROGATE_HI and hi >= _SURROGATE_LO:
            sys.exit(f"gen_unicode_script_tables: range U+{lo:04X}..U+{hi:04X} overlaps the surrogate block")
        prev_hi = hi


def _cross_check_regex(entries):
    """Gen-time cross-oracle: every code point's parsed script must equal the `regex` module's, if importable."""
    try:
        import regex  # noqa: PLC0415 - optional, gen-time only
    except ImportError:
        print("gen_unicode_script_tables: `regex` module absent, skipping the cross-oracle (parse-only).", file=sys.stderr)
        return
    matchers = {}

    def _script_of(cp):
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

    def _check(cp):
        name = _script_of(cp)
        if name == "Unknown":
            return None  # gaps are Unknown; the regex crate agrees but we do not tabulate Unknown
        matcher = matchers.get(name)
        if matcher is None:
            matcher = matchers.setdefault(name, regex.compile(rf"\p{{sc={name}}}"))
        return f"U+{cp:04X} parsed {name}" if not matcher.match(chr(cp)) else None

    common.validate_exhaustive(
        (cp for cp in range(0, _MAX_CP + 1) if not (_SURROGATE_LO <= cp <= _SURROGATE_HI)),
        _check, lambda n: sys.exit(f"gen_unicode_script_tables: {n} code point(s) disagree with the regex module"))


def _emit(entries, version, short_of, path):
    names = sorted({name for _, _, name in entries})
    enum = ["Unknown"] + names  # Unknown = 0, the script of every gap
    out = common.file_header(
        filename="unicode_script.hpp",
        brief="Unicode Script ranges for `\\p{sc=...}` (UCD contract, parsed from tools/ucd/Scripts.txt).",
        generator="gen_unicode_script_tables.py",
        doc_lines=[
            "Every assigned code point has exactly one Script, so this is one sorted `{lo, hi, script}` table",
            "partitioning the code space (gaps are `Unknown`). Parsed from the committed Scripts.txt and, at",
            f"generation, cross-checked against the `regex` module. Scripts data version: {version} (asserted).",
            "Wired at ast.hpp::resolve_property (`\\p{sc=...}` / a bare name / `\\p{scx=...}`), via the",
            "generated, loose-keyed `resolve_script`, which resolves both the long name (`Latin`) and the",
            "short UAX24/ISO 15924 code (`Latn`) -- see tools/ucd/PropertyValueAliases.txt.",
        ],
        guard="REAL_UNICODE_SCRIPT_HPP",
        includes=[
            "#include <cstddef>",
            "#include <cstdint>",
            "#include <string_view>",
        ],
        version_kind="tables",
        version_const="unicode_script_unidata_version")
    out += ["  /*!"]
    out += ["   * \\brief A Unicode Script value; `Unknown` (0) is every code point no script assigns."]
    out += ["   */"]
    out += ["  enum class script : std::uint8_t {"]
    out += [f"    {name}," for name in enum]
    out += ["    count", "  };", ""]
    out += ["  /*! \\brief One code-point range and the Script it belongs to (the table partitions the code space). */"]
    out += ["  struct script_range"]
    out += ["  {"]
    out += ["    char32_t lo; //!< First code point of the range."]
    out += ["    char32_t hi; //!< Last code point of the range, inclusive."]
    out += ["    script   sc; //!< The Script every code point in it belongs to."]
    out += ["  };", ""]
    out += [f"  //! \\brief Script partition — {len(entries)} ranges, sorted and disjoint."]
    out += ["  inline constexpr script_range script_ranges[] {"]
    out += [f"    {{0x{lo:04X}, 0x{hi:04X}, script::{name}}}," for lo, hi, name in entries]
    out += ["  };", ""]
    out += ["  /*!"]
    out += ["   * \\brief The Script of \\p cp (binary search; `Unknown` when no range covers it)."]
    out += ["   * \\param[in] cp The code point to look up."]
    out += ["   * \\return Its Script, or `script::Unknown` when no range covers it."]
    out += ["   */"]
    out += ["  constexpr script script_of(char32_t cp)"]
    out += ["  {"]
    out += ["    std::size_t lo {0};"]
    out += ["    std::size_t hi {sizeof(script_ranges) / sizeof(script_ranges[0])};"]
    out += ["    while (lo < hi) {"]
    out += ["      const std::size_t mid {lo + ((hi - lo) / 2)};"]
    out += ["      if (script_ranges[mid].hi < cp) {"]
    out += ["        lo = mid + 1;"]
    out += ["      }"]
    out += ["      else {"]
    out += ["        hi = mid;"]
    out += ["      }"]
    out += ["    }"]
    out += ["    const std::size_t n {sizeof(script_ranges) / sizeof(script_ranges[0])};"]
    out += ["    if (lo < n && cp >= script_ranges[lo].lo && cp <= script_ranges[lo].hi) {"]
    out += ["      return script_ranges[lo].sc;"]
    out += ["    }"]
    out += ["    return script::Unknown;"]
    out += ["  }", ""]
    out += ["  /*!"]
    out += ["   * \\brief Whether \\p cp belongs to Script \\p sc (== the UCD)."]
    out += ["   * \\param[in] sc The Script to test for."]
    out += ["   * \\param[in] cp The code point to test."]
    out += ["   * \\return Whether \\ref script_of answers \\p sc. Exclusive: a code point has exactly one Script."]
    out += ["   */"]
    out += ["  constexpr bool is_script_cp(script sc, char32_t cp) { return script_of(cp) == sc; }", ""]
    out += ["  /*! \\brief A loose-normalized (lowercase, no _/-/space) Script name and its value. */"]
    out += ["  struct script_alias_entry"]
    out += ["  {"]
    out += ["    std::string_view name; //!< The loose-normalized name (long name or short UAX24 code)."]
    out += ["    script           sc;   //!< The Script it names."]
    out += ["  };", ""]
    out += ["  //! \\brief Script names, loose-keyed; for the `\\p{sc=...}` / `\\p{scx=...}` parsers. Both the"]
    out += ["  //!        long name (`Latin`) and the short UAX24/ISO 15924 code (`Latn`) resolve to the same"]
    out += ["  //!        value -- `\\p{scx=...}` states its overrides in short codes only, and since this"]
    out += ["  //!        table is shared, `\\p{sc=...}` gains the short form too."]
    out += ["  inline constexpr script_alias_entry script_aliases[] {"]
    # A short code that coincides with its own script's long name (Thai, Lisu, Cham, ...) would otherwise
    # duplicate the row -- harmless (both resolve to the same value) but noise in the generated table.
    aliases = sorted({(_loose(n), n) for n in names} | {(_loose(short_of[n]), n) for n in names})
    out += [f'    {{"{key}", script::{n}}},' for key, n in aliases]
    out += ["  };", ""]
    out += ["  /*!"]
    out += ["   * \\brief Resolve a loose-normalized Script name to its value, or `count` if unknown."]
    out += ["   * \\param[in] loose A name already put through the parser's loose normalization."]
    out += ["   * \\return The Script, or `script::count` when no alias matches."]
    out += ["   */"]
    out += ["  constexpr script resolve_script(std::string_view loose)"]
    out += ["  {"]
    out += ["    for (const script_alias_entry& a : script_aliases) {"]
    out += ["      if (a.name == loose) {"]
    out += ["        return a.sc;"]
    out += ["      }"]
    out += ["    }"]
    out += ["    return script::count;"]
    out += ["  }", ""]
    out += common.file_footer("REAL_UNICODE_SCRIPT_HPP")
    common.write_lines(path, out, f"wrote {path}: {len(enum)} scripts, {len(entries)} ranges, Scripts {version}")


def generate(path):
    entries, version = _parse_scripts_txt(_SCRIPTS_TXT)
    _validate_structure(entries)
    _cross_check_regex(entries)
    short_of, pva_version = parse_sc_short_codes()
    names = sorted({name for _, _, name in entries})
    missing = [n for n in names if n not in short_of]
    if missing:
        sys.exit(f"gen_unicode_script_tables: no short UAX24 code for: {missing} "
                  f"(PropertyValueAliases.txt out of sync with Scripts.txt?)")
    # Cross-script loose-key collision guard: a short code (or long name) accidentally equal to a
    # DIFFERENT script's key would silently misroute a `\p{sc=...}` lookup -- catch it at generation, not
    # at a user's runtime surprise. A script's OWN short code coinciding with its OWN long name (Thai,
    # Lisu, Cham, ... -- the English name already IS the 4-letter code) is not a collision: both keys
    # resolve to the same value, so it is just a harmless duplicate row, not ambiguity.
    key_owner = {}
    collisions = []
    for n in names:
        for key in (_loose(n), _loose(short_of[n])):
            owner = key_owner.setdefault(key, n)
            if owner != n:
                collisions.append((key, owner, n))
    if collisions:
        sys.exit(f"gen_unicode_script_tables: cross-script loose-key collision(s): {collisions}")
    if pva_version != version:
        sys.exit(f"gen_unicode_script_tables: source version mismatch: Scripts.txt={version} "
                  f"PropertyValueAliases.txt={pva_version}")
    _emit(entries, version, short_of, path)


def main():
    generate(sys.argv[1] if len(sys.argv) > 1 else "include/real/unicode/unicode_script.hpp")


if __name__ == "__main__":
    main()
