r"""Grep-gate ratchet for the scoped-flags work (a structural net, not a behaviour test).

The parser used to read its flags from global bool members (icase_, ascii_, bytes_, ecma_ — and,
before scoped verbose landed, verbose_) throughout the parse. Scoping a flag means reading it from the
scope stack instead, so each moved flag *removes* global-member reads. This test counts those reads in
the parser and compiler and pins a per-flag ceiling: the count can only ever go DOWN.

- verbose_ is 0 now — scoped `(?x:...)` moved verbose entirely onto the scope stack. It must stay 0.
- icase_ and ascii_ are the scopable flags still read globally; their ceilings fall to 0 as each is
  scoped in turn.
- bytes_ and ecma_ are the intended residual: they are not scopable per pattern, so they stay global.

If a ceiling needs to RISE, that means a new global flag read was added instead of going through the
scope stack — which is exactly what this ratchet exists to prevent. Lower a ceiling when a change
removes reads; never raise one.
"""

import pathlib
import re
import unittest

_HEADERS = ["ast.hpp", "compiler.hpp"]
_FLAGS = ["icase_", "ascii_", "bytes_", "ecma_", "verbose_"]

# The current ceiling per flag member (code occurrences, comments stripped). Monotonic: only lower it.
_CEILING = {"icase_": 4, "ascii_": 5, "bytes_": 13, "ecma_": 9, "verbose_": 0}


def _strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)  # block comments
    src = re.sub(r"//.*", "", src)                    # line comments
    return src


def _count_reads():
    include_dir = pathlib.Path(__file__).resolve().parents[2] / "include" / "real"
    counts = dict.fromkeys(_FLAGS, 0)
    for name in _HEADERS:
        code = _strip_comments((include_dir / name).read_text())
        for flag in _FLAGS:
            counts[flag] += len(re.findall(r"\b" + flag + r"\b", code))
    return counts


class TestFlagScopeRatchet(unittest.TestCase):
    def test_global_flag_reads_do_not_grow(self):
        counts = _count_reads()
        for flag in _FLAGS:
            with self.subTest(flag=flag):
                self.assertLessEqual(
                    counts[flag], _CEILING[flag],
                    "global reads of {} rose to {} (ceiling {}); route the flag through the scope "
                    "stack instead of a global member".format(flag, counts[flag], _CEILING[flag]))

    def test_verbose_is_fully_scoped(self):
        # Scoped (?x:...)'s own invariant: verbose is read from the scope stack, never a global.
        self.assertEqual(_count_reads()["verbose_"], 0)


if __name__ == "__main__":
    unittest.main()
