// A MINIMAL translation unit: `real/real.hpp` and nothing else. Emits the same JSON shape
// bench_engines.cpp does, so benchmarks/bench_layout.py can judge a change against it unchanged.
//
// WHY THIS EXISTS, and it is a correction to how this project had been judging changes. The layout
// A/B ran against bench_engines.cpp, which includes <regex>, PCRE2 and RE2 alongside real.hpp and
// sits on the compiler's per-unit inlining budget (docs/design.dox §10.1). Three changes were refused
// during one session because they cost the code-point class rows 5 to 9 % THERE -- but a downstream
// consumer includes only real.hpp, so that budget pressure may be a property of the measuring
// harness rather than of the library. `--real-only` does not settle it: dropping PCRE2 and RE2 removes
// just 4.3 % of the binary, because <regex> is header-only and compiled either way.
//
// So this unit exists to answer one question: does a change that costs a row in the four-engine
// harness still cost it in a unit shaped like a real consumer's? The two instruments answer different
// questions and both are legitimate -- the harness governs the numbers published in
// docs/BENCHMARKS.md, this unit governs whether a change helps the people who use the library.
//
// Deliberately no competitor engines, no scaling sweep, no ReDoS section: every one of those would
// put back the code whose absence is the point.

#include <real/real.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef BENCH_LAYOUT_PAD
#  define BENCH_LAYOUT_PAD 0
#endif
#if BENCH_LAYOUT_PAD > 0
namespace {
  volatile int layout_sink {0}; //!< Keeps the pad bodies from folding away.

  //! One pad body — the layout draw knob, identical in intent to bench_engines.cpp's.
  template <int I>
#  if defined(__GNUC__) || defined(__clang__)
  __attribute__((noinline, used, cold))
#  endif
  void layout_pad()
  {
    layout_sink += I;
  }

  //! Instantiates \p N pad bodies. Never called.
  template <int N>
  void layout_pads()
  {
    layout_pad<N>();
    if constexpr (N > 0) {
      layout_pads<N - 1>();
    }
  }

  [[maybe_unused]] void (*const layout_anchor)() {&layout_pads<BENCH_LAYOUT_PAD>};
} // namespace
#endif

namespace {

  std::string repeat_to(const char* unit,
                        std::size_t target)
  {
    std::string s;
    while (s.size() < target) {
      s += unit;
    }
    s.resize(target);
    return s;
  }

  //! One case: the pattern, a corpus name for the report, and the corpus itself.
  struct probe_case
  {
    const char*  name;
    const char*  pattern;
    std::string  corpus;
  };

  //! Discards a warm-up scan, then returns \p n per-scan times in nanoseconds.
  std::vector<double> collect(const real::regex& re,
                              const std::string& text,
                              int                n)
  {
    (void) re.count_matches(text);
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      const auto t0 {std::chrono::steady_clock::now()};
      const auto hits {re.count_matches(text)};
      const auto t1 {std::chrono::steady_clock::now()};
      out.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
      if (hits == 0) {
        std::fprintf(stderr, "no match: corpus does not exercise this case\n");
        std::exit(2);
      }
    }
    return out;
  }

  //! JSON-escapes a case name. Needed rather than cosmetic: the names carry `\w`, `\p{L}` and friends,
  //! and a lone backslash is an INVALID JSON escape -- the consumer rejected the whole document.
  std::string json_name(const char* s)
  {
    std::string out;
    for (const char* c = s; *c != '\0'; ++c) {
      if (*c == '\\' || *c == '"') {
        out += '\\';
      }
      out += *c;
    }
    return out;
  }

  std::string json_samples(const std::vector<double>& xs)
  {
    std::string s {"["};
    for (std::size_t i {0}; i < xs.size(); ++i) {
      char buf[64];
      std::snprintf(buf, sizeof buf, "%s%.6f", i ? "," : "", xs[i]);
      s += buf;
    }
    return s + "]";
  }
} // namespace

int main()
{
  const char* env {std::getenv("BENCH_SAMPLES")};
  const int   n   {env != nullptr && std::atoi(env) > 0 ? std::atoi(env) : 30};

  const std::string prose {repeat_to("the quick brown fox jumps over the lazy dog and then rests 42 ",
                                     100000)};
  const std::string mixed {repeat_to("alice@example.com 2024-03-17 caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87 "
                                     "deadbeefcafe a1b2, Fox! ", 100000)};
  const std::string csv {repeat_to("alpha,beta,gamma,delta,epsilon,zeta,charlie,eta,theta,iota,",
                                   100000)};

  // The rows a batching change is judged on: the ones it targets, and the ones it must not charge.
  const std::vector<probe_case> cases {
    {"words [a-z]+", "[a-z]+", prose},
    {"words [a-z]{4,}", "[a-z]{4,}", prose},
    {"words [a-z]++", "[a-z]++", prose},
    {"single [a-z]", "[a-z]", prose},
    {"digits [0-9]+", "[0-9]+", mixed},
    {"fields [^,]+", "[^,]+", csv},
    {"literal charlie", "charlie", csv},
    {"unicode \\w+", "\\w+", mixed},
    {"unicode \\w{2,}", "\\w{2,}", mixed},
    {"unicode \\w++", "\\w++", mixed},
    {"unicode \\b\\w+\\b", "\\b\\w+\\b", mixed},
    {"unicode \\p{L}+", "\\p{L}+", mixed},
    {"unicode \\p{L}{3,}", "\\p{L}{3,}", mixed},
    {"unicode \\p{N}+", "\\p{N}+", mixed},
    {"unicode .", ".", mixed},
  };

  std::string out {"{\"cases\":["};
  for (std::size_t i {0}; i < cases.size(); ++i) {
    const real::regex re {cases[i].pattern};
    const auto        samples {collect(re, cases[i].corpus, n)};
    char              head[256];
    std::snprintf(head, sizeof head, "%s{\"name\":\"%s\",\"corpus_bytes\":%zu,\"engines\":{\"real\":{",
                  i ? "," : "", json_name(cases[i].name).c_str(), cases[i].corpus.size());
    out += head;
    out += "\"samples\":" + json_samples(samples) + "}}}";
  }
  out += "]}";
  std::printf("%s\n", out.c_str());
  return 0;
}
