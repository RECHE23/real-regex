// Volet B (malformed UTF-8 robustness): test_utf8.cpp already pins the core contract for a bare
// class/dot run (stops at a malformed sequence, C-0 property) and for empty-match iteration
// (boundary-aligned). This file crosses the SAME malformed-subject corpus with the features that
// file does not exercise on malformed input: Unicode `\w \d \s \b`, `\p{...}` (GC/script/scx/
// binary), case-insensitive matching, lookarounds, and region search (`pos`/`endpos`) where the
// boundary itself lands inside what would otherwise be a valid multi-byte sequence. The contract
// throughout: DEFINED and stable behavior (never a crash/OOB — proven by running this file under
// `make sanitize`, not by anything in the file itself) and no match that spans into a malformed
// byte run. Every malformed byte position is expected to behave as a non-match, non-word,
// non-property code unit — the same "malformed is never a code point" rule the engine already
// applies to `.` and class runs, extended here to the rest of the feature surface.
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include <sciforge/test/strings.hpp>
#include "real/real.hpp"

using real::flags;
using real::regex;
using test::bytes;
using test::cat;

namespace {

  bool searches(const std::string& pattern,
                const std::string& text,
                flags              f = flags::none)
  {
    return regex(pattern, f).search(text).matched();
  }

  // A catalog of malformed byte sequences, named for the assertion messages. Matches the classes
  // named in the fiche: isolated continuation, truncated lead (both at end-of-text and mid-text,
  // the latter followed by ASCII so a wrongly-permissive scan would visibly overrun), overlong
  // encodings of increasing length, an encoded surrogate, a code point past U+10FFFF, and the two
  // bytes that are never valid anywhere in UTF-8.
  struct malformed_case
  {
    std::string_view name;
    std::string      seq; // the malformed sequence itself
  };

  std::vector<malformed_case> malformed_catalog()
  {
    return {
      {"lone_continuation_80", bytes({0x80})},
      {"lone_continuation_bf", bytes({0xBF})},
      {"truncated_2byte_lead_eot", bytes({0xC3})},
      {"truncated_3byte_lead_eot", bytes({0xE2, 0x82})},
      {"truncated_4byte_lead_eot", bytes({0xF0, 0x9F, 0x98})},
      {"overlong_2byte_c0_80", bytes({0xC0, 0x80})},
      {"overlong_3byte_e0_80_80", bytes({0xE0, 0x80, 0x80})},
      {"overlong_4byte_f0_80_80_80", bytes({0xF0, 0x80, 0x80, 0x80})},
      {"surrogate_ed_a0_80", bytes({0xED, 0xA0, 0x80})},
      {"past_10ffff_f5", bytes({0xF5, 0x80, 0x80, 0x80})},
      {"invalid_lead_fe", bytes({0xFE})},
      {"invalid_lead_ff", bytes({0xFF})},
    };
  }

  // KNOWN GAP (found by this arc, reported not fixed -- volet B is test-only, "STOP + report" on
  // an engine bug): `.`'s own width/validity check -- used by bare `.` and by a fixed-width
  // lookaround's content check -- accepts these three as if they were one valid code point, even
  // though `decode_codepoint_strict` (the pattern-side validator) and the canonical-ranges
  // automaton used by an explicit-codepoint class (`[é]`, `[^é]` -- see test_utf8.cpp's
  // utf8_class_security_and_malformed, which pins `[^é]` correctly REJECTING the very same
  // surrogate bytes) both correctly reject them. Root cause not diagnosed here (that is the next
  // arc's job): the working hypothesis is that `.`'s width check validates the STRUCTURAL shape
  // (lead byte in the canonical C2-DF/E0-EF/F0-F4 range, plus the right COUNT of continuation
  // bytes in 0x80-0xBF) without also checking that the first continuation byte falls in the
  // length-specific sub-range that excludes overlong/surrogate encodings (E0 needs A0-BF not
  // 80-9F; ED needs 80-9F not A0-BF to exclude D800-DFFF; F0 needs 90-BF not 80-8F) --
  // exactly the check `decode_codepoint_strict` does (its `min_cp` guard) and the 2-byte case
  // gets "for free" by excluding C0/C1 from the lead set entirely, which is why only the 2-byte
  // overlong case (already in malformed_catalog above) is currently caught by `.`.
  std::vector<malformed_case> dot_width_gap_catalog()
  {
    return {
      {"overlong_3byte_e0_80_80", bytes({0xE0, 0x80, 0x80})},
      {"overlong_4byte_f0_80_80_80", bytes({0xF0, 0x80, 0x80, 0x80})},
      {"surrogate_ed_a0_80", bytes({0xED, 0xA0, 0x80})},
    };
  }
} // namespace

TEST(malformed_never_matches_unicode_word_digit_space)
{
  // \w \d \s in text mode are Unicode-aware (klass_cp); a malformed byte is not a valid code
  // point at all, so none of the three ever accept it -- same C-0 rule as `.`/class runs.
  for (const auto& mc : malformed_catalog()) {
    EXPECT(!searches(R"(^\w$)", mc.seq));
    EXPECT(!searches(R"(^\d$)", mc.seq));
    // \s: none of the catalog bytes are whitespace, but assert it explicitly rather than by
    // omission -- a malformed byte must not be *any* single-code-point class, not just \w/\d.
    EXPECT(!searches(R"(^\s$)", mc.seq));
  }
}

TEST(malformed_word_boundary_treats_it_as_non_word)
{
  // \b is defined on the word-ness of the code points either side of a position. A malformed
  // byte contributes no code point, so it is treated as non-word on both sides -- an ASCII word
  // immediately followed by a malformed sequence still gets a trailing \b (the malformed bytes
  // are "outside" the word, not silently absorbed into it).
  for (const auto& mc : malformed_catalog()) {
    const std::string text {cat({"ab", mc.seq})};
    EXPECT(searches(R"(ab\b)", text));      // \b fires right after the ASCII word, before the malformed run
    EXPECT(!searches(R"(\bab\B)", text));   // \B (non-boundary) must NOT fire there
  }
}

TEST(malformed_never_matches_property_classes)
{
  // \p{...} across every syntax form this arc landed: bare General_Category, sc=/scx= Script(_
  // Extensions), and a binary property. All route through the same code-point decode as \w/\d/\s
  // -- a malformed byte satisfies none of them.
  const std::string_view patterns[] = {
    R"(^\p{L}$)", R"(^\p{N}$)", R"(^\p{sc=Han}$)", R"(^\p{scx=Cyrl}$)", R"(^\p{Alphabetic}$)",
  };
  for (const auto& mc : malformed_catalog()) {
    for (const auto& pat : patterns) {
      EXPECT(!searches(std::string(pat), mc.seq));
    }
  }
}

TEST(malformed_case_insensitive_no_spurious_match)
{
  // (?i) on a non-ASCII literal/class must not be fooled into matching a malformed sequence that
  // happens to share a leading byte with the folded target -- e.g. (?i)é (C3 A9) against a
  // truncated C3-lead sequence, or against an overlong encoding that starts with the same byte.
  const std::string e {"é"};                          // C3 A9
  EXPECT(searches("(?i)é", e));
  EXPECT(!searches("(?i)é", bytes({0xC3})));          // truncated lead, same first byte as é
  EXPECT(!searches("(?i)é", bytes({0xC0, 0x80})));    // overlong, unrelated code point anyway
  EXPECT(!searches(R"((?i)^[à-ÿ]$)", bytes({0xC3}))); // icase class range, same guard
  for (const auto& mc : malformed_catalog()) {
    EXPECT(!searches("(?i)é", mc.seq));
  }
}

TEST(malformed_lookaround_stays_defined_no_crash)
{
  // A lookahead/lookbehind whose window lands on or straddles a malformed SUBJECT sequence must
  // produce a defined result, not read out of bounds (the actual OOB proof is this file compiled
  // and run under `make sanitize` -- ASan/UBSan, not an assertion here). Patterns stay well-formed
  // throughout -- only the subject carries malformed bytes (malformed PATTERN text is already a
  // compile-time rejection, tested in test_utf8.cpp's utf8_malformed_pattern_is_a_compile_error).
  //
  // The 3-entry dot_width_gap_catalog is deliberately EXCLUDED from this loop -- `.`'s width check
  // currently (wrongly) accepts those three, which is the known gap pinned separately below, not
  // a "no crash" question (nothing crashes; the match outcome is simply wrong). Filtering here
  // keeps this test asserting only what is actually true today.
  const auto gap = dot_width_gap_catalog();
  for (const auto& mc : malformed_catalog()) {
    const bool is_gap_case = std::any_of(gap.begin(), gap.end(),
                                         [&](const malformed_case& g) { return g.name == mc.name; });
    if (is_gap_case) {
      continue;
    }
    const std::string after  {cat({"ab", mc.seq})}; // malformed bytes right where the lookahead window looks
    const std::string before {cat({mc.seq, "cd"})}; // malformed bytes right where the lookbehind window looks

    // `ab(?=.)`: the lookahead needs ONE valid code point right after "ab" -- a malformed
    // sequence there can never satisfy it, but the attempt must not crash or read past it.
    EXPECT(!searches(R"(ab(?=.))", after));
    EXPECT(searches(R"(ab(?=.))", cat({"ab", "c"}))); // control: a real code point does satisfy it

    // `(?<=.)cd`: the lookbehind needs ONE valid code point right before "cd" -- same guard, the
    // other direction (lookbehind walks backward from the anchor, the more OOB-prone direction).
    EXPECT(!searches(R"((?<=.)cd)", before));
    EXPECT(searches(R"((?<=.)cd)", cat({"c", "cd"})));
  }
}

TEST(KNOWN_GAP_dot_width_accepts_3_4byte_overlong_and_surrogate)
{
  // Pins the CURRENT (wrong) behavior found by this arc, per volet B's "STOP + report, do not fix
  // silently" mandate -- this is a discovered engine bug, reported separately, not addressed here.
  // See dot_width_gap_catalog's comment for the full analysis and the working root-cause
  // hypothesis. `[é]`/`[^é]` (an explicit-codepoint class) already correctly reject every one of
  // these (test_utf8.cpp's utf8_class_security_and_malformed) -- only `.`'s own width check (used
  // directly by bare `.`/`.+` and by a fixed-width lookaround's content check) has this gap.
  //
  // If this test starts failing because the gap was CLOSED (the EXPECT below now sees `.` NOT
  // matching malformed input), that is progress, not a regression -- flip the assertions, fold the
  // three entries back into the main malformed_catalog(), and delete dot_width_gap_catalog and this
  // test. Do not "fix" this test to keep it green some other way.
  for (const auto& mc : dot_width_gap_catalog()) {
    EXPECT(regex(".").fullmatch(mc.seq).matched()); // WRONG: should be false; pinning today's reality
  }
}

TEST(malformed_region_truncation_matches_end_of_text_truncation)
{
  // A region boundary (`pos`/`endpos`) that lands inside what would otherwise be a valid
  // multi-byte sequence must behave exactly like the sequence being truncated at the real end of
  // the text -- the engine must never read past `endpos` to "complete" a code point it cannot see.
  const std::string e      {"é"}; // C3 A9 -- a valid 2-byte code point when whole
  const std::string euro   {"€"}; // E2 82 AC -- a valid 3-byte code point when whole
  const std::string emoji  {"😀"}; // F0 9F 98 80 -- a valid 4-byte code point when whole
  const std::string e1     {e.substr(0, 1)};
  const std::string euro1  {euro.substr(0, 1)};
  const std::string euro2  {euro.substr(0, 2)};
  const std::string emoji1 {emoji.substr(0, 1)};
  const std::string emoji3 {emoji.substr(0, 3)};

  // endpos cuts the sequence after its lead byte only: [pos, endpos) sees just the lead, which is
  // indistinguishable from a real end-of-text truncation of the same lead byte.
  EXPECT_EQ(regex(".").fullmatch(e, 0, 1).matched(), regex(".").fullmatch(e1).matched());
  EXPECT(!regex(".").fullmatch(e, 0, 1).matched()); // truncated lead is never a whole code point either way

  EXPECT_EQ(regex(".").fullmatch(euro, 0, 1).matched(), regex(".").fullmatch(euro1).matched());
  EXPECT_EQ(regex(".").fullmatch(euro, 0, 2).matched(), regex(".").fullmatch(euro2).matched());
  EXPECT(!regex(".").fullmatch(euro, 0, 1).matched());
  EXPECT(!regex(".").fullmatch(euro, 0, 2).matched());

  EXPECT_EQ(regex(".").fullmatch(emoji, 0, 1).matched(), regex(".").fullmatch(emoji1).matched());
  EXPECT_EQ(regex(".").fullmatch(emoji, 0, 3).matched(), regex(".").fullmatch(emoji3).matched());
  EXPECT(!regex(".").fullmatch(emoji, 0, 3).matched());

  // A run (`.+`/class+) truncated mid-cluster by endpos stops at the same length its end-of-text
  // twin would -- the truncated tail is not silently consumed past endpos, nor does the run's own
  // scan look beyond endpos to see whether the sequence "would have" completed.
  const std::string prefix_then_e {cat({"ab", e})}; // a b C3 A9
  const std::string prefix3       {prefix_then_e.substr(0, 3)};
  const auto        r             {regex("[^\"]+")};
  EXPECT_EQ(r.search(prefix_then_e, 0, 3).end(0), r.search(prefix3).end(0));
  EXPECT_EQ(r.search(prefix_then_e, 0, 3).end(0), std::size_t {2}); // stops before the truncated é
}
