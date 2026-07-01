"""Guard: the committed unicode_props.hpp is exactly what the generator produces (and re-validates).

Mirrors test_unicode_fold_regen: regenerate the \\w \\d \\s tables in a temp file -- which re-runs the
exhaustive validation against re -- and assert byte-identity with include/real/unicode_props.hpp.
Skipped when the running Python's Unicode version differs from the header's pin (the generator is
deterministic only per Unicode version). Uses only public re / unicodedata, so it runs on any Python.
"""
import os
import re
import sys
import tempfile
import unicodedata
import unittest

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_REPO, "scripts"))
import gen_unicode_props as gen  # noqa: E402

_HEADER = os.path.join(_REPO, "include", "real", "unicode_props.hpp")


class TestUnicodePropsRegen(unittest.TestCase):
    def test_header_is_freshly_generated_and_re_faithful(self):
        with open(_HEADER, encoding="utf-8") as f:
            committed = f.read()
        m = re.search(r'unicode_props_unidata_version \{"([\d.]+)"\}', committed)
        self.assertIsNotNone(m, "header is missing its pinned Unicode version")
        pinned = m.group(1)
        running = unicodedata.unidata_version
        if pinned != running:
            self.skipTest(f"Python Unicode {running} != header pin {pinned}; regenerate the tables")

        tables = {name: gen.build_ranges(pat) for name, pat in gen._PATTERNS.items()}
        for name, pat in gen._PATTERNS.items():
            gen.validate(name, tables[name], pat)  # re-runs the exhaustive re check (aborts on drift)
        tmp = tempfile.NamedTemporaryFile("w", suffix=".hpp", delete=False)
        tmp.close()
        try:
            gen.emit(tables, tmp.name)
            with open(tmp.name, encoding="utf-8") as f:
                regenerated = f.read()
        finally:
            os.unlink(tmp.name)
        self.assertEqual(
            regenerated, committed,
            "include/real/unicode_props.hpp is stale -- run scripts/gen_unicode_props.py")


if __name__ == "__main__":
    unittest.main()
