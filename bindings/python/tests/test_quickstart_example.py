r"""Runs bindings/python/examples/quickstart.py — the landing page's Python quickstart tab
(docs/site/_templates/landing.html, injected at build time from that file's `[quickstart]`
region — doc-site P1c) — as part of the normal `python-test` unittest discovery, against the
just-built binding. This is how `python-test`
(both `make python-test` and ci.yml's `python` job, which share the same
`unittest discover -s bindings/python/tests` entry point) proves the code the landing page shows
is exactly the code that's tested, never merely illustrative (doc-site P1b-A gate-snippet).

Chosen over a subprocess: the example file's own top-level `assert` already fails loud with a
useful traceback under `runpy`, in-process, with zero extra plumbing (no PYTHONPATH/env
duplication risk versus the parent unittest process, which subprocess.run would introduce).
"""

import runpy
import unittest
from pathlib import Path

_EXAMPLE = Path(__file__).resolve().parent.parent / "examples" / "quickstart.py"


class TestQuickstartExample(unittest.TestCase):
    def test_quickstart_example_runs(self) -> None:
        self.assertTrue(_EXAMPLE.is_file(), f"quickstart example not found: {_EXAMPLE}")
        runpy.run_path(str(_EXAMPLE), run_name="__main__")


if __name__ == "__main__":
    unittest.main()
