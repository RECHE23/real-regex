// Parser + matching for `\p{...}` / `\P{...}` / `\pX` Unicode property classes (General_Category and Script).
// The tables and their UCD oracles live in tests/unicode/; here we exercise the parser wiring: name resolution
// (short codes, long names, gc=/sc= prefixes, loose matching), negation, the `(?a)`-does-not-restrict rule, the
// bytes-mode rejection, named errors, and matching across engines. In-class \p{}, negation inside a class, and
// icase folding are later slices (P2), so they are not covered here.
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(unicode_property_general_category)
{
  EXPECT(real::regex(R"(\p{L})").fullmatch("A"));
  EXPECT(real::regex(R"(\p{L})").fullmatch("é")); // non-ASCII letter, via the high ranges
  EXPECT(!real::regex(R"(\p{L})").fullmatch("3"));
  EXPECT(real::regex(R"(\p{Lu})").fullmatch("A"));
  EXPECT(!real::regex(R"(\p{Lu})").fullmatch("a"));
  EXPECT(real::regex(R"(\p{Ll})").fullmatch("a"));
  EXPECT(real::regex(R"(\p{Nd})").fullmatch("3"));
  EXPECT(real::regex(R"(\p{N})").fullmatch("⅓")); // No is in the N group
  EXPECT(real::regex(R"(\pL)").fullmatch("x"));   // single-letter form == \p{L}
  EXPECT(real::regex(R"(\pN)").fullmatch("7"));
}

TEST(unicode_property_negated)
{
  EXPECT(real::regex(R"(\P{L})").fullmatch("3"));
  EXPECT(!real::regex(R"(\P{L})").fullmatch("A"));
  EXPECT(!real::regex(R"(\P{L})").fullmatch("é")); // é is a letter, so \P{L} rejects it
  EXPECT(real::regex(R"(\PL)").fullmatch(" "));
  EXPECT(real::regex(R"(\P{Nd})").fullmatch("A"));
}

TEST(unicode_property_script)
{
  EXPECT(real::regex(R"(\p{sc=Greek})").fullmatch("α"));
  EXPECT(!real::regex(R"(\p{sc=Greek})").fullmatch("A"));
  EXPECT(real::regex(R"(\p{Script=Latin})").fullmatch("Z"));
  EXPECT(real::regex(R"(\p{sc=Han})").fullmatch("中"));
  EXPECT(real::regex(R"(\p{sc=Hebrew})").fullmatch("א"));
  EXPECT(real::regex(R"(\P{sc=Greek})").fullmatch("A")); // negated script
}

TEST(unicode_property_aliases_and_loose_matching)
{
  EXPECT(real::regex(R"(\p{Letter})").fullmatch("é"));
  EXPECT(real::regex(R"(\p{Uppercase_Letter})").fullmatch("A"));
  EXPECT(real::regex(R"(\p{gc=Lu})").fullmatch("B"));
  EXPECT(real::regex(R"(\p{general_category=Nd})").fullmatch("5"));
  EXPECT(real::regex(R"(\p{LOWERCASE-letter})").fullmatch("a")); // case- and hyphen-insensitive
  EXPECT(real::regex(R"(\p{ Decimal Number })").fullmatch("9")); // spaces ignored
}

TEST(unicode_property_ascii_flag_does_not_restrict)
{
  // (?a) forces \w ASCII-only, but a Unicode property is always Unicode — the flag must not restrict it.
  EXPECT(real::regex(R"((?a)\p{L})").fullmatch("é"));
  EXPECT(real::regex(R"((?a)\p{sc=Greek})").fullmatch("α"));
  EXPECT(real::regex(R"(\p{L})", real::flags::ascii).fullmatch("é"));
}

TEST(unicode_property_matches_across_engines)
{
  // a long, homogeneous run drives the lazy-DFA klass_cp expansion, not only the small-input path
  const std::string many(4096, 'a');
  EXPECT(real::regex(R"(\p{L}+)").fullmatch(many));
  std::string mixed;
  for (int i = 0; i < 3000; ++i) {
    mixed += "éA"; // ASCII + non-ASCII letters interleaved
  }
  EXPECT(real::regex(R"(\p{L}+)").fullmatch(mixed));
  EXPECT(real::regex(R"(\p{Nd}+)").search("prefix12345suffix"sv));
  EXPECT(!real::regex(R"(\p{Nd}+)").search("no digits here"sv));
}

TEST(unicode_property_named_errors)
{
  EXPECT_THROWS(real::regex(R"(\p{Xyz})"), real::regex_error);         // unknown name
  EXPECT_THROWS(real::regex(R"(\p{White_Space})"), real::regex_error); // a binary property: not GC/Script (P2+)
  EXPECT_THROWS(real::regex(R"(\p{zz=Latin})"), real::regex_error);    // unknown namespace
  EXPECT_THROWS(real::regex(R"(\p{Latin)"), real::regex_error);        // unterminated
  EXPECT_THROWS(real::regex(R"(\p)"), real::regex_error);              // no name
  EXPECT_THROWS(real::regex(R"(\p{})"), real::regex_error);            // empty name
}

TEST(unicode_property_bytes_mode_rejects)
{
  EXPECT_THROWS(real::regex(R"(\p{L})", real::flags::bytes), real::regex_error);
  EXPECT_THROWS(real::regex(R"(\pL)", real::flags::bytes), real::regex_error);
}
