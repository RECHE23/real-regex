/*!
 * \file matrix4d.cpp
 * \brief The 4-D veto matrix — a committed gate, not an ad-hoc measurement.
 *
 * Repeated inner-literal routing fixes each came from measuring one slice of a four-dimensional space and
 * missing another. This screens the whole space: pattern x size x match/no-match x density. For every cell it
 * times the inner-literal route ON (production) against the core (route OFF, the pre-IL baseline) and asserts
 * two mechanical invariants:
 *
 *   1. NO REGRESSION: route-on <= core * TOLERANCE everywhere (the route never makes a case slower).
 *   2. NO-MATCH NEVER GATED: on a no-match haystack the route is memmem-only and must be far below the core
 *      (route-on <= core * NOMATCH_WIN) — the catastrophic-regression class that a size guard, placed wrong,
 *      reintroduces.
 *
 * A red cell sets a non-zero exit code, so this gates every future hot-path arc. `--short` runs a fast subset
 * for `full-local-gate`; no flag runs the full matrix (manual, for an arc's veto). It measures production
 * behaviour: the small-haystack guard stays ON (the point is that it never regresses or gates no-match).
 */
#include <real/real.hpp>
#include <real/automata/lazy_dfa.hpp> // inner_literal_route_disabled

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

  using clk = std::chrono::steady_clock;

  constexpr double tolerance   {1.05}; //!< route-on may be up to 5% over core (noise floor) and still pass.
  constexpr double nomatch_win {0.50}; //!< on no-match the route must be at least 2x the core (memmem-only).

  //! One find_iter sweep, ns per byte, best of three (noise floor, not a distribution — a gate, not a report).
  double ns_per_byte(const real::regex& re,
                     std::string_view   text,
                     int                iters)
  {
    double best {1e30};
    for (int rep = 0; rep < 3; ++rep) {
      std::size_t  sink {0};
      const auto   t0   {clk::now()};
      for (int i = 0; i < iters; ++i) {
        for (const auto& m : re.find_iter(text)) {
          sink += m.start();
        }
      }
      const auto            t1   {clk::now()};
      const volatile size_t keep {sink};
      static_cast<void>(keep);
      const double per           {std::chrono::duration<double, std::nano>(t1 - t0).count() / (double(iters) * double(text.size()))};
      best = std::min(best, per);
    }
    return best;
  }

  //! Grow \p unit to at least \p kb kibibytes — a versioned corpus, reproducible across runs.
  std::string corpus(std::string_view unit,
                     std::size_t      kb)
  {
    std::string text;
    text.reserve(kb * 1024 + unit.size());
    while (text.size() < kb * 1024) {
      text += unit;
    }
    return text;
  }

  //! Versioned corpus fragment shared by the sparse and no-match rows.
  const std::string words {"plain english words running along with nothing special to see here today ok fine "};

  int run(bool short_mode)
  {
    std::string sparse_email {};
    std::string sparse_date  {};
    for (int i = 0; i < 9; ++i) {
      sparse_email += words;
      sparse_date  += words;
    }
    sparse_email += "reach jane@corp.io ";
    sparse_date  += "on 2026-07-04 ";

    const real::regex email   {R"((\w+)@(\w+))"};
    const real::regex date    {R"(\d{4}-\d{2}-\d{2})"};
    const real::regex literal {R"(dog)"};    // no inner literal: sentinel, route never fires (IL == core)
    const real::regex klass   {R"([a-z]+)"}; // class loop: sentinel
    struct row
    {
      const char*        name;
      const real::regex* re;
      std::string        unit;
      bool               nomatch;
      bool               routes; //!< the pattern has an inner literal, so the IL route fires (email, date). A
                                 //!< sentinel (literal, class) never routes: route == core is expected, not gated.
    };

    std::vector<row> rows {
      {"email dense  ", &email, "reach jane@corp.io here ", false, true},
      {"email sparse ", &email, sparse_email, false, true},
      {"email nomatch", &email, words, true, true},
      {"date  dense  ", &date, "date 2026-07-04 x ", false, true},
      {"date  sparse ", &date, sparse_date, false, true},
      {"date  nomatch", &date, words, true, true},
    };
    if (!short_mode) {
      rows.push_back({"email 5word  ", &email, "one two jane@corp.io four five ", false, true});
      rows.push_back({"date  5word  ", &date, "one two 2026-07-04 four five ", false, true});
      rows.push_back({"literal hit  ", &literal, "the lazy dog runs ", false, false});
      rows.push_back({"literal none ", &literal, "the lazy cat runs ", true, false});
      rows.push_back({"class  hit   ", &klass, "the lazy words here ", false, false});
    }

    const std::vector<std::size_t> sizes {short_mode ? std::vector<std::size_t> {64}
                                                     : std::vector<std::size_t> {16, 64, 256, 2048}};

    std::printf("%-14s %6s %10s %10s  %s\n", "case", "KB", "route", "core", "verdict");
    int reds {0};
    for (const row& r : rows) {
      for (const std::size_t kb : sizes) {
        const std::string text  {corpus(r.unit, kb)};
        const int         iters {kb <= 64 ? 1500 : (kb <= 256 ? 250 : 40)};

        real::detail::inner_literal_route_disabled() = false;
        const double route {ns_per_byte(*r.re, text, iters)};
        real::detail::inner_literal_route_disabled() = true;
        const double core  {ns_per_byte(*r.re, text, iters)};
        real::detail::inner_literal_route_disabled() = false;

        const bool  regressed {route > core * tolerance};
        const bool  gated     {r.routes && r.nomatch && route > core * nomatch_win}; // a routing no-match must be memmem-only
        const char* verdict   {regressed ? "*** REGRESSION ***" : (gated ? "*** NO-MATCH GATED ***" : "ok")};
        if (regressed || gated) {
          ++reds;
        }
        std::printf("%-14s %6zu %10.3f %10.3f  %s\n", r.name, kb, route, core, verdict);
      }
    }
    std::printf("\n%s (%d red cell%s)\n", reds == 0 ? "MATRIX CLEAN" : "MATRIX HAS REGRESSIONS", reds, reds == 1 ? "" : "s");
    return reds == 0 ? 0 : 1;
  }
} // namespace

int main(int    argc,
         char** argv)
{
  const bool short_mode {argc > 1 && std::strcmp(argv[1], "--short") == 0};
  return run(short_mode);
}
