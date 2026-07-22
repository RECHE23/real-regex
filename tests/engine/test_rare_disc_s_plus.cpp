//! D1b: capture-free rare-disc + trailing [^\s]+ / \S+ — dedicated confirm route (no Pike).
//! Shape-key is compile-time form (not "http"); capturers decline; Unicode whitespace ends body.
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  using real::detail::dynamic_storage;

  real::detail::pattern_hints hints_of(std::string_view pat)
  {
    return dynamic_storage::compile(pat, real::flags::none).program.hints;
  }

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

TEST(rare_disc_s_plus_arms_capture_free_url_only)
{
  EXPECT(hints_of(R"(https?://[^\s]+)").rare_disc_s_plus);
  EXPECT(hints_of(R"(http://[^\s]+)").rare_disc_s_plus);
  EXPECT(hints_of(R"(https?://\S+)").rare_disc_s_plus);
  // Capturing body — route declines (would leave group slots empty).
  // Delimiter form of raw string: R"(...)" would end at the first ")".
  EXPECT(!hints_of(R"re(https?://([^\s]+))re").rare_disc_s_plus);
  EXPECT(hints_of(R"re(https?://([^\s]+))re").rare_disc >= 0); // disc still arms for prefilter
  // Whole-pattern class+ — not rare_disc_s_plus.
  EXPECT(!hints_of(R"([^\s]+)").rare_disc_s_plus);
  EXPECT(!hints_of(R"(\w+)").rare_disc_s_plus);
}

// D1c FIX-1: near-\S class must DECLINE (exhaustive hi-range equality).
// Fable repro: https?://[^\s\u0500]+ armed wrongly and swallowed U+0500.
TEST(rare_disc_s_plus_declines_near_miss_not_space_class)
{
  // \u0500 is U+0500 — not whitespace; excluding it is a near-miss of [^\s]
  // that the old sample-based armament would accept.
  EXPECT(!hints_of(R"re(https?://[^\s\u0500]+)re").rare_disc_s_plus);
  // Exact \S still arms.
  EXPECT(hints_of(R"(https?://[^\s]+)").rare_disc_s_plus);
  {
    const std::string_view pat {R"re(https?://[^\s\u0500]+)re"};
    // Body "aa" + U+0500 (UTF-8 D4 80) + "bb" — VM stops before U+0500.
    std::string text           {"http://aa"};
    text.push_back(static_cast<char>(0xD4));
    text.push_back(static_cast<char>(0x80));
    text += "bb ";
    EXPECT(spans_core(pat, text) == spans_routed(pat, text));
    // Core/routed both stop at U+0500 (span end 9 = after "http://aa").
    const real::regex re {pat};
    const auto        m  {re.search(text)};
    EXPECT(m.matched());
    EXPECT_EQ(m.end(), 9U);
  }
  real::detail::rare_disc_route_disabled() = false;
}

// D1c FIX-2: opt == disc is structural decline (not rarity-table luck).
TEST(rare_disc_s_plus_declines_when_opt_equals_disc)
{
  // Surface forms that could produce opt==disc decline s_plus by construction.
  EXPECT(!hints_of(R"re(x:?:/[^\s]+)re").rare_disc_s_plus);
  EXPECT(!hints_of(R"re(ab:?:[^\s]+)re").rare_disc_s_plus);
}

// D1c FIX-3: form-frontier fuzz — near-miss classes and capturers never arm s_plus;
// exact capture-free \S always arms; routed==core on mixed form×text.
TEST(rare_disc_s_plus_form_frontier_fuzz)
{
  // Exact forms that must arm.
  EXPECT(hints_of(R"(https?://\S+)").rare_disc_s_plus);
  EXPECT(hints_of(R"(http://\S+)").rare_disc_s_plus);
  // Near-miss / wrong forms that must decline.
  EXPECT(!hints_of(R"re(https?://([^\s]+))re").rare_disc_s_plus);      // capture
  EXPECT(!hints_of(R"re(https?://[^\s\u0500]+)re").rare_disc_s_plus);  // near-\S
  EXPECT(!hints_of(R"re(https?://[^\s\u00A1]+)re").rare_disc_s_plus);  // exclude U+00A1
  EXPECT(!hints_of(R"([^\s]+)").rare_disc_s_plus);                     // no scheme
  // Routed == core on exact form with random text (form fixed, text varies).
  const std::string_view             pat   {R"(https?://[^\s]+)"};
  // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed)
  std::mt19937                       rng   {0xD1C3U};
  std::uniform_int_distribution<int> coin  {0, 1};
  std::uniform_int_distribution<int> bdist {0, 255};
  for (int trial = 0; trial < 40; ++trial) {
    std::string text;
    for (int i = 0; i < 20; ++i) {
      text += "word ";
      if (coin(rng) != 0) {
        text += (coin(rng) != 0) ? "http://h/" : "https://s/";
        text += std::to_string(i);
        text += (coin(rng) != 0) ? "\xC2\xA0" : "\xE2\x80\xA8";
      }
      for (int k = 0; k < 4; ++k) {
        text.push_back(static_cast<char>(bdist(rng)));
      }
    }
    EXPECT(spans_core(pat, text) == spans_routed(pat, text));
  }
  // Near-miss form: always parity (s_plus off → same path under seam either way).
  {
    const std::string_view near {R"re(https?://[^\s\u0500]+)re"};
    std::string            text {"prefix http://x"};
    text.push_back(static_cast<char>(0xD4));
    text.push_back(static_cast<char>(0x80));
    text += "y https://z.com ";
    EXPECT(spans_core(near, text) == spans_routed(near, text));
  }
  real::detail::rare_disc_route_disabled() = false;
}

// D1c-land (ii): per-trial random non-space code point X → near-\S form must decline
// and spans must equal core. Covers the "next U+0500" nobody enumerated.
TEST(rare_disc_s_plus_random_nons_space_cp_declines)
{
  using real::detail::is_space_cp;
  // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed)
  std::mt19937                                    rng {0xD1C4U};
  std::uniform_int_distribution<std::uint32_t>    cdist {0x80U, 0x10FFFFU};
  std::uniform_int_distribution<int>              coin {0, 1};
  auto                                            hex_x = [](std::uint32_t cp) {
                                                            char buf[16];
                                                            // \x{HEX} form (same as the compiler's \x{500} surface) — any plane.
                                                            static_cast<void>(std::snprintf(buf, sizeof buf, "%X",
                                                                                            static_cast<unsigned>(cp)));
                                                            return std::string(buf);
                                                          };

  for (int trial = 0; trial < 60; ++trial) {
    // Draw a non-space, non-surrogate code point.
    char32_t x {};
    for (int attempt = 0; attempt < 64; ++attempt) {
      x = static_cast<char32_t>(cdist(rng));
      if (x >= 0xD800U && x <= 0xDFFFU) {
        continue; // UTF-16 surrogates are not scalar values
      }
      if (!is_space_cp(x)) {
        break;
      }
    }
    EXPECT(!is_space_cp(x));
    // Pattern: https?://[^\s\x{X}]+  — near-\S by excluding one non-space CP.
    // Non-raw concatenation: R"(...\x{)" would terminate the raw string at ")".
    // Full class close: \x{HEX} then ] then +
    const std::string pat = std::string("https?://[^\\s\\x{") + hex_x(static_cast<std::uint32_t>(x))
                            + "}]+";
    const auto h {hints_of(pat)};
    EXPECT(!h.rare_disc_s_plus); // must DECLINE — not exact \S
    // Span parity (both paths fall to general when s_plus is off).
    std::string text {"http://body"};
    // Append X as UTF-8 then more body bytes — VM must stop at X if X is excluded.
    // (Even when X is accepted as non-excluded body, routed==core still holds.)
    {
      // Encode X to UTF-8 without depending on encode_utf8_bytes in the test TU.
      const std::uint32_t cp {static_cast<std::uint32_t>(x)};
      if (cp < 0x800U) {
        text.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
        text.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
      }
      else if (cp < 0x10000U) {
        text.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
        text.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
      }
      else {
        text.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
        text.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
      }
    }
    text += "tail ";
    if (coin(rng) != 0) {
      text += "https://other.com/p ";
    }
    EXPECT(spans_core(pat, text) == spans_routed(pat, text));
  }
  real::detail::rare_disc_route_disabled() = false;
}

TEST(rare_disc_s_plus_routed_equals_core)
{
  const std::string_view pat     {R"(https?://[^\s]+)"};
  const std::string_view cases[] {
    "http://x",
    "https://x",
    "see http://a.com and https://b.org/x end",
    "http:/",
    "shttp://x",
    ":",
    "http://",
    "https://",
    "prefixhttp://end",
    "::::http://ok",
    "http://x https://y",
    "",
    // Forced multi-byte whitespace frontiers (would kill ASCII-only memchr).
    "http://a.com/p\xC2\xA0yyyy",                 // U+00A0
    "https://b.com/q\xE2\x80\xA8zzzz",            // U+2028
    "x http://a.com/p\xC2\xA0y https://b.com/q\xE2\x80\xA8z http://c.com/r ",
  };
  for (const auto text : cases) {
    EXPECT(spans_core(pat, text) == spans_routed(pat, text));
  }
  real::detail::rare_disc_route_disabled() = false;
}

TEST(rare_disc_s_plus_unicode_ws_terminates_url)
{
  const real::regex re {R"(https?://[^\s]+)"};
  {
    const std::string s {"http://x.com/a\xC2\xA0yyyy"};
    const auto        m {re.search(s)};
    EXPECT(m.matched());
    EXPECT_EQ(m[0], std::string_view("http://x.com/a"));
  }
  {
    const std::string s {"https://z.com/p\xE2\x80\xA8tail"};
    const auto        m {re.search(s)};
    EXPECT(m.matched());
    EXPECT_EQ(m[0], std::string_view("https://z.com/p"));
  }
}

TEST(rare_disc_s_plus_randomized_vs_core)
{
  // Randomized haystacks with forced Unicode space adjacencies + random bytes.
  // Routed (dedicated) == core (rare_disc off → general VM).
  const std::string_view             pat   {R"(https?://[^\s]+)"};
  // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed)
  std::mt19937                       rng   {0xD1B5U};
  std::uniform_int_distribution<int> coin  {0, 1};
  std::uniform_int_distribution<int> bdist {0, 255};

  for (int trial = 0; trial < 80; ++trial) {
    std::string text;
    text.reserve(4000);
    for (int i = 0; i < 40; ++i) {
      text += "filler words ";
      if (coin(rng) != 0) {
        text += (coin(rng) != 0) ? "http://ex.com/p" : "https://api.io/v";
        text += std::to_string(i);
        // Adjacent multi-byte whitespace (forced frontier).
        if (coin(rng) != 0) {
          text += "\xC2\xA0";     // U+00A0
        }
        else {
          text += "\xE2\x80\xA8"; // U+2028
        }
      }
      // Noise bytes (may include ':' and '/').
      for (int k = 0; k < 8; ++k) {
        text.push_back(static_cast<char>(bdist(rng)));
      }
    }
    EXPECT(spans_core(pat, text) == spans_routed(pat, text));
  }
  real::detail::rare_disc_route_disabled() = false;
}

TEST(rare_disc_s_plus_find_iter_equals_count_matches)
{
  const real::regex re {R"(https?://[^\s]+)"};
  std::string       dense;
  for (int i = 0; i < 500; ++i) {
    dense += "see http://a.b/c and https://x.y/z/w ";
  }
  std::size_t n_fi {0};
  for (const auto& m : re.find_iter(dense)) {
    (void)m;
    ++n_fi;
  }
  EXPECT_EQ(n_fi, re.count_matches(dense));
}
