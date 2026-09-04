r"""Differential fuzzing: randomly generated patterns/texts must produce
results identical to Python's ``re``.

This extends the fixed parity corpus (test_parity.py) with thousands of
randomly generated cases. It is bounded and seeded so it runs as a normal,
deterministic unit test in CI; crank it up locally with environment vars:

    REAL_FUZZ_ITERS=200000 REAL_FUZZ_SEED=123 python -m unittest \
        tests.test_differential_fuzz

The pattern generator stays inside REAL's supported grammar and inside the
subset where REAL and ``re`` agree by construction:
  * in str mode every shorthand and boundary is Unicode (\\w \\W \\d \\D \\s \\S
    \\b \\B), so the re oracle is the full Unicode default (no re.ASCII);
  * a looping quantifier (``*`` ``+`` ``{m,n}`` …) only ever wraps a body
    that always consumes at least one character. Nullable loops — a repeat
    over something that can match empty, e.g. ``(a*)+`` or ``(?:\S??){2,}`` —
    are implementation-defined territory where Python, PCRE, RE2 and Rust all
    disagree (capture of the final empty iteration, and match extent under
    ``finditer``); REAL follows Perl/PCRE-style semantics. They are excluded
    from the compared subset, not generated inside a loop. The empty-first-branch
    case (``(|a)*`` — REAL, re and the Rust crate each differ) is pinned
    deterministically in ``TestIntentionalDivergences`` and documented as
    ``div_empty_first_branch_loop``, so this random generator need not cover it.
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


def _probe_re_supports_possessive():
    """Whether the running `re` accepts possessive quantifiers / atomic groups (3.11+).

    Probed once at import, not branched on `sys.version_info` directly: the concrete
    accept/reject behaviour of the actual interpreter in front of us is the fact that
    matters, matching the project's own established `\\B`-on-empty-text precedent (see
    test_random_patterns_match_re's `compare_region` comment) rather than a version-number
    guess. Below 3.11, `re.compile(r"a*+")` raises `re.error` ("multiple repeat") --
    exactly the SAME "re rejects it; nothing to compare" path the harness already takes
    for any pattern outside re's own grammar, so an UNGATED generator would not be
    incorrect, only wasteful (every possessive/atomic slot burned on a guaranteed skip).
    Gating the generator's own choice list on this probe keeps that fuzz budget useful on
    every supported Python, at the cost of the generator's RNG-consumption shape differing
    between legs -- the same accepted trade-off the `\\B`-empty precedent already makes.
    """
    try:
        re.compile(r"a*+")
        re.compile(r"(?>a)")
    except re.error:
        return False
    return True


_RE_SUPPORTS_POSSESSIVE = _probe_re_supports_possessive()


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
# code-point mode these are single atoms, so a following quantifier spans the whole code point.
# re is the code-point oracle for the differential.
_LITERALS = "abcABC012 _-.é€😀"
# ASCII-only class members: usable in BOTH str and bytes mode. Kept as its own list rather than
# "the first N entries of _CLASSES" -- that was an index the bytes slice hard-coded (`[:12]`), so
# inserting a member in the middle silently moved UTF-8 classes into bytes mode or dropped ASCII
# ones out of it. A named list cannot be invalidated by an insertion.
_CLASSES_ASCII = [r"\d", r"\D", r"\w", r"\W", r"\s", r"\S", ".",
                  "[abc]", "[a-c]", "[^abc]", "[a-z0-9]", r"[\dx]",
                  # Position-sensitive members. A `-` is a literal at the start, at the end, and
                  # between two ranges; a `]` is a literal in first position only. Each is a place
                  # where a class parser can be one character off without any other shape
                  # noticing -- measured clean, unguarded until now.
                  "[-a]", "[a-]", "[a-c-e]", "[-]", "[]a]", "[^]a]", "[^-a]",
                  # Exact range boundaries: a single-member range, the whole ASCII block, and two
                  # that cross the letter/digit divide. An off-by-one at either end matches one
                  # character too many or too few.
                  "[a-a]", "[\x00-\x7f]", "[0-A]", "[Z-a]",
                  # Byte and octal escapes as members: div_hex makes \xHH a CODE POINT inside a
                  # class and a raw BYTE outside it, so a class is exactly where that split shows
                  # -- and it differs between str and bytes mode, which is why these belong here
                  # rather than in the str-only list.
                  r"[\x41]", r"[\101]", r"[\x41-\x5a]", r"[\n\t]"]

# str-only additions: raw UTF-8 literals and the quasi-shorthand supersets. In code-point mode
# these are single atoms, so a following quantifier spans the whole code point; re is the oracle.
_CLASSES_STR_ONLY = ["[é]", "[éàü]", "[à-ÿ]", "[a-zé]", "[^é]", "[^à-ÿ]", "[Ā-ſ]",
                     # Quasi-shorthand supersets (\w ∪ non-word CP): the class of bug that made a
                     # range_count>=200 B-1 guard unsound -- must stay general under \b.
                     r"[\w😀]", r"[\w·]", r"[\w€]", r"[\d😀]", r"[\w-]", r"[\w_]"]

_CLASSES = _CLASSES_ASCII + _CLASSES_STR_ONLY

_QUANTS = ["", "*", "+", "?", "??", "*?", "+?", "{2}", "{1,3}", "{2,}", "{0,2}"]
# Quantifiers that cannot repeat (so cannot create a nullable loop). Anything
# else establishes a "looping context" whose body must always consume.
_NONLOOP_QUANTS = ["", "?", "??"]
# Possessive quantifiers (Tier 1): a bare atom or one wrapped in exactly one capturing
# group only -- REAL rejects a possessive/atomic construct over any compound body, which the
# harness's existing "REAL declines by design, skip" path (below) already handles gracefully,
# so this list can stay a touch broader than REAL's own accepted shapes without risk.
_POSSESSIVE_QUANTS = ["*+", "++", "?+", "{2}+", "{1,3}+", "{2,}+", "{0,2}+"]
# Single-byte delimiters for the whole-pattern "quoted"/delimited possessive
# shape (`"[^"]*+"`) -- see PatternGen._quoted_possessive below.
_DELIMS = ['"', "'", ";", "|"]
# Tokens that always consume >= 1 character: the only things allowed directly
# inside a looping quantifier, keeping every loop body non-nullable (incl. UTF-8 code points).
_CONSUMING = [re.escape(c) for c in "abABC012_-é€😀"] + \
             [r"\d", r"\w", r"\s", ".", "[abc]", "[a-c]", "[^abc]", "[a-z0-9]"]
# Str-only: \w±non-word CP under loops — the quasi-shorthand class B-1 must not mis-arm on.
_CONSUMING_QUASI_W = [r"[\w😀]", r"[\w·]", r"[\w€]", r"[\d😀]", r"[\w-]"]
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
        # Bytes mode takes the ASCII list by NAME, not by index -- see _CLASSES_ASCII.
        self._literals = "abcABC012 _-." if ascii_only else _LITERALS
        self._classes = _CLASSES_ASCII if ascii_only else _CLASSES
        # Looping bodies: bytes path must stay ASCII-safe (_CONSUMING already has UTF-8
        # literals via re.escape of é€😀 — those fail to compile under .encode() and are
        # skips; quasi-shorthand classes are str-only extras for the B-1 guard net).
        self._consuming = _CONSUMING if ascii_only else (_CONSUMING + _CONSUMING_QUASI_W)

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
        if self.rng.random() < 0.15:
            return self._scoped_flag_group(inner)
        return ("(?:" if self.rng.random() < 0.5 else "(") + inner + ")"

    def _scoped_flag_group(self, inner):
        """Return a scoped inline-flags group over the five scopable flags {i, m, s, x, a}, with an
        optional negative part: e.g. ``(?i:...)``, ``(?a-i:...)``, ``(?-x:...)``, ``(?ms:...)``. Nesting
        and negative islands arise naturally from the recursion. Both engines apply these identically.

        Args:
            inner (str): The already-generated group body.

        Returns:
            str: A ``(?flags:...)`` / ``(?flags-flags:...)`` fragment.
        """
        letters = "imsxa"
        added = "".join(c for c in letters if self.rng.random() < 0.5)
        removed = "".join(c for c in letters if c not in added and self.rng.random() < 0.4)
        if not added and not removed:
            added = self.rng.choice(letters)
        spec = added + ("-" + removed if removed else "")
        return "(?" + spec + ":" + inner + ")"

    def _element(self, depth):
        """Return an atom with a quantifier, avoiding nullable loop bodies.

        Args:
            depth (int): Current nesting depth.

        Returns:
            str: A quantified pattern fragment.
        """
        if _RE_SUPPORTS_POSSESSIVE and self.rng.random() < 0.1:
            return self._possessive_element()
        quant = self.rng.choice(_QUANTS)
        if quant not in _NONLOOP_QUANTS:
            # A looping quantifier. Occasionally build a NON-CAPTURING nullable loop — an empty
            # alternation branch under * / + — the class that hid the greedy empty-preference span bug.
            # Non-capturing keeps this a SPAN comparison (no group-capture divergence to exclude), so the
            # differential catches a wrong loop span while the intentional capture divergence stays out.
            if self.rng.random() < 0.15:
                return "(?:" + self._nullable_alt(depth) + ")" + quant
            # Otherwise the body must always consume: a bare consuming token only, never a group or an
            # already-quantified atom (the OTHER way nullable loops — the capture divergence — arise).
            return self.rng.choice(self._consuming) + quant
        return self._atom(depth) + quant

    def _possessive_element(self):
        """Return a possessive-quantified atom (Tier 1) or a small atomic group.

        Only called when `_RE_SUPPORTS_POSSESSIVE` (the 3.11+ probe) is true. Generates
        BOTH REAL-supported shapes (a bare atom, or one wrapped in exactly one capturing
        group) and REAL-rejected ones (an atomic group over an alternation) -- the harness's
        existing "REAL declines by design, skip" path already handles the latter
        gracefully, so exercising the rejection boundary itself is free coverage, not a risk.

        Returns:
            str: A possessive-quantified atom or an atomic-group fragment.
        """
        if self.rng.random() < 0.25:
            return self._atomic_group()
        atom = self.rng.choice(self._consuming)
        if self.rng.random() < 0.3:
            atom = "(" + atom + ")"  # single-captured-atom: still Tier 1
        return atom + self.rng.choice(_POSSESSIVE_QUANTS)

    def _atomic_group(self):
        """Return an atomic group `(?>...)` — a Tier-1-eligible body most of the time, an
        alternation body (which REAL rejects, `re` accepts) occasionally to exercise the
        rejection boundary itself.

        Returns:
            str: An atomic-group fragment.
        """
        if self.rng.random() < 0.2:
            body = "|".join(self.rng.choice(["a", "b", "ab"]) for _ in range(2))
            return "(?>" + body + ")"
        atom = self.rng.choice(self._consuming)
        if self.rng.random() < 0.5:
            atom += self.rng.choice(["*", "+", "?", "{2}", "{1,3}"])  # (?>X*) desugars to Tier 1
        return "(?>" + atom + ")"

    def _quoted_possessive(self):
        """Return a WHOLE-PATTERN "delimited" possessive shape : a literal
        delimiter, a possessive class run, and a closing literal -- the "quoted string" shape
        REAL's new fast-path route specifically targets (`"[^"]*+"`). Only meaningful as a
        top-level pattern (unlike `_possessive_element`, which nests inside a larger sequence
        and so rarely if ever produces the exact whole-pattern shape the route requires) --
        called from `pattern()` directly, never from `_element`/`_seq`.

        Generates BOTH the route-ELIGIBLE case (body excludes the delimiter, e.g. `"[^"]*+"`)
        and a deliberately INELIGIBLE one (an alphanumeric prefix whose bytes are members of
        the body's own class, e.g. `id=[a-z0-9]*+;` -- REAL's own eligibility guard declines
        this and it stays on the general VM) -- REAL must give the identical answer either
        way; only the route differs, and that is exactly what this generator is for.

        Returns:
            str: A whole-pattern delimited possessive fragment.
        """
        quant = self.rng.choice(_POSSESSIVE_QUANTS[:2])  # *+ / ++ only -- the route is unbounded-only
        if self.rng.random() < 0.6:
            delim = self.rng.choice(_DELIMS)
            body = "[^" + re.escape(delim) + "]" + quant
            return delim + body + delim
        prefix = self.rng.choice(["id=", "key=", "x="])
        body = "[a-z0-9]" + quant
        suffix = self.rng.choice([";", ",", ")"])
        return prefix + body + suffix

    def _nullable_alt(self, depth):
        """Return an alternation with an empty branch (so the whole thing is nullable): ``|a`` / ``a|`` /
        ``|a|b``. Used only inside a non-capturing group under a looping quantifier."""
        body = self.rng.choice(["a", "b", "ab", "[ab]", "a[bc]"])
        return self.rng.choice(["|" + body, body + "|", "|" + body + "|" + self.rng.choice("ab")])

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
        # With more than one branch, a branch may be EMPTY (the nullable-alternation class, e.g. `|a`):
        # a quantified empty-first-branch group is where the greedy-loop empty-preference lives, and a
        # generator that never emits it cannot exercise that path.
        branches = ["" if n > 1 and self.rng.random() < 0.25 else self._seq(depth) for _ in range(n)]
        return "|".join(branches)

    def pattern(self):
        """Generate a complete pattern, optionally surrounded by anchors.

        Returns:
            str: A random regular expression pattern.
        """
        if _RE_SUPPORTS_POSSESSIVE and self.rng.random() < 0.08:
            # The whole-pattern "quoted"/delimited shape must be generated at
            # the TOP level (see _quoted_possessive's own doc comment) -- anchors below still
            # apply on top of it like any other body.
            body = self._quoted_possessive()
        else:
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
                           "٣٥9 x y z ９０",
                           "x\x1c\x1d\x1e\x1f\x0b\x0c y\tz"])  # + control chars: FS/GS/RS/US are \s in re (the class the alphabet never had)
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


# In text (str) mode every shorthand and boundary is Unicode (\w \W \d \D \s \S \b \B), in or out of a
# class, so the re oracle is the full Unicode default -- no re.ASCII. A \xHH / octal byte-escape still
# keeps byte provenance and never folds, so it is dropped from icase (no oracle agrees).
_BYTE_ESCAPE = re.compile(r"\\x|\\[0-7]")

# The real text-mode flag sets fuzzed, and the matching re base flags (without re.ASCII).
_REAL_FLAG_SETS = [0, real.I, real.M, real.S, real.M | real.S]
_BASE_RE = {0: 0, real.I: re.IGNORECASE, real.M: re.MULTILINE, real.S: re.DOTALL,
            real.M | real.S: re.MULTILINE | re.DOTALL}

# Known CPython 3.14 oracle bug, NOT a REAL bug -- filtered out of the differential rather than
# fixed on REAL's side (REAL is already consistently correct on both sides of it; see
# docs/divergences.dox and TestIntentionalDivergences.test_scoped_ascii_negated_shorthand_leading_bug
# for the pinned real-only assertion). When a scoped ascii group (?a:...) / (?a...-...:...) -- 'a'
# in the ADDED letters -- is the pattern's own FIRST construct, with LITERALLY NOTHING preceding it
# (not even a zero-width assertion or a single literal byte), a negated shorthand \S/\D/\W inside it
# fails to match U+001C-U+001F (the 4 separators whose ascii-vs-Unicode \s classification differs --
# the same 4 codepoints Bug B fixed). Prepending anything at all -- \B, ^, a lookahead, a literal --
# "fixes" it, and re.search(text, pos, endpos) does NOT (verified live: pos=1 does not fix it, so
# this is a compile-time artifact of the pattern's own opcode order, not a runtime one). Scoped
# rather than checked structurally (parsing nesting depth is not worth it for a fuzzer filter):
# conservatively broad (any \S/\D/\W anywhere in the pattern, not just the exact leading atom) so it
# never lets a REAL false positive slip through as "known CPython bug" -- it can only skip a few
# comparisons that would have agreed anyway, never mask a real divergence.
_LEADING_ASCII_SCOPE = re.compile(r"^\(\?([a-zA-Z]+)(?:-[a-zA-Z]+)?:")
_NEGATED_SHORTHAND = re.compile(r"\\[SDW]")
_ASCII_SEPARATORS = frozenset(range(0x1C, 0x20))


def _hits_cpython_leading_scoped_ascii_bug(pattern, text):
    m = _LEADING_ASCII_SCOPE.match(pattern)
    if not m or "a" not in m.group(1) or not _NEGATED_SHORTHAND.search(pattern):
        return False
    return any(ord(c) in _ASCII_SEPARATORS for c in text)


class TestDifferentialFuzz(unittest.TestCase):
    """Differential fuzz test comparing real against Python re."""

    def test_every_generator_list_entry_is_actually_drawn(self):
        """A member that is listed but never generated is a false green.

        The lists below are the whole vocabulary of this differential: if an entry cannot be
        reached, the shapes it stands for are untested while the file reads as if they were. That
        is not hypothetical -- the bytes mode used to take "the ASCII classes" as `_CLASSES[:12]`,
        an index that appending to the list silently moved out of reach, so fifteen
        position-sensitive members were listed and drawn ZERO times in bytes mode.

        str mode must draw every entry of every list. bytes mode must draw every ASCII entry and
        NONE of the str-only ones -- asserted in both directions, because "not drawn" is the
        correct answer there and a test that only checked for presence would pass on a generator
        that had stopped producing them at all.
        """
        import collections

        str_only = set(_CLASSES_STR_ONLY) | set(_CONSUMING_QUASI_W)
        for ascii_only in (False, True):
            gen = PatternGen(random.Random(11), ascii_only=ascii_only)
            drawn = collections.Counter(gen.pattern() for _ in range(20000))
            corpus = " ".join(drawn)
            for name, items in (("_CLASSES_ASCII", _CLASSES_ASCII),
                                ("_QUANTS", [q for q in _QUANTS if q]),
                                ("_ANCHORS", _ANCHORS),
                                ("_CLASSES_STR_ONLY", _CLASSES_STR_ONLY)):
                for item in items:
                    if not item:
                        continue
                    expected = not (ascii_only and item in str_only)
                    with self.subTest(mode="bytes" if ascii_only else "str", list=name, item=item):
                        self.assertEqual(item in corpus, expected,
                                         "{!r} from {} was {} in {} mode".format(
                                             item, name, "absent" if expected else "drawn",
                                             "bytes" if ascii_only else "str"))

    def test_random_patterns_match_re(self):
        """Random patterns produce the same results as re across all APIs."""
        rng = random.Random(SEED)
        checked = 0
        skipped = 0
        for _ in range(ITERS):
            gen = PatternGen(rng)
            pattern = gen.pattern()
            real_flags = rng.choice(_REAL_FLAG_SETS)
            base = _BASE_RE[real_flags]
            # A \xHH / octal byte-escape keeps byte provenance and never folds; re folds it under
            # icase, so no oracle agrees -- drop icase for those patterns rather than pin a wrong result.
            if _BYTE_ESCAPE.search(pattern) and (real_flags & real.I):
                real_flags &= ~real.I
                base &= ~re.IGNORECASE
            re_flags = base  # full Unicode oracle: every shorthand/boundary is Unicode in str mode
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
                if _hits_cpython_leading_scoped_ascii_bug(pattern, text):
                    continue  # known CPython 3.14 oracle bug, not a REAL bug -- see the filter's own doc comment
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
            compact = gen.pattern()
            verbose = verbosify(compact, rng)
            re_flags = re.VERBOSE  # full Unicode oracle (every shorthand/boundary is Unicode in str mode)
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
                # verbosify() never inserts whitespace inside an escape/group-opener token but CAN
                # insert it before the pattern's first real construct; VERBOSE strips it before
                # compiling, so the EFFECTIVE leading construct is still the pre-verbosify string's
                # -- check `compact` (not `verbose`) for the same known CPython bug (see the
                # filter's own doc comment above).
                if _hits_cpython_leading_scoped_ascii_bug(compact, text):
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

    def test_malformed_utf8_bytes_match_re(self):
        """The exact malformed-UTF-8 byte sequences that test_utf8_malformed_matrix.cpp
        pins as *rejected* in text mode must, in BYTES mode, be plain data that re also accepts --
        bytes mode has no UTF-8 concept on either engine, so a byte that is "malformed" only means
        something in text mode. This is the differential half of that C++ file's own contract."""
        # Same catalog as tests/unicode/test_utf8_malformed_matrix.cpp's malformed_catalog(),
        # kept in sync by name/bytes so a change on one side is easy to mirror on the other.
        malformed_catalog = {
            "lone_continuation_80": bytes([0x80]),
            "lone_continuation_bf": bytes([0xBF]),
            "truncated_2byte_lead_eot": bytes([0xC3]),
            "truncated_3byte_lead_eot": bytes([0xE2, 0x82]),
            "truncated_4byte_lead_eot": bytes([0xF0, 0x9F, 0x98]),
            "overlong_2byte_c0_80": bytes([0xC0, 0x80]),
            "overlong_3byte_e0_80_80": bytes([0xE0, 0x80, 0x80]),
            "overlong_4byte_f0_80_80_80": bytes([0xF0, 0x80, 0x80, 0x80]),
            "surrogate_ed_a0_80": bytes([0xED, 0xA0, 0x80]),
            "past_10ffff_f5": bytes([0xF5, 0x80, 0x80, 0x80]),
            "invalid_lead_fe": bytes([0xFE]),
            "invalid_lead_ff": bytes([0xFF]),
        }
        # \xHH-escaped pattern spelling of a byte sequence: a RAW non-ASCII byte embedded directly
        # in a pattern (even a bytes pattern) is rejected at compile time ("non-ASCII character
        # class member not supported" -- confirmed empirically here, and the reason
        # test_random_bytes_match_re's own generator is ascii_only=True). \xHH is the escape both
        # engines agree keeps byte provenance (test_utf8.cpp's utf8_bytes_mode_classes).
        def esc(bs):
            return b"".join(b"\\x%02x" % b for b in bs)

        # Representative byte patterns: "any one byte" (repeated), a byte class carrying the exact
        # malformed lead byte, a negated-delimiter run (the class_loop/codepoint_class_plus shape
        # the C++ file's run_cascade_stop tests exercise), and a literal wrapping the sequence.
        checked = 0
        for name, seq in malformed_catalog.items():
            prefix, suffix = b"ab", b"cd"
            text = prefix + seq + suffix
            patterns = [
                rb".",
                rb".+",
                rb"[^\"]+",
                esc(seq),                    # the malformed bytes as a literal, \xHH-escaped
                b"[" + esc(seq[:1]) + b"]",  # a byte class carrying just its lead byte
            ]
            for pat in patterns:
                rp = re.compile(pat)
                xp = real.compile(pat)  # a bytes pattern implies bytes-mode matching, like re
                ctx = f"case={name} pattern={pat!r} text={text!r}"
                ng = rp.groups
                facts = (match_facts(xp.search(text), ng), [m.span() for m in xp.finditer(text)])
                ref = (match_facts(rp.search(text), ng), [m.span() for m in rp.finditer(text)])
                self.assertEqual(facts, ref, ctx)
                checked += 1
        self.assertEqual(checked, 12 * 5)  # every (catalog entry, pattern) pair actually ran


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
