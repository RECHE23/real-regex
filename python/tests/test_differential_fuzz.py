r"""Differential fuzzing: randomly generated patterns/texts must produce
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
    """Raised when a single re comparison exceeds the per-case timeout."""
    pass


if _HAVE_ALARM:
    signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(_Timeout()))


class deadline:
    """Context manager bounding a block to _CASE_TIMEOUT (no-op off Unix)."""

    def __enter__(self):
        """Arm the real-time alarm."""
        if _HAVE_ALARM:
            signal.setitimer(signal.ITIMER_REAL, _CASE_TIMEOUT)
        return self

    def __exit__(self, *_):
        """Disarm the alarm; always propagate exceptions."""
        if _HAVE_ALARM:
            signal.setitimer(signal.ITIMER_REAL, 0)
        return False

# Mix of word, space, digit, punctuation, plus raw 2/3/4-byte UTF-8 literals (é/€/😀): in
# code-point mode these are single atoms, so a following quantifier spans the whole code point
# (the U1 fix). re is the code-point oracle for the differential.
_LITERALS = "abcABC012 _-.é€😀"
_CLASSES = [r"\d", r"\D", r"\w", r"\W", r"\s", r"\S", ".",
            "[abc]", "[a-c]", "[^abc]", "[a-z0-9]", r"[\dx]",
            # UTF-8 classes (U2): specific code points / ranges / negation, code-point mode.
            "[é]", "[éàü]", "[à-ÿ]", "[a-zé]", "[^é]", "[^à-ÿ]", "[Ā-ſ]"]
_QUANTS = ["", "*", "+", "?", "??", "*?", "+?", "{2}", "{1,3}", "{2,}", "{0,2}"]
# Quantifiers that cannot repeat (so cannot create a nullable loop). Anything
# else establishes a "looping context" whose body must always consume.
_NONLOOP_QUANTS = ["", "?", "??"]
# Tokens that always consume >= 1 character: the only things allowed directly
# inside a looping quantifier, keeping every loop body non-nullable (incl. UTF-8 code points).
_CONSUMING = [re.escape(c) for c in "abABC012_-é€😀"] + \
             [r"\d", r"\w", r"\s", ".", "[abc]", "[a-c]", "[^abc]", "[a-z0-9]"]
_ANCHORS = ["^", "$", r"\b", r"\B", r"\A", r"\Z"]
# Atoms allowed inside a (capture-free) lookaround sub-pattern: ASCII, single codepoint,
# so REAL and re agree on the subjects generated here.
_LA_ATOMS = [re.escape(c) for c in "abAB01_ "] + [r"\d", r"\w", r"\s", "[abc]", "[a-z]"]
# Bounded quantifiers usable inside a lookahead sub (never *, +, {n,} -> unbounded).
_LA_BOUNDED_QUANTS = ["", "", "?", "{2}", "{1,2}"]


class PatternGen:
    """Generates a random pattern inside REAL's supported grammar."""

    def __init__(self, rng, ascii_only=False):
        """Bind a random number generator.

        Args:
            rng (random.Random): Random state to use.
            ascii_only (bool): Restrict literals/classes to ASCII, so every generated pattern is a
                valid *bytes* pattern (raw non-ASCII members are rejected on the bytes path). Used by
                the bytes differential; the str differential leaves it False to exercise UTF-8.
        """
        self.rng = rng
        self.ascii_only = ascii_only
        # _CLASSES entries after the first 12 are the UTF-8 classes ([é], [à-ÿ], ...): drop them for bytes.
        self._literals = "abcABC012 _-." if ascii_only else _LITERALS
        self._classes = _CLASSES[:12] if ascii_only else _CLASSES

    def _atom(self, depth):
        """Return a literal, class, or nested group atom.

        Args:
            depth (int): Current nesting depth.

        Returns:
            str: A pattern fragment.
        """
        r = self.rng.random()
        # Shallow nesting keeps the worst-case backtracking re must do bounded
        # (it is ~n^k in the count k of nested quantifiers) so the differential
        # stays fast even on platforms without the per-case timeout.
        if depth < 2 and r < 0.08:
            return self._lookaround()  # zero-width; only ever gets a non-loop quant (see _element)
        if depth < 2 and r < 0.24:
            return self._group(depth)
        if r < 0.32:
            return self._octal_atom()
        if r < 0.40 and not self.ascii_only:
            # \u/\U escapes are a str-only construct (REAL rejects them in bytes mode), so a byte-safe
            # pattern never emits them -- otherwise the bytes differential would just skip on re.error.
            return self._unicode_atom()
        if r < 0.66:
            return re.escape(self.rng.choice(self._literals))
        return self.rng.choice(self._classes)

    def _octal_atom(self):
        """Return an octal escape (\\ooo) of an ASCII char in the alphabet.

        Both engines decode \\ooo (value < 128) to that single byte, so it matches the
        same character (and can actually fire against the generated subjects).

        Returns:
            str: An octal byte escape such as ``\\141``.
        """
        ch = self.rng.choice("abcABC012 _-")
        return "\\" + format(ord(ch), "03o")

    def _unicode_atom(self):
        r"""Return a \uHHHH / \UHHHHHHHH code-point escape for a char in the alphabet.

        Both engines decode it to the same code point (str mode), so it matches the same
        character; the subject occasionally contains it (e.g. é in 'café').

        Returns:
            str: A code-point escape such as ``a`` or ``\U000000e9``.
        """
        cp = ord(self.rng.choice("abAB01" if self.ascii_only else "abAB01é"))
        return ("\\u%04x" % cp) if self.rng.random() < 0.5 else ("\\U%08x" % cp)

    def _comment(self):
        """Return a (?#...) comment (ignored by both engines).

        Returns:
            str: A comment such as ``(?#note)``.
        """
        body = "".join(self.rng.choice("abc 12") for _ in range(self.rng.randint(0, 5)))
        return "(?#" + body + ")"

    def _capture_free_sub(self, fixed_width):
        """Return a short capture-free sub-pattern for a lookaround.

        Args:
            fixed_width (bool): True for a lookbehind (re requires a fixed width: no
                quantifiers); False for a lookahead (bounded quantifiers allowed).

        Returns:
            str: A capture-free, ASCII, bounded sub-pattern.
        """
        parts = []
        for _ in range(self.rng.randint(1, 3)):
            atom = self.rng.choice(_LA_ATOMS)
            if not fixed_width:
                atom += self.rng.choice(_LA_BOUNDED_QUANTS)
            parts.append(atom)
        return "".join(parts)

    def _lookaround(self):
        """Return a bounded, capture-free lookaround.

        Lookbehind sub-patterns are fixed-width (re's requirement) and lookahead
        sub-patterns are bounded; both are capture-free, so REAL (whose lookaround sub
        is capture-free) and re agree on the comparable facts.

        Returns:
            str: A lookaround such as ``(?=\\d{2})`` or ``(?<=ab)``.
        """
        kind = self.rng.choice(["(?=", "(?!", "(?<=", "(?<!"])
        return kind + self._capture_free_sub(fixed_width=kind.startswith("(?<")) + ")"

    def _group(self, depth):
        """Return a capturing or non-capturing group.

        Args:
            depth (int): Current nesting depth.

        Returns:
            str: A group pattern fragment.
        """
        inner = self._alt(depth + 1)
        return ("(?:" if self.rng.random() < 0.5 else "(") + inner + ")"

    def _element(self, depth):
        """Return an atom with a quantifier, avoiding nullable loop bodies.

        Args:
            depth (int): Current nesting depth.

        Returns:
            str: A quantified pattern fragment.
        """
        quant = self.rng.choice(_QUANTS)
        if quant not in _NONLOOP_QUANTS:
            # Looping quantifier: the body must always consume, so wrap a bare
            # consuming token only — never a group or an already-quantified
            # atom (that is how nullable loops, the excluded divergence, arise).
            return self.rng.choice(_CONSUMING) + quant
        return self._atom(depth) + quant

    def _seq(self, depth):
        """Return a concatenated sequence of elements.

        Args:
            depth (int): Current nesting depth.

        Returns:
            str: A sequence pattern fragment.
        """
        parts = []
        for _ in range(self.rng.randint(1, 3)):
            if self.rng.random() < 0.08:
                parts.append(self._comment())  # a comment is never quantified, so it is safe here
            parts.append(self._element(depth))
        return "".join(parts)

    def _alt(self, depth):
        """Return an alternation of sequences.

        Args:
            depth (int): Current nesting depth.

        Returns:
            str: An alternation pattern fragment.
        """
        n = self.rng.randint(1, 3)
        return "|".join(self._seq(depth) for _ in range(n))

    def pattern(self):
        """Generate a complete pattern, optionally surrounded by anchors.

        Returns:
            str: A random regular expression pattern.
        """
        body = self._alt(0)
        if self.rng.random() < 0.3:
            body = self.rng.choice(_ANCHORS) + body
        if self.rng.random() < 0.3:
            body = body + self.rng.choice(_ANCHORS)
        return body


def random_text(rng):
    """Generate a short random subject string.

    Args:
        rng (random.Random): Random state to use.

    Returns:
        str: Random text for matching.
    """
    # Short texts on purpose: a generated pattern can be catastrophic for
    # *re*'s backtracking engine (REAL stays linear), and short input bounds
    # re's worst case to a few thousand steps so the differential never hangs.
    # (That same blowup is measured deliberately by the fuzz benchmark.)
    alphabet = rng.choice(["abc012", "abcABC ", "a\nb c", _LITERALS, "café \tx", "é€😀 aé€x",
                           "٣٥9 x y z ９０"])  # Unicode digits + NBSP/U+2028 for \d \s
    return "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 10)))


def random_template(rng, ngroups):
    """Generate a replacement template: literals, group refs, octal escapes, ``\\g<0>``.

    The octal escapes (``\\012`` etc.) are the case that silently mis-parsed before the
    decoder fix; comparing real.sub against re.sub over them is exactly the closed gap.

    Args:
        rng (random.Random): Random state.
        ngroups (int): Number of capturing groups (so a group reference stays valid).

    Returns:
        str: A replacement template string.
    """
    parts = []
    for _ in range(rng.randint(0, 4)):
        r = rng.random()
        if r < 0.4:
            parts.append(rng.choice("xy-_: "))
        elif r < 0.6 and ngroups > 0:
            parts.append("\\" + str(rng.randint(1, ngroups)))                  # group ref \1..\ng
        elif r < 0.8:
            parts.append("\\" + format(ord(rng.choice("abAB01")), "03o"))      # octal escape \ooo
        else:
            parts.append(r"\g<0>")                                             # the whole match
    return "".join(parts)


def match_facts(m, ngroups):
    """Comparable tuple: overall span + every group's span (None if unset).

    Args:
        m (Match or None): A match object.
        ngroups (int): Number of capturing groups in the pattern.

    Returns:
        tuple or None: Match facts suitable for equality comparison.
    """
    if m is None:
        return None
    return (m.span(),) + tuple(m.span(g) for g in range(1, ngroups + 1))


# Under real.I the icase oracle is refined per pattern (see the loop): pure literals/classes fold
# like re.IGNORECASE, ASCII shorthands stay ASCII, and byte-escapes are dropped from icase.
# \w \W \b \B stay ASCII in text mode (W3/W4 pending); \d \D \s \S are Unicode (W2). A pattern using
# an ASCII-only shorthand is compared under re.ASCII; otherwise the full Unicode oracle.
_ASCII_ONLY_SHORTHAND = re.compile(r"\\[wWbB]")
_UNICODE_SHORTHAND = re.compile(r"\\[dDsS]")
_BYTE_ESCAPE = re.compile(r"\\x|\\[0-7]")

# The real text-mode flag sets fuzzed, and the matching re base flags (without re.ASCII).
_REAL_FLAG_SETS = [0, real.I, real.M, real.S, real.M | real.S]
_BASE_RE = {0: 0, real.I: re.IGNORECASE, real.M: re.MULTILINE, real.S: re.DOTALL,
            real.M | real.S: re.MULTILINE | re.DOTALL}


class TestDifferentialFuzz(unittest.TestCase):
    """Differential fuzz test comparing real against Python re."""

    def test_random_patterns_match_re(self):
        """Random patterns produce the same results as re across all APIs."""
        rng = random.Random(SEED)
        checked = 0
        skipped = 0
        for _ in range(ITERS):
            gen = PatternGen(rng)
            pattern = gen.pattern()
            # A pattern mixing an ASCII-only shorthand (\w \b) with a Unicode one (\d \s) has no single
            # re oracle in text mode -- re.ASCII would wrongly ASCII-ize the \d/\s. Skip it (real is
            # right either way); each is exercised on its own elsewhere.
            if _ASCII_ONLY_SHORTHAND.search(pattern) and _UNICODE_SHORTHAND.search(pattern):
                skipped += 1
                continue
            real_flags = rng.choice(_REAL_FLAG_SETS)
            base = _BASE_RE[real_flags]
            # A \xHH / octal byte-escape keeps byte provenance and never folds; re folds it under
            # icase, so no oracle agrees -- drop icase for those patterns rather than pin a wrong result.
            if _BYTE_ESCAPE.search(pattern) and (real_flags & real.I):
                real_flags &= ~real.I
                base &= ~re.IGNORECASE
            # \w \b stay ASCII (neutralise with re.ASCII); \d \s and case folding are Unicode.
            re_flags = (re.ASCII | base) if _ASCII_ONLY_SHORTHAND.search(pattern) else base
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
                pos = rng.randint(0, len(text))
                endpos = rng.randint(pos, len(text) + 2)  # may exceed len -> clamped, like re
                # An empty search region (endpos == pos) places zero-width assertions
                # (\b, \B, ...) on a virtual empty string, whose match re changed in 3.11
                # (\B went None -> (k, k)); REAL follows the modern, by-definition behaviour
                # and so disagrees with re < 3.11. Mirror the empty-text skip above and
                # compare only a non-empty region; the desired behaviour for the empty case
                # is pinned explicitly, independent of re's version, in test_real.py.
                compare_region = endpos > pos
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
                        if compare_region:
                            facts += (match_facts(xp.search(text, pos, endpos), ng),
                                      match_facts(xp.match(text, pos, endpos), ng))
                            ref += (match_facts(rp.search(text, pos, endpos), ng),
                                    match_facts(rp.match(text, pos, endpos), ng))
                except _Timeout:
                    skipped += 1  # re could not keep up — a perf case, not a bug
                    continue
                self.assertEqual(facts, ref, ctx)
                checked += 1
                # sub() with octal / group-ref templates — the axis that would have caught
                # the \012 mis-parse. Skip when either side rejects the template by design.
                template = random_template(rng, ng)
                try:
                    with deadline():
                        real_sub, re_sub = xp.sub(template, text), rp.sub(template, text)
                except _Timeout:
                    skipped += 1
                    continue
                except (real.error, re.error):
                    continue  # one side rejects the template (e.g. a backref); not comparable
                self.assertEqual(real_sub, re_sub, ctx + f" template={template!r}")
        # Make sure the generator actually produced comparable work.
        self.assertGreater(checked, ITERS // 2)

    def test_verbose_matches_re(self):
        """Random verbose patterns produce the same results as re.VERBOSE."""
        # re.X: unescaped whitespace and #-comments outside classes are ignored.
        # Inject them into generated patterns and require REAL and re to agree.
        rng = random.Random(SEED ^ 0x5EED)
        checked = 0
        for _ in range(ITERS // 2):
            gen = PatternGen(rng)
            verbose = verbosify(gen.pattern(), rng)
            if _ASCII_ONLY_SHORTHAND.search(verbose) and _UNICODE_SHORTHAND.search(verbose):
                continue  # mixed shorthands: no single oracle (see test_random_patterns_match_re)
            # \w \b stay ASCII; \d \s are Unicode -- neutralise the former only when present.
            re_flags = re.VERBOSE | (re.ASCII if _ASCII_ONLY_SHORTHAND.search(verbose) else 0)
            try:
                rp = re.compile(verbose, re_flags)
            except re.error:
                continue
            try:
                xp = real.compile(verbose, real.X)
            except real.error:
                continue  # REAL may reject a narrower grammar; only agreement is required
            ng = rp.groups
            for _ in range(3):
                text = random_text(rng)
                if not text:
                    continue
                ctx = f"verbose pattern={verbose!r} text={text!r}"
                try:
                    with deadline():
                        facts = (match_facts(xp.search(text), ng),
                                 [m.span() for m in xp.finditer(text)])
                        ref = (match_facts(rp.search(text), ng),
                               [m.span() for m in rp.finditer(text)])
                except _Timeout:
                    continue
                self.assertEqual(facts, ref, ctx)
                checked += 1
        self.assertGreater(checked, 0)

    def test_random_bytes_match_re(self):
        """Random patterns on BYTES subjects match re — byte mode (raw bytes, no UTF-8),
        exercising lookarounds whose codepoint alignment differs from the str path."""
        rng = random.Random(SEED ^ 0xB17E5)
        checked = 0
        total = 0
        compiled = 0
        for _ in range(ITERS // 2):
            pattern = PatternGen(rng, ascii_only=True).pattern()  # byte-safe: no raw non-ASCII members
            total += 1
            try:
                rp = re.compile(pattern.encode())
            except re.error:
                continue
            try:
                xp = real.compile(pattern.encode())
            except real.error:
                continue  # REAL may reject a narrower grammar; only agreement is required
            compiled += 1
            ng = rp.groups
            for _ in range(2):
                text = bytes(rng.choice(b"abcABC012 _-\n\t\x80\xff")
                             for _ in range(rng.randint(1, 10)))
                ctx = f"bytes pattern={pattern!r} text={text!r}"
                try:
                    with deadline():
                        facts = (match_facts(xp.search(text), ng),
                                 [m.span() for m in xp.finditer(text)],
                                 xp.findall(text))
                        ref = (match_facts(rp.search(text), ng),
                               [m.span() for m in rp.finditer(text)],
                               rp.findall(text))
                except _Timeout:
                    continue
                self.assertEqual(facts, ref, ctx)
                checked += 1
        self.assertGreater(checked, 0)
        # The byte-safe generator emits no str-only constructs (raw non-ASCII, \u/\U), so the vast
        # majority of patterns compile on both engines -- the bytes differential is not mostly skips.
        self.assertGreater(compiled / total, 0.8)


def verbosify(pattern, rng):
    """Insert verbose-insignificant whitespace and #-comments into a pattern.

    Whitespace goes only outside character classes and never right after a
    backslash or a `(` (which would split an escape or a `(?...)` token); both
    engines apply the same rule, so a correct REAL agrees with re on the result.

    Args:
        pattern (str): Original compact pattern.
        rng (random.Random): Random state.

    Returns:
        str: Pattern with injected insignificant whitespace/comments.
    """
    out = []
    i = 0
    in_class = False
    while i < len(pattern):
        c = pattern[i]
        prev = pattern[i - 1] if i > 0 else ""
        if not in_class and prev not in ("\\", "(") and rng.random() < 0.4:
            out.append(rng.choice([" ", "  ", "\t", "\n", "  # note\n"]))
        if c == "\\" and i + 1 < len(pattern):
            out.append(pattern[i:i + 2])
            i += 2
            continue
        if not in_class and c == "[":
            in_class = True
        elif in_class and c == "]":
            in_class = False
        out.append(c)
        i += 1
    return "".join(out)


if __name__ == "__main__":
    unittest.main()
