// Character classes, escape sequences and the `.` metacharacter, including
// the UTF-8 whole-codepoint guarantees of negated classes and dot.
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(simple_class_and_ranges)
{
  EXPECT(real::regex("[abc]").fullmatch("b"));
  EXPECT(!real::regex("[abc]").fullmatch("d"));
  EXPECT(real::regex("[a-z]").fullmatch("q"));
  EXPECT(!real::regex("[a-z]").fullmatch("Q"));
  EXPECT(real::regex("[a-zA-Z0-9_]").fullmatch("X"));
  EXPECT(real::regex("[a-c-e]").fullmatch("-")); // second '-' is literal
  EXPECT(real::regex("ba[dg]").search("a bag").start() == 2U);
}

TEST(class_literal_dash_and_bracket)
{
  EXPECT(real::regex("[-a]").fullmatch("-"));
  EXPECT(real::regex("[a-]").fullmatch("-"));
  EXPECT(real::regex("[]a]").fullmatch("]"));  // ']' first: literal
  EXPECT(real::regex("[^]a]").fullmatch("b")); // also after '^'
  EXPECT(!real::regex("[^]a]").fullmatch("]"));
}

TEST(negated_class_ascii)
{
  const real::regex rx("[^abc]");
  EXPECT(rx.fullmatch("d"));
  EXPECT(rx.fullmatch("\n")); // unlike '.', negation does not exclude \n
  EXPECT(!rx.fullmatch("a"));
  EXPECT(!rx.fullmatch(""));
}

TEST(negated_class_matches_whole_non_ascii_codepoint)
{
  // 'é' is two bytes (0xC3 0xA9); a negated class must consume both.
  EXPECT(real::regex("[^a]").fullmatch("é"));
  EXPECT(real::regex("[^a][^a]").fullmatch("éé"));
  EXPECT(real::regex("x[^a]y").fullmatch("xéy"));
  EXPECT(real::regex("[^a]").fullmatch("⊕"));   // 3 bytes
  EXPECT(real::regex("[^a]").fullmatch("𝄞"));   // 4 bytes
  EXPECT(!real::regex("[^a]").fullmatch("ab")); // exactly one codepoint
}

TEST(escape_classes_outside_brackets)
{
  EXPECT(real::regex("\\d\\d").fullmatch("42"));
  EXPECT(!real::regex("\\d").fullmatch("x"));
  EXPECT(real::regex("\\D").fullmatch("x"));
  EXPECT(real::regex("\\D").fullmatch("é")); // non-ASCII is a non-digit
  EXPECT(!real::regex("\\D").fullmatch("7"));
  EXPECT(real::regex("\\w").fullmatch("_"));
  EXPECT(!real::regex("\\w").fullmatch("-"));
  EXPECT(real::regex("\\W").fullmatch("-"));
  EXPECT(real::regex("\\s").fullmatch(" "));
  EXPECT(real::regex("\\s").fullmatch("\t"));
  EXPECT(real::regex("\\S").fullmatch("x"));
  EXPECT(!real::regex("\\S").fullmatch(" "));
}

TEST(escape_classes_inside_brackets)
{
  EXPECT(real::regex("[\\d]").fullmatch("5"));
  EXPECT(real::regex("[\\dab]").fullmatch("a"));
  EXPECT(real::regex("[\\w\\s]").fullmatch(" "));
  EXPECT(real::regex("[^\\d]").fullmatch("x"));
  EXPECT(!real::regex("[^\\d]").fullmatch("5"));
  EXPECT(real::regex("[\\b]").fullmatch("\x08")); // backspace inside class
}

TEST(control_and_hex_escapes)
{
  EXPECT(real::regex("\\n").fullmatch("\n"));
  EXPECT(real::regex("\\t").fullmatch("\t"));
  EXPECT(real::regex("\\r\\f\\v\\a").fullmatch("\r\f\v\a"));
  EXPECT(real::regex("\\0").fullmatch("\0"sv));
  EXPECT(real::regex("\\x41\\x62").fullmatch("Ab"));
  EXPECT(real::regex("[\\x30-\\x39]").fullmatch("7"));
}

TEST(octal_escapes)
{
  // \0 plus up to two octal digits, and a 1-7 digit followed by two more octal digits, are
  // octal byte escapes (value & 0xff), like \xHH.
  EXPECT(real::regex("\\012").fullmatch("\n"sv));         // 0o12 == '\n'
  EXPECT(real::regex("\\101\\102").fullmatch("AB"sv));    // 0o101 'A', 0o102 'B'
  EXPECT(real::regex("\\123").fullmatch("S"sv));          // 0o123 'S'
  EXPECT(real::regex("\\00").fullmatch("\0"sv));
  EXPECT(real::regex("\\000").fullmatch("\0"sv));
  EXPECT(real::regex("a\\060b").fullmatch("a0b"sv));      // 0o60 == '0'
  EXPECT_THROWS(real::regex("\\400"), real::regex_error); // 0o400 > 0o377, like re
}

TEST(backreferences_are_rejected)
{
  // Backreferences are unsupported; \1 (and \8 \9, which re reads as group refs, and \12 which
  // is not a 3-octal run) are a clear error rather than silently mis-parsed.
  EXPECT_THROWS(real::regex("(a)\\1"), real::regex_error);
  EXPECT_THROWS(real::regex("\\1"), real::regex_error);
  EXPECT_THROWS(real::regex("\\8"), real::regex_error);
  EXPECT_THROWS(real::regex("\\12"), real::regex_error);
}

TEST(unicode_codepoint_escapes)
{
  // \u / \U decode a code point and emit its UTF-8 bytes (str mode), like a literal char.
  EXPECT(real::regex("\\u00e9").fullmatch("é"sv));                // U+00E9 -> C3 A9
  EXPECT(real::regex("a\\u00e9b").fullmatch("aéb"sv));
  EXPECT(real::regex("\\u0041").fullmatch("A"));                  // ASCII code point
  EXPECT(real::regex("\\u20ac").fullmatch("€"sv));                // 3-byte: E2 82 AC
  EXPECT(real::regex("\\U0001F600").fullmatch("😀"sv));            // 4-byte: F0 9F 98 80
  EXPECT_EQ(real::regex("\\u00e9+").search("ééé"sv)[0], "ééé"sv); // quantifies the whole codepoint
  // Inside a class: ASCII members are fine, a non-ASCII code point is rejected like a literal é.
  EXPECT(real::regex("[\\u0041\\u0042]").fullmatch("B"));
  EXPECT_THROWS(real::regex("[\\u00e9]"), real::regex_error);
}

TEST(unicode_escape_rejections)
{
  EXPECT_THROWS(real::regex("\\u00e9", real::flags::bytes), real::regex_error); // not in bytes mode
  EXPECT_THROWS(real::regex("[\\u0041]", real::flags::bytes), real::regex_error);
  EXPECT_THROWS(real::regex("\\uD800"), real::regex_error);                     // surrogate
  EXPECT_THROWS(real::regex("\\U00110000"), real::regex_error);                 // > U+10FFFF
  EXPECT_THROWS(real::regex("\\u00e"), real::regex_error);                      // incomplete (3 hex)
  EXPECT_THROWS(real::regex("\\U0001F60"), real::regex_error);                  // incomplete (7 hex)
  EXPECT_THROWS(real::regex("\\N{BULLET}"), real::regex_error);                 // named unicode escape
  EXPECT_THROWS(real::regex("[\\N{BULLET}]"), real::regex_error);
}

TEST(dot_matches_one_codepoint_except_newline)
{
  EXPECT(real::regex(".").fullmatch("a"));
  EXPECT(real::regex(".").fullmatch("é"));
  EXPECT(real::regex(".").fullmatch("𝄞"));
  EXPECT(!real::regex(".").fullmatch("ab"));
  EXPECT(!real::regex(".").fullmatch("éa"));
  EXPECT(!real::regex(".").fullmatch("\n"));
  EXPECT(real::regex("a.c").fullmatch("abc"));
  EXPECT(real::regex("a.c").fullmatch("aéc"));
  EXPECT(!real::regex("a.c").fullmatch("a\nc"));
}

TEST(utf8_literals_still_match_their_bytes)
{
  EXPECT(real::regex("é").fullmatch("é"));
  EXPECT(real::regex("héllo").search("say héllo").start() == 4U);
}

TEST(bytes_mode_matches_raw_bytes)
{
  using real::flags;
  // Binary mode: . and [^…] are raw-byte complements, no UTF-8 structure.
  EXPECT(real::regex("[^;]", flags::bytes).fullmatch("\xFF"sv));
  EXPECT(!real::regex("[^;]", flags::bytes).fullmatch(";"));
  EXPECT(real::regex(".", flags::bytes).fullmatch("\xC3"sv)); // lone lead byte
  EXPECT(!real::regex(".", flags::bytes).fullmatch("é"sv));   // 2 bytes now
  EXPECT(real::regex("..", flags::bytes).fullmatch("é"sv));
  EXPECT(real::regex("\\D", flags::bytes).fullmatch("\x80"sv));
  EXPECT(!real::regex(".", flags::bytes).fullmatch("\n"));
  EXPECT(real::regex(".", flags::bytes | flags::dotall).fullmatch("\n"));
  // Empty matches advance one byte, not one codepoint.
  const real::regex xs("x*", flags::bytes);
  EXPECT_EQ(xs.find_all("é"sv).size(), 3U); // positions 0, 1, 2
}

TEST(invalid_utf8_subjects_make_progress)
{
  // Truncated/invalid sequences never stall iteration (advance >= 1 byte).
  const real::regex xs("x*");
  EXPECT_EQ(xs.find_all("a\xC3"
                        "b"sv)
            .size(),
            4U);                 // 0, 1, 2 (after the lone lead byte), 3
  const real::regex dot(".");
  EXPECT(!dot.search("\xC3"sv)); // lead byte without continuation: no dot
}

TEST(class_errors)
{
  EXPECT_THROWS(real::regex("[abc"), real::regex_error);
  EXPECT_THROWS(real::regex("[z-a]"), real::regex_error);
  EXPECT_THROWS(real::regex("[a-\\d]"), real::regex_error);
  EXPECT_THROWS(real::regex("[\\D]"), real::regex_error);
  EXPECT_THROWS(real::regex("[é]"), real::regex_error);
  EXPECT_THROWS(real::regex("\\x4"), real::regex_error);
  EXPECT_THROWS(real::regex("\\xg0"), real::regex_error);
  EXPECT_THROWS(real::regex("\\p"), real::regex_error);
}

TEST(unterminated_class_reports_open_bracket_position)
{
  try {
    real::regex rx("ab[cd");
    EXPECT(false);
  }
  catch (const real::regex_error& ex) {
    EXPECT_EQ(ex.position(), 2U);
  }
}

TEST(codepoint_class_fast_path)
{
  // `.` and negated classes (optionally greedy `+`) take a codepoint-aware
  // fast path; results must equal the general engine, including on multi-byte
  // and malformed UTF-8.
  const real::regex notcomma("[^,]+");
  const auto        runs = notcomma.find_all("a,bb,ccc");
  EXPECT_EQ(runs.size(), 3U);
  EXPECT_EQ(runs[2][0], "ccc"sv);
  // Multi-byte codepoints are consumed whole (é is two bytes).
  EXPECT_EQ(notcomma.search("café,x")[0], "café"sv);
  // `.` excludes newline; `.+` stops at it.
  const real::regex dotplus(".+");
  EXPECT_EQ(dotplus.find_all("a\nbb\nccc").size(), 3U);
  // Bare dot matches one codepoint at a time.
  const real::regex dot(".");
  EXPECT_EQ(dot.find_all("ab").size(), 2U);
  EXPECT(dot.fullmatch("é")); // one multi-byte codepoint
  // Malformed UTF-8: a lone continuation byte is not a codepoint, so [^,]
  // does not match it (exactly like the general engine).
  const char        bad[] {'a', static_cast<char>(0x80), 'b'};
  const std::string text(bad, sizeof bad);
  EXPECT_EQ(notcomma.find_all(text).size(), 2U); // "a" and "b", the 0x80 skipped
}
