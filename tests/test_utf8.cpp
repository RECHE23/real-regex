// UTF-8 literals: in code-point mode (the default, !flags::bytes) a raw multi-byte
// character is decoded to one atom — the same emission as `\uHHHH` — so a following quantifier applies
// to the whole code point, not just its last byte (the é+ bug). Malformed UTF-8 in the pattern is a
// compile error, not a silent literal. flags::bytes keeps raw byte semantics (the compat layer relies
// on it). This file's source is UTF-8; the malformation / boundary cases are built from raw bytes.
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include <sciforge/test/strings.hpp>
#include "real/real.hpp"
#include "real/utf8.hpp"

using real::flags;
using real::regex;
using test::bytes; // a byte string from explicit byte values (boundary / malformed sequences)
using test::cat;   // concatenate views without a chained operator+

namespace {

  // Whole-match byte length of the leftmost match, or std::string::npos if there is no match.
  std::size_t match_len(const std::string& pattern,
                        const std::string& text,
                        flags              f = flags::none)
  {
    const regex r(pattern, f);
    const auto  m {r.search(text)};
    return m.matched() ? m.end(0) - m.start(0) : std::string::npos;
  }

  // lvalue-safe wrappers (search/fullmatch delete the rvalue-string overload to avoid dangling).
  bool searches(const std::string& pattern,
                const std::string& text,
                flags              f = flags::none)
  {
    return regex(pattern, f).search(text).matched();
  }

  bool fullmatches(const std::string& pattern,
                   const std::string& text,
                   flags              f = flags::none)
  {
    return regex(pattern, f).fullmatch(text).matched();
  }
} // namespace

TEST(utf8_literal_atomizes_quantifier)
{
  const std::string e {"é"};             // U+00E9 = C3 A9
  EXPECT_EQ(match_len("é+", e + e), 4U); // + spans the whole code point (the bug it fixes)
  EXPECT_EQ(match_len("é*", cat({e, e, e})), 6U);
  EXPECT_EQ(match_len("é{2,3}", cat({e, e, e})), 6U);
  EXPECT(regex("é?").fullmatch(e));
  EXPECT(regex("é?").fullmatch(""));
  EXPECT(!fullmatches("é?", e + e));            // `?` is one code point, not two bytes
  EXPECT(fullmatches("aé+b", cat({"a", e, e, "b"})));
}

TEST(utf8_literal_equals_unicode_escape)
{
  // é+ must be identical to é+ (the shared emit_codepoint_utf8 path — one route, not two).
  const std::string e {"é"};
  for (const std::string& t : {e, cat({e, e}), cat({e, e, e}), cat({"x", e}), std::string()}) {
    EXPECT_EQ(regex("é+").search(t).matched(), regex("\\u00E9+").search(t).matched());
    EXPECT_EQ(match_len("é+", t), match_len("\\u00E9+", t));
  }
}

TEST(utf8_multibyte_widths)
{
  const std::string euro  {"€"}; // U+20AC, 3 bytes
  const std::string emoji {"😀"}; // U+1F600, 4 bytes
  EXPECT_EQ(match_len("€+", euro + euro), 6U);
  EXPECT_EQ(match_len("😀+", emoji + emoji), 8U);
  EXPECT_EQ(match_len("€+", euro + euro), match_len("\\u20AC+", euro + euro));
  EXPECT_EQ(match_len("😀+", emoji + emoji), match_len("\\U0001F600+", emoji + emoji));

  // Multi-code-point literal: "café" is caf + é; the `+` applies to the é only.
  const std::string cafe {"café"};
  EXPECT(regex("café").fullmatch(cafe));
  EXPECT_EQ(match_len("café+", cafe + "é"), 7U); // caf(3) + éé(4)
}

TEST(utf8_dot_matches_one_codepoint)
{
  // Regression: `.` consumes a whole code point (byte-automata), including 2/3/4-byte forms.
  const std::string e     {"é"};
  const std::string euro  {"€"};
  const std::string emoji {"😀"};
  EXPECT(regex(".").fullmatch(e));
  EXPECT(regex(".").fullmatch(euro));
  EXPECT(regex(".").fullmatch(emoji));
  EXPECT(fullmatches(".{3}", cat({e, euro, emoji}))); // three code points = 2+3+4 = 9 bytes
  EXPECT(fullmatches("^é+$", e + e));                 // anchors around a UTF-8 literal
}

TEST(utf8_malformed_pattern_is_a_compile_error)
{
  // Each malformation class rejected at compile time (code-point mode).
  EXPECT_THROWS(regex(bytes({0x80})), real::regex_error);                   // lone continuation
  EXPECT_THROWS(regex(bytes({0xBF})), real::regex_error);                   // lone continuation
  EXPECT_THROWS(regex(bytes({0xC3})), real::regex_error);                   // truncated 2-byte
  EXPECT_THROWS(regex(bytes({0xE2, 0x82})), real::regex_error);             // truncated 3-byte
  EXPECT_THROWS(regex(bytes({0xC3, 0x41})), real::regex_error);             // lead + non-continuation
  EXPECT_THROWS(regex(bytes({0xC0, 0x80})), real::regex_error);             // overlong NUL
  EXPECT_THROWS(regex(bytes({0xC1, 0xBF})), real::regex_error);             // overlong
  EXPECT_THROWS(regex(bytes({0xE0, 0x80, 0x80})), real::regex_error);       // overlong 3-byte
  EXPECT_THROWS(regex(bytes({0xF0, 0x80, 0x80, 0x80})), real::regex_error); // overlong 4-byte
  EXPECT_THROWS(regex(bytes({0xF5, 0x80, 0x80, 0x80})), real::regex_error); // > U+10FFFF
  EXPECT_THROWS(regex(bytes({0xFF})), real::regex_error);                   // invalid lead
  EXPECT_THROWS(regex(bytes({0xED, 0xA0, 0x80})), real::regex_error);       // surrogate U+D800

  // In bytes mode these are just raw byte literals (no UTF-8 validation) — construct + match.
  const std::string raw {bytes({0xC0, 0x80})};
  const regex       r(raw, flags::bytes);
  EXPECT(r.search(raw).matched());
}

TEST(utf8_overlong_reject_security)
{
  // A `\x00` NUL matcher must NEVER match an overlong NUL (C0 80) — the classic filter-bypass.
  EXPECT(!searches(R"(\x00)", bytes({0xC0, 0x80})));
  EXPECT(searches(R"(\x00)", bytes({0x00})));

  // A literal é matches only its canonical form C3 A9, not an overlong encoding.
  const std::string e {"é"};
  EXPECT(searches("é", e));
  EXPECT(!searches("é", bytes({0xC0, 0x80})));

  // `.` uses the canonical lead set (C2–DF/E0–EF/F0–F4), so C0/C1 are not accepted as a lead: an
  // overlong 2-byte sequence is not matched as a single code point.
  EXPECT(!fullmatches("^.$", bytes({0xC0, 0x80})));
  EXPECT(!fullmatches("^.$", bytes({0xC1, 0xBF})));
}

TEST(utf8_bytes_mode_unchanged)
{
  const std::string e {"é"}; // C3 A9
  // bytes mode: é+ is byte-C3 then byte-A9+ (the + on the last byte only) -> "C3 A9" = 2 bytes.
  EXPECT_EQ(match_len("é+", e + e, flags::bytes), 2U);
  // code-point mode atomises -> 4 bytes. The two modes are deliberately different.
  EXPECT_EQ(match_len("é+", e + e), 4U);
}

TEST(utf8_ascii_unchanged)
{
  EXPECT(regex("a+").fullmatch("aaa"));
  EXPECT_EQ(match_len("a+", "aaa"), 3U);
  EXPECT(regex(R"([a-z]+\d*)").search("abc123").matched());
  const std::string del {bytes({0x7F})};
  EXPECT(fullmatches(del, del));              // DEL, still one ASCII byte
  EXPECT(regex(R"(\x41+)").fullmatch("AAA")); // hex escape of ASCII 'A'
}

TEST(utf8_decode_codepoint_strict)
{
  using real::detail::decode_codepoint_strict;
  // ASCII decodes to itself (one byte).
  const std::string ascii {"A"};
  const auto        a     {decode_codepoint_strict(ascii, 0)};
  EXPECT(a.valid);
  EXPECT_EQ(a.length, 1U);
  EXPECT_EQ(a.cp, 0x41U);
  // Valid 2/3/4-byte code points.
  const std::string e     {"é"};
  const std::string euro  {"€"};
  const std::string emoji {"😀"};
  EXPECT(decode_codepoint_strict(e, 0).valid);
  EXPECT_EQ(decode_codepoint_strict(e, 0).cp, 0xE9U);
  EXPECT_EQ(decode_codepoint_strict(euro, 0).length, 3U);
  EXPECT_EQ(decode_codepoint_strict(emoji, 0).cp, 0x1F600U);
  // Malformations are rejected (the pattern-side validator).
  EXPECT(!decode_codepoint_strict(bytes({0x80}), 0).valid);             // lone continuation
  EXPECT(!decode_codepoint_strict(bytes({0xC0, 0x80}), 0).valid);       // overlong
  EXPECT(!decode_codepoint_strict(bytes({0xC3}), 0).valid);             // truncated
  EXPECT(!decode_codepoint_strict(bytes({0xED, 0xA0, 0x80}), 0).valid); // surrogate
  EXPECT(!decode_codepoint_strict(bytes({0xFF}), 0).valid);             // invalid lead
}

TEST(utf8_empty_match_advances_by_codepoint)
{
  // A nullable pattern's finditer over multi-byte text advances one whole code point after an empty
  // match (Python's rule) — exercising the 2/3/4-byte width of the codepoint advance.
  const std::string mixed {cat({"a", "é", "€", "😀", "b"})}; // 1+2+3+4+1 codepoints
  const regex       r("x*");
  std::size_t       count {0};
  for (const auto& m : r.find_iter(mixed)) {
    (void)m;
    ++count;
  }
  // re yields an empty match at each code-point boundary plus the end: 5 code points -> 6 matches.
  EXPECT_EQ(count, 6U);
}

TEST(utf8_character_classes)
{
  // A character class carries specific non-ASCII code points, not "any non-ASCII".
  const std::string e    {"é"};
  const std::string a    {"à"};
  const std::string u    {"ü"};
  const std::string euro {"€"};
  EXPECT(fullmatches("[é]", e));
  EXPECT(!fullmatches("[é]", a));                            // specific code point, not any non-ASCII
  EXPECT(fullmatches("[é]", e) == fullmatches(R"([é])", e)); // [é] == [é]
  EXPECT(fullmatches("[éàü]", a));
  EXPECT(fullmatches("[éàü]", u));
  EXPECT(!fullmatches("[éàü]", euro));
  // Mixed ASCII + non-ASCII members.
  EXPECT(fullmatches("[a-zé]", "m"));
  EXPECT(fullmatches("[a-zé]", e));
  EXPECT(!fullmatches("[a-zé]", a));
  EXPECT(!fullmatches("[a-zé]", "A"));
  // Quantified class over code points.
  EXPECT_EQ(match_len("[éà]+", cat({e, a, e})), 6U); // 3 code points x 2 bytes
}

TEST(utf8_class_ranges)
{
  const std::string a    {"à"};    // U+00E0
  const std::string e    {"é"};    // U+00E9 (in à..ÿ)
  const std::string y    {"ÿ"};    // U+00FF
  const std::string euro {"€"};
  EXPECT(fullmatches("[à-ÿ]", a)); // range endpoints + interior
  EXPECT(fullmatches("[à-ÿ]", e));
  EXPECT(fullmatches("[à-ÿ]", y));
  EXPECT(!fullmatches("[à-ÿ]", euro));
  // Range crossing 0x7F/0x80: [a-é] = a..0x7F (bitmap) + 0x80..é (code-point range).
  EXPECT(fullmatches("[a-é]", "a"));
  EXPECT(fullmatches("[a-é]", "~")); // 0x7E, in the ASCII part
  EXPECT(fullmatches("[a-é]", a));   // à = 0xE0 <= é = 0xE9
  EXPECT(fullmatches("[a-é]", e));
  EXPECT(!fullmatches("[a-é]", y));  // ÿ = 0xFF > é
  // 2-byte range via \u.
  const std::string u0100 {bytes({0xC4, 0x80})};
  const std::string u017F {bytes({0xC5, 0xBF})};
  const std::string u0180 {bytes({0xC6, 0x80})};
  EXPECT(fullmatches(R"([Ā-ſ])", u0100));
  EXPECT(fullmatches(R"([Ā-ſ])", u017F));
  EXPECT(!fullmatches(R"([Ā-ſ])", u0180));
}

TEST(utf8_class_negation)
{
  // The trap: [^é] must match every code point EXCEPT é (not "any non-ASCII", which would match é).
  const std::string e    {"é"};
  const std::string a    {"à"};
  const std::string u    {"ü"};
  const std::string euro {"€"};
  EXPECT(!fullmatches("[^é]", e));  // é is excluded
  EXPECT(fullmatches("[^é]", a));   // other code points match
  EXPECT(fullmatches("[^é]", u));
  EXPECT(fullmatches("[^é]", euro));
  EXPECT(fullmatches("[^é]", "a")); // ASCII too
  // Negated range.
  EXPECT(!fullmatches("[^à-ÿ]", a));
  EXPECT(!fullmatches("[^à-ÿ]", e));
  EXPECT(fullmatches("[^à-ÿ]", euro));
  EXPECT(fullmatches("[^à-ÿ]", "a"));
  // Overlapping members in a negated class are merged before complementing (é lies inside à-ÿ),
  // so [^à-ÿé] behaves as [^à-ÿ].
  EXPECT(!fullmatches("[^à-ÿé]", a));
  EXPECT(!fullmatches("[^à-ÿé]", e));
  EXPECT(fullmatches("[^à-ÿé]", euro));
}

TEST(utf8_class_security_and_malformed)
{
  const std::string e {"é"};
  // A class carrying specific code points uses the canonical UTF-8-ranges automaton, so it never
  // matches an overlong / surrogate encoding — positive AND negated. (`.` and an ASCII-only negated
  // class like `[^x]` keep the pre-existing "any non-ASCII" superset — see COMPATIBILITY.md.)
  EXPECT(!searches("[é]", bytes({0xC0, 0x80})));                                       // overlong NUL
  EXPECT(searches("[é]", e));
  EXPECT(!fullmatches("[^é]", bytes({0xC0, 0x80})));                                   // negated: overlong not a code point
  EXPECT(!fullmatches("[^é]", bytes({0xED, 0xA0, 0x80})));                             // negated: surrogate encoding excluded
  EXPECT(fullmatches("[^é]", bytes({0xC3, 0xA0})));                                    // but a real other code point matches (à)
  // Malformed member in the pattern -> compile error.
  EXPECT_THROWS(real::regex(cat({"[", bytes({0x80}), "]"})), real::regex_error);       // lone continuation
  EXPECT_THROWS(real::regex(cat({"[", bytes({0xC0, 0x80}), "]"})), real::regex_error); // overlong
  // bytes mode: a non-ASCII class member is still rejected (the compat layer relies on it).
  EXPECT_THROWS(real::regex("[é]", flags::bytes), real::regex_error);
}

// Perimeter pin for a stop-byte scan optimization (OPT-C, C-0): a TEXT-mode class/dot RUN validates
// UTF-8 structure — it stops at a malformed sequence even when that sequence lies BEFORE the class's
// stop byte. So scanning straight to the stop byte (e.g. memchr for the closing quote) would overshoot
// the malformed run and is UNSOUND in text mode. A bytes-mode run is byte-permissive and runs through.
// This fixes the perimeter of any such optimization to bytes mode.
TEST(utf8_text_run_stops_at_malformed_before_the_stop_byte)
{
  // ab | é(C3 A9) | cd | malformed(C3 41) | ef | " (index 10) | tail
  const std::string s {cat({"ab", bytes({0xC3, 0xA9}), "cd", bytes({0xC3, 0x41}), "ef\"tail"})};
  // Text mode: the run consumes ab+é+cd (0..5) then stops at the malformed C3 41 — NOT at the quote.
  EXPECT_EQ(match_len("[^\"]*", s), std::size_t {6});
  EXPECT_EQ(match_len(".*", s), std::size_t {6});
  // ASCII mode changes \w/\d/\s semantics only, not the UTF-8 validation of the run: still stops at 6.
  EXPECT_EQ(match_len("[^\"]*", s, flags::ascii), std::size_t {6});
  // Bytes mode is byte-permissive: every byte != '"' matches, so the run reaches the quote at index 10.
  EXPECT_EQ(match_len("[^\"]*", s, flags::bytes), std::size_t {10});
}

// The SWAR fast path for a code-point-class run (`[^"]+` / `.+` in text mode) must reproduce the
// scalar path exactly — above all it must still STOP at malformed UTF-8 (the C-0 property), and it must
// land word boundaries right (the 8-byte SWAR stride), whether a high byte falls at the run start, just
// before the stop, or after a cluster. `+` (not `*`) so the whole-pattern code-point-class fast path is
// what runs.
TEST(utf8_codepoint_run_swar_matches_the_scalar_path)
{
  // ab | é | cd | malformed C3 41 | ef | " (index 10) : the run stops at the malformed C3 41 (len 6).
  const std::string mal {cat({"ab", bytes({0xC3, 0xA9}), "cd", bytes({0xC3, 0x41}), "ef\"tail"})};
  EXPECT_EQ(match_len("[^\"]+", mal), std::size_t {6});
  EXPECT_EQ(match_len(".+", mal), std::size_t {6});
  // A high byte immediately before the stop, and at the run start.
  EXPECT_EQ(match_len("[^\"]+", cat({"aaa", bytes({0xC3, 0xA9}), "\"x"})), std::size_t {5});
  EXPECT_EQ(match_len("[^\"]+", cat({bytes({0xC3, 0xA9}), "\"z"})), std::size_t {2});
  // ASCII-run lengths across the 8-byte SWAR stride boundaries.
  for (const std::size_t n : {std::size_t {7}, std::size_t {8}, std::size_t {9}, std::size_t {15},
                              std::size_t {16}, std::size_t {17}, std::size_t {64}, std::size_t {65}}) {
    EXPECT_EQ(match_len("[^\"]+", std::string(n, 'a') + "\""), n);
  }
  // A stop right after a multi-byte cluster, and a search that skips leading stops.
  EXPECT_EQ(match_len("[^\"]+", cat({"a", bytes({0xC3, 0xA9}), "\"q"})), std::size_t {3});
  EXPECT_EQ(match_len("[^\"]+", "\"\"aaa\"z"), std::size_t {3});
}

TEST(utf8_bytes_mode_classes)
{
  // In bytes mode a class member >= 0x80 (from \xHH) is a RAW BYTE in the bitmap, not a
  // code point — so a bytes-mode class is byte-for-byte a std::basic_regex<char> class (compat relies
  // on it). The differential vs std::regex lives in test_compat.cpp.
  EXPECT(searches(R"([\xE9])", bytes({0xE9}), flags::bytes));         // the byte 0xE9
  EXPECT(!searches(R"([\xE9])", bytes({0xC3, 0xA9}), flags::bytes));  // NOT the code-point encoding
  EXPECT(searches(R"([\x80-\xFF])", bytes({0xFF}), flags::bytes));
  EXPECT(searches(R"([\x80-\xFF])", bytes({0x80}), flags::bytes));
  EXPECT(!searches(R"([\x80-\xFF])", "a", flags::bytes));
  EXPECT(searches(R"([^\x00-\x7f])", bytes({0xC3}), flags::bytes));   // negated byte class
  // code-point mode: [\xe9] is U+00E9 (unchanged by the fix).
  const std::string e {"é"};
  EXPECT(searches(R"([\xe9])", e));
  EXPECT(!searches(R"([\xe9])", bytes({0xE9})));
}

TEST(utf8_class_lookaround_width)
{
  // A code-point class contributes its real byte width to a fixed-width lookaround (and
  // to the 255-byte cap), not 1. Before the fix [é] counted as 1 byte -> a false (?<![é]) match and a
  // bypassable cap.
  const std::string e {"é"};
  EXPECT(searches("(?=[é])é", e));
  EXPECT(searches("(?<=[é])x", cat({e, "x"})));  // lookbehind width 2 for [é]
  EXPECT(searches("(?<![é])x", "ax"));           // not preceded by é -> matches
  EXPECT(!searches("(?<![é])x", cat({e, "x"}))); // preceded by é -> no match (the false-match fix)

  const auto rejects {[](const std::string& p) {
                        try {
                          const real::regex r(p);
                          return false;
                        }
                        catch (const real::regex_error&) {
                          return true;
                        }
                      }};
  EXPECT(!rejects("(?<=[é]{127})x")); // 127 x 2 = 254 <= 255
  EXPECT(rejects("(?<=[é]{128})x"));  // 128 x 2 = 256 > 255
  EXPECT(!rejects("(?<=[😀]{63})x"));  // 63 x 4 = 252 <= 255
  EXPECT(rejects("(?<=[😀]{64})x"));   // 64 x 4 = 256 > 255
  EXPECT(rejects("(?<=[^é]{64})x"));  // negated class -> 4 bytes -> 256 > 255
}

TEST(utf8_ranges_algorithm_units)
{
  using real::detail::code_range;
  using real::detail::complement_code_ranges;
  using real::detail::encode_utf8_bytes;
  using real::detail::utf8_range_sequences;

  // encode_utf8_bytes: canonical encodings.
  std::uint8_t buf[4] {};
  EXPECT_EQ(encode_utf8_bytes(0x41U, buf), 1U);
  EXPECT_EQ(buf[0], 0x41U);
  EXPECT_EQ(encode_utf8_bytes(0xE9U, buf), 2U);    // é
  EXPECT_EQ(buf[0], 0xC3U);
  EXPECT_EQ(buf[1], 0xA9U);
  EXPECT_EQ(encode_utf8_bytes(0x20ACU, buf), 3U);  // €
  EXPECT_EQ(encode_utf8_bytes(0x1F600U, buf), 4U); // 😀

  // A degenerate range is the single code point's bytes.
  const auto e {utf8_range_sequences(0xE9U, 0xE9U)};
  EXPECT_EQ(e.size(), 1U);
  EXPECT_EQ(e[0].length, 2U);
  EXPECT_EQ(e[0].parts[0].lo, 0xC3U);
  EXPECT_EQ(e[0].parts[0].hi, 0xC3U);

  // The whole 2-byte plane is one canonical sequence [C2-DF][80-BF] (excludes overlong C0/C1).
  const auto two {utf8_range_sequences(0x80U, 0x7FFU)};
  EXPECT_EQ(two.size(), 1U);
  EXPECT_EQ(two[0].parts[0].lo, 0xC2U);
  EXPECT_EQ(two[0].parts[0].hi, 0xDFU);
  EXPECT_EQ(two[0].parts[1].lo, 0x80U);
  EXPECT_EQ(two[0].parts[1].hi, 0xBFU);

  // Length boundaries split into separate sequences (2-byte vs 3-byte, etc.).
  EXPECT(utf8_range_sequences(0x7FFU, 0x800U).size() >= 2U);    // 0x7FF/0x800 boundary
  EXPECT(utf8_range_sequences(0xFFFFU, 0x10000U).size() >= 2U); // 0xFFFF/0x10000 boundary

  // Crossing 0x80 is not this function's job (the parser splits ASCII off); a range starting < 0x80
  // still produces a 1-byte sub-sequence.
  const auto crossing {utf8_range_sequences(0x61U, 0xE9U)};
  EXPECT(crossing.size() >= 2U);
  EXPECT_EQ(crossing[0].parts[0].lo, 0x61U); // the ASCII 1-byte part

  // Surrogates are excluded: a range spanning U+D800..U+DFFF omits them.
  const auto around_surrogate {utf8_range_sequences(0xD000U, 0xE001U)};
  for (const auto& seq : around_surrogate) {
    // No sequence may encode a surrogate: the 3-byte ED A0..BF form must not appear.
    if (seq.length == 3U && seq.parts[0].lo == 0xEDU) {
      EXPECT(seq.parts[1].hi < 0xA0U); // ED continuation stays in 80..9F (U+D000..U+D7FF)
    }
  }

  // Empty / NOP: lo > hi yields nothing.
  EXPECT_EQ(utf8_range_sequences(0x100U, 0x0FFU).size(), 0U);

  // complement_code_ranges over [0x80, 0x10FFFF].
  const auto comp {complement_code_ranges({{.lo = 0xE9U, .hi = 0xE9U}})};
  EXPECT_EQ(comp.size(), 2U);
  EXPECT_EQ(comp[0].lo, 0x80U);
  EXPECT_EQ(comp[0].hi, 0xE8U);
  EXPECT_EQ(comp[1].lo, 0xEAU);
  EXPECT_EQ(comp[1].hi, 0x10FFFFU);

  // Overlapping / adjacent inputs merge before complementing.
  const auto merged {complement_code_ranges({{.lo = 0xE0U, .hi = 0xFFU}, {.lo = 0xE9U, .hi = 0xE9U}})};
  EXPECT_EQ(merged.size(), 2U); // {E0..FF} absorbs {E9}, leaving [80..DF] and [100..10FFFF]
  EXPECT_EQ(merged[0].hi, 0xDFU);
  EXPECT_EQ(merged[1].lo, 0x100U);

  // Complement of the whole space is empty.
  EXPECT_EQ(complement_code_ranges({{.lo = 0x80U, .hi = 0x10FFFFU}}).size(), 0U);
}

TEST(utf8_class_program_size_bounded)
{
  // GAP-2: a large UTF-8 class compiles to a bounded automaton (byte-class subset construction keeps
  // it small); no program-size / DFA-state explosion.
  EXPECT(real::regex("[\\u0080-\\uFFFF]").raw_program().code.size() < 128U);
  EXPECT(real::regex("[\\u0000-\\U0010FFFF]").raw_program().code.size() < 128U);
  EXPECT(real::regex("[^é]").raw_program().code.size() < 128U);
}

TEST(utf8_class_effective_never_match_and_width)
{
  // GPT-5.4 re-audit: one effective (post-negation) class drives BOTH emission and width, so they
  // never disagree — and an impossible class is a never-match, not a crash.
  EXPECT(!searches("[^\\u0000-\\U0010FFFF]", "x"));  // negation of the whole space: matches nothing
  EXPECT(!searches("[^\\u0000-\\U0010FFFF]", "é"));  // (compiles + runs; the HIGH crash is gone)
  EXPECT(fullmatches("[\\u0000-\\U0010FFFF]", "x")); // the whole space matches every code point
  EXPECT(fullmatches("[\\u0000-\\U0010FFFF]", "é"));
  EXPECT(fullmatches("[\\u0000-\\U0010FFFF]", "😀"));

  const auto rejects {[](const std::string& p) {
                        try {
                          const real::regex r(p);
                          return false;
                        }
                        catch (const real::regex_error&) {
                          return true;
                        }
                      }};
  // Negated all-non-ASCII -> effective class is ASCII-only -> lookbehind width 1 (64 <= 255 accepted).
  EXPECT(!rejects("(?<=[^\\x80-\\U0010FFFF]{64})x"));
  EXPECT(fullmatches("[^\\x80-\\U0010FFFF]", "a"));
  EXPECT(!fullmatches("[^\\x80-\\U0010FFFF]", "é"));
  // Negated all-ASCII -> effective class is any non-ASCII -> width 4.
  EXPECT(!rejects("(?<=[^\\x00-\\x7f]{63})x")); // 252 <= 255
  EXPECT(rejects("(?<=[^\\x00-\\x7f]{64})x"));  // 256 > 255
  EXPECT(fullmatches("[^\\x00-\\x7f]", "é"));
  EXPECT(!fullmatches("[^\\x00-\\x7f]", "a"));

  // An impossible class contributes width 0 (never-match, not empty-match): a dead branch in a
  // bounded lookaround is accepted and never inflates the width, and the assertion still fails.
  EXPECT(!rejects("(?<=a|[^\\u0000-\\U0010FFFF]{300})x"));        // dead branch counts 0 -> alternation width 1
  EXPECT(searches("(?<=a|[^\\u0000-\\U0010FFFF]{300})x", "ax"));  // matches via the live `a` branch
  EXPECT(!searches("(?<=a|[^\\u0000-\\U0010FFFF]{300})x", "bx")); // the dead branch never matches
  EXPECT(!searches("(?<=[^\\u0000-\\U0010FFFF])x", "ax"));        // width 0 is never-match ...
  EXPECT(searches("(?<![^\\u0000-\\U0010FFFF])x", "ax"));         // ... so the negative form is always true
}

TEST(utf8_malformed_subject_iteration_is_boundary_aligned)
{
  // Malformed-subject policy: empty-match iteration advances to the next code-point boundary (a
  // non-continuation byte), the same boundary the matcher seeds at — so it always makes progress and
  // stays code-point-aligned. A malformed continuation run is stepped over as one unit.
  const real::regex xs("x*");
  const std::string overlong   {bytes({0xC0, 0x80})};
  const std::string bad_leads  {bytes({0xF5, 0xF6})};
  const std::string lead_ascii {cat({"a", bytes({0xC3}), "b"})};
  // Overlong C0 80: boundaries at 0 and 2 (0x80 is a continuation, not a start) -> 2 empty matches.
  EXPECT_EQ(xs.find_all(overlong).size(), 2U);
  // Two invalid leads F5 F6 (neither is a continuation): boundaries 0,1,2 -> 3 empty matches.
  EXPECT_EQ(xs.find_all(bad_leads).size(), 3U);
  // A lone lead then ASCII: a\xC3b -> boundaries 0,1,2,3 (the 'b' is not a continuation) -> 4.
  EXPECT_EQ(xs.find_all(lead_ascii).size(), 4U);

  // split and replace over malformed input observe the same boundary policy and never stall.
  // Nullable split at every boundary gap: one empty-delimited piece per boundary.
  EXPECT_EQ(xs.split(overlong).size(), xs.find_all(overlong).size() + 1);
  EXPECT_EQ(xs.split(bad_leads).size(), 4U);                                  // 3 empty matches -> 4 pieces
  // replace with a nullable pattern inserts at each boundary without dropping the malformed bytes.
  EXPECT_EQ(xs.replace(overlong, "-"), cat({"-", bytes({0xC0, 0x80}), "-"})); // boundaries 0 and 2
  EXPECT_EQ(xs.replace(bad_leads, "-"), cat({"-", bytes({0xF5}), "-", bytes({0xF6}), "-"}));
  EXPECT_EQ(xs.replace(lead_ascii, "-"),
            cat({"-", "a", "-", bytes({0xC3}), "-", "b", "-"}));              // every byte preserved
}

TEST(icase_unicode_folding_contract)
{
  using real::flags;
  const flags i                {flags::icase};
  // The canonical fold contract, end to end (matching, not just the table).
  const std::string e          {"é"};
  const std::string E          {"É"};
  const std::string SS         {"ẞ"};
  const std::string Kelvin     {bytes({0xE2, 0x84, 0xAA})};
  const std::string longs      {bytes({0xC5, 0xBF})}; // ſ
  const std::string dotI       {bytes({0xC4, 0xB0})}; // İ
  const std::string dotless    {bytes({0xC4, 0xB1})}; // ı
  const std::string sigma      {"σ"};
  const std::string Sigma      {"Σ"};
  const std::string finalSigma {"ς"};

  // Literals fold (é/É, ß/ẞ but ß != ss, k/K/Kelvin, i cluster, s/ſ, sigma cluster).
  EXPECT(fullmatches("é", E, i));
  EXPECT(fullmatches("ß", SS, i));
  EXPECT(!fullmatches("ß", "ss", i)); // simple fold, not full (ß is not ss)
  EXPECT(fullmatches("k", Kelvin, i));
  EXPECT(fullmatches("i", dotI, i));
  EXPECT(fullmatches("i", dotless, i));
  EXPECT(fullmatches("s", longs, i));
  EXPECT(fullmatches("σ", finalSigma, i)); // σ / ς
  EXPECT(fullmatches("σ", Sigma, i));      // σ / Σ
  EXPECT(fullmatches("Σ", sigma, i));      // and back

  // k in a class AND its negation.
  EXPECT(fullmatches("[k]", Kelvin, i));
  EXPECT(!fullmatches("[^k]", Kelvin, i)); // negation is of the FOLDED set {k,K,Kelvin}
  EXPECT(fullmatches("[^k]", e, i));       // but other code points still match

  // [a-z] under icase attracts the non-ASCII fold partners of its letters.
  EXPECT(fullmatches("[a-z]", Kelvin, i));
  EXPECT(fullmatches("[a-z]", longs, i));
  EXPECT(fullmatches("[a-z]", dotI, i));
  // [^\x00-\x7f] (all non-ASCII) under icase excludes those attracted partners.
  EXPECT(!fullmatches("[^\\x00-\\x7f]", Kelvin, i));
  EXPECT(!fullmatches("[^\\x00-\\x7f]", longs, i));

  // \xHH keeps byte provenance: never folds, even under icase.
  EXPECT(!fullmatches("\\xE9", E, i));               // \xE9 (byte 0xE9) does not match É
  EXPECT(fullmatches("\\xE9", bytes({0xE9}), i));    // it matches the raw byte

  // Lookbehind width uses the folded class: k folds to include Kelvin (3 bytes) -> width 3.
  const auto rejects {[](const std::string& p) {
                        try {
                          const real::regex r(p, flags::icase);
                          return false;
                        }
                        catch (const real::regex_error&) {
                          return true;
                        }
                      }};
  EXPECT(!rejects("(?<=k{85})x")); // 85 x 3 = 255 <= 255
  EXPECT(rejects("(?<=k{86})x"));  // 86 x 3 = 258 > 255

  // ASCII-only fold has no non-ASCII contamination: 'a' matches only a / A.
  EXPECT(fullmatches("a", "A", i));
  EXPECT(!fullmatches("a", e, i));
}

TEST(icase_range_fold_coalescing_membership)
{
  using real::flags;
  const flags i {flags::icase};
  // The compiler coalesces the fragmented fold of [a-é] (133 -> 43 ops) WITHOUT changing the accepted set.
  // The membership contract (oracle: re.IGNORECASE) is unchanged across the dedupe/merge:
  EXPECT(fullmatches("[a-é]", "É", i));                       // partner already inside the range
  EXPECT(fullmatches("[a-é]", bytes({0xE2, 0x84, 0xAA}), i)); // Kelvin (k's partner), OUT of range
  EXPECT(fullmatches("[a-é]", bytes({0xC3, 0xBE}), i));       // þ U+00FE (from Þ-fold) just above range
  EXPECT(fullmatches("[a-é]", bytes({0xC5, 0xBF}), i));       // ſ U+017F (from s)
  EXPECT(!fullmatches("[a-é]", bytes({0xC4, 0x80}), i));      // Ā U+0100 unrelated, just above -> no
  EXPECT(!fullmatches("[a-é]", bytes({0xC3, 0xBF}), i));      // ÿ U+00FF (Ÿ not in the range) -> no
  // Boundary: an ASCII range folds ASCII partners only; 0x7F and U+0080 never leak in.
  EXPECT(!fullmatches("[a-z]", bytes({0x7F}), i));
  EXPECT(!fullmatches("[a-z]", bytes({0xC2, 0x80}), i));      // U+0080
}

TEST(icase_byte_escape_ascii_folds_by_provenance)
{
  using real::flags;
  const flags       i      {flags::icase};
  const std::string Kelvin {bytes({0xE2, 0x84, 0xAA})};
  // CF-fix: a \xHH / octal escape with value < 0x80 is an ASCII character (byte == code point), so a
  // cased one folds under icase exactly like a raw ASCII literal (\x4B == K == \113).
  EXPECT(fullmatches("\\x4B", "k", i));
  EXPECT(fullmatches("\\x4B", "K", i));
  EXPECT(fullmatches("\\x4B", Kelvin, i)); // text mode: full Unicode fold
  EXPECT(fullmatches("\\113", "k", i));    // octal 0113 == 'K'
  EXPECT(fullmatches("\\113", Kelvin, i));
  // bytes mode: ASCII fold only (no Kelvin), matching std::basic_regex<char>.
  EXPECT(fullmatches("\\x4B", "k", flags::bytes | i));
  EXPECT(!fullmatches("\\x4B", Kelvin, flags::bytes | i));
  // A value >= 0x80 keeps byte provenance and is NEVER folded (the unchanged text-mode divergence).
  EXPECT(!fullmatches("\\xE9", "É", i));
  EXPECT(fullmatches("\\xE9", bytes({0xE9}), i));
  // Non-cased escapes are zero-overhead byte literals.
  EXPECT(fullmatches("\\x30", "0", i));
  EXPECT(!fullmatches("\\x30", "1", i));
}

TEST(icase_directed_lookbehind_and_static)
{
  using real::flags;
  const std::string Kelvin {bytes({0xE2, 0x84, 0xAA})};
  // Negative lookbehind of a folded class: (?<![k])x is false right after Kelvin (Kelvin IS a k).
  EXPECT(!searches("(?<![k])x", cat({Kelvin, "x"}), flags::icase));
  EXPECT(searches("(?<![k])x", cat({"a", "x"}), flags::icase)); // but true after a non-k
  // static_regex with a non-ASCII icase literal folds at compile time.
  static_assert(real::static_regex<"(?i)café">().fullmatch("CAFÉ").matched());
  EXPECT(real::static_regex<"(?i)café">().fullmatch("CAFÉ").matched());
}
