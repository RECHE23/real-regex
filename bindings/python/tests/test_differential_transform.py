"""`re` as the oracle for the TRANSFORMATION surfaces: split, sub, subn, findall, expand, accessors.

test_differential_fuzz asks `re` for every answer on ``search``/``finditer``. These surfaces were
covered only by hand-written expectations, which check the cases someone thought of -- and the
subcontracts here are the ones easiest to get subtly wrong:

* ``findall`` returns a SHAPE, not just values: ``list[str]`` with zero or one group, ``list[tuple]``
  from two, so ``[('a',)]`` and ``['a']`` are different answers and a value-only comparison passes
  either way.
* ``split`` splits on empty matches (CPython 3.7 changed this) and interleaves captures, with
  ``None`` for a group that did not participate.
* A negative ``maxsplit`` returns the subject untouched; a negative ``count`` replaces nothing.
* Templates carry their own grammar: ``\\g<0>``, ``\\g<name>``, octal ``\\0``, ``\\\\``.

Refusal is part of the contract, so the cases `re` REJECTS are compared on the exception rather than
skipped: a caller writing ``except re.error`` gets no help from an engine that raises ``TypeError``,
and ``\\g<9>`` on a two-group pattern must not quietly expand to nothing.

The default is EXHAUSTIVE -- the whole cross-product, 600k+ comparisons in about three seconds, so
there is no case count to justify and no sampling to defend. ``REAL_TRANSFORM_STRIDE`` thins it if a
later widening makes that untrue; the run always prints how many cases it dropped.
"""
import os
import re
import unittest

import real

#: Chosen for the subcontracts above: empty-matchable patterns (the 3.7 split rule and the
#: adjacent-empty sub rule), capturing shapes (findall's list-of-str vs list-of-tuple pivot), and
#: alternations where one branch's group does not participate (split's `None` slots).
_PATTERNS_ASCII = [
    "a", "ab", "[ab]+", "a*", "x*", "a*?", "a?", ".??", "", r"\b", r"\B",
    "(?=a)", "(?!a)", "^", "$", r"\A", r"\Z", "()", "(a|)", "(|a)", "(a|)b", "(a*)",
    "(a)", "(a)(b)", "(a)|(b)", "(a(b))", "(?P<x>a)", "(?P<x>a)(?P<y>b)",
    "(a)*", "(a)?", "(?:a)(b)", "(a)|b", "a|(b)", "[^a]", ".", "(.)", "(.)(.)",
    "(a)(b)(c)", "(a|b)(c|d)", "a{2}", "(a){2}", "a$|^b", r"\d+", r"\w",
]

#: The code-point route is not the byte route, so these carry a combining-free accent, an astral
#: pair, a case-folding pair whose sides differ in UTF-8 length (K and the Kelvin sign, long s and
#: s), and a script the ASCII list cannot reach.
_PATTERNS_UNICODE = [
    "é", "é+", "(é)", "[é]", "[^é]", "€", "😀", "(😀)", "😀*", "[à-ÿ]+",
    r"\w+", r"\W", r"\b\w+\b", "K", "ſ", "(?P<n>é)(?P<m>è)?", "à|é", ".",
]

_PATTERNS_BYTES = [b"a", b"ab", b"a*", b"", b"(a)", b"(a)(b)", b"[ab]+", rb"\b",
                   rb"\w+", rb"\d", b"(?P<x>a)", b"\xc3\xa9", b".", b"()"]

_SUBJECTS_ASCII = ["", "a", "b", "ab", "ba", "aab", "abc", "aaa", "banana",
                   "xaybz", "a\nb", "  a  ", "abab", "aa\n", "\n"]
_SUBJECTS_UNICODE = ["", "é", "ée", "éàü", "aéb", "€1", "😀", "a😀b", "😀😀",
                     "Ka", "ſs", "café", "naïve", "ÉÉ", "Кот"]
#: A lone 0xFF and a bare continuation pair are not valid UTF-8: on the bytes surfaces they must be
#: handled as bytes, and `re` says what that means.
_SUBJECTS_BYTES = [b"", b"a", b"ab", b"aab", b"\xc3\xa9", b"a\xc3\xa9b", b"\xff",
                   b"a\nb", b"\x80\x80", b"abab"]

#: Paired so the sweep never assumes real's flag values equal re's.
_FLAGSETS = [(0, 0), (real.I, re.I), (real.M, re.M), (real.S, re.S),
             (real.I | real.M, re.I | re.M), (real.M | real.S, re.M | re.S)]

_MAXSPLITS = [None, 0, 1, 2, -1]
_COUNTS = [None, 0, 1, 2, -1]

#: The last five are REFUSAL cases -- an out-of-range reference, a backreference on a groupless
#: pattern, a lone trailing backslash, and two unterminated `\g<` forms. They are here for exception
#: parity, which is why they are not filtered out.
_REPLS_STR = ["-", "", "<>", r"[\g<0>]", r"\g<1>", r"\g<x>", r"\1", "\\0", "\\\\", r"\n",
              r"x\g<0>y", r"\g<9>", r"\9", "\\", r"\g<", r"\g<0"]
_REPLS_BYTES = [b"-", b"", rb"[\g<0>]", rb"\g<1>", rb"\1", b"\\\\", b"\\"]

_FAMILIES = ((_PATTERNS_ASCII, _SUBJECTS_ASCII, _REPLS_STR),
             (_PATTERNS_UNICODE, _SUBJECTS_UNICODE, _REPLS_STR),
             (_PATTERNS_BYTES, _SUBJECTS_BYTES, _REPLS_BYTES))

#: 1 means every case, which is the default because the whole product costs about three seconds:
#: a cap nobody has to pay for is a cap nobody should defend. A stride above 1 sampling one residue
#: class of an inner dimension's period would erase a whole axis while the case count still looked
#: healthy, so `test_stride_touches_every_axis` enumerates what the stride in force actually reaches
#: -- it has to hold for a thinned run too, not only for this default.
_STRIDE = int(os.environ.get("REAL_TRANSFORM_STRIDE", "1"))

_MAX_REPORTED = 8


def _cases():
    """The full deterministic cross-product, in a fixed order. No RNG: a divergence found here is
    reproducible by construction rather than by recording a seed."""
    for patterns, subjects, repls in _FAMILIES:
        is_bytes = isinstance(patterns[0], bytes)
        template = rb"[\g<0>]" if is_bytes else r"[\g<0>]"
        for real_flags, re_flags in _FLAGSETS:
            for pattern in patterns:
                for subject in subjects:
                    yield ("findall", pattern, subject, None, real_flags, re_flags)
                    yield ("accessors", pattern, subject, None, real_flags, re_flags)
                    for maxsplit in _MAXSPLITS:
                        yield ("split", pattern, subject, maxsplit, real_flags, re_flags)
                    for repl in repls:
                        for count in _COUNTS:
                            yield ("sub", pattern, subject, (repl, count), real_flags, re_flags)
                        yield ("subn", pattern, subject, (repl, None), real_flags, re_flags)
                    yield ("sub_callable", pattern, subject, None, real_flags, re_flags)
                    yield ("expand", pattern, subject, template, real_flags, re_flags)


def _normalise(value):
    """Compare types alongside values: findall's contract is a shape, and a `str` where `bytes`
    belongs is a divergence a value-only comparison reads as agreement."""
    if isinstance(value, list):
        return [_normalise(item) for item in value]
    if isinstance(value, tuple):
        return ("tuple",) + tuple(_normalise(item) for item in value)
    if isinstance(value, dict):
        return ("dict",) + tuple(sorted((k, _normalise(v)) for k, v in value.items()))
    return (type(value).__name__, value)


def _exception_token(module, exc):
    """What the CALLER can branch on. `real.error` and `re.error` are distinct classes by
    construction, so both collapse to one token; every other type is compared by name, because an
    `except re.error` does not catch a TypeError."""
    error = getattr(module, "error", None)
    if error is not None and isinstance(exc, error):
        return "pattern-or-template-error"
    return type(exc).__name__


def _invoke(module, kind, pattern, subject, extra, flags):
    compiled = module.compile(pattern, flags)
    if kind == "findall":
        return compiled.findall(subject)
    if kind == "split":
        return compiled.split(subject) if extra is None else compiled.split(subject, extra)
    if kind in ("sub", "subn"):
        repl, count = extra
        method = compiled.sub if kind == "sub" else compiled.subn
        return method(repl, subject) if count is None else method(repl, subject, count)
    if kind == "sub_callable":
        replacement = b"<>" if isinstance(subject, bytes) else "<>"
        return compiled.sub(lambda _m: replacement, subject)
    if kind == "expand":
        return [m.expand(extra) for m in compiled.finditer(subject)]
    if kind == "accessors":
        return [(m.span(), m.groups(), m.groupdict(), m.lastindex, m.lastgroup, m.regs)
                for m in compiled.finditer(subject)]
    raise AssertionError("unknown surface " + kind)


def _answer(module, kind, pattern, subject, extra, flags):
    try:
        return _invoke(module, kind, pattern, subject, extra, flags), None
    except Exception as exc:                                    # noqa: BLE001 -- parity, not repair
        return None, _exception_token(module, exc)


class TestTransformationSurfacesMatchRe(unittest.TestCase):
    """One oracle for every transformation surface, values and refusals alike."""

    def test_transformation_surfaces_match_re(self):
        total = ran = agreed_refusals = 0
        divergences = []
        for index, case in enumerate(_cases()):
            total += 1
            if index % _STRIDE:
                continue
            kind, pattern, subject, extra, real_flags, re_flags = case
            want, want_exc = _answer(re, kind, pattern, subject, extra, re_flags)
            got, got_exc = _answer(real, kind, pattern, subject, extra, real_flags)
            ran += 1
            if want_exc is not None or got_exc is not None:
                if want_exc == got_exc:
                    agreed_refusals += 1
                    continue
                detail = "re raises %s, real raises %s" % (want_exc, got_exc)
            elif _normalise(got) != _normalise(want):
                detail = "re %r, real %r" % (want, got)
            else:
                continue
            divergences.append("%s(pattern=%r, subject=%r, extra=%r, flags=%d): %s"
                               % (kind, pattern, subject, extra, real_flags, detail))

        self.assertGreater(ran, 0, "the stride sampled nothing")
        print("\ntransform sweep: %d/%d cases compared (stride %d, %d dropped), "
              "%d refused identically"
              % (ran, total, _STRIDE, total - ran, agreed_refusals))
        if divergences:
            shown = "\n  ".join(divergences[:_MAX_REPORTED])
            more = ("\n  ... %d more" % (len(divergences) - _MAX_REPORTED)
                    if len(divergences) > _MAX_REPORTED else "")
            self.fail("%d transformation divergence(s) against re:\n  %s%s"
                      % (len(divergences), shown, more))

    def test_stride_touches_every_axis(self):
        """A stride that shares a factor with an inner dimension's period samples one residue class
        of it forever. The case count stays healthy while an entire axis goes unswept, so the guard
        cannot be an argument about 157 being prime -- it has to enumerate what the stride in force
        actually reaches."""
        seen = {"kind": set(), "pattern": set(), "subject": set(),
                "extra": set(), "flags": set()}
        for index, case in enumerate(_cases()):
            if index % _STRIDE:
                continue
            kind, pattern, subject, extra, real_flags, _ = case
            seen["kind"].add(kind)
            seen["pattern"].add(pattern)
            seen["subject"].add(subject)
            seen["extra"].add(repr(extra))
            seen["flags"].add(real_flags)

        expected_patterns = {p for patterns, _, _ in _FAMILIES for p in patterns}
        expected_subjects = {s for _, subjects, _ in _FAMILIES for s in subjects}
        expected_flags = {f for f, _ in _FLAGSETS}
        expected_kinds = {"findall", "accessors", "split", "sub", "subn",
                          "sub_callable", "expand"}

        self.assertEqual(seen["kind"], expected_kinds,
                         "stride %d never reaches these surfaces: %s"
                         % (_STRIDE, sorted(expected_kinds - seen["kind"])))
        self.assertEqual(seen["pattern"], expected_patterns,
                         "stride %d never reaches these patterns: %s"
                         % (_STRIDE, sorted(map(repr, expected_patterns - seen["pattern"]))))
        self.assertEqual(seen["subject"], expected_subjects,
                         "stride %d never reaches these subjects: %s"
                         % (_STRIDE, sorted(map(repr, expected_subjects - seen["subject"]))))
        self.assertEqual(seen["flags"], expected_flags,
                         "stride %d never reaches these flag sets: %s"
                         % (_STRIDE, sorted(expected_flags - seen["flags"])))

        # Every maxsplit, every count and every template must be drawn too -- those are the
        # innermost dimensions, so they are the ones a badly chosen stride aliases first.
        for maxsplit in _MAXSPLITS:
            self.assertIn(repr(maxsplit), seen["extra"],
                          "stride %d never draws maxsplit=%r" % (_STRIDE, maxsplit))
        drawn_repls = {extra for extra in seen["extra"] if extra.startswith("(")}
        for repls in (_REPLS_STR, _REPLS_BYTES):
            for repl in repls:
                self.assertTrue(any(repr(repl) in extra for extra in drawn_repls),
                                "stride %d never draws repl=%r" % (_STRIDE, repl))
        for count in _COUNTS:
            self.assertTrue(any(extra.endswith(", %r)" % (count,)) for extra in drawn_repls),
                            "stride %d never draws count=%r" % (_STRIDE, count))


if __name__ == "__main__":
    unittest.main()
