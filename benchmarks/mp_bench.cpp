/*!
 * \file mp_bench.cpp
 * \brief Multi-pattern throughput (informational — not a CI gate).
 *
 * Two tables, same-semantics, early-exit controlled:
 *
 *   A. FILTRE / IDS — which-matched: 8 present + (N−8) absent patterns force a full
 *      scan. Engines: REAL N-walks (search/pattern), optional RE2::Set, optional
 *      Hyperscan SINGLEMATCH. Assert set sizes equal when all three compile.
 *
 *   B. EXTRACTION — all non-overlapping matches on present patterns only.
 *      REAL count_matches N-walks vs optional RE2 FindAndConsume. Assert counts equal.
 *
 * Reproduce: `make bench-multipattern` (RE2/HS via pkg-config when present).
 * Not a pass/fail gate — absolute MB/s track the host; the durable content is the
 * shape (single-pass flat vs N-walks degrade) and the equal-set/count assertions.
 */
#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "real/real.hpp"
#include "real/regex_set.hpp"

#if defined(HAVE_RE2)
#  include <re2/re2.h>
#  include <re2/set.h>
#endif

#if defined(HAVE_HS)
#  include <hs/hs.h>
#endif

namespace {

  std::string corpus(std::size_t bytes)
  {
    static const char* unit =
      "2026-06-13 12:04:55 error id=a3f9c1d8 GET /api/x 200 from 10.0.2.15 user=bob key=val q=42\n"
      "2026-06-14 09:11:02 info id=deadbeef POST /login 302 from 192.168.1.7 user=alice n=7 debug\n";
    std::string s;
    s.reserve(bytes);
    while (s.size() < bytes) {
      s += unit;
    }
    s.resize(bytes);
    return s;
  }

  std::vector<std::string> present()
  {
    return {
      R"([0-9]{4}-[0-9]{2}-[0-9]{2})",
      R"([0-9]{2}:[0-9]{2}:[0-9]{2})",
      R"(error|warn|info|debug|fatal)",
      R"([a-f0-9]{8})",
      R"([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)",
      R"(GET|POST|PUT|DELETE)",
      R"(user=[a-z]+)",
      R"(q=[0-9]+)",
    };
  }

  std::string absent(int i)
  {
    return "SEVERITY_" + std::to_string(i) + "|traceId=[A-F0-9]{16}x" + std::to_string(i);
  }

  template <typename F>
  double best_ms(F&& f, int reps)
  {
    double best {1e30};
    for (int i = 0; i < reps; ++i) {
      const auto t0 {std::chrono::steady_clock::now()};
      f();
      const double ms {
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()};
      if (ms < best) {
        best = ms;
      }
    }
    return best;
  }

#if defined(HAVE_HS)
  std::set<unsigned> g_hs_ids;
  int                hs_cb(unsigned id, unsigned long long, unsigned long long, unsigned, void*)
  {
    g_hs_ids.insert(id);
    return 0;
  }
#endif

} // namespace

int main()
{
  const std::string text {corpus(1u << 20)};
  const double      MB   {static_cast<double>(text.size()) / 1e6};
  const auto        P    {present()};

  std::printf("corpus %.2f MB | MB/s (higher is better) | best-of-7\n", MB);
  std::printf("engines: REAL");
#if defined(HAVE_RE2)
  std::printf(" + RE2");
#endif
#if defined(HAVE_HS)
  std::printf(" + Hyperscan");
#endif
  std::printf("\n\n");

  std::printf("== TABLE A: FILTRE which-matched (%zu present + (N-%zu) absent → full scan) ==\n",
              P.size(), P.size());
  std::printf("%4s | %-11s | %-11s | %-11s | actives HS/RE2/REAL\n", "N", "HS", "RE2::Set",
              "REAL Nwalk");

  for (const int N : {16, 32, 64, 128}) {
    std::vector<std::string> pats {P};
    for (int i = static_cast<int>(P.size()); i < N; ++i) {
      pats.push_back(absent(i));
    }

    // REAL: regex_set which-matched (Stage-1 N-walks, per-pattern early-exit)
    std::vector<std::string_view> views;
    views.reserve(pats.size());
    for (const auto& p : pats) {
      views.emplace_back(p);
    }
    const real::regex_set set(views);
    std::size_t           real_n {0};
    const double t_real {best_ms(
      [&] {
        real_n = 0;
        for (bool b : set.matches(text)) {
          if (b) {
            ++real_n;
          }
        }
      },
      7)};

#if defined(HAVE_RE2)
    RE2::Set               rs(RE2::Options(), RE2::UNANCHORED);
    for (const auto& p : pats) {
      std::string err;
      if (rs.Add(p, &err) < 0) {
        std::printf("%4d RE2::Set Add fail: %s\n", N, err.c_str());
        continue;
      }
    }
    if (!rs.Compile()) {
      std::printf("%4d RE2::Set Compile fail\n", N);
      continue;
    }
    std::size_t  re2_n {0};
    const double t_re2 {best_ms(
      [&] {
        std::vector<int> m;
        rs.Match(text, &m);
        re2_n = m.size();
      },
      7)};
#else
    const double      t_re2 {0};
    const std::size_t re2_n {0};
#endif

#if defined(HAVE_HS)
    std::vector<const char*> cp;
    std::vector<unsigned>    ids, flags;
    for (int i = 0; i < N; ++i) {
      cp.push_back(pats[static_cast<std::size_t>(i)].c_str());
      ids.push_back(static_cast<unsigned>(i));
      flags.push_back(HS_FLAG_SINGLEMATCH | HS_FLAG_DOTALL);
    }
    hs_database_t*     db {nullptr};
    hs_compile_error_t* ce {nullptr};
    if (hs_compile_multi(cp.data(), flags.data(), ids.data(), static_cast<unsigned>(N), HS_MODE_BLOCK,
                         nullptr, &db, &ce)
        != HS_SUCCESS) {
      std::printf("%4d HS compile fail: %s\n", N, ce ? ce->message : "?");
      if (ce) {
        hs_free_compile_error(ce);
      }
      continue;
    }
    hs_scratch_t* sc {nullptr};
    hs_alloc_scratch(db, &sc);
    std::size_t  hs_n {0};
    const double t_hs {best_ms(
      [&] {
        g_hs_ids.clear();
        hs_scan(db, text.data(), static_cast<unsigned>(text.size()), 0, sc, hs_cb, nullptr);
        hs_n = g_hs_ids.size();
      },
      7)};
    hs_free_scratch(sc);
    hs_free_database(db);
#else
    const double      t_hs {0};
    const std::size_t hs_n {0};
#endif

    const char* ok {
#if defined(HAVE_RE2) && defined(HAVE_HS)
      (hs_n == re2_n && re2_n == real_n) ? "OK" : "**MISMATCH**"
#elif defined(HAVE_RE2)
      (re2_n == real_n) ? "OK" : "**MISMATCH**"
#else
      "OK"
#endif
    };

    std::printf("%4d | %6.0f MB/s | %6.0f MB/s | %6.0f MB/s | %zu/%zu/%zu %s\n", N,
                t_hs > 0 ? MB / (t_hs / 1e3) : 0.0, t_re2 > 0 ? MB / (t_re2 / 1e3) : 0.0,
                MB / (t_real / 1e3), hs_n, re2_n, real_n, ok);
  }

  std::printf("\n== TABLE B: EXTRACTION all-matches non-overlapping (present patterns only) ==\n");
  std::printf("%4s | %-11s | %-11s | counts REAL/RE2\n", "N", "REAL Nwalk", "RE2 Nwalk");
  for (const int N : {4, 8, static_cast<int>(P.size())}) {
    if (N > static_cast<int>(P.size())) {
      break;
    }
    std::vector<std::string> pats(P.begin(), P.begin() + N);
    std::vector<real::regex> rr;
    for (const auto& p : pats) {
      rr.emplace_back(p);
    }
    unsigned long rn {0};
    const double  t_real {best_ms(
      [&] {
        unsigned long c {0};
        for (const auto& re : rr) {
          c += re.count_matches(text);
        }
        rn = c;
      },
      7)};

#if defined(HAVE_RE2)
    std::vector<RE2*> rx;
    for (const auto& p : pats) {
      rx.push_back(new RE2(p));
    }
    unsigned long r2 {0};
    const double  t_re2 {best_ms(
      [&] {
        unsigned long c {0};
        for (auto* re : rx) {
          re2::StringPiece in(text);
          while (RE2::FindAndConsume(&in, *re)) {
            ++c;
          }
        }
        r2 = c;
      },
      7)};
    for (auto* re : rx) {
      delete re;
    }
    std::printf("%4d | %6.0f MB/s | %6.0f MB/s | %lu/%lu %s\n", N, MB / (t_real / 1e3),
                MB / (t_re2 / 1e3), rn, r2, rn == r2 ? "OK" : "**MISMATCH**");
#else
    std::printf("%4d | %6.0f MB/s | %6s | %lu/—\n", N, MB / (t_real / 1e3), "n/a", rn);
#endif
  }
  return 0;
}
