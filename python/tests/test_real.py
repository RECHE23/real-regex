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

    def test_noop_flags_accepted(self):
        """ASCII/A and UNICODE/U are accepted but have no effect."""
        self.assertTrue(real.compile("a", real.A).match("a"))
        self.assertTrue(real.compile("a", real.U).match("a"))

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


if __name__ == "__main__":
    unittest.main()
