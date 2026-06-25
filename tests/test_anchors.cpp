// Anchors, word boundaries and compilation flags (i, match, s), with Python's
// exact semantics for $ (matches before a final newline) and ^/$ in
// multiline mode.
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(caret_and_dollar_single_line)
{
  EXPECT_EQ(real::regex("^ab").search("abab").start(), 0U);
  EXPECT(!real::regex("b^a").search("ba"));
  EXPECT_EQ(real::regex("ab$").search("abab").start(), 2U);
  // Python: $ also matches just before a single trailing newline.
  EXPECT(real::regex("ab$").search("ab\n"));
  EXPECT(!real::regex("ab$").search("ab\n\n"));
  EXPECT(!real::regex("ab$").search("ab\nc"));
  EXPECT(real::regex("^$").fullmatch(""));
  EXPECT(real::regex("^$").search("\n")); // empty match before the \n
}

TEST(text_start_and_text_end_are_strict)
{
  EXPECT(real::regex("\\Aab").match("ab"));
  EXPECT(!real::regex("\\Aab").search("xab"));
  EXPECT(real::regex("ab\\Z").search("ab"));
  EXPECT(!real::regex("ab\\Z").search("ab\n")); // unlike $
}

TEST(multiline_mode)
{
  const real::regex rx("^b.d$", real::flags::multiline);
  EXPECT(rx.search("bad\nbed\nbud"));
  EXPECT_EQ(rx.search("xx\nbed\nyy").start(), 3U);
  EXPECT(!real::regex("^b.d$").search("xx\nbed\nyy"));
  // \A and \Z stay strict in multiline mode.
  EXPECT(!real::regex("\\Abed\\Z", real::flags::multiline).search("xx\nbed"));
}

TEST(word_boundaries)
{
  EXPECT_EQ(real::regex("\\bcat\\b").search("a cat!").start(), 2U);
  EXPECT(!real::regex("\\bcat\\b").search("concat"));
  EXPECT(!real::regex("\\bcat\\b").search("cats"));
  EXPECT(real::regex("\\bcat").match("cat"));    // boundary at start
  EXPECT(real::regex("cat\\b").search("a cat")); // boundary at end
  EXPECT(real::regex("\\Bcat").search("concat"));
  EXPECT(!real::regex("\\Bcat").search("a cat"));
  EXPECT_EQ(real::regex("\\b\\d+\\b").search("a 42 b")[0], "42"sv);
}

TEST(word_start_and_word_end_anchors)
{
  // \< asserts a word starts here (non-word/start on the left, word on the
  // right); \> asserts a word ends here. REAL extension beyond Python re.
  EXPECT_EQ(real::regex("\\<cat\\>").search("a cat here").start(), 2U);
  EXPECT(!real::regex("\\<cat\\>").search("category")); // not a whole word
  EXPECT(!real::regex("\\<cat\\>").search("concat"));
  EXPECT_EQ(real::regex("\\<\\w+").search("foo bar")[0], "foo"sv);
  EXPECT_EQ(real::regex("\\w+\\>").search(".foo.").start(), 1U);
  EXPECT(real::regex("\\<").match("abc"));                 // word start at the text start
  EXPECT_EQ(real::regex("\\>").search("abc").start(), 3U); // word end at text end
  EXPECT(!real::regex("\\<").search(" "));                 // no word, no word start
  // \< and \> are zero-width, like the other anchors.
  EXPECT_THROWS(real::regex("\\<?"), real::regex_error);
}

TEST(icase_flag)
{
  EXPECT(real::regex("hello", real::flags::icase).fullmatch("HeLLo"));
  EXPECT(real::regex("[a-z]+", real::flags::icase).fullmatch("AbC"));
  // Folding happens before negation: [^a] must reject both cases.
  EXPECT(!real::regex("[^a]", real::flags::icase).fullmatch("A"));
  EXPECT(real::regex("[^a]", real::flags::icase).fullmatch("b"));
  // Non-letters are untouched.
  EXPECT(real::regex("a-b", real::flags::icase).fullmatch("A-B"));
  EXPECT(!real::regex("hello").fullmatch("HELLO"));
}

TEST(dotall_flag)
{
  EXPECT(!real::regex("a.b").fullmatch("a\nb"));
  EXPECT(real::regex("a.b", real::flags::dotall).fullmatch("a\nb"));
  EXPECT(real::regex("a.b", real::flags::dotall).fullmatch("aéb"));
}

TEST(inline_flag_prefix)
{
  EXPECT(real::regex("(?i)hello").fullmatch("HELLO"));
  EXPECT(real::regex("(?im)^b$").search("a\nB"));
  EXPECT(real::regex("(?i)(?s)a.b").fullmatch("A\nB"));
  EXPECT(real::regex("(?a)x").fullmatch("x")); // ASCII: already the default
  const real::regex rx("(?is)a.b", real::flags::multiline);
  EXPECT(has_flag(rx.compile_flags(), real::flags::icase));
  EXPECT(has_flag(rx.compile_flags(), real::flags::dotall));
  EXPECT(has_flag(rx.compile_flags(), real::flags::multiline));
}

TEST(combined_flags_value)
{
  const real::regex rx("^a.b$", real::flags::icase | real::flags::dotall);
  EXPECT(rx.fullmatch("A\nB"));
  EXPECT_EQ(real::regex("x").compile_flags() == real::flags::none, true);
}

TEST(anchors_cannot_be_quantified)
{
  EXPECT_THROWS(real::regex("^*"), real::regex_error);
  EXPECT_THROWS(real::regex("$+"), real::regex_error);
  EXPECT_THROWS(real::regex("\\b?"), real::regex_error);
  EXPECT_THROWS(real::regex("^{2}"), real::regex_error);
  EXPECT(real::regex("(?:^)*a").search("a")); // grouped: allowed, like Python
  EXPECT(real::regex("^{").match("{"));       // invalid braces stay literal
}

TEST(literal_after_assertion_scans_to_next_occurrence)
{
  // Regression (differential fuzz): the pure-literal fast path must keep
  // scanning when a leading assertion fails at the first occurrence. \B2 on
  // "220": first '2' at 0 is a word boundary (\B fails); the match is the
  // second '2' at 1.
  EXPECT_EQ(real::regex("\\B2").search("220").start(), 1U);
  EXPECT_EQ(real::regex("\\Bb").search("bbc").start(), 1U);
  EXPECT(real::regex("\\bfoo\\b").search("a foo b")); // word-boundary literal
  EXPECT_EQ(real::regex("foo\\b").search("foofoo foo").start(), 3U);
  EXPECT(!real::regex("\\B2").search("2"));           // single '2': boundary, no match
}

TEST(prefilter_jump_resets_stale_seen_marks)
{
  // Regression (differential fuzz): after a position whose epsilon-explored
  // threads all die, the thread list keeps `seen` marks; when the prefilter
  // then jumps ahead and seeds there, those stale marks must not dedup away
  // the seed's own match thread. ^(\W?)[abc]?^ on "\n\nbb c\n" (multiline)
  // must still find the empty match at end-of-text (pos 7).
  const real::regex rx("^(\\W?)[abc]?^", real::flags::multiline | real::flags::dotall);
  const auto        all = rx.find_all("\n\nbb c\n");
  EXPECT_EQ(all.size(), 4U);
  EXPECT_EQ(all[3].start(), 7U);
  EXPECT_EQ(all[3].end(), 7U);
}

TEST(seed_threads_have_fresh_capture_slots)
{
  // Regression: a search seed must not inherit capture slots from dead
  // threads of earlier positions ((a) participates at pos 0, not in the
  // actual match at pos 2).
  auto match = real::regex("(a)*b").search("axb");
  EXPECT_EQ(match[0], "b"sv);
  EXPECT_EQ(match.start(1), real::npos); // Python: group(1) is None
}

TEST(search_keeps_seeding_after_dead_assertions)
{
  // Regression: a seed whose closure dies on a failed assertion must not
  // stop the search; ^ in multiline mode matches on a later line.
  EXPECT_EQ(real::regex("^b", real::flags::multiline).search("xx\nbed").start(), 3U);
  EXPECT_EQ(real::regex("(?m)^b$").search("a\nb").start(), 2U);
  EXPECT_EQ(real::regex("\\Axy").search("axy").matched(), false);
}

TEST(flag_errors)
{
  EXPECT_THROWS(real::regex("a(?i)b"), real::regex_error); // not at start
  EXPECT_THROWS(real::regex("(?i:a)"), real::regex_error); // scoped: unsupported
  EXPECT_THROWS(real::regex("(?u)a"), real::regex_error);  // unicode classes: unsupported
  EXPECT(real::regex("(?x) a b").fullmatch("ab"));         // verbose: supported
}

TEST(verbose_mode)
{
  // re.X: unescaped whitespace and # comments are ignored outside classes.
  EXPECT(real::regex("(?x) a b c").fullmatch("abc"));
  EXPECT(!real::regex("(?x) a b c").fullmatch("a bc"));             // whitespace in text is literal
  EXPECT(real::regex(R"((?x)\d{4} - \d{2})").fullmatch("2026-06"));
  EXPECT(real::regex("(?x) a # trailing comment\n b").fullmatch("ab"));
  EXPECT(real::regex("(?x)[ ]").fullmatch(" "));                    // space inside a class is literal
  EXPECT(real::regex(R"((?x)a\ b)").fullmatch("a b"));              // escaped space is literal
  EXPECT(real::regex("a b", real::flags::verbose).fullmatch("ab")); // constructor flag
  EXPECT(real::regex("(?ix) HELLO").fullmatch("hello"));            // combined with icase
}
