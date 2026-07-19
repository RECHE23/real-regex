// The klass_cp opcode: a text-mode Unicode shorthand (\w \d \s and negations) is a match-time
// code-point predicate — decode one code point, test membership (ASCII bitmap / range bsearch), then
// walk the continuation bytes through a computed skip into a [klass_cp][cont][cont][cont] chain. These
// are the VM-invariant torture tests: capture priority across the skip, every code-point length 1-4,
// malformed input, dedup, lookarounds, and the category quirks. Semantics equal re on well-formed
// UTF-8; malformed input is REAL's documented internal policy (a malformed sequence is a non-member).
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include <sciforge/test/strings.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

namespace {
  // UTF-8 bytes of some code points used across lengths (string_view: no throwing static init).
  constexpr std::string_view kKelvin {"\xE2\x84\xAA"};     // U+212A KELVIN SIGN (3 bytes), a word char
  constexpr std::string_view kEuro   {"\xE2\x82\xAC"};     // U+20AC (3 bytes), NOT a word char
  constexpr std::string_view kClef   {"\xF0\x9D\x84\x9E"}; // U+1D11E (4 bytes), NOT a word char
  constexpr std::string_view kMathA  {"\xF0\x9D\x90\x80"}; // U+1D400 MATH BOLD CAPITAL A (4 bytes), word
  constexpr std::string_view kEacute {"\xC3\xA9"};         // U+00E9 é (2 bytes), a word char
  constexpr std::string_view kArab3  {"\xD9\xA3"};         // U+0663 ARABIC-INDIC THREE (2 bytes), word + digit
  constexpr std::string_view kSuper2 {"\xC2\xB2"};         // U+00B2 SUPERSCRIPT TWO (2 bytes), word, NOT digit
  constexpr std::string_view kCombAcc{"\xCC\x81"};         // U+0301 COMBINING ACUTE (2 bytes), NOT a word char

  using test::cat;                                         // concatenate views into an owned std::string (the match API views into it)
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

TEST(klass_cp_fast_path_equivalence)
{
  // The whole-pattern scan-loop fast path (\w+) and the general VM (a capture forces it off) share
  // cp_class_matches, so they must agree -- including at the empty-match edge. Pins the gating.
  const std::string subj {cat({"  ", kEacute, kArab3, "! "})};
  const auto        fast {real::regex("\\w+").search(subj)};   // greedy_cp_class fast path
  const auto        slow {real::regex("(\\w+)").search(subj)}; // capture -> general VM
  EXPECT(fast.matched() && slow.matched());
  EXPECT_EQ(fast[0], slow[0]);
  // \w* is nullable: both paths yield the empty match at position 0 on a non-word start.
  EXPECT_EQ(real::regex("\\w*").search(kEuro)[0], ""sv);
  EXPECT_EQ(real::regex("(\\w*)").search(kEuro)[0], ""sv);
}

TEST(klass_cp_negative_lookbehind_at_multibyte_boundary)
{
  // (?<!\w)X: X only when NOT preceded by a word code point -- the lookbehind's klass_cp runs the
  // per-start backward scan across a multi-byte boundary.
  const std::string after_word    {cat({kEacute, "x"})}; // é (word) then x: lookbehind fails
  const std::string after_nonword {cat({kEuro, "x"})};   // € (non-word) then x: lookbehind holds
  EXPECT(!real::regex("(?<!\\w)x").search(after_word));
  EXPECT(real::regex("(?<!\\w)x").search(after_nonword).matched());
}

TEST(klass_cp_static_regex_is_constexpr_and_correct)
{
  // A text-mode \w+ static_regex compiles at compile time (small program) and matches Unicode.
  static_assert(real::static_regex<"\\w+">().search("café").matched());
  static_assert(!real::static_regex<"\\w+">().fullmatch("a b"));
  EXPECT(real::static_regex<"\\w+">().fullmatch("héllo").matched());
}

TEST(klass_cp_hi_table_matches_range_search_exhaustively_for_pL)
{
  // \p{} sparse hi membership (cp > U+07FF) must match the range tables bit-for-bit over the
  // entire Unicode space. Encode each scalar and fullmatch `\p{L}` — runtime path = European page +
  // thread-local 2-stage hi table.
  const real::regex                 pl   {R"(\p{L})"};
  const auto                        st   {real::detail::dynamic_storage::compile(R"(\p{L})", real::flags::none)};
  const auto                        pv   {st.view()};
  const real::detail::cp_class&     cc   {pv.cp_classes[0]};
  std::size_t                       mism {0};
  for (std::uint32_t cp = 0; cp < 0x110000U; ++cp) {
    if (cp >= 0xD800U && cp <= 0xDFFFU) {
      continue; // surrogates are not valid UTF-8 scalar values
    }
    bool want {false};
    if (cp < 0x80U) {
      want = cc.ascii.test(static_cast<std::uint8_t>(cp));
    }
    else {
      for (std::uint32_t k = 0; k < cc.range_count; ++k) {
        const auto& r {pv.cp_ranges[cc.range_begin + k]};
        if (cp >= r.lo && cp <= r.hi) {
          want = true;
          break;
        }
        if (r.lo > cp) {
          break;
        }
      }
    }
    char        buf[4] {};
    std::size_t n      {0};
    if (cp < 0x80U) {
      buf[0] = static_cast<char>(cp);
      n      = 1;
    }
    else if (cp < 0x800U) {
      buf[0] = static_cast<char>(0xC0U | (cp >> 6U));
      buf[1] = static_cast<char>(0x80U | (cp & 0x3FU));
      n      = 2;
    }
    else if (cp < 0x10000U) {
      buf[0] = static_cast<char>(0xE0U | (cp >> 12U));
      buf[1] = static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
      buf[2] = static_cast<char>(0x80U | (cp & 0x3FU));
      n      = 3;
    }
    else {
      buf[0] = static_cast<char>(0xF0U | (cp >> 18U));
      buf[1] = static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU));
      buf[2] = static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
      buf[3] = static_cast<char>(0x80U | (cp & 0x3FU));
      n      = 4;
    }
    const bool got {static_cast<bool>(pl.fullmatch(std::string_view {buf, n}))};
    if (want != got) {
      ++mism;
    }
  }
  EXPECT_EQ(mism, 0U);
}

TEST(klass_cp_mixed_members_union_regardless_of_order)
{
  // KLASS-MIX regression (P1, shipped v2026.7.3): a non-ASCII member — a literal, a range, or a second
  // predicate — that FOLLOWS a code-point predicate in a class must union into it, not be lost. The bug:
  // members accumulated in parse order but klass_cp binary-searches them, so a member landing below the
  // predicate's ranges was silently missed ([\dЩ] failed while [Щ\d] worked). Matrix: order x member-type,
  // pinned against Python re (which the crate co-arbitrated).
  const auto hit = [](std::string_view pat, std::string_view cp) {
                     const std::string p {pat};
                     const std::string s {cp};
                     return real::regex(p).search(s).matched();
                   };
  // predicate then a non-ASCII literal (é = U+00E9: a word char, not a digit) — either order must match é
  EXPECT(hit(cat({"[\\d", kEacute, "]"}), kEacute));              // [\dé]  (regression: this order lost é)
  EXPECT(hit(cat({"[", kEacute, "\\d]"}), kEacute));              // [é\d]  (the witness that always worked)
  EXPECT(hit(cat({"[\\d", kEacute, "]"}), kArab3));               // the \d part still matches a digit (U+0663)
  EXPECT(!hit(cat({"[\\d", kEacute, "]"}), kEuro));               // a non-member stays out (U+20AC)
  // predicate then a range that starts BELOW the predicate's non-ASCII ranges (é=U+00E9 .. €=U+20AC)
  EXPECT(hit(cat({"[\\d", kEacute, "-", kEuro, "]"}), kCombAcc)); // U+0301 is inside é..€ — matched via the range
  EXPECT(!hit(cat({"[\\d", kEacute, "-", kEuro, "]"}), kClef));   // U+1D11E is above the range — stays out
  // two predicates, each direction — the union must cover both halves
  EXPECT(hit("[\\w\\W]", kEuro));                                 // \W supplies the non-word U+20AC
  EXPECT(hit("[\\w\\W]", kEacute));                               // \w supplies é
  EXPECT(hit("[\\p{L}\\p{N}]", kArab3));                          // \p{N} supplies the Nd digit U+0663
  EXPECT(hit("[\\p{N}\\p{L}]", kEacute));                         // \p{L} supplies é, predicate order reversed
  EXPECT(hit("[\\p{L}\\p{N}]", kSuper2));                         // U+00B2 is No (a \p{N}) — must match
}
