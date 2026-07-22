// Measures fused which_matched (+ optional first-byte skip) vs pure N-walks vs RE2::Set.
// Not a CI gate. Build:
//   c++ -O2 -Iinclude [-DHAVE_RE2 $(pkg-config --cflags --libs re2)] benchmarks/s2a_measure.cpp
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

static std::string corpus_dense(std::size_t bytes)
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

// Sparse-realistic: generic English-ish text, rare log-like hits (skip's happy path).
static std::string corpus_sparse(std::size_t bytes)
{
  static const char* pad =
    "the quick brown fox jumps over the lazy dog and then runs through the field again. ";
  static const char* hit =
    "error id=a3f9c1d8 GET user=bob q=42\n";
  std::string s;
  s.reserve(bytes);
  std::size_t n {0};
  while (s.size() + 200 < bytes) {
    s += pad;
    ++n;
    if ((n % 40) == 0) {
      s += hit; // occasional real hits so sets are non-empty
    }
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

static void run_regime(const char* name, const std::string& text)
{
  const double MB {text.size() / 1e6};
  const auto   P  {present()};
  std::printf("\n=== %s | corpus %.2f MB | best-of-7 MB/s | same-host ===\n", name, MB);
  std::printf("engines: fused+skip | fused-no-skip | pure N-walks | RE2::Set%s\n",
#if defined(HAVE_RE2)
              " (on)"
#else
              " (off)"
#endif
  );
  std::printf("%4s | %8s | %10s | %10s | %10s | %10s | correct?\n", "N", "states", "fused+skip",
              "fused-nosk", "Nwalks", "RE2::Set");

  for (const int N : {32, 64, 128, 256}) {
    std::vector<std::string> pats {P};
    for (int i = static_cast<int>(P.size()); i < N; ++i) {
      pats.push_back("SEV" + std::to_string(i) + "|tr" + std::to_string(i) + "x");
    }
    std::vector<real::regex> rx;
    rx.reserve(pats.size());
    for (const auto& p : pats) {
      rx.emplace_back(p);
    }
    real::dfa fused {std::span<const real::regex>(rx), real::dfa_mode::which_matched};

    auto pure_nwalks = [&] {
      std::size_t c {0};
      for (const auto& re : rx) {
        if (re.search(text)) {
          ++c;
        }
      }
      return c;
    };

    const auto f_skip   {fused.which_matched(text, true)};
    const auto f_noskip {fused.which_matched(text, false)};
    bool       ok_skip  {f_skip.size() == f_noskip.size()};
    for (std::size_t i = 0; ok_skip && i < f_skip.size(); ++i) {
      ok_skip = f_skip[i] == f_noskip[i];
    }
    std::size_t nwalk_n {0};
    {
      std::size_t c {0};
      for (std::size_t i = 0; i < rx.size(); ++i) {
        const bool h {static_cast<bool>(rx[i].search(text))};
        if (h != f_skip[i]) {
          nwalk_n = static_cast<std::size_t>(-1);
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
    const bool ok_real {ok_skip && nwalk_n != static_cast<std::size_t>(-1)};

    volatile std::size_t sink {0};
    const double         t_skip {best_ms(
      [&] {
        sink += fused.which_matched(text, true).size();
      },
      7)};
    const double t_nosk {best_ms(
      [&] {
        sink += fused.which_matched(text, false).size();
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
    std::size_t f_n {0};
    for (bool b : f_skip) {
      if (b) {
        ++f_n;
      }
    }
    const bool ok {ok_real && f_n == re2_n};
    std::printf("%4d | %8zu | %8.0f MB/s | %8.0f MB/s | %8.0f MB/s | %8.0f MB/s | %s (set %zu/%zu skip=%d)\n",
                N, fused.state_count(), MB / (t_skip / 1e3), MB / (t_nosk / 1e3), MB / (tn / 1e3),
                MB / (tr / 1e3), ok ? "OK" : "**MISMATCH**", f_n, re2_n,
                static_cast<int>(fused.has_first_byte_skip()));
#else
    std::printf("%4d | %8zu | %8.0f MB/s | %8.0f MB/s | %8.0f MB/s | %8s | %s (skip=%d)\n", N,
                fused.state_count(), MB / (t_skip / 1e3), MB / (t_nosk / 1e3), MB / (tn / 1e3), "n/a",
                ok_real ? "OK" : "**MISMATCH**", static_cast<int>(fused.has_first_byte_skip()));
#endif
    (void) sink;
  }
}

int main()
{
  std::printf("fused which_matched | first-byte skip vs no-skip | same-host\n");
  run_regime("DENSE log-like", corpus_dense(1u << 20));
  run_regime("SPARSE realistic", corpus_sparse(1u << 20));
  return 0;
}
