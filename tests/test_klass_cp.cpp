// The klass_cp opcode (P1): a text-mode Unicode shorthand (\w \d \s and negations) is a match-time
// code-point predicate — decode one code point, test membership (ASCII bitmap / range bsearch), then
// walk the continuation bytes through a computed skip into a [klass_cp][cont][cont][cont] chain. These
// are the VM-invariant torture tests: capture priority across the skip, every code-point length 1-4,
// malformed input, dedup, lookarounds, and the category quirks. Semantics equal re on well-formed
// UTF-8; malformed input is REAL's documented internal policy (a malformed sequence is a non-member).
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

namespace {
  // UTF-8 bytes of some code points used across lengths (string_view: no throwing static init).
  constexpr std::string_view kKelvin {"\xE2\x84\xAA"};      // U+212A KELVIN SIGN (3 bytes), a word char
  constexpr std::string_view kEuro   {"\xE2\x82\xAC"};      // U+20AC (3 bytes), NOT a word char
  constexpr std::string_view kClef   {"\xF0\x9D\x84\x9E"};  // U+1D11E (4 bytes), NOT a word char
  constexpr std::string_view kMathA  {"\xF0\x9D\x90\x80"};  // U+1D400 MATH BOLD CAPITAL A (4 bytes), word
  constexpr std::string_view kEacute {"\xC3\xA9"};          // U+00E9 é (2 bytes), a word char
  constexpr std::string_view kArab3  {"\xD9\xA3"};          // U+0663 ARABIC-INDIC THREE (2 bytes), word + digit
  constexpr std::string_view kSuper2 {"\xC2\xB2"};          // U+00B2 SUPERSCRIPT TWO (2 bytes), word, NOT digit
  constexpr std::string_view kCombAcc{"\xCC\x81"};          // U+0301 COMBINING ACUTE (2 bytes), NOT a word char

  // Concatenate parts into an owned std::string (the match API takes a string_view into it).
  std::string cat(std::initializer_list<std::string_view> parts)
  {
    std::string out;
    for (const std::string_view part : parts) {
      out += part;
    }
    return out;
  }
} // namespace

TEST(klass_cp_lengths_1_to_4)
{
  // A single \w matches an ASCII (1B), 2B, 3B and (astral) 4B word code point; a non-word code point
  // of each width fails. Exercises the skip formula pc+1+(4-len) for len = 1/2/3/4.
  EXPECT(real::regex("\\w").fullmatch("a"));       // len 1
  EXPECT(real::regex("\\w").fullmatch(kEacute));   // len 2 (é)
  EXPECT(real::regex("\\w").fullmatch(kKelvin));   // len 3 (Kelvin)
  EXPECT(real::regex("\\w").fullmatch(kMathA));    // len 4 (word)
  EXPECT(!real::regex("\\w").fullmatch(kEuro));    // len 3, not a word char
  EXPECT(!real::regex("\\w").fullmatch(kClef));    // len 4, not a word char
  // A run of mixed widths, greedy +.
  const std::string run {cat({"a", kEacute, kKelvin, "z"})};
  EXPECT(real::regex("\\w+").fullmatch(run));
  const std::string hay {cat({"  ", kEacute, kKelvin, "! "})};
  const std::string ek  {cat({kEacute, kKelvin})};
  EXPECT_EQ(real::regex("\\w+").search(hay)[0], ek);
}

TEST(klass_cp_ring_counterexample_capture_priority)
{
  // The case that killed the ring design: an alternation where a multi-byte literal branch and a
  // \w branch both match the same code point. Leftmost-branch priority must hold across the skip —
  // group 1 is the literal (Kelvin) branch, exactly as re captures it.
  const std::string subj {cat({kKelvin, "y"})};
  const auto        m    {real::regex("(\xE2\x84\xAA|\\w)y").match(subj)};
  EXPECT(m.matched());
  EXPECT_EQ(m[1], kKelvin);  // alternative 1 wins, capturing all 3 bytes
  const auto m2 {real::regex("(\\w|\xE2\x84\xAA)y").match(subj)};
  EXPECT_EQ(m2[1], kKelvin); // \w branch first now, but still captures the whole code point
}

TEST(klass_cp_captures_across_alternation_and_quantifier)
{
  // (\w)(\w)+ over a mixed-width run: group 1 is the first code point, group 2 the last repetition.
  const std::string subj {cat({"a", kEacute, kKelvin})}; // 3 code points
  const auto        m    {real::regex("(\\w)(\\w)+").fullmatch(subj)};
  EXPECT(m.matched());
  EXPECT_EQ(m[1], "a"sv);
  EXPECT_EQ(m[2], kKelvin); // last iteration captured the final code point
}

TEST(klass_cp_negation_and_malformed)
{
  // \W matches a valid non-word code point; \w and \W BOTH reject a malformed sequence (canonical
  // only — REAL's documented policy; re never sees malformed str).
  EXPECT(real::regex("\\W").fullmatch(kEuro));
  EXPECT(!real::regex("\\W").fullmatch(kEacute)); // é is a word char
  const std::string bad_cont  {"\x80"};           // lone continuation
  const std::string bad_trunc2{"\xC3"};           // truncated 2-byte lead
  const std::string bad_trunc3{"\xE2\x84"};       // truncated 3-byte
  const std::string bad_over  {"\xC0\x80"};       // overlong NUL
  for (const std::string& bad : {bad_cont, bad_trunc2, bad_trunc3, bad_over}) {
    EXPECT(!real::regex("\\w").fullmatch(bad));
    EXPECT(!real::regex("\\W").fullmatch(bad));
  }
}

TEST(klass_cp_dedup_convergent_paths)
{
  // Two branches that both accept the same code point converge on the same continuation pc; the
  // per-list dedup must keep exactly the higher-priority thread (no double-count, no divergence).
  const std::string ae {cat({"a", kEacute})};
  EXPECT(real::regex("(?:\\w|\\w)+").fullmatch(ae));
  const std::string ey {cat({kEacute, "y"})};
  EXPECT_EQ(real::regex("(\\w|\\w)y").match(ey)[1], kEacute);
}

TEST(klass_cp_nullable_and_anchors)
{
  // \w* is nullable: it matches the empty string and does not over-consume; \w+ is not.
  EXPECT(real::regex("\\w*").fullmatch(""));
  const std::string ae {cat({"a", kEacute})};
  EXPECT(real::regex("\\w*").fullmatch(ae));
  EXPECT_EQ(real::regex("\\w*").search("  x")[0], ""sv); // empty match at position 0
  EXPECT(!real::regex("\\w+").fullmatch(""));
  // find_all advances past an astral non-word code point correctly (no split of the 4-byte sequence).
  const std::string mixed {cat({"a bb ", kClef, " ccc"})};
  const real::regex rx    {"\\w+"};
  EXPECT_EQ(rx.find_all(mixed).size(), 3U);
}

TEST(klass_cp_inside_lookaround)
{
  // \w inside a bounded lookaround (width up to 4 bytes): the sub-VM runs the same decode+skip.
  const std::string behind_lit    {cat({kEacute, "x"})};
  const std::string pat_lit       {cat({"(?<=", kEacute, ")x"})};
  EXPECT(real::regex(pat_lit).search(behind_lit).matched());        // literal behind
  const std::string behind_kelvin {cat({kKelvin, "x"})};
  EXPECT(real::regex("(?<=\\w)x").search(behind_kelvin).matched()); // \w behind (3-byte cp)
  const std::string behind_euro   {cat({kEuro, "x"})};
  EXPECT(!real::regex("(?<=\\w)x").search(behind_euro));            // € is not a word char
  EXPECT(real::regex("(?=\\w)").search(kEacute).matched());         // lookahead \w
  // The lookbehind budget counts \w as 4 bytes: {63} == 252 compiles, {64} == 256 is rejected.
  const std::string h63 {std::string(63, 'a') + "x"};
  EXPECT(real::regex("(?<=\\w{63})x").search(h63).matched());
  EXPECT_THROWS(real::regex("(?<=\\w{64})x"), real::regex_error);
}

TEST(klass_cp_category_quirks)
{
  // The traps a naive Unicode-category derivation gets wrong (oracle: re).
  EXPECT(real::regex("\\w").fullmatch(kArab3) && real::regex("\\d").fullmatch(kArab3));    // ٣: word + digit
  EXPECT(real::regex("\\w").fullmatch(kSuper2) && !real::regex("\\d").fullmatch(kSuper2)); // ²: word, not digit
  EXPECT(!real::regex("\\w").fullmatch(kCombAcc));                                         // U+0301 combining acute is NOT a word char
  // A decomposed "é" (e + U+0301) is two code points: \w+ matches only the base 'e' (the mark is
  // non-word).
  const std::string decomposed {cat({"e", kCombAcc})};
  EXPECT_EQ(real::regex("\\w+").search(decomposed)[0], "e"sv);
}

TEST(klass_cp_ascii_and_bytes_unchanged)
{
  using real::flags;
  // Under ascii / bytes the shorthand is the ASCII byte-NFA (no klass_cp): é is not \w.
  EXPECT(!real::regex("\\w", flags::ascii).fullmatch(kEacute));
  EXPECT(real::regex("\\w", flags::ascii).fullmatch("a"));
  EXPECT(!real::regex("\\w", flags::bytes).fullmatch(kEacute));
  EXPECT(real::regex("(?a)\\w+").fullmatch("abc"));
  EXPECT(!real::regex("(?a)\\w+").fullmatch(kEacute));
}
