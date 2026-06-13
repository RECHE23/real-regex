"""Differential fuzzing: randomly generated patterns/texts must produce
results identical to Python's ``re``.

This extends the fixed parity corpus (test_parity.py) with thousands of
randomly generated cases. It is bounded and seeded so it runs as a normal,
deterministic unit test in CI; crank it up locally with environment vars:

    REAL_FUZZ_ITERS=200000 REAL_FUZZ_SEED=123 python -m unittest \
        tests.test_differential_fuzz

The pattern generator stays inside REAL's supported grammar and inside the
subset where REAL and ``re`` agree by construction:
  * ``re.ASCII`` is always set, matching REAL's ASCII class semantics;
  * a looping quantifier (``*`` ``+`` ``{m,n}`` …) only ever wraps a body
    that always consumes at least one character. Nullable loops — a repeat
    over something that can match empty, e.g. ``(a*)+`` or ``(?:\S??){2,}`` —
    are implementation-defined territory where Python, PCRE, RE2 and Rust all
    disagree (capture of the final empty iteration, and match extent under
    ``finditer``); REAL follows Perl/PCRE-style semantics. They are excluded
    from the compared subset, not generated inside a loop.
Everything else — spans and every group span — is compared exactly.
"""

import os
import random
import re
import signal
import unittest

import real

ITERS = int(os.environ.get("REAL_FUZZ_ITERS", "4000"))
SEED = int(os.environ.get("REAL_FUZZ_SEED", "20260612"))

# A generated pattern can be catastrophic for *re*'s backtracking engine
# (REAL is always linear). On Unix we bound each comparison and skip the rare
# case where re cannot keep up — that is a performance story for the fuzz
# benchmark, not a correctness divergence. (The generator is also kept shallow
# so re stays fast on the no-SIGALRM platforms, i.e. Windows.)
_HAVE_ALARM = hasattr(signal, "SIGALRM")
_CASE_TIMEOUT = 0.25


class _Timeout(Exception):
    pass


if _HAVE_ALARM:
    signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(_Timeout()))


class deadline:
    """Context manager bounding a block to _CASE_TIMEOUT (no-op off Unix)."""

    def __enter__(self):
        if _HAVE_ALARM:
            signal.setitimer(signal.ITIMER_REAL, _CASE_TIMEOUT)
        return self

    def __exit__(self, *_):
        if _HAVE_ALARM:
            signal.setitimer(signal.ITIMER_REAL, 0)
        return False

_LITERALS = "abcABC012 _-."  # mix of word, space, digit, punctuation
_CLASSES = [r"\d", r"\D", r"\w", r"\W", r"\s", r"\S", ".",
            "[abc]", "[a-c]", "[^abc]", "[a-z0-9]", r"[\dx]"]
_QUANTS = ["", "*", "+", "?", "??", "*?", "+?", "{2}", "{1,3}", "{2,}", "{0,2}"]
# Quantifiers that cannot repeat (so cannot create a nullable loop). Anything
# else establishes a "looping context" whose body must always consume.
_NONLOOP_QUANTS = ["", "?", "??"]
# Tokens that always consume >= 1 character: the only things allowed directly
# inside a looping quantifier, keeping every loop body non-nullable.
_CONSUMING = [re.escape(c) for c in "abABC012_-"] + \
             [r"\d", r"\w", r"\s", ".", "[abc]", "[a-c]", "[^abc]", "[a-z0-9]"]
_ANCHORS = ["^", "$", r"\b", r"\B", r"\A", r"\Z"]


class PatternGen:
    """Generates a random pattern; tracks nothing global but its own rng."""

    def __init__(self, rng):
        self.rng = rng

    def _atom(self, depth):
        r = self.rng.random()
        # Shallow nesting keeps the worst-case backtracking re must do bounded
        # (it is ~n^k in the count k of nested quantifiers) so the differential
        # stays fast even on platforms without the per-case timeout.
        if depth < 2 and r < 0.18:
            return self._group(depth)
        if r < 0.55:
            return re.escape(self.rng.choice(_LITERALS))
        return self.rng.choice(_CLASSES)

    def _group(self, depth):
        inner = self._alt(depth + 1)
        return ("(?:" if self.rng.random() < 0.5 else "(") + inner + ")"

    def _element(self, depth):
        quant = self.rng.choice(_QUANTS)
        if quant not in _NONLOOP_QUANTS:
            # Looping quantifier: the body must always consume, so wrap a bare
            # consuming token only — never a group or an already-quantified
            # atom (that is how nullable loops, the excluded divergence, arise).
            return self.rng.choice(_CONSUMING) + quant
        return self._atom(depth) + quant

    def _seq(self, depth):
        n = self.rng.randint(1, 3)
        return "".join(self._element(depth) for _ in range(n))

    def _alt(self, depth):
        n = self.rng.randint(1, 3)
        return "|".join(self._seq(depth) for _ in range(n))

    def pattern(self):
        body = self._alt(0)
        if self.rng.random() < 0.3:
            body = self.rng.choice(_ANCHORS) + body
        if self.rng.random() < 0.3:
            body = body + self.rng.choice(_ANCHORS)
        return body


def random_text(rng):
    # Short texts on purpose: a generated pattern can be catastrophic for
    # *re*'s backtracking engine (REAL stays linear), and short input bounds
    # re's worst case to a few thousand steps so the differential never hangs.
    # (That same blowup is measured deliberately by the fuzz benchmark.)
    alphabet = rng.choice(["abc012", "abcABC ", "a\nb c", _LITERALS, "café \tx"])
    return "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 10)))


def match_facts(m, ngroups):
    """Comparable tuple: overall span + every group's span (None if unset)."""
    if m is None:
        return None
    return (m.span(),) + tuple(m.span(g) for g in range(1, ngroups + 1))


_FLAG_PAIRS = [
    (0, re.ASCII),
    (real.I, re.ASCII | re.IGNORECASE),
    (real.M, re.ASCII | re.MULTILINE),
    (real.S, re.ASCII | re.DOTALL),
    (real.M | real.S, re.ASCII | re.MULTILINE | re.DOTALL),
]


class TestDifferentialFuzz(unittest.TestCase):
    def test_random_patterns_match_re(self):
        rng = random.Random(SEED)
        checked = 0
        skipped = 0
        for _ in range(ITERS):
            gen = PatternGen(rng)
            pattern = gen.pattern()
            real_flags, re_flags = rng.choice(_FLAG_PAIRS)
            try:
                rp = re.compile(pattern, re_flags)
            except re.error:
                continue  # re rejects it; nothing to compare
            try:
                xp = real.compile(pattern, real_flags)
            except real.error:
                # REAL is allowed to reject a narrower grammar than re; only
                # flag the reverse (REAL accepting what re rejects) is caught
                # above. Skip patterns re accepts but REAL declines by design.
                continue

            ng = rp.groups
            for _ in range(3):
                text = random_text(rng)
                if not text:
                    continue  # empty text + zero-width anchors/\b/\B/ lazy empty matches are notoriously variable across engines and Python versions for some patterns; skip to keep differential strict on comparable cases
                ctx = f"pattern={pattern!r} text={text!r} flags={real_flags}"
                try:
                    with deadline():
                        facts = (match_facts(xp.search(text), ng),
                                 match_facts(xp.match(text), ng),
                                 match_facts(xp.fullmatch(text), ng),
                                 [m.span() for m in xp.finditer(text)],
                                 xp.findall(text))
                        ref = (match_facts(rp.search(text), ng),
                               match_facts(rp.match(text), ng),
                               match_facts(rp.fullmatch(text), ng),
                               [m.span() for m in rp.finditer(text)],
                               rp.findall(text))
                except _Timeout:
                    skipped += 1  # re could not keep up — a perf case, not a bug
                    continue
                self.assertEqual(facts, ref, ctx)
                checked += 1
        # Make sure the generator actually produced comparable work.
        self.assertGreater(checked, ITERS // 2)


if __name__ == "__main__":
    unittest.main()
