// Malformed UTF-8 robustness: test_utf8.cpp already pins the core contract for a bare
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include <sciforge/test/strings.hpp>
#include "real/automata/lazy_dfa.hpp" // lazy_dfa_route_disabled
#include "real/dfa.hpp"
#include "real/real.hpp"

using real::flags;
using real::regex;
using test::bytes;
using test::cat;

namespace {

  /*!
   * \brief A bare heap buffer of exactly \p total bytes -- no null-terminator slack.
   *
   * `std::string` always guarantees `data()[size()] == '\0'` as a valid, allocated byte, SSO or
   * not -- so a `std::string`-backed subject, even a large heap-allocated one, can never catch a
   * "read one byte past size()" bug: that read lands on the guaranteed terminator, still inside
   * the allocation. `std::vector<char>` has no such guarantee (`data()[size()]` is not part of
   * the allocation at all) and owns its storage via RAII, matching what the C-ABI / fuzz harness
   * hands the engine (a bare pointer+length pair) without leaking the buffer the way a raw
   * `new[]`/`.release()` did in an earlier version of this test (a leak LeakSanitizer
   * caught on the CI's Linux leg -- invisible locally, since LSan is not
   * enabled by default on this macOS toolchain; see the sanitize target's own comment).
   */
  std::vector<char> bare_heap_ending_in(std::size_t   total,
                                        unsigned char last_byte)
  {
    std::vector<char> buf(total, 'x');
    buf.back() = static_cast<char>(last_byte);
    return buf;
  }

  bool searches(const std::string& pattern,
                const std::string& text,
                flags              f = flags::none)
  {
    return regex(pattern, f).search(text).matched();
  }

  // A catalog of malformed byte sequences, named for the assertion messages. Matches the classes
  // the named cases: isolated continuation, truncated lead (both at end-of-text and mid-text,
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
      {.name = "lone_continuation_80", .seq = bytes({0x80})},
      {.name = "lone_continuation_bf", .seq = bytes({0xBF})},
      {.name = "truncated_2byte_lead_eot", .seq = bytes({0xC3})},
      {.name = "truncated_3byte_lead_eot", .seq = bytes({0xE2, 0x82})},
      {.name = "truncated_4byte_lead_eot", .seq = bytes({0xF0, 0x9F, 0x98})},
      {.name = "overlong_2byte_c0_80", .seq = bytes({0xC0, 0x80})},
      {.name = "overlong_3byte_e0_80_80", .seq = bytes({0xE0, 0x80, 0x80})},
      {.name = "overlong_4byte_f0_80_80_80", .seq = bytes({0xF0, 0x80, 0x80, 0x80})},
      {.name = "surrogate_ed_a0_80", .seq = bytes({0xED, 0xA0, 0x80})},
      {.name = "past_10ffff_f5", .seq = bytes({0xF5, 0x80, 0x80, 0x80})},
      {.name = "invalid_lead_fe", .seq = bytes({0xFE})},
      {.name = "invalid_lead_ff", .seq = bytes({0xFF})},
    };
  }

  // A former KNOWN GAP, fixed on every route: `.` and negated-ASCII-
  // only classes (`[^,]`) used to accept these four byte sequences as one valid code point --
  // overlong 3-byte, overlong 4-byte, an encoded surrogate, and a code point past U+10FFFF via a
  // structurally-plausible F4 lead. Two independent implementations shared the bug (neither
  // narrowed the FIRST continuation byte's valid range per lead byte the way
  // decode_codepoint_strict's min_cp/surrogate guard already did): pike.hpp's `run_codepoint_class`
  // fast path (bare `.`/`.+`, whole-pattern only) and compiler.hpp's `emit_codepoint_class`
  // byte-chain (every other route: `.` combined with other elements, lookaround content, the
  // lazy-DFA/one-pass byte-programs built from that same chain, and `real::dfa`).
  //
  // Both are now fixed via ONE shared table, `real::detail::utf8_second_byte_bounds_table`
  // (charclass.hpp): `[lo,hi]` bounds for the first continuation byte, indexed by lead byte,
  // narrowing exactly the four lead bytes below. A full `decode_codepoint_strict`-based fix (which
  // accumulates the code point via shifts, checked against min_cp/the surrogate block after the
  // fact) was measured and REJECTED for pike.hpp's fast path: +13% ns/B on a `.`-heavy corpus, too
  // costly for a per-byte hot loop that only needs a bounds check, not a full decode.
  //   - pike.hpp `run_codepoint_class::width`: the first continuation byte is bounds-checked
  //     against the table directly (site 1).
  //   - compiler.hpp `emit_any_codepoint_class`: the non-ASCII branches are built via
  //     `emit_class_codepoints`/`utf8_range_sequences` (the SAME canonical RE2-style splitting
  //     already used for `\p{...}` ranges), which independently derives the identical per-lead
  //     bounds from first principles (site 2). Because every downstream consumer of the compiled
  //     program (general Pike VM, lookaround, the lazy-DFA's byte-program, one-pass, `real::dfa`)
  //     just walks byte/klass/split/jump instructions with no `.`-specific logic of its own, fixing
  //     the compiled program fixes every one of those routes at once -- see
  //     strict_utf8_boundary_cases_rejected_on_every_route below for the route-by-route proof.
  std::vector<malformed_case> strict_utf8_boundary_catalog()
  {
    return {
      {.name = "overlong_3byte_e0_80_80", .seq = bytes({0xE0, 0x80, 0x80})},
      {.name = "overlong_4byte_f0_80_80_80", .seq = bytes({0xF0, 0x80, 0x80, 0x80})},
      {.name = "surrogate_ed_a0_80", .seq = bytes({0xED, 0xA0, 0x80})},
      // F4's own valid continuation range is 80-8F (codepoints up to U+10FFFF); 90-BF decodes to
      // U+110000-U+13FFFF, past the Unicode range -- the bug accepted any 80-BF continuation after
      // an F0-F4 lead regardless of which lead it specifically was.
      {.name = "out_of_range_f4_90_80_80", .seq = bytes({0xF4, 0x90, 0x80, 0x80})},
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

TEST(region_start_is_a_seed_position_even_when_it_is_a_continuation_byte)
{
  // Zero-width matches are codepoint-aligned: the seed check refuses a position whose byte is a
  // UTF-8 continuation byte, because a match cannot begin INSIDE a multi-byte sequence. That is a
  // claim about the byte BEFORE the position, and at the region's start there is no such byte --
  // whatever sits there is the region's first byte, not the tail of a sequence the region holds.
  //
  // Refusing it anyway cost the subject its position 0 entirely, so `^` -- the start of the
  // subject by definition -- matched NOTHING on any text beginning with 0x80..0xBF. `$` never
  // showed the fault, because the seed check short-circuits at pos == size() and so never asked
  // about the end. Reachable from every byte-oriented caller (C ABI, Go, Rust); not from Python,
  // whose str offsets are codepoint indices and whose bytes mode does not decode at all.
  for (const auto& mc : malformed_catalog()) {
    const auto anchored = regex("^").search(mc.seq);
    EXPECT(anchored.matched());
    EXPECT(anchored.start(0) == 0U);
    EXPECT(anchored.end(0) == 0U);

    const auto empty = regex("").search(mc.seq);
    EXPECT(empty.matched());
    EXPECT(empty.start(0) == 0U);                  // leftmost, so the first position must be the one reported

    const auto at_end = regex("$").search(mc.seq); // the control: this half always worked
    EXPECT(at_end.matched());
    EXPECT(at_end.start(0) == mc.seq.size());
  }
}

TEST(interior_continuation_byte_is_not_a_seed_position)
{
  // The other half of the region-start exemption, so it cannot quietly widen into "seed anywhere".
  //
  // The rule seed_viable enforces is about the byte AT the position, not about whether the position
  // is interior to a sequence: after "a", offset 1 is a genuine codepoint boundary, yet the lone
  // 0x80 sitting there still makes it a non-start. A lookbehind is what reaches that decision --
  // `(?<=a)` is satisfied at offset 1 and nowhere else in this subject, so the whole search hinges
  // on whether offset 1 may be seeded.
  //
  // Iteration cannot answer this question: find_iter's own no-progress rule already forbids an empty
  // match up to the next character boundary, so an empty-match walk stays aligned whatever
  // seed_viable decides, and a walk-based assertion here passes with the seed check disabled.
  const std::string after_a {cat({"a", bytes({0x80})})};
  EXPECT(!searches("(?<=a)", after_a));
  EXPECT(!searches("(?<=a)x*", after_a));
  EXPECT(!searches("(?<=a)", cat({"a", bytes({0x80}), "b"})));

  // A lead byte in the same slot IS a start position: what is refused is the continuation bit
  // pattern, not "the byte after an ASCII character".
  EXPECT(searches("(?<=a)", cat({"a", bytes({0xFF})})));
  EXPECT(searches("(?<=a)", "ab"));
}

TEST(empty_match_iteration_stays_codepoint_aligned_over_malformed_bytes)
{
  // find_iter's no-progress rule, not seed_viable: after an empty match it forbids another until
  // the next character boundary, which is what keeps a zero-width walk codepoint-aligned rather
  // than byte-aligned. Asserted separately because the two mechanisms are independent -- disabling
  // either one leaves the other still producing an aligned walk here.
  // Both the regex and the subject are named: an iterator range outlives neither a temporary
  // pattern nor a temporary text, and the deleted overloads say so.
  const regex              star    {"x*"};
  const std::string        after_a {cat({"a", bytes({0x80})})};
  std::vector<std::size_t> starts;
  for (const auto& m : star.find_iter(after_a)) {
    starts.push_back(m.start(0));
  }
  EXPECT(starts.size() == 2U); // 0 and 2; a byte-aligned engine reports three
  EXPECT(starts[0] == 0U);
  EXPECT(starts[1] == 2U);

  const std::string        pair {bytes({0x80, 0x80})};
  std::vector<std::size_t> pair_starts;
  for (const auto& m : star.find_iter(pair)) {
    pair_starts.push_back(m.start(0));
  }
  EXPECT(pair_starts.size() == 2U); // the run collapses to its two ENDS, never its middle
  EXPECT(pair_starts[0] == 0U);
  EXPECT(pair_starts[1] == 2U);
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
  for (const auto& mc : malformed_catalog()) {
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

TEST(dot_bare_fast_path_rejects_second_byte_bounds_cases)
{
  // Site 1: pike.hpp's run_codepoint_class fast path (bare `.`/`.+`, ONLY when that is the whole
  // pattern) bounds-checks the first continuation byte against utf8_second_byte_bounds_table.
  for (const auto& mc : strict_utf8_boundary_catalog()) {
    EXPECT(!regex(".").fullmatch(mc.seq).matched());
    const std::string doubled {cat({mc.seq, mc.seq})};
    EXPECT(!regex(".+").fullmatch(doubled).matched());
  }
  // Sanity: real multi-byte code points still match (no false negative from the bounds check).
  const std::string euro  {"\xE2\x82\xAC"};
  const std::string emoji {"\xF0\x9F\x98\x80"};
  EXPECT(regex(".").fullmatch(euro).matched());  // € (3-byte)
  EXPECT(regex(".").fullmatch(emoji).matched()); // (4-byte emoji)
}

TEST(dot_width_no_oob_read_past_heap_buffer_end)
{
  // Hotfix guard: run_codepoint_class's width lambda (site 1, pike.hpp) hoisted the 3-/4-byte
  // branches' first-continuation-byte read OUT of the short-circuit `i+2/i+3 < text.size()` guard,
  // losing the prior bounds protection -- a truncated 3-/4-byte lead as the
  // LAST byte of a subject then read one byte past the buffer. Caught by the C-ABI fuzzer under
  // ASan, not by this suite's own `make sanitize` run: every subject elsewhere in this file is a
  // `std::string`, and `std::string` ALWAYS guarantees `data()[size()] == '\0'` as a valid,
  // allocated byte -- SSO or heap-allocated, large or small, that guarantee masks a "read one byte
  // past size()" bug completely, since the read lands on the terminator, still inside the
  // allocation. bare_heap_ending_in gives a subject with no such slack (a `std::vector<char>` of
  // EXACTLY `total` bytes, no implicit terminator), matching the bare pointer+length pair the C
  // API / fuzz harness actually hands the engine -- the only shape that puts a real heap-redzone
  // boundary at `text.size()`. The vector owns its storage (RAII; an earlier raw
  // `new[]`/`.release()` version leaked, caught by LeakSanitizer on CI's Linux leg). The proof is
  // this test running clean under `make sanitize` (ASan aborts the whole process on the read this
  // pins, so a crash here IS the failure signal, not an EXPECT); the assertions below are a bonus
  // correctness check, not the point of the test.
  const unsigned char tricky_leads[] = {0xE0, 0xE4, 0xED, 0xF0, 0xF4};
  for (unsigned char lead : tricky_leads) {
    // The lead byte is the absolute last byte of the buffer (0 continuation bytes present) -- the
    // exact shape the fuzzer found (E0-EF hits the 3-byte branch's guard, F0-F4 the 4-byte one).
    const std::vector<char> buf {bare_heap_ending_in(70, lead)};
    const std::string_view  sv  {buf.data(), buf.size()};
    EXPECT(!regex(".+").fullmatch(sv).matched()); // the truncated tail can never be consumed
    EXPECT(regex(".+").search(sv).matched());     // the 69 leading ASCII bytes still match
  }
  // A 3-byte lead with exactly 1 (not 2) continuation bytes present -- the i+2 guard must also
  // catch this shallower truncation depth, not just "lead is the very last byte."
  {
    std::vector<char> buf(70, 'x');
    buf[68] = static_cast<char>(0xE0);
    buf[69] = static_cast<char>(0xA5); // one valid continuation byte, then end of buffer
    const std::string_view sv {buf.data(), buf.size()};
    EXPECT(!regex(".+").fullmatch(sv).matched());
    EXPECT(regex(".+").search(sv).matched());
  }
}

TEST(strict_utf8_boundary_cases_rejected_on_every_route)
{
  // Cohérence multi-routes: the four second-continuation-byte-bounds cases must be
  // rejected IDENTICALLY no matter which internal route a `.`/negated-ASCII-only-class pattern
  // takes -- fast-path bare, general Pike VM (forced via lookaround, which disqualifies onepass
  // and the lazy-DFA), one-pass, the lazy-DFA byte-program, and the separate public real::dfa
  // engine. Sites 1 and 2 share one bounds table (charclass.hpp), so there is no code path left
  // where the four cases could diverge by construction -- this test is the empirical proof of
  // that claim, not just the design argument for it.
  const auto cases {strict_utf8_boundary_catalog()};

  // -- one-pass: `a.b` and `a[^,]b` are BOTH one-pass-eligible (confirmed via is_one_pass in
  // test_onepass.cpp's own convention) -- real::regex's dispatch is transparent, so exercising
  // them through the public search/fullmatch API exercises the one-pass table walk.
  for (const auto& mc : cases) {
    const std::string ab {cat({"a", mc.seq, "b"})};
    EXPECT(!regex("a.b").fullmatch(ab).matched());
    EXPECT(!regex("a[^,]b").fullmatch(ab).matched());
  }

  // -- general Pike VM, forced: a lookaround clears every DFA/one-pass/class-loop fast-path hint
  // (prefilter.hpp's "has_lookaround" wipe), so `.` inside one always walks the plain compiled
  // byte/klass/split/jump chain with no shortcut in front of it.
  for (const auto& mc : cases) {
    const std::string after  {cat({"ab", mc.seq})};
    const std::string before {cat({mc.seq, "cd"})};
    EXPECT(!searches(R"(ab(?=.))", after));
    EXPECT(!searches(R"((?<=.)cd)", before));
  }

  // -- lazy-DFA: a differential against the SAME search with the route toggled off (test_lazy_
  // dfa_route.cpp's own convention) -- if disabling the route ever changed the (correct) answer,
  // the DFA path would be the one diverging. Padded well past lazy_dfa_min_input (512 B) so the
  // route actually engages, not just compiles eligible.
  for (const auto& mc : cases) {
    std::string corpus     {cat({std::string(700, 'x'), "a", mc.seq, "b"})};
    real::detail::lazy_dfa_route_disabled() = true;
    const bool without_dfa {regex("a.b").search(corpus).matched()};
    real::detail::lazy_dfa_route_disabled() = false;
    const bool with_dfa    {regex("a.b").search(corpus).matched()};
    EXPECT(!without_dfa);
    EXPECT(without_dfa == with_dfa);
  }

  // -- real::dfa: the separate public maximal-munch engine. dfa_flatten copies `.`'s compiled
  // byte/klass/split/jump chain through unchanged (no klass_cp involved), so it inherits the same
  // fix; unlike every route above, a wrong answer here would surface as "the DFA claims a match
  // real::regex itself rejects", not a thrown dfa_error (the pattern compiles and constructs fine
  // both before and after that change).
  {
    std::vector<real::regex> pats {real::regex("a.b")};
    const real::dfa          d    {std::span<const real::regex>(pats)};
    for (const auto& mc : cases) {
      const std::string ab {cat({"a", mc.seq, "b"})};
      EXPECT(!d.match(ab).has_value());
    }
    const std::string valid {cat({"a", std::string {"\xE2\x82\xAC"}, "b"})}; // sanity: € still munches
    EXPECT(d.match(valid).has_value());
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
