#!/usr/bin/env python3
"""RegexSet before/after: the old Python N-loop vs the new native real::regex_set wire-in (R4).

Before (kept here ONLY as the timed baseline, not production code): RegexSet.matches() looped
N individual Pattern.search() calls in Python. After: RegexSet wraps real::regex_set directly
(bindings/python/src/_real.cpp's RegexSetObject) -- Stage-1 N-walks run inside C++ (no per-pattern
Python call/return overhead), and a large-enough DFA-eligible subset (>= regex_set.hpp's
fused_min_eligible, 56) instead takes a single fused multi-accept DFA pass (Stage-2), a code path
the old loop could never take at all.

Two corpora: SMALL (below the fused threshold -- isolates the Python-call-overhead removal alone)
and LARGE (>= 56 DFA-eligible patterns -- also exercises the fused Stage-2 pass). Best-of-N wall
time, matches-per-second derived. Not a CI gate (informational, like the other benchmarks/*.py).

Run manually:  PYTHONPATH=bindings/python python3 benchmarks/regexset_bench.py
"""
import time

import real


def n_loop_matches(patterns, text):
    """The exact algorithm RegexSet.matches() used before R4 -- the timed baseline."""
    return [p.search(text) is not None for p in patterns]


def best_ms(fn, reps):
    fn()
    best = float("inf")
    for _ in range(reps):
        t0 = time.perf_counter()
        fn()
        t1 = time.perf_counter()
        best = min(best, (t1 - t0) * 1000.0)
    return best


def bench(label, pattern_strs, text, reps=200):
    patterns = [real.compile(p) for p in pattern_strs]
    rs = real.RegexSet(pattern_strs)

    before_ms = best_ms(lambda: n_loop_matches(patterns, text), reps)
    after_ms = best_ms(lambda: rs.matches(text), reps)

    reference = n_loop_matches(patterns, text)
    assert rs.matches(text) == reference, f"{label}: native/N-loop parity broke"

    ratio = after_ms / before_ms
    print(f"{label:32s} n={len(pattern_strs):3d} before={before_ms:8.4f}ms "
          f"after={after_ms:8.4f}ms ratio={ratio:.3f} "
          f"({'faster' if ratio < 1 else 'slower'} {abs(1 - ratio) * 100:.1f}%)")


def main():
    small_patterns = [
        "alpha", "beta", "gamma", "[0-9]+", "[a-z]+", "[A-Z]+", r"\d{2,4}",
        "^start", "end$", "a|b|c", "foo(bar)?", "[^x]+", "colou?r", "(?:ab)+",
    ]
    small_text = "gamma matches here, and 1234, colour too, at the very end"

    large_patterns = ([f"tok{i:03d}" for i in range(40)] +
                      [rf"[a-{chr(ord('a') + (i % 20))}]+{i}" for i in range(30)])
    # 1 MiB-scale, like regex_set.hpp's own fused_min_eligible calibration note: the fused
    # single-pass DFA's advantage over N separate Python-level searches is in NOT re-scanning a
    # large subject N times, so it only shows up once the subject is actually large.
    unit = "nothing_here_at_all zzzz29 aaaa5 bbbb12 filler words go here too "
    large_text = (unit * (1 + (1_000_000 // len(unit)))) + "tok017 tok038"

    print("RegexSet before/after (N-loop vs native real::regex_set):")
    bench("small (Stage-1, below fused)", small_patterns, small_text)
    bench("large (>=56, Stage-2 fused, ~1MiB)", large_patterns, large_text, reps=20)


if __name__ == "__main__":
    main()
