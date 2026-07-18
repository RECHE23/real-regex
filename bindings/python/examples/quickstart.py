"""quickstart.py — the landing page's Python quickstart tab (docs/site/index.md), run by
`python-test` (tests/test_quickstart_example.py) against the just-built binding so the code the
page shows is exactly the code that's tested, never merely illustrative (doc-site P1b-A
gate-snippet). A pair of region-boundary comments further down bounds the block a Sphinx
`literalinclude` pulls verbatim onto the page (NOTE: keep this docstring from ever spelling out
that pair of comments literally — a literalinclude start-after/end-before match is a
first-occurrence substring search, and a mention up here would shadow the real one below) — do
not edit the marked lines without also checking docs/site/index.md's Python tab still matches
byte-for-byte.
"""

# [quickstart]
import real as re   # drop-in for the standard library's re

m = re.search(r"(\w+)@(\w+)\.(\w+)", "info@example.com")
m.group(2)                # 'example' — linear time, no backtracking cliff
# [/quickstart]

assert m.group(2) == "example", f"quickstart snippet drifted: got {m.group(2)!r}"
