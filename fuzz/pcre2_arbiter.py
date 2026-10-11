"""Arbitrates the divergences fuzz/pcre2_differential.cpp prints: REAL against PCRE2, decided by `re`.

Reads the differential's lines on standard input. Where REAL and PCRE2 disagree, `re` answers the same
query (the third-party `regex` module, in its `re`-compatible V0 mode, when `re` refuses the pattern: a
variable-width lookbehind). Each divergence lands in one class:

  lookaround-captures  REAL and PCRE2 agree once the groups opened inside a lookaround are set aside
                       (REAL leaves those captures empty: div_lookaround_captures)
  pcre-semantics       REAL answers as `re` does; PCRE2 is the one that differs
  nullable-loop        REAL differs from `re` on a pattern with a nullable loop (documented: REAL follows
                       RE2 there, not `re`)
  BUG                  anything else, and every left_context line

Exit status 1 when any BUG was seen, or when no summary line arrived (the differential did not finish).
"""

import collections
import re
import sys

import regex

FLAGS = ((1, re.IGNORECASE), (2, re.MULTILINE), (4, re.DOTALL))
_compiled = {}


def compile_reference(pattern, bits):
    key = (pattern, bits)
    cache = _compiled
    if key not in cache:
        flags = 0
        for bit, flag in FLAGS:
            if bits & bit:
                flags |= flag
        try:
            cache[key] = (re.compile(pattern, flags), "re")
        except re.error:
            try:
                cache[key] = (regex.compile(pattern, flags | regex.V0), "regex")
            except regex.error as error:
                cache[key] = (None, f"refused by re and regex: {error}")
    return cache[key]


def spans(text):
    if text == "nomatch":
        return None
    return [None if g == "-" else tuple(int(v) for v in g.split(",")) for g in text.split(";")]


def reference_answer(compiled, what, subject, pos, groups):
    # The differential counts bytes; `re` counts code points.
    char_pos = len(subject.encode()[:pos].decode())
    m = getattr(compiled, what)(subject, char_pos)
    if m is None:
        return None
    out = []
    for g in range(groups):
        start, end = m.span(g)
        out.append(None if start < 0 else (len(subject[:start].encode()), len(subject[:end].encode())))
    return out


def masked(answer, groups):
    if answer is None:
        return None
    return [None if g in groups else span for g, span in enumerate(answer)]


def main():
    counts = collections.Counter()
    examples = collections.defaultdict(list)
    summaries = []
    for line in sys.stdin:
        fields = line.rstrip("\n").split("\t")
        if fields[0] == "S":
            summaries.append(" ".join(fields[1:]))
            continue
        if fields[0] == "L":
            _, bits, pos, pattern_hex, subject_hex = fields
            counts["BUG"] += 1
            examples["BUG"].append(f"left_context: /{bytes.fromhex(pattern_hex).decode()}/ flags={bits} "
                                   f"subject={bytes.fromhex(subject_hex.strip('-')).decode()!r} pos={pos}")
            continue
        if fields[0] != "D":
            print(line, end="")
            continue
        _, what, bits, _ucp, pos, pattern_hex, subject_hex, mine, theirs, nullable_loop, look_groups = fields
        pattern = bytes.fromhex(pattern_hex).decode()
        subject = bytes.fromhex(subject_hex.strip("-")).decode()
        bits, pos = int(bits), int(pos)
        mine, theirs = spans(mine), spans(theirs)
        inside = set() if look_groups == "-" else {int(g) for g in look_groups.split(",")}
        compiled, engine = compile_reference(pattern, bits)
        reference = None if compiled is None else reference_answer(compiled, what, subject, pos, compiled.groups + 1)
        if compiled is not None and masked(mine, inside) == masked(theirs, inside):
            kind = "lookaround-captures"
        elif compiled is not None and masked(mine, inside) == masked(reference, inside):
            kind = "pcre-semantics"
        elif nullable_loop == "1":
            kind = "nullable-loop"
        else:
            kind = "BUG"
        counts[kind] += 1
        if len(examples[kind]) < (20 if kind == "BUG" else 3):
            examples[kind].append(f"{what} /{pattern}/ flags={bits} subject={subject!r} pos={pos} real={mine} "
                                  f"pcre={theirs} {engine}={reference}")
    for summary in summaries:
        print(summary)
    for kind in ("lookaround-captures", "pcre-semantics", "nullable-loop", "BUG"):
        print(f"{kind}: {counts[kind]}")
        for example in examples[kind]:
            print(f"  {example}")
    if not summaries:
        print("no summary line: the differential did not finish")
        return 1
    return 1 if counts["BUG"] else 0


if __name__ == "__main__":
    sys.exit(main())
