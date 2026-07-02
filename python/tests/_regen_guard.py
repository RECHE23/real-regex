"""Shared body for the two Unicode-table regen guards (test_unicode_props_regen, test_unicode_fold_regen).

Both guards do the same thing: regenerate the committed header into a temp file -- which re-runs the
generator's exhaustive validation against `re` -- and assert byte-identity. They differ only in which
generator they drive and which header/version-constant they check, so that shape lives here.
"""
import os
import re
import tempfile
import unicodedata


def repo_root():
    """The repository root (three levels up from python/tests/)."""
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def assert_regenerates_byte_identical(test_case, header_path, version_const, regen_to):
    """Regenerate `header_path` and assert it is byte-identical to the committed file.

    Reads the committed header, skips (deterministic-per-version generators) when the running Python's
    Unicode version differs from the header's pin, then calls `regen_to(tmp_path)` -- which must build,
    exhaustively validate against `re`, and emit -- and compares byte-for-byte.

    Args:
        test_case: The unittest.TestCase (for assert/skip).
        header_path: Absolute path of the committed generated header.
        version_const: Name of the emitted `..._unidata_version` constant to read the pin from.
        regen_to: Callable(path) that regenerates the header at `path`.
    """
    with open(header_path, encoding="utf-8") as f:
        committed = f.read()
    match = re.search(re.escape(version_const) + r' \{"([\d.]+)"\}', committed)
    test_case.assertIsNotNone(match, "header is missing its pinned Unicode version")
    pinned = match.group(1)
    running = unicodedata.unidata_version
    if pinned != running:
        test_case.skipTest(f"Python Unicode {running} != header pin {pinned}; regenerate the table")

    tmp = tempfile.NamedTemporaryFile("w", suffix=".hpp", delete=False)
    tmp.close()
    try:
        regen_to(tmp.name)
        with open(tmp.name, encoding="utf-8") as f:
            regenerated = f.read()
    finally:
        os.unlink(tmp.name)
    test_case.assertEqual(
        regenerated, committed,
        f"{os.path.relpath(header_path, repo_root())} is stale -- regenerate it with its scripts/gen_*.py")
