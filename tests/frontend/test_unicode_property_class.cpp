// Parser + matching for `\p{...}` / `\P{...}` / `\pX` Unicode property classes (General_Category and Script).
// The tables and their UCD oracles live in tests/unicode/; here we exercise the parser wiring: name resolution
// (short codes, long names, gc=/sc= prefixes, loose matching), negation, the `(?a)`-does-not-restrict rule, the
// bytes-mode rejection, named errors, matching across engines, and — as of P2 — in-class \p{} with negation
// (plus the enclosing [^...] on top) and icase membership-then-fold (pinned against the UCD oracle).
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
  EXPECT_THROWS(real::regex(R"(\p{Xyz})"), real::regex_error);            // unknown name (not GC/Script/binary)
  EXPECT_THROWS(real::regex(R"(\p{zz=Latin})"), real::regex_error);       // unknown namespace
  EXPECT_THROWS(real::regex(R"(\p{gc=White_Space})"), real::regex_error); // binary prop has no namespace of its own
  EXPECT_THROWS(real::regex(R"(\p{Latin)"), real::regex_error);           // unterminated
  EXPECT_THROWS(real::regex(R"(\p)"), real::regex_error);                 // no name
  EXPECT_THROWS(real::regex(R"(\p{})"), real::regex_error);               // empty name
  EXPECT_THROWS(real::regex(R"(\p{sc=NoSuchScript})"), real::regex_error);
  EXPECT_THROWS(real::regex(R"(\p{scx=NoSuchScript})"), real::regex_error);
}

TEST(unicode_property_binary)
{
  // Binary properties (\p{Name}, no namespace of its own, same as PCRE2) -- the tables and their UCD
  // oracle live in tests/unicode/test_unicode_binprop.cpp; here, the parser wiring: bare-name resolution
  // after GC/Script both decline, negation, and in-class.
  EXPECT(real::regex(R"(\p{White_Space})").fullmatch(" "));
  EXPECT(!real::regex(R"(\p{White_Space})").fullmatch("x"));
  EXPECT(real::regex(R"(\p{Alphabetic})").fullmatch("a"));
  EXPECT(!real::regex(R"(\p{Alphabetic})").fullmatch("3"));
  EXPECT(real::regex(R"(\p{Emoji})").fullmatch("\xF0\x9F\x98\x80"sv)); // U+1F600 GRINNING FACE
  EXPECT(real::regex(R"(\p{Hex_Digit})").fullmatch("F"));
  EXPECT(!real::regex(R"(\p{Hex_Digit})").fullmatch("G"));
  EXPECT(real::regex(R"(\p{ WHITE-space })").fullmatch(" "));          // loose matching, same as GC/Script
  EXPECT(real::regex(R"(\P{White_Space})").fullmatch("x"));            // negation
  EXPECT(!real::regex(R"(\P{White_Space})").fullmatch(" "));
  EXPECT(real::regex(R"([\p{White_Space}x])").fullmatch("x"));         // in-class, mixed with a literal
  EXPECT(real::regex(R"([\p{White_Space}x])").fullmatch(" "));
  EXPECT(!real::regex(R"([\p{White_Space}x])").fullmatch("y"));
  EXPECT(real::regex(R"([^\p{White_Space}])").fullmatch("x"));         // negated class containing a binary property
  EXPECT(!real::regex(R"([^\p{White_Space}])").fullmatch(" "));
}

TEST(unicode_property_script_short_codes)
{
  // sc= now resolves BOTH the long name (Latin) and the short UAX24/ISO 15924 code (Latn) to the same
  // value -- one shared alias table with scx= (PropertyValueAliases.txt maps them).
  EXPECT(real::regex(R"(\p{sc=Latn})").fullmatch("A"));
  EXPECT(real::regex(R"(\p{sc=Latin})").fullmatch("A"));
  EXPECT(real::regex(R"(\p{sc=Grek})").fullmatch("\xCE\xB1"sv));     // α
  EXPECT(real::regex(R"(\p{sc=Thai})").fullmatch("\xE0\xB8\x81"sv)); // ก -- name == its own short code
}

TEST(unicode_property_scx)
{
  // Script_Extensions (\p{scx=...}, NOT a partition -- the tables and their UCD oracle live in
  // tests/unicode/test_unicode_scx.cpp) -- here, the parser wiring: negation, in-class, and the
  // multi-scx proof.
  const std::string_view digit {"\xD9\xA0"};                  // U+0660 ARABIC-INDIC DIGIT ZERO: scx={Arab, Thaa, Yezi}
  EXPECT(real::regex(R"(\p{scx=Arab})").fullmatch(digit));
  EXPECT(real::regex(R"(\p{scx=Thaa})").fullmatch(digit));    // same code point, a DIFFERENT script's scx
  EXPECT(real::regex(R"(\p{scx=Arabic})").fullmatch(digit));  // long name works too
  EXPECT(!real::regex(R"(\p{scx=Latin})").fullmatch(digit));
  EXPECT(!real::regex(R"(\P{scx=Arab})").fullmatch(digit));   // negation
  EXPECT(real::regex(R"(\P{scx=Latin})").fullmatch(digit));
  EXPECT(real::regex(R"([\p{scx=Arab}x])").fullmatch(digit)); // in-class
  EXPECT(real::regex(R"([\p{scx=Arab}x])").fullmatch("x"));
}

TEST(unicode_property_scx_has_no_bare_name_form)
{
  // scx has no bare-name form (PCRE2: a bare \p{Name} never means Script_Extensions) -- but since sc= and
  // scx= now share the SAME name resolver (a script's short code resolves via bare \p{Name} too, see
  // unicode_property_script_short_codes), a missing bare form cannot be proven by a name failing to
  // resolve anymore. It shows up in the RESULT instead: bare \p{Grek} uses script_ranges (the Script
  // partition), \p{scx=Grek} uses the superset. U+0300 COMBINING GRAVE ACCENT is Inherited in the
  // partition (excluded from bare \p{Grek}) but IS in Greek's scx (included by \p{scx=Grek}).
  const std::string_view combining_grave {"\xCC\x80"}; // U+0300
  EXPECT(!real::regex(R"(\p{Grek})").fullmatch(combining_grave));
  EXPECT(!real::regex(R"(\p{sc=Grek})").fullmatch(combining_grave));
  EXPECT(real::regex(R"(\p{scx=Grek})").fullmatch(combining_grave));
}

TEST(unicode_property_bytes_mode_rejects)
{
  EXPECT_THROWS(real::regex(R"(\p{L})", real::flags::bytes), real::regex_error);
  EXPECT_THROWS(real::regex(R"(\pL)", real::flags::bytes), real::regex_error);
  EXPECT_THROWS(real::regex(R"([\p{L}])", real::flags::bytes), real::regex_error); // in a class too
}

TEST(unicode_property_inside_a_class)
{
  EXPECT(real::regex(R"([\p{L}\d_])").fullmatch("5"));  // a property mixes with \d and a literal
  EXPECT(real::regex(R"([\p{L}\d_])").fullmatch("A"));
  EXPECT(real::regex(R"([\p{L}\d_])").fullmatch("é"));
  EXPECT(!real::regex(R"([\p{L}\d_])").fullmatch("-"));
  EXPECT(real::regex(R"([\p{Nd}])").fullmatch("٣")); // Arabic-indic digit, non-ASCII
  // negation inside a class, and the enclosing [^...] negating on top
  EXPECT(real::regex(R"([\P{L}])").fullmatch("3"));
  EXPECT(!real::regex(R"([\P{L}])").fullmatch("A"));
  EXPECT(!real::regex(R"([\P{L}])").fullmatch("é"));
  EXPECT(real::regex(R"([^\p{L}])").fullmatch("3"));
  EXPECT(!real::regex(R"([^\p{L}])").fullmatch("A"));
  EXPECT(real::regex(R"([^\P{L}])").fullmatch("A")); // double negation == [\p{L}]
  EXPECT(!real::regex(R"([^\P{L}])").fullmatch("3"));
}

TEST(unicode_property_icase_membership_then_fold)
{
  // Under IGNORECASE the property class folds by the existing pipeline (membership then fold) — no special
  // code. Each expectation was verified against the `regex` module (the UCD icase oracle), not decreed:
  // \p{Lu} folds to include every code point whose simple case-fold lands on an uppercase letter.
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("a"));   // ascii lower folds in
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("k"));   // k
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("K"));   // KELVIN SIGN folds to k
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("σ"));   // Greek small sigma
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("ς"));   // Greek final sigma
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("ı"));   // dotless i -> I
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("İ"));   // capital I with dot (already Lu)
  // \P{Lu} folds THEN negates: a code point folding to an uppercase letter is excluded, so both A and a fail.
  EXPECT(!real::regex(R"((?i)\P{Lu})").fullmatch("A"));
  EXPECT(!real::regex(R"((?i)\P{Lu})").fullmatch("a"));
  // the same holds in a class
  EXPECT(real::regex(R"((?i)[\p{Lu}])").fullmatch("a"));
}

TEST(unicode_property_icase_grid)
{
  // The seven-answer grid — each icase/negation edge, verified against the oracle (stdlib re / the regex
  // module) and pinned so the contract cannot drift. All match except the fold-then-negate \P{Lu} on 'a'.
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("é"));  // 1. é folds to É (Lu)
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("Σ"));  // 2. capital sigma — already Lu
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("ς"));  // 3. final sigma folds to Σ
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("K"));  // 4. KELVIN — already Lu
  EXPECT(!real::regex(R"((?i)\P{Lu})").fullmatch("a")); // 5. fold-then-negate: a -> A (Lu) is excluded
  EXPECT(real::regex(R"((?i)\p{Lu})").fullmatch("ı"));  // 6. Turkish dotless-i folds with I (== re, != crate)
  EXPECT(real::regex(R"([^\P{L}])").fullmatch("A"));    // 7. double negation == [\p{L}]
}
