"""quickstart.py — the landing page's Python quickstart tab (docs/site/_templates/landing.html,
doc-site P1c), run by `python-test` (tests/test_quickstart_example.py) against the just-built
binding so the code the page shows is exactly the code that's tested, never merely illustrative
(doc-site P1b-A gate-snippet). A pair of region-boundary comments further down bounds the block
conf.py's `_inject_quickstart` (html-page-context hook) reads and Pygments-highlights onto the
page at build time (NOTE: keep this docstring from ever spelling out that pair of comments
literally — the hook's own region search is a first-occurrence substring search, the same
start-after/end-before contract Sphinx `literalinclude` used before P1c, and a mention up here
would shadow the real one below) — do not edit the marked lines without also checking the
landing's Python tab still matches byte-for-byte (rebuild with `make docs-site` and diff).
"""

# [quickstart]
import real as re   # drop-in for the standard library's re

m = re.search(r"(\w+)@(\w+)\.(\w+)", "info@example.com")
m.group(2)                # 'example' — linear time, no backtracking cliff
# [/quickstart]

assert m.group(2) == "example", f"quickstart snippet drifted: got {m.group(2)!r}"
