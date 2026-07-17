// Character classes, escape sequences and the `.` metacharacter, including
// the UTF-8 whole-codepoint guarantees of negated classes and dot.
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"
#include "real/core/charclass.hpp"
#include "real/unicode/unicode_props.hpp"

using namespace std::string_view_literals;

// Drift-guard: the hand-written ASCII bitsets (`word_set`/`digit_set`/`space_set`, tier-1 core) must agree
// with the oracle-generated code-point ranges (`is_*_cp`, tier-2 unicode, built from `re.fullmatch`) over
// the whole ASCII range — EXCEPT U+001C-U+001F (FS/GS/RS/US), where `space_set` (ASCII-mode `\s`,
// `flags::ascii`/`flags::bytes`) and `is_space_cp` (text/Unicode-mode `\s`) legitimately DIVERGE: Python
// `re.match(r"(?a)\s", "\x1c")` does not match, `re.match(r"\s", "\x1c")` (no ascii flag) does. `space_set`
// once wrongly included those four (conflating `str.isspace()`/text-mode `\s` with ASCII-mode `\s`, which
// are different predicates) — found live by differential fuzzing (`(?a)\s` matching `'\x1c'` where `re`
// does not) and fixed by removing them; this test pins the CORRECT divergence so a future "fix" cannot
// silently re-merge the two sets. Core cannot include the generated tables (a layer violation), so the two
// are separate sources of truth by design; only `\s` has this ASCII/Unicode divergence -- `\w`/`\d` do not.
TEST(ascii_shorthands_match_generated_ranges)
{
  using namespace real::detail;
  for (unsigned c = 0; c < 128; ++c) {
    const auto     b  {static_cast<std::uint8_t>(c)};
    const char32_t cp {c};
    EXPECT(word_set().test(b) == is_word_cp(cp));
    EXPECT(digit_set().test(b) == is_digit_cp(cp));
    if (c >= 0x1C && c <= 0x1F) {
      EXPECT(!space_set().test(b));  // ASCII-mode \s: FS/GS/RS/US are NOT whitespace (re.match(r"(?a)\s", ...))
      EXPECT(is_space_cp(cp));       // text/Unicode-mode \s: they ARE (re.match(r"\s", ...), no ascii flag)
    }
    else {
      EXPECT(space_set().test(b) == is_space_cp(cp));
    }
  }
}

TEST(space_matches_cpython_file_separators_in_text_mode_only)
{
  // Python re's TEXT-mode `\s` (no ascii flag) matches U+001C-U+001F (str.isspace); ASCII-mode `\s`
  // (flags::ascii, and bytes mode) does NOT — pin both directions across `\s`, `\S`, `[\s]`, `[^\s]`, at
  // the matching level (not just the class definition) — the regression the ASCII set once carried.
  for (char sep = 0x1C; sep <= 0x1F; ++sep) {
    const std::string s(1, sep);
    EXPECT(real::regex(R"(\s)").fullmatch(s));  // text mode: matches
    EXPECT(!real::regex(R"(\S)").fullmatch(s));
    EXPECT(real::regex(R"([\s])").fullmatch(s));
    EXPECT(!real::regex(R"([^\s])").fullmatch(s));

    EXPECT(!real::regex(R"((?a)\s)").fullmatch(s)); // ASCII mode: does NOT match
    EXPECT(real::regex(R"((?a)\S)").fullmatch(s));
    EXPECT(!real::regex(R"((?a)[\s])").fullmatch(s));
    EXPECT(real::regex(R"((?a)[^\s])").fullmatch(s));

    EXPECT(!real::regex(R"(\s)", real::flags::bytes).fullmatch(s)); // bytes mode: same as ASCII
    EXPECT(!real::regex(R"(\s)", real::flags::ascii).fullmatch(s)); // API flag: same as inline (?a)
  }
  EXPECT(real::regex(R"(\s)").fullmatch(" "sv));  // ordinary whitespace still matches, both modes
  EXPECT(real::regex(R"((?a)\s)").fullmatch(" "sv));
  EXPECT(!real::regex(R"(\s)").fullmatch("a"sv)); // a letter does not
}

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
  // Inside a class (code-point mode): ASCII members and non-ASCII code points are both members.
  EXPECT(real::regex("[\\u0041\\u0042]").fullmatch("B"));
  EXPECT(real::regex("[\\u00e9]").fullmatch("é"sv));              // é is a class member
  EXPECT(!real::regex("[\\u00e9]").fullmatch("à"sv));             // and only that code point
  EXPECT(real::regex("[é]").fullmatch("é"sv));                    // [é] == [é]
  EXPECT(!real::regex("[é]").fullmatch("à"sv));
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

TEST(raw_byte_escape_requires_bytes_mode)
{
  // \C (RE2's raw-byte escape hatch, D1 volet A of the RE2 drop-in / issue #2): rejected in plain text
  // mode -- its span can land mid-codepoint, which corrupts a byte-to-char binding conversion (the same
  // reasoning \u/\U keep in bytes mode, mirrored the other direction).
  EXPECT_THROWS(real::regex(R"(\C)"), real::regex_error);
  EXPECT_THROWS(real::regex(R"(\C+)"), real::regex_error);
  EXPECT_THROWS(real::regex(R"(a\Cb)"), real::regex_error);
}

TEST(raw_byte_escape_allow_raw_byte_widens_the_gate)
{
  using real::flags;
  // flags::allow_raw_byte (D1, the RE2-compat \C-in-text-mode completion): a second, independent gate
  // for byte-offset-native consumers only (e.g. real::compat::re2) -- \C itself is unaffected, still
  // always exactly one raw byte, possibly mid-codepoint.
  EXPECT(real::regex(R"(\C)", flags::allow_raw_byte).fullmatch("\xC3"sv));
  EXPECT(real::regex(R"(a\Cb)", flags::allow_raw_byte).fullmatch("aXb"sv));
  EXPECT_THROWS(real::regex(R"(\C)", flags::none), real::regex_error); // neither gate -> still rejected
  // \C mixed with codepoint-aware constructs in the SAME program (the D0 spike's own question): the
  // whole rest of the pattern stays codepoint-aware, only \C itself descends to the byte.
  {
    const auto m {real::regex(R"(caf\C)", flags::allow_raw_byte).search("caf\xC3\xA9"sv)};
    EXPECT(m.matched());
    EXPECT_EQ(m.start(0), std::size_t {0});
    EXPECT_EQ(m.end(0), std::size_t {4}); // "caf" (3 codepoint-aware bytes) + \C's first byte of é
  }
  EXPECT(real::regex(R"([a-z]+\C[a-z]+)", flags::allow_raw_byte).search("abc\xC3xyz"sv).matched());
}

TEST(raw_byte_escape_matches_exactly_one_byte)
{
  using real::flags;
  // \C matches any single byte, unconditionally -- including a literal '\n' (unlike '.', even under
  // bytes|dotall) and half of a multi-byte codepoint (RE2's own semantics: \C descends to the byte even
  // under RE2's default UTF-8 mode).
  EXPECT(real::regex(R"(\C)", flags::bytes).fullmatch("\n"));
  EXPECT(real::regex(R"(\C)", flags::bytes).fullmatch("\xFF"sv));
  EXPECT(!real::regex(R"(\C)", flags::bytes).fullmatch("\xC3\xA9"sv));  // é, 2 bytes: \C alone is too short
  EXPECT(real::regex(R"(\C\C)", flags::bytes).fullmatch("\xC3\xA9"sv)); // 2x \C spans the same 2 bytes
  const auto m {real::regex(R"(\C)", flags::bytes).search("\xC3\xA9"sv)};
  EXPECT(m.matched());
  EXPECT_EQ(m.start(0), std::size_t {0});
  EXPECT_EQ(m.end(0), std::size_t {1});                                             // mid-codepoint span -- exactly RE2's own byte-offset semantics
  // Tier-1 possessive path (\C++ / \C*+): is_single_atom already covers node_kind::any, so \C is eligible;
  // exercises the emit_tier1_atom_test duplicate of the emit_node byte-klass shape.
  EXPECT(real::regex(R"(\C++)", flags::bytes).fullmatch("\xC3\xA9\xE2\x82\xAC"sv)); // 2+3 = 5 bytes total
  EXPECT(real::regex(R"(\Ca\C)", flags::bytes).fullmatch("X"
                                                         "a"
                                                         "\xC3"sv));
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
  EXPECT_THROWS(real::regex("[a-\\d]"), real::regex_error);                 // \d is not a range endpoint
  EXPECT_THROWS(real::regex("[é]", real::flags::bytes), real::regex_error); // non-ASCII class member: bytes mode only
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

TEST(unicode_shorthand_d_s_text_mode)
{
  // In text mode \d \s match Unicode (\w \b were still ASCII at this stage). Oracle is re: \d == Nd,
  // \s == Unicode whitespace. \D \S are their complements over all code points.
  EXPECT(real::regex("\\d").fullmatch("٣"));  // ARABIC-INDIC DIGIT THREE (Nd)
  EXPECT(real::regex("\\d").fullmatch("９"));  // FULLWIDTH DIGIT NINE (Nd)
  EXPECT(!real::regex("\\d").fullmatch("½")); // VULGAR FRACTION HALF (No) is not a \d digit
  EXPECT(real::regex("\\d").fullmatch("7"));  // ASCII still matches
  EXPECT(real::regex("\\s").fullmatch(" "));  // NBSP
  EXPECT(real::regex("\\s").fullmatch(" "));  // LINE SEPARATOR
  EXPECT(real::regex("\\s").fullmatch(" "));  // ASCII space still matches
  EXPECT(!real::regex("\\s").fullmatch("é"));
  // Negations: \D \S match any code point outside the (Unicode) set.
  EXPECT(real::regex("\\D").fullmatch("é"));
  EXPECT(!real::regex("\\D").fullmatch("٣"));
  EXPECT(real::regex("\\S").fullmatch("é"));
  EXPECT(!real::regex("\\S").fullmatch(" "));
  // In a class: [\d] == \d, [^\d] == \D (Unicode ranges included).
  EXPECT(real::regex("[\\d]").fullmatch("٣"));
  EXPECT(real::regex("[^\\d]").fullmatch("é"));
  EXPECT(!real::regex("[^\\d]").fullmatch("٣"));
  EXPECT(real::regex("[\\d.]+").fullmatch("٣.5")); // Unicode digit mixed with a literal member
  // icase is a no-op on \d \s (they are not cased).
  EXPECT(real::regex("\\d", real::flags::icase).fullmatch("٣"));
}

TEST(unicode_shorthand_ascii_flag_reverts)
{
  using real::flags;
  // flags::ascii (re.A) keeps \d \s ASCII even in text mode; \w unchanged.
  EXPECT(!real::regex("\\d", flags::ascii).fullmatch("٣"));
  EXPECT(real::regex("\\d", flags::ascii).fullmatch("7"));
  EXPECT(!real::regex("\\s", flags::ascii).fullmatch(" "));
  EXPECT(!real::regex("[\\d]", flags::ascii).fullmatch("٣"));
  EXPECT(real::regex("(?a)\\d").fullmatch("7") && !real::regex("(?a)\\d").fullmatch("٣")); // inline (?a)
  // Under ascii+icase folding is strictly ASCII, matching re.A|re.IGNORECASE: k folds to K only, never
  // to Kelvin (U+212A) -- re does the same (an earlier "CPython wart" note was a corrupted-probe error).
  EXPECT(!real::regex("k", flags::ascii | flags::icase).fullmatch("K"));                   // Kelvin
  EXPECT(real::regex("k", flags::ascii | flags::icase).fullmatch("K"));                    // ASCII fold still works
  EXPECT(!real::regex("é", flags::ascii | flags::icase).fullmatch("É"));                   // no Unicode fold
  // Bytes mode is unchanged (ASCII shorthands).
  EXPECT(real::regex("\\d", flags::bytes).fullmatch("7"));
}

TEST(unicode_shorthand_lookbehind_budget)
{
  using real::flags;
  // \d in text mode spans up to 4 bytes, so the fixed-width lookbehind budget (255 bytes) tightens:
  // \d{63} == 252 bytes compiles, \d{64} == 256 is rejected. Under ascii, \d is 1 byte again.
  const std::string h63  {std::string(63, '7') + "x"};
  const std::string h255 {std::string(255, '7') + "x"};
  EXPECT(real::regex("(?<=\\d{63})x").search(h63).matched());
  EXPECT_THROWS(real::regex("(?<=\\d{64})x"), real::regex_error);
  EXPECT(real::regex("(?<=\\d{255})x", flags::ascii).search(h255).matched());
}

TEST(unicode_shorthand_in_class_text_mode)
{
  // A shorthand inside a class is Unicode in text mode, and \W \D \S in a class are accepted
  // (they were rejected before). Membership matches re; the identities [^\W]==\w etc hold.
  EXPECT(real::regex("[\\w]").fullmatch("é"));  // é is a word char (was ASCII-only when \w was byte-level)
  EXPECT(real::regex("[\\w-]+").fullmatch("café-au"));
  EXPECT(real::regex("[\\d]").fullmatch("٣"));  // Arabic digit
  EXPECT(real::regex("[\\s]").fullmatch(" "));  // NBSP
  // \W \D \S in a class (accepted, ~ complement).
  EXPECT(real::regex("[\\W]").fullmatch("€"));  // € is a non-word code point
  EXPECT(!real::regex("[\\W]").fullmatch("é")); // é is a word char, excluded from \W
  EXPECT(real::regex("[\\D]").fullmatch("é"));  // é is a non-digit
  EXPECT(!real::regex("[\\D]").fullmatch("٣")); // ٣ is a digit, excluded from \D
  EXPECT(real::regex("[a\\W]").fullmatch("a")); // 'a' plus the non-word complement
  EXPECT(real::regex("[a\\W]").fullmatch("€"));
  // Identities: [^\W] == \w, [^\D] == \d, [^\S] == \s (ranges included).
  EXPECT(real::regex("[^\\W]").fullmatch("é") && real::regex("\\w").fullmatch("é"));
  EXPECT(real::regex("[^\\D]").fullmatch("٣") && real::regex("\\d").fullmatch("٣"));
  EXPECT(!real::regex("[^\\W]").fullmatch("€"));      // € is non-word, so excluded from [^\W]==\w
  // [\b] stays backspace (regression pin).
  EXPECT(real::regex("[\\b]").fullmatch("\x08"sv));
  // icase on an in-class property is a no-op on the property (word-ness is fold-closed).
  EXPECT(real::regex("[\\w]", real::flags::icase).fullmatch("é"));
}

TEST(unicode_shorthand_in_class_ascii_bytes)
{
  using real::flags;
  // Under ascii, an in-class \d stays ASCII; \W still matches non-ASCII (re.A \W matches é).
  EXPECT(!real::regex("[\\d]", flags::ascii).fullmatch("٣"));
  EXPECT(real::regex("[\\W]", flags::ascii).fullmatch("é")); // re.A \W matches é
  EXPECT(real::regex("[\\w]", flags::bytes).fullmatch("a"));
}

TEST(named_codepoint_escape)
{
  // \N{U+XXXX} is the code-point path spelled by scalar value (a *name* is resolved in the binding).
  EXPECT(real::regex("\\N{U+0041}").fullmatch("A").matched());
  EXPECT(real::regex("\\N{U+1F600}").fullmatch("\xF0\x9F\x98\x80").matched());                   // astral, 4 bytes
  EXPECT(real::regex("[\\N{U+0041}b]").fullmatch("A").matched());                                // a class member, for free
  EXPECT_THROWS(real::regex("\\N{name}"), real::regex_error);                                    // the engine takes only U+XXXX
  EXPECT_THROWS(real::regex(std::string("\\N{U+0041}"), real::flags::bytes), real::regex_error); // bytes: no code point
  EXPECT_THROWS(real::regex("\\N{U+D800}"), real::regex_error);                                  // a surrogate is rejected
}

TEST(braced_hex_codepoint_escape)
{
  // \x{...} is RE2/Perl's braced code-point escape — lives on the code-point path with \u/\U/\N
  // (shares parse_braced_hex_scalar's digit-loop/surrogate/overflow validation), NOT the \xHH byte
  // path (re2-parity-measurement.md: closes the \x{10FFFF} gap).
  EXPECT(real::regex("\\x{41}").fullmatch("A").matched());
  EXPECT(real::regex("\\x{1F600}").fullmatch("\xF0\x9F\x98\x80").matched());    // astral, > U+FFFF, 4 bytes
  EXPECT(real::regex("\\x{10FFFF}").fullmatch("\xF4\x8F\xBF\xBF").matched());   // max scalar
  EXPECT(real::regex("[\\x{1F600}]").fullmatch("\xF0\x9F\x98\x80").matched());  // in-class member
  EXPECT(!real::regex("[\\x{1F600}]").fullmatch("\xF0\x9F\x98\x81").matched()); // and only that one
  // \x{1F600}+ quantifies the whole code point, not a UTF-8 byte.
  EXPECT_EQ(real::regex("\\x{1F600}+").search("\xF0\x9F\x98\x80\xF0\x9F\x98\x80"sv)[0],
            "\xF0\x9F\x98\x80\xF0\x9F\x98\x80"sv);

  // \xHH (two-hex byte escape) is STRICTLY UNCHANGED: no '{' after \x keeps the existing byte path.
  EXPECT(real::regex("\\x41\\x62").fullmatch("Ab").matched());
  EXPECT(real::regex("[\\x30-\\x39]").fullmatch("7").matched());
  EXPECT_THROWS(real::regex("\\x4"), real::regex_error);
  EXPECT_THROWS(real::regex("\\xg0"), real::regex_error);

  // bytes mode: \x{...} has no code-point meaning there, like \u/\U/\N (rejected, not silently truncated).
  EXPECT_THROWS(real::regex(std::string("\\x{41}"), real::flags::bytes), real::regex_error);
  EXPECT_THROWS(real::regex(std::string("[\\x{41}]"), real::flags::bytes), real::regex_error);

  // Surrogate / overflow: real keeps the Python-semantics core codepoint validation (shared with
  // \u/\U/\N) — measured vs libre2 11.0.0: RE2 rejects \x{110000} too (parity), but RE2 accepts a
  // surrogate in \x{...} where real does not (a confirmed sub-edge, tracked as a fuzz_re2 KNOWN-GAP
  // rather than relaxed here — relaxing would riddle the shared \u/\U/\N validation).
  EXPECT_THROWS(real::regex("\\x{D800}"), real::regex_error);
  EXPECT_THROWS(real::regex("\\x{110000}"), real::regex_error);

  // parse_braced_hex_scalar (shared with \N{U+XXXX}): lowercase hex digits, a missing digit run, and
  // an unterminated brace -- pins the shared helper's full branch coverage, not just the \x{...}-new
  // paths above.
  EXPECT(real::regex("\\x{1f600}").fullmatch("\xF0\x9F\x98\x80").matched()); // lowercase a-f hex digits
  EXPECT_THROWS(real::regex("\\x{}"), real::regex_error);                    // no hex digits
  EXPECT_THROWS(real::regex("\\x{41"), real::regex_error);                   // unterminated (no '}')
}

TEST(braced_hex_codepoint_escape_ecma_pins)
{
  using real::flags;
  // \x{...} is NOT ECMAScript (ES spells this \u{...}); under ecma, \x{ falls back to the two-hex
  // path, which then fails on the non-hex '{' -- the same rejection ecma already produced before
  // this feature existed (pin so a future change to the ecma gate cannot silently start accepting it).
  EXPECT_THROWS(real::regex("\\x{41}", flags::ecma), real::regex_error);
  EXPECT_THROWS(real::regex("[\\x{41}]", flags::ecma), real::regex_error);
  // \xHH under ecma is unchanged: still the two-hex ECMAScript escape.
  EXPECT(real::regex("\\x41", flags::ecma).fullmatch("A").matched());
  const std::string ff(1, '\xFF');
  EXPECT(real::regex("\\xFF", flags::ecma | flags::bytes).fullmatch(ff).matched());
}

TEST(quoted_literal_span)
{
  // \Q...\E literal quoting (RE2/Perl; libre2 11.0.0 is the oracle for every edge pinned here —
  // re2-parity-measurement.md, closed 2026-07-17). The span is a sequence of literal atoms; every
  // metacharacter inside is inert.
  EXPECT(real::regex("\\Qa.b\\E").fullmatch("a.b").matched());
  EXPECT(!real::regex("\\Qa.b\\E").fullmatch("axb").matched()); // '.' is literal, not any-char
  EXPECT(real::regex("\\Q(a|b)*\\E").fullmatch("(a|b)*").matched());
  EXPECT(real::regex("\\Qa|b\\E").fullmatch("a|b").matched());  // '|' inside the span: no alternation
  EXPECT(!real::regex("\\Qa|b\\E").fullmatch("a").matched());
  EXPECT(real::regex("\\Qa+\\E").fullmatch("a+").matched());    // '+' inside the span is literal
  EXPECT(!real::regex("\\Qa+\\E").fullmatch("aa").matched());

  // A quantifier after \E binds to the span's LAST character (RE2: \Qab\E+ == ab+).
  EXPECT(real::regex("\\Qab\\E+").fullmatch("abb").matched());
  EXPECT(!real::regex("\\Qab\\E+").fullmatch("abab").matched());
  EXPECT(real::regex("\\Qab\\E{2}").fullmatch("abb").matched()); // counted form binds the same
  EXPECT(real::regex("x\\Qab\\E?").fullmatch("xa").matched());   // '?' on 'b' only: 'a' stays required
  EXPECT(!real::regex("x\\Qab\\E?").fullmatch("x").matched());

  // Empty \Q\E is grammar-invisible (RE2-measured): a following quantifier re-binds to the previous
  // atom (a\Q\E+ == a+); with no previous atom it is an error, like RE2's "no argument for repetition".
  EXPECT(real::regex("\\Q\\E").fullmatch("").matched());
  EXPECT(real::regex("a\\Q\\Eb").fullmatch("ab").matched());
  EXPECT(real::regex("a\\Q\\E+").fullmatch("aa").matched());
  EXPECT(real::regex("ab\\Q\\E+").fullmatch("abb").matched());   // re-binds mid-sequence (== ab+) ...
  EXPECT(!real::regex("ab\\Q\\E+").fullmatch("abab").matched()); // ... to the LAST atom only
  EXPECT_THROWS(real::regex("\\Q\\E+"), real::regex_error);
  EXPECT_THROWS(real::regex("(\\Q\\E+)"), real::regex_error);

  // Unterminated \Q quotes to the end of the pattern; the "dumb scan" takes a backslash literally
  // unless it is exactly followed by 'E' (no escape processing, no nesting inside the span).
  EXPECT(real::regex("\\Qa.b").fullmatch("a.b").matched());
  EXPECT(real::regex("\\Qa\\").fullmatch("a\\").matched());        // trailing lone backslash is literal
  EXPECT(real::regex("\\Qa\\Qb\\E").fullmatch("a\\Qb").matched()); // inner \Q: two literal chars
  EXPECT(real::regex("\\Qa\\\\Eb").fullmatch("a\\b").matched());   // '\' then \E terminator
  EXPECT(real::regex("\\Qa\\nb\\E").fullmatch("a\\nb").matched()); // \n inside the span: 2 literal chars
  EXPECT(!real::regex("\\Qa\\nb\\E").fullmatch("a\nb").matched()); // NOT a newline

  // Multibyte: one atom per code point, so a quantifier repeats the whole last code point. A
  // malformed UTF-8 byte inside the span is a pattern error (same rule as a raw literal, text mode).
  EXPECT(real::regex("\\Q\xF0\x9F\x98\x80\\E").fullmatch("\xF0\x9F\x98\x80").matched());
  EXPECT(real::regex("\\Q\xF0\x9F\x98\x80\\E+").fullmatch("\xF0\x9F\x98\x80\xF0\x9F\x98\x80").matched());
  EXPECT_THROWS(real::regex("\\Q\xC3\\E"), real::regex_error); // lone lead byte in the span

  // The span folds under icase like any literal (RE2 (?i) folds \Q spans), incl. a scoped (?i:...).
  EXPECT(real::regex("\\Qab\\E", real::flags::icase).fullmatch("AB").matched());
  EXPECT(real::regex("(?i:\\Qab\\E)c").fullmatch("ABc").matched());
  EXPECT(!real::regex("(?i:\\Qab\\E)c").fullmatch("ABC").matched());

  // Group interaction: ')' inside the span is literal, so the span can eat a close-paren (then the
  // group is unbalanced, an error — RE2 agrees) and works inside alternation branches.
  EXPECT(real::regex("(\\Qa.b\\E|x)").fullmatch("a.b").matched());
  EXPECT(real::regex("(\\Qa)b\\E)").fullmatch("a)b").matched());
  EXPECT_THROWS(real::regex("(\\Qa)b\\E"), real::regex_error);

  // bytes mode (non-ecma): the span is per-byte literal (the gate is the dialect, not the encoding).
  EXPECT(real::regex(std::string("\\Qa.b\\E"), real::flags::bytes).fullmatch("a.b").matched());

  // Verbose mode: whitespace inside the span stays literal — a quoted span protects its spaces
  // (REAL's own call: RE2 rejects (?x) entirely, so there is no oracle; Perl's \Q quotes spaces too).
  EXPECT(real::regex("\\Qa b\\E", real::flags::verbose).fullmatch("a b").matched());
  EXPECT(real::regex("a \\Qb c\\E", real::flags::verbose).fullmatch("ab c").matched());
}

TEST(quoted_literal_span_rejections)
{
  using real::flags;
  // \E without \Q: RE2 rejects ("invalid escape sequence: \E") and real always has — parity pin.
  EXPECT_THROWS(real::regex("\\E"), real::regex_error);
  EXPECT_THROWS(real::regex("a\\Eb"), real::regex_error);
  // In-class [\Q...\E]: RE2 rejects ("invalid escape sequence: \Q") — real keeps rejecting, measured
  // 2026-07-17, deliberately NOT over-implemented.
  EXPECT_THROWS(real::regex("[\\Qab\\E]"), real::regex_error);
  // ecma pins: \Q is not ECMAScript; the dialect gate preserves the unsupported-escape rejection
  // (the std-compat layer relies on this statu quo).
  EXPECT_THROWS(real::regex("\\Qa\\E", flags::ecma), real::regex_error);
  EXPECT_THROWS(real::regex(std::string("\\Qa.b\\E"), flags::ecma | flags::bytes), real::regex_error);
}

TEST(octal_escapes_inside_a_class)
{
  // Inside a class every \digit is octal — there are no back-references in a class (re's rule).
  const std::string b01(1, '\x01');
  const std::string b07(1, '\x07');
  const std::string nl("\n");
  const std::string A("A");
  const std::string eight("8");
  EXPECT(real::regex("[\\1]").fullmatch(b01).matched());    // one octal digit
  EXPECT(real::regex("[\\7]").fullmatch(b07).matched());
  EXPECT(real::regex("[\\12]").fullmatch(nl).matched());    // two (0o12 == 10 == '\n')
  EXPECT(real::regex("[\\101]").fullmatch(A).matched());    // three (0o101 == 65 == 'A')
  EXPECT(real::regex("[\\18]").fullmatch(b01).matched());   // \1 then a literal '8'
  EXPECT(real::regex("[\\18]").fullmatch(eight).matched());
  EXPECT_THROWS(real::regex("[\\8]"), real::regex_error);   // 8/9 are not octal, no back-reference here
  EXPECT_THROWS(real::regex("[\\400]"), real::regex_error); // above 0o377, out of range (like re)
}
