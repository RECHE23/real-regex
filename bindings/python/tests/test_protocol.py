"""Protocol surface vs re: Pattern.repr, copy, pickle, error fields.

No __print__ exists in Python; print() uses __str__ then __repr__.
re.Pattern pickles (recompile); re.Match does not — those are not one gap.
"""
import copy
import pickle
import re
import unittest

import real


def _re_shaped(text):
    """real.compile(...) → re.compile(...) so a repr can be compared to CPython."""
    return text.replace("real.compile", "re.compile")


class TestPatternRepr(unittest.TestCase):
    def test_repr_mirrors_re(self):
        cases = [
            (r"(?P<w>\w+)", 0),
            (r"(?P<w>\w+)", real.I),
            (r"a", real.I | real.M),
            (r"a", real.I | real.M | real.S | real.X),
            (r"\w+", real.A),
            (b"ab+", 0),
            (b"ab+", real.I),
            ("a'b", 0),
            ('a"b', 0),
        ]
        for pat, flags in cases:
            with self.subTest(pat=pat, flags=flags):
                rp = re.compile(pat, flags)
                pp = real.compile(pat, flags)
                self.assertEqual(_re_shaped(repr(pp)), repr(rp))

    def test_repr_truncates_like_re(self):
        pat = "x" * 250
        self.assertEqual(_re_shaped(repr(real.compile(pat))), repr(re.compile(pat)))

    def test_fallback_repr_is_compile_shaped(self):
        p = real.compile(r"(\w+)\1", fallback=True)
        self.assertEqual(repr(p), f"real.compile({r'(\w+)\1'!r})")


class TestCopyAndDunders(unittest.TestCase):
    def test_pattern_copy_is_self(self):
        p = real.compile(r"ab+", real.I)
        self.assertIs(copy.copy(p), p)
        self.assertIs(copy.deepcopy(p), p)

    def test_match_copy_is_self(self):
        m = real.compile(r"ab+").search("xxabb")
        self.assertIs(copy.copy(m), m)
        self.assertIs(copy.deepcopy(m), m)

    def test_fallback_pattern_copy_is_self(self):
        p = real.compile(r"(\w+)\1", fallback=True)
        self.assertIs(copy.copy(p), p)
        self.assertIs(copy.deepcopy(p), p)

    def test_fallback_match_copy_is_self(self):
        m = real.compile(r"(\w+)\1", fallback=True).search("hello hello")
        self.assertIsNotNone(m)
        self.assertIs(copy.copy(m), m)
        self.assertIs(copy.deepcopy(m), m)

    def test_fallback_match_unknown_dunders_raise(self):
        # __getattr__ must not relay protocol names. object already has
        # __reduce__/__getstate__; these three are absent on both us and
        # object, and a relay would raise as re.Match instead.
        m = real.compile(r"(\w+)\1", fallback=True).search("hello hello")
        for name in ("__iter__", "__enter__", "__next__"):
            with self.subTest(name=name):
                with self.assertRaises(AttributeError) as ctx:
                    getattr(m, name)
                self.assertIn("_FallbackMatch", str(ctx.exception))
                self.assertNotIn("re.Match", str(ctx.exception))


class TestPatternPickle(unittest.TestCase):
    def test_pattern_roundtrip(self):
        p = real.compile(r"(?P<w>\w+)", real.I)
        q = pickle.loads(pickle.dumps(p))
        self.assertEqual(q, p)
        self.assertEqual(q.pattern, p.pattern)
        self.assertEqual(q.flags, p.flags)
        self.assertEqual(q.search("Hello").group("w"), "Hello")

    def test_fallback_pattern_roundtrip(self):
        p = real.compile(r"(\w+)\1", fallback=True)
        q = pickle.loads(pickle.dumps(p))
        self.assertEqual(q.pattern, p.pattern)
        self.assertEqual(q.engine, "re")
        self.assertIsNotNone(q.search("hello hello"))

    def test_match_is_not_pickleable(self):
        m = real.compile(r"ab+").search("xxabb")
        with self.assertRaises(TypeError):
            pickle.dumps(m)
        with self.assertRaises(TypeError):
            pickle.dumps(re.compile(r"ab+").search("xxabb"))


class TestErrorFields(unittest.TestCase):
    def test_compile_syntax_fills_re_fields(self):
        with self.assertRaises(real.error) as ctx:
            real.compile("(")
        err = ctx.exception
        self.assertEqual(err.msg, "missing ), unterminated subpattern")
        self.assertEqual(err.pattern, "(")
        self.assertEqual(err.pos, 0)
        self.assertEqual(err.lineno, 1)
        self.assertEqual(err.colno, 1)

    def test_lineno_colno_from_newlines(self):
        pat = "ab\n("
        with self.assertRaises(real.error) as ctx:
            real.compile(pat)
        err = ctx.exception
        self.assertEqual(err.pattern, pat)
        self.assertEqual(err.pos, 3)
        self.assertEqual(err.lineno, 2)
        self.assertEqual(err.colno, 1)


class TestClassGetItem(unittest.TestCase):
    def test_pattern_and_match_are_subscriptable(self):
        self.assertEqual(str(real.Pattern[str]), "real.Pattern[str]")
        self.assertEqual(str(real.Match[str]), "real.Match[str]")


if __name__ == "__main__":
    unittest.main()
