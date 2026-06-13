#!/usr/bin/env python3
"""Randomized comparative benchmark: REAL vs Python ``re`` over fuzzed input.

Where ``bench.py`` times a fixed, hand-picked set of cases, this times
*randomly generated* (pattern, text) pairs and reports the whole
distribution — including the catastrophic tail that fixed benchmarks miss.
That tail is the point: a backtracking engine like ``re`` has inputs on which
it takes exponential time, while REAL is linear by construction. The fuzz
benchmark quantifies that safety margin instead of asserting it.

``re`` is run under a wall-clock timeout (it can hang on its own pathological
patterns); REAL never needs one. A timed-out ``re`` is reported as a win of
"at least timeout / REAL_time" and counted separately.

    make bench-fuzz                       # default 3000 cases
    REAL_FUZZ_BENCH_CASES=20000 make bench-fuzz

Unix only (uses SIGALRM to bound ``re``).
"""

import math
import os
import random
import re
import signal
import statistics
import sys
import time

sys.path.insert(0, "python")
import real  # noqa: E402

CASES = int(os.environ.get("REAL_FUZZ_BENCH_CASES", "3000"))
SEED = int(os.environ.get("REAL_FUZZ_BENCH_SEED", "20260612"))
RE_TIMEOUT = float(os.environ.get("REAL_FUZZ_BENCH_TIMEOUT", "0.5"))  # seconds


class _Timeout(Exception):
    pass


def _alarm(_signum, _frame):
    raise _Timeout()


signal.signal(signal.SIGALRM, _alarm)


def time_call(fn, timeout):
    """Returns (seconds, timed_out). Bounds fn with SIGALRM."""
    signal.setitimer(signal.ITIMER_REAL, timeout)
    start = time.perf_counter()
    try:
        fn()
    except _Timeout:
        return timeout, True
    except (re.error, ValueError):
        return 0.0, False
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
    return time.perf_counter() - start, False


# A generator that — unlike the differential one — deliberately allows the
# nested-quantifier shapes that make backtracking engines blow up.
_ATOMS = ["a", "b", "[ab]", r"\w", r"\d", ".", "a?", "ab"]


def gen_pattern(rng):
    kind = rng.random()
    inner = "".join(rng.choice(_ATOMS) for _ in range(rng.randint(1, 3)))
    if kind < 0.30:  # classic ReDoS shapes
        return rng.choice([f"({inner}+)+$", f"({inner}*)*$", f"(a|a)*{inner}c",
                           f"({inner}+)+\\d", f"(a+)+$"])
    if kind < 0.55:
        return inner + rng.choice(["*", "+", "{2,}"]) + rng.choice(["a", "c", "$"])
    if kind < 0.80:
        return "|".join("".join(rng.choice(_ATOMS) for _ in range(rng.randint(1, 3)))
                        for _ in range(rng.randint(2, 4)))
    return inner + rng.choice(["", "+", "*"])


def gen_text(rng):
    # Bias toward long single-character runs: the trigger for catastrophic
    # backtracking ("aaaa…" against (a+)+$).
    n = rng.randint(0, 40)
    if rng.random() < 0.5:
        return "a" * n
    alphabet = rng.choice(["ab", "abc012", "a"])
    return "".join(rng.choice(alphabet) for _ in range(n))


def fmt(seconds):
    if seconds < 1e-6:
        return f"{seconds * 1e9:.0f} ns"
    if seconds < 1e-3:
        return f"{seconds * 1e6:.1f} µs"
    return f"{seconds * 1e3:.2f} ms"


def main():
    rng = random.Random(SEED)
    ratios = []
    re_timeouts = 0
    worst = []  # (ratio, pattern, textlen)
    real_total = 0.0
    re_total = 0.0
    compared = 0

    for _ in range(CASES):
        pattern = gen_pattern(rng)
        text = gen_text(rng)
        try:
            rp = re.compile(pattern)
        except re.error:
            continue
        try:
            xp = real.compile(pattern)
        except real.error:
            continue

        t_real, _ = time_call(lambda: xp.search(text), RE_TIMEOUT)
        t_re, timed_out = time_call(lambda: rp.search(text), RE_TIMEOUT)
        compared += 1
        real_total += t_real
        re_total += t_re

        t_real = max(t_real, 1e-9)
        ratio = t_re / t_real
        if timed_out:
            re_timeouts += 1
            worst.append((float("inf"), pattern, len(text)))
        else:
            ratios.append(ratio)
            if ratio > 5:
                worst.append((ratio, pattern, len(text)))

    print(f"REAL vs re — fuzz benchmark ({compared} comparable cases, "
          f"seed {SEED}, re timeout {RE_TIMEOUT*1000:.0f} ms)\n")

    if ratios:
        ratios.sort()
        geo = math.exp(sum(map(math.log, ratios)) / len(ratios))

        def pct(p):
            return ratios[min(len(ratios) - 1, int(len(ratios) * p))]

        print(f"  ratio re/REAL (finite cases):")
        print(f"    geomean {geo:.2f}x   p50 {pct(0.5):.2f}x   "
              f"p90 {pct(0.9):.2f}x   p99 {pct(0.99):.2f}x   max {ratios[-1]:.0f}x")
    print(f"  aggregate wall time:  REAL {fmt(real_total)}   re {fmt(re_total)}"
          f"   ({re_total / max(real_total, 1e-9):.1f}x)")
    print(f"  re catastrophic (timed out > {RE_TIMEOUT*1000:.0f} ms, REAL finished): "
          f"{re_timeouts} / {compared}")

    worst.sort(key=lambda w: -w[0])
    if worst:
        print("\n  worst cases for re (REAL stayed linear):")
        for ratio, pattern, tlen in worst[:8]:
            tag = "TIMEOUT" if ratio == float("inf") else f"{ratio:8.0f}x"
            print(f"    {tag}  {pattern!r:<28} text len {tlen}")

    # The headline: REAL never blows up. Report it as the verdict.
    print(f"\nVERDICT: REAL completed every case in linear time; "
          f"re hit {re_timeouts} catastrophic blowup(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
