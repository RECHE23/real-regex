#!/usr/bin/env python3
"""Consumer for the multi-engine collector (bench_engines.cpp).

The C++ binary only measures and emits JSON; this script parses it and applies the shared,
dependency-free stats module (sciforge.bench) to produce the report: per-engine
median ns/byte with a ratio-vs-REAL and a bootstrap CI, ASCII box-plots of the per-scan time
distributions (REAL / std / PCRE2 / RE2 side by side per case), the scaling sweep, and the
ReDoS contrast. A "ratio > 1" means REAL is faster than that engine. Engines the pattern does
not support (RE2 on a lookaround) are shown as "unsupported", not a hole.

  python benchmarks/bench_engines.py path/to/bench_engines      # runs the binary
  python benchmarks/bench_engines.py --json engines.json        # reads a saved run
Run via: make bench-engines  (manual; not a CI gate).
"""

import argparse
import json
import os
import statistics
import subprocess
import sys

# Dep-free stats from SciForge's shared substrate (sciforge.bench); the sibling
# ../sciforge/python is on PYTHONPATH via the Makefile (make bench-engines).
from sciforge.bench import ascii_boxplot, ratio_ci  # noqa: E402

BOOTSTRAP_B = int(os.environ.get("BENCH_BOOTSTRAP", "1000"))
ENGINE_W = 27  # fixed width for an engine column (ns/byte + ratio + CI)


def load(args):
    if args.json:
        with open(args.json) as fh:
            return json.load(fh)
    proc = subprocess.run([args.binary], capture_output=True, text=True, check=True)
    return json.loads(proc.stdout)


def median_nspb(samples, corpus_bytes):
    return statistics.median(samples) / corpus_bytes


def engine_cell(entry, real_samples, corpus_bytes):
    """A fixed-width table cell for one engine: ns/byte, ratio-vs-REAL, bootstrap CI."""
    if entry == "unsupported":
        return "unsupported".rjust(ENGINE_W)
    samples = entry["samples"]
    if not samples:
        return "refused".rjust(ENGINE_W)
    nspb = median_nspb(samples, corpus_bytes)
    ratio, low, high = ratio_ci(samples, real_samples, B=BOOTSTRAP_B)
    return f"{nspb:7.2f} {ratio:5.2f}x [{low:.2f}-{high:.2f}]".rjust(ENGINE_W)


def engine_order(doc):
    return [e for e in doc["meta"]["engines"]]


def print_meta(doc):
    m = doc["meta"]
    print(f"engine throughput — REAL vs {', '.join(e for e in m['engines'] if e != 'real')}")
    print(f"cpu={m['cpu']}  compiler={m['compiler']}  flags={m['flags']}  "
          f"samples={int(m['samples'])}  bootstrap={BOOTSTRAP_B}")
    print("ns/byte (lower better); (x) = engine_time / REAL_time, >1 means REAL faster.\n")


def print_cases(doc):
    engines = engine_order(doc)
    others = [e for e in engines if e != "real"]
    header = f"{'case':<26} {'family':<14} {'REAL ns/B':>10}"
    for e in others:
        header += " " + f"{e + ' ns/B(x[95% CI])':>{ENGINE_W}}"
    header += f"   match({'/'.join(e[0] for e in engines)})"
    print(header)
    print("-" * len(header))
    families = list(dict.fromkeys(c["family"] for c in doc["cases"]))
    for family in families:
        for case in (c for c in doc["cases"] if c["family"] == family):
            eng = case["engines"]
            real_samples = eng["real"]["samples"]
            corpus_bytes = case["corpus_bytes"]
            row = f"{case['name']:<26} {case['family']:<14} {median_nspb(real_samples, corpus_bytes):10.3f}"
            for e in others:
                row += " " + engine_cell(eng[e], real_samples, corpus_bytes)
            counts = "/".join(str(eng[e]["count"]) if isinstance(eng[e], dict) else "—" for e in engines)
            row += f"   {counts}"
            print(row)


def print_case_boxplots(doc, names):
    print("\nper-scan time distributions (ns; left = faster):")
    by_name = {c["name"]: c for c in doc["cases"]}
    for name in names:
        case = by_name.get(name)
        if case is None:
            continue
        series, labels = [], []
        for e in engine_order(doc):
            entry = case["engines"][e]
            if isinstance(entry, dict) and entry["samples"]:
                series.append(entry["samples"])
                labels.append(e)
        if series:
            print(f"  {case['name']}:")
            print(ascii_boxplot(series, labels, width=42))


def print_scaling(doc):
    engines = engine_order(doc)
    print("\nscaling sweep — [a-z]+ across sizes (median ns/byte + ratio-vs-REAL):")
    header = f"{'size':>9}  {'REAL ns/B':>10}"
    for e in (e for e in engines if e != "real"):
        header += f" {e + '(x)':>14}"
    print(header)
    for point in doc["scaling"]:
        size = int(point["size"])
        eng = point["engines"]
        real_samples = eng["real"]["samples"]
        row = f"{size:>9}  {median_nspb(real_samples, size):10.3f}"
        for e in (e for e in engines if e != "real"):
            entry = eng[e]
            if isinstance(entry, dict) and entry["samples"]:
                ratio = statistics.median(entry["samples"]) / statistics.median(real_samples)
                row += f" {median_nspb(entry['samples'], size):7.3f}({ratio:4.2f}x)"
            else:
                row += f" {'—':>14}"
        print(row)


def print_redos(doc):
    print("\nReDoS — (a+)+b over 'a'*N (no match): REAL/RE2 stay linear, std backtracks.")
    for item in doc["redos"]:
        engine, n, samples = item["engine"], int(item["n"]), item["samples"]
        if samples:
            ms = statistics.median(samples) / 1e6
            print(f"  {engine:<5} N={n:<7} {ms:8.3f} ms")
        else:
            print(f"  {engine:<5} N={n:<7} refused (catastrophic backtracking)")


def main():
    parser = argparse.ArgumentParser(description="multi-engine benchmark consumer")
    parser.add_argument("binary", nargs="?", help="path to the bench_engines binary to run")
    parser.add_argument("--json", metavar="PATH", help="read a saved JSON run instead")
    args = parser.parse_args()
    if not args.binary and not args.json:
        parser.error("give a binary path or --json PATH")

    doc = load(args)
    print_meta(doc)
    print_cases(doc)
    print_case_boxplots(doc, ["words [a-z]+", "digits [0-9]+", "lookahead [a-z]+(?=[a-z])"])
    print_scaling(doc)
    print_redos(doc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
