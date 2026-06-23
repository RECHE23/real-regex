"""Parity suite: real and re must produce identical results on a corpus.

Patterns avoid the single documented divergence (capture of a nullable
loop's final empty iteration) and use re.ASCII so re's classes match REAL's
ASCII class semantics.
"""

import re
import unittest

import real

PATTERNS = [
    r"needle",
    r"\d+",
    r"\d{4}-\d{2}-\d{2}",
    r"(\d{4})-(\d{2})-(\d{2})",
    r"(?P<user>\w+)@(?P<host>\w+)\.(\w+)",
    r"[a-z]+[0-9]?",
    r"(cat|dog|bird)s?",
    r"a|ab|abc",
    r"^\w+",
    r"\w+$",
    r"\bcat\b",
    r"x*",
    r"a?b+c*",
    r"(a)|(b)|(c)",
    r"\s+",
    r"[^,]+",
    r".",
    r".+?,",
    r"(ab){2,3}",
    r"colou?r",
    r"\A\w+",
    r"(a(b)?)c",
]

TEXTS = [
    "",
    "the needle in the haystack",
    "dates: 2026-06-10 and 1999-12-31!",
    "bob@example.com, alice@test.org",
    "cats and dogs and birds",
    "abc abcabc a ab",
    "line one\nline two\nline three",
    "  spaced   out  ",
    "csv,values,here,,empty",
    "color colour colouur",
    "café au lait, naïve résumé",  # non-ASCII subjects
    "ababab abab",
    "a" * 200 + "b",
]

FLAG_SETS = [
    (0, re.ASCII),
    (real.I, re.ASCII | re.IGNORECASE),
    (real.M, re.ASCII | re.MULTILINE),
    (real.S, re.ASCII | re.DOTALL),
]


class TestParity(unittest.TestCase):
    """Compare real and Python re on a shared corpus."""

    def for_all(self, check):
        """Run ``check`` against every pattern/text/flag combination."""
        for pattern in PATTERNS:
            for text in TEXTS:
                for real_flags, re_flags in FLAG_SETS:
                    with self.subTest(pattern=pattern, text=text[:20],
                                      flags=real_flags):
                        check(real.compile(pattern, real_flags),
                              re.compile(pattern, re_flags), text)

    @staticmethod
    def match_facts(m):
        """Extract comparable facts from a Match object.

        Args:
            m (Match or None): A match object from either real or re.

        Returns:
            tuple or None: (full span, groups, per-group spans) or None.
        """
        if m is None:
            return None
        return (m.span(), m.groups(),
                tuple(m.span(g) for g in range(1, (m.re.groups or 0) + 1)))

    def test_search_parity(self):
        """search() yields identical spans/groups."""
        self.for_all(lambda p, r, t: self.assertEqual(
            self.match_facts(p.search(t)), self.match_facts(r.search(t))))

    def test_match_parity(self):
        """match() yields identical spans/groups."""
        self.for_all(lambda p, r, t: self.assertEqual(
            self.match_facts(p.match(t)), self.match_facts(r.match(t))))

    def test_fullmatch_parity(self):
        """fullmatch() yields identical spans/groups."""
        self.for_all(lambda p, r, t: self.assertEqual(
            self.match_facts(p.fullmatch(t)), self.match_facts(r.fullmatch(t))))

    def test_findall_parity(self):
        """findall() yields identical results."""
        self.for_all(lambda p, r, t: self.assertEqual(p.findall(t), r.findall(t)))

    def test_finditer_spans_parity(self):
        """finditer() yields identical spans."""
        self.for_all(lambda p, r, t: self.assertEqual(
            [m.span() for m in p.finditer(t)], [m.span() for m in r.finditer(t)]))

    def test_split_parity(self):
        """split() yields identical results."""
        self.for_all(lambda p, r, t: self.assertEqual(p.split(t), r.split(t)))

    # Large subjects take the GIL-released two-phase findall/split path (spans collected
    # without the GIL, Python objects built under it). The corpus above maxes at ~200 B,
    # so it only exercises the small interleaved path; these cases repeat a unit well
    # past the two-phase threshold to pin that path against re. Nullable / zero-width
    # patterns are the classic trap (empty matches plus the 3.7+ advance), and the
    # multi-group case exercises the flat span buffer's stride. All avoid the one
    # documented divergence (capture of a nullable loop's final empty iteration), so re
    # and REAL must agree exactly.
    LARGE_UNITS = [
        (r"a*", "xaay"),                  # nullable: empties interleave with runs
        (r"\w*", "ab cd  ef "),           # nullable across whitespace gaps
        (r"x*", "axxbxc"),                # nullable, sparse matches
        (r"\d{0,2}", "12 3 456 78 "),     # bounded nullable
        (r",", "a,b,,c,"),                # separators: many matches incl. empties
        (r"(\w+)@(\w+)", "bob@ex alice@te "),  # multi-group: exercises buffer stride
        (r"", "abcde"),                   # zero-width everywhere (empty pattern)
        (r"\b", "ab cd ef "),             # zero-width at word boundaries
    ]

    def test_large_subject_parity(self):
        """findall/split over large subjects (two-phase path) match re, including
        maxsplit (which early-stops the span-collection phase)."""
        for pattern, unit in self.LARGE_UNITS:
            text = unit * (16 * 1024 // len(unit) + 1)  # ~16 KB: well past the threshold
            self.assertGreaterEqual(len(text.encode()), 8192)
            p, r = real.compile(pattern), re.compile(pattern, re.ASCII)
            with self.subTest(pattern=pattern, n=len(text)):
                self.assertEqual(p.findall(text), r.findall(text))
                self.assertEqual(p.split(text), r.split(text))
                self.assertEqual(p.split(text, 3), r.split(text, 3))

    def test_sub_parity(self):
        """sub() with a literal replacement yields identical results."""
        self.for_all(lambda p, r, t: self.assertEqual(p.sub("#", t), r.sub("#", t)))

    def test_sub_with_group_refs_parity(self):
        """sub() with back-references yields identical results."""
        for pattern, repl in [(r"(\w+)@(\w+)", r"\2/\1"),
                              (r"(?P<a>\d)(\d)", r"\g<a>-\2")]:
            for text in TEXTS:
                with self.subTest(pattern=pattern, text=text[:20]):
                    self.assertEqual(
                        real.compile(pattern).sub(repl, text),
                        re.compile(pattern, re.ASCII).sub(repl, text))

    def test_subn_parity(self):
        """subn() returns the same (string, count) pair as re — pins the shared
        non-callable template path through the counted variant too."""
        for pattern, repl in [(r"(\w+)@(\w+)", r"\2/\1"), (r"\s+", "_")]:
            for text in TEXTS:
                with self.subTest(pattern=pattern, text=text[:20]):
                    self.assertEqual(
                        real.compile(pattern).subn(repl, text),
                        re.compile(pattern, re.ASCII).subn(repl, text))

    def test_expand_parity(self):
        r"""Match.expand reproduces re.Match.expand across templates: \1, \g<name>,
        \g<1>, \g<0> (the whole match), escapes, empty, and repeated references."""
        cases = [
            (r"(\w+)@(\w+)", "bob@example", r"\2/\1"),
            (r"(\w+)@(\w+)", "bob@example", r"\g<2>/\g<1>"),     # \g<1>, \g<2>
            (r"(?P<u>\w+)@(?P<h>\w+)", "bob@example", r"\g<u> at \g<h>"),
            (r"(\d+)", "abc 42 xyz", r"[\g<0>]"),                # \g<0> = whole match
            (r"(\d+)", "abc 42 xyz", r"n=\1 raw=\\ end"),        # \\ -> one backslash
            (r"(\w+)", "hi", "line1\\nline2\\t\\1"),             # \n, \t escapes
            (r"(\w+)", "hi", r""),                               # empty template
            (r"(\w)(\w)(\w)", "abc", r"\3\2\1\3\2\1"),           # repeated references
            (r"(\w+)", "hi", r"!:/@%=,\1"),                      # non-escaped punctuation
        ]
        for pattern, text, template in cases:
            with self.subTest(pattern=pattern, template=template):
                pm, rm = (real.compile(pattern).search(text),
                          re.compile(pattern, re.ASCII).search(text))
                self.assertIsNotNone(pm)
                self.assertIsNotNone(rm)
                self.assertEqual(pm.expand(template), rm.expand(template))

    def test_expand_combined_template_parity(self):
        r"""A single template mixing \g<0>, \1 and a literal with backslashes."""
        pattern, text, template = r"(\w+)", "word", r"[\g<0>]=\1\\done"
        pm = real.compile(pattern).search(text)
        rm = re.compile(pattern, re.ASCII).search(text)
        self.assertEqual(pm.expand(template), rm.expand(template))

    def test_expand_nonparticipating_group_parity(self):
        """A template touching a group that did not participate matches re — the
        group contributes nothing (the shared sub/expand semantics)."""
        pattern, text = r"(a)|(b)", "a"  # group 2 is unset on "a"
        for template in [r"[\1][\2]", r"\g<2>", r"x\2y\1"]:
            with self.subTest(template=template):
                pm = real.compile(pattern).search(text)
                rm = re.compile(pattern, re.ASCII).search(text)
                self.assertEqual(pm.expand(template), rm.expand(template))

    def test_expand_bytes_parity(self):
        r"""Match.expand on a bytes pattern with a bytes template."""
        pattern, text, template = rb"(\w+)=(\w+)", b"k=v", rb"\2:\1:[\g<0>]"
        pm = real.compile(pattern).search(text)
        rm = re.compile(pattern).search(text)
        self.assertEqual(pm.expand(template), rm.expand(template))

    def test_expand_non_ascii_offsets_parity(self):
        r"""expand resolves byte offsets correctly when non-ASCII text precedes the
        match (byte offset != character offset)."""
        pattern, text, template = r"(\d+)-(\d+)", "café 12-34", r"\2-\1 [\g<0>]"
        pm = real.compile(pattern).search(text)
        rm = re.compile(pattern, re.ASCII).search(text)
        self.assertIsNotNone(pm)
        self.assertEqual(pm.expand(template), rm.expand(template))

    def test_expand_from_search_and_finditer_parity(self):
        r"""expand works on a match from search AND from each finditer match."""
        pattern, text, template = r"(\w)(\d)", "a1 b2 c3", r"\2\1"
        rp, rr = real.compile(pattern), re.compile(pattern, re.ASCII)
        self.assertEqual(rp.search(text).expand(template),
                         rr.search(text).expand(template))
        self.assertEqual([m.expand(template) for m in rp.finditer(text)],
                         [m.expand(template) for m in rr.finditer(text)])

    def test_expand_errors(self):
        r"""Wrong template type raises TypeError; an out-of-range group reference
        raises (like sub) — for both real and re."""
        sm = real.compile(r"(\w+)").search("hi")
        with self.assertRaises(TypeError):
            sm.expand(b"\\1")                       # bytes template on a str pattern
        bm = real.compile(rb"(\w+)").search(b"hi")
        with self.assertRaises(TypeError):
            bm.expand("\\1")                        # str template on a bytes pattern
        with self.assertRaises(re.error):
            re.compile(r"(\w+)").search("hi").expand(r"\99")
        with self.assertRaises(real.error):
            sm.expand(r"\99")                       # out-of-range group, like sub

    def test_bytes_parity(self):
        """Bytes patterns behave identically to re on bytes subjects."""
        for pattern in [rb"\d+", rb"(\w+)=(\w+)", rb"[^;]+"]:
            for text in [b"a=1;b=22;c=333", b"", b"\x00\xffraw bytes 42"]:
                with self.subTest(pattern=pattern, text=text):
                    p, r = real.compile(pattern), re.compile(pattern)
                    self.assertEqual(p.findall(text), r.findall(text))
                    self.assertEqual(
                        self.match_facts(p.search(text)),
                        self.match_facts(r.search(text)))

    def test_escape_parity_per_char(self):
        """real.escape agrees with re.escape on every ASCII char — proving the
        CPython 3.7+ semantics (only the special set is escaped; punctuation such
        as ! : / @ % = , is NOT, unlike pre-3.7 re.escape)."""
        for code in range(128):
            ch = chr(code)
            with self.subTest(code=code):
                self.assertEqual(real.escape(ch), re.escape(ch))

    def test_escape_parity_samples(self):
        """real.escape matches re.escape on representative strings, including the
        non-special punctuation, the regex metacharacters, and non-ASCII text."""
        samples = [
            "", "abc_123", "a.b*c+", "!:/@%=,", "a b\tc\n", "(x|y)?",
            "[a-z]{2,3}", "$^\\.|", "# comment ~&", "héllo", "naïve—world",
        ]
        for sample in samples:
            with self.subTest(sample=sample):
                self.assertEqual(real.escape(sample), re.escape(sample))

    def test_escape_parity_bytes(self):
        """real.escape matches re.escape for bytes patterns, on every byte value
        and on representative byte strings."""
        for value in range(256):
            raw = bytes([value])
            with self.subTest(value=value):
                self.assertEqual(real.escape(raw), re.escape(raw))
        for raw in [b"", b"a.b*c+", b"!:/@%=,", b"\x00\xff (raw) ~&#"]:
            with self.subTest(raw=raw):
                self.assertEqual(real.escape(raw), re.escape(raw))


if __name__ == "__main__":
    unittest.main()
