#!/usr/bin/env python3
"""Executable layering contract for include/real/.

The engine headers are partitioned into dependency tiers. This check makes the contract enforceable: a
header may include only from its own tier or a lower one. An include that climbs a tier — the foundation
reaching up into the runtime, say — fails the build, so the layering stays a fact rather than a wish.

The tiers, low to high (verified against the real include graph, which is why unicode sits *above* core:
its generated tables index the IR's code_range):

    version                 a dependency-free leaf; any tier may include it
    core                    the IR and primitives: program, config, charclass
    unicode                 UTF-8 decode + the generated property/fold tables (use the IR)
    engine, automata        the runtime: the Pike VM, storage-free scratch, prefilter, assert eval,
                            the lazy DFAs and one-pass extractor. ONE tier: pike -> onepass is one-way
                            (onepass never includes pike) — a cross-reference allowed within the tier,
                            forbidden across tiers.
    frontend                the parser and compiler (they consume the runtime's prefilter / utf8_ranges)
    root                    the public headers (real.hpp, dfa.hpp) and the storage assembly they drive

compat/ (std/, re2/, …) holds the drop-in compatibility consumers and ranks with root.
"""
import contextlib
import io
import pathlib
import re
import sys
import tempfile

RANK = {"core": 1, "unicode": 2, "engine": 3, "automata": 3, "frontend": 4, "root": 5, "compat": 5}
# The first path segment after "real/" is captured for tiering; any deeper segments (e.g. compat's own
# std/, re2/ subfolders) are consumed but not captured, so a two-level include is tiered by its top
# directory alone, same as a one-level one.
INCLUDE = re.compile(r'#include\s+[<"]real/(?:([a-z0-9_]+)/)?(?:[a-z0-9_]+/)*([a-z0-9_]+)\.hpp[>"]')


def check(root: pathlib.Path) -> int:
    """Judges the header tree under ``root`` (an ``include/real`` directory); prints the verdict."""
    headers = sorted(root.rglob("*.hpp"))
    if not headers:
        print("check-layers: FAIL -- no headers under include/real/")
        return 1
    layer_of = {}
    for path in headers:
        rel = path.relative_to(root)
        layer_of[path.stem] = rel.parts[0] if len(rel.parts) > 1 else "root"

    violations = []
    for path in headers:
        from_layer = layer_of.get(path.stem, "root")
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.lstrip().startswith("//"):
                continue  # a commented include (e.g. the "Users: #include ..." banner) is not a real edge
            m = INCLUDE.search(line)
            if not m:
                continue
            to_layer = m.group(1) or "root"
            base = m.group(2)
            if base == "version":
                continue  # the leaf everyone may use
            if RANK.get(to_layer, 5) > RANK.get(from_layer, 5):
                violations.append(f"  {from_layer}/{path.stem}.hpp includes {to_layer}/{base}.hpp "
                                  f"(a tier {RANK.get(to_layer, 5)} header from tier {RANK.get(from_layer, 5)})")

    # The engine headers avoid std::hash / std::unordered_map: their out-of-line libc++ symbols (e.g.
    # __hash_memory) drift across toolchains, and lazy_dfa.hpp's hash_trans states the rule. It was being
    # broken in the very file that documents it, with nothing checking -- so it is checked here. One line
    # per genuine exception carries REAL_ALLOW_STD_HASH and says why; anything else is a violation.
    hash_uses = []
    for path in sorted(root.rglob("*.hpp")):
        rel = path.relative_to(root)
        if rel.parts[0] == "compat":
            continue  # the compat shims mirror foreign APIs and are not on any scan path
        for num, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            stripped = line.strip()
            if stripped.startswith(("//", "*", "/*")):
                continue  # prose NAMING the rule is not a use of it -- lazy_dfa.hpp and onepass.hpp both explain it
            if "REAL_ALLOW_STD_HASH" in line:
                continue
            if re.search(r"\bstd::hash\b|\bstd::unordered_(map|set)\b|#include <unordered_(map|set)>", line):
                hash_uses.append(f"  {rel}:{num}: {line.strip()[:88]}")

    if violations:
        print("check-layers: forbidden upward include(s) — the layering contract is broken:")
        print("\n".join(violations))
        return 1
    if hash_uses:
        print("check-layers: std::hash / std::unordered_* in an engine header (libc++ symbols drift across")
        print("  toolchains — see lazy_dfa.hpp's hash_trans). Use the in-house FNV, or mark the line")
        print("  REAL_ALLOW_STD_HASH with the reason it is safe there:")
        print("\n".join(hash_uses))
        return 1
    print(f"check-layers: {len(headers)} headers, layering holds "
          "(core < unicode < runtime < frontend < root), and no")
    print("  unmarked std::hash / std::unordered_* in the engine headers.")
    return 0


# Arm markers: the substring each verdict prints, so a case asserts WHICH arm answered, not only that
# something did. A case that trips an arm must print that arm's marker and no other arm's.
_ARMS = {
    "empty": "no headers under include/real/",
    "upward": "forbidden upward include",
    "hash": "std::hash / std::unordered_* in an engine header",
    "ok": "layering holds",
}


def self_test() -> int:
    """Drives each arm of ``check`` alone against a synthetic tree.

    Every case is one small tree and the arm it must reach: the verdict has to carry that arm's
    marker and none of the others', so an arm that stopped firing, or one that fires on the wrong
    case, fails here rather than leaving the exit status unchanged.
    """
    cases = [
        # (name, {relative path: content}, expected rc, arm)
        ("empty tree", {}, 1, "empty"),
        ("core includes engine", {"core/program.hpp": '#include "real/engine/pike.hpp"\n',
                                  "engine/pike.hpp": ""}, 1, "upward"),
        ("engine includes compat (compat ranks with root)",
         {"engine/pike.hpp": '#include "real/compat/std/regex.hpp"\n', "compat/std/regex.hpp": ""}, 1, "upward"),
        ("a root header includes the engine", {"real.hpp": '#include "real/engine/pike.hpp"\n',
                                               "engine/pike.hpp": ""}, 0, "ok"),
        ("a commented upward include is not an edge",
         {"core/program.hpp": '// #include "real/engine/pike.hpp"\n', "engine/pike.hpp": ""}, 0, "ok"),
        ("version is the leaf every tier may include",
         {"core/program.hpp": '#include "real/version.hpp"\n', "version.hpp": ""}, 0, "ok"),
        ("std::unordered_map in an engine header",
         {"engine/pike.hpp": "std::unordered_map<int, int> m;\n"}, 1, "hash"),
        ("std::hash in a compat shim is exempt", {"compat/std/regex.hpp": "std::hash<int> h;\n"}, 0, "ok"),
        ("a line marked REAL_ALLOW_STD_HASH is exempt",
         {"engine/pike.hpp": "std::hash<int> h; // REAL_ALLOW_STD_HASH: never crosses a library boundary\n"},
         0, "ok"),
        ("prose naming the rule is not a use", {"automata/lazy_dfa.hpp": "// avoid std::hash here\n"}, 0, "ok"),
    ]
    failures = 0
    for name, files, want_rc, arm in cases:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "include" / "real"
            root.mkdir(parents=True)
            for rel, content in files.items():
                path = root / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = check(root)
        text = out.getvalue()
        wrong = [other for other, marker in _ARMS.items() if other != arm and marker in text]
        if rc != want_rc or _ARMS[arm] not in text or wrong:
            print(f"SELF-TEST FAILED: {name}: rc={rc} (want {want_rc}), arm {arm!r} "
                  f"{'present' if _ARMS[arm] in text else 'ABSENT'}, other arms {wrong}")
            print("    " + text.replace("\n", "\n    "))
            failures += 1
    if failures:
        print(f"check-layers: self-test FAILED ({failures} of {len(cases)} case(s))")
        return 1
    print(f"check-layers: self-test OK — {len(cases)} cases, each arm (empty tree, upward include, "
          "engine std::hash) reached alone, and each exemption (root, commented include, version, compat, "
          "marker, prose) left alone")
    return 0


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    return check(pathlib.Path(__file__).resolve().parent.parent / "include" / "real")


if __name__ == "__main__":
    sys.exit(main())
