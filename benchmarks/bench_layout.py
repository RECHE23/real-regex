#!/usr/bin/env python3
"""Layout-robust A/B for the engine benchmark.

WHY THIS EXISTS
---------------
`real` is header-only: every route body is inlined into the consumer's translation unit, and its
address, alignment and I-cache colour are then decided by code this project does not write and will
never see. A single build is therefore ONE SAMPLE from a distribution of layouts -- not "the"
performance of the change under test.

That is not a theoretical worry here, it is a measured one. Deleting 21 lines of compile-time-only
code -- a helper reached once per `regex` construction and never on any scan path -- moved
`digits [0-9]+` by **+16.7 %** on x86-64 in a single-build A/B. No mechanism can make that real. The
instrument was reporting layout, and the project had been reading layout as engine work.

It also falsified the rule this repository had been using to tell the two apart ("rows that move in
the same direction on both ISAs are real, rows that disagree are placement"): that same impossible
+16.7 % moved in the SAME direction on both ISAs.

WHAT THIS DOES INSTEAD
----------------------
Compile the same source K times into K different layouts, run each, and compare DISTRIBUTIONS. The
draws are paired: draw i on side A and draw i on side B are built with the same perturbation, so the
comparison is a paired one, which is what gives usable power at small K.

The perturbation is semantics-free -- function alignment, plus a count of dead `used` functions that
shift everything emitted after them. Neither can change what the program computes.

Honest limits, stated here rather than discovered later:

* Identical perturbation parameters do NOT produce identical layouts on the two sides: side B has
  different code, so its addresses differ regardless. Pairing controls the PERTURBATION, not the
  layout. It reduces variance; it does not eliminate the confound.
* K draws sample the layout space very sparsely. This tool answers "is the observed shift larger than
  this row's own layout noise", not "what is this row's true expected time".
* The noise floor is per row and per machine. Run `--null` on the machine you intend to judge on.
  A floor measured on arm64 does not license a verdict on x86-64.

DECISION RULE
-------------
A change is reported as REAL for a row only when BOTH hold:

  1. its median paired delta exceeds that row's NOISE FLOOR, measured by `--null` -- the same source
     against itself, whose true delta is exactly zero by construction, so whatever the null reports is
     the instrument talking; and
  2. every draw agrees in sign, i.e. the observed spread does not straddle zero.

Condition 2 is there because condition 1 alone is not enough at this K: the null gives some rows an
implausibly TIGHT floor by luck (`hex [0-9a-f]{8}` came back at +/-0.1 %), and the first judged
comparison duly flagged that row as REAL on a +0.2 % median whose spread was [-0.8, +1.8]. A spread
straddling zero is not evidence of a shift whatever its median does. The null licenses this second
condition rather than taste: on a true-zero change, not one of the twenty rows produced a
single-signed spread.

Direction agreement across draws is printed, and is deliberately NOT a criterion. The first version
of this tool used "agrees on K-1 of K draws" and the null falsified it immediately: on a change with
a true delta of zero, `(?i)café` agreed 8 draws out of 8 at +3.4 %. Consistency of sign measures how
systematic a layout artefact is, not whether it is one. The same reasoning retires the rule this
repository used before this tool existed -- "rows moving the same way on both ISAs are real work,
rows disagreeing are placement" -- which the null shows to be an artefact detector with no power.

Anything that does not clear its floor is reported as INDISTINGUISHABLE -- not as "no change", which
the data does not support either.

Reference: Curtsinger & Berger, "STABILIZER: Statistically Sound Performance Evaluation"
(ASPLOS 2013), for the randomise-then-use-statistics argument; Mytkowicz, Diwan, Hauswirth &
Sweeney, "Producing Wrong Data Without Doing Anything Obviously Wrong!" (ASPLOS 2009), for why the
single-build A/B misleads in the first place.
"""

from __future__ import annotations

import argparse
import json
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

# The layout draws, in a FIXED order so run i is always the same perturbation. Deliberately not
# random: a reproducible sweep can be re-run against a later tree and compared draw by draw, and a
# seeded RNG would only add a knob nobody would keep constant.
DRAWS: list[tuple[int, int]] = [
    (16, 0),    # (function alignment in bytes, number of dead pad bodies)
    (16, 7),
    (32, 0),
    (32, 13),
    (64, 3),
    (8, 0),
    (8, 21),
    (64, 31),
]


# The null floor's estimator. NOT a maximum, and the reason is a design fault this tool shipped with
# for one afternoon: the widest excursion GROWS WITH THE SAMPLE SIZE, so a max-based floor is not a
# consistent estimator and cannot be stabilised by taking more data. Measured: at 8 readings
# `digits [0-9]+` calibrated at 1.0 %, at 24 readings the same configuration gave 6.0 %, and the tool
# was at that moment advising the reader to "raise --reps until the ratio settles" -- advice that
# could never be satisfied. A high quantile converges to a fixed value instead, which is what a
# threshold has to do to mean anything.
FLOOR_QUANTILE = 0.95


def floor_of(deltas: list[float]) -> float:
    """The noise floor for one row: a high quantile of |delta|, linearly interpolated."""
    mags = sorted(abs(d) for d in deltas)
    if not mags:
        return 0.0
    if len(mags) == 1:
        return mags[0]
    pos = FLOOR_QUANTILE * (len(mags) - 1)
    lo = int(pos)
    hi = min(lo + 1, len(mags) - 1)
    return mags[lo] + (mags[hi] - mags[lo]) * (pos - lo)


def build(tree: Path, out: Path, align: int, pad: int, sciforge: Path, extra_env: dict[str, str],
          real_only: bool = False, source: str = "bench_engines.cpp") -> None:
    """Compiles one layout draw of the engine benchmark.

    `real_only` drops the optional competitor engines, which is a METHODOLOGY experiment rather than a
    convenience: MEASUREMENT.md §6 suspected that linking four engines into one executable widens
    REAL's noise floor, because REAL's code then sits beside whatever the competitors brought. Running
    `--null --real-only` and comparing the floors answers that. (A first check tempers the
    expectation: dropping PCRE2 and RE2 removes only 4.3 % of the binary -- `std::regex` is
    header-only and compiled either way, and the other two are dynamically linked.)
    """
    cflags = ["-std=c++20", "-O2", f"-falign-functions={align}", f"-DBENCH_LAYOUT_PAD={pad}",
              '-DBENCH_FLAGS="-O2"', '-DBENCH_COMMIT="layout-sweep"',
              "-I", str(tree / "include"), "-I", str(sciforge)]
    libs: list[str] = []
    for pkg, macro in (() if real_only else (("libpcre2-8", "HAVE_PCRE2"), ("re2", "HAVE_RE2"))):
        probe = subprocess.run(["pkg-config", "--exists", pkg], check=False,
                               capture_output=True, env={**extra_env})
        if probe.returncode == 0:
            cflags.append(f"-D{macro}")
            cf = subprocess.run(["pkg-config", "--cflags", pkg], capture_output=True, text=True,
                                check=True, env={**extra_env})
            cflags.extend(cf.stdout.split())
            lf = subprocess.run(["pkg-config", "--libs", pkg], capture_output=True, text=True,
                                check=True, env={**extra_env})
            libs.extend(lf.stdout.split())
    # Libraries go AFTER the translation unit, which is required rather than conventional: GNU ld
    # resolves left to right, so `-lpcre2-8 source.cpp` links cleanly on macOS and fails on Linux with
    # "undefined reference" for every symbol the source needs. Found the hard way on the x86 devbox.
    cmd = ["c++", *cflags, str(tree / "benchmarks" / source), "-o", str(out), *libs]
    subprocess.run(cmd, check=True, env={**extra_env})


def measure(binary: Path, env: dict[str, str]) -> dict[str, float]:
    """Runs one draw and returns {case name: REAL ns/B}.

    ns/B is derived exactly as `bench_engines.py` derives the published tables -- median of the raw
    samples over the corpus size -- so a number here is comparable with a number there, and this
    tool cannot drift away from the reference by computing its own statistic.
    """
    proc = subprocess.run([str(binary)], capture_output=True, text=True, check=True, env=env)
    doc = json.loads(proc.stdout)
    out: dict[str, float] = {}
    for key in ("cases", "unicode_cases"):
        for case in doc.get(key, []):
            real = case.get("engines", {}).get("real")
            if isinstance(real, dict) and real.get("samples"):
                out[case["name"]] = statistics.median(real["samples"]) / float(case["corpus_bytes"])
    return out


def sweep_pair(tree_a: Path, tree_b: Path, draws: list[tuple[int, int]], sciforge: Path,
               env: dict[str, str], reps: int, real_only: bool = False,
               source: str = "bench_engines.cpp") -> tuple[list[dict[str, float]], list[dict[str, float]]]:
    """Builds every draw of both sides, then runs them INTERLEAVED.

    Interleaving is not a nicety. The first version of this tool ran side A's whole sweep and then
    side B's, and its null calibration -- the same source against itself, true delta exactly zero --
    came back with a systematic POSITIVE bias: most rows' medians sat at +1 to +4 %, because the
    second sweep ran on a warmer machine. Alternating A, B, A, B within each draw puts that drift on
    both sides of every comparison instead of all of it on one.

    Each (draw, rep) yields one paired reading. Binaries are built once and reused across reps.
    """
    with tempfile.TemporaryDirectory(prefix="layout-") as tmp:
        bins: list[tuple[Path, Path]] = []
        for i, (align, pad) in enumerate(draws):
            a = Path(tmp) / f"A_{i}"
            b = Path(tmp) / f"B_{i}"
            print(f"  build draw {i + 1}/{len(draws)}  align={align} pad={pad}", file=sys.stderr)
            build(tree_a, a, align, pad, sciforge, env, real_only, source)
            build(tree_b, b, align, pad, sciforge, env, real_only, source)
            bins.append((a, b))
        ra: list[dict[str, float]] = []
        rb: list[dict[str, float]] = []
        for rep in range(reps):
            for i, (a, b) in enumerate(bins):
                print(f"  run rep {rep + 1}/{reps} draw {i + 1}/{len(bins)}", file=sys.stderr)
                # A then B then B then A: the two orders cancel any residual within-pair drift.
                a1 = measure(a, env)
                b1 = measure(b, env)
                b2 = measure(b, env)
                a2 = measure(a, env)
                ra.append({k: min(a1[k], a2[k]) for k in a1.keys() & a2.keys()})
                rb.append({k: min(b1[k], b2[k]) for k in b1.keys() & b2.keys()})
    return ra, rb


def paired_deltas(a: list[dict[str, float]], b: list[dict[str, float]]) -> dict[str, list[float]]:
    """Per case, the per-draw percentage delta of b against a."""
    rows = set(a[0])
    for r in a[1:] + b:
        rows &= set(r)
    return {name: [(bi[name] - ai[name]) / ai[name] * 100.0 for ai, bi in zip(a, b)]
            for name in sorted(rows)}


def report(deltas: dict[str, list[float]], floors: dict[str, float] | None, k: int) -> int:
    """Prints the verdict table. Returns the number of rows judged REAL."""
    real = 0
    width = max((len(n) for n in deltas), default = 10)
    head = f"{'case':{width}} {'median':>8} {'spread':>15} {'agree':>6}"
    print(head + ("  floor    verdict" if floors else "     (null: this IS the floor)"))
    print("-" * (len(head) + (20 if floors else 30)))
    for name, ds in sorted(deltas.items(), key=lambda kv: -abs(statistics.median(kv[1]))):
        med = statistics.median(ds)
        agree = max(sum(1 for d in ds if d > 0), sum(1 for d in ds if d < 0))
        line = f"{name:{width}} {med:+7.1f}% [{min(ds):+6.1f},{max(ds):+6.1f}] {agree:>3}/{k}"
        if floors is None:
            print(line)
            continue
        floor = floors.get(name, 0.0)
        straddles = min(ds) <= 0.0 <= max(ds)
        verdict = "REAL" if abs(med) > floor and not straddles else "indistinguishable"
        real += verdict == "REAL"
        print(f"{line}  {floor:5.1f}%    {verdict}")
    return real


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, type=Path, help="tree for side A")
    ap.add_argument("--cand", type=Path, help="tree for side B; omit with --null")
    ap.add_argument("--null", action="store_true",
                    help="calibration: run --base against ITSELF to measure each row's noise floor")
    ap.add_argument("--sciforge", type=Path,
                    default=Path.home() / "Projects" / "sciforge" / "include")
    ap.add_argument("--draws", type=int, default=len(DRAWS),
                    help=f"how many of the {len(DRAWS)} fixed draws to use")
    ap.add_argument("--floors", type=Path,
                    help="JSON written by a previous --null run, used to judge this comparison")
    ap.add_argument("--save-floors", type=Path, help="write the measured floors here (--null only)")
    ap.add_argument("--save-deltas", type=Path,
                    help="write the raw per-draw deltas here, so a rule change can be re-applied "
                         "to an existing sweep instead of paying for another one")
    ap.add_argument("--source", default="bench_engines.cpp",
                    help="translation unit to measure. bench_minimal.cpp includes ONLY real.hpp, which "
                         "is what a consumer's unit looks like; the default includes <regex>, PCRE2 and "
                         "RE2 and is the instrument the published tables use. The two answer different "
                         "questions -- see bench_minimal.cpp's own header.")
    ap.add_argument("--real-only", action="store_true",
                    help="build without PCRE2/RE2 -- see build() for the methodology question this "
                         "answers")
    ap.add_argument("--reps", type=int, default=1,
                    help="timed repetitions of the interleaved run over every draw")
    args = ap.parse_args()

    if not args.null and args.cand is None:
        ap.error("--cand is required unless --null is given")
    if shutil.which("c++") is None:
        print("no c++ on PATH", file=sys.stderr)
        return 2

    draws = DRAWS[: args.draws]
    env = {"PATH": "/usr/bin:/bin:/usr/local/bin:/opt/homebrew/bin"}
    import os
    for passthrough in ("PKG_CONFIG_PATH", "LD_LIBRARY_PATH", "HOME"):
        if passthrough in os.environ:
            env[passthrough] = os.environ[passthrough]

    # In --null mode side B IS side A: the true delta is exactly zero by construction, so whatever
    # comes out is the instrument's own noise -- measured per row, because rows do not share a floor
    # (one whose matches are dense pays layout differently from one that is call-overhead bound).
    other = args.base if args.null else args.cand
    print(f"side A: {args.base}\nside B: {other}" + ("  (null calibration)" if args.null else ""),
          file=sys.stderr)
    a, b = sweep_pair(args.base, other, draws, args.sciforge, env, args.reps, args.real_only,
                      args.source)
    k = len(a)

    if args.null:
        deltas = paired_deltas(a, b)
        report(deltas, None, k)
        floors = {n: floor_of(ds) for n, ds in deltas.items()}
        print(f"\nnoise floor per row = the {FLOOR_QUANTILE:.0%} quantile of |delta| over this sweep")
        print("(a quantile, NOT the widest excursion -- see floor_of for why a max cannot work).")
        print("A candidate must beat its row's floor to be reported as REAL.")
        print(f"\n{'case':44} {'floor':>7} {'max':>7}")
        for n, ds in sorted(floors.items(), key=lambda kv: -kv[1]):
            print(f"{n[:44]:44} {ds:6.1f}% {max(abs(x) for x in deltas[n]):6.1f}%")
        # SPLIT-HALF CHECK: the floor is a MAXIMUM over readings, which is a high-variance statistic.
        # Comparing the two halves of this sweep says how much to trust it. This exists because three
        # cross-configuration comparisons (arm64 vs x86-64, four engines vs REAL-only) each showed
        # floors moving in BOTH directions, which is what an unstable estimator looks like -- and the
        # tempting conclusion "configuration X is noisier" was not supported by it.
        if k >= 4:
            half = k // 2
            worst_ratio = 0.0
            worst_row = ""
            for name, ds in deltas.items():
                lo = floor_of(ds[:half])
                hi = floor_of(ds[half:])
                if min(lo, hi) > 0.0 and max(lo, hi) / min(lo, hi) > worst_ratio:
                    worst_ratio, worst_row = max(lo, hi) / min(lo, hi), name
            print(f"\nsplit-half stability: the two halves of this sweep disagree by up to "
                  f"{worst_ratio:.1f}x ({worst_row}).")
            if worst_ratio > 2.0:
                print("  ^ that is a LOT. The floors remain usable as thresholds (they are conservative")
                print("    by construction), but do NOT compare them against another configuration's")
                print("    floors to conclude which is noisier: three such comparisons here -- arm64 vs")
                print("    x86-64, four engines vs REAL-only -- each moved floors in BOTH directions,")
                print("    which is what an unstable estimate looks like, not a property of the build.")
        if args.save_deltas:
            args.save_deltas.write_text(json.dumps(deltas, indent=2, sort_keys=True))
            print(f"raw per-draw deltas written to {args.save_deltas}")
        if args.save_floors:
            args.save_floors.write_text(json.dumps(floors, indent=2, sort_keys=True))
            print(f"floors written to {args.save_floors}")
        return 0

    deltas = paired_deltas(a, b)
    floors = json.loads(args.floors.read_text()) if args.floors else {}
    if not floors:
        print("WARNING: no --floors given, so every verdict below is unjudged. Run --null first.\n")
    n_real = report(deltas, floors, k)
    print(f"\n{n_real} row(s) judged REAL; the rest are indistinguishable from layout noise.")
    if args.save_deltas:
        args.save_deltas.write_text(json.dumps(deltas, indent=2, sort_keys=True))
        print(f"raw per-draw deltas written to {args.save_deltas} (re-judgeable without re-running)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
