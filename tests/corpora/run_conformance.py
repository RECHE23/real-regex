#!/usr/bin/env python3
"""Run the vendored public conformance corpora against REAL, classified by the SciForge corpus contract.

Wires REAL (the engine under test) and Python ``re`` (the oracle) into ``sciforge.corpus.run_corpus``,
one manifest per vendored file, and prints the per-status tally the scorecard consumes. The empty-
iteration group-capture divergence is recognised and classed ``intentional_divergence`` (its link);
everything else that disagrees with ``re`` is a ``bug`` and is listed for triage.

Usage: python3 tests/corpora/run_conformance.py [--json out.json]
"""

import argparse
import hashlib
import json
import pathlib
import sys

import re

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "python"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "sciforge" / "python"))

import real
from sciforge.corpus import BUG, INTENTIONAL_DIVERGENCE, OUT_OF_CONTRACT, Manifest, is_empty_iteration_capture
from sciforge.corpus.runner import load_cases_dat, load_cases_rust_toml, re_like_engine, run_corpus

HERE = pathlib.Path(__file__).resolve().parent
_DIVERGENCE = lambda r, o, c: "div_empty_iteration_capture" if is_empty_iteration_capture(r, o) else None

# rust-regex: MIT OR Apache-2.0, leftmost-first (== re). Every file is a find-all (finditer) corpus.
_RUST_LICENSE = "MIT OR Apache-2.0"
_RUST = ["anchored", "flags", "multiline", "unicode", "utf8", "word-boundary", "word-boundary-special",
         "iter", "misc", "substring", "regression", "no-unicode", "bytes", "crlf"]
# Fowler / AT&T tests via the Go testdata: POSIX leftmost-longest origin (so leftmost-first disagreements
# are out_of_contract, not bugs), BSD-licensed.
_FOWLER = ["basic", "nullsubexpr", "repetition"]


def _sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _rust_manifest(name, path):
    return Manifest(origin="https://github.com/rust-lang/regex/blob/master/testdata/{}.toml".format(name),
                    sha256=_sha256(path), license=_RUST_LICENSE, attribution="rust-lang/regex",
                    retrieved="2026-07-03", semantics="leftmost-first", type="text", api="finditer",
                    oracle="python_re", notes="find-all corpus").validate()


def _fowler_manifest(name, path):
    return Manifest(origin="https://github.com/golang/go/blob/master/src/regexp/testdata/{}.dat".format(name),
                    sha256=_sha256(path), license="BSD-3-Clause",
                    attribution="AT&T Labs (Glenn Fowler) via the Go regexp testdata",
                    retrieved="2026-07-03", semantics="posix", type="text", api="search",
                    oracle="python_re", notes="AT&T POSIX tests; leftmost-longest origin").validate()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", type=pathlib.Path)
    args = ap.parse_args()
    engine, oracle = re_like_engine(real), re_like_engine(re)
    report = {"corpora": {}, "totals": dict.fromkeys(
        ["pass", "bug", "intentional_divergence", "out_of_contract", "excluded_by_design", "filtered"], 0)}
    bugs = []

    def record(name, manifest, cases, filtered):
        rep = run_corpus(cases, manifest, engine=engine, oracle=oracle, divergence_of=_DIVERGENCE)
        counts = rep.counts()
        counts["filtered"] = filtered
        report["corpora"][name] = {"sha256": manifest.sha256[:12], "counts": dict(counts)}
        for key in report["totals"]:
            report["totals"][key] += counts.get(key, 0)
        for result in rep.results:
            if result.status == BUG:
                bugs.append((name, result.pattern, result.real_result, result.oracle_result))
        print("  {:26} pass={:<5} bug={:<4} int_div={:<4} out_of_contract={:<4} filtered={}".format(
            name, counts["pass"], counts["bug"], counts["intentional_divergence"],
            counts["out_of_contract"], filtered))

    print("=== rust-regex ===")
    for name in _RUST:
        path = HERE / "rust" / (name + ".toml")
        if not path.exists():
            continue
        cases, filtered = load_cases_rust_toml(path)
        record("rust/" + name, _rust_manifest(name, path), cases, filtered)
    print("=== fowler (Go testdata) ===")
    for name in _FOWLER:
        path = HERE / "fowler" / (name + ".dat")
        if not path.exists():
            continue
        manifest = _fowler_manifest(name, path)
        cases = load_cases_dat(path, manifest)
        record("fowler/" + name, manifest, cases, 0)

    print("\n=== TOTALS ===", report["totals"])
    print("=== BUG BUCKET ({}) — every one goes to triage before any fix ===".format(len(bugs)))
    for name, pattern, rr, oo in bugs[:40]:
        print("  [{}] {!r}\n      real={} re={}".format(name, pattern, rr, oo))
    if args.json:
        report["bugs"] = [{"corpus": n, "pattern": p, "real": r, "re": o} for n, p, r, o in bugs]
        args.json.write_text(json.dumps(report, indent=2))
        print("wrote", args.json)


if __name__ == "__main__":
    main()
