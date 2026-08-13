// `count_matches` no longer walks the same way `find_iter` does: it returns a number, so no caller can
// observe a capture group, and its walk runs MATCHING-ONLY -- the capture-free walk the compiler otherwise
// grants only to patterns with no user groups at all (real.hpp's count_walk, which sets
// pattern_hints::capture_free_walk on its own copy of the program view).
//
// That makes the two surfaces two different walks over the same language, and this file is what holds them
// to the same answer. The shapes that matter are the ones that write a capture WITHOUT a `save` opcode: a
// Tier-1 possessive over a captured atom writes its group from step()'s fast path, and with a `\b`/`\B`
// wrap the fixed-shape route fills slots directly. Those are exactly what the compiler's `slot_count == 2`
// condition exists to protect (see test_fixed_shape_wb_captures.cpp, which found that hole the hard way),
// and count_walk deliberately drops that condition -- so the capture-free branch of
// tier1_capture_on_match is load-bearing here: without it, a start offset would be handed to the COW
// pool's cow_write as if it were a block handle.
//
// The second half of every case is the one a "count is just a count" reading would skip: the walk owns a
// PRIVATE view, so the regex's own program must come out untouched and its ordinary capture-reading
// surface must still answer, on the same object, after a matching-only walk has run on it.
#include <cstddef>
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  //! \brief Counts by iteration -- the reference walk, which does write captures.
  std::size_t by_iteration(const real::regex& re,
                           std::string_view   subject)
  {
    std::size_t n {0};
    for (const auto& m : re.find_iter(subject)) {
      (void) m;
      ++n;
    }
    return n;
  }

  //! \brief Both walks agree on the count, and the capture surface still works afterwards.
  void agree(const char     * pattern,
             std::string_view subject,
             real::flags      f = real::flags::none)
  {
    const real::regex re        {pattern, f};
    const std::size_t reference {by_iteration(re, subject)};
    EXPECT_EQ(re.count_matches(subject), reference);
    // Order reversed on a second object: a matching-only walk must not be a prerequisite for, nor a
    // corruption of, anything the ordinary walk then does.
    const real::regex re2 {pattern, f};
    EXPECT_EQ(re2.count_matches(subject), reference);
    EXPECT_EQ(by_iteration(re2, subject), reference);
  }
}  // namespace

TEST(count_matching_only_agrees_without_captures)
{
  // Already capture-free by the compiler's own guard: nothing may change for these.
  agree(R"([a-z]+)", "some words here and there");
  agree(R"((?:foo|bar)+baz)", "xx foobarbaz yy foobaz");
  agree(R"(\w+)", "alpha beta gamma");
}

TEST(count_matching_only_agrees_with_user_captures)
{
  // The compiler refuses the flag on every one of these; count_walk forces it.
  agree(R"((foo|bar)+baz)", "xx foobarbaz yy foobaz");
  agree(R"(\b(\w+)@(\w+)\.(com|org)\b)", "mail rene@example.com and a@b.org now");
  agree(R"((a)(b)(c))", "abcabcabc");
  agree(R"((a+)(b*)c)", "aabcaaabbc");
  agree(R"(((a)|(b))+c)", "abc bac c");
  agree(R"((x)?y)", "y xy y");
  agree(R"(^(GET|POST) (/\S*))", "GET /a\n");
}

TEST(count_matching_only_agrees_on_captures_written_without_save)
{
  // Tier-1 possessive over a CAPTURED atom: step() writes the group through the COW pool, and with a
  // `\b`/`\B` wrap the fixed-shape route fills the slots itself. Both modes, because the fixed-shape
  // capture bug that motivates this coverage reproduced identically in bytes mode.
  for (const real::flags f : {real::flags::none, real::flags::bytes}) {
    agree(R"(\b(\w)*+)", "alpha beta gamma", f);
    agree(R"((\w)*+)", "alpha beta", f);
    agree(R"(\B(\w){2}+)", "C1c_BB11BB", f);
    agree(R"(\b(a)++b)", "aaab ab b", f);
    agree(R"(([0-9])++)", "12 345 6", f);
  }
}

TEST(count_matching_only_agrees_on_empty_and_zero_width)
{
  // The iterator's empty-match advance rule is the one piece of walk state that is NOT a capture, so it
  // must survive a walk that carries no block.
  agree(R"((a*))", "baaac");
  agree(R"(())", "  ");
  agree(R"((?m)^(\w+))", "one\ntwo\nthree");
  agree(R"((\w+)(?=@))", "rene@example bob@host");
  agree(R"((?:))", "abc");
}

TEST(count_matching_only_preserves_the_capture_surface)
{
  // The walk copies the program view; the regex's own hints must be unchanged, so groups still read.
  const real::regex re {R"(\b(\w+)@(\w+)\.(com|org)\b)"};
  EXPECT_EQ(re.count_matches("mail rene@example.com and a@b.org now"), 2U);
  const auto m         {re.search("mail rene@example.com and a@b.org now")};
  EXPECT(m.matched());
  EXPECT_EQ(std::string(m[1]), std::string("rene"));
  EXPECT_EQ(std::string(m[2]), std::string("example"));
  EXPECT_EQ(std::string(m[3]), std::string("com"));

  // And a Tier-1 captured possessive, whose group is written outside any `save`.
  const real::regex tier1 {R"(\B(\w){2}+)", real::flags::bytes};
  EXPECT_EQ(tier1.count_matches("C1c_BB11BB"), 4U);
  const auto t            {tier1.search("C1c_BB11BB")};
  EXPECT(t.matched());
  EXPECT_EQ(std::string(t[1]), std::string("c"));
}

TEST(count_matching_only_honours_the_region)
{
  // count_walk rebuilds find_iter's region truncation by hand (substr for endpos, pos as the start);
  // a drift there would be invisible to every whole-subject case above.
  const real::regex re      {R"((\w+))"};
  const std::string subject {"alpha beta gamma delta"};
  EXPECT_EQ(re.count_matches(subject), 4U);
  EXPECT_EQ(re.count_matches(subject, 6), 3U);
  EXPECT_EQ(re.count_matches(subject, 0, 10), 2U);
  EXPECT_EQ(re.count_matches(subject, 6, 16), 2U);
  EXPECT_EQ(re.count_matches(subject, 6, 6), 0U);
}
