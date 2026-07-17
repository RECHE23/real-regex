//! D1 rare-discriminant prefilter: memchr(`:`) + back-verify `https?` + `//` → VM confirm.
//! Routed == core (meta-seam), near-misses never invent a match, multi-hit find_iter parity.
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  std::vector<std::pair<std::size_t, std::size_t>> spans(const real::regex& re,
                                                         std::string_view   text)
  {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (const auto& m : re.find_iter(text)) {
      out.emplace_back(m.start(0), m.end(0));
    }
    return out;
  }

  std::vector<std::pair<std::size_t, std::size_t>> spans_routed(std::string_view pat,
                                                                std::string_view text)
  {
    const real::regex re {pat};
    real::detail::rare_disc_route_disabled() = false;
    return spans(re, text);
  }

  std::vector<std::pair<std::size_t, std::size_t>> spans_core(std::string_view pat,
                                                              std::string_view text)
  {
    const real::regex re {pat};
    real::detail::rare_disc_route_disabled() = true;
    const auto out       {spans(re, text)};
    real::detail::rare_disc_route_disabled() = false;
    return out;
  }
} // namespace

TEST(rare_disc_routed_equals_core)
{
  struct testcase {
    std::string_view pat;
    std::string_view text;
  };
  // Near-misses that must never produce a false match, plus real hits at boundaries / multi-hit.
  const testcase cases[] {
    {.pat = R"(https?://[^\s]+)", .text = "http://x"},
    {.pat = R"(https?://[^\s]+)", .text = "https://x"},
    {.pat = R"(https?://[^\s]+)", .text = "see http://a.com and https://b.org/x end"},
    {.pat = R"(https?://[^\s]+)", .text = "http:/"},           // incomplete after colon
    {.pat = R"(https?://[^\s]+)", .text = "shttp://x"},        // polluted prefix → still matches inner http://
    {.pat = R"(https?://[^\s]+)", .text = ":"},
    {.pat = R"(https?://[^\s]+)", .text = "a:b:c::::d"},       // dense colon filler, no URL
    {.pat = R"(https?://[^\s]+)", .text = "http://"},          // scheme only, no host body
    {.pat = R"(https?://[^\s]+)", .text = "https://"},
    {.pat = R"(https?://[^\s]+)", .text = "http: //x"},        // space after colon
    {.pat = R"(https?://[^\s]+)", .text = "http//x"},          // missing colon
    {.pat = R"(https?://[^\s]+)", .text = "HTTPS://x"},        // case-sensitive default
    {.pat = R"(https?://[^\s]+)", .text = "prefixhttp://end"},
    {.pat = R"(https?://[^\s]+)", .text = "::::http://ok"},
    {.pat = R"(https?://[^\s]+)", .text = "http://x https://y"},
    {.pat = R"(https?://[^\s]+)", .text = ""},
    {.pat = R"(http://[^\s]+)",   .text = "http://only https://not"}, // plain http disc (no opt)
    {.pat = R"(http://[^\s]+)",   .text = "see http://a.com end"},
  };
  for (const testcase& tc : cases) {
    const auto core   {spans_core(tc.pat, tc.text)};
    const auto routed {spans_routed(tc.pat, tc.text)};
    EXPECT(core == routed);
  }
  real::detail::rare_disc_route_disabled() = false;
}

TEST(rare_disc_near_miss_never_matches)
{
  const real::regex re            {R"(https?://[^\s]+)"};
  // Back-verify must reject these; VM would reject too — pin both routes.
  const std::string_view misses[] {
    "http:/", "http://", "https://", "http//x", "http: //x", ":", "::::",
    "htt://x", "hxxp://x", "HTTP://x", "ftp://x",
  };
  for (const auto miss : misses) {
    real::detail::rare_disc_route_disabled() = false;
    EXPECT(!re.search(miss));
    real::detail::rare_disc_route_disabled() = true;
    EXPECT(!re.search(miss));
  }
  real::detail::rare_disc_route_disabled() = false;
}

TEST(rare_disc_finds_every_url_match)
{
  const real::regex re {R"(https?://[^\s]+)"};
  EXPECT_EQ(re.search("http://x").start(), 0U);
  EXPECT_EQ(re.search("https://x").start(), 0U);
  EXPECT_EQ(re.search("head http://example.com tail")[0], std::string_view("http://example.com"));
  EXPECT_EQ(re.search("shttp://x").start(), 1U); // match at the inner `http://`

  std::string multi;
  multi += "nope ";
  multi += "http://a ";
  multi += "filler:colon:noise ";
  multi += "https://b.org/path ";
  multi += "http:/bad ";
  multi += "https://c";
  std::size_t n {0};
  for (const auto& m : re.find_iter(multi)) {
    (void)m;
    ++n;
  }
  EXPECT_EQ(n, 3U);

  // Dense colon noise must not mask a real hit later.
  std::string dense_colon(200, ':');
  dense_colon += "http://found";
  EXPECT_EQ(re.search(dense_colon)[0], std::string_view("http://found"));
}

TEST(rare_disc_meta_seam_forces_prefix_path)
{
  // With the route forced off, search still finds URLs (via prefix/first-byte); with it on, same spans.
  const real::regex re   {R"(https?://[^\s]+)"};
  const std::string text {
    "noise http://one more https://two end http:/bad : : shttp://three"
  };
  real::detail::rare_disc_route_disabled() = false;
  const auto routed {spans(re, text)};
  real::detail::rare_disc_route_disabled() = true;
  const auto core   {spans(re, text)};
  real::detail::rare_disc_route_disabled() = false;
  EXPECT_EQ(routed.size(), 3U);
  EXPECT(routed == core);
}

TEST(rare_disc_dense_colon_matches_core)
{
  // Density abandon: a colon-dense haystack with sparse real URLs must keep every span (sticky
  // switch to prefix after failed verifies — never miss a match).
  std::string text;
  for (int i = 0; i < 200; ++i) {
    text += "a:b:c:d:e:f:g:h:i:j ";
    if (i % 40 == 0) {
      text += "http://hit ";
    }
    if (i % 55 == 0) {
      text += "https://secure ";
    }
  }
  const auto core   {spans_core(R"(https?://[^\s]+)", text)};
  const auto routed {spans_routed(R"(https?://[^\s]+)", text)};
  EXPECT(!core.empty());
  EXPECT(routed == core);
}
