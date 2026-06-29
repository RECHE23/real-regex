#!/usr/bin/env python3
"""REAL as a drop-in for the supported subset of Python's ``re``.

    pip install real-regex
    python python/dropin.py
"""
import real as re  # the API mirrors re for the supported subset

# search returns a Match (or None); groups by index and by name.
m = re.search(r"(?P<year>\d{4})-(\d{2})", "date 2026-06-29")
print("search :", m.group(), "| year =", m.group("year"))

# findall and sub work as in re.
print("findall:", re.findall(r"\w+", "one two three"))
print("sub    :", re.sub(r"\s+", "_", "a  b   c"))
