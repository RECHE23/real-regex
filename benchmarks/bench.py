#!/usr/bin/env python3
"""REAL vs Python re — a distribution-aware comparative benchmark.

Each case checks result equality between real and re (a fast wrong answer is not a win),
then times them on adjacent (paired) runs to collect a distribution of per-operation times.
The summary is a CI-aware paired geometric mean: REAL is called "faster" only when the
bootstrap confidence interval of the geomean clears 1.0 — otherwise "indecisive".

Cases are grouped into feature families (scaling, density, match/no-match, ASCII/Unicode,
quantifiers & captures) plus a REAL-only differentiator profile (bounded lookarounds and a
pathological ReDoS pattern: REAL stays linear, re backtracks).

Output is dependency-free: an enriched ASCII table (median/IQR/CI/ratio), ASCII box-plots,
and an optional JSON export. A matplotlib reader is a separate, opt-in benchmarks/plot.py
(Phase 2). Run from the repository root: make bench-python  (manual; not a CI gate).

  python benchmarks/bench.py [--json out.json] [--profile]
  BENCH_SAMPLES=40 BENCH_BOOTSTRAP=1000   # tunable via the environment
"""

import argparse
import gc
import json
import os
import platform
import random
import re
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone

sys.path.insert(0, "python")
import real  # noqa: E402
# Dep-free stats from SciForge's shared substrate (sciforge.bench); the sibling
# ../sciforge/python is on PYTHONPATH via the Makefile (make bench-python).
from sciforge.bench import ascii_boxplot, fmt, geomean_ci, median_iqr  # noqa: E402

N_SAMPLES = int(os.environ.get("BENCH_SAMPLES", "40"))
BOOTSTRAP_B = int(os.environ.get("BENCH_BOOTSTRAP", "1000"))
TARGET_BATCH = 0.005  # seconds: inner-loop size so perf_counter overhead is amortised


# ---------------------------------------------------------------------------
# Deterministic corpora
# ---------------------------------------------------------------------------

random.seed(20260610)

WORDS = ["".join(random.choices("abcdefghijklmnopqrstuvwxyz", k=random.randint(2, 10)))
         for _ in range(2000)]


def prose(size):
    out = []
    n = 0
    while n < size:
        w = random.choice(WORDS)
        if random.random() < 0.08:
            w = str(random.randint(0, 99999))
        elif random.random() < 0.03:
            w = w.capitalize() + ","
        out.append(w)
        n += len(w) + 1
        if random.random() < 0.07:
            out.append("\n")
    return " ".join(out)


SIZES = [1_000, 10_000, 100_000, 1_000_000]
PROSE = {size: prose(size) for size in SIZES}
PROSE_100K = PROSE[100_000]
PROSE_1M = PROSE[1_000_000]
LOG_TEXT = "\n".join(
    f"{random.randint(10, 31)}/06/2026 {random.choice(WORDS)} id={random.randint(1, 10**9):08x} "
    f"status={random.choice(['ok', 'err', 'retry'])} user={random.choice(WORDS)}"
    for _ in range(2000))
EMAILS = " ".join(
    random.choice([f"{random.choice(WORDS)}@{random.choice(WORDS)}.com",
                   random.choice(WORDS)])
    for _ in range(5000))
# A Unicode corpus: prose interleaved with accented and CJK runs, to contrast with ASCII.
UNICODE_100K = " ".join(
    random.choice(WORDS) if random.random() < 0.6
    else random.choice(["café", "naïve", "über", "δοκιμή", "日本語", "Ωμέγα", "résumé"])
    for _ in range(20_000))[:100_000]


# ---------------------------------------------------------------------------
# Measurement — paired, gc-disabled, distribution of per-op times
# ---------------------------------------------------------------------------

def _calibrate(fn):
    fn()
    t0 = time.perf_counter()
    fn()
    once = max(time.perf_counter() - t0, 1e-9)
    return max(1, int(TARGET_BATCH / once))


def _batch(fn, n):
    t0 = time.perf_counter()
    for _ in range(n):
        fn()
    return (time.perf_counter() - t0) / n


def collect_pair(re_fn, real_fn, samples=N_SAMPLES):
    """Per-op time distributions for re and REAL, measured on adjacent runs.

    The inner loop (n iterations) amortises perf_counter overhead; the outer loop gathers
    `samples` per-op times each. re and REAL are timed back-to-back per sample so the ratios
    are paired (same machine state). GC is disabled across the timed region.
    """
    re_fn()
    real_fn()  # warmup + first-run caches
    n_re, n_real = _calibrate(re_fn), _calibrate(real_fn)
    re_times, real_times, ratios = [], [], []
    gc.disable()
    try:
        for _ in range(samples):
            r = _batch(re_fn, n_re)
            x = _batch(real_fn, n_real)
            re_times.append(r)
            real_times.append(x)
            ratios.append(r / x)
    finally:
        gc.enable()
    return re_times, real_times, ratios


def collect_one(fn, samples=N_SAMPLES):
    """A per-op time distribution for a single function (REAL-only profiles)."""
    fn()
    n = _calibrate(fn)
    out = []
    gc.disable()
    try:
        for _ in range(samples):
            out.append(_batch(fn, n))
    finally:
        gc.enable()
    return out


# ---------------------------------------------------------------------------
# Cases, grouped into feature families
# ---------------------------------------------------------------------------

CASES = []


def case(name, family, pattern, method, text, *args, flags=0, re_flags=None, key=True):
    """Register a comparative case. `family` groups it; `key=True` cases gate the verdict."""
    CASES.append(dict(name=name, family=family, pattern=pattern, method=method,
                      text=text, args=args, flags=flags, re_flags=re_flags, key=key))


# --- scaling 1KB -> 1MB (the headline: linear-time throughput) -------------
for size in SIZES:
    label = {1_000: "1KB", 10_000: "10KB", 100_000: "100KB", 1_000_000: "1MB"}[size]
    case(f"words findall @{label}", "scaling", r"\w+", "findall", PROSE[size])

# --- match density (sparse vs dense) ---------------------------------------
case("digits sparse @100KB", "density", r"\d+", "findall", PROSE_100K)
case("words dense @100KB", "density", r"\w+", "findall", PROSE_100K)

# --- match vs no-match ------------------------------------------------------
NEEDLE_HIT = PROSE_1M + " needle in the haystack"
case("literal hit @1MB", "match/no-match", r"needle", "search", NEEDLE_HIT)
case("literal miss @1MB", "match/no-match", r"needle", "search", PROSE_1M)
case("anchored miss @1MB", "match/no-match", r"\Aneedle", "search", PROSE_1M)

# --- ASCII vs Unicode -------------------------------------------------------
case("word starts ASCII (m)", "ascii/unicode", r"^\w+", "findall", PROSE_100K,
     flags=real.M, re_flags=re.ASCII | re.MULTILINE)
case("words ASCII @100KB", "ascii/unicode", r"\w+", "findall", PROSE_100K)
case("non-space Unicode", "ascii/unicode", r"\S+", "findall", UNICODE_100K,
     re_flags=re.UNICODE)

# --- quantifiers & captures -------------------------------------------------
DATES = PROSE_100K + " due 2026-06-10 then " + prose(200) + " 1999-12-31."
case("date search @100KB", "quantifiers/captures", r"\d{4}-\d{2}-\d{2}", "search", DATES)
case("date findall groups", "quantifiers/captures", r"(\d{4})-(\d{2})-(\d{2})", "findall", DATES)
case("hex ids findall", "quantifiers/captures", r"id=[0-9a-f]{8}", "findall", LOG_TEXT)
case("emails findall groups", "quantifiers/captures", r"(\w+)@(\w+)\.(\w+)", "findall", EMAILS)
case("alternation findall", "quantifiers/captures", r"cat|dog|bird|fish", "findall", PROSE_100K)
case("sub spaces @100KB", "quantifiers/captures", r"\s+", "sub", PROSE_100K, " ")
case("sub dates refs", "quantifiers/captures", r"(\d{4})-(\d{2})-(\d{2})", "sub", DATES, r"\3/\2/\1")
case("split commas @100KB", "quantifiers/captures", r",\s*", "split", PROSE_100K)


def run_case(spec):
    """Verify equality, then collect the paired distribution. Returns a JSON-ready record
    or None on a result mismatch."""
    rp = re.compile(spec["pattern"], re.ASCII if spec["re_flags"] is None else spec["re_flags"])
    xp = real.compile(spec["pattern"], spec["flags"])
    method, args, text = spec["method"], spec["args"], spec["text"]

    expected = getattr(rp, method)(*args, text)
    got = getattr(xp, method)(*args, text)
    if method == "search":
        expected = None if expected is None else expected.span()
        got = None if got is None else got.span()
    if expected != got:
        return None

    re_times, real_times, ratios = collect_pair(lambda: getattr(rp, method)(*args, text),
                                                lambda: getattr(xp, method)(*args, text))
    median, q1, q3, iqr, mn = median_iqr(ratios)
    _, ci_low, ci_high = geomean_ci(ratios, B=BOOTSTRAP_B)
    return dict(
        name=spec["name"], family=spec["family"], unit="ratio re/real", key=spec["key"],
        re_median=statistics.median(re_times), real_median=statistics.median(real_times),
        samples_re=re_times, samples_real=real_times, ratios=ratios,
        stats=dict(median=median, iqr=iqr, min=mn, ci_low=ci_low, ci_high=ci_high))


# ---------------------------------------------------------------------------
# REAL-only differentiators: bounded lookarounds (linear) + ReDoS
# ---------------------------------------------------------------------------

def lookaround_profile():
    """REAL throughput on a bounded lookaround across sizes, with a linear fit. This is a
    REAL-only profile (re's lookaround backtracks differently): the point is that REAL stays
    linear — a near-1.0 R² and a flat MB/s confirm O(n)."""
    pattern = r"\w+(?=\s)"  # bounded lookahead
    xp = real.compile(pattern)
    points = []
    for size in SIZES:
        text = PROSE[size]
        t = median_iqr(collect_one(lambda: xp.findall(text)))[0]
        points.append((len(text), t, len(text) / t / 1e6))  # (bytes, sec, MB/s)
    slope, intercept, r2 = _linear_fit([(n, t) for n, t, _ in points])
    return points, slope, r2


def _linear_fit(xy):
    xs = [x for x, _ in xy]
    ys = [y for _, y in xy]
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx if sxx else 0.0
    intercept = my - slope * mx
    ss_res = sum((y - (slope * x + intercept)) ** 2 for x, y in xy)
    ss_tot = sum((y - my) ** 2 for y in ys)
    r2 = 1.0 - ss_res / ss_tot if ss_tot else 1.0
    return slope, intercept, r2


def redos_profile():
    """The pathological (a+)+b: re is exponential (kept tiny to measure), REAL is linear on
    a 10k input. Informational — a throughput contrast, not a fair ratio."""
    pattern = r"(a+)+b"
    n_re = 24
    t_re = median_iqr(collect_one(lambda: re.search(pattern, "a" * n_re)))[0]
    t_real = median_iqr(collect_one(lambda: real.search(pattern, "a" * 10_000)))[0]
    return n_re, t_re, t_real


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def git_commit():
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"],
                                       text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "unknown"


def meta():
    return dict(
        bench="real-vs-re", date=datetime.now(timezone.utc).isoformat(timespec="seconds"),
        commit=git_commit(),
        host=platform.node(),
        cpu=platform.processor() or platform.machine(),
        versions=dict(python=sys.version.split()[0], real=real.__version__),
        config=dict(samples=N_SAMPLES, bootstrap=BOOTSTRAP_B))


def main(json_path=None):
    info = meta()
    print(f"REAL {info['versions']['real']} vs re — Python {info['versions']['python']} "
          f"on {info['cpu']} ({info['host']})")
    print(f"samples={N_SAMPLES}  bootstrap={BOOTSTRAP_B}  commit={info['commit']}  {info['date']}\n")

    records, failures = [], []
    by_family = {}
    for spec in CASES:
        record = run_case(spec)
        if record is None:
            failures.append(spec["name"])
            continue
        records.append(record)
        by_family.setdefault(spec["family"], []).append(record)

    header = f"{'case':<24} {'family':<18} {'re':>9} {'REAL':>9} {'ratio (95% CI)':>22}"
    print(header)
    print("-" * len(header))
    for family in dict.fromkeys(spec["family"] for spec in CASES):
        for record in by_family.get(family, []):
            stats = record["stats"]
            ci = f"{stats['median']:.2f}x [{stats['ci_low']:.2f}-{stats['ci_high']:.2f}]"
            print(f"{record['name']:<24} {record['family']:<18} "
                  f"{fmt(record['re_median'], 6)} {fmt(record['real_median'], 6)} {ci:>22}")
    if failures:
        print(f"\n  result mismatches (excluded): {', '.join(failures)}")

    # Box-plots, re vs REAL side by side per case (own time axis), for a few representative
    # cases — a typical win, a differentiator blow-out, and a case where re wins.
    by_name = {r["name"]: r for r in records}
    print("\nper-op time distributions — re vs REAL (left = faster):")
    for name in ["words findall @1MB", "date search @100KB", "emails findall groups"]:
        record = by_name.get(name)
        if record is None:
            continue
        print(f"  {name}:")
        print(ascii_boxplot([record["samples_re"], record["samples_real"]], ["re", "REAL"], width=42))

    # And the headline scaling family as ratio distributions on one shared axis.
    print("\nratio distributions — scaling family (re/real; right = REAL faster):")
    scaling = by_family.get("scaling", [])
    if scaling:
        print(ascii_boxplot([r["ratios"] for r in scaling],
                            [r["name"].replace(" findall", "") for r in scaling], width=46))

    # REAL-only differentiators.
    points, _slope, r2 = lookaround_profile()
    print("\nREAL-only — bounded lookahead throughput (linear-time profile):")
    for n, t, mbps in points:
        print(f"  {n:>9} bytes  {fmt(t, 6)}  {mbps:6.1f} MB/s")
    print(f"  linear fit R² = {r2:.4f}  (≈1.0 confirms O(n), not backtracking)")
    n_re, t_re, t_real = redos_profile()
    print(f"\nREAL-only — ReDoS (a+)+b: re n={n_re} {fmt(t_re, 6)}  vs  REAL n=10000 {fmt(t_real, 6)} "
          f"({t_re / t_real:.0f}x on tiny-vs-large input)")

    # CI-aware verdict over the key cases' median ratios.
    key_medians = [r["stats"]["median"] for r in records if r["key"]]
    geomean, ci_low, ci_high = geomean_ci(key_medians, B=BOOTSTRAP_B)
    if ci_low > 1.0:
        verdict = f"REAL faster overall: {geomean:.2f}x [{ci_low:.2f}-{ci_high:.2f}] (CI clears 1.0)"
        ok = True
    elif ci_high < 1.0:
        verdict = f"REAL slower overall: {geomean:.2f}x [{ci_low:.2f}-{ci_high:.2f}] (CI below 1.0)"
        ok = False
    else:
        verdict = f"indecisive: {geomean:.2f}x [{ci_low:.2f}-{ci_high:.2f}] (CI straddles 1.0)"
        ok = True  # indecisive is not a regression; only a mismatch or a clear-slower fails
    print("-" * len(header))
    print(f"geomean over {len(key_medians)} key cases: {verdict}")

    payload = dict(meta=info, cases=[_json_case(r) for r in records],
                   summary=dict(geomean=geomean, geomean_ci=[ci_low, ci_high], verdict=verdict))
    if json_path:
        with open(json_path, "w") as fh:
            json.dump(payload, fh, indent=2)
        print(f"JSON written: {json_path}")

    ok = ok and not failures
    print("VERDICT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def _json_case(record):
    """A compact JSON record per case (keeps the raw samples for the offline plotter)."""
    return dict(name=record["name"], family=record["family"], unit=record["unit"],
                samples_re=record["samples_re"], samples_real=record["samples_real"],
                ratios=record["ratios"], stats=record["stats"])


def profile_main():
    """cProfile cumulative view of the Python side (C++ hot paths use the C++ micro-benches)."""
    import cProfile
    import io
    import pstats
    pr = cProfile.Profile()
    pr.enable()
    code = main()
    pr.disable()
    s = io.StringIO()
    pstats.Stats(pr, stream=s).sort_stats(pstats.SortKey.CUMULATIVE).print_stats(25)
    print(s.getvalue())
    return code


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="REAL vs re comparative benchmark")
    parser.add_argument("--json", metavar="PATH", help="write the full results as JSON")
    parser.add_argument("--profile", action="store_true", help="cProfile the run")
    opts = parser.parse_args()
    if opts.profile:
        sys.exit(profile_main())
    sys.exit(main(json_path=opts.json))
