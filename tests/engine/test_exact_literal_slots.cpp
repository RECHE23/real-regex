//! Exact-literal slot fill: group-0 end is always cand+len; captures mirror the literal spans.
#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace {

  struct span
  {
    std::size_t start;
    std::size_t end;
  };

  std::vector<span> collect(const char     * pat,
                            std::string_view text)
  {
    const real::regex    re {pat};
    std::vector<span>    out;
    for (const auto& m : re.find_iter(text)) {
      out.push_back({.start = m.start(), .end = m.end()});
    }
    return out;
  }
} // namespace

TEST(exact_literal_plain_spans)
{
  const auto s {collect("dog", "the dog and dog end")};
  EXPECT_EQ(s.size(), 2U);
  EXPECT_EQ(s[0].start, 4U);
  EXPECT_EQ(s[0].end, 7U);
  EXPECT_EQ(s[1].start, 12U);
  EXPECT_EQ(s[1].end, 15U);
}

TEST(exact_literal_single_capture_mirrors_whole_match)
{
  const real::regex re {R"((dog))"};
  const auto        m  {re.search("xxdogyy")};
  EXPECT(m.matched());
  EXPECT_EQ(m.start(0), 2U);
  EXPECT_EQ(m.end(0), 5U);
  EXPECT_EQ(m.start(1), 2U);
  EXPECT_EQ(m.end(1), 5U);
}

TEST(exact_literal_multi_capture_slots)
{
  const real::regex re {R"((d)(og))"};
  const auto        m  {re.search("dog")};
  EXPECT(m.matched());
  EXPECT_EQ(m.start(0), 0U);
  EXPECT_EQ(m.end(0), 3U);
  EXPECT_EQ(m.start(1), 0U);
  EXPECT_EQ(m.end(1), 1U);
  EXPECT_EQ(m.start(2), 1U);
  EXPECT_EQ(m.end(2), 3U);
}

TEST(exact_literal_wb_wrap_and_reject)
{
  const real::regex re  {R"(\bdog\b)"};
  const auto        hit {re.search("cat dog cat")};
  EXPECT(hit.matched());
  EXPECT_EQ(hit.start(), 4U);
  EXPECT_EQ(hit.end(), 7U);
  EXPECT(!re.search("catdogcat").matched());
}

TEST(exact_literal_find_iter_slot_reuse)
{
  // Multiple matches: ensure_size reuse must not leave stale group-0 ends.
  const auto s {collect("ab", "ab x ab y ab")};
  EXPECT_EQ(s.size(), 3U);
  EXPECT_EQ(s[0].start, 0U);
  EXPECT_EQ(s[0].end, 2U);
  EXPECT_EQ(s[1].start, 5U);
  EXPECT_EQ(s[1].end, 7U);
  EXPECT_EQ(s[2].start, 10U);
  EXPECT_EQ(s[2].end, 12U);
}

// --- the one-search route (pattern_hints::literal_one_search) -------------------------------------
// The hint lets run_exact_literal answer a whole search with a single find_prefix, skipping
// next_candidate's chain, literal_at's re-compare and replay_literal's per-match program walk. Each
// of those is redundant ONLY under the hint's own terms, so this pins WHICH shapes carry it: a shape
// that wrongly gained it would lose the assertion retry (silent wrong spans), and one that wrongly
// lost it would only be slower. Both directions are asserted below.

namespace {
  [[nodiscard]] bool one_search(const char* pat)
  {
    const real::regex re {pat};
    return re.raw_program().hints.literal_one_search;
  }
} // namespace

TEST(exact_literal_one_search_hint_armed)
{
  EXPECT(one_search("dog"));       // plain, groupless, >= 2 bytes
  EXPECT(one_search("ab"));
  EXPECT(one_search("localhost")); // longer literal, same shape
}

TEST(exact_literal_one_search_hint_declined)
{
  EXPECT(!one_search("x"));            // 1 byte: next_candidate uses find_byte, not find_prefix
  EXPECT(!one_search(R"(\bdog)"));     // lead assertion must be checked per occurrence
  EXPECT(!one_search(R"(dog\b)"));     // trailing assertion likewise
  EXPECT(!one_search(R"(\Bdog)"));
  EXPECT(!one_search("^dog"));         // anchored_start takes an earlier next_candidate branch
  EXPECT(!one_search("(?m)^dog"));     // line_anchored likewise
  EXPECT(!one_search(R"(\Adog)"));
  EXPECT(!one_search(R"([a-z]+)"));    // not an exact literal at all
}

TEST(exact_literal_one_search_agrees_with_assertion_retry)
{
  // The shapes the hint declines keep the general loop's retry-on-assertion-failure. `\B2` on "220"
  // is the differential-fuzz finding that first forced that retry to exist: the occurrence at 0 fails
  // \B and the scan must go on to the one at 1 rather than report no match.
  const auto b {collect(R"(\B2)", "220")};
  EXPECT_EQ(b.size(), 1U);
  EXPECT_EQ(b[0].start, 1U);
  EXPECT_EQ(b[0].end, 2U);

  // A declined shape and an armed one must agree wherever the assertion is satisfied everywhere.
  EXPECT_EQ(collect(R"(\bab)", "ab ab").size(), collect("ab", "ab ab").size());
}

TEST(exact_literal_one_search_boundaries)
{
  // No match, match at offset 0, match flush against the end, and a literal longer than the subject:
  // the one-search path returns each without the general loop's bounds re-check.
  EXPECT(collect("dog", "cat cat").empty());
  EXPECT(collect("dog", "").empty());
  EXPECT(collect("dogs", "dog").empty());
  const auto head {collect("ab", "ab")};
  EXPECT_EQ(head.size(), 1U);
  EXPECT_EQ(head[0].start, 0U);
  EXPECT_EQ(head[0].end, 2U);
  const auto tail {collect("go", "dogo")};
  EXPECT_EQ(tail.size(), 1U);
  EXPECT_EQ(tail[0].start, 2U);
  EXPECT_EQ(tail[0].end, 4U);

  // Overlapping occurrences: non-overlapping iteration resumes at the previous end, not start+1.
  const auto aa {collect("aa", "aaaa")};
  EXPECT_EQ(aa.size(), 2U);
  EXPECT_EQ(aa[0].start, 0U);
  EXPECT_EQ(aa[1].start, 2U);

  // match / fullmatch keep their own anchored branch above the one-search path.
  const real::regex re {"ab"};
  EXPECT(re.match("abc").matched());
  EXPECT(!re.match("xab").matched());
  EXPECT(re.fullmatch("ab").matched());
  EXPECT(!re.fullmatch("abc").matched());
}
