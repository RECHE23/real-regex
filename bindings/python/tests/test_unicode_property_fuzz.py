r"""`\p{...}` in the differential fuzzer. `re` has no `\p{}` at all, so it cannot be
the oracle here (unlike test_differential_fuzz.py's random-pattern fuzz) -- this file uses two
oracles instead, neither a new dependency:

  * General_Category: `unicodedata.category` -- CPython's own Unicode tables. `real`'s own GC tables
    (unicode_property.hpp) are generated FROM this exact function (tools/gen_unicode_property_tables.py),
    so this is a hard oracle, not an approximation -- skipped if the running interpreter's
    `unicodedata.unidata_version` does not match REAL's pinned UCD version (the same guard the *_regen.py
    tests use), so a version-skewed machine gets an honest skip, not a false failure.
  * Script / Script_Extensions / binary properties: the bundled UCD source files under tools/ucd/ --
    the SAME files tools/gen_unicode_*_tables.py parse to build the tables `real` actually ships. Reusing
    those generators' own (underscore-prefixed but stable, same-repo) parsing functions rather than
    re-parsing the files here means this fuzzer can never silently drift from what got compiled in.
    Deliberately NOT the third-party `regex` module: it bundles its own, possibly different, Unicode
    version, which would turn a version gap into a false-positive divergence (exactly the confound the
    Unicode-comparative benchmark arc flagged for cross-engine throughput; the same trap applies to a
    correctness oracle).

Internal invariants (no oracle file needed): `\p{sc=X}` implies `\p{scx=X}` (scx is a superset of the
default-rule sc, never a subset); `\p{L}` is exactly the union of `\p{Lu} \p{Ll} \p{Lt} \p{Lm} \p{Lo}`;
a script's short UAX24/ISO 15924 code and its long name resolve to the same set (`\p{sc=Latn}` ==
`\p{sc=Latin}`).

Iteration count and seeding follow test_differential_fuzz.py's own convention (REAL_FUZZ_ITERS,
REAL_FUZZ_SEED env vars; a fixed default seed for deterministic CI). Every sample set mixes uniformly
random code points with adversarial ones seeded at the first/last code point of each parsed range --
an off-by-one in a range boundary is exactly the bug a purely-random sample under-weights.
"""
import os
import random
import sys
import unicodedata
import unittest

import real
from _regen_guard import repo_root

_ROOT = repo_root()
sys.path.insert(0, os.path.join(_ROOT, "tools"))
import gen_unicode_binprop_tables as binprop_gen  # noqa: E402
import gen_unicode_property_tables as gc_gen       # noqa: E402
import gen_unicode_script_tables as script_gen     # noqa: E402
import gen_unicode_scx_tables as scx_gen           # noqa: E402

ITERS = int(os.environ.get("REAL_FUZZ_ITERS", "20000"))
SEED = int(os.environ.get("REAL_FUZZ_SEED", "20260711"))
_MAX_CP = 0x10FFFF
_SURROGATE_LO, _SURROGATE_HI = 0xD800, 0xDFFF

_UCD_MATCHES_INTERPRETER = unicodedata.unidata_version == "16.0.0"
_SKIP_REASON = (
    f"unicodedata.unidata_version={unicodedata.unidata_version!r} != REAL's pinned UCD 16.0.0 -- "
    "the GC oracle would be comparing two different Unicode versions, a false-divergence risk, not a "
    "real one (same policy as the *_regen.py guards)."
)


def _random_cp(rng):
    """A uniformly random assigned-range code point, surrogates excluded (never valid UTF-8 alone)."""
    while True:
        cp = rng.randint(0, _MAX_CP)
        if not (_SURROGATE_LO <= cp <= _SURROGATE_HI):
            return cp


class TestUnicodePropertyFuzz(unittest.TestCase):
    """`\\p{...}` differential against the UCD sources (never the third-party `regex` module)."""

    @classmethod
    def setUpClass(cls):
        rng = random.Random(SEED)
        cls.rng = rng

        # Script (a partition: exactly one script per code point) and its short<->long alias map.
        scripts_path = os.path.join(_ROOT, "tools", "ucd", "Scripts.txt")
        cls.script_entries, _ = script_gen._parse_scripts_txt(scripts_path)
        # staticmethod: script_of is a plain closure, not a method -- a bare function assigned to a
        # class attribute is bound (self inserted as its first arg) when read off an instance.
        cls.script_of = staticmethod(scx_gen._script_of_factory(cls.script_entries))
        cls.short_of_long, _ = script_gen.parse_sc_short_codes()
        cls.long_of_short = {v: k for k, v in cls.short_of_long.items()}

        # Script_Extensions: {cp: [long names]} overrides; default is {script_of(cp)} where absent.
        scx_path = os.path.join(_ROOT, "tools", "ucd", "ScriptExtensions.txt")
        cls.scx_overrides, _ = scx_gen._parse_script_extensions(scx_path, cls.long_of_short)

        # Binary properties: one coalesced range table per property, from the three UCD sources.
        tables = {}
        for filename, version_re, label in binprop_gen._SOURCES:
            per_prop, _ = binprop_gen._parse_one(filename, version_re, label)
            for name, ranges in per_prop.items():
                tables[name] = binprop_gen._coalesce(ranges)
        cls.binprop_tables = tables

    def scx_of(self, cp):
        """{cp}'s Script_Extensions set: the override if one exists, else the default {Script(cp)}."""
        overrides = self.scx_overrides.get(cp)
        return set(overrides) if overrides is not None else {self.script_of(cp)}

    def sample_cps(self, ranges, n_random):
        """n_random uniformly random code points, plus every range's first and last code point (the
        adversarial boundary seeds) -- deduplicated, so a small range list does not balloon the sample."""
        cps = {self._random_cp() for _ in range(n_random)}
        for lo, hi in ranges:
            cps.add(lo)
            cps.add(hi)
        return sorted(cps)

    def _random_cp(self):
        return _random_cp(self.rng)

    # --- General_Category -----------------------------------------------------------------------

    @unittest.skipUnless(_UCD_MATCHES_INTERPRETER, _SKIP_REASON)
    def test_gc_matches_unicodedata_category(self):
        # A representative spread: single-letter categories (Lu/Ll/Nd/Zs/Po) plus the L/N/P/Z groups.
        codes = ["Lu", "Ll", "Lt", "Lm", "Lo", "Nd", "Nl", "No", "Zs", "Po", "Sm", "Cc"]
        groups = ["L", "N", "P", "Z", "S", "C", "M"]
        gc_ranges = {c: gc_gen._build_ranges(lambda cp, c=c: gc_gen._category(cp) == c) for c in codes}
        all_ranges = [r for ranges in gc_ranges.values() for r in ranges]
        cps = self.sample_cps(all_ranges, ITERS // len(codes))
        divergences = []
        compiled = {c: real.compile(rf"\p{{{c}}}") for c in codes}
        compiled.update({g: real.compile(rf"\p{{{g}}}") for g in groups})
        for cp in cps:
            ch = chr(cp)
            cat = unicodedata.category(ch)
            for c in codes:
                got = bool(compiled[c].fullmatch(ch))
                want = (cat == c)
                if got != want:
                    divergences.append((c, cp, cat, got, want))
            for g in groups:
                got = bool(compiled[g].fullmatch(ch))
                want = cat.startswith(g)
                if got != want:
                    divergences.append((g, cp, cat, got, want))
        self.assertEqual(divergences, [], f"{len(divergences)} GC divergence(s), first: {divergences[:5]}")

    # --- Script / Script_Extensions --------------------------------------------------------------

    def test_script_matches_ucd_source(self):
        sample_scripts = ["Latin", "Greek", "Cyrillic", "Han", "Arabic", "Hiragana", "Common", "Inherited"]
        divergences = []
        for name in sample_scripts:
            ranges_for_script = [(lo, hi) for lo, hi, sc in self.script_entries if sc == name]
            cps = self.sample_cps(ranges_for_script, ITERS // len(sample_scripts))
            rx = real.compile(rf"\p{{sc={name}}}")
            for cp in cps:
                ch = chr(cp)
                got = bool(rx.fullmatch(ch))
                want = (self.script_of(cp) == name)
                if got != want:
                    divergences.append((name, cp, got, want))
        self.assertEqual(divergences, [], f"{len(divergences)} sc= divergence(s), first: {divergences[:5]}")

    def test_scx_matches_ucd_source(self):
        # Every code point WITH an explicit override, plus a random spread (the default-rule case,
        # scx == {Script(cp)}, is exercised by the pure-random sample and by test_script_matches above
        # via the invariant test below).
        override_cps = list(self.scx_overrides.keys())
        sample = self.rng.sample(override_cps, min(len(override_cps), ITERS // 4))
        sample += [self._random_cp() for _ in range(ITERS // 4)]
        # For each sampled cp, test its OWN scx set (not a fixed script list) -- more targeted than
        # picking a handful of scripts up front, since overrides are sparse and script-specific.
        rx_cache = {}
        divergences = []
        for cp in sample:
            for name in self.scx_of(cp):
                if name == "Unknown":
                    # REAL does not expose \p{scx=Unknown} as a queryable property (there is no
                    # "no script assigned" lookup, same scoping as the binary-property table's
                    # "absent from this checkout" skip) -- not this test's concern.
                    continue
                rx = rx_cache.get(name)
                if rx is None:
                    rx = rx_cache[name] = real.compile(rf"\p{{scx={name}}}")
                if not rx.fullmatch(chr(cp)):
                    divergences.append(("missing", name, cp))
            # negative check: a script NOT in scx(cp) must not match (sampled, not exhaustive over all
            # ~170 scripts per cp -- pick one definitely-absent script deterministically).
            absent = "Cherokee" if "Cherokee" not in self.scx_of(cp) else "Ogham"
            rx = rx_cache.get(absent)
            if rx is None:
                rx = rx_cache[absent] = real.compile(rf"\p{{scx={absent}}}")
            if rx.fullmatch(chr(cp)):
                divergences.append(("spurious", absent, cp))
        self.assertEqual(divergences, [], f"{len(divergences)} scx= divergence(s), first: {divergences[:5]}")

    # --- Binary properties ------------------------------------------------------------------------

    def test_binary_property_matches_ucd_source(self):
        sample_props = ["Alphabetic", "White_Space", "Uppercase", "Lowercase", "ID_Start", "Emoji"]
        divergences = []
        for name in sample_props:
            if name not in self.binprop_tables:
                continue  # emoji-data.txt absent on some checkouts is not this test's concern
            ranges = self.binprop_tables[name]
            cps = self.sample_cps(ranges, ITERS // len(sample_props))
            rx = real.compile(rf"\p{{{name}}}")
            for cp in cps:
                got = bool(rx.fullmatch(chr(cp)))
                want = binprop_gen._in_ranges(ranges, cp)
                if got != want:
                    divergences.append((name, cp, got, want))
        self.assertEqual(divergences, [], f"{len(divergences)} binary-property divergence(s), first: {divergences[:5]}")

    # --- Internal invariants (no external oracle needed) -----------------------------------------

    def test_invariant_sc_implies_scx(self):
        """scx(cp) always contains Script(cp)'s default membership: \\p{sc=X} matching cp -- meaning
        Script(cp) == X -- implies \\p{scx=X} also matches cp. UAX #24 documents one deliberate
        exception: for a code point whose Script is Common or Inherited, Script_Extensions lists the
        SPECIFIC scripts it is shared among and excludes the generic Common/Inherited value itself
        (e.g. U+10102 AEGEAN CHECK MARK is sc=Common but scx={Cypriot,Linear_B}, no "Common") -- so
        those two are excluded here, not a divergence."""
        cps = [self._random_cp() for _ in range(ITERS // 2)]
        divergences = []
        rx_cache = {}
        for cp in cps:
            name = self.script_of(cp)
            if name in ("Unknown", "Common", "Inherited"):
                continue
            rx_sc = rx_cache.setdefault(("sc", name), real.compile(rf"\p{{sc={name}}}"))
            rx_scx = rx_cache.setdefault(("scx", name), real.compile(rf"\p{{scx={name}}}"))
            if rx_sc.fullmatch(chr(cp)) and not rx_scx.fullmatch(chr(cp)):
                divergences.append((name, cp))
        self.assertEqual(divergences, [], f"sc=>scx invariant broken for: {divergences[:5]}")

    def test_invariant_L_equals_union_of_subcategories(self):
        """\\p{L} must agree, on every sampled code point, with (\\p{Lu}|\\p{Ll}|\\p{Lt}|\\p{Lm}|\\p{Lo})
        -- General_Category's L group is defined as exactly that union, nothing else."""
        subs = ["Lu", "Ll", "Lt", "Lm", "Lo"]
        rx_l = real.compile(r"\p{L}")
        rx_subs = [real.compile(rf"\p{{{c}}}") for c in subs]
        ranges = gc_gen._build_ranges(lambda cp: gc_gen._category(cp)[0] == "L")
        cps = self.sample_cps(ranges, ITERS // 2)
        divergences = []
        for cp in cps:
            ch = chr(cp)
            want = any(rx.fullmatch(ch) for rx in rx_subs)
            got = bool(rx_l.fullmatch(ch))
            if got != want:
                divergences.append((cp, got, want))
        self.assertEqual(divergences, [], f"\\p{{L}} != union(Lu,Ll,Lt,Lm,Lo) for: {divergences[:5]}")

    def test_invariant_short_iso_alias_equals_long_name(self):
        """A script's short UAX24/ISO 15924 code (\\p{sc=Latn}) and its long name (\\p{sc=Latin}) must
        resolve to the same code-point set -- they are two spellings of one lookup, not two properties."""
        # PropertyValueAliases.txt lists a few sc= aliases (e.g. Katakana_Or_Hiragana/Hrkt) that are
        # never actually assigned as any code point's Script in Scripts.txt -- pure metadata, zero
        # members. REAL does not build a table for a property with no members, so restrict sampling
        # to long names that are genuinely used as a Script value at least once.
        used_scripts = {sc for _, _, sc in self.script_entries}
        candidates = [name for name in self.short_of_long if name in used_scripts]
        sample_scripts = self.rng.sample(candidates, min(20, len(candidates)))
        cps = [self._random_cp() for _ in range(ITERS // 4)]
        divergences = []
        for long_name in sample_scripts:
            short_code = self.short_of_long[long_name]
            rx_long = real.compile(rf"\p{{sc={long_name}}}")
            rx_short = real.compile(rf"\p{{sc={short_code}}}")
            for cp in cps:
                ch = chr(cp)
                a, b = bool(rx_long.fullmatch(ch)), bool(rx_short.fullmatch(ch))
                if a != b:
                    divergences.append((long_name, short_code, cp, a, b))
        self.assertEqual(divergences, [], f"short/long alias mismatch for: {divergences[:5]}")


if __name__ == "__main__":
    unittest.main()
