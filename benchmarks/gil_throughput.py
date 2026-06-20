#!/usr/bin/env python3
"""Single-shot match throughput under threads.

GIL Tranche 1 releases the GIL around the VM scan in match/fullmatch/search, so
threads can match in parallel; it also moves that path off the Pattern's shared
scratch onto local scratch. This bench shows (a) mono-thread throughput — to expose
the local-scratch cost vs the old shared scratch — and (b) multi-thread scaling on
distinct Patterns + subjects. Run it on the before/after builds and compare.

Run manually:  PYTHONPATH=python python3 benchmarks/gil_throughput.py
"""
import threading
import time

import real

PATTERN = r"\w+@\w+\.\w+"


def _work(pattern, subject, iters):
    search = pattern.search
    for _ in range(iters):
        search(subject)


def throughput(nthreads, subject, total_iters):
    pats = [real.compile(PATTERN) for _ in range(nthreads)]  # distinct Pattern per thread
    per = total_iters // nthreads
    threads = [threading.Thread(target=_work, args=(pats[i], subject, per))
               for i in range(nthreads)]
    start = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.perf_counter() - start
    return (per * nthreads) / elapsed  # searches / second


def main():
    cases = [
        ("tiny   (16 B)", "a@b.co word x", 400_000),
        ("medium ( 1 KB)", ("word " * 200) + "a@b.co", 60_000),
        ("large  (64 KB)", ("word " * 13_000) + "a@b.co", 3_000),
    ]
    print(f"search throughput (searches/sec), pattern {PATTERN!r}:")
    for label, subject, total in cases:
        base = throughput(1, subject, total)
        line = f"  {label}: 1T={base:11,.0f}"
        for nt in (2, 4, 8):
            rate = throughput(nt, subject, total)
            line += f" | {nt}T={rate:11,.0f} ({rate / base:4.2f}x)"
        print(line)


if __name__ == "__main__":
    main()
