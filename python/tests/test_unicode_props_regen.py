"""Guard: the committed unicode_props.hpp is exactly what the generator produces (and re-validates).

Mirrors test_unicode_fold_regen (both use _regen_guard): regenerate the \\w \\d \\s tables in a temp
file -- which re-runs the exhaustive validation against re -- and assert byte-identity with
include/real/unicode_props.hpp. Skipped when the running Python's Unicode version differs from the
header's pin (the generator is deterministic only per Unicode version). Uses only public
re / unicodedata, so it runs on any Python.
"""
import os
import sys
import unittest

from _regen_guard import assert_regenerates_byte_identical, repo_root

sys.path.insert(0, os.path.join(repo_root(), "scripts"))
import gen_unicode_props as gen  # noqa: E402

_HEADER = os.path.join(repo_root(), "include", "real", "unicode_props.hpp")


def _regenerate(path):
    tables = {name: gen.build_ranges(pat) for name, pat in gen._PATTERNS.items()}
    for name, pat in gen._PATTERNS.items():
        gen.validate(name, tables[name], pat)  # re-runs the exhaustive re check (aborts on drift)
    gen.emit(tables, path)


class TestUnicodePropsRegen(unittest.TestCase):
    def test_header_is_freshly_generated_and_re_faithful(self):
        assert_regenerates_byte_identical(self, _HEADER, "unicode_props_unidata_version", _regenerate)


if __name__ == "__main__":
    unittest.main()
