"""Parity suite: real and re must produce identical results on a corpus.

Patterns avoid the single documented divergence (capture of a nullable
loop's final empty iteration). The re oracle flags are chosen per pattern by
_text_oracle: \\w \\d \\s are Unicode in text mode, \\b \\B stay ASCII.
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
    # UTF-8 literals in code-point mode: a raw multi-byte char is one atom, so the quantifier
    # spans the whole code point (the é+ bug). re is the code-point oracle.
    r"é+",
    r"café",
    r"résumé",
    r"na.ve",
    r"caf.",
    r".{3}",
    r"€+",
    r"[a-z]é",
    r"(é)(é)",
    r"coûte",
    r"😀+",
    # UTF-8 character classes (U2): specific code points / ranges / negation, re the oracle.
    r"[é]",
    r"[éàü]",
    r"[à-ÿ]",
    r"[a-zé]",
    r"[a-é]",
    r"[^é]",
    r"[^à-ÿ]",
    r"[éà]+",
    r"[Ā-ſ]+",
    r"gr[éè]ce",
    # icase cross-boundary (CF2): ASCII literals/classes fold to non-ASCII partners under re.I.
    r"k",
    r"[ks]",
    r"straße",
    r"σ+",
    r"MASSE",
    # \w \d \s are Unicode in text mode (\b stays ASCII); oracle switches per pattern.
    r"\s",
    r"\S+",
    r"\D",
    r"[\d]+",
    r"[^\d]",
    r"\w",
    r"\w+",
    r"\W+",
    r"(\w+)\s+(\w+)",
    r"\bété\b",
    r"\b\w+\b",
    r"\B\w",
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
    "ça coûte €12, résumé 😀😀 ok",
    "ééé and éé and é",
    "ababab abab",
    "a" * 200 + "b",
    "Kelvin K k ſ S s İ ı I i ß ẞ σ Σ ς masse maße",  # icase fold partners (Kelvin, long-s, dotted-i…)
    "a٣b5 x y z ٠١٢ ９ é",  # Unicode digits (Arabic ٣, fullwidth ９) + spaces (NBSP, U+2028)
]

FLAG_SETS = [
    (0, re.ASCII),                       # oracle refined per-pattern (see _text_oracle)
    (real.I, re.ASCII | re.IGNORECASE),  # idem
    (real.M, re.ASCII | re.MULTILINE),
    (real.S, re.ASCII | re.DOTALL),
]

# In text (str) mode every shorthand and boundary is Unicode now (\w \W \d \D \s \S \b \B \< \>), and
# case folding is full Unicode, so a str-mode real pattern is compared against re with no re.ASCII.
def _text_oracle(pattern, base):  # noqa: ARG001 - pattern kept for call-site symmetry
    """The re flags to compare a text-mode real pattern against (base = 0 or re.IGNORECASE)."""
    return base  # full Unicode, matching re's str default


class TestParity(unittest.TestCase):
    """Compare real and Python re on a shared corpus."""

    # The non-ASCII re base flags corresponding to each real text-mode flag set.
    _BASE = {0: 0, real.I: re.IGNORECASE, real.M: re.MULTILINE, real.S: re.DOTALL}

    def for_all(self, check):
        """Run ``check`` against every pattern/text/flag combination."""
        for pattern in PATTERNS:
            for text in TEXTS:
                for real_flags, _ in FLAG_SETS:
                    re_flags = _text_oracle(pattern, self._BASE[real_flags])
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
            p, r = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))
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
                        re.compile(pattern, _text_oracle(pattern, 0)).sub(repl, text))

    def test_subn_parity(self):
        """subn() returns the same (string, count) pair as re — pins the shared
        non-callable template path through the counted variant too."""
        for pattern, repl in [(r"(\w+)@(\w+)", r"\2/\1"), (r"\s+", "_")]:
            for text in TEXTS:
                with self.subTest(pattern=pattern, text=text[:20]):
                    self.assertEqual(
                        real.compile(pattern).subn(repl, text),
                        re.compile(pattern, _text_oracle(pattern, 0)).subn(repl, text))

    def _assert_sub_parity(self, pattern, repl, text):
        """real.sub == re.sub, and if re rejects the template, REAL rejects it too."""
        rp = real.compile(pattern)
        rr = re.compile(pattern, 0 if isinstance(pattern, bytes) else _text_oracle(pattern, 0))
        try:
            expected = rr.sub(repl, text)
        except re.error:
            with self.assertRaises(real.error):
                rp.sub(repl, text)
            return
        self.assertEqual(rp.sub(repl, text), expected)

    def test_pattern_octal_escapes_parity(self):
        r"""Octal escapes in PATTERNS (twin of the template fix, shared decoder): \012 \101
        \123 \0 \00 \000 match the byte they encode, like re. str + bytes; ASCII values."""
        str_cases = [
            (r"\012", "\n"), (r"\101", "A"), (r"\123", "S"), (r"\0", "\x00"),
            (r"\00", "\x00"), (r"\000", "\x00"), (r"a\101b", "aAb"), (r"\060\061", "01"),
        ]
        for pattern, subject in str_cases:
            with self.subTest(pattern=pattern, kind="str"):
                self.assertEqual(self.match_facts(real.compile(pattern).search(subject)),
                                 self.match_facts(re.compile(pattern, _text_oracle(pattern, 0)).search(subject)))
        # \400 > 0o377 is an error in re; REAL rejects it too.
        with self.assertRaises(re.error):
            re.compile(r"\400")
        with self.assertRaises(real.error):
            real.compile(r"\400")
        for pattern, subject in [(rb"\012", b"\n"), (rb"\101", b"A"), (rb"\0", b"\x00"),
                                 (rb"\123", b"S")]:
            with self.subTest(pattern=pattern, kind="bytes"):
                self.assertEqual(self.match_facts(real.compile(pattern).search(subject)),
                                 self.match_facts(re.compile(pattern).search(subject)))

    def test_sub_octal_and_group_escapes_parity(self):
        r"""Replacement digit escapes follow CPython: \0-prefixed and all-octal three-digit
        runs are octal escapes, the rest are group references. Parity with re on str and bytes,
        error cases included (re raises -> REAL raises). \012 must give '\n', not group 0."""
        str_cases = [
            (r"x", r"\0"), (r"x", r"\00"), (r"x", r"\000"), (r"x", r"\012"),
            (r"x", r"\0377"), (r"x", r"\101"), (r"x", r"\200"), (r"x", r"a\0b"),
            (r"(x)", r"\1"), (r"(x)", r"\1\012"), (r"(x)", r"[\0\1]"),
            (r"(x)", r"\123"),                       # all-octal 3-digit -> octal, not group 123
            (r"x", r"\8"), (r"x", r"\9"), (r"(x)", r"\08"),
        ]
        for pattern, repl in str_cases:
            with self.subTest(pattern=pattern, repl=repl, kind="str"):
                self._assert_sub_parity(pattern, repl, "axbxc")
        byte_cases = [
            (rb"x", rb"\0"), (rb"x", rb"\012"), (rb"x", rb"\0377"), (rb"x", rb"\101"),
            (rb"x", rb"\200"), (rb"(x)", rb"\1"), (rb"(x)", rb"\123"), (rb"x", rb"\8"),
        ]
        for pattern, repl in byte_cases:
            with self.subTest(pattern=pattern, repl=repl, kind="bytes"):
                self._assert_sub_parity(pattern, repl, b"axbxc")

    def test_sub_large_subject_parity(self):
        """sub/subn on a large subject (past the GIL-release threshold, ~15 KB) are
        byte-identical to re — exercises the GIL-released non-callable path, including a
        count cap (the count-break inside run_template_sub)."""
        text = "word " * 3000  # ~15 KB, well past the 4 KB threshold
        for pattern, repl in [(r"\w+", r"<\g<0>>"), (r"(\w)(\w+)", r"\2\1"), (r"\s+", "_")]:
            with self.subTest(pattern=pattern):
                rp, rr = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))
                self.assertEqual(rp.sub(repl, text), rr.sub(repl, text))
                self.assertEqual(rp.subn(repl, text), rr.subn(repl, text))
                self.assertEqual(rp.sub(repl, text, 5), rr.sub(repl, text, 5))  # count cap

    def test_sub_large_subject_threaded_parity(self):
        """sub on a large subject from several threads sharing one Pattern stays correct
        and byte-identical to re — proves the GIL-released scan is reentrant."""
        import threading
        text = "word " * 4000  # ~20 KB
        rp = real.compile(r"\w+")
        expected = re.compile(r"\w+").sub(r"<\g<0>>", text)
        results = [None] * 8
        def work(i):
            results[i] = rp.sub(r"<\g<0>>", text)
        threads = [threading.Thread(target=work, args=(i,)) for i in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(results, [expected] * 8)

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
                          re.compile(pattern, _text_oracle(pattern, 0)).search(text))
                self.assertIsNotNone(pm)
                self.assertIsNotNone(rm)
                self.assertEqual(pm.expand(template), rm.expand(template))

    def test_expand_combined_template_parity(self):
        r"""A single template mixing \g<0>, \1 and a literal with backslashes."""
        pattern, text, template = r"(\w+)", "word", r"[\g<0>]=\1\\done"
        pm = real.compile(pattern).search(text)
        rm = re.compile(pattern, _text_oracle(pattern, 0)).search(text)
        self.assertEqual(pm.expand(template), rm.expand(template))

    def test_expand_nonparticipating_group_parity(self):
        """A template touching a group that did not participate matches re — the
        group contributes nothing (the shared sub/expand semantics)."""
        pattern, text = r"(a)|(b)", "a"  # group 2 is unset on "a"
        for template in [r"[\1][\2]", r"\g<2>", r"x\2y\1"]:
            with self.subTest(template=template):
                pm = real.compile(pattern).search(text)
                rm = re.compile(pattern, _text_oracle(pattern, 0)).search(text)
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
        rm = re.compile(pattern, _text_oracle(pattern, 0)).search(text)
        self.assertIsNotNone(pm)
        self.assertEqual(pm.expand(template), rm.expand(template))

    def test_expand_from_search_and_finditer_parity(self):
        r"""expand works on a match from search AND from each finditer match."""
        pattern, text, template = r"(\w)(\d)", "a1 b2 c3", r"\2\1"
        rp, rr = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))
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

    def test_lazy_char_spans_non_ascii_parity(self):
        """char_spans is computed lazily (only on .start/.end/.span). Verify the char
        offsets stay correct (parity with re) on a non-ASCII subject — where byte offset
        != char offset — regardless of access order, for matches from search and
        finditer."""
        text = "café crème déjà 42 testé voilà"  # multi-byte chars shift later offsets
        pattern = r"\w+"
        rp, rr = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))

        # .group() FIRST (the path that now skips char_spans), THEN the offsets.
        pm, rm = rp.search(text), rr.search(text)
        self.assertEqual(pm.group(), rm.group())
        self.assertEqual(pm.span(), rm.span())              # forces the lazy compute
        self.assertEqual((pm.start(), pm.end()), (rm.start(), rm.end()))
        self.assertEqual(pm.span(), rm.span())              # idempotent second read

        # Opposite order: .start() BEFORE .group().
        pm2 = rp.search(text)
        self.assertEqual(pm2.start(), rm.start())
        self.assertEqual(pm2.group(), rm.group())

        # finditer: spans correct for matches at every position (parity with re), and
        # the .group()-then-.span() order under iteration.
        self.assertEqual([m.span() for m in rp.finditer(text)],
                         [m.span() for m in rr.finditer(text)])
        self.assertEqual([(m.group(), m.span()) for m in rp.finditer(text)],
                         [(m.group(), m.span()) for m in rr.finditer(text)])

    def _cmp_region(self, pattern, text, pos, endpos, flags=0):
        """Assert real == re for match/search/fullmatch(text, pos, endpos)."""
        re_flags = _text_oracle(pattern, 0)
        re_flags |= re.MULTILINE if (flags & real.M) else 0
        re_flags |= re.DOTALL if (flags & real.S) else 0
        re_flags |= re.IGNORECASE if (flags & real.I) else 0
        rp, rr = real.compile(pattern, flags), re.compile(pattern, re_flags)
        for method in ("match", "search", "fullmatch"):
            with self.subTest(pattern=pattern, pos=pos, endpos=endpos, method=method, flags=flags):
                self.assertEqual(self.match_facts(getattr(rp, method)(text, pos, endpos)),
                                 self.match_facts(getattr(rr, method)(text, pos, endpos)))

    def test_pos_endpos_parity(self):
        """pos/endpos match re: pos>0, endpos<len, both, pos==endpos, pos>endpos,
        out-of-range (clamped); spans and groups absolute."""
        for pattern, text, pos, endpos in [
            (r"\w+", "foo bar baz", 4, 7),
            (r"\w+", "foo bar baz", 4, 100),    # endpos clamped to len
            (r"\w+", "foo bar baz", 0, 3),
            (r"(\w)(\w+)", "xx hello", 3, 8),   # absolute group spans
            (r"\w+", "hello", 2, 2),            # pos == endpos
            (r"\w+", "hello", 4, 2),            # pos > endpos → no match
            (r"\w+", "hello", 100, 100),        # pos clamped
            (r"\d+", "ab123cd", 0, 5),
        ]:
            self._cmp_region(pattern, text, pos, endpos)

    def test_pos_endpos_anchor_parity(self):
        r"""The anchor table (\A \Z ^ $) under pos/endpos matches re — pos is the VM
        start, not a slice."""
        for pattern, text, pos, endpos, flags in [
            (r"\Abar", "foobar", 3, 6, 0),         # \A at pos>0 fails (NOT a slice)
            (r"\Afoo", "foobar", 0, 6, 0),         # \A at 0 holds
            (r"^bar", "foo\nbar", 4, 7, real.M),   # ^ ML at pos right after \n holds
            (r"^bar", "foobar", 3, 6, real.M),     # ^ ML at pos not after \n fails
            (r"^bar", "foo\nbar", 4, 7, 0),        # ^ non-ML at pos>0 fails even after \n
            (r"o\Z", "foo", 0, 3, 0),              # \Z at endpos
            (r"o$", "foo", 0, 3, 0),               # $ at endpos
            (r"o$", "foo\nx", 0, 4, 0),            # $ non-ML before trailing \n at endpos-1
            (r"o$", "foo\nbar", 0, 7, real.M),     # $ ML before internal \n
        ]:
            self._cmp_region(pattern, text, pos, endpos, flags)

    def test_pos_endpos_non_ascii_char_offsets_parity(self):
        """For a str subject pos/endpos are CHARACTER offsets (re semantics) even with
        multi-byte chars; endpos on a char boundary cuts correctly."""
        text = "café déjà 42 voilà"  # multi-byte: char offset != byte offset past 'é'
        for pattern, pos, endpos in [
            (r"\w+", 0, 4), (r"\w+", 5, 9), (r"\w+", 7, 18),
            (r"\d+", 0, 18), (r"\d+", 10, 12), (r".", 5, 6),
        ]:
            self._cmp_region(pattern, text, pos, endpos)

    def test_pos_endpos_bytes_parity(self):
        """For a bytes subject pos/endpos are BYTE offsets."""
        text = b"foo bar 123 baz"
        for pattern, pos, endpos in [
            (rb"\w+", 4, 11), (rb"\d+", 0, 11), (rb"\w+", 8, 11), (rb"\w+", 4, 2),
        ]:
            rp, rr = real.compile(pattern), re.compile(pattern)
            for method in ("match", "search", "fullmatch"):
                with self.subTest(pattern=pattern, pos=pos, endpos=endpos, method=method):
                    self.assertEqual(
                        self.match_facts(getattr(rp, method)(text, pos, endpos)),
                        self.match_facts(getattr(rr, method)(text, pos, endpos)))

    def _cmp_findall_finditer(self, pattern, text, pos, endpos, flags=0):
        """Assert real == re for findall and finditer(text, pos, endpos)."""
        re_flags = _text_oracle(pattern, 0) | (re.MULTILINE if (flags & real.M) else 0)
        rp, rr = real.compile(pattern, flags), re.compile(pattern, re_flags)
        with self.subTest(pattern=pattern, pos=pos, endpos=endpos, flags=flags):
            self.assertEqual(rp.findall(text, pos, endpos), rr.findall(text, pos, endpos))
            self.assertEqual([m.span() for m in rp.finditer(text, pos, endpos)],
                             [m.span() for m in rr.finditer(text, pos, endpos)])

    def test_findall_finditer_region_parity(self):
        """findall/finditer(text, pos, endpos) == re: pos>0, endpos<len, both, clamp,
        pos>endpos (empty)."""
        for pattern, text, pos, endpos in [
            (r"\w+", "foo bar baz qux", 4, 11),
            (r"\w+", "foo bar baz", 4, 100),    # endpos clamped
            (r"\w+", "foo bar baz", 0, 7),
            (r"(\w)(\w+)", "aa bb cc", 3, 8),
            (r"\w+", "hello", 4, 2),            # pos > endpos → empty
            (r"\w+", "hello", 100, 100),        # clamped → empty
            (r"\d+", "a1 b22 c333", 0, 6),
        ]:
            self._cmp_findall_finditer(pattern, text, pos, endpos)

    def test_finditer_stops_at_endpos_midword_parity(self):
        """findall/finditer stop at endpos even for a match that would extend past it."""
        self._cmp_findall_finditer(r"\w+", "hello world", 0, 8)  # → "hello", "wo"

    def test_findall_finditer_region_anchor_parity(self):
        r"""\A/\Z/^/$ x ML within a findall/finditer region == re."""
        for pattern, text, pos, endpos, flags in [
            (r"\A\w+", "foobar", 3, 6, 0),               # \A never at pos>0
            (r"^\w+", "foo\nbar\nbaz", 4, 11, real.M),   # ^ ML after \n
            (r"^\w+", "foo\nbar\nbaz", 4, 11, 0),        # ^ non-ML: only absolute 0
            (r"\w+\Z", "foobar", 0, 3, 0),               # \Z at endpos
            (r"\w+$", "foo\nbar", 0, 7, real.M),         # $ ML before internal \n
        ]:
            self._cmp_findall_finditer(pattern, text, pos, endpos, flags)

    def test_match_pos_endpos_attributes_parity(self):
        """Match.pos/.endpos == re on match/search/fullmatch (with and without
        pos/endpos), including clamp and bytes."""
        for pattern, text, args in [
            (r"\w+", "hello world", ()),
            (r"\w+", "hello world", (3,)),
            (r"\w+", "hello world", (3, 8)),
            (r"\w+", "hello world", (3, 100)),    # endpos clamped
            (r"\w+", "hello world", (100, 200)),  # both clamped → no match
        ]:
            rp, rr = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))
            for method in ("match", "search", "fullmatch"):
                with self.subTest(pattern=pattern, args=args, method=method):
                    pm, rm = getattr(rp, method)(text, *args), getattr(rr, method)(text, *args)
                    if rm is None:
                        self.assertIsNone(pm)
                    else:
                        self.assertEqual((pm.pos, pm.endpos), (rm.pos, rm.endpos))
        bp, br = real.compile(rb"\w+"), re.compile(rb"\w+")  # bytes: byte offsets
        bpm, brm = bp.search(b"foo bar", 2, 6), br.search(b"foo bar", 2, 6)
        self.assertEqual((bpm.pos, bpm.endpos), (brm.pos, brm.endpos))

    def test_finditer_match_pos_endpos_parity(self):
        """Matches from finditer(pos, endpos) carry the call's .pos/.endpos, not 0/len."""
        text = "foo bar baz qux"
        rp, rr = real.compile(r"\w+"), re.compile(r"\w+")
        pm, rm = list(rp.finditer(text, 4, 11)), list(rr.finditer(text, 4, 11))
        self.assertEqual([(m.pos, m.endpos) for m in pm], [(m.pos, m.endpos) for m in rm])
        self.assertTrue(pm and all(m.pos == 4 and m.endpos == 11 for m in pm))

    def test_findall_finditer_same_region_consistent(self):
        """findall and finditer over the same region produce the same texts."""
        rp = real.compile(r"\w+")
        text = "a1 b2 c3 d4"
        self.assertEqual(rp.findall(text, 3, 8), [m.group() for m in rp.finditer(text, 3, 8)])

    def test_findall_finditer_region_non_ascii_parity(self):
        """Region findall/finditer with a non-ASCII subject: character offsets == re."""
        text = "café déjà 42 voilà"
        for pos, endpos in [(0, 4), (5, 9), (7, 18), (0, 18)]:
            self._cmp_findall_finditer(r"\w+", text, pos, endpos)

    def test_findall_finditer_region_bytes_parity(self):
        """Region findall/finditer with a bytes subject: byte offsets == re."""
        text = b"foo bar 123 baz"
        for pos, endpos in [(4, 11), (0, 7), (8, 15)]:
            rp, rr = real.compile(rb"\w+"), re.compile(rb"\w+")
            with self.subTest(pos=pos, endpos=endpos):
                self.assertEqual(rp.findall(text, pos, endpos), rr.findall(text, pos, endpos))
                self.assertEqual([m.span() for m in rp.finditer(text, pos, endpos)],
                                 [m.span() for m in rr.finditer(text, pos, endpos)])

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

    # Bounded lookahead (?=...) / (?!...). Two REAL-specific divergences keep these cases
    # narrow: (1) REAL's lookahead sub is capture-free (re captures inside a lookaround), so
    # every capturing group stays OUTSIDE the lookahead; (2) re accepts an unbounded sub
    # (e.g. (?=a*)) whereas REAL rejects it to stay linear — those belong in test_real.py,
    # not here. On the bounded, capture-free-inside cases below the two engines must agree
    # exactly, in search/match/fullmatch as in findall/finditer.
    LOOKAHEAD_CASES = [
        (r"(?=\d)\w+", "abc 123 xyz"),       # leading assertion then consume
        (r"(?=\d)\w+", "a1b2c3"),
        (r"(?=\d)\w+", "no digits here"),
        (r"\d+(?=px)", "10px 20em 30px"),    # trailing assertion
        (r"\w+(?=:)", "key: value k2: v2"),
        (r"foo(?!bar)", "foobar foobaz foo"),  # negative
        (r"q(?!u)\w", "quack quit qix qa"),
        (r"(?=\bfoo)\w+", "a foo barfoo foobar"),  # \b assertion inside the sub
        (r"(?=\d{3})\d+", "12 345 6789"),    # bounded repeat inside (L_max = 3)
        (r"a(?=b)", "ab ac ab"),
        (r"a(?!b)", "ab ac ad"),
        (r"\w+(?=\d)", "abc1 de2 fg"),       # word run ending right before a digit
    ]

    def test_lookahead_match_parity(self):
        """search/match/fullmatch with bounded lookahead == re (spans + groups)."""
        for pattern, text in self.LOOKAHEAD_CASES:
            p, r = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))
            for method in ("search", "match", "fullmatch"):
                with self.subTest(pattern=pattern, text=text, method=method):
                    self.assertEqual(self.match_facts(getattr(p, method)(text)),
                                     self.match_facts(getattr(r, method)(text)))

    def test_lookahead_findall_finditer_parity(self):
        """findall/finditer with bounded lookahead == re (every match, every span)."""
        for pattern, text in self.LOOKAHEAD_CASES:
            p, r = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))
            with self.subTest(pattern=pattern, text=text):
                self.assertEqual(p.findall(text), r.findall(text))
                self.assertEqual([m.span() for m in p.finditer(text)],
                                 [m.span() for m in r.finditer(text)])

    def test_lookahead_multibyte_literal_parity(self):
        r"""A multi-byte UTF-8 literal inside the lookahead: its L_max must be the true
        byte length (café = 5 bytes), and offsets/results stay byte-for-byte with re."""
        for pattern, text in [(r"(?=café)\w+", "au café crème"),
                              (r"\w+(?=é)", "café déjà fin")]:
            p, r = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))
            with self.subTest(pattern=pattern):
                self.assertEqual(self.match_facts(p.search(text)),
                                 self.match_facts(r.search(text)))
                self.assertEqual([m.span() for m in p.finditer(text)],
                                 [m.span() for m in r.finditer(text)])

    # Bounded lookbehind (?<=...) / (?<!...). Same constraints as lookahead (capture-free
    # sub, bounded length); the cases here also pin the exact-end-at-pos rule (a sub that
    # matches earlier in the window must NOT count) and alternation behind with branches of
    # different lengths (which exercises distinct candidate starts).
    LOOKBEHIND_CASES = [
        (r"(?<=ab)c", "xabc abc"),
        (r"(?<=ab)c", "abxc"),                  # trap: "ab" ends before, not at, the 'c'
        (r"(?<=\d)x", "1x 2x ax"),
        (r"(?<=\d{3})x", "12x 123x 1234x"),     # bounded repeat behind
        (r"(?<!ab)c", "xc abc cc"),             # negative
        (r"(?<!\d)x", "1x ax bx"),
        (r"(?<=foo)bar", "foobar xbar foobar"),
        # NB: variable-width lookbehind (e.g. (?<=a|bb)) is NOT a parity case — re rejects it
        # ("look-behind requires fixed-width pattern"); REAL accepts any bounded sub. That
        # capability is pinned as a REAL-only differentiator in tests/test_lookaround.cpp.
    ]

    def test_lookbehind_match_parity(self):
        """search/match/fullmatch with bounded lookbehind == re (spans + groups)."""
        for pattern, text in self.LOOKBEHIND_CASES:
            p, r = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))
            for method in ("search", "match", "fullmatch"):
                with self.subTest(pattern=pattern, text=text, method=method):
                    self.assertEqual(self.match_facts(getattr(p, method)(text)),
                                     self.match_facts(getattr(r, method)(text)))

    def test_lookbehind_findall_finditer_parity(self):
        """findall/finditer with bounded lookbehind == re (every match, every span)."""
        for pattern, text in self.LOOKBEHIND_CASES:
            p, r = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))
            with self.subTest(pattern=pattern, text=text):
                self.assertEqual(p.findall(text), r.findall(text))
                self.assertEqual([m.span() for m in p.finditer(text)],
                                 [m.span() for m in r.finditer(text)])

    def test_lookbehind_non_ascii_offsets_parity(self):
        r"""Multi-byte subject: L_max counts bytes and a candidate start may not split a
        codepoint (A9); character offsets must still match re."""
        for pattern, text in [(r"(?<=é)x", "café éx déjà"),
                              (r"(?<=\w)é", "aé bé .é")]:
            p, r = real.compile(pattern), re.compile(pattern, _text_oracle(pattern, 0))
            with self.subTest(pattern=pattern):
                self.assertEqual(self.match_facts(p.search(text)),
                                 self.match_facts(r.search(text)))
                self.assertEqual([m.span() for m in p.finditer(text)],
                                 [m.span() for m in r.finditer(text)])

    def test_match_lastindex_lastgroup_regs_parity(self):
        """Match.lastindex / .lastgroup / .regs == re across flat, nested, alternation,
        optional and named groups. lastindex is the last group to CLOSE (re semantics), not
        the highest index -- ((a)(b)) -> 1, (a*)(b*) on '' -> 2, ((a*)) on '' -> 1."""
        cases = [
            (r"(a)(b)", "ab"), (r"((a)(b))", "ab"), (r"(a)|(b)", "b"), (r"(a)|(b)", "a"),
            (r"(a)(b)?", "a"), (r"(a)(b)?", "ab"), (r"(a*)(b*)", ""), (r"((a*))", ""),
            (r"(a)(b)(c)", "abc"), (r"((a)(b))(c)", "abc"), (r"(a(b(c)))", "abc"),
            (r"(?P<x>a)(?P<y>b)", "ab"), (r"(?P<x>a)(?P<y>b)?", "a"),
            (r"(?P<outer>(?P<inner>a))", "a"), (r"x", "x"), (r"(a)(b)?(c)", "ac"),
        ]
        for pattern, text in cases:
            with self.subTest(pattern=pattern, text=text):
                pm = real.compile(pattern).match(text)
                rm = re.compile(pattern, _text_oracle(pattern, 0)).match(text)
                self.assertIsNotNone(pm)
                self.assertEqual(pm.lastindex, rm.lastindex)
                self.assertEqual(pm.lastgroup, rm.lastgroup)
                self.assertEqual(pm.regs, rm.regs)

    def test_lookaround_bytes_parity(self):
        """Bounded, capture-free lookarounds on BYTES subjects match re — byte-mode alignment
        (the lookbehind start scan does not skip continuation bytes)."""
        for pattern, subject in [
            (rb"(?<=ab)c", b"xabc"), (rb"(?<=ab)c", b"abxc"), (rb"\d+(?=px)", b"10px 20em"),
            (rb"a(?!b)", b"ab ac ad"), (rb"(?<!\d)x", b"1x ax"), (rb"(?<=foo)bar", b"foobar xbar"),
        ]:
            with self.subTest(pattern=pattern):
                rp, rr = real.compile(pattern), re.compile(pattern)
                self.assertEqual(self.match_facts(rp.search(subject)),
                                 self.match_facts(rr.search(subject)))
                self.assertEqual([m.span() for m in rp.finditer(subject)],
                                 [m.span() for m in rr.finditer(subject)])

    def test_unicode_escapes_parity(self):
        r"""\uHHHH / \UHHHHHHHH match the same code point as re (str mode): REAL emits the
        UTF-8 bytes, re matches the code point, and on a str subject they agree."""
        for pattern, subject in [
            (r"\u00e9", "café"), (r"a\u00e9b", "x aéb y"), (r"\u0041", "an A here"),
            (r"\U0001F600", "hi 😀!"), (r"caf\u00e9", "a café b"), (r"\u00e9+", "ééé done"),
        ]:
            with self.subTest(pattern=pattern):
                self.assertEqual(self.match_facts(real.compile(pattern).search(subject)),
                                 self.match_facts(re.compile(pattern, _text_oracle(pattern, 0)).search(subject)))

    def test_inline_comment_parity(self):
        r"""(?#...) is a comment in both engines: consumed to the first ')', emits nothing."""
        for pattern, subject in [
            (r"a(?#x)b", "ab cab"), (r"(?#lead)\d+", "x 42 y"), (r"a(?# a backslash \ here )b", "ab"),
            (r"(?#c)\w+(?#c2)", "hello"),
        ]:
            with self.subTest(pattern=pattern):
                self.assertEqual(self.match_facts(real.compile(pattern).search(subject)),
                                 self.match_facts(re.compile(pattern, _text_oracle(pattern, 0)).search(subject)))

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
