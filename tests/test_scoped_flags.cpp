// Scoped inline flags: `(?x:...)`. Verbose is the one flag whose scope changes tokenization
// (insignificant whitespace and `#` comments), so it is honoured per-scope from the flag-scope stack.
// The other scoped flags (i/a/s/m) are still rejected until they are supported; this pins both.
#include <string>

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

TEST(scoped_dotall_and_multiline_are_still_rejected)
{
  // s (dotall) and m (multiline) are not scopable yet; x / i / a are, so a group mixing a supported
  // flag with s or m is still rejected as a whole.
  EXPECT_THROWS(real::regex("(?s:.)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?m:^)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?-s:.)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?i-s:a)"), real::regex_error); // i is fine, but the scoped -s is not
  EXPECT_THROWS(real::regex("(?-:a)"), real::regex_error);   // a '-' with no flag after it
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
