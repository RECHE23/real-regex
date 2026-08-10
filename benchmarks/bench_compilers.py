#!/usr/bin/env python3
"""Same source, two compilers, one table -- the leg this project did not have.

WHY THIS EXISTS. Several optimisations in the engine deliberately leave memory uninitialised, and their
comments carry measured figures: "a 496-byte clear per search()", "~30 % of the instruction count",
"constructing plus destroying this state costs 9.5 ns". Those numbers are true under clang and false under
g++. Measured on one x86-64 host, constructing `dynamic_storage::state_type` (7 736 bytes) with a real
compiler barrier: clang++ 20 takes 13.5 ns, g++ 15 takes 43.8 ns -- 3.2x, from the compiler alone. g++ bulk
zeroes an aggregate that clang tracks precisely, so the saving those comments describe does not happen where
most Linux users compile.

Nothing here could see it, and that is the point being fixed:

  * local development and `bench_layout.py` run Apple clang;
  * the gate's GCC leg checks CORRECTNESS only, never performance;
  * the throughput rows use 100 KB corpora, where 30 ns per call is 0.0003 ns/byte.

Each is defensible alone. Together they hide a whole class of optimisation that does not fire on the
compiler a deployment actually uses. This script asks the question directly: same source, each available
compiler, per-row ratio.

It reports rather than gates. A ratio far from 1.0 is not a defect by itself -- compilers differ -- but it
IS a claim that any "leave it uninitialised" note in the engine must survive on both, and a row whose ratio
moves after such a change is the signal that it did not.

    make bench-compilers            # every compiler found
    make bench-compilers SAMPLES=60 # more readings per row
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "benchmarks", "bench_minimal.cpp")
CANDIDATES = ["clang++", "g++", "g++-14", "g++-15"]


def compilers():
    """Every candidate present on PATH, de-duplicated by the version string it reports."""
    found, seen = [], set()
    for cc in CANDIDATES:
        path = shutil.which(cc)
        if path is None:
            continue
        try:
            ver = subprocess.run([cc, "--version"], capture_output=True, text=True,
                                 check=False).stdout.splitlines()[0]
        except (OSError, IndexError):
            continue
        if ver in seen:
            continue
        seen.add(ver)
        found.append((cc, ver))
    return found


def measure(cc, tmp, includes, samples):
    """Builds and runs bench_minimal with `cc`; returns {row name: best ns}."""
    exe = os.path.join(tmp, "bench_minimal_" + cc.replace("+", "p"))
    cmd = [cc, "-std=c++20", "-O2", "-DNDEBUG"] + includes + [SOURCE, "-o", exe]
    build = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if build.returncode != 0:
        print(f"  {cc}: build failed -- skipped", file=sys.stderr)
        print(build.stderr.strip().splitlines()[-1] if build.stderr.strip() else "", file=sys.stderr)
        return None
    env = dict(os.environ, BENCH_SAMPLES=str(samples))
    run = subprocess.run([exe], capture_output=True, text=True, check=False, env=env)
    if run.returncode != 0:
        print(f"  {cc}: run failed ({run.returncode}) -- skipped", file=sys.stderr)
        return None
    doc = json.loads(run.stdout)
    # The best (minimum) reading per row: a noise floor, not a distribution -- this is a comparison
    # between binaries, and the minimum is the reading least polluted by whatever else the host is doing.
    return {c["name"]: min(c["engines"]["real"]["samples"]) for c in doc["cases"]}


def main():
    samples = int(os.environ.get("SAMPLES", "30"))
    sciforge = os.environ.get("SCIFORGE_INCLUDE", os.path.join(ROOT, "..", "sciforge", "include"))
    includes = ["-I" + os.path.join(ROOT, "include"), "-I" + sciforge]

    found = compilers()
    if len(found) < 2:
        names = ", ".join(cc for cc, _ in found) or "none"
        print(f"bench-compilers: need two compilers, found: {names}")
        return 0

    print("bench-compilers -- same source, one row per case, ns per scan (lower is better)\n")
    for cc, ver in found:
        print(f"  {cc:10s} {ver}")
    print()

    with tempfile.TemporaryDirectory() as tmp:
        results = [(cc, measure(cc, tmp, includes, samples)) for cc, _ in found]
    results = [(cc, r) for cc, r in results if r]
    if len(results) < 2:
        print("bench-compilers: fewer than two compilers produced a reading")
        return 0

    base_cc, base = results[0]
    others = results[1:]
    width = max(len(n) for n in base)
    head = f"  {'case':{width}}  {base_cc:>12}"
    for cc, _ in others:
        head += f"  {cc:>12}  {'ratio':>7}"
    print(head)
    print("  " + "-" * (len(head) - 2))

    worst = (1.0, "")
    for name in base:
        line = f"  {name:{width}}  {base[name]:12.1f}"
        for cc, other in others:
            if name not in other:
                line += f"  {'-':>12}  {'-':>7}"
                continue
            ratio = other[name] / base[name] if base[name] else 0.0
            line += f"  {other[name]:12.1f}  {ratio:6.2f}x"
            if ratio > worst[0]:
                worst = (ratio, f"{name} ({cc} / {base_cc})")
        print(line)

    print(f"\n  worst ratio: {worst[0]:.2f}x on {worst[1]}")
    print("  A ratio far from 1.0 is a report, not a verdict: compilers differ. It matters when an")
    print("  optimisation's own comment claims a saving -- that claim has to hold on both legs.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
