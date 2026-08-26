#!/usr/bin/env python3
"""Can the Unicode property cross-oracle actually run, and if not, which of the two reasons?

The gate used to ask one question -- `import regex` -- and report a skip only when the module was
missing. That made installing the module look like closing the hole: the line would leave
GATE_SKIPS while the oracle stayed mute, because the module's bundled property data is a DIFFERENT
Unicode than the committed UCD sources and every generator declines on that skew (see
`_gen_common.regex_version_skew`). A skip that disappears without the check running is worse than
one that stays.

So this asks every question `_cross_check_regex` asks, in its order, and names which one bit:

  ucd-skew -- the INTERPRETER's unicodedata is not the UCD these tables were built from, so the
              assigned-code-point filter the oracle needs cannot be trusted. This is the first
              decline and the one a probe that only looked for `regex` could not see: on a
              CPython whose Unicode predates the tables (3.10 against UCD 16), the module can be
              installed and agree with its interpreter while the oracle still does not run.
  absent   -- the module is not installed; CI installs it, so this must not be seen there
  skew     -- installed, but a different Unicode than the interpreter: the comparison would say
              nothing about our tables
  ready    -- the exhaustive comparison can run

Exit status is 0 for `ready` and 1 otherwise, so a caller can branch; `--print-skip` emits the
GATE_SKIPS line instead, so the gate records the real reason rather than a stale one.
"""
from __future__ import annotations

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import _gen_common as common  # noqa: E402 - after the sys.path anchor


_STAMP_HEADER = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "include", "real", "unicode", "unicode_binprop.hpp")
_STAMP_RE = re.compile(r'unicode_binprop_unidata_version\s*\{"([^"]+)"\}')


def tables_ucd_version() -> str | None:
    """The UCD the committed tables were generated from, read off their own stamp.

    The generators derive it by parsing the UCD sources; this reads the constant they emitted, so
    the probe cannot drift from the headers it is asking about.
    """
    try:
        with open(_STAMP_HEADER, encoding="utf-8") as fh:
            m = _STAMP_RE.search(fh.read())
    except OSError:
        return None
    return m.group(1) if m else None


def verdict() -> tuple[str, str]:
    """Returns (state, reason): 'ready', 'ucd-skew', 'absent' or 'skew'.

    The three declines are asked in the generators' own order (`_cross_check_regex`), because a
    probe that asks fewer questions than the check it stands for can answer 'ready' about a check
    that will not run — which is the failure this whole file exists to end, not to relocate.
    """
    stamp = tables_ucd_version()
    if stamp is None:
        return ("absent", "the tables' UCD stamp could not be read; the oracle's premise is unknown")
    ucd_skew = common.cross_oracle_skew(stamp)
    if ucd_skew is not None:
        return ("ucd-skew", ucd_skew)
    try:
        import regex  # noqa: PLC0415 - optional, this is the probe for it
    except ImportError:
        return ("absent", "the `regex` module is not installed in this interpreter")
    skew = common.regex_version_skew(regex)
    if skew is not None:
        return ("skew", skew)
    return ("ready", f"the interpreter and the `regex` module both agree with UCD {stamp}")


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
