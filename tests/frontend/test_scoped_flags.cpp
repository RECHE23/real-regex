// Scoped inline flags `(?flags:...)` / `(?-flags:...)`. Each of the five flags is honoured per scope:
// verbose (x) changes tokenization, icase (i) and ascii (a) drive folding and the \w\d\s / \b tables,
// dotall (s) the dot, multiline (m) the ^/$ anchors. A scope reads from the flag-scope stack (parser)
// and the per-node effective_flags (compiler). These tests pin the scoping and its boundaries.
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

TEST(scoped_verbose_ignores_internal_whitespace)
{
  // Inside (?x:...), unescaped spaces are insignificant; outside, they are literal.
  EXPECT(real::regex("(?x:a )b").fullmatch("ab"));
  EXPECT(!real::regex("(?x:a )b").fullmatch("a b"));
  EXPECT(real::regex("(?x:a b)c d").fullmatch("abc d"));   // the space before 'd' is outside the scope
  EXPECT(!real::regex("(?x:a b)c d").fullmatch("abcd"));   // ... so it is significant
  EXPECT(real::regex("(?x:a\tb\n\f c)").fullmatch("abc")); // every whitespace kind is insignificant
}

TEST(scoped_verbose_ignores_hash_comments)
{
  // A `#` runs to the end of the line inside a verbose scope.
  EXPECT(real::regex("(?x:a #comment\n b)d").fullmatch("abd"));
  EXPECT(!real::regex("(?x:a #comment\n b)d").fullmatch("a b d"));
}

TEST(nested_minus_x_restores_significance)
{
  // (?-x:...) turns verbose back off for its body, even nested inside (?x:...).
  EXPECT(real::regex("(?x:(?-x:a b))").fullmatch("a b"));
  EXPECT(!real::regex("(?x:(?-x:a b))").fullmatch("ab"));
  // and a scope re-enabling x nested inside -x: the inner spaces are dropped, the outer has none
  EXPECT(real::regex("(?-x:a(?x: b )c)").fullmatch("abc"));
  EXPECT(!real::regex("(?-x:a(?x: b )c)").fullmatch("a bc"));
}

TEST(global_x_with_scoped_minus_x)
{
  // A global (?x) at the start, then a scoped (?-x:...) island of significance.
  EXPECT(real::regex("(?x)a (?-x:b c) d").fullmatch("ab cd"));
  EXPECT(!real::regex("(?x)a (?-x:b c) d").fullmatch("abcd")); // the inner space is significant
}

TEST(scoped_dotall_selects_the_dot_per_node)
{
  // (?s:.) matches \n inside the island; a . outside stays newline-excluding.
  EXPECT(real::regex("(?s:.)").fullmatch("\n"));
  EXPECT(!real::regex(".").fullmatch("\n"));
  EXPECT(real::regex("(?s:a.b)").fullmatch("a\nb"));
  EXPECT(!real::regex("(?s:a).").fullmatch("a\n")); // the trailing . is outside (?s:)
}

TEST(scoped_multiline_selects_the_anchors_per_node)
{
  // (?m:^ $) are line-relative inside the island; outside they are absolute. \Z / \z ignore m.
  EXPECT(real::regex("(?m:^b)").search("a\nb").matched());
  EXPECT(!real::regex("^b").search("a\nb").matched());
  EXPECT(real::regex("(?m:b$)").search("b\nc").matched());
  EXPECT(!real::regex("b$").search("b\nc").matched());
  EXPECT(!real::regex("(?m:b\\Z)").search("b\nc").matched());                        // \Z is insensitive to m
  EXPECT(!real::regex("(?-m:^b)", real::flags::multiline).search("a\nb").matched()); // negative island
}

TEST(a_dash_with_no_flag_is_still_an_error)
{
  EXPECT_THROWS(real::regex("(?-:a)"), real::regex_error); // a '-' with no flag after it
}

TEST(scoped_verbose_in_bytes_mode)
{
  using real::flags;
  // Verbose scoping works the same over raw bytes.
  EXPECT(real::regex("(?x:a )b", flags::bytes).fullmatch("ab"));
  EXPECT(!real::regex("(?x:a )b", flags::bytes).fullmatch("a b"));
}

TEST(scoped_icase_folds_only_within_the_scope)
{
  // (?i:...) folds cased literals to their whole orbit within the scope; outside it is case-sensitive.
  EXPECT(real::regex("(?i:é)").fullmatch("É"));    // full Unicode fold of a non-ASCII letter
  EXPECT(real::regex("(?i:k)").fullmatch("K"));    // k folds to the Kelvin sign
  EXPECT(real::regex("(?i:a)b").fullmatch("Ab"));
  EXPECT(!real::regex("(?i:a)b").fullmatch("aB")); // the 'b' outside the scope is case-sensitive
  EXPECT(real::regex("(?i:[k])").fullmatch("K"));  // a scoped-icase class folds too
  EXPECT(!real::regex("[k]").fullmatch("K"));
}

TEST(negative_icase_island_turns_folding_off)
{
  // In an icase-global pattern, (?-i:...) is an island of case-sensitivity.
  const real::regex rx("(?i:(?-i:k)K)");
  EXPECT(rx.fullmatch("kK"));
  EXPECT(!rx.fullmatch("KK"));                                        // the first k, inside -i, does not fold
  EXPECT(!real::regex("(?-i:K)", real::flags::icase).fullmatch("K")); // island kills the orbit
}

TEST(scoped_ascii_selects_the_shorthand_tables)
{
  // (?a:\w) is ASCII word; a bare \w in the same pattern stays Unicode — both tables coexist.
  EXPECT(!real::regex("(?a:\\w)").fullmatch("é"));
  EXPECT(real::regex("\\w").fullmatch("é"));
  EXPECT(real::regex("(?a:\\w)\\w").fullmatch("aé")); // ASCII then Unicode in ONE pattern
  EXPECT(!real::regex("(?a:\\d)").fullmatch("٠"));    // Arabic-Indic zero: not an ASCII digit
}

TEST(scoped_ascii_word_boundary)
{
  // \b word-ness follows the scope: under (?a:...) it is ASCII, so 'é' is a non-word char.
  EXPECT(real::regex("(?a:\\bx)").search("éx").matched());  // ASCII: boundary before x
  EXPECT(!real::regex("\\bx").search("éx").matched());      // Unicode: é and x both word, no boundary
  EXPECT(!real::regex("(?a:\\Bx)").search("éx").matched()); // \B is the complement
}

TEST(global_flags_prefix_removal_re2_parity)
{
  // (?i-s) at the very start: RE2 syntax for "enable i, disable s" from here on (RE2 parity,
  // measured 2026-07-17 vs libre2 11.0.0 — see .recovery/re2-parity-measurement.md and
  // fuzz/fuzz_re2.cpp's check_partial_full((?i-s)A.B, ...) match-parity pins). i is active
  // (A folds to a), s is inactive (the dot does not cross \n).
  EXPECT(real::regex("(?i-s)A.B").fullmatch("axb"));
  EXPECT(!real::regex("(?i-s)A.B").fullmatch("a\nb"));

  // Pure removal, no addition: (?-s) alone still disables dotall for the whole pattern —
  // measured vs libre2 first (both reject "\n", both accept any other single byte).
  EXPECT(!real::regex("(?-s).").fullmatch("\n"));
  EXPECT(real::regex("(?-s).").fullmatch("x"));

  // Chained global prefixes accumulate on the base scope, same as the add-only path.
  EXPECT(real::regex("(?i)(?-s)A.B").fullmatch("axb"));
  EXPECT(!real::regex("(?i)(?-s)A.B").fullmatch("a\nb"));
}

TEST(global_flags_prefix_dash_with_no_flag_is_an_error)
{
  // Mirrors a_dash_with_no_flag_is_still_an_error, but for the unscoped global-prefix parse
  // (parse_global_flags_prefix), which has its own accept('-')/loop — same failure message.
  const char* const msg = "missing flag";
  {
    bool threw = false;
    try {
      real::regex re("(?i-)a");
    }
    catch (const real::regex_error& e) {
      threw = true;
      EXPECT(std::string_view(e.what()).find(msg) != std::string_view::npos);
    }
    EXPECT(threw);
  }
  {
    bool threw = false;
    try {
      real::regex re("(?-)a"); // pure dash, no added letters either
    }
    catch (const real::regex_error& e) {
      threw = true;
      EXPECT(std::string_view(e.what()).find(msg) != std::string_view::npos);
    }
    EXPECT(threw);
  }
}

TEST(global_flags_prefix_removal_scoped_form_unaffected)
{
  // (?i-s:...) is the pre-existing SCOPED form (parse_group), not the global prefix — the new
  // global -removed parse must not change it: still requires a trailing ':', still pops back
  // to the outer scope after the group body.
  EXPECT(real::regex("(?i-s:A.B)").fullmatch("axb"));
  EXPECT(!real::regex("(?i-s:A.B)").fullmatch("a\nb"));
  EXPECT(!real::regex("(?i-s:a)b").fullmatch("AB")); // 'b' outside the scope: no icase, no removal either
}

TEST(ungreedy_inline_flag_swaps_default_greediness)
{
  // (?U) (RE2 parity, measured 2026-07-17 vs libre2 11.0.0 — see fuzz/fuzz_re2.cpp's
  // check_partial_capture((?U)(a+), ...) extent pins): a bare quantifier becomes lazy and the
  // explicit '?' re-inverts back to greedy. Resolved at parse time into node.lazy — the swap
  // covers +, *, ? and {n,m} alike.
  EXPECT_EQ(real::regex("(?U)a+").search("aaa")[0], std::string_view {"a"});
  EXPECT_EQ(real::regex("(?U)a+?").search("aaa")[0], std::string_view {"aaa"});
  EXPECT_EQ(real::regex("(?U)a*").search("aaa")[0], std::string_view {""});
  EXPECT_EQ(real::regex("(?U)a{1,3}").search("aaa")[0], std::string_view {"a"});
  // No-U behavior unchanged (regression guard for the explicit_q refactor).
  EXPECT_EQ(real::regex("a+").search("aaa")[0], std::string_view {"aaa"});
  EXPECT_EQ(real::regex("a+?").search("aaa")[0], std::string_view {"a"});
}

TEST(ungreedy_scoped_and_removal)
{
  // (?U:...) swaps only inside its scope; (?-U:...) restores greedy inside a (?U) pattern —
  // both measured vs libre2 (extents "a" / "aaa" respectively). The scoped forms work by
  // construction: 'U' joins is_flag_letter, and the scope push/pop does the rest.
  EXPECT_EQ(real::regex("(?U:a+)").search("aaa")[0], std::string_view {"a"});
  EXPECT_EQ(real::regex("(?U:a+)b").search("aaab")[0], std::string_view {"aaab"}); // lazy still reaches b
  EXPECT_EQ(real::regex("(?U)(?-U:a+)").search("aaa")[0], std::string_view {"aaa"});
}

TEST(ungreedy_constructor_flag)
{
  // flags::ungreedy as a constructor flag seeds the base scope exactly like a leading (?U).
  using real::flags;
  EXPECT_EQ(real::regex("a+", flags::ungreedy).search("aaa")[0], std::string_view {"a"});
  EXPECT_EQ(real::regex("a+?", flags::ungreedy).search("aaa")[0], std::string_view {"aaa"});
}

TEST(ungreedy_possessive_interaction_agrees_with_re2)
{
  // Under (?U) a bare quantifier is lazy, so the possessive '+' is never consumed and `(?U)a++`
  // fails "multiple repeat" — measured 2026-07-17: libre2 rejects it too ("bad repetition
  // operator: ++", RE2 has no possessives). Agreement, not a KNOWN-GAP.
  bool threw = false;
  try {
    real::regex re("(?U)a++");
  }
  catch (const real::regex_error& e) {
    threw = true;
    EXPECT(std::string_view(e.what()).find("multiple repeat") != std::string_view::npos);
  }
  EXPECT(threw);
  // Without U the native possessive is untouched.
  EXPECT_EQ(real::regex("a++").search("aaa")[0], std::string_view {"aaa"});
}

// compile_flags() reports the set IN FORCE, so it must agree with what the engine actually did.
// It did not always: the removal reached the parser's base scope (matching was correct) while the
// reported value came from the add-only inline_flags, so a global removal was silently over-reported.
TEST(compile_flags_reflects_a_global_removal)
{
  // Removal of a constructor flag: honoured by matching AND absent from the reported set.
  const real::regex minus_i("(?-i)a", real::flags::icase);
  EXPECT(!has_flag(minus_i.compile_flags(), real::flags::icase));
  EXPECT(!minus_i.search("A").matched()); // the engine agrees: case-sensitive
  EXPECT(minus_i.search("a").matched());

  const real::regex minus_s("(?-s).", real::flags::dotall);
  EXPECT(!has_flag(minus_s.compile_flags(), real::flags::dotall));
  EXPECT(!minus_s.search("\n").matched());

  // Add-then-remove in one group nets to removed, and only the named flag is touched.
  const real::regex both("(?im-i)a", real::flags::none);
  EXPECT(!has_flag(both.compile_flags(), real::flags::icase));
  EXPECT(has_flag(both.compile_flags(), real::flags::multiline));

  // A removal that names a flag nobody set is a no-op, not a corruption of the rest.
  const real::regex noop("(?-x)a", real::flags::icase | real::flags::multiline);
  EXPECT(has_flag(noop.compile_flags(), real::flags::icase));
  EXPECT(has_flag(noop.compile_flags(), real::flags::multiline));
  EXPECT(noop.search("A").matched());

  // Additions still report, unchanged by this arc.
  EXPECT(has_flag(real::regex("(?i)a").compile_flags(), real::flags::icase));
}

// static_regex computes effective_flags on its own path (a constexpr double-parse), so it needs its
// own pin: the two storages must answer the same thing for the same pattern.
TEST(static_regex_compile_flags_reflects_a_global_removal)
{
  static constexpr real::static_regex<"(?-i)a", real::flags::icase> minus_i;
  // Asserted at COMPILE time: static_storage builds effective_flags in a constant expression, so a
  // regression here is a build failure, not a test failure.
  static_assert(!has_flag(minus_i.compile_flags(), real::flags::icase),
                "static_storage must clear a global removal from effective_flags");
  static_assert(has_flag(real::static_regex<"(?i)a"> {}.compile_flags(), real::flags::icase),
                "an addition must still be reported");
  EXPECT(!minus_i.search("A").matched());
  EXPECT(minus_i.search("a").matched());
}

TEST(unknown_flag_letter_is_diagnosed_as_unknown_flag)
{
  // Once a flags group has started (a valid letter or '-'), an unknown letter is
  // "unknown flag" at that letter — not "global flags not at the start" and not
  // "missing flag". Oracle: CPython _parse_flags; unknown flag takes
  // precedence over placement (a(?iz)b has both faults). The list is the unknown-flag
  // cases only: REAL's set is not re's (it adds U, it has no u/L).
  const char* const unknown_flag_cases[] = {
    "(?iz)a", "(?iz:a)", "(?i-z)a", "(?i-z:a)", "(?imz)a", "(?i-mz)a", "a(?iz)b",
  };
  for (const char* pattern : unknown_flag_cases) {
    bool threw {false};
    try {
      real::regex re {pattern};
    }
    catch (const real::regex_error& e) {
      threw = true;
      EXPECT(std::string_view(e.what()).find("unknown flag") != std::string_view::npos);
    }
    EXPECT(threw);
  }

  // github.com/RECHE23/real-regex/issues/6: (?iz)a used to steal the placement message.
  try {
    real::regex re {"(?iz)a"};
    EXPECT(false);
  }
  catch (const real::regex_error& e) {
    EXPECT(e.position() == 3);
    EXPECT(std::string_view(e.what()).find("unknown flag") != std::string_view::npos);
  }

  // z first after '?' never enters the flags parser: still unknown extension (re agrees).
  try {
    real::regex re {"(?zi)a"};
    EXPECT(false);
  }
  catch (const real::regex_error& e) {
    EXPECT(std::string_view(e.what()).find("unknown extension") != std::string_view::npos);
    EXPECT(std::string_view(e.what()).find("unknown flag") == std::string_view::npos);
  }

  // Genuine misplaced global / missing-flag stay those messages.
  try {
    real::regex re {"a(?i)b"};
    EXPECT(false);
  }
  catch (const real::regex_error& e) {
    EXPECT(std::string_view(e.what()).find("global flags not at the start of the expression") !=
           std::string_view::npos);
  }
  try {
    real::regex re {"(?i-)a"};
    EXPECT(false);
  }
  catch (const real::regex_error& e) {
    EXPECT(std::string_view(e.what()).find("missing flag") != std::string_view::npos);
    EXPECT(std::string_view(e.what()).find("missing -, : or )") == std::string_view::npos);
  }

  // The flag set itself is unchanged: U still compiles; a well-formed scoped group still does.
  EXPECT(real::regex("(?U)a").fullmatch("a").matched());
  EXPECT(real::regex("(?i:a)").fullmatch("A").matched());
  // Leading-global prefix must still backtrack on (?P rather than call P an unknown flag.
  EXPECT(real::regex("(?P<n>a)").fullmatch("a").matched());
}

TEST(flags_terminator_is_named_not_placement)
{
  // github.com/RECHE23/real-regex/issues/6 follow-up: after a flags run, re's _parse_flags
  // asks whether the next byte is a terminator. Recycling the placement message for a
  // non-terminator is a false sentence — (?i*) at the start of the pattern IS at the start.
  // Four outcomes, pinned so swapping fail_if_unknown_flag past the terminator check
  // (or collapsing the four into placement) goes red.
  const char* const placement     = "global flags not at the start of the expression";
  const char* const missing_term  = "missing -, : or )";
  const char* const missing_flag  = "missing flag";
  const char* const missing_colon = "missing :";

  const auto expect_msg_at = [](const char* pattern, const char* needle, std::size_t pos,
                                const char* not_a, const char* not_b) {
                               try {
                                 real::regex re {pattern};
                                 EXPECT(false);
                               }
                               catch (const real::regex_error& e) {
                                 EXPECT(e.position() == pos);
                                 const std::string_view what {e.what()};
                                 EXPECT(what.find(needle) != std::string_view::npos);
                                 EXPECT(what.find(not_a) == std::string_view::npos);
                                 EXPECT(what.find(not_b) == std::string_view::npos);
                               }
                             };

  // Well-formed unscoped, not at the start: placement, reported at the `(`. It is the GROUP that is
  // misplaced, so re points at where the group begins and not at the byte the scan stopped on —
  // three characters later here, and arbitrarily further on a longer flag run.
  expect_msg_at("a(?i)b", placement, 1, missing_term, missing_flag);

  // Non-terminator after added flags — including at the start of the pattern.
  expect_msg_at("(?i*)", missing_term, 3, placement, missing_flag);
  expect_msg_at("(?i7)", missing_term, 3, placement, missing_flag);
  expect_msg_at("(?i@)", missing_term, 3, placement, missing_flag);
  expect_msg_at("(?i", missing_term, 3, placement, missing_flag);
  expect_msg_at("a(?i*)b", missing_term, 4, placement, missing_flag);

  // Dash with no flag letter: missing flag, not missing -, : or ).
  expect_msg_at("(?i-*)a", missing_flag, 4, missing_term, placement);
  expect_msg_at("(?i-)", missing_flag, 4, missing_term, placement);

  // After a removal letter, a non-colon (RE2 still accepts ')' as global — that's placement).
  expect_msg_at("(?i-s*)", missing_colon, 5, placement, missing_term);
  expect_msg_at("(?i-s", missing_colon, 5, placement, missing_term);
  // Same rule, and the message here is deliberately NOT re's: re calls `(?i-s)` a `missing :` while
  // this accepts the RE2 global form, so only the placement half is shared. The position follows
  // this message rather than re's — a misplaced group is reported where the group starts.
  expect_msg_at("x(?i-s)y", placement, 1, missing_colon, missing_term);

  // Leading well-formed (?i) / RE2 (?i-s) still compile: the prefix took them.
  EXPECT(real::regex("(?i)a").fullmatch("A").matched());
  EXPECT(real::regex("(?i-s)A.B").fullmatch("axb"));
}
