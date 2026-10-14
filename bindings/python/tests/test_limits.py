"""real.Limits: a pattern held to limits lower than the build's, refused past one with error.kind == "limit".

Every refusal says why in ``kind``; a pattern past a limit is never delegated to ``re`` by the fallback policy,
which would run it whatever the caller bounded; a limit is a positive int or None, and one past the build's
ceiling is refused when the pattern is compiled.
"""
import unittest

import real


class Limits(unittest.TestCase):
    def assertRefused(self, pattern, limits, kind="limit"):
        with self.assertRaises(real.error) as caught:
            real.compile(pattern, limits=limits)
        self.assertEqual(caught.exception.kind, kind)
        return caught.exception

    def test_each_limit_holds_at_it_and_refuses_past_it(self):
        cases = [
            (real.Limits(repeat_count=5), "a{5}", "a{6}"),
            (real.Limits(group_count=2), "(a)(b)", "(a)(b)(c)"),
            (real.Limits(nesting_depth=3), "(?:(?:(?:a)))", "(?:(?:(?:(?:a))))"),
            (real.Limits(lookaround_length=4), "(?=abcd)a", "(?=abcde)a"),
            (real.Limits(program_size=100), "x" * 20, "x" * 150),
        ]
        for limits, within, past in cases:
            with self.subTest(limits=limits):
                self.assertEqual(real.compile(within, limits=limits).pattern, within)
                self.assertRefused(past, limits)
        error = self.assertRefused("a{6}", real.Limits(repeat_count=5))
        self.assertEqual((error.msg, error.pos), ("repetition count too large", 2))

    def test_the_dfa_memory_is_a_limit_on_the_cache_not_on_the_answer(self):
        small = real.compile(r"\b(?:alpha|beta|gamma)\w*\d", limits=real.Limits(dfa_memory=4096))
        text = "x " * 5000 + "gamma7"
        self.assertEqual(small.search(text).group(), real.compile(r"\b(?:alpha|beta|gamma)\w*\d").search(text).group())

    def test_every_refusal_says_why(self):
        for pattern, kind in [("a)", "syntax"), (r"(a)\1", "unsupported"), ("a{2000}", "limit")]:
            with self.subTest(pattern=pattern), self.assertRaises(real.error) as caught:
                real.compile(pattern)
            self.assertEqual(caught.exception.kind, kind)
        self.assertIsNone(real.error.kind)  # an error raised before the engine has none

    def test_the_fallback_never_runs_a_pattern_past_a_limit(self):
        self.assertRefused("a{6}", real.Limits(repeat_count=5))
        with self.assertRaises(real.error) as caught:
            real.compile("a{6}", fallback=True, limits=real.Limits(repeat_count=5))
        self.assertEqual(caught.exception.kind, "limit")
        with self.assertRaises(real.error):
            real.compile("a{2000}", fallback=True)
        self.assertEqual(real.compile(r"(a)\1", fallback=True).engine, "re")  # unsupported still delegates

    def test_a_limit_is_a_positive_int_and_no_more_than_the_builds(self):
        for value in (0, -1, 1.5, True, "5"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                real.Limits(repeat_count=value)
        self.assertRefused("a", real.Limits(repeat_count=5000))
        self.assertRefused("a", real.Limits(group_count=1 << 40))
        self.assertRefused("a", real.Limits(program_size=1 << 40))  # past a 32-bit size too: never read as 0
        self.assertIsNotNone(real.compile("a", limits=real.Limits(dfa_memory=1 << 34)))  # no ceiling
        with self.assertRaises(TypeError):
            real.compile("a", limits=(1, 2))
        with self.assertRaises(AttributeError):
            real.Limits().repeat_count = 3

    def test_limits_compare_and_print_by_value(self):
        self.assertEqual(real.Limits(repeat_count=5), real.Limits(repeat_count=5))
        self.assertNotEqual(real.Limits(repeat_count=5), real.Limits(repeat_count=6))
        self.assertEqual(hash(real.Limits(group_count=2)), hash(real.Limits(group_count=2)))
        self.assertEqual(repr(real.Limits(repeat_count=5, dfa_memory=4096)),
                         "real.Limits(repeat_count=5, dfa_memory=4096)")
        self.assertNotEqual(real.Limits(), (0,) * 6)

    def test_a_bounded_pattern_is_compiled_apart_from_the_cache(self):
        plain = real.compile("a{6}")
        with self.assertRaises(real.error):
            real.compile("a{6}", limits=real.Limits(repeat_count=5))
        self.assertIs(real.compile("a{6}"), plain)
        self.assertEqual(real.compile(b"a{3}", limits=real.Limits(repeat_count=3)).search(b"aaa").group(), b"aaa")
        self.assertEqual(real.compile(r"\N{DIGIT ONE}", limits=real.Limits(repeat_count=3)).search("1").group(), "1")


if __name__ == "__main__":
    unittest.main()
