#!/usr/bin/env python3
"""Can the Unicode property cross-oracle actually run, and if not, which of the two reasons?

The gate used to ask one question -- `import regex` -- and report a skip only when the module was
missing. That made installing the module look like closing the hole: the line would leave
GATE_SKIPS while the oracle stayed mute, because the module's bundled property data is a DIFFERENT
Unicode than the committed UCD sources and every generator declines on that skew (see
`_gen_common.regex_version_skew`). A skip that disappears without the check running is worse than
one that stays.

So this asks both, and names which one bit:

  absent  -- the module is not installed; CI installs it, so this must not be seen there
  skew    -- installed, but a different Unicode: the comparison would say nothing about our tables
  ready   -- the exhaustive comparison can run

Exit status is 0 for `ready` and 1 otherwise, so a caller can branch; `--print-skip` emits the
GATE_SKIPS line instead, so the gate records the real reason rather than a stale one.
"""
from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import _gen_common as common  # noqa: E402 - after the sys.path anchor


def verdict() -> tuple[str, str]:
    """Returns (state, reason) where state is 'ready', 'absent' or 'skew'."""
    try:
        import regex  # noqa: PLC0415 - optional, this is the probe for it
    except ImportError:
        return ("absent", "the `regex` module is not installed in this interpreter")
    skew = common.regex_version_skew(regex)
    if skew is not None:
        return ("skew", skew)
    return ("ready", "the `regex` module agrees with these tables' Unicode")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--print-skip", action="store_true",
                   help="emit the GATE_SKIPS line (nothing when the oracle is ready)")
    args = p.parse_args()

    state, reason = verdict()
    if args.print_skip:
        if state != "ready":
            print(f"step 20: Unicode property cross-oracle did not run -- {reason}")
        return 0
    print(f"check-unicode-oracle: {state} -- {reason}")
    return 0 if state == "ready" else 1


if __name__ == "__main__":
    sys.exit(main())
