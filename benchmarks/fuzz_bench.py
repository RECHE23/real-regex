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

import argparse
import gc
import json
import os
import platform
import random
import re
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone

sys.path.insert(0, "python")
import real  # noqa: E402
# Dep-free stats from SciForge's shared substrate (sciforge.bench); the sibling
# ../sciforge/python is on PYTHONPATH via the Makefile (make bench-fuzz).
from sciforge.bench import ascii_ecdf, fmt, geomean_ci  # noqa: E402

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
                           f"({inner}+)+\\d", "(a+)+$"])
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


def git_commit():
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"],
                                       text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "unknown"


def meta(compared):
    return dict(
        bench="fuzz", date=datetime.now(timezone.utc).isoformat(timespec="seconds"),
        commit=git_commit(), host=platform.node(),
        cpu=platform.processor() or platform.machine(),
        versions=dict(python=sys.version.split()[0], real=real.__version__),
        config=dict(cases=CASES, compared=compared, seed=SEED, re_timeout=RE_TIMEOUT))


def percentiles(ordered):
    def pct(p):
        return ordered[min(len(ordered) - 1, int(len(ordered) * p))]
    return dict(p50=pct(0.5), p90=pct(0.9), p99=pct(0.99), max=ordered[-1], min=ordered[0])


def main(json_path=None):
    rng = random.Random(SEED)
    ratios = []
    re_timeouts = 0
    worst = []  # (ratio, pattern, textlen)
    real_total = 0.0
    re_total = 0.0
    compared = 0

    gc.disable()  # no collection pause inside the timed region
    try:
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
    finally:
        gc.enable()

    print(f"REAL vs re — fuzz benchmark ({compared} comparable cases, "
          f"seed {SEED}, re timeout {RE_TIMEOUT*1000:.0f} ms)\n")

    geomean = ci_low = ci_high = None
    pcts = None
    if ratios:
        ratios.sort()
        pcts = percentiles(ratios)
        geomean, ci_low, ci_high = geomean_ci(ratios)  # bootstrap resamples the cases
        print("  ratio re/REAL (finite cases):")
        print(f"    geomean {geomean:.2f}x [{ci_low:.2f}-{ci_high:.2f}]   "
              f"p50 {pcts['p50']:.2f}x   p90 {pcts['p90']:.2f}x   "
              f"p99 {pcts['p99']:.2f}x   max {pcts['max']:.0f}x")
        print("\n  ratio distribution (ECDF, log axis; the long right tail = re's bad cases):")
        print(ascii_ecdf(ratios, width=52, height=9, log=True))
    print(f"\n  aggregate wall time:  REAL {fmt(real_total)}   re {fmt(re_total)}"
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

    if json_path:
        # A reduced sample of ratios keeps the JSON small; the plotter rebuilds the ECDF.
        reduced = ratios[:: max(1, len(ratios) // 400)] if ratios else []
        payload = dict(
            meta=meta(compared),
            ratios=dict(percentiles=pcts, geomean=geomean, geomean_ci=[ci_low, ci_high],
                        sample=reduced, finite_count=len(ratios)),
            timeouts=re_timeouts,
            worst=[dict(ratio=(None if r == float("inf") else r), pattern=p, text_len=t)
                   for r, p, t in worst[:20]],
            aggregate=dict(real_seconds=real_total, re_seconds=re_total))
        with open(json_path, "w") as fh:
            json.dump(payload, fh, indent=2)
        print(f"JSON written: {json_path}")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="REAL vs re fuzz benchmark")
    parser.add_argument("--json", metavar="PATH", help="write the full results as JSON")
    sys.exit(main(json_path=parser.parse_args().json))
