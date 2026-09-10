#!/usr/bin/env python3
"""Run the vendored public conformance corpora against REAL, classified by the SciForge corpus contract.

Wires REAL (the engine under test) and Python ``re`` (the oracle) into ``sciforge.corpus.run_corpus``,
one manifest per vendored file, and prints the per-status tally the scorecard consumes. Documented
disagreements with ``re`` (the empty-iteration capture, the ``\\p{…}`` superset, RE2 ``(?U)``) are
classed ``intentional_divergence``; everything else that disagrees is a ``bug`` and is listed for
triage. The numbers in ``docs/TESTS.md`` are compared to this run: a published count that this
file does not reproduce is a red, the same fault ``check_tolerated_count`` exists for on other pages.

Usage: python3 tests/corpora/run_conformance.py [--json out.json]
       python3 tests/corpora/run_conformance.py --self-test
"""

import argparse
import copy
import hashlib
import json
import pathlib
import sys

import re

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "bindings" / "python"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "sciforge" / "python"))

import real
from sciforge.corpus import BUG, INTENTIONAL_DIVERGENCE, OUT_OF_CONTRACT, Manifest, is_empty_iteration_capture
from sciforge.corpus.runner import load_cases_dat, load_cases_rust_toml, re_like_engine, run_corpus

HERE = pathlib.Path(__file__).resolve().parent
TESTS_MD = HERE.parents[1] / "docs" / "TESTS.md"

#: re rejects these outright; REAL accepts them (documented supersets). A real result against an
#: oracle compile-error is that class, not a bug. The first version of this file only recognised
#: the empty-iteration capture, so rust/unicode's fifty ``\p{…}`` rows and rust/flags's ``(?U)``
#: sat in the bug bucket on a living page that said 0 bugs.
_PROPERTY_CLASS = re.compile(r"\\[pP](?:\{[^}]*\}|[A-Za-z])")
_UNGREEDY_FLAG = re.compile(r"\(\?[imsxaU-]*U")


def divergence_of(real_result, oracle_result, case):
    """The divergences.dox link for a documented disagreement, or None (→ bug)."""
    if is_empty_iteration_capture(real_result, oracle_result):
        return "div_empty_iteration_capture"
    if oracle_result != "error" or real_result in (None, "error"):
        return None
    if _PROPERTY_CLASS.search(case.pattern):
        return "div_property"
    if _UNGREEDY_FLAG.search(case.pattern):
        return "div_inline_flags"
    return None


# rust-regex: MIT OR Apache-2.0, leftmost-first (== re). Every file is a find-all (finditer) corpus.
# `empty` is the corpus that surfaced the nullable-loop leftmost-first bug; omitting it left the
# published 19/19 row and the "this suite is the regression guard" sentence with no execution.
_RUST_LICENSE = "MIT OR Apache-2.0"
_RUST = ["anchored", "flags", "multiline", "unicode", "utf8", "word-boundary", "word-boundary-special",
         "iter", "misc", "substring", "regression", "no-unicode", "bytes", "crlf", "empty"]
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


HEADLINE_RE = re.compile(
    r"\*\*Result — (\d+) / (\d+) in-contract cases pass, (\d+) documented divergences, (\d+) bugs\.\*\*")
ROW_RE = re.compile(
    r"^\| (rust/[\w-]+|fowler/\w+) \| ([^|]+) \| ([^|]+) \| ([^|]+) \| ([^|]+) \|", re.M)
API_OUT_RE = re.compile(
    r"^\| rust/anchored · substring · bytes \| — \| — \| — \| (\d+) \(all API-out\) \|", re.M)
_API_OUT = ("rust/anchored", "rust/substring", "rust/bytes")


def _cell_int(cell):
    """A table cell: em-dash is 0, `7 / 9` is the pass (left of the slash), else an int."""
    cell = cell.strip().replace("—", "-")
    if cell in ("-", ""):
        return 0
    if "/" in cell:
        return int(cell.split("/", 1)[0].strip())
    return int(cell)


def report_from_page(text):
    """A run-shaped report that agrees with TESTS.md.

    Drifting one field of this, or of the page, is how each scorecard arm is driven alone. A report
    of `{pass: 1, corpora: {}}` trips every arm at once, so blinding one still leaves the assertion
    green — the same co-trigger that hid a keep-list/find pair behind a bare `apt-get update`.
    """
    corpora = {}
    totals = dict.fromkeys(
        ["pass", "bug", "intentional_divergence", "out_of_contract", "excluded_by_design", "filtered"], 0)
    for row in ROW_RE.finditer(text):
        name, pass_cell, div_cell, ooc_cell, filt_cell = row.groups()
        pcell = pass_cell.strip()
        if "/" in pcell:
            left, right = pcell.split("/", 1)
            pass_n, scored = int(left.strip()), int(right.strip())
        else:
            pass_n, scored = 0, 0
        div_n = _cell_int(div_cell)
        ooc_n = _cell_int(ooc_cell)
        filt_n = _cell_int(filt_cell)
        counts = {
            "pass": pass_n,
            "bug": scored - pass_n - div_n,
            "intentional_divergence": div_n,
            "out_of_contract": ooc_n,
            "excluded_by_design": 0,
            "filtered": filt_n,
        }
        corpora[name] = {"counts": counts}
        for key, value in counts.items():
            totals[key] += value
    api = API_OUT_RE.search(text)
    api_n = int(api.group(1)) if api else 0
    for index, name in enumerate(_API_OUT):
        filt = api_n if index == 0 else 0
        corpora[name] = {"counts": {
            "pass": 0, "bug": 0, "intentional_divergence": 0, "out_of_contract": 0,
            "excluded_by_design": 0, "filtered": filt,
        }}
        totals["filtered"] += filt
    return {"totals": totals, "corpora": corpora}


def scorecard_problems(report, *, page_text=None, rust_names=None):
    """Where docs/TESTS.md disagrees with this run. Empty means the published count is this run.

    `rust_names` is the load list (default `_RUST`) so the empty-in-list arm can be blinded without
    mutating the module constant. rust/empty has its own row check; the run→page bijection skips it
    so a missing empty row is not also counted as a bijection miss — those two would otherwise
    co-trigger, and blinding one would still leave this green. The three `is None` guards (headline,
    a listed row with no run, the API-out row) are verified by crash, not by the self-test: blinding
    one dereferences None. An AttributeError here is that missing guard, not a harness bug.
    """
    text = TESTS_MD.read_text(encoding="utf-8") if page_text is None else page_text
    names = list(_RUST if rust_names is None else rust_names)
    problems = []
    totals = report["totals"]
    in_contract = totals["pass"] + totals["bug"] + totals["intentional_divergence"]
    hit = HEADLINE_RE.search(text)
    if hit is None:
        problems.append("docs/TESTS.md: no Result headline in the expected shape")
    else:
        want = (totals["pass"], in_contract, totals["intentional_divergence"], totals["bug"])
        got = tuple(int(hit.group(i)) for i in range(1, 5))
        if got != want:
            problems.append("headline is {} — this run is {}".format(got, want))

    if "empty" not in names:
        problems.append("_RUST does not load empty — the published rust/empty row and the "
                        "regression-guard sentence have no execution")
    if "| rust/empty |" not in text:
        problems.append("docs/TESTS.md has no rust/empty row")

    listed = set()
    for row in ROW_RE.finditer(text):
        name, pass_cell, div_cell, ooc_cell, filt_cell = row.groups()
        listed.add(name)
        counts = report["corpora"].get(name)
        if counts is None:
            problems.append("{} is in TESTS.md but was not run".format(name))
            continue
        c = counts["counts"]
        scored = c["pass"] + c["bug"] + c["intentional_divergence"]
        want_pass = "{} / {}".format(c["pass"], scored) if scored else "—"
        # The pass cell of a scored corpus is `N / M`; a dash means zero scored cases.
        got_pass = pass_cell.strip()
        if scored and _cell_int(pass_cell) != c["pass"]:
            problems.append("{} pass cell {} != this run {}".format(name, got_pass, want_pass))
        if _cell_int(div_cell) != c["intentional_divergence"]:
            problems.append("{} intentional_divergence {} != {}".format(
                name, div_cell.strip(), c["intentional_divergence"]))
        if _cell_int(ooc_cell) != c["out_of_contract"]:
            problems.append("{} out_of_contract {} != {}".format(
                name, ooc_cell.strip(), c["out_of_contract"]))
        if name not in _API_OUT and _cell_int(filt_cell) != c["filtered"]:
            problems.append("{} filtered {} != {}".format(name, filt_cell.strip(), c["filtered"]))

    api_out = API_OUT_RE.search(text)
    api_sum = sum(report["corpora"].get(n, {"counts": {"filtered": 0}})["counts"]["filtered"]
                  for n in _API_OUT)
    if api_out is None:
        problems.append("docs/TESTS.md: no combined API-out row for anchored/substring/bytes")
    elif int(api_out.group(1)) != api_sum:
        problems.append("API-out filtered {} != this run {}".format(api_out.group(1), api_sum))

    for name in names:
        key = "rust/" + name
        if key in _API_OUT or key == "rust/empty":
            continue
        if key not in listed:
            problems.append("{} was run but has no TESTS.md row".format(key))
    return problems


class _Case:
    def __init__(self, pattern):
        self.pattern = pattern


def self_test():
    """Drive each documented-superset arm, then each scorecard arm, without the live corpus."""
    match = [{"span": [0, 1], "groups": []}]
    if divergence_of(match, "error", _Case(r"\p{Lu}+")) != "div_property":
        print("run_conformance: SELF-TEST FAILED — \\p{…} vs re-error did NOT map to div_property")
        return 1
    if divergence_of(match, "error", _Case("(?U)a+?")) != "div_inline_flags":
        print("run_conformance: SELF-TEST FAILED — (?U) vs re-error did NOT map to div_inline_flags")
        return 1
    if divergence_of(match, match, _Case("a")) is not None:
        print("run_conformance: SELF-TEST FAILED — an agreement mapped to a divergence")
        return 1
    if divergence_of(match, "error", _Case("abc")) is not None:
        print("run_conformance: SELF-TEST FAILED — a re-error without a documented superset "
              "mapped to a divergence (that is how a real bug would be swallowed)")
        return 1
    other = [{"span": [0, 2], "groups": []}]
    if divergence_of(match, other, _Case(r"\p{Lu}+")) is not None:
        print("run_conformance: SELF-TEST FAILED — a real disagreement on a \\p{…} pattern was "
              "excused. The superset is only a compile-error class; if the oracle answers, a "
              "span mismatch is a bug.")
        return 1
    if divergence_of(match, other, _Case("(?U)a+?")) is not None:
        print("run_conformance: SELF-TEST FAILED — a real disagreement on a (?U) pattern was "
              "excused. Same arm as \\p{…}: the documented form does not blank-cheque a mismatch "
              "once the oracle has a response.")
        return 1

    page = TESTS_MD.read_text(encoding="utf-8")
    faithful = report_from_page(page)
    base = scorecard_problems(faithful, page_text=page)
    if base:
        print("run_conformance: SELF-TEST FAILED — a report built from TESTS.md disagrees with it "
              "(the cases below prove nothing until this is green):")
        for problem in base:
            print("    {}".format(problem))
        return 1

    def must_trip(label, needle, *, report=None, page_text=None, rust_names=None):
        problems = scorecard_problems(
            faithful if report is None else report,
            page_text=page if page_text is None else page_text,
            rust_names=rust_names)
        if not problems:
            print("run_conformance: SELF-TEST FAILED — {} did NOT trip the scorecard".format(label))
            return False
        if len(problems) != 1 or needle not in problems[0]:
            print("run_conformance: SELF-TEST FAILED — {} tripped but did not name only {!r}:".format(
                label, needle))
            for problem in problems:
                print("    {}".format(problem))
            return False
        return True

    drifted_totals = copy.deepcopy(faithful)
    drifted_totals["totals"]["pass"] += 1
    if not must_trip("a drifted headline", "headline is", report=drifted_totals):
        return 1
    no_headline = HEADLINE_RE.sub("**Result — (the count lives in the table.)**", page, count=1)
    if not must_trip("a missing Result headline", "no Result headline", page_text=no_headline):
        return 1
    without_empty = [name for name in _RUST if name != "empty"]
    if not must_trip("empty dropped from _RUST", "_RUST does not load empty",
                     rust_names=without_empty):
        return 1
    no_empty_row = re.sub(r"^\| rust/empty \|.*\n", "", page, count=1, flags=re.M)
    if not must_trip("a missing rust/empty row", "has no rust/empty row", page_text=no_empty_row):
        return 1
    ghost_page = page.replace(
        "| rust/flags |", "| rust/ghost | 1 / 1 | — | — | — |\n| rust/flags |", 1)
    if not must_trip("a TESTS.md row that was not run", "is in TESTS.md but was not run",
                     page_text=ghost_page):
        return 1
    drifted_pass = copy.deepcopy(faithful)
    drifted_pass["corpora"]["rust/flags"]["counts"]["pass"] = 0
    if not must_trip("a drifted pass cell", "rust/flags pass cell", report=drifted_pass):
        return 1
    drifted_div = copy.deepcopy(faithful)
    drifted_div["corpora"]["rust/unicode"]["counts"]["intentional_divergence"] = 0
    if not must_trip("a drifted intentional_divergence cell", "rust/unicode intentional_divergence",
                     report=drifted_div):
        return 1
    drifted_ooc = copy.deepcopy(faithful)
    drifted_ooc["corpora"]["fowler/basic"]["counts"]["out_of_contract"] = 0
    if not must_trip("a drifted out_of_contract cell", "fowler/basic out_of_contract",
                     report=drifted_ooc):
        return 1
    drifted_filt = copy.deepcopy(faithful)
    drifted_filt["corpora"]["rust/multiline"]["counts"]["filtered"] = 0
    if not must_trip("a drifted filtered cell", "rust/multiline filtered", report=drifted_filt):
        return 1
    drifted_api = copy.deepcopy(faithful)
    drifted_api["corpora"]["rust/anchored"]["counts"]["filtered"] = 0
    if not must_trip("a drifted API-out filtered count", "API-out filtered", report=drifted_api):
        return 1
    no_api = re.sub(r"^\| rust/anchored · substring · bytes \|.*\n", "", page, count=1, flags=re.M)
    if not must_trip("a missing API-out row", "no combined API-out row", page_text=no_api):
        return 1
    extra = list(_RUST) + ["ghost"]
    if not must_trip("a run corpus with no TESTS.md row", "was run but has no TESTS.md row",
                     rust_names=extra):
        return 1

    print("run_conformance: self-test OK — \\p{…} and (?U) map to their sections only against a "
          "re-error; a span disagreement on those same patterns is a bug; an undocumentable "
          "re-error is a bug; and each scorecard arm (headline, empty-in-_RUST, empty row, "
          "pass / intentional_divergence / out_of_contract / filtered, API-out, bijection both "
          "ways) trips on that arm alone.")
    return 0


def main():
    if "--self-test" in sys.argv[1:]:
        return self_test()
    pre = self_test()
    if pre != 0:
        return pre
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", type=pathlib.Path)
    args = ap.parse_args()
    engine, oracle = re_like_engine(real), re_like_engine(re)
    report = {"corpora": {}, "totals": dict.fromkeys(
        ["pass", "bug", "intentional_divergence", "out_of_contract", "excluded_by_design", "filtered"], 0)}
    bugs = []

    def record(name, manifest, cases, filtered):
        rep = run_corpus(cases, manifest, engine=engine, oracle=oracle, divergence_of=divergence_of)
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
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print("wrote", args.json)

    problems = scorecard_problems(report)
    if problems:
        print("check-conformance-scorecard: FAIL — docs/TESTS.md does not match this run")
        for line in problems:
            print("    {}".format(line))
        return 1
    print("check-conformance-scorecard: OK — docs/TESTS.md matches this run")
    return 0


if __name__ == "__main__":
    sys.exit(main())
