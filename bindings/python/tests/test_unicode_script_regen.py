"""Guard: the committed unicode_script.hpp is exactly what the generator produces from the bundled Scripts.txt.

Mirrors test_unicode_props_regen / test_unicode_property_regen (all three share _regen_guard): regenerate the
header and assert byte-identity. The Script *tables* come from the committed tools/ucd/Scripts.txt (16.0.0) and
are interpreter-independent — but the emitted `..._unidata_version` STAMP comes from the running interpreter's
`unicodedata` (via the shared file header), so on a Python whose Unicode version differs from the header's pin
the regeneration cannot reproduce the stamp. Like the sibling guards (and the \\B-version-variant skip), we
therefore skip-with-a-named-reason when the versions differ: the tables never vary, and the version-independent
C++ guard (test_unicode_script) keeps every Python floor covered.
"""
import os
import sys

from _regen_guard import assert_regenerates_byte_identical, repo_root

sys.path.insert(0, os.path.join(repo_root(), "tools"))
import gen_unicode_script_tables as gen  # noqa: E402
import unittest  # noqa: E402

_HEADER = os.path.join(repo_root(), "include", "real", "unicode", "unicode_script.hpp")


class TestUnicodeScriptRegen(unittest.TestCase):
    def test_header_is_freshly_generated_from_scripts_txt(self):
        assert_regenerates_byte_identical(self, _HEADER, "unicode_script_unidata_version", gen.generate)


if __name__ == "__main__":
    unittest.main()
