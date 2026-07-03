r"""Grep-gate ratchet for the scoped-flags work (a structural net, not a behaviour test).

The parser used to read its flags from global bool members (icase_, ascii_, bytes_, ecma_, verbose_)
throughout the parse. Scoping a flag means reading it from the scope stack (parser) or the node's
stamped effective_flags (compiler) instead, so each scoped flag *removes* global-member reads. This
test counts those reads in the parser and compiler and pins a per-flag ceiling: it can only go DOWN.

- verbose_, icase_ and ascii_ are 0 now — scoped `(?x:...)`, `(?i:...)` and `(?a:...)` moved them
  entirely onto the scope stack / node bits. They must stay 0.
- bytes_ and ecma_ are the intended residual: they are not scopable per pattern, so they stay global.
  (multiline and dotall are not cached as parser members; they are scoped separately.)

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
_CEILING = {"icase_": 0, "ascii_": 0, "bytes_": 13, "ecma_": 9, "verbose_": 0}


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

    def test_scoped_flags_have_no_global_reads(self):
        # The scoped flags' own invariant: verbose, icase and ascii are read from the scope stack /
        # node bits, never a global member.
        counts = _count_reads()
        for flag in ["verbose_", "icase_", "ascii_"]:
            with self.subTest(flag=flag):
                self.assertEqual(counts[flag], 0)


if __name__ == "__main__":
    unittest.main()
