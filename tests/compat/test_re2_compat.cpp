// real::compat::re2 (RE2 drop-in) -- REAL-only coverage (no RE2 dependency; RE2 is the
// test-time oracle for the differential proof, done separately against a real, locally-installed
// RE2, not part of this tracked suite). This file exercises the wrapper's own contract: option
// wiring, the no-fallback/no-exception error shape, Arg extraction, Consume/Replace semantics,
// QuoteMeta, and Set.
#include <clocale>
#if defined(__APPLE__)
#  include <xlocale.h> // newlocale/uselocale: not declared by <clocale> under libstdc++ on Darwin
#endif
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#if !defined(_WIN32)
#  include <unistd.h> // dup/dup2: stderr captured for the log_errors tests
#endif

#include <sciforge/test/framework.hpp>
#include "real/compat/re2/re2.hpp"

namespace rc2 = real::compat::re2;
using namespace std::string_view_literals;

TEST(fullmatch_extracts_typed_groups)
{
  std::string s;
  int         i {};
  EXPECT(rc2::RE2::FullMatch("ruby:1234", "(\\w+):(\\d+)", &s, &i));
  EXPECT_EQ(s, std::string {"ruby"});
  EXPECT_EQ(i, 1234);
}

TEST(fullmatch_fails_not_enough_subpatterns)
{
  std::string s;
  EXPECT(!rc2::RE2::FullMatch("ruby:1234", "\\w+:\\d+", &s));
}

TEST(fullmatch_fails_on_conversion_failure)
{
  int i {};
  EXPECT(!rc2::RE2::FullMatch("ruby", "(.*)", &i));
}

TEST(fullmatch_zero_args_just_tests)
{
  EXPECT(rc2::RE2::FullMatch("abc", "a.c"));
  EXPECT(!rc2::RE2::FullMatch("abx", "a.c"));
}

TEST(fullmatch_partial_does_not_match_a_substring)
{
  EXPECT(!rc2::RE2::FullMatch("xabcx", "abc"));
  EXPECT(rc2::RE2::PartialMatch("xabcx", "abc"));
}

TEST(arg_parses_bool_int_double_string_view)
{
  bool             b {};
  int              i {};
  long             l {};
  unsigned         u {};
  double           d {};
  float            f {};
  std::string_view sv;
  EXPECT(rc2::RE2::FullMatch("true", "(\\w+)", &b));
  EXPECT(b);
  EXPECT(rc2::RE2::FullMatch("false", "(\\w+)", &b));
  EXPECT(!b);
  EXPECT(rc2::RE2::FullMatch("1", "(\\w+)", &b));
  EXPECT(b);
  EXPECT(!rc2::RE2::FullMatch("nope", "(\\w+)", &b));
  EXPECT(rc2::RE2::FullMatch("-42", "(-?\\d+)", &i));
  EXPECT_EQ(i, -42);
  EXPECT(rc2::RE2::FullMatch("123456789", "(\\d+)", &l));
  EXPECT_EQ(l, 123456789L);
  EXPECT(rc2::RE2::FullMatch("42", "(\\d+)", &u));
  EXPECT_EQ(u, 42U);
  EXPECT(rc2::RE2::FullMatch("3.5", "([0-9.]+)", &d));
  EXPECT(d > 3.49 && d < 3.51);
  EXPECT(rc2::RE2::FullMatch("2.5", "([0-9.]+)", &f));
  EXPECT(f > 2.49F && f < 2.51F);
  EXPECT(rc2::RE2::FullMatch("hello", "(\\w+)", &sv));
  EXPECT_EQ(std::string(sv), std::string {"hello"});
}

TEST(arg_nullptr_skips_a_group)
{
  std::string s;
  EXPECT(rc2::RE2::FullMatch("a:b", "(\\w+):(\\w+)", nullptr, &s));
  EXPECT_EQ(s, std::string {"b"});
}

TEST(arg_numeric_parse_fails_on_a_non_participating_group)
{
  // A plain numeric Arg (not std::optional<T>) against an unset optional group: the empty
  // submatch can't parse as a number, so the whole match fails -- real RE2's own behavior too
  // (its optional<T>-specific handling is a distinct, unimplemented-here overload; see the file
  // header's scope note).
  std::string s;
  int         i {};
  EXPECT(!rc2::RE2::FullMatch("a", R"((a)(?:x(\d+))?)", &s, &i));
  double d      {};
  EXPECT(!rc2::RE2::FullMatch("a", R"((a)(?:x([\d.]+))?)", &s, &d));
}

TEST(arg_floating_parse_fails_on_trailing_garbage)
{
  double d {};
  EXPECT(!rc2::RE2::FullMatch("1.2.3", R"(([\d.]+))", &d));
}

TEST(arg_floating_parse_rejects_overflow_of_its_own_type)
{
  // The conversion must match the destination, not always go through double: 1e40 fits a double and
  // does not fit a float, so a float destination has to REFUSE it rather than store an infinity.
  float  f {};
  double d {};
  EXPECT(!rc2::RE2::FullMatch("1e40", R"(([\de+.]+))", &f));
  EXPECT(rc2::RE2::FullMatch("1e40", R"(([\de+.]+))", &d));
  EXPECT(d > 9e39 && d < 1.1e40);
  EXPECT(!rc2::RE2::FullMatch("1e400", R"(([\de+.]+))", &d));
  // A value the type does hold still parses, so the stricter conversion costs nothing legitimate.
  EXPECT(rc2::RE2::FullMatch("1e30", R"(([\de+.]+))", &f));
  EXPECT(f > 9e29F && f < 1.1e30F);
}

TEST(arg_default_constructor_and_explicit_parser)
{
  const rc2::Arg noop;
  EXPECT(noop.parse("anything", 8));

  int                    x {};
  const rc2::Arg::Parser length_into_x {[](const char* /*text*/, std::size_t length, void* dest) {
                                          *static_cast<int*>(dest) = static_cast<int>(length);
                                          return true;
                                        }};
  const rc2::Arg custom(&x, length_into_x);
  EXPECT(custom.parse("abc", 3));
  EXPECT_EQ(x, 3);
}

TEST(re2_ok_error_pattern_options_accessors)
{
  const rc2::RE2 re("(a)(b)(c)");
  EXPECT(re.ok());
  EXPECT_EQ(re.error_code(), rc2::RE2::ErrorCode::NoError);
  EXPECT(re.error().empty());
  EXPECT_EQ(re.pattern(), std::string {"(a)(b)(c)"});
  EXPECT_EQ(re.NumberOfCapturingGroups(), 3);
}

TEST(re2_bad_pattern_reports_syntax_error_not_a_throw)
{
  const rc2::RE2 re("(unterminated");
  EXPECT(!re.ok());
  EXPECT_EQ(re.error_code(), rc2::RE2::ErrorCode::ErrorSyntax);
  EXPECT(!re.error().empty());
  EXPECT_EQ(re.NumberOfCapturingGroups(), 0);
}

TEST(re2_backreference_reports_unsupported_error)
{
  // A well-formed pattern REAL's linear engine cannot represent -- error_kind::unsupported.
  const rc2::RE2 re(R"((a)\1)");
  EXPECT(!re.ok());
  EXPECT_EQ(re.error_code(), rc2::RE2::ErrorCode::ErrorUnsupported);
}

TEST(re2_constructs_from_a_bare_string_view)
{
  const rc2::RE2 re(std::string_view {"a.c"});
  EXPECT(re.ok());
  EXPECT(rc2::RE2::FullMatch("abc", re));
}

TEST(re2_options_accessor_returns_construction_options)
{
  rc2::RE2::Options opt;
  opt.set_case_sensitive(false);
  const rc2::RE2 re("a", opt);
  EXPECT(!re.options().case_sensitive());
}

TEST(fullmatch_partial_find_and_consume_fail_cleanly_on_a_bad_re)
{
  const rc2::RE2    bad("(unterminated");
  std::string_view  input {"abc"};
  EXPECT(!rc2::RE2::FullMatch("abc", bad));
  EXPECT(!rc2::RE2::PartialMatch("abc", bad));
  EXPECT(!rc2::RE2::FindAndConsume(&input, bad));
  EXPECT_EQ(std::string(input), std::string {"abc"});
}

TEST(find_and_consume_rejects_null_input)
{
  EXPECT(!rc2::RE2::FindAndConsume(nullptr, "a"));
}

TEST(re2_constructs_implicitly_from_literal_and_std_string)
{
  // The whole point of the three-constructor split: a string literal or std::string argument
  // converts to a temporary RE2 in one implicit hop, so FullMatch(text, "pattern", …) compiles.
  std::string pat {"a.c"};
  EXPECT(rc2::RE2::FullMatch("abc", "a.c"));
  EXPECT(rc2::RE2::FullMatch("abc", pat));
}

TEST(options_case_sensitive_false_maps_to_icase)
{
  rc2::RE2::Options opt;
  opt.set_case_sensitive(false);
  EXPECT(!opt.case_sensitive());
  const rc2::RE2 re("HELLO", opt);
  EXPECT(re.ok());
  EXPECT(rc2::RE2::FullMatch("hello", re));
  EXPECT(rc2::RE2::FullMatch("HELLO", re));
}

TEST(options_dot_nl_maps_to_dotall)
{
  rc2::RE2::Options opt;
  opt.set_dot_nl(true);
  const rc2::RE2 re(".", opt);
  EXPECT(re.ok());
  EXPECT(rc2::RE2::FullMatch("\n", re));
}

TEST(options_literal_escapes_the_pattern)
{
  rc2::RE2::Options opt;
  opt.set_literal(true);
  EXPECT(opt.literal());
  const rc2::RE2 re("a.b*c", opt);
  EXPECT(re.ok());
  EXPECT(rc2::RE2::FullMatch("a.b*c", re));
  EXPECT(!rc2::RE2::FullMatch("axbxxc", re));
}

TEST(options_longest_match_changes_partial_match_boundary)
{
  rc2::RE2::Options first_opt;
  const rc2::RE2    first("a|ab", first_opt);
  std::string       s1 {"xabx"};
  EXPECT(rc2::RE2::Replace(&s1, first, "#"));
  EXPECT_EQ(s1, std::string {"x#bx"}); // leftmost-first: "a" only

  rc2::RE2::Options longest_opt;
  longest_opt.set_longest_match(true);
  EXPECT(longest_opt.longest_match());
  const rc2::RE2 longest("a|ab", longest_opt);
  std::string    s2 {"xabx"};
  EXPECT(rc2::RE2::Replace(&s2, longest, "#"));
  EXPECT_EQ(s2, std::string {"x#x"}); // leftmost-longest: "ab"
}

TEST(options_every_getter_setter_round_trips)
{
  rc2::RE2::Options opt;
  opt.set_max_mem(1234);
  EXPECT_EQ(opt.max_mem(), std::int64_t {1234});
  opt.set_encoding(rc2::RE2::Options::EncodingLatin1);
  EXPECT_EQ(opt.encoding(), rc2::RE2::Options::EncodingLatin1);
  opt.set_log_errors(false);
  EXPECT(!opt.log_errors());
  opt.set_perl_classes(true);
  EXPECT(opt.perl_classes());
  opt.set_word_boundary(true);
  EXPECT(opt.word_boundary());
  opt.set_one_line(true);
  EXPECT(opt.one_line());
}

TEST(options_unsupported_ones_reject_cleanly)
{
  {
    rc2::RE2::Options opt;
    opt.set_posix_syntax(true);
    const rc2::RE2 re("a", opt);
    EXPECT(!re.ok());
    EXPECT_EQ(re.error_code(), rc2::RE2::ErrorCode::ErrorUnsupported);
  }
  {
    rc2::RE2::Options opt;
    opt.set_never_nl(true);
    const rc2::RE2 re(".", opt);
    EXPECT(!re.ok());
  }
  {
    rc2::RE2::Options opt;
    opt.set_never_capture(true);
    const rc2::RE2 re("(a)", opt);
    EXPECT(!re.ok());
  }
  {
    rc2::RE2::Options opt;
    opt.set_encoding(rc2::RE2::Options::EncodingLatin1);
    const rc2::RE2 re("a", opt);
    EXPECT(!re.ok());
  }
}

TEST(consume_is_prefix_anchored_and_shrinks_input)
{
  std::string_view  input {"123xyz"};
  int               n     {};
  EXPECT(rc2::RE2::Consume(&input, R"((\d+))", &n));
  EXPECT_EQ(n, 123);
  EXPECT_EQ(std::string(input), std::string {"xyz"});
}

TEST(consume_fails_when_match_is_not_at_the_start)
{
  std::string_view  input {"xyz123"};
  int               n     {};
  EXPECT(!rc2::RE2::Consume(&input, R"((\d+))", &n));
  EXPECT_EQ(std::string(input), std::string {"xyz123"}); // unchanged on failure
}

TEST(consume_rejects_null_input_or_bad_re)
{
  int              n     {};
  const rc2::RE2   bad("(unterminated");
  std::string_view input {"123"};
  EXPECT(!rc2::RE2::Consume(nullptr, R"((\d+))", &n));
  EXPECT(!rc2::RE2::Consume(&input, bad, &n));
}

TEST(find_and_consume_tokenizes_a_whole_string)
{
  std::string_view  input {"12 34 56"};
  int               n     {};
  std::vector<int>  vals;
  const rc2::RE2    num(R"((\d+))");
  while (rc2::RE2::FindAndConsume(&input, num, &n)) {
    vals.push_back(n);
  }
  EXPECT_EQ(vals.size(), std::size_t {3});
  EXPECT_EQ(vals[0], 12);
  EXPECT_EQ(vals[1], 34);
  EXPECT_EQ(vals[2], 56);
  EXPECT(input.empty());
}

TEST(find_and_consume_respects_longest_match)
{
  rc2::RE2::Options opt;
  opt.set_longest_match(true);
  const rc2::RE2   re("a|ab", opt);
  std::string_view input {"ab"};
  EXPECT(rc2::RE2::FindAndConsume(&input, re));
  EXPECT(input.empty()); // consumed "ab", not just "a"
}

TEST(replace_rewrites_the_first_match_only)
{
  std::string s {"a1 b2 c3"};
  EXPECT(rc2::RE2::Replace(&s, R"((\w)(\d))", "\\2\\1"));
  EXPECT_EQ(s, std::string {"1a b2 c3"});
}

TEST(replace_returns_false_and_leaves_str_unchanged_on_no_match)
{
  std::string s {"hello"};
  EXPECT(!rc2::RE2::Replace(&s, "zzz", "X"));
  EXPECT_EQ(s, std::string {"hello"});
}

TEST(replace_rejects_null_str_or_bad_re)
{
  std::string    s {"hello"};
  const rc2::RE2 bad("(unterminated");
  EXPECT(!rc2::RE2::Replace(nullptr, "hello", "X"));
  EXPECT(!rc2::RE2::Replace(&s, bad, "X"));
}

TEST(replace_malformed_rewrite_fails_leaves_str_unchanged)
{
  std::string s {"a1"};
  EXPECT(!rc2::RE2::Replace(&s, R"((\w)(\d))", "\\9")); // group 9 out of range
  EXPECT_EQ(s, std::string {"a1"});
  EXPECT(!rc2::RE2::Replace(&s, R"((\w)(\d))", "trailing\\"));
  EXPECT_EQ(s, std::string {"a1"});
  EXPECT(!rc2::RE2::Replace(&s, R"((\w)(\d))", "\\q")); // invalid escape
  EXPECT_EQ(s, std::string {"a1"});
}

TEST(replace_backslash_escape_is_literal)
{
  std::string s {"a1"};
  EXPECT(rc2::RE2::Replace(&s, R"((\w)(\d))", "\\\\\\1"));
  EXPECT_EQ(s, std::string {"\\a"});
}

TEST(global_replace_rewrites_every_match)
{
  std::string s {"a1 b2 c3"};
  const int   n {rc2::RE2::GlobalReplace(&s, R"((\w)(\d))", "\\2\\1")};
  EXPECT_EQ(n, 3);
  EXPECT_EQ(s, std::string {"1a 2b 3c"});
}

TEST(global_replace_zero_matches_returns_zero_unchanged)
{
  std::string s {"no digits here"};
  const int   n {rc2::RE2::GlobalReplace(&s, R"((\d+))", "#")};
  EXPECT_EQ(n, 0);
  EXPECT_EQ(s, std::string {"no digits here"});
}

TEST(global_replace_malformed_rewrite_fails_whole_call)
{
  std::string s {"a1 b2"};
  const int   n {rc2::RE2::GlobalReplace(&s, R"((\w)(\d))", "\\9")};
  EXPECT_EQ(n, 0);
  EXPECT_EQ(s, std::string {"a1 b2"});
}

TEST(global_replace_rejects_null_str_or_bad_re)
{
  std::string    s {"a1"};
  const rc2::RE2 bad("(unterminated");
  EXPECT_EQ(rc2::RE2::GlobalReplace(nullptr, "a1", "#"), 0);
  EXPECT_EQ(rc2::RE2::GlobalReplace(&s, bad, "#"), 0);
}

TEST(global_replace_respects_longest_match)
{
  rc2::RE2::Options opt;
  opt.set_longest_match(true);
  const rc2::RE2 re("a|ab", opt);
  std::string    s {"ab ab"};
  const int      n {rc2::RE2::GlobalReplace(&s, re, "#")};
  EXPECT_EQ(n, 2);
  EXPECT_EQ(s, std::string {"# #"});
}

// Hardening #2 (v7.49): GlobalReplace must skip a zero-width match that abuts the end of the
// previous accepted match — real RE2's empty-advance policy. Dumping find_iter raw double-replaced
// (e.g. a* on "aa" → "##" instead of "#"). Oracle = true libre2 via fuzz/fuzz_re2.cpp; these pin
// the RE2-correct outcomes without a libre2 link in the tracked suite.
TEST(global_replace_skips_empty_match_abutting_previous_end)
{
  {
    std::string s {"aa"};
    EXPECT_EQ(rc2::RE2::GlobalReplace(&s, "a*", "#"), 1);
    EXPECT_EQ(s, std::string {"#"});
  }
  {
    std::string s {"a"};
    EXPECT_EQ(rc2::RE2::GlobalReplace(&s, "a*", "#"), 1);
    EXPECT_EQ(s, std::string {"#"});
  }
  {
    std::string s {"aaa"};
    EXPECT_EQ(rc2::RE2::GlobalReplace(&s, "a*", "#"), 1);
    EXPECT_EQ(s, std::string {"#"});
  }
  {
    std::string s {"aab"};
    EXPECT_EQ(rc2::RE2::GlobalReplace(&s, "a*", "#"), 2);
    EXPECT_EQ(s, std::string {"#b#"});
  }
}

TEST(global_replace_still_applies_non_abutting_empty_matches)
{
  // a* on "bbb": empty matches at 0,1,2,3 are non-abutting (each advanced past the prior empty's
  // end by find_iter) — must still rewrite (RE2 yields #b#b#b#).
  std::string s {"bbb"};
  EXPECT_EQ(rc2::RE2::GlobalReplace(&s, "a*", "#"), 4);
  EXPECT_EQ(s, std::string {"#b#b#b#"});
}

TEST(global_replace_empty_subject_nullable_is_one_replacement)
{
  std::string s;
  EXPECT_EQ(rc2::RE2::GlobalReplace(&s, "a*", "#"), 1);
  EXPECT_EQ(s, std::string {"#"});
}

TEST(quote_meta_escapes_metacharacters)
{
  const std::string q {rc2::RE2::QuoteMeta("a.b*c")};
  EXPECT(rc2::RE2::FullMatch("a.b*c", q));
  EXPECT(!rc2::RE2::FullMatch("axbxxc", q));
}

TEST(quote_meta_handles_nul_and_high_bit_bytes)
{
  const std::string with_nul {std::string("a") + '\0' + "b"};
  EXPECT_EQ(rc2::RE2::QuoteMeta(with_nul), std::string {"a\\x00b"});
  const std::string utf8     {"caf\xC3\xA9"};
  const std::string q        {rc2::RE2::QuoteMeta(utf8)};
  EXPECT(rc2::RE2::FullMatch(utf8, q));
}

TEST(set_match_reports_every_hit_by_index)
{
  rc2::RE2::Options opt;
  rc2::RE2::Set     set(opt, rc2::RE2::Anchor::UNANCHORED);
  EXPECT_EQ(set.Add("foo", nullptr), 0);
  EXPECT_EQ(set.Add("bar", nullptr), 1);
  EXPECT(set.Compile());
  std::vector<int> hits;
  EXPECT(set.Match("foobar", &hits));
  EXPECT_EQ(hits.size(), std::size_t {2});
  EXPECT(!set.Match("nothing", &hits));
  EXPECT(hits.empty());
}

TEST(set_add_rejects_a_bad_member_pattern)
{
  rc2::RE2::Options opt;
  rc2::RE2::Set     set(opt, rc2::RE2::Anchor::UNANCHORED);
  std::string       error;
  EXPECT_EQ(set.Add("(unterminated", &error), -1);
  EXPECT(!error.empty());
}

TEST(set_add_rejects_an_unsupported_option)
{
  rc2::RE2::Options opt;
  opt.set_posix_syntax(true);
  rc2::RE2::Set set(opt, rc2::RE2::Anchor::UNANCHORED);
  std::string   error;
  EXPECT_EQ(set.Add("a", &error), -1);
  EXPECT(!error.empty());
}

TEST(set_anchor_start_and_both)
{
  {
    rc2::RE2::Options opt;
    rc2::RE2::Set     set(opt, rc2::RE2::Anchor::ANCHOR_START);
    set.Add("foo", nullptr);
    EXPECT(set.Compile());
    std::vector<int> hits;
    EXPECT(set.Match("foobar", &hits));
    EXPECT(!set.Match("xfoobar", &hits));
  }
  {
    rc2::RE2::Options opt;
    rc2::RE2::Set     set(opt, rc2::RE2::Anchor::ANCHOR_BOTH);
    set.Add("foo", nullptr);
    EXPECT(set.Compile());
    std::vector<int> hits;
    EXPECT(set.Match("foo", &hits));
    EXPECT(!set.Match("foobar", &hits));
  }
}

TEST(set_match_before_compile_fails)
{
  rc2::RE2::Options opt;
  rc2::RE2::Set     set(opt, rc2::RE2::Anchor::UNANCHORED);
  set.Add("foo", nullptr);
  std::vector<int> hits;
  EXPECT(!set.Match("foo", &hits));
}

TEST(set_match_without_output_param)
{
  rc2::RE2::Options opt;
  rc2::RE2::Set     set(opt, rc2::RE2::Anchor::UNANCHORED);
  set.Add("foo", nullptr);
  EXPECT(set.Compile());
  EXPECT(set.Match("foo", nullptr));
  EXPECT(!set.Match("bar", nullptr));
}

// \C-in-text-mode completion. Real RE2 accepts \C in its default UTF-8 mode (matches
// exactly one byte, possibly mid-codepoint); rejecting it here would have been an asterisk on the
// "drop-in" claim. Safe unconditionally for this layer specifically: the whole API is byte-offset
// C++ (mirroring RE2's own), so the char-offset hazard that keeps \C gated to flags::bytes on a
// char-offset REAL-native surface (e.g. Python str) never applies here.

TEST(raw_byte_escape_is_accepted_in_default_mode)
{
  const rc2::RE2 re(R"(a\Cb)");
  EXPECT(re.ok());
  EXPECT(rc2::RE2::FullMatch("aXb", re));
}

TEST(raw_byte_escape_matches_exactly_one_byte_possibly_mid_codepoint)
{
  // "café" = c a f \xC3 \xA9 (UTF-8) -- \C descends to the byte even though the rest of the
  // pattern (and the surrounding text) is codepoint-aware, RE2's own exact semantics.
  const rc2::RE2 re(R"(caf\C)");
  EXPECT(re.ok());
  EXPECT(rc2::RE2::PartialMatch("caf\xC3\xA9", re));
  std::string_view input {"caf\xC3\xA9"};
  EXPECT(rc2::RE2::Consume(&input, re));
  EXPECT_EQ(input, "\xA9"sv); // \C consumed only \xC3, the first byte of the 2-byte codepoint
}

TEST(raw_byte_escape_mixed_with_codepoint_aware_constructs)
{
  const rc2::RE2 re(R"([a-z]+\C[a-z]+)");
  EXPECT(re.ok());
  EXPECT(rc2::RE2::FullMatch("abc\xC3xyz", re));
}

TEST(raw_byte_escape_reachable_through_set)
{
  rc2::RE2::Options opt;
  rc2::RE2::Set     set(opt, rc2::RE2::Anchor::UNANCHORED);
  EXPECT_EQ(set.Add(R"(a\Cb)", nullptr), 0);
  EXPECT(set.Compile());
  std::vector<int> hits;
  EXPECT(set.Match("aXb", &hits));
  EXPECT_EQ(hits.size(), std::size_t {1});
}

// A leading `+` on an integer, as RE2's `strtol`-based parse takes it; one sign only, and none on an unsigned
// destination that the value could not hold.
TEST(fullmatch_integer_takes_a_leading_plus)
{
  int           i {};
  long          l {};
  unsigned int  u {};
  EXPECT(rc2::RE2::FullMatch("+12", "(.*)", &i));
  EXPECT_EQ(i, 12);
  EXPECT(rc2::RE2::FullMatch("+12", "(.*)", &l));
  EXPECT_EQ(l, 12L);
  EXPECT(rc2::RE2::FullMatch("+12", "(.*)", &u));
  EXPECT_EQ(u, 12U);
  EXPECT(!rc2::RE2::FullMatch("+-12", "(.*)", &i));
  EXPECT(!rc2::RE2::FullMatch("++12", "(.*)", &i));
  EXPECT(!rc2::RE2::FullMatch("+", "(.*)", &i));
  EXPECT(!rc2::RE2::FullMatch("-12", "(.*)", &u));
  EXPECT(!rc2::RE2::FullMatch(" 12", "(.*)", &i));
}

// Floating-point groups read the C locale's decimal separator, as RE2 does: under a locale whose separator is a
// comma, "1,5" parses and "1.5" does not. Checked only where such a locale is installed.
TEST(fullmatch_floating_follows_the_locale_as_re2_does)
{
  double d {};
  EXPECT(rc2::RE2::FullMatch("1.5", "(.*)", &d));
  EXPECT_EQ(d, 1.5);
#if defined(__unix__) || defined(__APPLE__)
  // This thread's locale only (uselocale), which the strto* functions read.
  auto* const french {newlocale(LC_NUMERIC_MASK, "fr_FR.UTF-8", static_cast<locale_t>(nullptr))};
  if (french != static_cast<locale_t>(nullptr)) {
    auto* const    previous {uselocale(french)};
    const bool     comma    {rc2::RE2::FullMatch("1,5", "(.*)", &d)};
    const double   value    {d};
    const bool     dot      {rc2::RE2::FullMatch("1.5", "(.*)", &d)};
    uselocale(previous);
    freelocale(french);
    EXPECT(comma);
    EXPECT_EQ(value, 1.5);
    EXPECT(!dot);
  }
#endif
}

namespace {
  //! \brief `n` copies of `a{1000}`: a program of about 1000 * n instructions.
  std::string thousand_as(int n)
  {
    std::string p;
    for (int i {0}; i < n; ++i) {
      p += "a{1000}";
    }
    return p;
  }

  //! \brief The program bytes `max_mem` is held against, counted as the layer counts them.
  std::size_t program_bytes_of(const std::string& pattern)
  {
    const real::regex                re {pattern};
    const real::detail::program_view p  {re.raw_program()};
    return sizeof(real::detail::dynamic_program) + p.code.size_bytes() + p.classes.size_bytes() + p.names.size_bytes()
           + p.lookarounds.size_bytes() + p.cp_classes.size_bytes() + p.cp_ranges.size_bytes()
           + p.prefix_code.size_bytes() + p.prefix_classes.size_bytes() + p.prefix_cp_classes.size_bytes()
           + p.prefix_cp_ranges.size_bytes();
  }
} // namespace

// max_mem bounds the compiled program as RE2 bounds it: two thirds of it, in bytes, and a pattern past that
// fails with ErrorPatternTooLarge and RE2's own text. The default rejects nothing REAL compiles.
TEST(max_mem_bounds_the_compiled_program)
{
  rc2::RE2::Options small;
  small.set_max_mem(1 << 20);
  const rc2::RE2 over {thousand_as(100), small}; // about 1.2 MB of program against 0.7 MB
  EXPECT(!over.ok());
  EXPECT(over.error_code() == rc2::RE2::ErrorCode::ErrorPatternTooLarge);
  EXPECT_EQ(over.error(), std::string {"pattern too large - compile failed"});
  const rc2::RE2 fits {thousand_as(262)}; // near REAL's instruction bound, inside the 8 MiB default
  EXPECT(fits.ok());
  EXPECT(rc2::RE2::FullMatch(std::string(262000, 'a'), fits));
}

// The bound, exactly: a budget whose two thirds is the program's size takes it, one byte less refuses it.
TEST(max_mem_bound_is_two_thirds_of_the_budget)
{
  const std::string   pattern {thousand_as(5)};
  const std::size_t   bytes   {program_bytes_of(pattern)};
  const std::int64_t  exact   {static_cast<std::int64_t>(((bytes * 3) + 1) / 2)}; // two thirds of it is `bytes`
  rc2::RE2::Options   at;
  at.set_max_mem(exact);
  EXPECT((rc2::RE2 {pattern, at}.ok()));
  rc2::RE2::Options below;
  below.set_max_mem(exact - 2);
  EXPECT(!(rc2::RE2 {pattern, below}.ok()));
}

// Where two thirds of the budget is not positive, RE2 bounds the program to 100 000 instructions.
TEST(max_mem_of_zero_bounds_instructions)
{
  rc2::RE2::Options none;
  none.set_max_mem(0);
  EXPECT((rc2::RE2 {thousand_as(99), none}.ok()));
  const rc2::RE2 over {thousand_as(101), none};
  EXPECT(!over.ok());
  EXPECT(over.error_code() == rc2::RE2::ErrorCode::ErrorPatternTooLarge);
}

// A program past REAL's own instruction bound is too large, not malformed, whatever max_mem says.
TEST(program_past_the_instruction_bound_is_too_large)
{
  rc2::RE2::Options huge;
  huge.set_max_mem(std::int64_t {1} << 40);
  const rc2::RE2 over {thousand_as(300), huge};
  EXPECT(!over.ok());
  EXPECT(over.error_code() == rc2::RE2::ErrorCode::ErrorPatternTooLarge);
}

// A set whose members together exceed max_mem fails to compile, as RE2's does; each member alone fits.
TEST(set_compile_fails_past_max_mem)
{
  rc2::RE2::Options small;
  small.set_max_mem(1 << 20);
  rc2::RE2::Set set {small, rc2::RE2::Anchor::UNANCHORED};
  EXPECT_EQ(set.Add(thousand_as(40), nullptr), 0);
  EXPECT_EQ(set.Add(thousand_as(40), nullptr), 1);
  EXPECT(!set.Compile());
  rc2::RE2::Set roomy {rc2::RE2::Options {}, rc2::RE2::Anchor::UNANCHORED};
  EXPECT_EQ(roomy.Add(thousand_as(40), nullptr), 0);
  EXPECT_EQ(roomy.Add(thousand_as(40), nullptr), 1);
  EXPECT(roomy.Compile());
}

#if !defined(_WIN32)
namespace {
  //! \brief What \p body writes to stderr, captured through a temporary file.
  template <typename Body>
  std::string stderr_of(const Body& body)
  {
    std::FILE* const capture {std::tmpfile()};
    if (capture == nullptr) {
      return "<no capture>";
    }
    static_cast<void>(std::fflush(stderr));
    const int saved {dup(fileno(stderr))};
    if (saved < 0) {
      static_cast<void>(std::fclose(capture));
      return "<no capture>";
    }
    dup2(fileno(capture), fileno(stderr));
    body();
    static_cast<void>(std::fflush(stderr));
    dup2(saved, fileno(stderr));
    close(saved);
    static_cast<void>(std::fseek(capture, 0, SEEK_SET));
    std::string out;
    for (int c {std::fgetc(capture)}; c != EOF; c = std::fgetc(capture)) {
      out += static_cast<char>(c);
    }
    static_cast<void>(std::fclose(capture));
    return out;
  }
} // namespace

// log_errors reads true by default, as RE2's, and logs nothing until set_log_errors(true) arms it; then a failed
// compile writes RE2's message, and ok()/error() report the same either way.
TEST(re2_log_errors_is_opt_in_and_writes_re2s_message)
{
  const rc2::RE2::Options defaults;
  EXPECT(defaults.log_errors());
  std::string quiet_error;
  EXPECT_EQ(stderr_of([&] {
                        const rc2::RE2 re {"a(b", defaults};
                        EXPECT(!re.ok());
                        quiet_error = re.error();
                      }),
            std::string {});

  rc2::RE2::Options armed;
  armed.set_log_errors(true);
  std::string       logged_error;
  const std::string logged {stderr_of([&] {
                                        const rc2::RE2 re {"a(b", armed};
                                        EXPECT(!re.ok());
                                        logged_error = re.error();
                                      })};
  EXPECT_EQ(logged, "Error parsing 'a(b': " + logged_error + "\n");
  EXPECT_EQ(logged_error, quiet_error);

  rc2::RE2::Options disarmed;
  disarmed.set_log_errors(true);
  disarmed.set_log_errors(false);
  EXPECT_EQ(stderr_of([&] { static_cast<void>(rc2::RE2 {"a(b", disarmed}); }), std::string {});
  EXPECT_EQ(stderr_of([&] { static_cast<void>(rc2::RE2 {"a(b", armed}.ok()); }).empty(), false);
  EXPECT_EQ(stderr_of([&] { static_cast<void>(rc2::RE2 {"ab", armed}); }), std::string {}); // a pattern that compiles
}

// A pattern past max_mem logs RE2's "Error compiling"; a long one is printed as its first 100 bytes and "...";
// a set's Add prints the pattern whole, as RE2's does.
TEST(re2_log_errors_names_the_pattern_as_re2_does)
{
  rc2::RE2::Options armed;
  armed.set_log_errors(true);
  armed.set_max_mem(1024); // two thirds of a KiB: far below this pattern
  std::string big;
  for (int i {0}; i < 2000; ++i) {
    big += "ab|";
  }
  big += "cd";
  bool too_large {false};
  EXPECT_EQ(stderr_of([&] {
                        const rc2::RE2 re {big, armed};
                        too_large = re.error_code() == rc2::RE2::ErrorCode::ErrorPatternTooLarge;
                      }),
            "Error compiling '" + big.substr(0, 100) + "...'\n");
  EXPECT(too_large);

  rc2::RE2::Options parse;
  parse.set_log_errors(true);
  const std::string long_bad {std::string(150, 'x') + "("};
  const std::string printed  {stderr_of([&] { static_cast<void>(rc2::RE2 {long_bad, parse}); })};
  EXPECT(printed.rfind("Error parsing '" + long_bad.substr(0, 100) + "...': ", 0) == 0U);

  rc2::RE2::Set     set      {parse, rc2::RE2::Anchor::UNANCHORED};
  std::string       error;
  const std::string from_add {stderr_of([&] { EXPECT_EQ(set.Add(long_bad, &error), -1); })};
  std::string       want     {"Error parsing '"};
  want += long_bad;
  want += "': ";
  want += error;
  want += '\n';
  EXPECT_EQ(from_add, want);
}
#endif
