// S2a measure-only: unanchored multi-accept which_matched vs N-walks Stage-1.
// Not a CI gate — report states + MB/s for the Stage-2 go/no-go.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "real/dfa.hpp"
#include "real/real.hpp"
#include "real/regex_set.hpp"

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
  std::printf("S2a measure | corpus %.2f MB | best-of-7 MB/s\n", MB);
  std::printf("%4s | %8s | %12s | %12s | correct?\n", "N", "states", "fused", "Nwalks");
  for (const int N : {8, 16, 32, 64, 128}) {
    std::vector<std::string> pats {P};
    for (int i = static_cast<int>(P.size()); i < N; ++i) {
      pats.push_back("SEV" + std::to_string(i) + "|tr" + std::to_string(i) + "x");
    }
    std::vector<real::regex> rx;
    for (const auto& p : pats) {
      rx.emplace_back(p);
    }
    real::dfa fused {std::span<const real::regex>(rx), real::dfa_mode::which_matched};
    std::vector<std::string_view> views;
    for (const auto& p : pats) {
      views.emplace_back(p);
    }
    real::regex_set nwalk {views};

    const auto fhit = fused.which_matched(text);
    const auto nhit = nwalk.matches(text);
    bool       ok   {fhit.size() == nhit.size()};
    for (std::size_t i = 0; ok && i < fhit.size(); ++i) {
      ok = fhit[i] == nhit[i];
    }

    volatile std::size_t sink {0};
    const double         tf {best_ms(
      [&] {
        const auto h {fused.which_matched(text)};
        sink += h.size();
      },
      7)};
    const double tn {best_ms(
      [&] {
        const auto h {nwalk.matches(text)};
        sink += h.size();
      },
      7)};
    (void) sink;
    std::printf("%4d | %8zu | %8.0f MB/s | %8.0f MB/s | %s\n", N, fused.state_count(),
                MB / (tf / 1e3), MB / (tn / 1e3), ok ? "OK" : "**MISMATCH**");
  }
  return 0;
}
