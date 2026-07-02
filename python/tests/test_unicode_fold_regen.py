"""Guard: the committed unicode_fold.hpp is exactly what the generator produces (and re-validates).

This is the second safety net alongside the C++ contract tests (and uses the shared _regen_guard): it
regenerates the orbit table in a temp file -- which re-runs the exhaustive validation against
re.IGNORECASE -- and asserts byte-identity with include/real/unicode_fold.hpp. Skipped when the
running Python's Unicode version differs from the header's pin (the generator is deterministic only
per Unicode version).
"""
import os
import sys
import unittest

from _regen_guard import assert_regenerates_byte_identical, repo_root

sys.path.insert(0, os.path.join(repo_root(), "scripts"))
try:
    import gen_unicode_fold as gen  # noqa: E402
    _GEN_IMPORT_ERROR = None
except Exception as exc:  # noqa: BLE001 - the generator needs CPython 3.11+ (re._compiler._EXTRA_CASES)
    gen = None
    _GEN_IMPORT_ERROR = exc

_HEADER = os.path.join(repo_root(), "include", "real", "unicode_fold.hpp")


def _regenerate(path):
    cased = gen.cased_codepoints()
    orbits = gen.build_orbits(cased)
    gen.validate(orbits, cased)  # re-runs the exhaustive re.IGNORECASE validation (aborts on drift)
    gen.emit(orbits, path)


class TestUnicodeFoldRegen(unittest.TestCase):
    def test_header_is_freshly_generated_and_re_faithful(self):
        if gen is None:
            self.skipTest(f"generator unavailable (needs CPython 3.11+): {_GEN_IMPORT_ERROR}")
        assert_regenerates_byte_identical(self, _HEADER, "unicode_fold_unidata_version", _regenerate)


if __name__ == "__main__":
    unittest.main()
