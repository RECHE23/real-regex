// The `.`/negated-class shape was the one class scan with no batch filler: it crossed a full route
// entry PER MATCH where the byte- and code-point-class routes cross one per `batch_cap`. Measured on
// their own fast paths, `[a-z]+` cost 5.55 ns per match against `[^,]+`'s 19.45 — and `fields [^,]+`
// and `.` are the two weakest rows in docs/BENCHMARKS.md against PCRE2-JIT.
//
// A batched walk and a repeated `search()` take DIFFERENT code (the filler versus
// `run_codepoint_class`), so the tests below are differential: whatever `find_iter` reports must be
// exactly what searching again from the previous end reports. That is the property a filler can break
// in ways a single-pattern spot check would miss — at the batch boundary, across the empty-match
// rules, or by carrying `pos_` wrong.
//
// These PASS with the filler disabled, and say so rather than implying otherwise: with it off both
// sides of every comparison are the same route, so they agree trivially. They are a guard against a
// filler that misbehaves, not evidence that one is installed — whether it is chosen is not observable
// from the public API (the eligibility flag is private and the route counters need REAL_PROFILE), and
// the speed it buys belongs in the benchmarks, not here.
#include <string>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  using spans = std::vector<std::pair<std::size_t, std::size_t>>;

  //! Every match, via the batched walk.
  spans by_iteration(const real::regex& re,
                     const std::string& text)
  {
    spans out;
    for (const auto& m : re.find_iter(text)) {
      out.emplace_back(m.start(), m.end());
    }
    return out;
  }

  //! Every match, by searching again from where the last one ended — the unbatched route.
  spans by_search(const real::regex& re,
                  const std::string& text)
  {
    spans       out;
    std::size_t pos {0};
    while (pos <= text.size()) {
      const auto m {re.search(text, pos)};
      if (!m.matched()) {
        break;
      }
      out.emplace_back(m.start(), m.end());
      pos = m.end() > m.start() ? m.end() : m.start() + 1;
    }
    return out;
  }

  //! A subject long enough to cross the batch boundary several times over.
  std::string csv(std::size_t fields)
  {
    std::string s;
    for (std::size_t i = 0; i < fields; ++i) {
      s += "field";
      s += static_cast<char>('a' + static_cast<char>(i % 26));
      s += ',';
    }
    return s;
  }
} // namespace

// The batched walk and the unbatched search must agree, span for span, across the batch boundary.
// `batch_cap` is 4, so 200 fields crosses it fifty times — and the first field of each refill is
// the one a mis-carried `pos_` would drop or duplicate.
TEST(the_batched_walk_agrees_with_repeated_search)
{
  const std::string text {csv(200)};
  for (const char* p : {"[^,]+", "[^ ]+", "."}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  EXPECT_EQ(by_iteration(real::regex {"[^,]+"}, text).size(), 200U);
}

// Non-ASCII subjects: the filler decodes code points, so a multi-byte character must be one match
// position, not one per byte, and a match must never split a character.
TEST(the_filler_agrees_on_multi_byte_subjects)
{
  const std::string text {"café 世界,Привет 😀,naïve résumé,ﬁn"};
  for (const char* p : {"[^,]+", ".", "[^ ]+"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  // `.` counts CHARACTERS, not bytes: 😀 is one match of four bytes.
  const auto dots {by_iteration(real::regex {"."}, std::string {"a😀b"})};
  EXPECT_EQ(dots.size(), 3U);
  EXPECT_EQ(dots[1].second - dots[1].first, 4U);
}

// Malformed UTF-8 must stop a run exactly where the unbatched route stops it — the filler carries the
// same strict width test, and this is where a byte-wise shortcut would diverge.
TEST(malformed_input_stops_the_run_identically)
{
  const std::string bogus {"ab\xC3\xC3\xA9 cd,ef\x80\x80,gh"};
  for (const char* p : {"[^,]+", ".", "[^ ]+"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, bogus), by_search(re, bogus));
  }
}

// The shapes batching must decline: an anchored pattern (the filler scans forward and would report a
// match past the anchor), and the non-search modes, which keep the original route.
TEST(the_shapes_batching_declines_are_unchanged)
{
  const std::string text {"  abc def"};
  EXPECT(!real::regex {"^[^ ]+"}.search(text).matched() || real::regex {"^[^ ]+"}.search(text).start() == 0U);
  EXPECT_EQ(by_iteration(real::regex {"^[^ ]+"}, text), by_search(real::regex {"^[^ ]+"}, text));

  EXPECT(real::regex {"[^,]+"}.fullmatch("abc"));
  EXPECT(!real::regex {"[^,]+"}.fullmatch("a,c"));
  EXPECT_EQ(real::regex {"[^,]+"}.match("abc,def").end(), 3U);

  // Region-aware search still honours pos/endpos, which the batched walk must not overrun.
  const real::regex  region_re      {"[^,]+"};
  const std::string  region_subject {"abc,def,ghi"};
  const auto         region         {region_re.search(region_subject, 4, 7)};
  EXPECT(region.matched());
  EXPECT_EQ(region.start(), 4U);
  EXPECT_EQ(region.end(), 7U);
}

// A single-codepoint form (no `+`) and an empty-capable one exercise the filler's run handling at both
// extremes: one character per span, and the empty-match advance rule.
TEST(single_and_empty_capable_forms_agree)
{
  const std::string text {"a,bb,,ccc"};
  for (const char* p : {"[^,]", "[^,]*", "[^,]+"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  EXPECT_EQ(real::regex {"[^,]"}.count_matches(text), 6U); // a b b c c c
}

// The bare single byte-class (`[a-z]`, no quantifier) is the fourth batched shape, and the one with
// the least margin for error: every span is exactly one byte, so a filler off by one position drops or
// duplicates a match instead of merely mis-sizing a run. Same differential as above — the batched walk
// against searching again from the previous end.
TEST(the_single_class_walk_agrees_with_repeated_search)
{
  const std::string text {csv(200)};
  for (const char* p : {"[a-z]", "[aeiou]", "[a-zA-Z]", "[a-z0-9]", "[^a-z]"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  // Spans are one byte wide and consecutive matches are consecutive positions -- the property the
  // filler exploits, asserted rather than assumed.
  const auto runs {by_iteration(real::regex {"[a-z]"}, std::string {"abc,de"})};
  EXPECT_EQ(runs.size(), 5U);
  for (const auto& s : runs) {
    EXPECT_EQ(s.second - s.first, 1U);
  }
  EXPECT_EQ(runs[2].first, 2U); // 'c', then the comma is skipped
  EXPECT_EQ(runs[3].first, 4U);
}

// A single byte-class over non-ASCII input: the class is a BYTE class, so a multi-byte character's
// bytes are tested individually. Whatever the general route decides there, the filler must decide
// identically -- this is where a filler that assumed character alignment would diverge.
TEST(the_single_class_filler_agrees_on_multi_byte_subjects)
{
  const std::string text {"café 世界,Привет 😀,naïve résumé"};
  for (const char* p : {"[a-z]", "[^a-z]", "[a-zA-Z]", "[aeiou]"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  const std::string bogus {"ab\xC3\xC3\xA9 cd\x80\x80gh"};
  for (const char* p : {"[a-z]", "[^a-z]"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, bogus), by_search(re, bogus));
  }
}

// The shapes the single-class filler must decline, each pinned by BEHAVIOUR rather than by the hint:
// an anchor (the filler scans forward and would report a match past it), a capture wrap (slots the
// filler does not fill), a `\b` wrap (a per-candidate assertion), and the non-search modes.
TEST(the_single_class_shapes_batching_declines)
{
  const std::string text {"  abc def"};
  EXPECT_EQ(by_iteration(real::regex {"^[a-z]"}, text), by_search(real::regex {"^[a-z]"}, text));
  EXPECT(!real::regex {"^[a-z]"}.search(text).matched());

  const real::regex captured {"([a-z])"};
  const auto        cm       {captured.search(text)};
  EXPECT(cm.matched());
  EXPECT_EQ(cm.start(1), 2U); // the group is filled, which the filler never does
  EXPECT_EQ(cm.end(1), 3U);
  EXPECT_EQ(by_iteration(captured, text), by_search(captured, text));

  const real::regex bounded {R"(\b[a-z]\b)"};
  EXPECT_EQ(by_iteration(bounded, std::string {"a bc d"}), by_search(bounded, std::string {"a bc d"}));
  EXPECT_EQ(by_iteration(bounded, std::string {"a bc d"}).size(), 2U); // 'a' and 'd', not b/c

  EXPECT(real::regex {"[a-z]"}.fullmatch("a"));
  EXPECT(!real::regex {"[a-z]"}.fullmatch("ab"));
  EXPECT_EQ(real::regex {"[a-z]"}.match("abc").end(), 1U);

  const real::regex region_re {"[a-z]"};
  const std::string subject   {"abcdefghi"};
  const auto        region    {region_re.search(subject, 4, 7)};
  EXPECT(region.matched());
  EXPECT_EQ(region.start(), 4U);
  EXPECT_EQ(region.end(), 5U);
  EXPECT_EQ(region_re.count_matches(subject), 9U);
}

// The dropped-leading-`\b` shape (`\b\w+\b`, where the recognizer proves the assertion redundant on
// a maximal run and removes it, setting pattern_hints::wb_lead_maximal_run). Batching used to decline
// it outright; the fillers now carry the same one-position window-edge guard the general route does,
// and THIS TEST IS THAT GUARD'S ONLY PROOF. The whole risk lives at a caller-supplied `pos > 0`: `pos`
// restricts where a match may start, it does NOT assert that `text[pos - 1]` is absent or non-word, so
// a run beginning exactly at `pos` inside a word must be refused even though B-1's redundancy argument
// would otherwise accept it. Found live by differential fuzzing before the guard existed.
TEST(the_dropped_lead_word_boundary_agrees_from_every_offset)
{
  const std::string text {"the quick_brown fox42 caf\xC3\xA9 na\xC3\xAFve, w\xC3\xB6rld"};
  for (const char* p : {R"(\b\w+\b)", R"(\b\w+)", R"(\w+\b)", R"(\b[A-Za-z0-9_]+\b)"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
    // EVERY offset, not a sampled few: the guard fires only when the batch's first candidate
    // coincides with `pos`, so the offsets that exercise it are exactly the ones interior to a word.
    // `find_iter(text, pos)` is the BATCHED path; `search(text, pos)` is the general route, which is
    // therefore the oracle. They must agree on the first match at every single offset.
    for (std::size_t pos {0}; pos <= text.size(); ++pos) {
      const auto general {re.search(text, pos)};
      auto       range   {re.find_iter(text, pos)};
      const auto it      {range.begin()};
      const bool batched {!it.exhausted()};
      EXPECT_EQ(batched, general.matched());
      if (batched && general.matched()) {
        EXPECT_EQ(it->start(), general.start());
        EXPECT_EQ(it->end(), general.end());
      }
    }
  }
  // The discriminating case in the open, so a regression names itself: searching `\b\w+` from inside
  // a word must NOT match at that offset -- there is no boundary there -- and must skip the whole run.
  const real::regex bw   {R"(\b\w+)"};
  const std::string word {"hello world"};
  EXPECT_EQ(bw.search(word, 2).start(), 6U); // inside "hello" -> "world", not "llo"
  EXPECT_EQ(bw.search(word, 0).start(), 0U);
  EXPECT_EQ(bw.search(word, 6).start(), 6U); // AT a boundary: accepted
  // A subject that is one long word has no second run to fall back on.
  const std::string one {"abcdef"};
  EXPECT(!bw.search(one, 3).matched());
  // ... and the batched walk must reach the same conclusion, not merely the general route.
  auto solo {bw.find_iter(one, 3)};
  EXPECT(solo.begin().exhausted());
}

// A BARE unbounded possessive loop is REDIRECTED by the recognizer to the greedy selector -- byte and
// code-point kinds both -- because it is the same language: possessive takes the maximal run and never
// gives it back, and with nothing after the loop there is nothing to give back to. That makes these
// patterns take a batched route they never used to, so the spans need the same differential every
// other batched shape gets here, not just the hint pins in test_possessive_fastpath.cpp.
TEST(the_redirected_possessive_walk_agrees_with_repeated_search)
{
  const std::string text {"abc,de f 42 ghij caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87 x_y1"};
  for (const char* p : {"[a-z]++", R"(\w++)", "(?>[a-z]+)", R"((?>\w+))", "[a-z0-9]++"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  // The equivalence the redirect rests on, span for span rather than by count.
  EXPECT_EQ(by_iteration(real::regex {"[a-z]++"}, text), by_iteration(real::regex {"[a-z]+"}, text));
  EXPECT_EQ(by_iteration(real::regex {R"(\w++)"}, text), by_iteration(real::regex {R"(\w+)"}, text));
  EXPECT_EQ(by_iteration(real::regex {"(?>[a-z]+)"}, text), by_iteration(real::regex {"[a-z]+"}, text));
  EXPECT_EQ(by_iteration(real::regex {R"((?>\w+))"}, text), by_iteration(real::regex {R"(\w+)"}, text));
  // Region search from an offset must agree too -- the redirect changes the route, not the bounds.
  const real::regex bp {R"(\w++)"};
  for (std::size_t pos {0}; pos <= text.size(); ++pos) {
    const auto poss {bp.search(text, pos)};
    const auto grdy {real::regex {R"(\w+)"}.search(text, pos)};
    EXPECT_EQ(poss.matched(), grdy.matched());
    if (poss.matched()) {
      EXPECT_EQ(poss.start(), grdy.start());
      EXPECT_EQ(poss.end(), grdy.end());
    }
  }
}

// The shapes the redirect refuses, each pinned by behaviour: `X*+` can match empty, a required suffix
// means the match is not the run, and an enveloping capture keeps its slots where the greedy path does
// not look.
TEST(the_possessive_shapes_the_redirect_refuses)
{
  const std::string text {"ab,,cd;ef 12"};
  for (const char* p : {"[a-z]*+", "[a-z]++;", R"(\w*+x)", R"(\d++x)"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  EXPECT(by_iteration(real::regex {"[a-z]*+"}, text).size()
         > by_iteration(real::regex {"[a-z]++"}, text).size()); // `*+` matches empty, `++` does not

  const real::regex delim {"[a-z]++;"};
  const auto        dm    {delim.search(text)};
  EXPECT(dm.matched());
  EXPECT_EQ(dm.end(), 7U); // the semicolon is part of the match

  const real::regex captured {"([a-z]++)"};
  const auto        cm       {captured.search(text)};
  EXPECT(cm.matched());
  EXPECT_EQ(cm.start(1), cm.start());
  EXPECT_EQ(cm.end(1), cm.end());
  EXPECT_EQ(by_iteration(captured, text), by_search(captured, text));
}

// The `{k,}` counted minimum on the CODE-POINT class route (`\w{2,}`, `\p{L}{3,}`). `min` is counted in
// CODE POINTS there, not bytes, so a run of three 2-byte characters satisfies `{3,}` while six ASCII
// bytes of a 7-minimum does not -- the discriminating property, and the one a byte-length shortcut
// would get wrong on exactly the multi-byte input this engine exists for.
TEST(the_codepoint_counted_minimum_agrees_with_repeated_search)
{
  const std::string text {"a ab abc caf\xC3\xA9 na\xC3\xAFve \xE4\xB8\xAD\xE6\x96\x87 \xD0\x9F\xD1\x80\xD0\xB8 x_1"};
  for (const char* p : {R"(\w{2,})", R"(\w{3,})", R"(\w{6,})", R"(\p{L}{2,})", R"(\p{L}{3,})"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  // Counted in CODE POINTS: "中文" is two characters in six bytes, so it satisfies `{2,}` and not
  // `{3,}`. A byte-length check would accept both.
  const std::string cjk {"\xE4\xB8\xAD\xE6\x96\x87"};
  EXPECT_EQ(real::regex {R"(\p{L}{2,})"}.count_matches(cjk), 1U);
  EXPECT_EQ(real::regex {R"(\p{L}{3,})"}.count_matches(cjk), 0U);
  // A too-short run must be SKIPPED, not merely trimmed: the next run is still found.
  const auto spans {by_iteration(real::regex {R"(\w{3,})"}, std::string {"ab cdef gh ijkl"})};
  EXPECT_EQ(spans.size(), 2U);
  EXPECT_EQ(spans[0].first, 3U);  // "cdef"
  EXPECT_EQ(spans[1].first, 11U); // "ijkl"
}

// The fixed ALTERNATION route, batched below the Aho-Corasick branch floor. Two properties matter and
// neither is obvious from the spans alone: branches are leftmost-FIRST in source order (so `the|then`
// matches "the" inside "then", not the longer one), and a match may end inside a later 16-byte block,
// so the walk must resume from the match END rather than from the mask it was scanning.
TEST(the_alternation_walk_agrees_with_repeated_search)
{
  const std::string text {"the fox and the dog then thefox catdogfish caf\xC3\xA9 dogma foxtrot"};
  for (const char* p : {"the|fox|dog", "cat|dog", "the|then", "fox|f", "a|b|c"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  // Leftmost-first among branches, not longest: "then" yields "the".
  const auto tf {by_iteration(real::regex {"the|then"}, std::string {"then"})};
  EXPECT_EQ(tf.size(), 1U);
  EXPECT_EQ(tf[0].second, 3U);
  // A subject long enough that matches land across many 16-byte blocks, including one that STRADDLES
  // a block boundary -- the case a mask-carried scan gets wrong if it resumes from the mask.
  std::string wide;
  for (int i = 0; i < 40; ++i) {
    wide += "xxxxxxxxxxxxxxdog";
  }
  const real::regex dg {"cat|dog"};
  EXPECT_EQ(by_iteration(dg, wide), by_search(dg, wide));
  EXPECT_EQ(by_iteration(dg, wide).size(), 40U);
}

// The alternation shapes batching must decline. The branch-count bound is the load-bearing one: a
// batched walk bypasses run(), so a shape the Aho-Corasick density gate could claim must NOT be
// batched, or a measured routing decision would be silently overruled.
TEST(the_alternation_shapes_batching_declines)
{
  const std::string text {"cat dog fish bird owl rat hen cat dog fish bird owl rat hen"};
  for (const char* p : {"cat|dog|fish|bird", "cat|dog|fish|bird|owl|rat|hen", "(cat|dog)", "^cat|dog"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  // A capturing alternation keeps its group, which the batched path does not fill.
  const real::regex captured {"(cat|dog)"};
  const auto        cm       {captured.search(text)};
  EXPECT(cm.matched());
  EXPECT_EQ(cm.start(1), cm.start());
  EXPECT_EQ(cm.end(1), cm.end());
}

// A KEPT `\b`/`\B` wrap on a byte class (`\b[a-z]+\b`). Unlike `\b\w+\b`, whose leading assertion the
// recognizer proves redundant and drops, a maximal `[a-z]+` run can legitimately begin after `_` or a
// digit -- both word characters -- so the wrap is real and the filler must evaluate it on EVERY span,
// skipping whole runs that fail it. That is the property under test, and `_` and digits are the
// discriminating neighbours.
TEST(the_kept_word_boundary_walk_agrees_with_repeated_search)
{
  const std::string text {"abc _def 9ghi jkl_ mno9 pqr caf\xC3\xA9 stu"};
  for (const char* p : {R"(\b[a-z]+\b)", R"(\b[a-z]+)", R"([a-z]+\b)", R"(\B[a-z]+\B)",
                        R"(\b[a-z0-9]+\b)"}) {
    const real::regex re {p};
    EXPECT_EQ(by_iteration(re, text), by_search(re, text));
  }
  // The discriminating cases in the open. `_def`: the run "def" starts after `_`, a word character,
  // so there is no boundary and `\b[a-z]+\b` must NOT match it. Same for "ghi" after `9`, and for
  // "jkl" before `_`, and "mno" before `9`.
  const auto spans {by_iteration(real::regex {R"(\b[a-z]+\b)"}, text)};
  EXPECT_EQ(spans.size(), 3U); // abc, pqr, stu -- and nothing else
  EXPECT_EQ(spans[0].first, 0U);
  EXPECT_EQ(text.substr(spans[1].first, spans[1].second - spans[1].first), std::string {"pqr"});
  EXPECT_EQ(text.substr(spans[2].first, spans[2].second - spans[2].first), std::string {"stu"});
  // A failing wrap must skip the WHOLE run, not retry inside it: "_def" yields nothing at all, and
  // the next accepted run is found.
  EXPECT_EQ(by_iteration(real::regex {R"(\b[a-z]+\b)"}, std::string {"_abcdef ghi"}).size(), 1U);
  // Region search from every offset, against the general route as oracle.
  const real::regex bw {R"(\b[a-z]+\b)"};
  for (std::size_t pos {0}; pos <= text.size(); ++pos) {
    const auto general {bw.search(text, pos)};
    auto       range   {bw.find_iter(text, pos)};
    const auto it      {range.begin()};
    EXPECT_EQ(!it.exhausted(), general.matched());
    if (general.matched() && !it.exhausted()) {
      EXPECT_EQ(it->start(), general.start());
      EXPECT_EQ(it->end(), general.end());
    }
  }
}

// count_matches and find_all read the same walk, so they must report the same thing the iteration does
// — this is the API surface the batched path actually serves.
TEST(the_public_counters_agree_with_the_walk)
{
  const std::string text {csv(64)};
  for (const char* p : {"[^,]+", ".", "[^ ]+"}) {
    const real::regex re {p};
    const spans       it {by_iteration(re, text)};
    EXPECT_EQ(re.count_matches(text), it.size());
    EXPECT_EQ(re.find_all(text).size(), it.size());
  }
}
