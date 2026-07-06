"""Guard: the committed unicode_script.hpp is exactly what the generator produces from the bundled Scripts.txt.

Unlike the \\w\\d\\s and Gc guards, the Script table's source is the committed tools/ucd/Scripts.txt (a
version-pinned UCD file, not the running Python's unicodedata), so there is nothing to skip on a Python
Unicode-version mismatch: regenerate from the same file and assert byte-identity. Catches both header drift and
an unnoticed Scripts.txt change. The gen-time `regex` cross-oracle is optional and does not affect the emitted
bytes, so this runs on any Python.
"""
import os
import sys
import tempfile
import unittest

from _regen_guard import repo_root

sys.path.insert(0, os.path.join(repo_root(), "tools"))
import gen_unicode_script_tables as gen  # noqa: E402

_HEADER = os.path.join(repo_root(), "include", "real", "unicode", "unicode_script.hpp")


class TestUnicodeScriptRegen(unittest.TestCase):
    def test_header_is_freshly_generated_from_scripts_txt(self):
        with open(_HEADER, encoding="utf-8") as f:
            committed = f.read()
        tmp = tempfile.NamedTemporaryFile("w", suffix=".hpp", delete=False)
        tmp.close()
        try:
            gen.generate(tmp.name)
            with open(tmp.name, encoding="utf-8") as f:
                regenerated = f.read()
        finally:
            os.unlink(tmp.name)
        self.assertEqual(committed, regenerated,
                         "include/real/unicode/unicode_script.hpp is stale -- regenerate it from tools/ucd/Scripts.txt")


if __name__ == "__main__":
    unittest.main()
