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
