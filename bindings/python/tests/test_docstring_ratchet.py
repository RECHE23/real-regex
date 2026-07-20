"""Docstring completeness ratchet over the public Python surface (a structural net).

Every public object must carry a non-empty docstring, and every public callable a
signature ``inspect.signature`` can parse — for the native methods that is the
text-signature convention: a ``name($self, ...)`` first line followed by ``--``,
which CPython lifts into ``__text_signature__`` (and which makes ``help()`` and
IDE tooling show real signatures). A method, function, property or type landing
without either fails here; the surface can only get *more* documented.
"""

import inspect
import unittest

import real


def _public_members(namespace):
    return [(name, value) for name, value in sorted(vars(namespace).items())
            if not name.startswith("_")]


class DocstringRatchetTest(unittest.TestCase):
    def assert_documented(self, qualname, obj):
        self.assertTrue(getattr(obj, "__doc__", None), f"{qualname}: empty __doc__")

    def assert_signature(self, qualname, obj):
        try:
            inspect.signature(obj)
        except (TypeError, ValueError) as exc:
            self.fail(f"{qualname}: inspect.signature failed ({exc})")

    def check_type(self, tp):
        self.assert_documented(tp.__name__, tp)
        for name, member in _public_members(tp):
            qualname = f"{tp.__name__}.{name}"
            self.assert_documented(qualname, member)
            if inspect.isroutine(member):
                self.assert_signature(qualname, member)

    def test_native_types(self):
        pattern = real.compile("a")
        for tp in (real.Pattern, real.Match,
                   type(pattern.finditer("a")),
                   type(real.RegexSet(["a"])._set)):
            self.check_type(tp)

    def test_native_module(self):
        self.assert_documented("real._real", real._real)
        self.assert_documented("real._real.compile", real._real.compile)
        self.assert_signature("real._real.compile", real._real.compile)
        self.assert_documented("real.error", real.error)

    def test_wrapper_module(self):
        self.assert_documented("real", real)
        for name in real.__all__:
            obj = getattr(real, name)
            if inspect.isroutine(obj):
                self.assert_documented(f"real.{name}", obj)
                self.assert_signature(f"real.{name}", obj)

    def test_wrapper_classes(self):
        # The fallback classes are private, but their instances are the public
        # surface whenever fallback=True compiles through re.
        for tp in (real.RegexSet, real._FallbackPattern, real._FallbackMatch):
            self.check_type(tp)


if __name__ == "__main__":
    unittest.main()
