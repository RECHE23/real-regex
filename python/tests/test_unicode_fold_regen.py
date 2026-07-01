"""Guard: the committed unicode_fold.hpp is exactly what the generator produces (and re-validates).

This is the CF1 double-filet alongside the C++ contract tests: it regenerates the orbit table in a
temp file -- which re-runs the exhaustive validation against re.IGNORECASE -- and asserts it is
byte-identical to include/real/unicode_fold.hpp. Skipped when the running Python's Unicode version
differs from the header's pin (the generator is deterministic only per Unicode version).
"""
import os
import re
import sys
import tempfile
import unicodedata
import unittest

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_REPO, "scripts"))
import gen_unicode_fold as gen  # noqa: E402

_HEADER = os.path.join(_REPO, "include", "real", "unicode_fold.hpp")


class TestUnicodeFoldRegen(unittest.TestCase):
    def test_header_is_freshly_generated_and_re_faithful(self):
        with open(_HEADER, encoding="utf-8") as f:
            committed = f.read()
        m = re.search(r'unicode_fold_unidata_version \{"([\d.]+)"\}', committed)
        self.assertIsNotNone(m, "header is missing its pinned Unicode version")
        pinned = m.group(1)
        running = unicodedata.unidata_version
        if pinned != running:
            self.skipTest(f"Python Unicode {running} != header pin {pinned}; regenerate the table")

        cased = gen.cased_codepoints()
        orbits = gen.build_orbits(cased)
        gen.validate(orbits, cased)  # re-runs the exhaustive re.IGNORECASE validation (aborts on drift)
        tmp = tempfile.NamedTemporaryFile("w", suffix=".hpp", delete=False)
        tmp.close()
        try:
            gen.emit(orbits, tmp.name)
            with open(tmp.name, encoding="utf-8") as f:
                regenerated = f.read()
        finally:
            os.unlink(tmp.name)
        self.assertEqual(
            regenerated, committed,
            "include/real/unicode_fold.hpp is stale — run scripts/gen_unicode_fold.py")


if __name__ == "__main__":
    unittest.main()
