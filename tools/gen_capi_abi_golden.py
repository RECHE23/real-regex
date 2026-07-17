#!/usr/bin/env python3
"""Generate the C ABI surface golden from bindings/c/real_capi.h (single source of truth).

The golden pins contract shape for the frozen-additive C ABI: enum ordinals, documented
flag bit values (cross-checked against real::flags in the C++ test), and normalized
function prototypes (return type + param types; parameter *names* are stripped — a rename
is not an ABI break). Order of FN lines follows header appearance (conservative pin).

Usage:
  python3 tools/gen_capi_abi_golden.py              # write tests/bindings/capi_abi_golden.txt
  python3 tools/gen_capi_abi_golden.py --stdout     # print golden to stdout
  python3 tools/gen_capi_abi_golden.py --check      # exit 1 if committed golden is stale
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "bindings" / "c" / "real_capi.h"
GOLDEN = ROOT / "tests" / "bindings" / "capi_abi_golden.txt"

# Documented C ABI flag bits (header table) — must match real::flags for the shared set.
# allow_raw_byte=256 is C++-only (not in the C flags table); not pinned here.
DOCUMENTED_FLAGS = [
    ("icase", 1),
    ("multiline", 2),
    ("dotall", 4),
    ("bytes", 8),
    ("verbose", 16),
    ("ecma", 32),
    ("ascii", 64),
    ("dollar_endonly", 128),
]


def strip_comments(text: str) -> str:
    # Remove /* ... */ then // lines (header is C-style).
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//.*?$", " ", text, flags=re.M)
    return text


def extract_enums(body: str) -> list[tuple[str, int]]:
    """Parse `enum { NAME = N, ... }` blocks; return (name, value) in appearance order."""
    out: list[tuple[str, int]] = []
    for m in re.finditer(r"enum\s*\{([^}]*)\}", body):
        block = m.group(1)
        for em in re.finditer(r"(REAL_(?:ERR|MODE)_[A-Z0-9_]+)\s*=\s*(\d+)", block):
            out.append((em.group(1), int(em.group(2))))
    return out


def normalize_type_token(t: str) -> str:
    t = re.sub(r"\s+", " ", t.strip())
    # "const char *" / "const char*" → "const char*"
    t = re.sub(r"\s+\*", "*", t)
    t = re.sub(r"\*\s+", "*", t)
    # "const char* const*" style
    t = re.sub(r"\s*,\s*", ",", t)
    return t


def strip_param_name(param: str) -> str:
    """Turn 'const char* pattern' or 'size_t len' into the type only."""
    p = param.strip()
    if not p or p == "void":
        return p
    # Array forms unlikely in this ABI; handle trailing []
    array_suffix = ""
    am = re.search(r"(\[\s*\])\s*$", p)
    if am:
        array_suffix = "[]"
        p = p[: am.start()].rstrip()
    # Split type / name: last identifier is the name unless the whole thing is a type keyword.
    # Handle: type name, type *name, type **name, const type * const name
    tokens = p.split()
    if len(tokens) == 1:
        return normalize_type_token(p) + array_suffix
    # If last token looks like a name (identifier, no *), drop it.
    last = tokens[-1]
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", last) and "*" not in last:
        typ = " ".join(tokens[:-1])
        return normalize_type_token(typ) + array_suffix
    return normalize_type_token(p) + array_suffix


def extract_functions(body: str) -> list[str]:
    """Extract normalized FN lines: ret name(type,type,...)"""
    # Only the extern "C" region content already; match top-level decls ending in );
    # Multi-line prototypes: join until );
    decls: list[str] = []
    # Remove preprocessor and braces content of enums already plain
    # Match return-type + real_* name + ( params );
    pattern = re.compile(
        r"((?:const\s+)?(?:struct\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*?)\s+"
        r"(real_[a-z0-9_]+)\s*\((.*?)\)\s*;",
        re.S,
    )
    for m in pattern.finditer(body):
        ret = normalize_type_token(m.group(1))
        name = m.group(2)
        params_raw = m.group(3).strip()
        if not params_raw or params_raw == "void":
            params_n = "void"
        else:
            # Split on commas not inside parentheses (none expected here).
            parts = [p.strip() for p in params_raw.split(",") if p.strip()]
            params_n = ",".join(strip_param_name(p) for p in parts)
        decls.append(f"FN {ret} {name}({params_n})")
    return decls


def generate(header_text: str) -> str:
    body = strip_comments(header_text)
    # Drop preprocessor noise; keep braces until enums are extracted.
    body = re.sub(r'#\s*include[^\n]*', " ", body)
    body = re.sub(r'#\s*ifndef[^\n]*', " ", body)
    body = re.sub(r'#\s*define[^\n]*', " ", body)
    body = re.sub(r'#\s*endif[^\n]*', " ", body)
    body = re.sub(r'#\s*ifdef[^\n]*', " ", body)
    body = re.sub(r"extern\s+\"C\"\s*\{", " ", body)

    enums = extract_enums(body)
    # After enums: flatten braces so multi-line prototypes are easier to match.
    body_flat = body.replace("}", " ")

    lines: list[str] = [
        "# REAL C ABI golden — GENERATED from bindings/c/real_capi.h",
        "# DO NOT EDIT BY HAND. Regenerate: python3 tools/gen_capi_abi_golden.py",
        "# Source of truth: the header. This file is a pin so silent contract drift fails CI.",
        "",
        "# --- enums (ordinal contract) ---",
    ]
    for name, val in enums:
        lines.append(f"ENUM {name}={val}")
    lines.append("")
    lines.append("# --- flags (documented C bitmask; must equal real::flags) ---")
    for name, val in DOCUMENTED_FLAGS:
        lines.append(f"FLAG {name}={val}")
    lines.append("")
    lines.append("# --- functions (normalized prototypes, param names stripped) ---")
    for fn in extract_functions(body_flat):
        lines.append(fn)
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--stdout", action="store_true", help="print golden to stdout")
    ap.add_argument("--check", action="store_true", help="fail if golden file is stale")
    ap.add_argument(
        "--inject-omit-fn",
        metavar="NAME",
        help="can-fail: omit this real_* function from generated golden (must break --check)",
    )
    ap.add_argument(
        "--inject-enum",
        metavar="NAME=VAL",
        help="can-fail: override an ENUM line (e.g. REAL_ERR_SYNTAX=99)",
    )
    args = ap.parse_args()

    text = HEADER.read_text(encoding="utf-8")
    golden = generate(text)

    if args.inject_omit_fn:
        golden = "\n".join(
            ln for ln in golden.splitlines() if f" {args.inject_omit_fn}(" not in ln
        ) + "\n"
    if args.inject_enum:
        name, _, val = args.inject_enum.partition("=")
        patched: list[str] = []
        for ln in golden.splitlines():
            if ln.startswith(f"ENUM {name}="):
                patched.append(f"ENUM {name}={val}")
            else:
                patched.append(ln)
        golden = "\n".join(patched) + "\n"

    if args.stdout:
        sys.stdout.write(golden if golden.endswith("\n") else golden + "\n")
        return 0

    if args.check:
        if not GOLDEN.is_file():
            print(f"gen_capi_abi_golden: FAIL — missing {GOLDEN}", file=sys.stderr)
            return 1
        existing = GOLDEN.read_text(encoding="utf-8")
        if existing != golden:
            print(
                "gen_capi_abi_golden: FAIL — golden stale vs bindings/c/real_capi.h\n"
                "  regenerate: python3 tools/gen_capi_abi_golden.py",
                file=sys.stderr,
            )
            # Show a short unified-ish diff of line sets
            old_set = existing.splitlines()
            new_set = golden.splitlines()
            for i, (a, b) in enumerate(zip(old_set, new_set)):
                if a != b:
                    print(f"  first mismatch at line {i + 1}:", file=sys.stderr)
                    print(f"    golden: {a}", file=sys.stderr)
                    print(f"    header: {b}", file=sys.stderr)
                    break
            else:
                if len(old_set) != len(new_set):
                    print(
                        f"  length golden={len(old_set)} header={len(new_set)}",
                        file=sys.stderr,
                    )
            return 1
        print(f"gen_capi_abi_golden: OK — {GOLDEN.relative_to(ROOT)} matches header")
        return 0

    GOLDEN.parent.mkdir(parents=True, exist_ok=True)
    GOLDEN.write_text(golden if golden.endswith("\n") else golden + "\n", encoding="utf-8")
    n_fn = sum(1 for ln in golden.splitlines() if ln.startswith("FN "))
    n_en = sum(1 for ln in golden.splitlines() if ln.startswith("ENUM "))
    print(
        f"gen_capi_abi_golden: wrote {GOLDEN.relative_to(ROOT)} "
        f"({n_en} enums, {len(DOCUMENTED_FLAGS)} flags, {n_fn} functions)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
