"""Shared scaffolding for the Unicode table generators (gen_unicode_props.py, gen_unicode_fold.py).

The two generators build different data (property ranges vs case-fold orbits) from different oracles,
but they share the same envelope: a GENERATED file header with the pinned Unicode version, an
exhaustive validation pass that aborts on any disagreement with `re`, and the write-and-report tail.
That envelope lives here so each generator stays just its algorithm plus configuration; the emitted
bytes are unchanged (the regen guards assert byte-identity).
"""
import sys
import unicodedata


def unidata_version():
    """The running CPython's Unicode data version -- the pin baked into each generated header."""
    return unicodedata.unidata_version


def _norm_version(v):
    """`16.0` and `16.0.0` are the same UCD; compare on the numeric tuple, not the string."""
    parts = [int(x) for x in str(v).split(".") if x.isdigit()]
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts[:3])


def cross_oracle_skew(source_version):
    """A reason string when the interpreter's UCD differs from the tables' source version, else None.

    The FIRST of the two skews a gen-time cross-oracle can face, and the only one with a version string
    on both sides: the tables are parsed from committed UCD sources, while everything asked of
    `unicodedata` answers for the interpreter. When those disagree, nothing built here can be checked
    against anything read there, so the oracle skips and says which two versions it declined to mix.
    The second skew -- the `regex` module's own property data -- has no version to read and is settled
    by behaviour instead; see `regex_version_skew`.

    Same shape as the guard bindings/python/tests/test_unicode_property_fuzz.py already applies to its
    own oracle: an honest skip on version skew rather than a failure that would accuse the tables.
    """
    if _norm_version(unicodedata.unidata_version) != _norm_version(source_version):
        return (f"interpreter unicodedata {unicodedata.unidata_version} != these tables' UCD sources "
                f"{source_version}; the assigned-code-point filter the cross-oracle needs cannot be "
                f"trusted across that skew")
    return None


def regex_version_skew(regex_module):
    r"""A reason string when the `regex` module's property data is a different Unicode than ours.

    WHY A BEHAVIOURAL PROBE AND NOT A VERSION STRING. The module ships its own property tables and
    exposes no version for them -- `regex._regex_core.unicodedata` is just the stdlib module it
    imports, which reports the INTERPRETER's Unicode, not the tables'. So the question is asked the
    only way it can be answered: does the module agree with this UCD about which code points EXIST?
    `\p{Cn}` is that question, and one disagreement settles it.

    WHY THIS AND NOT A DOMAIN FILTER, which was tried first and is not enough. Skipping code points
    unassigned here removes the obvious half -- U+1CEF0 and U+1F8D0..U+1F8D8 for `\p{Math}`, ten code
    points UCD 16.0 does not define at all -- and it took the binary-property disagreements from 10 to
    1 and the `scx` ones from 44 to 6. The remainder are ASSIGNED code points whose property VALUE
    moved between versions: U+0306, U+0308, U+0320, U+0331 are combining marks whose Script_Extensions
    set grows as new scripts are added. No domain filter can see that. When the two sides are different
    Unicodes, a disagreement is not evidence of a bug and the oracle has nothing to say.

    Exhaustive rather than sampled: it must not report "same version" on a skew it happened not to
    sample, since that is exactly when it would turn a version difference into a false table bug.
    """
    unassigned = regex_module.compile(r"\p{Cn}")
    for cp in range(0, 0x110000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        ch = chr(cp)
        if (unicodedata.category(ch) == "Cn") != bool(unassigned.match(ch)):
            return (f"the `regex` module and UCD {unicodedata.unidata_version} disagree on whether "
                    f"U+{cp:04X} is assigned, so its bundled property data is a different Unicode than "
                    f"these tables; a mismatch would say nothing about this generator")
    return None


def file_header(*, filename, brief, generator, doc_lines, guard, includes, version_kind, version_const):
    """The GENERATED-header preamble shared by both tables, up to and including the version constant.

    Parameters carry every part that differs between the two headers so the output is byte-for-byte
    what each generator emitted before this module existed.

    Args:
        filename: The header's basename (for the \\file line).
        brief: The \\brief one-liner.
        generator: The producing script's basename (for the DO-NOT-EDIT line).
        doc_lines: Body doc lines (already Unicode-version-interpolated), each emitted as ` * <line>`.
        guard: The include-guard macro.
        includes: Exact `#include` lines (and any interleaved blank) between version.hpp and namespace.
        version_kind: The noun in the version-constant doc ("tables" or "orbits").
        version_const: The name of the emitted `..._unidata_version` constant.
    """
    ver = unidata_version()
    out = [
        "/*!",
        f" * \\file {filename}",
        f" * \\brief {brief}",
        " *",
        f" * GENERATED by tools/{generator} -- DO NOT EDIT BY HAND.",
        " *",
    ]
    out += [f" * {line}" for line in doc_lines]
    out += [
        " */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "// Internal — do not include directly.",
        "// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/compat/std/regex.hpp>).",
        "",
        '#include "real/version.hpp"',
        "",
    ]
    out += includes
    out += [
        "",
        "namespace real::detail {",
        "",
        f'  inline constexpr const char* {version_const} {{"{ver}"}}; '
        f"//!< The Unicode data version these {version_kind} were generated from.",
        "",
    ]
    return out


def file_footer(guard):
    """The namespace-close + include-guard-close tail shared by both headers."""
    return [
        "} // namespace real::detail",
        "",
        f"#endif // {guard}",
        "",
    ]


def validate_exhaustive(code_points, check, on_abort):
    """Second-net validation: every code point's built-table answer must agree with `re`.

    Loops `code_points`, and for each calls `check(cp)`; a non-None return is a disagreement whose
    string is printed (up to eight). If any disagreed, aborts (sys.exit) with `on_abort(count)` -- so a
    table-building bug can never slip past into a committed header. Each generator supplies its own
    `check`/`on_abort` so the diagnostic wording stays exactly what it was (ranges vs orbits).

    Args:
        code_points: Iterable of code points to check.
        check: cp -> None if the table agrees with `re`, else a short mismatch-detail string.
        on_abort: count -> the sys.exit message when any code point disagreed.
    """
    mismatches = 0
    for cp in code_points:
        detail = check(cp)
        if detail is not None:
            mismatches += 1
            if mismatches <= 8:
                print("  MISMATCH " + detail, file=sys.stderr)
    if mismatches:
        sys.exit(on_abort(mismatches))


def write_lines(path, lines, summary):
    """Write `lines` (newline-joined, trailing newline) to `path` and print `summary`."""
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(summary)
