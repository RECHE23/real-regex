#!/usr/bin/env python3
"""Assert that `count_walk` still hands its walk an INTENT, not a mutated view.

WHY THIS EXISTS, AND WHY IT IS NOT A BENCHMARK. `count_matches` runs its walk with the capture-free
policy set on the program view. The obvious way to write that -- copy the view into a local, set the
bit, pass the local -- costs a SECOND copy of `detail::program_view`, which is 440 bytes. That shipped
as 23f27ec and charged a fixed +12 to +13 ns on every `count_matches` call: +6.8 % on a 3-byte subject,
+6.3 % at 8 bytes, +5.9 % at 24, with 6 of 6 paired interleaved draws agreeing and the base and
candidate ranges not overlapping. 36a5106 fixed it by passing the intent instead, so the range sets the
bit on the member it was going to copy into anyway.

WHY A STOPWATCH CANNOT GUARD IT. The cost is FIXED, so it disappears into any throughput measurement:
at 4 KiB it amortises to about 0.3 %, and the regression moved NOT ONE of the 26 rows the layout
instrument then had. Adding a short `count_matches` row does not work either -- there is no cheap one.
Seven shapes measured between 185 and 245 ns per call, because the cost is the range-plus-iterator
entry (on the same 35-byte miss: `search` 70 ns, `count_matches` 214, `find_iter` 213), and the two
candidate rows' noise floors came out at 6.2 % and 6.6 %, the worst two of 28, against an effect worth
about 6.5 %. A floor the size of the effect is not a guard. So this counts the SHAPE instead, at zero
nanoseconds and with no floor to clear -- the same reasoning v2026.8.14 used when it found five
allocations per call by size and by symbol rather than by stopwatch.

WHAT WOULD BRING IT BACK. Someone finds the argument list long, hoists a `program_view` local back into
`count_walk`, and sets the bit there. Everything still passes: the answers are identical, every gate is
green, and the layout judgment reports 26 rows of silence.

SCOPE, STATED SO IT IS NOT MISREAD AS BROADER. This gate makes exactly three claims about one function.
It does NOT claim that no local `program_view` exists anywhere -- `basic_regex::may_start_with` builds
one to read two hint fields, which is the same smell on a path this gate deliberately does not cover,
because a guard that quietly grows past what it measured is how a check starts lying.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "include" / "real" / "real.hpp"

#: The claims, as (predicate name, compiled regex, what a failure means).
CLAIMS = (
    (
        "no local view",
        re.compile(r"\bprogram_view\s+\w+\s*[{=]"),
        False,  # must NOT appear
        "declares a local `program_view`. That is the second 440-byte copy: the range copies what it is\n"
        "    handed, so a mutated local is copied twice. Pass `matching_only` and let the range set the bit.",
    ),
    (
        "builds from program_.view()",
        re.compile(r"program_\.view\(\)"),
        True,  # must appear
        "no longer constructs its range straight from `program_.view()`. Anything in between is a copy.",
    ),
    (
        "asks for the matching-only walk",
        # The CALL's own shape, not a bare `true`: `matching_only` is the last argument of
        # `basic_match_range`, right behind the semantics. A loose `\btrue\b` was satisfied by any other
        # `true` someone later added to this body -- it would have gone on passing while the thing it
        # claims to pin drifted, which is the failure mode this whole file exists to refuse.
        re.compile(r"match_semantics::first\s*,\s*true"),
        True,  # must appear -- the matching_only argument
        "no longer passes the matching-only argument behind the semantics, so `count_matches` silently\n"
        "    went back to writing capture groups nobody reads. That is a performance regression with no\n"
        "    failing test behind it.",
    ),
)


def body_of(text, name):
    """The brace-balanced body of `name`, or None. Starts at the first `{` after the signature."""
    start = text.find(f"std::size_t {name}(")
    if start < 0:
        return None
    open_brace = text.find("{", text.find(")", start))
    if open_brace < 0:
        return None
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace : i + 1]
    return None


def main():
    text = HEADER.read_text(encoding="utf-8")
    body = body_of(text, "count_walk")
    if body is None:
        print("check_percall_copies: FAIL -- count_walk not found in include/real/real.hpp.")
        print("    If it was renamed, rename it here too; if it was inlined back into count_matches,")
        print("    this gate's whole subject is gone and the removal needs its own reasoning.")
        return 1

    # Comments carry the rationale and would otherwise satisfy or trip every claim below.
    code = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)
    code = re.sub(r"//[^\n]*", " ", code)

    failures = []
    for name, pattern, must_appear, why in CLAIMS:
        found = pattern.search(code) is not None
        if found != must_appear:
            failures.append((name, why))

    if failures:
        print("check_percall_copies: FAIL -- count_walk's shape changed:")
        for name, why in failures:
            print(f"  [{name}] count_walk {why}")
        print()
        print("This is a per-CALL cost, so no benchmark in this repository will tell you: it is fixed,")
        print("it vanishes into throughput rows, and a short count_matches row cannot be built (see")
        print("benchmarks/bench_minimal.cpp's refusal note). Fix the shape, do not re-time it.")
        return 1

    print("check_percall_copies: OK -- count_walk passes the intent, not a mutated view")
    return 0


if __name__ == "__main__":
    sys.exit(main())
