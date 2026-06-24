#!/usr/bin/env python3
"""Match throughput under threads: single-shot search, and findall / split.

The GIL is released around the pure-C++ scan in match/fullmatch/search (single
shot) and, above a subject-size threshold, around the collect-spans phase of
findall / split — so threads scan in parallel while the Python objects are still
built under the GIL. This bench shows (a) mono-thread throughput — to expose any
per-call overhead (local scratch, the offset buffer) versus the old path — and
(b) multi-thread scaling on distinct Patterns + subjects. Run it on the
before/after builds and compare; the small (< 512 B) cases must stay flat (the
GIL is deliberately kept), the large ones should scale.

Run manually:  PYTHONPATH=python python3 benchmarks/gil_throughput.py
"""
import threading
import time

import real


def _run(work, pattern_str, nthreads, subject, total_iters):
    pats = [real.compile(pattern_str) for _ in range(nthreads)]  # distinct Pattern per thread
    per = total_iters // nthreads
    threads = [threading.Thread(target=work, args=(pats[i], subject, per))
               for i in range(nthreads)]
    start = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.perf_counter() - start
    return (per * nthreads) / elapsed  # calls / second


def _search(pattern, subject, iters):
    f = pattern.search
    for _ in range(iters):
        f(subject)


def _findall(pattern, subject, iters):
    f = pattern.findall
    for _ in range(iters):
        f(subject)


def _split(pattern, subject, iters):
    f = pattern.split
    for _ in range(iters):
        f(subject)


def _sub(pattern, subject, iters):
    f = pattern.sub
    for _ in range(iters):
        f(r"<\g<0>>", subject)  # non-callable template (\g<0> -> apply_template path)


def table(title, work, pattern_str, cases):
    print(f"{title}, pattern {pattern_str!r}:")
    for label, subject, total in cases:
        base = _run(work, pattern_str, 1, subject, total)
        line = f"  {label}: 1T={base:11,.0f}"
        for nt in (2, 4, 8):
            rate = _run(work, pattern_str, nt, subject, total)
            line += f" | {nt}T={rate:11,.0f} ({rate / base:4.2f}x)"
        print(line)


def main():
    search_cases = [
        ("tiny   (16 B)", "a@b.co word x", 400_000),
        ("medium ( 1 KB)", ("word " * 200) + "a@b.co", 60_000),
        ("large  (64 KB)", ("word " * 13_000) + "a@b.co", 3_000),
    ]
    # findall / split: many matches across the subject so the scan does real work.
    # < 4 KB stays on the small path (GIL kept → flat, by design: the build phase would
    # otherwise make frequent toggling regress); >= 4 KB takes the two-phase path. Note
    # the ceiling is ~2x for these fast-scanning patterns — the O(matches) Python-object
    # build runs under the GIL and is the serial bottleneck.
    multi_cases = [
        ("1 KB  (GIL kept) ", "word " * 200, 20_000),
        ("16 KB (two-phase)", "word " * 3_200, 1_500),
        ("64 KB (two-phase)", "word " * 13_000, 400),
    ]
    table("search throughput (searches/sec)", _search, r"\w+@\w+\.\w+", search_cases)
    print()
    table("findall throughput (calls/sec)", _findall, r"\w+", multi_cases)
    print()
    table("split throughput (calls/sec)", _split, r"\s+", multi_cases)
    print()
    # sub (non-callable template): per-match work is pure C++ and the ONLY Python object
    # is the final string (not O(matches)), so the parallelisable fraction is ~the whole
    # op — expected to scale better than findall/split's ~2x build-bound ceiling.
    table("sub throughput (calls/sec)", _sub, r"\w+", multi_cases)


if __name__ == "__main__":
    main()
