// S2c measure: fused which_matched vs pure N-walks vs RE2::Set (same host/harness).
// Not a CI gate. Build: c++ -O2 -Iinclude [-DHAVE_RE2 $(pkg-config --cflags --libs re2)] s2a_measure.cpp
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "real/dfa.hpp"
#include "real/real.hpp"

#if defined(HAVE_RE2)
#  include <re2/re2.h>
#  include <re2/set.h>
#endif

static std::string corpus(std::size_t bytes)
{
  static const char* u =
    "2026-06-13 12:04:55 error id=a3f9c1d8 GET /api/x 200 from 10.0.2.15 user=bob q=42\n"
    "2026-06-14 09:11:02 info id=deadbeef POST /login 302 from 192.168.1.7 user=alice n=7\n";
  std::string s;
  while (s.size() < bytes) {
    s += u;
  }
  s.resize(bytes);
  return s;
}

static std::vector<std::string> present()
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

template <typename F>
static double best_ms(F&& f, int r)
{
  double b {1e30};
  for (int i = 0; i < r; ++i) {
    const auto t0 {std::chrono::steady_clock::now()};
    f();
    const double m {
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()};
    if (m < b) {
      b = m;
    }
  }
  return b;
}

int main()
{
  const std::string text {corpus(1u << 20)};
  const double      MB   {text.size() / 1e6};
  const auto        P    {present()};
  std::printf("S2c measure | corpus %.2f MB | best-of-7 MB/s | same-host\n", MB);
  std::printf("engines: fused which_matched | pure N-walks search | RE2::Set%s\n\n",
#if defined(HAVE_RE2)
              " (on)"
#else
              " (off)"
#endif
  );
  std::printf("%4s | %8s | %12s | %12s | %12s | correct?\n", "N", "states", "fused", "Nwalks",
              "RE2::Set");
  // Regime where fused path is used (eligible ≥ 56); also N=32 for reference.
  for (const int N : {32, 64, 128, 256}) {
    std::vector<std::string> pats {P};
    for (int i = static_cast<int>(P.size()); i < N; ++i) {
      pats.push_back("SEV" + std::to_string(i) + "|tr" + std::to_string(i) + "x");
    }
    std::vector<real::regex> rx;
    for (const auto& p : pats) {
      rx.emplace_back(p);
    }
    real::dfa fused {std::span<const real::regex>(rx), real::dfa_mode::which_matched};

    // Pure N-walks (not regex_set — that would take the fused path for large N).
    auto pure_nwalks = [&] {
      std::size_t c {0};
      for (const auto& re : rx) {
        if (re.search(text)) {
          ++c;
        }
      }
      return c;
    };

    const auto fhit = fused.which_matched(text);
    std::size_t nwalk_n {0};
    {
      std::size_t c {0};
      for (std::size_t i = 0; i < rx.size(); ++i) {
        const bool h {static_cast<bool>(rx[i].search(text))};
        if (h != fhit[i]) {
          nwalk_n = static_cast<std::size_t>(-1); // mismatch sentinel
          break;
        }
        if (h) {
          ++c;
        }
      }
      if (nwalk_n != static_cast<std::size_t>(-1)) {
        nwalk_n = c;
      }
    }
    const bool ok_real {nwalk_n != static_cast<std::size_t>(-1)};

    volatile std::size_t sink {0};
    const double         tf {best_ms(
      [&] {
        sink += fused.which_matched(text).size();
      },
      7)};
    const double tn {best_ms(
      [&] {
        sink += pure_nwalks();
      },
      7)};

#if defined(HAVE_RE2)
    RE2::Set rs(RE2::Options(), RE2::UNANCHORED);
    for (const auto& p : pats) {
      std::string err;
      if (rs.Add(p, &err) < 0) {
        std::printf("%4d RE2 Add fail: %s\n", N, err.c_str());
        continue;
      }
    }
    if (!rs.Compile()) {
      std::printf("%4d RE2 Compile fail\n", N);
      continue;
    }
    std::size_t  re2_n {0};
    const double tr {best_ms(
      [&] {
        std::vector<int> m;
        rs.Match(text, &m);
        re2_n = m.size();
      },
      7)};
    // Count fused hits for set-size compare.
    std::size_t f_n {0};
    for (bool b : fhit) {
      if (b) {
        ++f_n;
      }
    }
    const bool ok {ok_real && f_n == re2_n};
    std::printf("%4d | %8zu | %8.0f MB/s | %8.0f MB/s | %8.0f MB/s | %s (set %zu/%zu)\n", N,
                fused.state_count(), MB / (tf / 1e3), MB / (tn / 1e3), MB / (tr / 1e3),
                ok ? "OK" : "**MISMATCH**", f_n, re2_n);
#else
    std::printf("%4d | %8zu | %8.0f MB/s | %8.0f MB/s | %8s | %s\n", N, fused.state_count(),
                MB / (tf / 1e3), MB / (tn / 1e3), "n/a", ok_real ? "OK" : "**MISMATCH**");
#endif
    (void) sink;
  }
  return 0;
}
