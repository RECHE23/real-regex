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

std/ is the std::regex-compatibility consumer and ranks with root.
"""
import pathlib
import re
import sys

RANK = {"core": 1, "unicode": 2, "engine": 3, "automata": 3, "frontend": 4, "root": 5, "std": 5}
INCLUDE = re.compile(r'#include\s+[<"]real/(?:([a-z]+)/)?([a-z0-9_]+)\.hpp[>"]')


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent / "include" / "real"
    layer_of = {}
    for path in root.rglob("*.hpp"):
        rel = path.relative_to(root)
        layer_of[path.stem] = rel.parts[0] if len(rel.parts) > 1 else "root"

    violations = []
    for path in sorted(root.rglob("*.hpp")):
        from_layer = layer_of.get(path.stem, "root")
        for line in path.read_text().splitlines():
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

    if violations:
        print("check-layers: forbidden upward include(s) — the layering contract is broken:")
        print("\n".join(violations))
        return 1
    print("check-layers: the include layering holds (core < unicode < runtime < frontend < root).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
