"""Guard: the committed unicode_property.hpp is exactly what the generator produces (and re-validates).

Mirrors test_unicode_props_regen (both use _regen_guard), but for the `\\p{Gc}` property tables under the UCD
contract: regenerate them in a temp file -- which re-runs the exhaustive validation against `unicodedata.category`
over every non-surrogate scalar -- and assert byte-identity with include/real/unicode/unicode_property.hpp.
Skipped when the running Python's Unicode version differs from the header's pin (the generator is deterministic
only per Unicode version). Uses only public `unicodedata`, so it runs on any Python. This is the permanent
per-property exhaustive oracle.
"""
import os
import sys
import unittest

from _regen_guard import assert_regenerates_byte_identical, repo_root

sys.path.insert(0, os.path.join(repo_root(), "tools"))
import gen_unicode_property_tables as gen  # noqa: E402

_HEADER = os.path.join(repo_root(), "include", "real", "unicode", "unicode_property.hpp")


class TestUnicodePropertyRegen(unittest.TestCase):
    def test_header_is_freshly_generated_and_ucd_faithful(self):
        assert_regenerates_byte_identical(self, _HEADER, "unicode_property_unidata_version", gen.generate)


if __name__ == "__main__":
    unittest.main()
