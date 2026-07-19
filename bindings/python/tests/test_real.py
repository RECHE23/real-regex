"""Behavioral tests for the real module (API shape, types, errors)."""

import re
import unittest

import real


class TestCompile(unittest.TestCase):
    """Tests for real.compile and compilation-time behavior."""

    def test_compile_returns_pattern(self):
        """compile() returns a Pattern with the expected attributes."""
        p = real.compile(r"\d+")
        self.assertIsInstance(p, real.Pattern)
        self.assertEqual(p.pattern, r"\d+")
        self.assertEqual(p.groups, 0)

    def test_compile_pattern_passthrough(self):
        """compile() returns an already-compiled Pattern unchanged."""
        p = real.compile(r"x")
        self.assertIs(real.compile(p), p)
        with self.assertRaises(ValueError):
            real.compile(p, real.I)

    def test_cache(self):
        """Repeated compile() calls for the same pattern return the same object."""
        self.assertIs(real.compile(r"abc"), real.compile(r"abc"))
        real.purge()

    def test_invalid_patterns_raise_error(self):
        """Invalid syntax raises real.error."""
        for pattern in [r"(", r")", r"a**", r"[z-a]", r"(?>a|b)",  # atomic groups: Tier 1 bodies only; a compound/alternating body is not
                        r"(?P=name)", r"(?P<1>x)", r"\q", "a\\"]:
            with self.assertRaises(real.error, msg=pattern):
                real.compile(pattern)

    def test_unsupported_flags_raise(self):
        """Unsupported re flags raise real.error."""
        import re
        for flag in (re.L, re.DEBUG):
            with self.assertRaises(real.error):
                real.compile("a", int(flag))

    def test_verbose_flag(self):
        """VERBOSE/X ignores unescaped whitespace and # comments outside classes."""
        self.assertTrue(real.compile(r"a b c", real.X).fullmatch("abc"))
        self.assertTrue(real.compile(r"\d{4} - \d{2}", real.VERBOSE).fullmatch("2026-06"))
        self.assertTrue(real.compile("a # note\nb", real.X).fullmatch("ab"))
        self.assertTrue(real.compile(r"[ ]", real.X).fullmatch(" "))  # literal in class
        self.assertTrue(real.compile(r"(?x) a b c").fullmatch("abc"))  # inline (?x)

    def test_ascii_and_unicode_flags(self):
        """A restricts \\d \\s (and folding) to ASCII; U is a no-op (Unicode is the default)."""
        self.assertTrue(real.compile("a", real.A).match("a"))  # no effect on a plain literal
        self.assertTrue(real.compile("a", real.U).match("a"))
        self.assertEqual(real.findall(r"\d", "٣5", real.A), ["5"])   # A: ASCII digits only
        self.assertEqual(real.findall(r"\d", "٣5", real.U), ["٣", "5"])  # U: Unicode (default)

    def test_program_size_limit(self):
        """Patterns exceeding the compiled-program size cap raise real.error."""
        # Under cap (a{200} ~200 instr) still works and matches.
        p = real.compile("a{200}")
        self.assertTrue(p.match("a" * 200))
        # Over cap (nested unroll product >> 262144) raises cleanly.
        # This is the #1 security fix: prevents DoS from tiny patterns.
        with self.assertRaises(real.error):
            real.compile("((a{400}){400}){2}")


class TestMatchObject(unittest.TestCase):
    """Tests for the real.Match object accessors."""

    def test_basic_accessors(self):
        """group(), groups(), groupdict(), span(), start(), end() work as expected."""
        m = real.search(r"(?P<y>\d{4})-(\d{2})", "on 2026-06-10!")
        self.assertEqual(m.group(), "2026-06")
        self.assertEqual(m.group(0), "2026-06")
        self.assertEqual(m.group(1), "2026")
        self.assertEqual(m.group("y"), "2026")
        self.assertEqual(m.group(1, 2), ("2026", "06"))
        self.assertEqual(m[2], "06")
        self.assertEqual(m["y"], "2026")
        self.assertEqual(m.groups(), ("2026", "06"))
        self.assertEqual(m.groupdict(), {"y": "2026"})
        self.assertEqual(m.span(), (3, 10))
        self.assertEqual(m.start(2), 8)
        self.assertEqual(m.end("y"), 7)
        self.assertEqual(m.string, "on 2026-06-10!")
        self.assertIs(m.re, real.compile(r"(?P<y>\d{4})-(\d{2})"))

    def test_unset_group(self):
        """Unset groups return None (or the default) and span (-1, -1)."""
        m = real.fullmatch(r"(a)|(b)", "b")
        self.assertIsNone(m.group(1))
        self.assertEqual(m.groups(), (None, "b"))
        self.assertEqual(m.groups(default=""), ("", "b"))
        self.assertEqual(m.span(1), (-1, -1))

    def test_bad_group_raises(self):
        """Out-of-range or unknown groups raise IndexError."""
        m = real.search(r"(a)", "a")
        with self.assertRaises(IndexError):
            m.group(2)
        with self.assertRaises(IndexError):
            m.group("nope")

    def test_non_ascii_subject_uses_char_indices(self):
        """Span indices are character offsets, not byte offsets."""
        m = real.search(r"au", "café au lait")
        self.assertEqual(m.span(), (5, 7))
        self.assertEqual(m.group(), "au")
        m = real.search(r"l\w+", "café au lait")
        self.assertEqual(m.span(), (8, 12))


class TestBytes(unittest.TestCase):
    """Tests for bytes patterns and subjects."""

    def test_bytes_pattern_and_subject(self):
        """Bytes patterns work on bytes subjects."""
        p = real.compile(rb"(\d+)")
        m = p.search(b"a 42!")
        self.assertEqual(m.group(0), b"42")
        self.assertEqual(m.span(), (2, 4))
        self.assertEqual(p.findall(b"1 22"), [b"1", b"22"])
        self.assertEqual(p.sub(rb"<\1>", b"a 42!"), b"a <42>!")

    def test_type_mixing_is_rejected(self):
        """Mixing str and bytes patterns/subjects raises TypeError."""
        with self.assertRaises(TypeError):
            real.compile(rb"a").search("a")
        with self.assertRaises(TypeError):
            real.compile(r"a").search(b"a")
        with self.assertRaises(TypeError):
            real.compile(r"a").sub(b"x", "a")


class TestSub(unittest.TestCase):
    """Tests for sub() / subn() replacement templates."""

    def test_template_references(self):
        """Numeric and named back-references are expanded correctly."""
        self.assertEqual(real.sub(r"(\w+)@(\w+)", r"\2:\1", "bob@host"), "host:bob")
        self.assertEqual(real.sub(r"(?P<u>\w+)@", r"\g<u>!", "bob@x"), "bob!x")
        self.assertEqual(real.sub(r"(a)", r"\g<1>\g<0>", "a"), "aa")
        self.assertEqual(real.sub(r"a", r"\n\\", "a"), "\n\\")
        self.assertEqual(real.sub(r"a", r"\!", "a"), r"\!")  # punct keeps backslash

    def test_template_errors(self):
        """Invalid back-references raise real.error."""
        with self.assertRaises(real.error):
            real.sub(r"(a)", r"\2", "a")
        with self.assertRaises(real.error):
            real.sub(r"a", r"\q", "a")
        with self.assertRaises(real.error):
            real.sub(r"a", r"\g<zz>", "a")

    def test_callable_and_count(self):
        """Callable replacements and count limits work."""
        self.assertEqual(real.sub(r"\d+", lambda m: str(int(m.group()) * 2), "3 4"),
                         "6 8")
        self.assertEqual(real.sub(r"x", "-", "xxx", 2), "--x")
        self.assertEqual(real.subn(r"x", "-", "xxx"), ("---", 3))


class TestModuleFunctions(unittest.TestCase):
    """Tests for the module-level convenience functions."""

    def test_match_vs_search_vs_fullmatch(self):
        """match, search and fullmatch differ in anchoring semantics."""
        self.assertIsNone(real.match(r"b", "ab"))
        self.assertIsNotNone(real.search(r"b", "ab"))
        self.assertIsNone(real.fullmatch(r"a", "ab"))
        self.assertIsNotNone(real.fullmatch(r"ab", "ab"))

    def test_findall_shapes(self):
        """findall returns the right shape depending on group count."""
        self.assertEqual(real.findall(r"\d+", "a1 b22"), ["1", "22"])
        self.assertEqual(real.findall(r"(\d)\d", "12 34"), ["1", "3"])
        self.assertEqual(real.findall(r"(a)|(b)", "ab"), [("a", ""), ("", "b")])

    def test_regex_set_which_matched(self):
        """RegexSet which-matched: construction order, any-match, compile-fail."""
        s = real.RegexSet([r"alpha", r"beta", r"gamma"])
        self.assertEqual(len(s), 3)
        self.assertEqual(s.matches("xx beta yy"), [False, True, False])
        self.assertEqual(s.which("xx beta yy"), [1])
        self.assertTrue(s.is_match("please find beta"))
        self.assertFalse(s.is_match("nothing"))
        # Oracle: N × search
        text = "2026-06-13 error id=a3f9c1d8"
        pats = [r"[0-9]{4}-[0-9]{2}-[0-9]{2}", r"error|warn", r"absent_xyz"]
        rs = real.RegexSet(pats)
        self.assertEqual(rs.matches(text),
                         [real.search(p, text) is not None for p in pats])
        with self.assertRaises(real.error):
            real.RegexSet([r"ok", r"(?>a|b)"])  # atomic groups: Tier 1 bodies only; a compound/alternating body is not

    def test_regex_set_native_matches_n_loop_reference(self):
        """R4: RegexSet now wraps real::regex_set directly (Stage-1/Stage-2 fused inside the C++
        engine) instead of looping N individual Pattern.search calls in Python. Differential
        proof that the rewire changed nothing observable: for every (pattern-set, text) pair,
        RegexSet.matches/.which/.is_match must agree EXACTLY (order included) with the N-loop
        reference built from individual real.search calls -- the old implementation's own logic,
        kept here purely as the oracle, not as production code."""
        patterns = [
            r"alpha", r"beta", r"gamma", r"[0-9]+", r"[a-z]+", r"[A-Z]+",
            r"\d{2,4}", r"^start", r"end$", r"a|b|c", r"foo(bar)?", r"[^x]+",
            r"colou?r", r"(?:ab)+", r"x{2,5}", r"[[:digit:]]+", r"q(?=u)",
            r"(?<=a)b", r"\bword\b", r"\W+", r"\s+", r"[^\s]+", r".*",
            r"(cat|dog)s?", r"1[0-9]{3}", r"[A-Za-z0-9_]+", r"--+", r"==",
        ]
        texts = [
            "alpha beta gamma 123 xyz ABC",
            "no matches for most of these ZZZ",
            "start of the line, and the end",
            "foobar foo colour color coloor",
            "abababab xxxxx q qu quack",
            "a cat and a dog, cats and dogs, 1999",
            "word boundaries: word, sword, words",
            "",
            "____----====....",
            "\t\n  \r\n whitespace only  ",
        ]
        rs = real.RegexSet(patterns)
        self.assertEqual(len(rs), len(patterns))
        for text in texts:
            reference = [real.search(p, text) is not None for p in patterns]
            self.assertEqual(rs.matches(text), reference, msg=f"matches() diverged on {text!r}")
            self.assertEqual(rs.which(text), [i for i, hit in enumerate(reference) if hit],
                             msg=f"which() diverged on {text!r}")
            self.assertEqual(rs.is_match(text), any(reference), msg=f"is_match() diverged on {text!r}")
            # Region-aware: pos/endpos must thread through to the same reference shape.
            if len(text) >= 4:
                pos, endpos = 1, len(text) - 1
                # real.compile(p).search(text, pos, endpos) is the reference: region-aware (pos is
                # the VM anchor, not a slice), matching regex_set's own pos/endpos contract exactly.
                region_reference = [real.compile(p).search(text, pos, endpos) is not None
                                    for p in patterns]
                self.assertEqual(rs.matches(text, pos=pos, endpos=endpos), region_reference,
                                 msg=f"region matches() diverged on {text!r}")

    def test_regex_set_native_stage2_fused_parity(self):
        """The fused Stage-2 single-pass DFA (regex_set.hpp's fused_min_eligible = 56) is new
        code the old Python N-loop never exercised at all -- push a set past that threshold with
        plain DFA-eligible patterns (no lookaround, no Unicode \\w/\\d/\\s) and prove the fused
        path still agrees with the N-loop reference, order included."""
        patterns = [f"tok{i:03d}" for i in range(40)] + \
                   [rf"[a-{chr(ord('a') + (i % 20))}]+{i}" for i in range(30)]
        self.assertGreaterEqual(len(patterns), 56)
        rs = real.RegexSet(patterns)
        texts = [
            " ".join(f"tok{i:03d}" for i in range(0, 40, 3)),
            "aaaa5 bbbb12 nothing_here zzzz29",
            "no tokens and no classes match this one at all",
            "tok000tok001tok002 back to back, aaa0aaa1aaa2",
        ]
        for text in texts:
            reference = [real.search(p, text) is not None for p in patterns]
            self.assertEqual(rs.matches(text), reference, msg=f"fused matches() diverged on {text!r}")
            self.assertEqual(rs.which(text), [i for i, hit in enumerate(reference) if hit])
            self.assertEqual(rs.is_match(text), any(reference))

    def test_count_matches(self):
        """count_matches agrees with findall / finditer without materialising Match objects."""
        self.assertEqual(real.count_matches(r"\d+", "a1 b22 c333"), 3)
        self.assertEqual(real.count_matches(r"[a-z]+", "abc def"), 2)
        # Trailing-LA class+: same count as finditer (fast path on count_matches only).
        la = r"[a-z]+(?=[a-z])"
        text = "abc def ghi"
        self.assertEqual(real.count_matches(la, text),
                         sum(1 for _ in real.finditer(la, text)))
        self.assertEqual(real.count_matches(la, "abc def"), 2)  # [0,2) [4,6)
        p = real.compile(r"\d+")
        self.assertEqual(p.count_matches("1 22 333"), 3)
        self.assertEqual(p.count_matches("1 22 333", pos=2), 2)  # from "22 333"
        # endpos=4 → region "1 22"; matches "1" and "22"
        self.assertEqual(p.count_matches("1 22 333", endpos=4), 2)
        self.assertEqual(p.count_matches("1 22 333", endpos=4),
                         len(p.findall("1 22 333", endpos=4)))
        # Module-level and Pattern both exposed.
        self.assertTrue(hasattr(real, "count_matches"))
        self.assertTrue(hasattr(p, "count_matches"))
    def test_finditer_and_split(self):
        """finditer yields Match objects and split respects maxsplit."""
        spans = [m.span() for m in real.finditer(r"x*", "axb")]
        self.assertEqual(spans, [(0, 0), (1, 2), (2, 2), (3, 3)])
        self.assertEqual(real.split(r"(x)|(y)", "axbyc"),
                         ["a", "x", None, "b", None, "y", "c"])
        self.assertEqual(real.split(r",", "a,b,c", maxsplit=1), ["a", "b,c"])

    def test_finditer_is_lazy_and_reentrant(self):
        """finditer returns a real lazy iterator: self-iterating, exhausting with
        StopIteration, reentrant (independent cursors on one Pattern), and scaling
        to many matches without materialising them."""
        p = real.compile(r"\d+")
        it = p.finditer("a1b22c333")
        self.assertIs(iter(it), it)                                # its own iterator
        self.assertEqual(next(it).group(), "1")                    # lazy: first match, no draining
        self.assertEqual([m.group() for m in it], ["22", "333"])  # resumes from the cursor
        with self.assertRaises(StopIteration):
            next(it)
        i1, i2 = p.finditer("1 2 3"), p.finditer("4 5 6")          # independent cursors, same Pattern
        self.assertEqual(
            [next(i1).group(), next(i2).group(), next(i1).group(), next(i2).group()],
            ["1", "4", "2", "5"])
        self.assertEqual(sum(1 for _ in p.finditer("x9 " * 10000)), 10000)  # scale

    def test_finditer_euro_class_empty_alt_matches_re(self):
        """CI finding (macos-3.10 differential, 2026-07-17): finditer must not drop the
        mid-string ``€€`` match for an empty-leading alternation over a quasi-shorthand
        class with an astral-adjacent subject.

        Reported once as REAL ``[(0,0),(7,7)]`` vs re ``[(0,0),(4,6),(7,7)]`` on
        ``pattern='^|[\\w€]{2}|\\137?[é]??$'`` / ``text='€😀é €€x'`` / ``M|S``.
        Deterministic pin (floor): re is the oracle. Non-determinism is investigated
        separately under ASan; this pin makes a *stable* wrong-match fail immediately.
        """
        pattern = r"^|[\w€]{2}|\137?[é]??$"
        text = "€😀é €€x"
        flags = real.M | real.S
        re_flags = re.M | re.S
        xp = real.compile(pattern, flags)
        rp = re.compile(pattern, re_flags)
        self.assertEqual([m.span() for m in xp.finditer(text)],
                         [m.span() for m in rp.finditer(text)])
        self.assertEqual(xp.findall(text), rp.findall(text))
        # Full region matrix: any (pos, endpos) search span must match re.
        for pos in range(len(text) + 1):
            for endpos in range(pos, len(text) + 1):
                mr = xp.search(text, pos, endpos)
                mp = rp.search(text, pos, endpos)
                self.assertEqual(
                    None if mr is None else mr.span(),
                    None if mp is None else mp.span(),
                    f"pos={pos} endpos={endpos}")

    def test_search_is_thread_safe(self):
        """Concurrent search/match on shared and distinct Patterns give correct
        results. match/fullmatch/search release the GIL around the scan for subjects
        >= 512 B (here ~1 KB) and use per-call-local scratch — no shared state to race."""
        import threading
        big = "word " * 200  # ~1 KB (>= the 512 B GIL-release threshold), contains no digits
        subjects = [big + f"#{i}" for i in range(400)]
        expected = [str(i) for i in range(400)]  # search(\d+) finds the trailing number
        results = [None] * len(subjects)
        errors = []
        shared = real.compile(r"\d+")

        def worker(indices, pat):
            try:
                for i in indices:
                    found = pat.search(subjects[i])
                    results[i] = found.group() if found else None
                    if pat.match(subjects[i]) is not None:  # anchored: subject starts with "word"
                        errors.append(f"match unexpectedly matched at {i}")
            except BaseException as exc:  # noqa: BLE001 — record; asserted in the main thread
                errors.append(exc)

        threads = []
        for t in range(4):
            pat = shared if t % 2 == 0 else real.compile(r"\d+")  # mix shared + distinct Patterns
            threads.append(threading.Thread(target=worker, args=(range(t, len(subjects), 4), pat)))
        for th in threads:
            th.start()
        for th in threads:
            th.join()
        self.assertEqual(errors, [])
        self.assertEqual(results, expected)

    def test_escape_roundtrip(self):
        """escape() produces a pattern that matches the original literal."""
        for text in ["a.b*c", "1+1=2?", "[hi]{2}|x^$", "plain"]:
            self.assertTrue(real.fullmatch(real.escape(text), text), text)
        self.assertTrue(real.fullmatch(real.escape(b"a.b"), b"a.b"))

    def test_flags(self):
        """IGNORECASE, MULTILINE and DOTALL work as expected."""
        self.assertTrue(real.search(r"hello", "say HELLO", real.I))
        self.assertTrue(real.search(r"^b", "a\nb", real.M))
        self.assertTrue(real.fullmatch(r"a.b", "a\nb", real.S))
        self.assertTrue(real.search(r"(?i)hello", "HELLO"))


class TestNestingDepth(unittest.TestCase):
    """Tests for the parser recursion limit."""

    def test_deep_nesting_raises_instead_of_crashing(self):
        """Excessive nesting raises real.error instead of crashing."""
        # Used to overflow the C stack and kill the interpreter.
        with self.assertRaises(real.error):
            real.compile("(" * 50000 + "a" + ")" * 50000)
        self.assertTrue(real.compile("(" * 100 + "a" + ")" * 100).match("a"))


class TestRedosSafety(unittest.TestCase):
    """Tests demonstrating linear-time matching."""

    def test_pathological_pattern_is_fast(self):
        """A catastrophic pattern for backtracking engines is fast here."""
        import time
        start = time.perf_counter()
        self.assertIsNone(real.search(r"(a+)+b", "a" * 500))
        self.assertLess(time.perf_counter() - start, 0.1)


class TestWordEdgeAnchors(unittest.TestCase):
    """Tests for REAL's \< and \> word-edge anchors."""
    # \< and \> are word-start / word-end anchors (a REAL extension; Python re
    # has no equivalent), reusing the same ASCII word-character notion as \b.

    def test_word_start_and_end(self):
        """\< and \> anchor at word edges."""
        self.assertEqual(real.findall(r"\<\w+", "foo bar-baz"), ["foo", "bar", "baz"])
        self.assertEqual(real.findall(r"\w+\>", "foo bar-baz"), ["foo", "bar", "baz"])

    def test_whole_word_only(self):
        """\<cat\> matches only the whole word."""
        self.assertTrue(real.search(r"\<cat\>", "a cat here"))
        self.assertIsNone(real.search(r"\<cat\>", "category"))
        self.assertIsNone(real.search(r"\<cat\>", "concat"))

    def test_zero_width_at_edges(self):
        """Word-edge anchors are zero-width and work at string boundaries."""
        self.assertEqual(real.search(r"\<", "abc").span(), (0, 0))
        self.assertEqual(real.search(r"\>", "abc").span(), (3, 3))


class TestIntentionalDivergences(unittest.TestCase):
    r"""Every intentional divergence from Python re, pinned as a real-only assertion.

    These lock REAL's contract independently of the running re version; the rationale
    for each is in the "Differences from Python re" documentation page (docs/divergences.dox).
    """

    def test_character_class_non_ascii_members(self):
        # A str-mode class carries specific non-ASCII code points.
        # The broad differential-vs-re lives in test_parity; here we pin the behaviour.
        self.assertIsNotNone(real.compile(r"[é]").fullmatch("é"))
        self.assertIsNone(real.compile(r"[é]").fullmatch("à"))      # a specific code point, not "any non-ASCII"
        self.assertIsNotNone(real.compile(r"[\u00e9]").fullmatch("é"))
        self.assertIsNotNone(real.compile(r"[à-ÿ]").fullmatch("ê")) # a code-point range
        self.assertIsNone(real.compile(r"[^é]").fullmatch("é"))     # negation excludes é ...
        self.assertIsNotNone(real.compile(r"[^é]").fullmatch("à"))  # ... but matches other code points

    def test_named_scalar_escape_is_a_superset(self):
        r"""\N{U+XXXX} (the scalar form) is a REAL extension: the engine accepts it on both the C++
        and Python surfaces, while re knows \N{...} only as a *name* and rejects U+XXXX. \N{NAME}
        itself is exact re-parity (see the parity suite); this pins the intentional superset."""
        self.assertIsNotNone(real.compile(r"\N{U+0041}").fullmatch("A"))   # REAL accepts the scalar form
        self.assertIsNotNone(real.compile(r"\N{U+1F600}").fullmatch("😀"))  # incl. astral
        with self.assertRaises(re.error):
            re.compile(r"\N{U+0041}")                                       # re rejects it (name only)

    def test_nullable_loop_final_iteration_capture(self):
        r"""A */+ loop over a nullable body: re runs one final EMPTY iteration and captures it, while
        REAL keeps the last CONSUMING iteration (the RE2 / Rust / Go convention). The overall match span
        agrees; only the inner group differs. Double-pinned — both the re value and the REAL value, so a
        change on either side is caught. See the div_empty_iteration_capture divergences section."""
        # Still diverges — the body CONSUMES on its last iteration, so REAL keeps that span while re
        # takes an extra empty step. Double-pinned (both values).
        diverging = [
            (r"(a*)*", "a", (1, 1), (0, 1)),
            (r"(a*)+", "a", (1, 1), (0, 1)),
            (r"(a|)*", "a", (1, 1), (0, 1)),
        ]
        for pattern, subject, re_group, real_group in diverging:
            with self.subTest(pattern=pattern, kind="diverges"):
                rm = re.search(pattern, subject)
                lm = real.search(pattern, subject)
                self.assertEqual(rm.span(), lm.span())    # the whole-match span is identical
                self.assertEqual(rm.span(1), re_group)    # re captures the final empty iteration
                self.assertEqual(lm.span(1), real_group)  # REAL keeps the last consuming one
        # Now AGREES with re — a body that can only match empty: the greedy-loop empty-exit fix applies
        # the empty iteration's capture, exactly like re.
        for pattern, subject in [(r"()*", ""), (r"($)*", "")]:
            with self.subTest(pattern=pattern, kind="now-parity"):
                self.assertEqual(real.search(pattern, subject).span(1),
                                 re.search(pattern, subject).span(1))

    def test_empty_first_branch_loop_span_forced_non_empty(self):
        r"""An unbounded */+ loop whose body's FIRST alternative is empty and a later one consumes
        ((|a)*): under a forced-non-empty retry (finditer/sub), re exits the loop through the empty
        branch (shortest non-empty match) while REAL consumes maximally. Only the finditer span sequence
        differs -- search/match/fullmatch, and the empty-LAST and bounded forms, are in parity.
        Double-pinned (both sequences). See the div_empty_first_branch_loop divergences section; the
        regex crate does a third thing, so re is the arbiter, never the crate."""
        diverging = [
            (r"(|a)*", "aa", [(0, 0), (0, 1), (1, 1), (1, 2), (2, 2)], [(0, 0), (0, 2), (2, 2)]),
            (r"(|a)+", "aa", [(0, 0), (0, 1), (1, 1), (1, 2), (2, 2)], [(0, 0), (0, 2), (2, 2)]),
            (r"(a||b)*", "ab", [(0, 1), (1, 1), (1, 2), (2, 2)], [(0, 2), (2, 2)]),
        ]
        for pattern, subject, re_spans, real_spans in diverging:
            with self.subTest(pattern=pattern, kind="diverges"):
                self.assertEqual([m.span() for m in re.finditer(pattern, subject)], re_spans)
                self.assertEqual([m.span() for m in real.finditer(pattern, subject)], real_spans)
        # Green witnesses: single-match is in parity, and the empty-LAST + bounded forms agree fully.
        for pattern, subject in [(r"(|a)*", "aa"), (r"(|a)+", "aa")]:
            with self.subTest(pattern=pattern, kind="single-match-parity"):
                self.assertEqual(real.search(pattern, subject).span(), re.search(pattern, subject).span())
        for pattern, subject in [(r"(a|)*", "aa"), (r"(|a){2}", "aa"), (r"(|a){1,3}", "aa")]:
            with self.subTest(pattern=pattern, kind="parity"):
                self.assertEqual([m.span() for m in real.finditer(pattern, subject)],
                                 [m.span() for m in re.finditer(pattern, subject)])

    def test_icase_folds_unicode(self):
        # Text-mode icase does full Unicode simple case folding, like re.IGNORECASE: ASCII letters,
        # and non-ASCII code points, fold -- é matches É, and k matches Kelvin (U+212A).
        self.assertIsNotNone(real.compile("a", real.I).fullmatch("A"))
        self.assertIsNotNone(real.compile(r"é", real.I).fullmatch("é"))
        self.assertIsNotNone(real.compile(r"é", real.I).fullmatch("É"))
        self.assertIsNotNone(real.compile("k", real.I).fullmatch("K"))  # k <-> Kelvin
        # \w stays ASCII for now; \d \s are Unicode -- see test_shorthands_d_s_are_unicode.

    def test_shorthands_d_s_are_unicode(self):
        # \d \s are Unicode in str mode (like re); \w stays ASCII for now.
        self.assertEqual(real.findall(r"\d", "a٣b5"), ["٣", "5"])  # Arabic-Indic digit + ASCII
        self.assertIsNone(real.compile(r"\d").fullmatch("½"))       # No is not a \d digit
        self.assertIsNotNone(real.compile(r"\s").fullmatch("\u00a0"))  # NBSP
        self.assertIsNotNone(real.compile(r"\D").fullmatch("é"))
        self.assertEqual(real.findall(r"[\d]+", "٣5.9"), ["٣5", "9"])
        self.assertEqual(real.findall(r"\w+", "café"), ["café"])  # \w is Unicode (code-point-predicate match)
        self.assertEqual(real.findall(r"\w", "٣"), ["٣"])           # ٣ is a Unicode word char

    def test_ascii_flag_reverts_shorthands(self):
        # re.A keeps \d \s and folding ASCII in str mode; re.U is a no-op (Unicode is the default).
        self.assertEqual(real.findall(r"\d", "a٣b5", real.A), ["5"])
        self.assertIsNone(real.compile(r"\s", real.A).fullmatch("\u00a0"))  # NBSP not ASCII space
        self.assertIsNotNone(real.compile("k", real.A | real.I).fullmatch("k"))  # ASCII fold still works
        self.assertEqual(real.findall(r"\d", "٣5", real.U), ["٣", "5"])

    def test_scoped_ascii_negated_shorthand_leading_bug(self):
        r"""Not a REAL divergence from re's CONTRACT -- a known CPython 3.14 oracle BUG, filtered
        out of the differential fuzzer (test_differential_fuzz.py's
        _hits_cpython_leading_scoped_ascii_bug) rather than compared against, and pinned here
        instead. When a scoped ascii group -- (?a:...), 'a' among the ADDED letters -- is the
        PATTERN'S OWN FIRST CONSTRUCT with nothing preceding it at all, CPython's negated
        shorthand \S/\D/\W inside it wrongly fails to match U+001C-U+001F (the 4 separators whose
        ascii-vs-Unicode \s classification differs -- see Bug B / div_ascii in divergences.dox):
        `re.search(r"(?a:\S)", "\x1c")` is None even though \x1c is NOT ascii whitespace, so \S
        should match it -- re's own (?a:\s) on the same input agrees it is not whitespace ([]) yet
        (?a:\S) ALSO returns [] on the SAME codepoint, an outright partition violation ('\x1c'
        matches neither \s nor \S). Prepending literally anything -- \B, ^, a lookahead, or a
        single literal byte -- "fixes" it on re's side; re.search(text, pos, endpos) does NOT (a
        compile-time artifact of re's own opcode order, not a runtime one). REAL has no such
        inconsistency: bare and \B-prefixed agree, and \s/\S partition every one of these
        codepoints correctly, with or without a leading construct.
        """
        for sep in "\x1c\x1d\x1e\x1f":
            with self.subTest(sep=hex(ord(sep))):
                # re's own bug, pinned as ground truth so a re upgrade that fixes it is caught
                # (this subTest would then fail loudly, telling us to drop the filter/pin).
                self.assertIsNone(re.search(r"(?a:\S)", sep))
                self.assertIsNotNone(re.search(r"\B(?a:\S)", sep))
                # REAL: consistent, correct, and unaffected by a leading \B either way.
                self.assertIsNotNone(real.search(r"(?a:\S)", sep))
                self.assertIsNotNone(real.search(r"\B(?a:\S)", sep))
                self.assertIsNone(real.search(r"(?a:\s)", sep))
        # Control: an ordinary ascii whitespace byte is unaffected (not part of the bug's scope).
        self.assertIsNone(re.search(r"(?a:\S)", " "))
        self.assertIsNone(real.search(r"(?a:\S)", " "))

    def test_hex_escape_is_byte_level(self):
        # \xHH for HH >= 0x80 matches the raw byte, not chr(HH): re str \xe9 matches U+00E9 (é),
        # REAL \xe9 matches the single byte 0xE9 (which a well-formed str's UTF-8 of é never is).
        self.assertIsNotNone(real.compile(rb"\xe9").fullmatch(b"\xe9"))
        self.assertIsNone(real.compile(r"\xe9").search("é"))

    def test_word_boundary_on_empty_is_modern(self):
        # \B matches the empty string/region (no boundary to negate); \b does not. REAL's
        # by-definition behaviour == re >= 3.11 (re < 3.11 had the opposite quirk for \B).
        self.assertEqual(real.search(r"\B", "").span(), (0, 0))
        self.assertEqual(real.compile(r"\B").search("abc", 0, 0).span(), (0, 0))
        self.assertEqual(real.compile(r"2*C*\B").search("c20", 0, 0).span(), (0, 0))
        self.assertIsNone(real.search(r"\b", ""))
        self.assertIsNone(real.compile(r"\b").search("abc", 0, 0))

    def test_nullable_loop_keeps_last_nonempty_iteration(self):
        # (a*)* on "aa": REAL (like Perl/PCRE, which forbid repeating an empty match) reports
        # the last NON-empty iteration for group 1 ("aa"); re reports the final empty one ("").
        # Group 0 is identical in both.
        m = real.fullmatch(r"(a*)*", "aa")
        self.assertEqual(m.group(0), "aa")
        self.assertEqual(m.group(1), "aa")

    def test_rejected_by_design(self):
        # Each rejected for a documented reason (see the divergences page): backreferences and conditional
        # groups are non-regular (they would break linearity). \p{Gc}, \p{sc=...} and the standard binary
        # properties are now supported (see test_unicode_property_classes / test_unicode_binary_properties);
        # Bidi_Class (a real UAX44 property, enumerated not binary) is not tabulated, so it stays rejected.
        # (\N{NAME} is now supported — see the parity suite.)
        for pattern in [r"(a)\1", r"(?P=name)", r"(?(1)a|b)", r"\p{Bidi_Class=L}"]:
            with self.subTest(pattern=pattern):
                with self.assertRaises(real.error):
                    real.compile(pattern)

    def test_unicode_property_classes(self):
        # \p{General_Category} and \p{sc=Script} are a documented SUPERSET of stdlib re (which rejects \p
        # entirely with "bad escape \p"). GC and Script only in this version; (?a) does not restrict them.
        self.assertTrue(real.compile(r"\p{L}").fullmatch("é"))
        self.assertIsNone(real.compile(r"\p{L}").fullmatch("3"))
        self.assertTrue(real.compile(r"\p{Lu}").fullmatch("A"))
        self.assertIsNone(real.compile(r"\p{Lu}").fullmatch("a"))
        self.assertTrue(real.compile(r"\P{L}").fullmatch("3"))
        self.assertTrue(real.compile(r"\p{sc=Greek}").fullmatch("α"))
        self.assertTrue(real.compile(r"\p{Letter}").fullmatch("Z"))  # long alias
        self.assertTrue(real.compile(r"\pN").fullmatch("7"))          # single-letter form
        self.assertTrue(real.compile(r"(?a)\p{L}").fullmatch("é"))    # (?a) does not restrict \p{}
        self.assertEqual([m.group() for m in real.compile(r"\p{L}+").finditer("ab cd")], ["ab", "cd"])
        # inside a class, with negation and an enclosing [^...] on top
        self.assertTrue(real.compile(r"[\p{L}\d_]").fullmatch("5"))
        self.assertTrue(real.compile(r"[\P{L}]").fullmatch("3"))
        self.assertIsNone(real.compile(r"[\P{L}]").fullmatch("A"))
        self.assertTrue(real.compile(r"[^\P{L}]").fullmatch("A"))    # double negation == [\p{L}]
        # icase membership-then-fold — \p{Lu} folds to include lowercase; \P{Lu} folds then negates (P2)
        self.assertTrue(real.compile(r"(?i)\p{Lu}").fullmatch("a"))
        self.assertTrue(real.compile(r"(?i)\p{Lu}").fullmatch("K"))  # KELVIN folds to k
        self.assertTrue(real.compile(r"(?i)\p{Lu}").fullmatch("ı"))  # Turkish dotless-i folds with I, like re
        self.assertIsNone(real.compile(r"(?i)\P{Lu}").fullmatch("a"))

    def test_unicode_binary_properties(self):
        # The standard UCD binary properties (\p{Alphabetic}, \p{White_Space}, ...) are ALSO a documented
        # SUPERSET of stdlib re (which rejects \p entirely). No namespace of its own, same as PCRE2; a bare
        # name tries General_Category, then Script, then a binary property.
        self.assertTrue(real.compile(r"\p{White_Space}").fullmatch(" "))
        self.assertIsNone(real.compile(r"\p{White_Space}").fullmatch("x"))
        self.assertTrue(real.compile(r"\p{Alphabetic}").fullmatch("a"))
        self.assertIsNone(real.compile(r"\p{Alphabetic}").fullmatch("3"))
        self.assertTrue(real.compile(r"\p{Emoji}").fullmatch("\U0001F600"))  # GRINNING FACE, astral
        self.assertTrue(real.compile(r"\p{ WHITE-space }").fullmatch(" "))   # loose matching, same as GC/Script
        self.assertTrue(real.compile(r"\P{White_Space}").fullmatch("x"))    # negation
        self.assertIsNone(real.compile(r"\P{White_Space}").fullmatch(" "))
        self.assertTrue(real.compile(r"[\p{White_Space}x]").fullmatch("x")) # in-class
        self.assertTrue(real.compile(r"[\p{White_Space}x]").fullmatch(" "))
        # an explicit gc=/sc= namespace does NOT fall through to a binary property on a miss -- only a bare
        # name does (a misspelled explicit namespace should fail, not silently resolve elsewhere)
        with self.assertRaises(real.error):
            real.compile(r"\p{gc=White_Space}")

    def test_unicode_script_extensions(self):
        # \p{scx=...} (Script_Extensions, NOT a partition unlike Script) -- sc=/scx= share one name
        # resolver that accepts both the long name (Latin) and the short UAX24/ISO 15924 code (Latn).
        self.assertTrue(real.compile(r"\p{sc=Latn}").fullmatch("A"))          # short code, sc=
        digit = "٠"  # ARABIC-INDIC DIGIT ZERO: scx={Arab, Thaa, Yezi}
        self.assertTrue(real.compile(r"\p{scx=Arab}").fullmatch(digit))
        self.assertTrue(real.compile(r"\p{scx=Thaa}").fullmatch(digit))       # same cp, a different script's scx
        self.assertIsNone(real.compile(r"\p{scx=Latin}").fullmatch(digit))
        # scx has no bare-name form (PCRE2): the difference shows up in the RESULT, since sc=/scx= share
        # names now -- U+0300 is Inherited in the Script partition (excluded from bare \p{Grek}) but IS
        # in Greek's scx.
        combining_grave = "̀"
        self.assertIsNone(real.compile(r"\p{Grek}").fullmatch(combining_grave))
        self.assertTrue(real.compile(r"\p{scx=Grek}").fullmatch(combining_grave))

    def test_lookbehind_variable_width_accepted(self):
        # A bounded variable-width lookbehind is ACCEPTED -- beyond re/PCRE, which require a
        # fixed width.
        import re as _re
        with self.assertRaises(_re.error):
            _re.compile(r"(?<=a|bb)c")
        self.assertEqual(real.compile(r"(?<=a|bb)c").search("xbbc").group(), "c")


class TestCppIntegration(unittest.TestCase):
    """Tests for the C++ embedding helpers."""

    def test_get_include_resolves_to_the_headers(self):
        """get_include() points to the shipped C++ headers."""
        import os
        inc = real.get_include()
        self.assertTrue(os.path.isdir(inc))
        self.assertTrue(os.path.isfile(os.path.join(inc, "real", "real.hpp")))

    def test_get_config(self):
        """get_config() returns version, include path and C++ standard."""
        cfg = real.get_config()
        self.assertEqual(cfg["version"], real.__version__)
        self.assertEqual(cfg["include"], real.get_include())
        self.assertEqual(cfg["cxx_standard"], "c++20")


class TestPatternIntrospection(unittest.TestCase):
    """Pattern.groupindex and Pattern.flags expose the compiled pattern's metadata."""

    def test_groupindex(self):
        import re
        p = real.compile(r"(?P<year>\d{4})-(?P<month>\d{2})-(\d{2})")
        self.assertEqual(dict(p.groupindex), {"year": 1, "month": 2})  # group 3 is unnamed
        r = re.compile(r"(?P<year>\d{4})-(?P<month>\d{2})-(\d{2})", re.ASCII)
        self.assertEqual(dict(p.groupindex), dict(r.groupindex))       # parity with re
        self.assertEqual(dict(real.compile("abc").groupindex), {})

    def test_flags_reflect_compile_arguments(self):
        self.assertEqual(real.compile("x", real.I).flags & real.I, real.I)
        both = real.M | real.S
        self.assertEqual(real.compile("x", both).flags & both, both)
        self.assertEqual(real.compile("x").flags & real.I, 0)


class TestBackreferencesRejected(unittest.TestCase):
    """Backreferences are a documented limitation; the digit decoder must reject them with a
    clear error rather than silently mis-parsing (and never confuse them with octal escapes)."""

    def test_backref_rejected(self):
        for pattern in [r"(a)\1", r"(a)(b)\2", r"\1", r"\8", r"\12"]:
            with self.subTest(pattern=pattern):
                with self.assertRaises(real.error):
                    real.compile(pattern)

    def test_octal_still_compiles(self):
        # The twin of \1: octal escapes must NOT be rejected.
        for pattern in [r"\012", r"\101", r"\0", r"\000"]:
            with self.subTest(pattern=pattern):
                self.assertIsNotNone(real.compile(pattern))


class TestErrorHierarchy(unittest.TestCase):
    """real.error must subclass re.error so ``except re.error:`` catches REAL's errors."""

    def test_real_error_subclasses_re_error(self):
        import re
        self.assertTrue(issubclass(real.error, re.error))
        self.assertTrue(issubclass(real.error, Exception))

    def test_error_caught_at_every_level(self):
        """A compile error is catchable as real.error (native), re.error (the
        re-compatibility point) and Exception (the common base)."""
        import re
        with self.assertRaises(real.error):
            real.compile("(")
        with self.assertRaises(re.error):     # <- the re-compatibility guarantee
            real.compile("(")
        with self.assertRaises(Exception):
            real.compile("(")


class TestBindingCleanup(unittest.TestCase):
    """Anti-regression smoke for the binding's exception/cleanup surface: match
    construction and the scan paths must build correct objects (no leak, no UB) across
    search / finditer / findall / split / sub. The real oracle for the OOM paths is a
    clean ``make sanitize``; these pin the happy paths the new guards wrap."""

    def test_search_groups_intact(self):
        p = real.compile(r"(\d+)-(\d+)")
        m = p.search("a 12-34 b")
        self.assertIsNotNone(m)
        self.assertEqual(m.span(1), (2, 4))
        self.assertEqual(m.span(2), (5, 7))
        self.assertEqual(m.group(1), "12")

    def test_finditer_and_findall(self):
        p = real.compile(r"(\d+)-(\d+)")
        self.assertEqual([m.span() for m in p.finditer("1-2 3-4")], [(0, 3), (4, 7)])
        self.assertEqual(p.findall("1-2 3-4"), [("1", "2"), ("3", "4")])

    def test_split_and_sub(self):
        self.assertEqual(real.compile(r"\s+").split("a b  c"), ["a", "b", "c"])
        self.assertEqual(real.compile(r"(\d+)").sub(r"<\1>", "a1b22"), "a<1>b<22>")
        self.assertEqual(real.compile(r"\d+").sub(lambda m: f"[{m.group()}]", "a1b22"),
                         "a[1]b[22]")


class TestMatchRepr(unittest.TestCase):
    """Match.__repr__ mirrors re's format (only the module prefix differs)."""

    def test_repr_mirrors_re(self):
        import re
        for pattern, text in [(r"(\d+)", "abc 123 def"), (r".+", "x" * 100),
                              (r".+", "café crème")]:
            with self.subTest(pattern=pattern):
                pm = real.compile(pattern).search(text)
                rm = re.compile(pattern, re.ASCII).search(text)
                self.assertEqual(repr(pm).replace("real.Match", "re.Match"), repr(rm))

    def test_repr_bytes(self):
        import re
        pm = real.compile(rb"\w+").search(b"hello")
        rm = re.compile(rb"\w+").search(b"hello")
        self.assertEqual(repr(pm).replace("real.Match", "re.Match"), repr(rm))


class TestPatternValueSemantics(unittest.TestCase):
    """re.Pattern is a value type: equal text + flags compare equal and hash equal."""

    def test_equality_and_hash(self):
        a = real.compile("ab+", real.I)
        real.purge()
        b = real.compile("ab+", real.I)             # distinct object, same text + flags
        self.assertIsNot(a, b)
        self.assertEqual(a, b)
        self.assertEqual(hash(a), hash(b))
        self.assertEqual(len({a, b, real.compile("ab+", real.I)}), 1)  # usable as set/dict keys

    def test_inequality(self):
        a = real.compile("ab+", real.I)
        self.assertNotEqual(a, real.compile("ab+"))            # different flags
        self.assertNotEqual(a, real.compile("ab*", real.I))   # different text
        self.assertNotEqual(a, real.compile(b"ab+", real.I))  # str vs bytes

    def test_compare_to_non_pattern(self):
        a = real.compile("x")
        self.assertFalse(a == 42)   # NotImplemented -> Python falls back, no crash
        self.assertTrue(a != 42)
        self.assertNotEqual(a, "x")


class TestUnicodeAndConstructs(unittest.TestCase):
    r"""\u / \U code-point escapes, (?#...) comments, and clean rejections."""

    def test_unicode_escape_rejections(self):
        # str-mode \u / \U / \N{U+XXXX} work (see parity tests); these malformed forms are rejected
        # with clear errors — a surrogate, out of range, or incomplete hex.
        for pattern in [r"\uD800", r"\U00110000", r"\u00e", r"\U0001F60",
                        r"\N{U+D800}", r"\N{U+110000}", r"\N{U+}", r"\N{0041}"]:
            with self.subTest(pattern=pattern):
                with self.assertRaises(real.error):
                    real.compile(pattern)

    def test_unicode_escape_rejected_in_bytes(self):
        for pattern in [rb"\u0041", rb"\U0001F600", rb"[\u0041]"]:
            with self.subTest(pattern=pattern):
                with self.assertRaises(real.error):
                    real.compile(pattern)

    def test_non_ascii_class_member_accepted_in_str_mode(self):
        # A non-ASCII class member is accepted in str mode; bytes mode still rejects raw high bytes.
        self.assertIsNotNone(real.compile(r"[\u00e9]").search("é"))
        self.assertIsNotNone(real.compile(r"[é]").search("a café"))
        with self.assertRaises(real.error):
            real.compile(b"[\xc3\xa9]")  # bytes-mode: raw non-ASCII class member -> rejected

    def test_icase_unicode_escape_folds(self):
        # \u00e9 (é) is a code-point literal, so under icase it folds like the raw é -- it matches É
        # (Unicode case folding). A \xHH escape keeps byte provenance and does NOT fold.
        self.assertIsNotNone(real.compile(r"\u00e9", real.I).search("é"))
        self.assertIsNotNone(real.compile(r"\u00e9", real.I).search("É"))
        self.assertIsNone(real.compile(rb"\xe9", real.I).search(b"\xc9"))  # \xHH byte provenance: no fold

    def test_group_construct_rejections(self):
        for pattern in [r"(?P=name)", r"(?(1)a|b)", r"(a)(?(1)b)"]:
            with self.subTest(pattern=pattern):
                with self.assertRaises(real.error):
                    real.compile(pattern)


if __name__ == "__main__":
    unittest.main()
