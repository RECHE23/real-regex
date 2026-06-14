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


if __name__ == "__main__":
    unittest.main()
