#!/usr/bin/env python3
"""Peak-memory of lazy ``finditer`` vs materialising every match.

``Pattern.finditer`` yields one ``Match`` at a time, so iterating it holds O(1)
matches live; calling ``list(...)`` on it (or the old eager implementation) holds
O(n). This quantifies the gap with ``tracemalloc``. Note tracemalloc only traces
Python-level allocations — each Match also owns C++ span vectors (raw ``new``) that
are *not* counted here, so the real eager footprint is even larger than shown.

Run manually:  PYTHONPATH=python python3 benchmarks/finditer_memory.py
"""
import tracemalloc

import real


def peak_kib(make):
    tracemalloc.start()
    keep = make()  # noqa: F841 — held until the peak is read
    _, peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    del keep
    return peak / 1024.0


def main():
    pattern = real.compile(r"\d+")
    for n in (50_000, 200_000):
        text = "x7 " * n
        lazy = peak_kib(lambda: sum(1 for _ in pattern.finditer(text)))   # O(1) live
        eager = peak_kib(lambda: list(pattern.finditer(text)))            # O(n) live
        print(f"n={n:>7} matches | lazy peak {lazy:9.1f} KiB | "
              f"eager peak {eager:9.1f} KiB | {eager / max(lazy, 1.0):5.1f}x")


if __name__ == "__main__":
    main()
