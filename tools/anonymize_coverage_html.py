#!/usr/bin/env python3
"""Strip machine-local prefixes from llvm-cov HTML.

llvm-cov 16's ``-path-equivalence`` remaps coverage data onto local source
files so the renderer can find them. It does NOT rewrite the output hrefs or
the ``coverage/Users/<who>/...`` directory tree -- measured: the flag left
every published URL carrying ``/Users/rchenard`` and exited 0. The thing that
actually lands on Pages is this post-pass.

Usage:
    python3 tools/anonymize_coverage_html.py HTML_DIR ABS,NAME [ABS,NAME ...]

``ABS`` is a workspace root (real-regex, sciforge). ``NAME`` is the published
prefix (``real-regex``, ``sciforge``). Directories are moved and every ``*.html``
string is rewritten. ``style.css`` hrefs are recomputed for the new depth --
moving a file up six components would otherwise 404 the stylesheet.

Fails closed if the primary tree cannot be found and has not already been
rewritten. A missing sibling (no ``../sciforge``) is skipped when that prefix
is also absent from the HTML.
"""

from __future__ import annotations

import os
import re
import shutil
import sys
from pathlib import Path

_STYLE_HREF = re.compile(r"""href=(['"])(?:\.\./)*style\.css\1""")


def _die(msg: str) -> None:
    print(f"anonymize_coverage_html: FAIL -- {msg}", file=sys.stderr)
    sys.exit(1)


def _parse_mapping(raw: str) -> tuple[Path, str]:
    if "," not in raw:
        _die(f"mapping must be ABS,NAME (got {raw!r})")
    abs_s, name = raw.split(",", 1)
    if not abs_s or not name or "/" in name or name in (".", ".."):
        _die(f"invalid mapping {raw!r}")
    return Path(abs_s), name


def _prefix_strings(root: Path) -> list[str]:
    """Longest-first absolute prefixes that llvm-cov may have written."""
    resolved = root if root.is_absolute() else root.resolve()
    variants = {str(resolved)}
    text = str(resolved)
    if text.startswith("/private"):
        variants.add(text[len("/private") :])
    elif text.startswith("/"):
        variants.add("/private" + text)
    return sorted(variants, key=len, reverse=True)


def _candidate_dirs(html_dir: Path, root: Path) -> list[Path]:
    out: list[Path] = []
    for prefix in _prefix_strings(root):
        parts = Path(prefix).parts
        if not parts or parts[0] != "/":
            continue
        out.append(html_dir / "coverage" / Path(*parts[1:]))
    return out


def _find_src(html_dir: Path, root: Path) -> Path | None:
    for cand in _candidate_dirs(html_dir, root):
        if cand.is_dir():
            return cand
    return None


def _prune_empty_parents(start: Path, stop: Path) -> None:
    p = start
    while p != stop and p.is_dir():
        try:
            next(p.iterdir())
            return
        except StopIteration:
            parent = p.parent
            p.rmdir()
            p = parent


def _rewrite_html_file(path: Path, html_dir: Path, replacements: list[tuple[str, str]]) -> None:
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    original = text
    for old, new in replacements:
        text = text.replace(old, new)
    rel_style = Path(os.path.relpath(html_dir / "style.css", path.parent)).as_posix()

    def _style(_m: re.Match[str]) -> str:
        q = _m.group(1)
        return f"href={q}{rel_style}{q}"

    text = _STYLE_HREF.sub(_style, text, count=1)
    if text != original:
        path.write_text(text, encoding="utf-8", errors="surrogateescape")


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        _die("usage: anonymize_coverage_html.py HTML_DIR ABS,NAME [ABS,NAME ...]")
    html_dir = Path(argv[1])
    if not (html_dir / "index.html").is_file():
        _die(f"{html_dir}/index.html missing -- not an llvm-cov HTML tree")

    mappings = [_parse_mapping(a) for a in argv[2:]]
    # Longest first. The `coverage{prefix}` form must beat the bare prefix:
    # hrefs are `coverage/Users/.../real-regex/file.html`, and replacing
    # `/Users/.../real-regex` with `real-regex` would glue it into
    # `coveragereal-regex/file.html` (measured).
    replacements: list[tuple[str, str]] = []
    for root, name in mappings:
        for prefix in _prefix_strings(root):
            replacements.append((f"coverage{prefix}", f"coverage/{name}"))
            replacements.append((prefix, name))
    replacements.sort(key=lambda p: len(p[0]), reverse=True)

    coverage_root = html_dir / "coverage"
    if not coverage_root.is_dir():
        _die(f"{coverage_root} missing -- llvm-cov HTML layout changed")

    primary_root, primary_name = mappings[0]
    primary_src = _find_src(html_dir, primary_root)
    primary_dest = coverage_root / primary_name

    if primary_src is None and not primary_dest.is_dir():
        tried = ", ".join(str(c) for c in _candidate_dirs(html_dir, primary_root))
        _die(
            f"no llvm-cov tree for {primary_root} (tried {tried}) and "
            f"{primary_dest} is absent -- HTML layout changed"
        )

    for root, name in mappings:
        src = _find_src(html_dir, root)
        dest = coverage_root / name
        if src is None:
            prefixes = _prefix_strings(root)
            # Sibling optional: sciforge is absent on a lone clone. Fail only
            # when the HTML still names that machine path.
            # Cheap scan of the index (the inventory); full-tree check is below.
            index = (html_dir / "index.html").read_text(encoding="utf-8", errors="replace")
            leftover = any(p in index for p in prefixes)
            if leftover and not dest.is_dir():
                _die(f"index.html still names {root} but {dest} was not produced")
            continue
        if src.resolve() != dest.resolve():
            if dest.exists():
                shutil.rmtree(dest)
            dest.parent.mkdir(parents=True, exist_ok=True)
            src.rename(dest)
            _prune_empty_parents(src.parent, coverage_root)

    for path in html_dir.rglob("*.html"):
        _rewrite_html_file(path, html_dir, replacements)

    # Witnesses: the published names exist, the machine prefixes do not.
    if not primary_dest.is_dir():
        _die(f"published tree {primary_dest} missing after rewrite")
    index = (html_dir / "index.html").read_text(encoding="utf-8", errors="replace")
    for old, _new in replacements:
        if old in index:
            _die(f"index.html still contains machine prefix {old}")
    for leaked in ("Users", "home"):
        leaked_dir = coverage_root / leaked
        if leaked_dir.exists():
            _die(f"leftover machine-path directory {leaked_dir}")
    glued = re.search(r"href='coverage(?!/)[^']+\.html'", index)
    if glued:
        _die(f"href rewrite glued the prefix: {glued.group(0)}")
    href = re.search(r"href='(coverage/[^']+\.html)'", index)
    if href is None or not (html_dir / href.group(1)).is_file():
        _die(
            "index.html href does not resolve to a file after rewrite "
            f"(sample {href.group(0) if href else 'missing'})"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
