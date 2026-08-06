#!/usr/bin/env python3
"""Perturb each of the engine's decision constants and require that SOME test notices.

WHY. Line coverage reports that a line RAN, not that anything depended on its value, and this engine
has repeatedly shipped code that no test actually held. A constant that can be changed by 4x in
either direction without a single test failing is either dead or unverified, and this is the only
check in the tree that can tell you which constants those are.

It earned its place on the day it was written: it reported `ac_density_work_threshold` unguarded --
a routing threshold added hours earlier, with three tests written specifically for it. Those tests
pinned the SIGN of the decision and not its value, because their subjects sat at products of 6 and
3996 against a threshold of 550. The tests passed for the right reason and still pinned nothing.

WHAT "UNGUARDED" MEANS, EXACTLY. That no test reacted to a 4x change -- NOT that nothing tests the
constant. Read a row as a question, not a verdict. There are three honest reasons a row comes back
unguarded, and only one of them is work:

  1. The constant is a DEFAULT for an injectable parameter, and the tests pass their own value. Then
     the BEHAVIOUR at the boundary is tested and the DEFAULT is a headroom choice no test can hold --
     `max_nodes` is capped at 65000 and `tests/automata/test_onepass.cpp` exercises the decline by
     constructing with node_cap=2, because a 65000-node pattern is impractical to build organically.
     That is better design than a hard-coded limit, not a gap. Such rows are labelled INJECTABLE
     below and are not counted as unguarded.
  2. The constant is a MEASURED threshold whose test cases sit far from its boundary. That IS work:
     bracket it with cases just either side (tests/engine/test_ac_density_gate.cpp has both shapes).
  3. The constant is pure sizing -- a hash bucket count, a cache way count -- where a 4x change
     cannot alter any observable behaviour, only speed. Nothing to write; say so where it is declared.

WHAT TO DO WITH A ROW. Two different fixes, and the distinction is the point:
  - the invariant is a MEASURED threshold  -> a test, bracketing the value from both sides;
  - the invariant is a RELATION between constants -> a static_assert, which covers every build where
    a test can only sample. `ac_density_work_threshold_low >= ac_density_work_threshold` is the
    example: swap those two and both routing regions become unsound while every test still passes.

NOT A GATE. Each trial is a full rebuild, so a sweep is tens of minutes; and a legitimately
insensitive constant would make it red forever. Run it deliberately, read it, act on it.

USAGE: make sabotage-sweep   (or: python3 tools/sabotage_sweep.py)
"""
import atexit
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAT  = re.compile(r"static constexpr\s+[A-Za-z_:<>\s]*?\b([a-z_][a-z0-9_]*)\s*\{(\d+)\}")


def injectable(name):
    """True when the constant is only a DEFAULT for a parameter callers (and tests) can override."""
    hit = subprocess.run(f"grep -rn '= *{name}\\b' include/real/ | grep -E '\\(|,'",
                         shell=True, cwd=ROOT, capture_output=True, text=True)
    return bool(hit.stdout.strip())


def collect():
    out = []
    for f in sorted(ROOT.glob("include/real/**/*.hpp")):
        txt = f.read_text(encoding="utf-8")
        for m in PAT.finditer(txt):
            if m.group(1).startswith("npos"):
                continue
            out.append((f, m.group(1), int(m.group(2)), m.group(0)))
    return out


def run(cmd, timeout=1800):
    return subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True, text=True, timeout=timeout)


def trial(path, original, decl, old, new):
    path.write_text(original.replace(decl, decl.replace("{%d}" % old, "{%d}" % new), 1),
                    encoding="utf-8")
    # The EXIT CODE, not the text. The first cut grepped `make test | tail -5` for ctest's summary
    # line, which make pushes out of the last five lines with its own error output whenever the run
    # fails -- so every genuine catch read as a miss and the whole table came back uniformly
    # "unguarded". Calibrating against a constant already proven guarded by hand is what caught it.
    result = run("make test")
    path.write_text(original, encoding="utf-8")
    blob = result.stdout + result.stderr
    if "error:" in blob and "tests passed" not in blob:
        return "no-compile"
    return "caught" if result.returncode != 0 else "MISSED"


def main():
    # A kill mid-trial leaves a patched header behind; it happened, and the patched value was then
    # captured by a hand-made backup and nearly re-applied. Restore whatever ends this process.
    atexit.register(lambda: subprocess.run("git checkout include/", shell=True, cwd=ROOT))

    consts = collect()
    print(f"{len(consts)} decision constants\n", flush=True)
    print(f"{'constant':<34}{'value':>8}  {'/4':>10}{'x4':>10}   verdict", flush=True)
    unguarded = []
    for path, name, val, decl in consts:
        original = path.read_text(encoding="utf-8")
        lo = max(1, val // 4) if val > 1 else 0
        hi = val * 4 if val > 0 else 8
        r1 = trial(path, original, decl, val, lo)
        r2 = trial(path, original, decl, val, hi)
        guarded = "caught" in (r1, r2)
        label = "guarded" if guarded else ("INJECTABLE" if injectable(name) else "UNGUARDED")
        if label == "UNGUARDED":
            unguarded.append((name, val, str(path.relative_to(ROOT))))
        print(f"{name:<34}{val:>8}  {r1:>10}{r2:>10}   {label}", flush=True)
    print(f"\n{len(unguarded)} constant(s) no test reacts to at 4x "
          "(INJECTABLE rows excluded -- their boundary is tested through the hook):", flush=True)
    for name, val, where in unguarded:
        print(f"  {name} = {val}   ({where})", flush=True)
    print("\nEach is a question: is the invariant a measured threshold (write a bracketing test) or a\n"
          "relation between constants (write a static_assert)? Neither answer is 'raise the value'.",
          flush=True)


main()
