// Heterogeneous fixed-shape pair-filter route: routed == unrouted, everywhere.
//
// The route (pike.hpp's run_pair_filtered_shape) only *filters* candidate starts — two positions tested
// per vector compare — and hands every survivor to the same match_fixed_body_wb walk run_fixed_shape
// uses. So it is transparent by contract, and the seam
// (real::detail::fixed_shape_pair_route_disabled) is what turns that contract into a differential: for
// every (pattern, subject) below, the routed answer must equal the unrouted one, span for span, in every
// run mode.
//
// A vector block scan goes wrong at boundaries, so the subjects are built to cross them: matches at
// offset 0, flush against the end, at every offset around the 16-byte seam and past 64, plus subjects
// shorter than one block and shorter than the shape itself. Both ARMED and VETOED shapes are included —
// the vetoed ones prove the veto changes routing without changing answers.
#include <sciforge/test/framework.hpp>

#include <real/real.hpp>
#include <real/automata/lazy_dfa.hpp>

#include <string>
#include <string_view>
#include <vector>

using real::detail::fixed_shape_pair_route_disabled;

namespace {

  struct observation
  {
    std::vector<std::pair<std::size_t, std::size_t>> spans;       //!< every non-overlapping match
    bool                                             overran  {}; //!< the walk hit the cap (never expected)
    bool                                             searched {};
    std::size_t                                      s_start  {real::npos};
    std::size_t                                      s_end    {real::npos};
    bool                                             matched  {}; //!< re.match (anchored)
    bool                                             full     {}; //!< re.fullmatch
  };

  bool operator==(const observation& a,
                  const observation& b)
  {
    return a.spans == b.spans && a.overran == b.overran && a.searched == b.searched && a.s_start == b.s_start
           && a.s_end == b.s_end && a.matched == b.matched && a.full == b.full;
  }

  //! \brief Every observable of \p pat against \p text, with the pair route forced on or off.
  observation observe(const char*      pat,
                      std::string_view text,
                      bool             route_off)
  {
    fixed_shape_pair_route_disabled() = route_off;
    const real::regex re {pat};
    observation       obs;
    for (const auto& m : re.find_iter(text)) {
      obs.spans.emplace_back(m.start(0), m.end(0));
      if (obs.spans.size() > 400U) {
        obs.overran = true; // a walk that stopped advancing looks exactly like this
        break;
      }
    }
    const auto s {re.search(text)};
    obs.searched = s.matched();
    if (s.matched()) {
      obs.s_start = s.start(0);
      obs.s_end   = s.end(0);
    }
    obs.matched                       = re.match(text).matched();
    obs.full                          = re.fullmatch(text).matched();
    fixed_shape_pair_route_disabled() = false;
    return obs;
  }

  //! \brief Patterns whose shape ARMS the route (heterogeneous, no memchr-able single byte) and
  //!        patterns the compiler VETOES (a rare byte or a unique first byte, so the ordinary
  //!        memchr prefilter is better) — both must answer identically either way.
  const std::vector<const char*>& patterns()
  {
    static const std::vector<const char*> pats {
      // armed: icase literals of several widths (each position a fold pair)
      "(?i)ab", "(?i)the", "(?i)cafe", "(?i)hello", "(?i)abcdefgh", "(?i)abcdefghijklmnop",
      // armed: mixed class shapes
      "[ab][cd]", "[a-c][x-z][0-9]", "[ab][c-e][fg]",
      // armed and \b-wrapped (the verify applies the boundary, the filter must not change it)
      R"((?i)\bthe\b)", R"((?i)cafe\b)",
      // vetoed: a required rare byte / a unique first byte -> ordinary prefilter
      "[0-9]{2}:[0-9]{2}", "ab[0-9]cd", "x[0-9]",
      // CAPTURING fixed shapes -- the class that caught this route's one real bug. The shape qualifies
      // (heterogeneous, no memchr-able byte) but the route writes only slots [0,1], so the dispatch must
      // decline on slot_count: `([ab])(a)` otherwise reported a match with a 2-slot buffer where the
      // program wants 6, and find_iter then never advanced. That runaway was found by exhaustive-compat's
      // 3.2M cases, NOT by this file's original hand-picked list -- which is why the class is here now.
      "([ab])(a)", "[ab]([ab]a)", "([ab])a", "(?i)(ca)(fe)", "([a-c])([x-z])([0-9])",
      "(?i)c(af)e", "([ab])([cd])",
      // not applicable: homogeneous, or one position wide
      "[0-9a-f]{8}", "(?i)zzz", "(?i)a",
    };
    return pats;
  }

  //! \brief Subjects that put a match at offset 0, flush at the end, and at every offset around the
  //!        16-byte block seam and past 64 — plus sub-block and sub-shape lengths.
  std::vector<std::string> subjects()
  {
    std::vector<std::string> out {
      "", "a", "ab", "AB", "aB", "the", "THE", "cafe", "CAFE", "CaFe",
      "xx", "0:0", "12:34", "deadbeef", "zzz", "ZZZ", "ac", "bd", "az9",
    };
    const std::string_view filler {"qwrtyupsdfghjklvnm"}; // no letter of the needles above
    for (std::size_t pad = 0; pad <= 70; ++pad) {
      const std::string lead(pad, 'q');
      out.push_back(lead + "CaFe");                        // flush against the end
      out.push_back(lead + "CaFe" + std::string(20, 'q')); // interior, every seam offset
      out.push_back("Ab" + lead);                          // at offset 0
      out.push_back(lead + "12:34" + std::string(9, 'q')); // a vetoed shape, same offsets
    }
    for (std::size_t n : {15U, 16U, 17U, 31U, 32U, 33U, 63U, 64U, 65U, 129U}) {
      out.emplace_back(n, 'A');                            // all-lead-byte: max verify pressure
      out.push_back(std::string(n, 'a') + "B");            // hit only in the last window
      out.push_back(std::string(n / 2, 'q') + "the" + std::string(n / 2, 'q'));
      std::string repeated;
      while (repeated.size() < n) {
        repeated += "CaFe";
        repeated += filler.substr(0, 3);
      }
      out.push_back(repeated);                                     // dense matches across many blocks
    }
    return out;
  }
} // namespace

TEST(fixed_shape_pair_route_agrees_with_the_scalar_walk)
{
  const std::vector<std::string> texts    {subjects()};
  std::size_t                    compared {0};
  for (const char* pat : patterns()) {
    for (const std::string& text : texts) {
      const observation routed {observe(pat, text, /*route_off=*/ false)};
      const observation walked {observe(pat, text, /*route_off=*/ true)};
      EXPECT(routed == walked);
      // 400 matches on subjects this short means the walk stopped advancing — the runaway signature.
      EXPECT(!routed.overran);
      ++compared;
    }
  }
  // The cross product actually ran (a silently empty loop would "pass").
  EXPECT(compared > 4000U);
  // The seam is left off, so no later test inherits a disabled route.
  EXPECT(!fixed_shape_pair_route_disabled());
}

TEST(fixed_shape_pair_route_arms_only_where_intended)
{
  const auto width = [](const char* pat) {
                       const real::regex re {pat};
                       return static_cast<unsigned>(re.raw_program().hints.fs_pair_width);
                     };
  // Armed: heterogeneous, >= 2 positions, and no single byte the ordinary prefilter could memchr.
  EXPECT(width("(?i)ab") == 2U);
  EXPECT(width("(?i)cafe") == 4U);
  EXPECT(width("[ab][cd]") == 2U);
  EXPECT(width("[a-c][x-z][0-9]") == 3U);

  // Vetoed because a single byte IS memchr-able — the measured reason: `[0-9]{2}:[0-9]{2}` carries
  // rare_byte ':' and the platform memchr (AVX2 on x86-64) beats a 128-bit block filter there.
  EXPECT(width("[0-9]{2}:[0-9]{2}") == 0U);
  EXPECT(width("ab[0-9]cd") == 0U);
  EXPECT(width("x[0-9]") == 0U);

  // A CAPTURING shape can qualify by SHAPE and still must never take the route — it needs its group
  // slots filled (run_fixed_shape's fill_fixed_saves path) and this route writes only [0,1]. The hint
  // stays armed; the veto is the dispatch's `slot_count <= 2` test. What pins the behaviour is this pair
  // of facts together: the hint arms here, and the differential above proves the answers are identical
  // either way (they would not be if the dispatch let these through — that was the runaway).
  EXPECT(width("([ab])(a)") >= 2U);
  EXPECT(width("(?i)(ca)(fe)") >= 2U);

  // Not applicable: the homogeneous fused scan owns these, and a 1-position shape has no pair.
  EXPECT(width("[0-9a-f]{8}") == 0U);
  EXPECT(width("(?i)zzz") == 0U);
  EXPECT(width("(?i)a") == 0U);
}

TEST(fixed_shape_pair_route_stays_linear_in_billed_work)
{
  // The deterministic work counter, not wall time — the same gate that caught the historical O(n^2)
  // icase cascade. The route bills its 16-candidate rounds; 4x the bytes must cost ~4x, not ~16x.
  const auto work = [](std::size_t n) {
                      std::string text;
                      text.reserve(n + 128);
                      while (text.size() < n) {
                        text += "qwrtyupsdfghjklvnm qwrtyupsdfghjklvnm ";
                        text += "CaFe ";
                      }
                      const real::regex re {"(?i)cafe"};
                      real::detail::prefilter_work_units() = 0;
                      std::size_t matches  {0};
                      for (const auto& m : re.find_iter(text)) {
                        (void) m;
                        ++matches;
                      }
                      EXPECT(matches > 0U);
                      return real::detail::prefilter_work_units();
                    };
  (void) work(1U << 12);                      // warmup, discarded
  const std::uint64_t small {work(1U << 18)}; // 256 KiB
  const std::uint64_t large {work(1U << 20)}; // 1 MiB — 4x the bytes
  EXPECT(small > 0U);                         // a route that bills NOTHING is invisible to this gate
  EXPECT(large < small * 8U);                 // O(n) -> ~4x; O(n^2) -> ~16x
  EXPECT_EQ(work(1U << 20), large);           // determinism pin (work count, not wall time)
}
