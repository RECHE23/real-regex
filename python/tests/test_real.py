"""Behavioral tests for the real module (API shape, types, errors)."""

import unittest

import real


class TestCompile(unittest.TestCase):
    def test_compile_returns_pattern(self):
        p = real.compile(r"\d+")
        self.assertIsInstance(p, real.Pattern)
        self.assertEqual(p.pattern, r"\d+")
        self.assertEqual(p.groups, 0)

    def test_compile_pattern_passthrough(self):
        p = real.compile(r"x")
        self.assertIs(real.compile(p), p)
        with self.assertRaises(ValueError):
            real.compile(p, real.I)

    def test_cache(self):
        self.assertIs(real.compile(r"abc"), real.compile(r"abc"))
        real.purge()

    def test_invalid_patterns_raise_error(self):
        for pattern in [r"(", r")", r"a**", r"[z-a]", r"(?=x)", r"(?<=x)",
                        r"(?P=name)", r"(?P<1>x)", r"\q", "a\\"]:
            with self.assertRaises(real.error, msg=pattern):
                real.compile(pattern)

    def test_unsupported_flags_raise(self):
        import re
        for flag in (re.X, re.L, re.DEBUG):
            with self.assertRaises(real.error):
                real.compile("a", int(flag))

    def test_noop_flags_accepted(self):
        self.assertTrue(real.compile("a", real.A).match("a"))
        self.assertTrue(real.compile("a", real.U).match("a"))

    def test_program_size_limit(self):
        # Under cap (a{200} ~200 instr) still works and matches.
        p = real.compile("a{200}")
        self.assertTrue(p.match("a" * 200))
        # Over cap (nested unroll product >> 262144) raises cleanly.
        # This is the #1 security fix: prevents DoS from tiny patterns.
        with self.assertRaises(real.error):
            real.compile("((a{400}){400}){2}")


class TestMatchObject(unittest.TestCase):
    def test_basic_accessors(self):
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
        m = real.fullmatch(r"(a)|(b)", "b")
        self.assertIsNone(m.group(1))
        self.assertEqual(m.groups(), (None, "b"))
        self.assertEqual(m.groups(default=""), ("", "b"))
        self.assertEqual(m.span(1), (-1, -1))

    def test_bad_group_raises(self):
        m = real.search(r"(a)", "a")
        with self.assertRaises(IndexError):
            m.group(2)
        with self.assertRaises(IndexError):
            m.group("nope")

    def test_non_ascii_subject_uses_char_indices(self):
        m = real.search(r"au", "café au lait")
        self.assertEqual(m.span(), (5, 7))
        self.assertEqual(m.group(), "au")
        m = real.search(r"l\w+", "café au lait")
        self.assertEqual(m.span(), (8, 12))


class TestBytes(unittest.TestCase):
    def test_bytes_pattern_and_subject(self):
        p = real.compile(rb"(\d+)")
        m = p.search(b"a 42!")
        self.assertEqual(m.group(0), b"42")
        self.assertEqual(m.span(), (2, 4))
        self.assertEqual(p.findall(b"1 22"), [b"1", b"22"])
        self.assertEqual(p.sub(rb"<\1>", b"a 42!"), b"a <42>!")

    def test_type_mixing_is_rejected(self):
        with self.assertRaises(TypeError):
            real.compile(rb"a").search("a")
        with self.assertRaises(TypeError):
            real.compile(r"a").search(b"a")
        with self.assertRaises(TypeError):
            real.compile(r"a").sub(b"x", "a")


class TestSub(unittest.TestCase):
    def test_template_references(self):
        self.assertEqual(real.sub(r"(\w+)@(\w+)", r"\2:\1", "bob@host"), "host:bob")
        self.assertEqual(real.sub(r"(?P<u>\w+)@", r"\g<u>!", "bob@x"), "bob!x")
        self.assertEqual(real.sub(r"(a)", r"\g<1>\g<0>", "a"), "aa")
        self.assertEqual(real.sub(r"a", r"\n\\", "a"), "\n\\")
        self.assertEqual(real.sub(r"a", r"\!", "a"), r"\!")  # punct keeps backslash

    def test_template_errors(self):
        with self.assertRaises(real.error):
            real.sub(r"(a)", r"\2", "a")
        with self.assertRaises(real.error):
            real.sub(r"a", r"\q", "a")
        with self.assertRaises(real.error):
            real.sub(r"a", r"\g<zz>", "a")

    def test_callable_and_count(self):
        self.assertEqual(real.sub(r"\d+", lambda m: str(int(m.group()) * 2), "3 4"),
                         "6 8")
        self.assertEqual(real.sub(r"x", "-", "xxx", 2), "--x")
        self.assertEqual(real.subn(r"x", "-", "xxx"), ("---", 3))


class TestModuleFunctions(unittest.TestCase):
    def test_match_vs_search_vs_fullmatch(self):
        self.assertIsNone(real.match(r"b", "ab"))
        self.assertIsNotNone(real.search(r"b", "ab"))
        self.assertIsNone(real.fullmatch(r"a", "ab"))
        self.assertIsNotNone(real.fullmatch(r"ab", "ab"))

    def test_findall_shapes(self):
        self.assertEqual(real.findall(r"\d+", "a1 b22"), ["1", "22"])
        self.assertEqual(real.findall(r"(\d)\d", "12 34"), ["1", "3"])
        self.assertEqual(real.findall(r"(a)|(b)", "ab"), [("a", ""), ("", "b")])

    def test_finditer_and_split(self):
        spans = [m.span() for m in real.finditer(r"x*", "axb")]
        self.assertEqual(spans, [(0, 0), (1, 2), (2, 2), (3, 3)])
        self.assertEqual(real.split(r"(x)|(y)", "axbyc"),
                         ["a", "x", None, "b", None, "y", "c"])
        self.assertEqual(real.split(r",", "a,b,c", maxsplit=1), ["a", "b,c"])

    def test_escape_roundtrip(self):
        for text in ["a.b*c", "1+1=2?", "[hi]{2}|x^$", "plain"]:
            self.assertTrue(real.fullmatch(real.escape(text), text), text)
        self.assertTrue(real.fullmatch(real.escape(b"a.b"), b"a.b"))

    def test_flags(self):
        self.assertTrue(real.search(r"hello", "say HELLO", real.I))
        self.assertTrue(real.search(r"^b", "a\nb", real.M))
        self.assertTrue(real.fullmatch(r"a.b", "a\nb", real.S))
        self.assertTrue(real.search(r"(?i)hello", "HELLO"))


class TestNestingDepth(unittest.TestCase):
    def test_deep_nesting_raises_instead_of_crashing(self):
        # Used to overflow the C stack and kill the interpreter.
        with self.assertRaises(real.error):
            real.compile("(" * 50000 + "a" + ")" * 50000)
        self.assertTrue(real.compile("(" * 100 + "a" + ")" * 100).match("a"))


class TestRedosSafety(unittest.TestCase):
    def test_pathological_pattern_is_fast(self):
        import time
        start = time.perf_counter()
        self.assertIsNone(real.search(r"(a+)+b", "a" * 500))
        self.assertLess(time.perf_counter() - start, 0.1)


class TestWordEdgeAnchors(unittest.TestCase):
    # \< and \> are word-start / word-end anchors (a REAL extension; Python re
    # has no equivalent), reusing the same ASCII word-character notion as \b.
    def test_word_start_and_end(self):
        self.assertEqual(real.findall(r"\<\w+", "foo bar-baz"), ["foo", "bar", "baz"])
        self.assertEqual(real.findall(r"\w+\>", "foo bar-baz"), ["foo", "bar", "baz"])

    def test_whole_word_only(self):
        self.assertTrue(real.search(r"\<cat\>", "a cat here"))
        self.assertIsNone(real.search(r"\<cat\>", "category"))
        self.assertIsNone(real.search(r"\<cat\>", "concat"))

    def test_zero_width_at_edges(self):
        self.assertEqual(real.search(r"\<", "abc").span(), (0, 0))
        self.assertEqual(real.search(r"\>", "abc").span(), (3, 3))


class TestCppIntegration(unittest.TestCase):
    def test_get_include_resolves_to_the_headers(self):
        import os
        inc = real.get_include()
        self.assertTrue(os.path.isdir(inc))
        self.assertTrue(os.path.isfile(os.path.join(inc, "real", "real.hpp")))

    def test_get_config(self):
        cfg = real.get_config()
        self.assertEqual(cfg["version"], real.__version__)
        self.assertEqual(cfg["include"], real.get_include())
        self.assertEqual(cfg["cxx_standard"], "c++20")


if __name__ == "__main__":
    unittest.main()
