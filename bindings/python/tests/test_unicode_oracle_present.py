"""Guard: the Unicode property cross-oracle's ABSENCE can no longer be silent.

The four regen guards each decline the cross-oracle when the `regex` module is missing, print a
reason to stderr, and pass. That is right for a developer machine and wrong for CI, where nothing
read those lines: the module was in no runner's dependency list, so the oracle had never run there
-- while the local gate printed "CI covers those" about it.

Four states; only `absent` under CI is a defect:

  ucd-skew -- the interpreter's unicodedata is not the UCD these tables were built from (3.10
              against UCD 16). The generators decline before they look for `regex`.
  absent   -- nobody installed the module. Under CI that is a broken dependency list, so this FAILS.
  skew     -- installed, but a different Unicode than the interpreter (today: U+088F). Declining
              is correct; the exhaustive comparison still has not run.
  ready    -- the comparison can run.

A check publishes its denominator. `test_oracle_verdict_is_published` prints the state on every
run, because "OK (skipped=8)" does not say whether 3.10 took `ucd-skew` or never reached this file.
"""
import os
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

    def test_ci_installs_the_oracle_module(self):
        state, reason = probe.verdict()
        if os.environ.get("CI"):
            self.assertNotEqual(
                state, "absent",
                "the `regex` module is missing from a CI runner, so every cross-oracle declined "
                f"there and nothing said so: {reason}. Add it to ci.yml's python job `pre-install`.",
            )
        elif state == "absent":
            self.skipTest(f"not CI, and {reason}")

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


if __name__ == "__main__":
    unittest.main()
