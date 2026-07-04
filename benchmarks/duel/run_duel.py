#!/usr/bin/env python3
"""REAL-vs-rust duel: the same patterns over the same corpora through both engines, ns/byte and match-count
cross-checked, non-cherry-picked. Emits a Markdown table (paste into BENCHMARKS.md). The date row is a
deliberate no-match scan (no yyyy-mm-dd in its corpus): a prefilter gap, not captures."""
import subprocess
import sys
import tempfile

HERE = "/Users/rchenard/Projects/real-regex/benchmarks/duel"
REAL = f"{HERE}/real_bench"
RUST = f"{HERE}/rust_bench/target/release/rust_bench"
N = 20000  # corpus repetitions

WORDS = "the quick brown fox jumps over the lazy dog " * N
DIGITS = "42 1000 7 88 305 12 9999 6 ".replace(" ", "  ") * N
CSV = "alpha,bravo,charlie,delta,echo,foxtrot," * N
EMAILS = "contact john.doe@example.com or jane@corp.io today " * N
NODATE = "no dates here just plain english words running along " * N  # date pattern finds NOTHING
MIXED = "id_42 name_foo val_7 key_bar ok_9 " * N

CASES = [
    # (label, pattern, text) — chosen to span REAL's fast-path strengths and rust's DFA/prefilter strengths
    ("literal `dog`",           r"dog",                          WORDS),
    ("alternation `fox|dog|cat`", r"fox|dog|cat",                WORDS),
    ("class `[a-z]+`",          r"[a-z]+",                       WORDS),
    ("digits `[0-9]+`",         r"[0-9]+",                       DIGITS),
    ("fields `[^,]+`",          r"[^,]+",                        CSV),
    (r"word-bound `\b\w+\b`",   r"\b\w+\b",                      WORDS),
    (r"email `(\w+)@(\w+)`",    r"(\w+)@(\w+)",                  EMAILS),
    (r"ident `(\w+)_(\w+)`",    r"(\w+)_(\w+)",                  MIXED),
    (r"date no-match `\d{4}-\d{2}-\d{2}`", r"\d{4}-\d{2}-\d{2}", NODATE),
]


def run(binary, pattern, text):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(text)
        path = f.name
    out = subprocess.run([binary, pattern, path], capture_output=True, text=True).stdout.split()
    return float(out[0]) if out[0] != "unsupported" else None, int(out[1])


def main():
    print(f"{'case':40s} {'REAL ns/B':>10s} {'rust ns/B':>10s} {'winner':>16s}  match✓")
    rows = []
    for label, pat, text in CASES:
        rr, rc = run(REAL, pat, text)
        ur, uc = run(RUST, pat, text)
        agree = (rc == uc)
        if rr is None or ur is None:
            verdict = "REAL-only" if ur is None else "rust-only"
        elif rr < ur:
            verdict = f"REAL {ur/rr:.1f}x"
        else:
            verdict = f"rust {rr/ur:.1f}x"
        print(f"{label:40s} {rr:10.3f} {ur:10.3f} {verdict:>16s}  {'yes' if agree else 'NO('+str(rc)+'/'+str(uc)+')'}")
        rows.append((label, rr, ur, verdict, agree, rc))
    return rows


if __name__ == "__main__":
    main()
