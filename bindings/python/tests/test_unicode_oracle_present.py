"""Guard: the Unicode property cross-oracle's ABSENCE can no longer be silent.

The four regen guards each decline the cross-oracle when the `regex` module is missing, print a
reason to stderr, and pass. That is right for a developer machine and wrong for CI, where nothing
read those lines: the module was in no runner's dependency list, so the oracle had never run there
-- while the local gate printed "CI covers those" about it.

Two different states, and only one of them is a defect:

  absent -- nobody installed it. Under CI that is a broken dependency list, so this FAILS.
  skew   -- installed, but its bundled property data is a different Unicode than the committed UCD
            (today: U+088F). The comparison would say nothing about our tables, so declining is
            correct and this test reports it without failing. It is not a pass either: the exhaustive
            comparison still has not run, and the gate's ledger says so in its own line.

The version skew is the reason installing the module does not close the hole, and the reason this
guard checks for presence rather than asserting the oracle ran.
"""
import os
import sys
import unittest

from _regen_guard import repo_root

sys.path.insert(0, os.path.join(repo_root(), "tools"))
import check_unicode_oracle as probe  # noqa: E402


class TestUnicodeOraclePresence(unittest.TestCase):
    def test_ci_installs_the_oracle_module(self):
        state, reason = probe.verdict()
        if os.environ.get("CI"):
            self.assertNotEqual(
                state, "absent",
                "the `regex` module is missing from a CI runner, so every cross-oracle declined "
                f"there and nothing said so: {reason}. Add it to ci.yml's python job `deps`.",
            )
        elif state == "absent":
            self.skipTest(f"not CI, and {reason}")

    def test_the_skew_state_is_reported_not_hidden(self):
        """A skew is a legitimate decline — but it must be nameable, and name a code point."""
        state, reason = probe.verdict()
        if state != "skew":
            self.skipTest(f"the oracle is '{state}', not skewed")
        self.assertIn("U+", reason)          # the disagreeing code point, not a vague version claim
        self.assertRegex(reason, r"UCD \d")  # and which Unicode it was compared against


if __name__ == "__main__":
    unittest.main()
