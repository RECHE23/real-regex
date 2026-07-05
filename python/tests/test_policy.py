"""The drop-in policy: strict (default) vs fallback, and the real-in-fallback ≡ re oracle."""
import re
import unittest

import real

# Patterns the linear engine cannot represent, but the standard library re can. (Not a bounded
# variable-width lookbehind — real accepts those, and re rejects them; they are real-eligible, not fallback.)
_INELIGIBLE = [
    (r"(\w+)\1", "hello hello world"),          # backreference
    (r"(?P<w>\w+) (?P=w)", "go go stop"),       # named backreference
    (r"(a)(?(1)b|c)", "ab ac"),                 # conditional group
]


class TestStrictDefault(unittest.TestCase):
    def test_ineligible_raises_by_default(self):
        for pat, _ in _INELIGIBLE:
            with self.assertRaises(real.error, msg=pat):
                real.compile(pat)

    def test_eligible_compiles_on_real(self):
        p = real.compile(r"(\w+)@(\w+)")
        self.assertEqual(p.engine, "real")
        self.assertEqual(p.search("a@b").group(2), "b")

    def test_module_functions_are_strict_by_default(self):
        with self.assertRaises(real.error):
            real.search(r"(\w+)\1", "hi hi")


class TestFallbackPolicy(unittest.TestCase):
    def test_fallback_kwarg_delegates_and_is_observable(self):
        p = real.compile(r"(\w+)\1", fallback=True)
        self.assertEqual(p.engine, "re")
        m = p.search("hello hello")
        self.assertIsNotNone(m)
        self.assertIs(m.re, p)            # the wrapped Match points back to the real proxy

    def test_module_level_default(self):
        real.fallback = True
        try:
            self.assertEqual(real.compile(r"(a)\1").engine, "re")
            self.assertIsNotNone(real.search(r"(a)\1", "aa"))
        finally:
            real.fallback = False
        # the per-call kwarg wins over the module default
        with self.assertRaises(real.error):
            real.compile(r"(a)\1", fallback=False)

    def test_eligible_still_uses_real_under_fallback(self):
        p = real.compile(r"\d+", fallback=True)
        self.assertEqual(p.engine, "real")  # fallback only kicks in for ineligible patterns


class TestFallbackEqualsRe(unittest.TestCase):
    """The oracle: a fallback-backed pattern must behave exactly like re — spans, groups, finditer."""

    def test_spans_groups_and_finditer(self):
        for pat, subject in _INELIGIBLE:
            rp = real.compile(pat, fallback=True)
            sp = re.compile(pat)
            rm = rp.search(subject)
            sm = sp.search(subject)
            self.assertEqual(rm is None, sm is None, pat)
            if rm is not None:
                self.assertEqual(rm.span(), sm.span(), pat)
                self.assertEqual(rm.groups(), sm.groups(), pat)
                self.assertEqual(rm.group(0), sm.group(0), pat)
            self.assertEqual([m.span() for m in rp.finditer(subject)],
                             [m.span() for m in sp.finditer(subject)], pat)
            self.assertEqual(rp.findall(subject), sp.findall(subject), pat)

    def test_named_groupdict(self):
        rp = real.compile(r"(?P<w>\w+) (?P=w)", fallback=True)
        m = rp.search("go go stop")
        self.assertEqual(m.groupdict(), {"w": "go"})


if __name__ == "__main__":
    unittest.main()
