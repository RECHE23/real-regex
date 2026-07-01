"""Behavioral tests for the real module (API shape, types, errors)."""

import unittest

import real


class TestCompile(unittest.TestCase):
    """Tests for real.compile and compilation-time behavior."""

    def test_compile_returns_pattern(self):
        """compile() returns a Pattern with the expected attributes."""
        p = real.compile(r"\d+")
        self.assertIsInstance(p, real.Pattern)
        self.assertEqual(p.pattern, r"\d+")
        self.assertEqual(p.groups, 0)

    def test_compile_pattern_passthrough(self):
        """compile() returns an already-compiled Pattern unchanged."""
        p = real.compile(r"x")
        self.assertIs(real.compile(p), p)
        with self.assertRaises(ValueError):
            real.compile(p, real.I)

    def test_cache(self):
        """Repeated compile() calls for the same pattern return the same object."""
        self.assertIs(real.compile(r"abc"), real.compile(r"abc"))
        real.purge()

    def test_invalid_patterns_raise_error(self):
        """Invalid syntax raises real.error."""
        for pattern in [r"(", r")", r"a**", r"[z-a]", r"(?>x)",  # (?>x): atomic group, unsupported
                        r"(?P=name)", r"(?P<1>x)", r"\q", "a\\"]:
            with self.assertRaises(real.error, msg=pattern):
                real.compile(pattern)

    def test_unsupported_flags_raise(self):
        """Unsupported re flags raise real.error."""
        import re
        for flag in (re.L, re.DEBUG):
            with self.assertRaises(real.error):
                real.compile("a", int(flag))

    def test_verbose_flag(self):
        """VERBOSE/X ignores unescaped whitespace and # comments outside classes."""
        self.assertTrue(real.compile(r"a b c", real.X).fullmatch("abc"))
        self.assertTrue(real.compile(r"\d{4} - \d{2}", real.VERBOSE).fullmatch("2026-06"))
        self.assertTrue(real.compile("a # note\nb", real.X).fullmatch("ab"))
        self.assertTrue(real.compile(r"[ ]", real.X).fullmatch(" "))  # literal in class
        self.assertTrue(real.compile(r"(?x) a b c").fullmatch("abc"))  # inline (?x)

    def test_ascii_and_unicode_flags(self):
        """A restricts \\d \\s (and folding) to ASCII; U is a no-op (Unicode is the default)."""
        self.assertTrue(real.compile("a", real.A).match("a"))  # no effect on a plain literal
        self.assertTrue(real.compile("a", real.U).match("a"))
        self.assertEqual(real.findall(r"\d", "٣5", real.A), ["5"])   # A: ASCII digits only
        self.assertEqual(real.findall(r"\d", "٣5", real.U), ["٣", "5"])  # U: Unicode (default)

    def test_program_size_limit(self):
        """Patterns exceeding the compiled-program size cap raise real.error."""
        # Under cap (a{200} ~200 instr) still works and matches.
        p = real.compile("a{200}")
        self.assertTrue(p.match("a" * 200))
        # Over cap (nested unroll product >> 262144) raises cleanly.
        # This is the #1 security fix: prevents DoS from tiny patterns.
        with self.assertRaises(real.error):
            real.compile("((a{400}){400}){2}")


class TestMatchObject(unittest.TestCase):
    """Tests for the real.Match object accessors."""

    def test_basic_accessors(self):
        """group(), groups(), groupdict(), span(), start(), end() work as expected."""
        m = real.search(r"(?P<y>\d{4})-(\d{2})", "on 2026-06-10!")
        self.assertEqual(m.group(), "2026-06")
        self.assertEqual(m.group(0), "2026-06")
        self.assertEqual(m.group(1), "2026")
        self.assertEqual(m.group("y"), "2026")
        self.assertEqual(m.group(1, 2), ("2026", "06"))
        self.assertEqual(m[2], "06")
        self.assertEqual(m["y"], "2026")
        self.assertEqual(m.groups(), ("2026", "06"))
        self.assertEqual(m.groupdict(), {"y": "2026"})
        self.assertEqual(m.span(), (3, 10))
        self.assertEqual(m.start(2), 8)
        self.assertEqual(m.end("y"), 7)
        self.assertEqual(m.string, "on 2026-06-10!")
        self.assertIs(m.re, real.compile(r"(?P<y>\d{4})-(\d{2})"))

    def test_unset_group(self):
        """Unset groups return None (or the default) and span (-1, -1)."""
        m = real.fullmatch(r"(a)|(b)", "b")
        self.assertIsNone(m.group(1))
        self.assertEqual(m.groups(), (None, "b"))
        self.assertEqual(m.groups(default=""), ("", "b"))
        self.assertEqual(m.span(1), (-1, -1))

    def test_bad_group_raises(self):
        """Out-of-range or unknown groups raise IndexError."""
        m = real.search(r"(a)", "a")
        with self.assertRaises(IndexError):
            m.group(2)
        with self.assertRaises(IndexError):
            m.group("nope")

    def test_non_ascii_subject_uses_char_indices(self):
        """Span indices are character offsets, not byte offsets."""
        m = real.search(r"au", "café au lait")
        self.assertEqual(m.span(), (5, 7))
        self.assertEqual(m.group(), "au")
        m = real.search(r"l\w+", "café au lait")
        self.assertEqual(m.span(), (8, 12))


class TestBytes(unittest.TestCase):
    """Tests for bytes patterns and subjects."""

    def test_bytes_pattern_and_subject(self):
        """Bytes patterns work on bytes subjects."""
        p = real.compile(rb"(\d+)")
        m = p.search(b"a 42!")
        self.assertEqual(m.group(0), b"42")
        self.assertEqual(m.span(), (2, 4))
        self.assertEqual(p.findall(b"1 22"), [b"1", b"22"])
        self.assertEqual(p.sub(rb"<\1>", b"a 42!"), b"a <42>!")

    def test_type_mixing_is_rejected(self):
        """Mixing str and bytes patterns/subjects raises TypeError."""
        with self.assertRaises(TypeError):
            real.compile(rb"a").search("a")
        with self.assertRaises(TypeError):
            real.compile(r"a").search(b"a")
        with self.assertRaises(TypeError):
            real.compile(r"a").sub(b"x", "a")


class TestSub(unittest.TestCase):
    """Tests for sub() / subn() replacement templates."""

    def test_template_references(self):
        """Numeric and named back-references are expanded correctly."""
        self.assertEqual(real.sub(r"(\w+)@(\w+)", r"\2:\1", "bob@host"), "host:bob")
        self.assertEqual(real.sub(r"(?P<u>\w+)@", r"\g<u>!", "bob@x"), "bob!x")
        self.assertEqual(real.sub(r"(a)", r"\g<1>\g<0>", "a"), "aa")
        self.assertEqual(real.sub(r"a", r"\n\\", "a"), "\n\\")
        self.assertEqual(real.sub(r"a", r"\!", "a"), r"\!")  # punct keeps backslash

    def test_template_errors(self):
        """Invalid back-references raise real.error."""
        with self.assertRaises(real.error):
            real.sub(r"(a)", r"\2", "a")
        with self.assertRaises(real.error):
            real.sub(r"a", r"\q", "a")
        with self.assertRaises(real.error):
            real.sub(r"a", r"\g<zz>", "a")

    def test_callable_and_count(self):
        """Callable replacements and count limits work."""
        self.assertEqual(real.sub(r"\d+", lambda m: str(int(m.group()) * 2), "3 4"),
                         "6 8")
        self.assertEqual(real.sub(r"x", "-", "xxx", 2), "--x")
        self.assertEqual(real.subn(r"x", "-", "xxx"), ("---", 3))


class TestModuleFunctions(unittest.TestCase):
    """Tests for the module-level convenience functions."""

    def test_match_vs_search_vs_fullmatch(self):
        """match, search and fullmatch differ in anchoring semantics."""
        self.assertIsNone(real.match(r"b", "ab"))
        self.assertIsNotNone(real.search(r"b", "ab"))
        self.assertIsNone(real.fullmatch(r"a", "ab"))
        self.assertIsNotNone(real.fullmatch(r"ab", "ab"))

    def test_findall_shapes(self):
        """findall returns the right shape depending on group count."""
        self.assertEqual(real.findall(r"\d+", "a1 b22"), ["1", "22"])
        self.assertEqual(real.findall(r"(\d)\d", "12 34"), ["1", "3"])
        self.assertEqual(real.findall(r"(a)|(b)", "ab"), [("a", ""), ("", "b")])

    def test_finditer_and_split(self):
        """finditer yields Match objects and split respects maxsplit."""
        spans = [m.span() for m in real.finditer(r"x*", "axb")]
        self.assertEqual(spans, [(0, 0), (1, 2), (2, 2), (3, 3)])
        self.assertEqual(real.split(r"(x)|(y)", "axbyc"),
                         ["a", "x", None, "b", None, "y", "c"])
        self.assertEqual(real.split(r",", "a,b,c", maxsplit=1), ["a", "b,c"])

    def test_finditer_is_lazy_and_reentrant(self):
        """finditer returns a real lazy iterator: self-iterating, exhausting with
        StopIteration, reentrant (independent cursors on one Pattern), and scaling
        to many matches without materialising them."""
        p = real.compile(r"\d+")
        it = p.finditer("a1b22c333")
        self.assertIs(iter(it), it)                                # its own iterator
        self.assertEqual(next(it).group(), "1")                    # lazy: first match, no draining
        self.assertEqual([m.group() for m in it], ["22", "333"])  # resumes from the cursor
        with self.assertRaises(StopIteration):
            next(it)
        i1, i2 = p.finditer("1 2 3"), p.finditer("4 5 6")          # independent cursors, same Pattern
        self.assertEqual(
            [next(i1).group(), next(i2).group(), next(i1).group(), next(i2).group()],
            ["1", "4", "2", "5"])
        self.assertEqual(sum(1 for _ in p.finditer("x9 " * 10000)), 10000)  # scale

    def test_search_is_thread_safe(self):
        """Concurrent search/match on shared and distinct Patterns give correct
        results. match/fullmatch/search release the GIL around the scan for subjects
        >= 512 B (here ~1 KB) and use per-call-local scratch — no shared state to race."""
        import threading
        big = "word " * 200  # ~1 KB (>= the 512 B GIL-release threshold), contains no digits
        subjects = [big + f"#{i}" for i in range(400)]
        expected = [str(i) for i in range(400)]  # search(\d+) finds the trailing number
        results = [None] * len(subjects)
        errors = []
        shared = real.compile(r"\d+")

        def worker(indices, pat):
            try:
                for i in indices:
                    found = pat.search(subjects[i])
                    results[i] = found.group() if found else None
                    if pat.match(subjects[i]) is not None:  # anchored: subject starts with "word"
                        errors.append(f"match unexpectedly matched at {i}")
            except BaseException as exc:  # noqa: BLE001 — record; asserted in the main thread
                errors.append(exc)

        threads = []
        for t in range(4):
            pat = shared if t % 2 == 0 else real.compile(r"\d+")  # mix shared + distinct Patterns
            threads.append(threading.Thread(target=worker, args=(range(t, len(subjects), 4), pat)))
        for th in threads:
            th.start()
        for th in threads:
            th.join()
        self.assertEqual(errors, [])
        self.assertEqual(results, expected)

    def test_escape_roundtrip(self):
        """escape() produces a pattern that matches the original literal."""
        for text in ["a.b*c", "1+1=2?", "[hi]{2}|x^$", "plain"]:
            self.assertTrue(real.fullmatch(real.escape(text), text), text)
        self.assertTrue(real.fullmatch(real.escape(b"a.b"), b"a.b"))

    def test_flags(self):
        """IGNORECASE, MULTILINE and DOTALL work as expected."""
        self.assertTrue(real.search(r"hello", "say HELLO", real.I))
        self.assertTrue(real.search(r"^b", "a\nb", real.M))
        self.assertTrue(real.fullmatch(r"a.b", "a\nb", real.S))
        self.assertTrue(real.search(r"(?i)hello", "HELLO"))


class TestNestingDepth(unittest.TestCase):
    """Tests for the parser recursion limit."""

    def test_deep_nesting_raises_instead_of_crashing(self):
        """Excessive nesting raises real.error instead of crashing."""
        # Used to overflow the C stack and kill the interpreter.
        with self.assertRaises(real.error):
            real.compile("(" * 50000 + "a" + ")" * 50000)
        self.assertTrue(real.compile("(" * 100 + "a" + ")" * 100).match("a"))


class TestRedosSafety(unittest.TestCase):
    """Tests demonstrating linear-time matching."""

    def test_pathological_pattern_is_fast(self):
        """A catastrophic pattern for backtracking engines is fast here."""
        import time
        start = time.perf_counter()
        self.assertIsNone(real.search(r"(a+)+b", "a" * 500))
        self.assertLess(time.perf_counter() - start, 0.1)


class TestWordEdgeAnchors(unittest.TestCase):
    """Tests for REAL's \< and \> word-edge anchors."""
    # \< and \> are word-start / word-end anchors (a REAL extension; Python re
    # has no equivalent), reusing the same ASCII word-character notion as \b.

    def test_word_start_and_end(self):
        """\< and \> anchor at word edges."""
        self.assertEqual(real.findall(r"\<\w+", "foo bar-baz"), ["foo", "bar", "baz"])
        self.assertEqual(real.findall(r"\w+\>", "foo bar-baz"), ["foo", "bar", "baz"])

    def test_whole_word_only(self):
        """\<cat\> matches only the whole word."""
        self.assertTrue(real.search(r"\<cat\>", "a cat here"))
        self.assertIsNone(real.search(r"\<cat\>", "category"))
        self.assertIsNone(real.search(r"\<cat\>", "concat"))

    def test_zero_width_at_edges(self):
        """Word-edge anchors are zero-width and work at string boundaries."""
        self.assertEqual(real.search(r"\<", "abc").span(), (0, 0))
        self.assertEqual(real.search(r"\>", "abc").span(), (3, 3))


class TestIntentionalDivergences(unittest.TestCase):
    r"""Every intentional divergence from Python re, pinned as a real-only assertion.

    These lock REAL's contract independently of the running re version; the rationale
    for each is in the "Differences from Python re" documentation page (docs/divergences.dox).
    """

    def test_character_class_non_ascii_members(self):
        # U2: a str-mode class carries specific non-ASCII code points (was rejected before U2).
        # The broad differential-vs-re lives in test_parity; here we pin the behaviour.
        self.assertIsNotNone(real.compile(r"[é]").fullmatch("é"))
        self.assertIsNone(real.compile(r"[é]").fullmatch("à"))      # a specific code point, not "any non-ASCII"
        self.assertIsNotNone(real.compile(r"[\u00e9]").fullmatch("é"))
        self.assertIsNotNone(real.compile(r"[à-ÿ]").fullmatch("ê")) # a code-point range
        self.assertIsNone(real.compile(r"[^é]").fullmatch("é"))     # negation excludes é ...
        self.assertIsNotNone(real.compile(r"[^é]").fullmatch("à"))  # ... but matches other code points

    def test_icase_folds_unicode(self):
        # Text-mode icase does full Unicode simple case folding, like re.IGNORECASE: ASCII letters,
        # and non-ASCII code points, fold -- é matches É, and k matches Kelvin (U+212A).
        self.assertIsNotNone(real.compile("a", real.I).fullmatch("A"))
        self.assertIsNotNone(real.compile(r"é", real.I).fullmatch("é"))
        self.assertIsNotNone(real.compile(r"é", real.I).fullmatch("É"))
        self.assertIsNotNone(real.compile("k", real.I).fullmatch("K"))  # k <-> Kelvin
        # \w stays ASCII for now; \d \s are Unicode -- see test_shorthands_d_s_are_unicode.

    def test_shorthands_d_s_are_unicode(self):
        # W2: \d \s are Unicode in str mode (like re); \w stays ASCII for now.
        self.assertEqual(real.findall(r"\d", "a٣b5"), ["٣", "5"])  # Arabic-Indic digit + ASCII
        self.assertIsNone(real.compile(r"\d").fullmatch("½"))       # No is not a \d digit
        self.assertIsNotNone(real.compile(r"\s").fullmatch("\u00a0"))  # NBSP
        self.assertIsNotNone(real.compile(r"\D").fullmatch("é"))
        self.assertEqual(real.findall(r"[\d]+", "٣5.9"), ["٣5", "9"])
        self.assertEqual(real.findall(r"\w", "٣"), [])              # \w still ASCII

    def test_ascii_flag_reverts_shorthands(self):
        # re.A keeps \d \s and folding ASCII in str mode; re.U is a no-op (Unicode is the default).
        self.assertEqual(real.findall(r"\d", "a٣b5", real.A), ["5"])
        self.assertIsNone(real.compile(r"\s", real.A).fullmatch("\u00a0"))  # NBSP not ASCII space
        self.assertIsNotNone(real.compile("k", real.A | real.I).fullmatch("k"))  # ASCII fold still works
        self.assertEqual(real.findall(r"\d", "٣5", real.U), ["٣", "5"])

    def test_hex_escape_is_byte_level(self):
        # \xHH for HH >= 0x80 matches the raw byte, not chr(HH): re str \xe9 matches U+00E9 (é),
        # REAL \xe9 matches the single byte 0xE9 (which a well-formed str's UTF-8 of é never is).
        self.assertIsNotNone(real.compile(rb"\xe9").fullmatch(b"\xe9"))
        self.assertIsNone(real.compile(r"\xe9").search("é"))

    def test_word_boundary_on_empty_is_modern(self):
        # \B matches the empty string/region (no boundary to negate); \b does not. REAL's
        # by-definition behaviour == re >= 3.11 (re < 3.11 had the opposite quirk for \B).
        self.assertEqual(real.search(r"\B", "").span(), (0, 0))
        self.assertEqual(real.compile(r"\B").search("abc", 0, 0).span(), (0, 0))
        self.assertEqual(real.compile(r"2*C*\B").search("c20", 0, 0).span(), (0, 0))
        self.assertIsNone(real.search(r"\b", ""))
        self.assertIsNone(real.compile(r"\b").search("abc", 0, 0))

    def test_nullable_loop_keeps_last_nonempty_iteration(self):
        # (a*)* on "aa": REAL (like Perl/PCRE, which forbid repeating an empty match) reports
        # the last NON-empty iteration for group 1 ("aa"); re reports the final empty one ("").
        # Group 0 is identical in both.
        m = real.fullmatch(r"(a*)*", "aa")
        self.assertEqual(m.group(0), "aa")
        self.assertEqual(m.group(1), "aa")

    def test_rejected_by_design(self):
        # Each rejected for a documented reason (see the divergences page): backreferences
        # would break linearity; \N{} / \p{} need megabytes of Unicode tables; conditional
        # groups are non-regular.
        for pattern in [r"(a)\1", r"(?P=name)", r"\N{BULLET}", r"(?(1)a|b)", r"\p{L}"]:
            with self.subTest(pattern=pattern):
                with self.assertRaises(real.error):
                    real.compile(pattern)

    def test_lookbehind_variable_width_accepted(self):
        # A bounded variable-width lookbehind is ACCEPTED -- beyond re/PCRE, which require a
        # fixed width.
        import re as _re
        with self.assertRaises(_re.error):
            _re.compile(r"(?<=a|bb)c")
        self.assertEqual(real.compile(r"(?<=a|bb)c").search("xbbc").group(), "c")


class TestCppIntegration(unittest.TestCase):
    """Tests for the C++ embedding helpers."""

    def test_get_include_resolves_to_the_headers(self):
        """get_include() points to the shipped C++ headers."""
        import os
        inc = real.get_include()
        self.assertTrue(os.path.isdir(inc))
        self.assertTrue(os.path.isfile(os.path.join(inc, "real", "real.hpp")))

    def test_get_config(self):
        """get_config() returns version, include path and C++ standard."""
        cfg = real.get_config()
        self.assertEqual(cfg["version"], real.__version__)
        self.assertEqual(cfg["include"], real.get_include())
        self.assertEqual(cfg["cxx_standard"], "c++20")


class TestPatternIntrospection(unittest.TestCase):
    """Pattern.groupindex and Pattern.flags expose the compiled pattern's metadata."""

    def test_groupindex(self):
        import re
        p = real.compile(r"(?P<year>\d{4})-(?P<month>\d{2})-(\d{2})")
        self.assertEqual(dict(p.groupindex), {"year": 1, "month": 2})  # group 3 is unnamed
        r = re.compile(r"(?P<year>\d{4})-(?P<month>\d{2})-(\d{2})", re.ASCII)
        self.assertEqual(dict(p.groupindex), dict(r.groupindex))       # parity with re
        self.assertEqual(dict(real.compile("abc").groupindex), {})

    def test_flags_reflect_compile_arguments(self):
        self.assertEqual(real.compile("x", real.I).flags & real.I, real.I)
        both = real.M | real.S
        self.assertEqual(real.compile("x", both).flags & both, both)
        self.assertEqual(real.compile("x").flags & real.I, 0)


class TestBackreferencesRejected(unittest.TestCase):
    """Backreferences are a documented limitation; the digit decoder must reject them with a
    clear error rather than silently mis-parsing (and never confuse them with octal escapes)."""

    def test_backref_rejected(self):
        for pattern in [r"(a)\1", r"(a)(b)\2", r"\1", r"\8", r"\12"]:
            with self.subTest(pattern=pattern):
                with self.assertRaises(real.error):
                    real.compile(pattern)

    def test_octal_still_compiles(self):
        # The twin of \1: octal escapes must NOT be rejected.
        for pattern in [r"\012", r"\101", r"\0", r"\000"]:
            with self.subTest(pattern=pattern):
                self.assertIsNotNone(real.compile(pattern))


class TestErrorHierarchy(unittest.TestCase):
    """real.error must subclass re.error so ``except re.error:`` catches REAL's errors."""

    def test_real_error_subclasses_re_error(self):
        import re
        self.assertTrue(issubclass(real.error, re.error))
        self.assertTrue(issubclass(real.error, Exception))

    def test_error_caught_at_every_level(self):
        """A compile error is catchable as real.error (native), re.error (the
        re-compatibility point) and Exception (the common base)."""
        import re
        with self.assertRaises(real.error):
            real.compile("(")
        with self.assertRaises(re.error):     # <- the re-compatibility guarantee
            real.compile("(")
        with self.assertRaises(Exception):
            real.compile("(")


class TestBindingCleanup(unittest.TestCase):
    """Anti-regression smoke for the binding's exception/cleanup surface: match
    construction and the scan paths must build correct objects (no leak, no UB) across
    search / finditer / findall / split / sub. The real oracle for the OOM paths is a
    clean ``make sanitize``; these pin the happy paths the new guards wrap."""

    def test_search_groups_intact(self):
        p = real.compile(r"(\d+)-(\d+)")
        m = p.search("a 12-34 b")
        self.assertIsNotNone(m)
        self.assertEqual(m.span(1), (2, 4))
        self.assertEqual(m.span(2), (5, 7))
        self.assertEqual(m.group(1), "12")

    def test_finditer_and_findall(self):
        p = real.compile(r"(\d+)-(\d+)")
        self.assertEqual([m.span() for m in p.finditer("1-2 3-4")], [(0, 3), (4, 7)])
        self.assertEqual(p.findall("1-2 3-4"), [("1", "2"), ("3", "4")])

    def test_split_and_sub(self):
        self.assertEqual(real.compile(r"\s+").split("a b  c"), ["a", "b", "c"])
        self.assertEqual(real.compile(r"(\d+)").sub(r"<\1>", "a1b22"), "a<1>b<22>")
        self.assertEqual(real.compile(r"\d+").sub(lambda m: f"[{m.group()}]", "a1b22"),
                         "a[1]b[22]")


class TestMatchRepr(unittest.TestCase):
    """Match.__repr__ mirrors re's format (only the module prefix differs)."""

    def test_repr_mirrors_re(self):
        import re
        for pattern, text in [(r"(\d+)", "abc 123 def"), (r".+", "x" * 100),
                              (r".+", "café crème")]:
            with self.subTest(pattern=pattern):
                pm = real.compile(pattern).search(text)
                rm = re.compile(pattern, re.ASCII).search(text)
                self.assertEqual(repr(pm).replace("real.Match", "re.Match"), repr(rm))

    def test_repr_bytes(self):
        import re
        pm = real.compile(rb"\w+").search(b"hello")
        rm = re.compile(rb"\w+").search(b"hello")
        self.assertEqual(repr(pm).replace("real.Match", "re.Match"), repr(rm))


class TestPatternValueSemantics(unittest.TestCase):
    """re.Pattern is a value type: equal text + flags compare equal and hash equal."""

    def test_equality_and_hash(self):
        a = real.compile("ab+", real.I)
        real.purge()
        b = real.compile("ab+", real.I)             # distinct object, same text + flags
        self.assertIsNot(a, b)
        self.assertEqual(a, b)
        self.assertEqual(hash(a), hash(b))
        self.assertEqual(len({a, b, real.compile("ab+", real.I)}), 1)  # usable as set/dict keys

    def test_inequality(self):
        a = real.compile("ab+", real.I)
        self.assertNotEqual(a, real.compile("ab+"))            # different flags
        self.assertNotEqual(a, real.compile("ab*", real.I))   # different text
        self.assertNotEqual(a, real.compile(b"ab+", real.I))  # str vs bytes

    def test_compare_to_non_pattern(self):
        a = real.compile("x")
        self.assertFalse(a == 42)   # NotImplemented -> Python falls back, no crash
        self.assertTrue(a != 42)
        self.assertNotEqual(a, "x")


class TestUnicodeAndConstructs(unittest.TestCase):
    r"""\u / \U code-point escapes, (?#...) comments, and clean rejections."""

    def test_unicode_escape_rejections(self):
        # str-mode \u / \U work (see parity tests); these forms are rejected with clear errors.
        for pattern in [r"\uD800", r"\U00110000", r"\u00e", r"\U0001F60", r"\N{BULLET}"]:
            with self.subTest(pattern=pattern):
                with self.assertRaises(real.error):
                    real.compile(pattern)

    def test_unicode_escape_rejected_in_bytes(self):
        for pattern in [rb"\u0041", rb"\U0001F600", rb"[\u0041]"]:
            with self.subTest(pattern=pattern):
                with self.assertRaises(real.error):
                    real.compile(pattern)

    def test_non_ascii_class_member_accepted_in_str_mode(self):
        # U2: a non-ASCII class member is accepted in str mode; bytes mode still rejects raw high bytes.
        self.assertIsNotNone(real.compile(r"[\u00e9]").search("é"))
        self.assertIsNotNone(real.compile(r"[é]").search("a café"))
        with self.assertRaises(real.error):
            real.compile(b"[\xc3\xa9]")  # bytes-mode: raw non-ASCII class member -> rejected

    def test_icase_unicode_escape_folds(self):
        # \u00e9 (é) is a code-point literal, so under icase it folds like the raw é -- it matches É
        # (Unicode case folding, CF2). A \xHH escape keeps byte provenance and does NOT fold.
        self.assertIsNotNone(real.compile(r"\u00e9", real.I).search("é"))
        self.assertIsNotNone(real.compile(r"\u00e9", real.I).search("É"))
        self.assertIsNone(real.compile(rb"\xe9", real.I).search(b"\xc9"))  # \xHH byte provenance: no fold

    def test_group_construct_rejections(self):
        for pattern in [r"(?P=name)", r"(?(1)a|b)", r"(a)(?(1)b)"]:
            with self.subTest(pattern=pattern):
                with self.assertRaises(real.error):
                    real.compile(pattern)


if __name__ == "__main__":
    unittest.main()
