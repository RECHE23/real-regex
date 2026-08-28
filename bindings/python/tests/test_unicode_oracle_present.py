"""Guard: the Unicode property cross-oracle's ABSENCE can no longer be silent.

The four regen guards each decline the cross-oracle when the `regex` module is missing, print a
reason to stderr, and pass. That is right for a developer machine and for any runner that did not
declare the oracle -- SciForge's ecosystem job, a downstream packager, a fork with its own CI.
GitHub sets CI=true on every Actions runner, so treating CI as "our python job provisioned regex"
turns a legitimate absence into a red. The declaration is REAL_ORACLE_REQUIRED, set next to
pre-install in ci.yml's test-cmd.

Four states; only `absent` under REAL_ORACLE_REQUIRED is a defect:

  ucd-skew -- the interpreter's unicodedata is not the UCD these tables were built from (3.10
              against UCD 16). The generators decline before they look for `regex`.
  absent   -- nobody installed the module. When we declared the oracle required, that is a
              broken dependency list, so this FAILS.
  skew     -- installed, but a different Unicode than the interpreter (unpinned `regex` after
              2025.9.1 assigns U+088F; our tables and CPython 3.14 are UCD 16.0.0). Declining
              is correct; the comparison does not run against a skewed oracle.
  ready    -- the comparison can run. CI's python 3.14 job pins `regex==2025.9.1` so this is
              the 3.14 path; 3.10 stays `ucd-skew`.

A check publishes its denominator. `test_oracle_verdict_is_published` prints the state on every
run, because "OK (skipped=8)" does not say whether 3.10 took `ucd-skew` or never reached this file.
"""
import os
import re
import sys
import unittest

from _regen_guard import repo_root

sys.path.insert(0, os.path.join(repo_root(), "tools"))
import check_unicode_oracle as probe  # noqa: E402


class TestUnicodeOraclePresence(unittest.TestCase):
    def test_oracle_verdict_is_published(self):
        """Always runs. Silence is how `ready` and `ucd-skew` look the same in a non-verbose log."""
        state, reason = probe.verdict()
        sys.stderr.write(f"check-unicode-oracle: {state} -- {reason}\n")
        sys.stderr.flush()

    def test_declared_oracle_is_installed(self):
        state, reason = probe.verdict()
        if os.environ.get("REAL_ORACLE_REQUIRED"):
            self.assertNotEqual(
                state, "absent",
                "the `regex` module is missing where REAL_ORACLE_REQUIRED=1, so every "
                f"cross-oracle declined and nothing said so: {reason}. Keep it in ci.yml's "
                "python job `pre-install` (next to this flag in `test-cmd`).",
            )
        elif state == "absent":
            self.skipTest(f"REAL_ORACLE_REQUIRED unset, and {reason}")

    def test_every_decline_names_what_it_compared(self):
        """A decline is legitimate — but never vague. Each state must say which two things differed.

        The two skews are different facts and are asserted differently: `skew` is settled by
        behaviour and must name the disagreeing code point, `ucd-skew` has a version string on
        both sides and must show them. A probe that answered `ready` here while a generator
        declined would be the original silence, moved.
        """
        state, reason = probe.verdict()
        if state == "skew":
            self.assertIn("U+", reason)          # the disagreeing code point, not a vague claim
            self.assertRegex(reason, r"UCD \d")  # and which Unicode it was compared against
        elif state == "ucd-skew":
            self.assertRegex(reason, r"\d+\.\d+")     # the interpreter's version …
            self.assertIn(probe.tables_ucd_version(), reason)  # … and the tables' own stamp
        else:
            self.skipTest(f"the oracle is '{state}', not declined for a skew")

    def test_ci_and_gate_venv_pin_the_same_oracle(self):
        """Two copies of the pin. If they drift, CI runs a comparison GATE_STRICT does not, or the reverse."""
        root = repo_root()
        with open(os.path.join(root, ".github", "workflows", "ci.yml"), encoding="utf-8") as fh:
            ci = fh.read()
        with open(os.path.join(root, "Makefile"), encoding="utf-8") as fh:
            makefile = fh.read()
        ci_m = re.search(r"pre-install:\s*'regex==([^']+)'", ci)
        mk_m = re.search(r"mypy 'regex==([^']+)' setuptools", makefile)
        self.assertIsNotNone(ci_m, "ci.yml python job has no regex== pin in pre-install")
        self.assertIsNotNone(mk_m, "Makefile gate-venv has no regex== pin on the pip line")
        self.assertEqual(
            ci_m.group(1), mk_m.group(1),
            "ci.yml pre-install and make gate-venv must pin the same regex; "
            "raise both when the tables move to the next UCD",
        )


if __name__ == "__main__":
    unittest.main()
