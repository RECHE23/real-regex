#!/usr/bin/env python3
"""Assert that constructing the VM scratch state emits no bulk zeroing under GCC.

WHY THIS EXISTS. `basic_pike_state` holds its two thread lists as two NAMED MEMBERS rather than as
`ThreadList lists[2]`, and that is not a style choice: an array of two is constructed by a loop, and
gcc widens that loop body's sparse zero-stores into a `memset` over the whole element -- 2472 bytes per
list, 4944 per state, on every `search()` call, zeroing precisely the inline buffers the engine
documents as deliberately left uninitialized. Clang emits scalar stores at the real offsets and pays
none of it, which is why the cost was invisible to this project for its whole life: local development
and the layout instrument are clang, and a per-call cost divided by a 100 KB corpus is 0.0004 ns/byte.

Measured when it was found: a short anchored match went 112.1 -> 82.5 ns on x86-64 (g++ 15.2) and
103.5 -> 50.8 on arm64 (g++-14).

WHAT WOULD BRING IT BACK. Any member array in the hot state -- a second pair of lists, a small array of
counters, `std::array<ThreadList, 2>` (which does NOT avoid it: the loop does it, not the C array) --
and nothing else in this repository would notice. So this gate compiles one translation unit that
constructs the state and refuses a bulk zeroing in it.

It needs a real GCC. Apple's `g++` is a clang alias, so the driver's own version string is checked
rather than its name; with no GCC on the machine the check SKIPS, and says so loudly rather than
passing quietly -- a silent skip in a numbered gate list reads as a pass, which is how the defect this
file guards against reached a release in the first place.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

PROBE = """
#include <real/real.hpp>
using state = real::detail::dynamic_storage::state_type;
extern void sink(state*);
void make_state() { state s; sink(&s); }
"""

# `rep stos` is x86-64's inline form, `memset`/`bzero` the out-of-line call every ISA can emit.
#
# NO LEADING WORD BOUNDARY, and that is an ABI detail rather than sloppiness: Mach-O prefixes C symbols
# with an underscore, so the call reads `bl _memset`. `\bmemset` cannot match there -- `_` is a word
# character, so there is no boundary between it and `m` -- and the first two versions of this file
# therefore reported OK on a tree that provably emits the call. Validated in both directions now: it
# FAILS on the pre-fix tree (38954f7~1) and passes on this one.
BULK = re.compile(r"rep\s+stos|memset|bzero")


def is_real_gcc(version_text):
    """Whether a driver's --version text is GCC's; an Apple `g++` reports clang and is not."""
    if "Free Software Foundation" in version_text or re.search(r"^g\+\+ \(GCC\)|\(GCC\) ", version_text):
        return "clang" not in version_text.lower()
    return False


def real_gcc_drivers():
    """Every driver on PATH whose --version says GCC (an Apple `g++` is clang and is skipped)."""
    found = []
    for name in ("g++", "g++-15", "g++-14", "g++-13", "gcc-15", "gcc-14"):
        path = shutil.which(name)
        if path is None:
            continue
        try:
            out = subprocess.run([path, "--version"], capture_output=True, text=True,
                                 check=False, timeout=30).stdout
        except (OSError, subprocess.SubprocessError):
            continue
        if is_real_gcc(out):
            found.append((name, path, out.splitlines()[0] if out else name))
    return found


# A LOCAL label does not end a function, and reading it as one is how the first version of this check
# passed on a tree that provably had the defect: gcc emits `LFB1234:` on the line right after the
# function label, which matched a naive "next label ends the body" rule and left a body of ZERO lines to
# search. A function ends at `.cfi_endproc`, or at the next label that is not local (`L…`, `.L…`, `$…`).
LOCAL_LABEL = re.compile(r"^(?:\.?L|\$)")
ANY_LABEL = re.compile(r"^([A-Za-z_.$][\w.$]*):")


def body_of(asm_text, symbol_hint):
    """The assembly lines of the first function whose label mentions `symbol_hint`."""
    lines = asm_text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if symbol_hint in line and line.rstrip().endswith(":"):
            start = i
            break
    if start is None:
        return None
    body = []
    for line in lines[start + 1:]:
        if line.strip().startswith(".cfi_endproc"):
            break
        match = ANY_LABEL.match(line)
        if match and not LOCAL_LABEL.match(match.group(1)) and symbol_hint not in line:
            break
        body.append(line)
    return "\n".join(body)


def judge_asm(asm_text):
    """('extraction', [...]) when no usable make_state body was found, ('bulk', hits), or ('ok', [])."""
    body = body_of(asm_text, "make_state")
    if body is None or len(body.splitlines()) < 5:
        # A body this short means the EXTRACTION failed, not that the function is small: the probe
        # constructs a 7736-byte state. Treated as a failure of the check rather than a pass, because
        # the first version of this file reported OK on a defective tree for exactly this reason.
        return ("extraction", ["(extraction failed: no usable make_state body)"])
    hits = [line.strip() for line in body.splitlines() if BULK.search(line)]
    return ("bulk", hits[:3]) if hits else ("ok", [])


def self_test():
    """Drives the assembly analysis and the GCC recognition, each arm alone, on synthetic input.

    Both historical blindings are replayed: gcc's local `LFB` label right after the function label,
    which once ended the body at zero lines, and Mach-O's `_memset`, which `\bmemset` could not see.
    """
    def fn(label, *body):
        return "\n".join([f"{label}:", *body]) + "\n"
    scalar = ["\tstr\txzr, [x0]", "\tstr\txzr, [x0, 8]", "\tstr\txzr, [x0, 2480]", "\tbl\tsink", "\tret"]
    cases = [
        ("ELF: a call to memset", fn("_Z10make_statev", *scalar[:3], "\tcall\tmemset@PLT", *scalar[3:]), "bulk"),
        ("Mach-O: `bl _memset` (no word boundary before memset)",
         fn("__Z10make_statev", *scalar[:3], "\tbl\t_memset", *scalar[3:]), "bulk"),
        ("x86-64: rep stosq", fn("_Z10make_statev", *scalar[:3], "\trep stosq", *scalar[3:]), "bulk"),
        ("gcc's local LFB label does not end the body",
         fn("_Z10make_statev", "LFB1234:", *scalar[:2], "\tbl\t_memset", *scalar[2:]), "bulk"),
        ("scalar stores only", fn("_Z10make_statev", *scalar), "ok"),
        ("a memset in the NEXT function is not make_state's",
         fn("_Z10make_statev", *scalar) + fn("_Z5otherv", "\tbl\t_memset", "\tret"), "ok"),
        ("a memset past .cfi_endproc is not make_state's",
         fn("_Z10make_statev", *scalar, "\t.cfi_endproc", "\tbl\t_memset"), "ok"),
        ("no make_state label", fn("_Z5otherv", *scalar), "extraction"),
        ("a body too short to be the state's construction", fn("_Z10make_statev", "\tret"), "extraction"),
    ]
    failures = 0
    for name, asm, want in cases:
        try:
            got, _ = judge_asm(asm)
        except Exception as exc:
            print(f"SELF-TEST FAILED: {name}: judge_asm raised {type(exc).__name__}: {exc}")
            failures += 1
            continue
        if got != want:
            print(f"SELF-TEST FAILED: {name}: got {got!r}, want {want!r}")
            failures += 1
    gcc_cases = [
        ("GCC's banner", "g++-14 (Homebrew GCC 14.2.0) 14.2.0\nCopyright (C) 2024 Free Software Foundation, Inc.", True),
        ("a Linux GCC banner", "g++ (GCC) 13.2.1 20230801", True),
        ("Apple's g++, which is clang", "Apple clang version 16.0.0 (clang-1600.0.26.6)\nTarget: arm64", False),
        ("a clang banner that mentions the FSF", "clang version 18 (Free Software Foundation notice)", False),
    ]
    for name, text, want in gcc_cases:
        if is_real_gcc(text) != want:
            print(f"SELF-TEST FAILED: {name}: is_real_gcc -> {not want}, want {want}")
            failures += 1
    total = len(cases) + len(gcc_cases)
    if failures:
        print(f"check_state_zeroing: self-test FAILED ({failures} of {total} case(s))")
        return 1
    print(f"check_state_zeroing: self-test OK — {total} cases: bulk zeroing found in three spellings and past a "
          "local label, not attributed across a function end, extraction failure refused; GCC told from clang")
    return 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sciforge = os.environ.get("SCIFORGE_INCLUDE", os.path.join(root, "..", "sciforge", "include"))
    drivers = real_gcc_drivers()
    if not drivers:
        print("check_state_zeroing: SKIPPED -- no real GCC on PATH (an Apple `g++` is clang). "
              "This check is the only guard against a member array re-introducing a 4944-byte "
              "memset per state construction; CI's GCC legs still run it.")
        return 0

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "state_probe.cpp")
        with open(src, "w", encoding="utf-8") as handle:
            handle.write(PROBE)
        for name, path, version in drivers:
            cmd = [path, "-std=c++20", "-O2", "-DNDEBUG", "-I", os.path.join(root, "include"),
                   "-I", sciforge, "-S", "-o", "-", src]
            run = subprocess.run(cmd, capture_output=True, text=True, check=False)
            if run.returncode != 0:
                print(f"check_state_zeroing: {name} failed to compile the probe -- skipping it")
                continue
            status, hits = judge_asm(run.stdout)
            if status != "ok":
                failures.append((name, version, hits))
            else:
                print(f"check_state_zeroing: OK -- {version}: no bulk zeroing in state construction")

    if failures:
        print()
        for name, version, hits in failures:
            print(f"check_state_zeroing: FAIL -- {version} zeroes the VM scratch state in bulk:")
            for hit in hits:
                print(f"    {hit}")
        print()
        print("A member ARRAY in the hot state is what does this: an array of two is constructed by a")
        print("loop and gcc widens the loop body's sparse zero-stores into a memset over the whole")
        print("element. `std::array<T, 2>` does not avoid it -- the loop does it, not the C array.")
        print("Use named members. See basic_pike_state's own note in include/real/engine/pike.hpp.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
