// Unicode \w \d \s property tables: the ranges + is_word_cp / is_digit_cp / is_space_cp, tested in
// isolation (independently of the parser/compiler/VM that consume them). The ranges are
// validated against re at generation time; these contract tests are the standing second net, pinning
// the category-derivation quirks (which make a naive Unicode-category derivation wrong), the range
// boundaries, and the exact totals.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/unicode_props.hpp"

using real::detail::code_range;
using real::detail::digit_ranges;
using real::detail::digit_ranges_size;
using real::detail::is_digit_cp;
using real::detail::is_space_cp;
using real::detail::is_word_cp;
using real::detail::space_ranges;
using real::detail::space_ranges_size;
using real::detail::word_ranges;
using real::detail::word_ranges_size;

namespace {
  std::size_t count_cps(const code_range* ranges,
                        std::size_t       n)
  {
    std::size_t total {0};
    for (std::size_t i = 0; i < n; ++i) {
      total += (ranges[i].hi - ranges[i].lo) + 1;
    }
    return total;
  }
} // namespace

TEST(unicode_props_quirks_contract)
{
  // These are the traps that make "derive \w from the L/N/… categories" wrong; they must never
  // regress silently (oracle: re's \w \d \s).
  EXPECT(is_word_cp(U'_'));                                         // the one Pc that is a word char
  EXPECT(!is_word_cp(0x203FU));                                     // ‿ UNDERTIE (Pc) is NOT a word char
  EXPECT(is_word_cp(0x00B2U));                                      // ² SUPERSCRIPT TWO (No) IS a word char
  EXPECT(is_word_cp(0x00BDU));                                      // ½ VULGAR FRACTION HALF (No) IS a word char
  EXPECT(!is_digit_cp(0x00B2U));                                    // ... but No is NOT a \d digit
  EXPECT(is_word_cp(0x0663U));                                      // ٣ ARABIC-INDIC DIGIT THREE (Nd): word ...
  EXPECT(is_digit_cp(0x0663U));                                     // ... and digit
  EXPECT(is_space_cp(0x00A0U));                                     // NBSP is whitespace
  EXPECT(is_space_cp(0x2028U));                                     // LINE SEPARATOR is whitespace
  EXPECT(is_space_cp(0x0009U));                                     // tab
  EXPECT(is_word_cp(0x00E9U));                                      // é: word ...
  EXPECT(!is_digit_cp(0x00E9U));                                    // ... not a digit ...
  EXPECT(!is_space_cp(0x00E9U));                                    // ... not whitespace
  EXPECT(is_word_cp(U'a') && is_word_cp(U'Z') && is_word_cp(U'5')); // ASCII word chars
  EXPECT(!is_word_cp(U'-') && !is_word_cp(U' '));                   // ASCII non-word
}

TEST(unicode_props_range_boundaries)
{
  // Representative ranges across the UTF-8 length classes; cp-1 / cp+1 sit outside.
  EXPECT(is_word_cp(U'a') && is_word_cp(U'z') && !is_word_cp(U'z' + 1));         // ASCII (1-byte) range a-z
  EXPECT(!is_word_cp(U'a' - 1));                                                 // '`' just below 'a'
  EXPECT(is_word_cp(0x00C0U) && is_word_cp(0x00D6U) && !is_word_cp(0x00D7U));    // À-Ö word, × (0xD7) not
  EXPECT(is_digit_cp(0x0660U) && is_digit_cp(0x0669U) && !is_digit_cp(0x066AU)); // arabic 0-9, then ٪
  EXPECT(is_word_cp(0x1D400U) && !is_word_cp(0x1D3FFU));                         // astral (4-byte): MATH BOLD A, one below not
  // Surrogates are in no table (never valid code points).
  EXPECT(!is_word_cp(0xD800U) && !is_word_cp(0xDFFFU));
  EXPECT(!is_space_cp(0xD800U) && !is_digit_cp(0xDC00U));
  // The very top code point is not a word/digit/space char.
  EXPECT(!is_word_cp(0x10FFFFU) && !is_digit_cp(0x10FFFFU) && !is_space_cp(0x10FFFFU));
}

TEST(unicode_props_exact_totals)
{
  // The Unicode-16.0.0 code-point totals; a drift in any table breaks here (double net with the
  // generator's own assertion and the regen guard).
  EXPECT_EQ(count_cps(word_ranges, word_ranges_size), 142940U);
  EXPECT_EQ(count_cps(digit_ranges, digit_ranges_size), 760U);
  EXPECT_EQ(count_cps(space_ranges, space_ranges_size), 29U);
  EXPECT_EQ(word_ranges_size, 771U);
  EXPECT_EQ(digit_ranges_size, 71U);
  EXPECT_EQ(space_ranges_size, 10U);
}

TEST(unicode_props_unidata_version_is_pinned)
{
  EXPECT(std::string_view(real::detail::unicode_props_unidata_version) == "16.0.0");
}
