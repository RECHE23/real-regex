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
              CPython whose Unicode predates the tables (3.11 against UCD 16), the module can be
              installed and agree with its interpreter while the oracle still does not run.
  absent   -- the module is not installed; CI installs it, so this must not be seen there
  skew     -- installed, but a different Unicode than the interpreter: the comparison would say
              nothing about our tables
  ready    -- the exhaustive comparison can run

Exit status is 0 for `ready` and 1 otherwise, so a caller can branch; `--print-skip` emits the
GATE_SKIPS line instead, so the gate records the real reason rather than a stale one. `--self-test`
drives each state, and the order of the questions, with the environment substituted.
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


def _import_regex():
    import regex  # noqa: PLC0415 - optional, this is the probe for it
    return regex


def verdict(stamp_of=tables_ucd_version, ucd_skew_of=None, import_regex=_import_regex,
            regex_skew_of=None) -> tuple[str, str]:
    """Returns (state, reason): 'ready', 'ucd-skew', 'absent' or 'skew'.

    The three declines are asked in the generators' own order (`_cross_check_regex`), because a
    probe that asks fewer questions than the check it stands for can answer 'ready' about a check
    that will not run — which is the failure this whole file exists to end, not to relocate. The
    parameters are the environment; the defaults are the real one, and the self-test substitutes it.
    """
    ucd_skew_of = ucd_skew_of or common.cross_oracle_skew
    regex_skew_of = regex_skew_of or common.regex_version_skew
    stamp = stamp_of()
    if stamp is None:
        return ("absent", "the tables' UCD stamp could not be read; the oracle's premise is unknown")
    ucd_skew = ucd_skew_of(stamp)
    if ucd_skew is not None:
        return ("ucd-skew", ucd_skew)
    try:
        regex = import_regex()
    except ImportError:
        return ("absent", "the `regex` module is not installed in this interpreter")
    skew = regex_skew_of(regex)
    if skew is not None:
        return ("skew", skew)
    return ("ready", f"the interpreter and the `regex` module both agree with UCD {stamp}")


def self_test() -> int:
    """Drives each state with a substituted environment, including the order of the questions."""
    def missing():
        raise ImportError("no regex")

    fine = dict(stamp_of=lambda: "16.0.0", ucd_skew_of=lambda s: None, import_regex=lambda: object(),
                regex_skew_of=lambda r: None)
    cases = [
        ("the tables' stamp cannot be read", dict(fine, stamp_of=lambda: None), "absent", "stamp could not be read"),
        ("the interpreter's UCD differs from the tables'", dict(fine, ucd_skew_of=lambda s: "UCD 15.1 vs 16.0"),
         "ucd-skew", "UCD 15.1 vs 16.0"),
        # The reason this file exists: a UCD skew must be named even when the module is ALSO absent,
        # because installing the module would not make the oracle run.
        ("a UCD skew outranks an absent module",
         dict(fine, ucd_skew_of=lambda s: "UCD 15.1 vs 16.0", import_regex=missing), "ucd-skew", "UCD 15.1"),
        ("the module is not installed", dict(fine, import_regex=missing), "absent", "not installed"),
        ("the module's Unicode differs", dict(fine, regex_skew_of=lambda r: "regex UCD 15.0"), "skew", "regex UCD 15.0"),
        ("everything agrees", fine, "ready", "both agree with UCD 16.0.0"),
    ]
    failures = 0
    for name, env, want_state, want_reason in cases:
        try:
            state, reason = verdict(**env)
        except Exception as exc:
            print(f"SELF-TEST FAILED: {name}: verdict raised {type(exc).__name__}: {exc}")
            failures += 1
            continue
        if state != want_state or want_reason not in reason:
            print(f"SELF-TEST FAILED: {name}: got ({state!r}, {reason!r}), want state {want_state!r} "
                  f"with {want_reason!r}")
            failures += 1
    if failures:
        print(f"check-unicode-oracle: self-test FAILED ({failures} of {len(cases)} case(s))")
        return 1
    print(f"check-unicode-oracle: self-test OK — {len(cases)} cases: each state reached alone, and a UCD skew "
          "named ahead of an absent module")
    return 0


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--print-skip", action="store_true",
                   help="emit the GATE_SKIPS line (nothing when the oracle is ready)")
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        return self_test()

    state, reason = verdict()
    if args.print_skip:
        if state != "ready":
            print(f"step 20: Unicode property cross-oracle did not run -- {reason}")
        return 0
    print(f"check-unicode-oracle: {state} -- {reason}")
    return 0 if state == "ready" else 1


if __name__ == "__main__":
    sys.exit(main())
