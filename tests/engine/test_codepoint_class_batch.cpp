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
// `batch_cap` is 16, so 200 fields crosses it a dozen times — and the first field of each refill is
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
