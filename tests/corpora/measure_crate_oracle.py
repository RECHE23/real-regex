#!/usr/bin/env python3
"""Measurement, not a runner change: how would the vendored rust corpus classify if the CRATE
(its stored `matches`) were the oracle instead of Python re?

Instrument rules, after the first version of this script caught itself cheating:

  * Flags are mapped EXPLICITLY (icase -> IGNORECASE, ascii -> ASCII) and an unknown name raises.
    The runner's getattr(module, name.upper(), 0) turns "icase" into 0 on both engines -- a flag
    silently dropped on both sides is invisible to a real-vs-re comparison.
  * The stored spans are BYTE offsets (the crate is byte-oriented); real/re answer in code points
    for str subjects. Stored spans are converted byte -> code point before any comparison.

Buckets per case:
  A0. real and re both reject, the crate compiles   -> unsupported-syntax gap, not a divergence.
  A1. real == re, crate differs, empty-adjacency    -> the crate's finditer empty-match rule.
  A2. real == re, crate differs, \\b/\\B in pattern  -> word-boundary semantics.
  A3. real == re, crate differs, anything else      -> THE UNKNOWN HEAP (printed in full).
  B.  real == crate, re differs                     -> real already sides with the crate.
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "bindings" / "python"))
sys.path.insert(0, str(HERE.parents[2] / "sciforge" / "python"))

import re as stdlib_re
import real
from sciforge.corpus.runner import load_cases_rust_toml

RUST_DIR = HERE / "rust"
RUST = ["anchored", "flags", "multiline", "unicode", "utf8", "word-boundary", "word-boundary-special",
        "iter", "misc", "substring", "regression", "no-unicode", "bytes", "crlf", "empty"]

FLAG_MAP = {"icase": "IGNORECASE", "ascii": "ASCII", "multiline": "MULTILINE"}


def run(module, pattern, text, flags):
    value = 0
    for name in flags:
        attr = FLAG_MAP.get(name)
        if attr is None:
            raise ValueError("unmapped flag name: {!r}".format(name))
        value |= getattr(module, attr)
    try:
        compiled = module.compile(pattern, value)
    except (module.error, OverflowError, RecursionError, MemoryError):
        return "error"
    out = []
    for m in compiled.finditer(text):
        out.append((m.span(), tuple(m.span(i) for i in range(1, len(m.groups()) + 1))))
    return out


def byte_to_cp_index(text):
    """Map each UTF-8 byte offset to its code-point index for a str subject."""
    raw = text.encode("utf-8")
    table = []
    cp = 0
    for byte_offset in range(len(raw) + 1):
        table.append(cp)
        if byte_offset < len(raw) and (raw[byte_offset] & 0xC0) != 0x80:
            cp += 1
    return table


def norm_stored(expected, table, case=None):
    """Stored crate spans (bytes, -1 for unset) -> code-point spans, tuples for comparison."""
    out = []
    for m in expected:
        span = tuple(m["span"])
        groups = tuple(tuple(g) for g in m["groups"])
        try:
            if span != (-1, -1):
                span = (table[span[0]], table[span[1]])
            groups = tuple((-1, -1) if len(g) == 0 or g == (-1, -1) else (table[g[0]], table[g[1]])
                           for g in groups)
        except IndexError:
            print("  !! stored span out of range: pat={!r} input!={!r} span={} groups={}".format(
                case.pattern if case else "?", case.input if case else "?", span, groups))
            raise
        out.append((span, groups))
    return out


def norm_run(result):
    if result == "error":
        return result
    return [(tuple(m[0]), tuple(tuple(g) for g in m[1])) for m in result]


buckets = {"A0": [], "A1": [], "A2": [], "A3": [], "B": []}
per_corpus = {}
for name in RUST:
    path = RUST_DIR / (name + ".toml")
    if not path.exists():
        continue
    cases, _filtered = load_cases_rust_toml(path)
    for case in cases:
        real_r = norm_run(run(real, case.pattern, case.input, case.flags))
        re_r = norm_run(run(stdlib_re, case.pattern, case.input, case.flags))
        stored = norm_stored(case.expected, byte_to_cp_index(case.input), case)
        if real_r == "error" and re_r == "error":
            if stored:
                buckets["A0"].append((name, case, stored))
            continue
        if real_r == re_r and real_r != stored:
            # A1: an empty span occurs on either side -- the crate's finditer suppresses an empty
            # match adjacent to a non-empty one, an ITERATOR rule re does not have. (Span units
            # are already normalised above, so what remains here is match-set semantics.)
            if any(s[0] == s[1] for s, _g in real_r) or any(s[0] == s[1] for s, _g in stored):
                key = "A1"
            elif "\\b" in case.pattern or "\\B" in case.pattern:
                key = "A2"
            else:
                key = "A3"
            buckets[key].append((name, case, real_r, stored))
            per_corpus.setdefault(name, [0, 0, 0, 0])
            per_corpus[name][int(key[1])] += 1
        elif real_r == stored and re_r != real_r:
            buckets["B"].append((name, case, real_r, re_r))

print("A0. both reject, crate compiles (unsupported syntax):", len(buckets["A0"]))
print("A1. empty-adjacency / iterator rule:", len(buckets["A1"]))
print("A2. word-boundary patterns:", len(buckets["A2"]))
print("A3. OTHER -- the unknown heap:", len(buckets["A3"]))
print("B.  real sides with the crate against re:", len(buckets["B"]))
print()
print("per corpus (A0/A1/A2/A3):")
for name in sorted(per_corpus):
    a0, a1, a2, a3 = per_corpus[name]
    print("  {:26s} {:3d} {:3d} {:3d} {:3d}".format("rust/" + name, a0, a1, a2, a3))
print()
print("A0 patterns (each once):")
seen = set()
for name, case, stored in buckets["A0"]:
    if case.pattern not in seen:
        seen.add(case.pattern)
        print("  rust/{:20s} {!r}".format(name, case.pattern))
print()
print("A3 heap, in full:")
for name, case, real_r, stored in buckets["A3"]:
    print("  rust/{} pat={!r} input={!r}".format(name, case.pattern, case.input))
    print("      real==re: {}".format(real_r))
    print("      crate   : {}".format(stored))
print()
print("B cases, in full:")
for name, case, real_r, re_r in buckets["B"]:
    print("  rust/{} pat={!r} input={!r}".format(name, case.pattern, case.input))
    print("      real==crate: {}".format(real_r))
    print("      re         : {}".format(re_r))
