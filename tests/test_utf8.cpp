// UTF-8 literals (arc UTF-8, U1): in code-point mode (the default, !flags::bytes) a raw multi-byte
// character is decoded to one atom — the same emission as `\uHHHH` — so a following quantifier applies
// to the whole code point, not just its last byte (the é+ bug). Malformed UTF-8 in the pattern is a
// compile error, not a silent literal. flags::bytes keeps raw byte semantics (the compat layer relies
// on it). This file's source is UTF-8; the malformation / boundary cases are built from raw bytes.
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"
#include "real/utf8.hpp"

using real::flags;
using real::regex;

namespace {

  // A byte string from explicit byte values (for boundary / malformed sequences).
  std::string bytes(std::initializer_list<int> values)
  {
    std::string result;
    for (const int value : values) {
      result += static_cast<char>(value);
    }
    return result;
  }

  // Whole-match byte length of the leftmost match, or std::string::npos if there is no match.
  std::size_t match_len(const std::string& pattern,
                        const std::string& text,
                        flags              f = flags::none)
  {
    const regex r(pattern, f);
    const auto  m {r.search(text)};
    return m.matched() ? m.end(0) - m.start(0) : std::string::npos;
  }

  // Concatenate parts without a chained operator+ (which allocates intermediate temporaries).
  std::string cat(std::initializer_list<std::string_view> parts)
  {
    std::string result;
    for (const std::string_view part : parts) {
      result.append(part);
    }
    return result;
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
  // U2: a character class carries specific non-ASCII code points, not "any non-ASCII".
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
