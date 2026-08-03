// `\A`/`^` is peeled by the shape recognizers and honoured as a MODE by the routes they arm
// (prefilter.hpp's shape_lead::anchored_start, pike.hpp's anchored_mode). That is two pieces of
// machinery that must agree, in a place where disagreeing is silent: a forward-scanning route armed
// for a pattern pinned to position 0 returns matches the program forbids, and every existing test
// still passes because they use unanchored patterns.
//
// The walk cases are not decoration. The batched walk bypasses run() -- which is where the anchor
// becomes prefix anchoring -- so it must refuse anchored shapes, and it did not until a spans-level
// differential caught `^[a-z]+` reporting a match at offset 2 of "  abc".
#include <string>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/automata/lazy_dfa.hpp" // class_fastpath_disabled
#include "real/real.hpp"

namespace {

  std::vector<std::pair<std::size_t, std::size_t>> walk(const real::regex& rx,
                                                        std::string_view   t)
  {
    std::vector<std::pair<std::size_t, std::size_t>> v;
    for (const auto& m : rx.find_iter(t)) {
      v.emplace_back(m.start(0), m.end(0));
    }
    return v;
  }
} // namespace

TEST(anchored_class_shape_matches_only_at_zero)
{
  for (const char* pat : {"^[a-z]+", "\\A[a-z]+"}) {
    const real::regex rx {pat};
    EXPECT(rx.search(std::string_view {"abc def"}).matched());
    EXPECT(rx.search(std::string_view {"abc def"}).start(0) == 0);
    EXPECT(rx.search(std::string_view {"abc def"}).end(0) == 3);
    EXPECT(!rx.search(std::string_view {"  abc"}).matched());   // the run exists, but not at 0
    EXPECT(!rx.search(std::string_view {"123abc"}).matched());
    EXPECT(!rx.search(std::string_view {"\nabc"}).matched());   // not multiline: `^` is the SUBJECT start
    EXPECT(!rx.search(std::string_view {""}).matched());
    // A region that begins past 0 can never hold the match, whatever it contains.
    EXPECT(!rx.search(std::string_view {"abc def"}, 1).matched());
    EXPECT(!rx.search(std::string_view {"xabc"}, 1).matched());
  }
}

TEST(anchored_class_shape_yields_one_match_per_walk)
{
  const real::regex rx                                {"^[a-z]+"};
  EXPECT(walk(rx, "abc def").size() == 1);
  const std::pair<std::size_t, std::size_t> first_run {0, 3};
  EXPECT(walk(rx, "abc def")[0] == first_run);
  EXPECT(walk(rx, "abc\ndef").size() == 1);
  EXPECT(walk(rx, "aaa,bbb").size() == 1);
  EXPECT(walk(rx, "  abc").empty());
  EXPECT(walk(rx, "123abc").empty());
}

TEST(anchored_codepoint_class_shape)
{
  const real::regex rx {"^\\w+"};
  EXPECT(rx.search(std::string_view {"abc def"}).matched());
  EXPECT(rx.search(std::string_view {"abc def"}).end(0) == 3);
  EXPECT(!rx.search(std::string_view {" abc"}).matched());
  EXPECT(walk(rx, "abc def").size() == 1);
}

TEST(multiline_caret_is_not_peeled)
{
  // Multiline `^` is a DIFFERENT assertion (line start), so the peel must not apply and the walk
  // must still find a run after every newline.
  const real::regex rx {"^[a-z]+", real::flags::multiline};
  EXPECT(walk(rx, "abc\ndef").size() == 2);
  EXPECT(walk(rx, "abc\ndef")[1].first == 4);
  EXPECT(rx.search(std::string_view {"\nabc"}).matched());
}

TEST(anchored_shape_agrees_with_the_general_route)
{
  // The seam differential in miniature: forcing the shape routes out must not change an answer.
  for (const char* pat : {"^[a-z]+", "^\\w+", "^[0-9]+", "^[^,]+"}) {
    for (const char* subj : {"", "a", "abc def", "  abc", "123abc", "abc\ndef", "aaa,bbb"}) {
      real::detail::class_fastpath_disabled() = false;
      const auto fast    {walk(real::regex {pat}, subj)};
      real::detail::class_fastpath_disabled() = true;
      const auto general {walk(real::regex {pat}, subj)};
      real::detail::class_fastpath_disabled() = false;
      EXPECT(fast == general);
    }
  }
}
