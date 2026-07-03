// Anchors, word boundaries and compilation flags (i, match, s), with Python's
// exact semantics for $ (matches before a final newline) and ^/$ in
// multiline mode.
#include <regex>
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

TEST(unicode_word_boundaries)
{
  // \b \B \< \> use Unicode word-ness in text mode (a code point ending/starting at the position).
  const std::string ete_phrase {"un été chaud"};
  const std::string cafe_hay   {"le café ici"};
  const std::string cafes      {"cafés"};
  EXPECT_EQ(real::regex("\\b\\w+\\b").search(ete_phrase)[0], "un"sv);
  EXPECT(real::regex("\\bété\\b").search(ete_phrase).matched()); // é is a word char both sides
  EXPECT(real::regex("\\bcafé\\b").search(cafe_hay).matched());
  EXPECT(!real::regex("\\bcafé\\b").search(cafes));              // 'é' then 's': no boundary inside
  // A decomposed "é" (e + U+0301): the combining mark is non-word, so \be\b matches the base 'e'.
  const std::string decomp2 {"e\xCC\x81 x"};                     // e + U+0301 (combining acute, non-word)
  EXPECT(real::regex("\\be\\b").search(decomp2).matched());
  EXPECT_EQ(real::regex("\\be\\b").search(decomp2).start(), 0U);
  // \< \> follow the same word-ness.
  const std::string phrase {"un été"};
  EXPECT(real::regex("\\<été\\>").search(phrase).matched());
  // \B is the negation: inside a Unicode word run.
  const std::string cafe {"café"};
  EXPECT_EQ(real::regex("\\B").search(cafe).start(), 1U); // between 'c' and 'a'
  // ASCII mode: word-ness stays byte-level (é is non-word under re.A), so 'caf' then é is a boundary.
  const std::string ascii_hay {"a caf\xC3\xA9"};
  EXPECT(real::regex("caf\\b", real::flags::ascii).search(ascii_hay).matched());
  const real::regex ascii_w   {"\\w+", real::flags::ascii};
  EXPECT_EQ(ascii_w.find_all(ascii_hay).size(), 2U); // "a", "caf" -- é splits the run under re.A
  // Bytes mode: \b is byte-level too (unchanged from the text-mode Unicode word-ness above).
  EXPECT(real::regex("caf\\b", real::flags::bytes).search(ascii_hay).matched());
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
  EXPECT_THROWS(real::regex("(?s:.)"), real::regex_error); // scoped dotall: not yet supported
  EXPECT_THROWS(real::regex("(?u)a"), real::regex_error);  // unicode classes: unsupported
  EXPECT(real::regex("(?x) a b").fullmatch("ab"));         // verbose: supported
  EXPECT(real::regex("(?i:a)").fullmatch("A"));            // scoped icase: supported
}

TEST(ecma_flag_dollar_end_only)
{
  // Default (Python): `$` (no multiline) matches at end OR just before a final `\n`.
  EXPECT(real::regex("foo$").search("foo\n").matched());
  EXPECT(real::regex("foo$").search("foo").matched());
  // ecma: `$` (no multiline) matches ONLY at the very end (ECMAScript `$`).
  EXPECT(!real::regex("foo$", real::flags::ecma).search("foo\n").matched());
  EXPECT(real::regex("foo$", real::flags::ecma).search("foo").matched());
  // The flag is orthogonal to multiline: `^`/`$` at line boundaries are unaffected.
  EXPECT(real::regex("foo$", real::flags::multiline).search("foo\nbar").matched());
  EXPECT(real::regex("foo$", real::flags::multiline | real::flags::ecma).search("foo\nbar").matched());
  // \Z is always end-only, with or without the flag (unchanged).
  EXPECT(!real::regex(R"(foo\Z)").search("foo\n").matched());

  // Differential: ecma `$` must match std::regex's ECMAScript `$` (end-only).
  const std::regex      ecma {"foo$"}; // std::regex defaults to ECMAScript
  for (const std::string subject : {"foo", "foo\n", "foobar", "\nfoo"}) {
    const bool std_match  {std::regex_search(subject, ecma)};
    const bool real_match {real::regex("foo$", real::flags::ecma).search(subject).matched()};
    EXPECT_EQ(real_match, std_match);
  }
}

TEST(ecma_flag_dot_excludes_cr)
{
  // Default (Python): `.` (no dotall) excludes `\n` only; `\r` is an ordinary character.
  EXPECT(real::regex(".").search("\r").matched());
  EXPECT(!real::regex(".").search("\n").matched());
  // ecma: `.` (no dotall) excludes `\n` AND `\r` (ECMAScript), at the byte level.
  EXPECT(!real::regex(".", real::flags::ecma).search("\r").matched());
  EXPECT(!real::regex(".", real::flags::ecma).search("\n").matched());
  EXPECT(real::regex(".", real::flags::ecma).search("a").matched());
  EXPECT(real::regex(".", real::flags::ecma).search("\t").matched()); // \t is not a line terminator
  // A byte of a multi-byte line terminator (U+2028 = E2 80 A8) is ordinary at byte level.
  EXPECT(real::regex(".", real::flags::ecma | real::flags::bytes).search("\xE2").matched());
  // dotall still wins: `.` matches everything, ecma notwithstanding.
  EXPECT(real::regex(".", real::flags::ecma | real::flags::dotall).search("\r").matched());

  // Differential vs std::regex<char> ECMAScript `.` over the line-terminator bytes.
  const std::regex ecma_dot {"."}; // ECMAScript: `.` excludes \n, \r, U+2028, U+2029
  for (const std::string subject : {"\r", "\n", "a", "\t"}) {
    const bool std_match  {std::regex_search(subject, ecma_dot)};
    const bool real_match {real::regex(".", real::flags::ecma | real::flags::bytes).search(subject).matched()};
    EXPECT_EQ(real_match, std_match);
  }
}

TEST(ecma_flag_anchor_escapes_are_literals)
{
  // REAL extensions: `\A \Z` (text-start/end) and `\< \>` (word-start/end) are anchors by default.
  EXPECT(real::regex(R"(\Aabc)").search("abc").matched());
  EXPECT(!real::regex(R"(\Aabc)").search("xabc").matched());   // \A anchors to the start
  EXPECT(real::regex(R"(foo\>)").search("foo bar").matched()); // \> = word end
  EXPECT(!real::regex(R"(foo\>)").search("foobar").matched());

  // Under ecma, ECMAScript has no such escapes — they are identity-escape LITERALS.
  EXPECT(real::regex(R"(\Aabc)", real::flags::ecma).search("Aabc").matched()); // \A == literal 'A'
  EXPECT(!real::regex(R"(\Aabc)", real::flags::ecma).search("abc").matched()); // not an anchor
  EXPECT(real::regex(R"(\Z)", real::flags::ecma).search("xZy").matched());     // \Z == literal 'Z'
  EXPECT(real::regex(R"(a\>b)", real::flags::ecma).search("a>b").matched());   // \> == literal '>'
  EXPECT(real::regex(R"(a\<b)", real::flags::ecma).search("a<b").matched());   // \< == literal '<'

  // `\a` is the bell (0x07) by default (Python); ECMAScript has no `\a` -> literal 'a' (both
  // outside and inside a class — parse_byte_escape is shared).
  const std::string bell(1, '\a');
  EXPECT(real::regex(R"(\a)").search(bell).matched());                   // default: bell
  EXPECT(real::regex(R"(\a)", real::flags::ecma).search("a").matched()); // ecma: literal 'a'
  EXPECT(real::regex(R"([\a]+)", real::flags::ecma).fullmatch("aaa").matched());
  EXPECT(!real::regex(R"(\a)", real::flags::ecma).search(bell).matched());

  // Differential: ecma `\<` / `\>` must match std::regex's ECMAScript identity escape (literal).
  for (const auto& [pat, subj] : std::vector<std::pair<std::string, std::string>> {
    {R"(\>)", "a>b"}, {R"(\<)", "a<b"}, {R"(x\>y)", "x>y"}}) {
    const bool real_match {real::regex(pat, real::flags::ecma | real::flags::bytes).search(subj).matched()};
    const bool std_match  {std::regex_search(subj, std::regex(pat, std::regex::ECMAScript))};
    EXPECT_EQ(real_match, std_match);
  }
}

TEST(ecma_flag_bracket_close_semantics)
{
  // Default (Python): a `]` right after `[` or `[^` is a literal class member.
  EXPECT(real::regex("[]]").search("]").matched());   // class containing ']'
  EXPECT(real::regex("[^]]").search("x").matched());  // not-']'
  EXPECT(!real::regex("[^]]").search("]").matched());

  // ecma: `]` always closes. `[]` is the empty class (matches nothing); `[^]` is its negation
  // (matches ANY character, including a newline — the ECMAScript "any incl. newline" idiom).
  EXPECT(!real::regex("[]", real::flags::ecma | real::flags::bytes).search("a").matched());  // empty
  EXPECT(real::regex("[^]", real::flags::ecma | real::flags::bytes).search("a").matched());  // any
  EXPECT(real::regex("[^]", real::flags::ecma | real::flags::bytes).search("\n").matched()); // incl. \n
  // `[^]]+` is `[^]` (any) then `]+`, NOT a not-']' class.
  EXPECT(real::regex("[^]]+", real::flags::ecma | real::flags::bytes).fullmatch("a]]").matched());
  EXPECT(!real::regex("[^]]+", real::flags::ecma | real::flags::bytes).fullmatch("ab").matched());
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

TEST(unicode_word_boundary_malformed)
{
  // word_before back-decodes the code point ending at the position; a malformed or misaligned run on
  // the left reads as non-word (REAL's documented internal policy -- re never sees malformed str).
  // These pin the malformed branch (a lone continuation, a run past the 3-byte back-scan cap, a
  // truncated lead, an overlong form) and the well-formed success path.
  const std::string lone_cont  {"\x80z"};             // lone continuation then a word char
  const std::string long_run   {"\x80\x80\x80\x80z"}; // > 3 continuation bytes (the back-scan cap)
  const std::string trunc_lead {"\xC3z"};             // 2-byte lead with no continuation
  const std::string overlong   {"\xC0\x80z"};         // overlong NUL then a word char
  for (const std::string* s : {&lone_cont, &long_run, &trunc_lead, &overlong}) {
    EXPECT(real::regex("\\bz").search(*s).matched()); // z (word) after a non-word run: a boundary
  }
  // Well-formed left side: a valid é ending exactly at the position is a word char, so there is no
  // boundary between é and z (both word) -- exercises the success path (decode ends exactly at pos).
  const std::string wellformed {"éz"};
  EXPECT(!real::regex("é\\bz").search(wellformed)); // no boundary inside a word run
  EXPECT(real::regex("\\Bz").search(wellformed).matched());
}

TEST(z_is_an_exact_alias_of_capital_z)
{
  // \z is an exact alias of \Z: end of the text, no MULTILINE interaction (Python 3.14's meaning).
  EXPECT(real::regex("a\\z").search("a").matched());
  EXPECT(!real::regex("a\\z").search("ab").matched());
  EXPECT(!real::regex("a\\z").search("a\nb").matched());
  EXPECT(!real::regex("a\\z").search("a\n").matched()); // \Z/\z are the ABSOLUTE end (unlike $, not before \n)
  // Byte-identical to \Z on the same inputs, MULTILINE included (neither is affected by it).
  for (const auto* subject : {"a", "ab", "a\n", "a\nb", "a\na"}) {
    EXPECT_EQ(real::regex("a\\z").search(subject).matched(), real::regex("a\\Z").search(subject).matched());
    EXPECT_EQ(real::regex("(?m)a\\z").search(subject).matched(), real::regex("(?m)a\\Z").search(subject).matched());
  }
}
