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

#include "bench_warmup.hpp"

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

  //! \brief Sink for every scan result, so no compiler can hoist a loop-invariant scan out of the timed
  //!        loop or elide it as dead. It is not decoration: the per-call rows read 0.0 ns under g++ before
  //!        this existed -- same regex, same subject each iteration, so g++ hoisted the whole call and the
  //!        readings measured nothing while clang's (which did not hoist) looked fine. A benchmark that one
  //!        compiler optimises away reports a 0 that is indistinguishable from "infinitely fast".
  volatile std::size_t scan_sink {0};

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

  //! Which API a row measures. Rows measured through `count_matches` cannot see a route that only
  //! `find_iter` misses -- which is how a 12x gap on `[a-z]+(?=[a-z])` survived: three of four entry
  //! points took the trailing-lookaround route and the iterator did not, while every row here measured
  //! one of the three that did.
  enum class surface : std::uint8_t
  {
    count,     //!< `count_matches` -- the throughput surface every row used before.
    find_iter, //!< the lazy iterator, which yields full results and selects routes on its own.
    search,    //!< one `search` per call: the per-CALL regime, where a fixed cost is not amortised.
    match,     //!< one `match` per call: validation, where the answer is often NO.
    replace,   //!< one `replace` per call: the operation a trim/normalise pass actually performs.
  };

  //! One case: the pattern, the corpus, which API measures it, and whether a match is expected.
  //!
  //! `expect_hit` exists for the REJECT rows. A validation call that correctly answers NO is a regime in
  //! its own right -- and the one where `std::regex` is furthest ahead, because a backtracker rejects on
  //! the first byte while this engine pays its walk. Refusing to measure a zero-match case (which this
  //! harness did) means never seeing that.
  struct probe_case
  {
    const char* name;
    const char* pattern;
    std::string corpus;
    surface     api {surface::count};
    bool        expect_hit {true};
  };

  //! Discards a warm-up scan, then returns \p n per-scan times in nanoseconds.
  std::vector<double> collect(const real::regex& re,
                              const std::string& text,
                              int                n,
                              surface            api,
                              bool               expect_hit)
  {
    const auto scan = [&re, &text](surface api) -> std::size_t {
      switch (api) {
        case surface::count:
          return re.count_matches(text);
        case surface::search:
          return static_cast<bool>(re.search(text)) ? 1U : 0U;
        case surface::match:
          return static_cast<bool>(re.match(text)) ? 1U : 0U;
        case surface::replace:
          return re.replace(text, "X").size();
        default: {
          std::size_t hits {0};
          for (const auto& m : re.find_iter(text)) {
            (void) m;
            ++hits;
          }
          return hits;
        }
      }
    };
    (void) scan(api);
    // BATCH THE TIMED REGION, calibrated per case. A per-call row measures ~40 ns, and reading the clock
    // around a single call of that length measures the CLOCK: the per-call rows read 0.0 ns under one
    // compiler and 42 under another, from nothing but where each landed relative to the tick, and the
    // minimum of such samples is 0. Growing the batch until it spans ~50 us puts the reading three orders
    // of magnitude above the granularity; a throughput row over 100 KB already exceeds that at one
    // iteration, so it keeps `inner == 1` and its numbers are unchanged.
    int inner {1};
    for (; inner < (1 << 20); inner *= 2) {
      const auto c0 {std::chrono::steady_clock::now()};
      for (int k = 0; k < inner; ++k) {
        scan_sink = scan(api);
      }
      if (std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - c0).count() >= 50.0) {
        break;
      }
    }
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      const auto t0 {std::chrono::steady_clock::now()};
      std::size_t hits {0};
      for (int k = 0; k < inner; ++k) {
        hits = scan(api);
        scan_sink = hits; // see scan_sink: g++ hoists the invariant scan out of this loop otherwise
      }
      const auto t1 {std::chrono::steady_clock::now()};
      out.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / inner);
      if (expect_hit && hits == 0) {
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
  // Ramps the governor and pins the core BEFORE any row is timed; on a powersave host the per-call rows
  // otherwise carry up to 3.8x of pure frequency noise. See benchmarks/bench_warmup.hpp.
  bench::ramp_and_pin();
  const char* env {std::getenv("BENCH_SAMPLES")};
  const int   n   {env != nullptr && std::atoi(env) > 0 ? std::atoi(env) : 30};

  const std::string prose {repeat_to("the quick brown fox jumps over the lazy dog and then rests 42 ",
                                     100000)};
  const std::string mixed {repeat_to("alice@example.com 2024-03-17 caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87 "
                                     "deadbeefcafe a1b2, Fox! ", 100000)};
  const std::string csv {repeat_to("alpha,beta,gamma,delta,epsilon,zeta,charlie,eta,theta,iota,",
                                   100000)};
  // SHORT subjects, and they are the point of the rows at the end: a fixed per-CALL cost is invisible at
  // 100 KB (400 ns is 0.004 ns/byte) and dominant at 60 (6.7 ns/byte). Every row above amortises over a
  // large corpus, so this file could not see the regime a caller reaches when it validates one field at a
  // time -- which is how a real deployment reported 3x against std::regex on a shape nothing here measured.
  const std::string stamp_exact {"2026-08-10_11:43:27"};                                  // the subject IS the match
  const std::string stamp_in    {"champ = 2026-08-10_11:43:27 ; suite de la ligne"};       // the match is inside it
  const std::string pad_line    {"   nomDuChampAvecUnPeuDeLongueur   "};

  // The rows a batching change is judged on: the ones it targets, and the ones it must not charge.
  //
  // The last three exist because their absence was a hole, not an oversight to leave standing. Every
  // row above is served by a batched route and reads one route entry per `batch_cap` matches, so none of
  // them has any per-match dispatch left to recover — while `date`, `lookahead` and `alternation` are
  // exactly the rows docs/BENCHMARKS.md §A has REAL behind PCRE2-JIT, and the first two still cross a
  // full route entry per match (1.0003 and 1.0001, measured under REAL_PROFILE). They were judgeable only
  // on the four-engine harness, which sits on a different inlining budget than a consumer does
  // (docs/MEASUREMENT.md §5.5) — so the two targets worth pursuing could not be judged by the instrument
  // that decides whether a change lands. Patterns and corpora are the harness's own, so a row here and a
  // row there measure the same thing.
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
    {"date {4}-{2}-{2}", "[0-9]{4}-[0-9]{2}-[0-9]{2}", mixed},
    {"lookahead [a-z]+(?=[a-z])", "[a-z]+(?=[a-z])", prose},
    {"alt the|fox|dog", "the|fox|dog", prose},
    // The lazy-DFA route, which had no row here at all -- and it was also the only route with no batch
    // filler, which is not a coincidence: an unmeasured route is where an unamortised per-match return
    // survives. A literal alternation (the row above) is claimed by the alternation recognizer; put a
    // CLASS in a branch and no recognizer claims it, so it lands on the DFA. It billed 0.9949 engine
    // entries per match against 0.2501 for every batched route, and 10.00 ns/B against `[a-z]+`'s 1.20.
    {"alt class [a-z]+|[0-9]+", "[a-z]+|[0-9]+", prose},
    // The INNER-LITERAL route, which had no row either, for the same reason and with the same consequence:
    // it billed 1.001 engine entries per match. `\w+@\w+` is the shape the route was built for -- a match
    // that does not START with a literal but requires one inside it, so no prefix skip applies and the `@`
    // is the most selective thing available. It reuses the `mixed` corpus (one address per ~57 bytes)
    // rather than adding a fourth 100 KB one, because a data row in this unit is not free.
    {"inner lit \\w+@\\w+", R"(\w+@\w+)", mixed},
    // The SAME pattern as the row above it, through the iterator instead of `count_matches`. It exists
    // because its absence hid a 12x: `find_iter` could not reach the trailing-lookaround route (a return
    // type cannot name a specialization a runtime hint picks), so it ran the general VM at 53.84 ns/B
    // against `count_matches`' 4.41 while every non-lookaround pattern had the two surfaces within 1 %.
    // A row that only ever measures one API cannot see a route the other one misses.
    {"lookahead find_iter", "[a-z]+(?=[a-z])", prose, surface::find_iter},
    // The per-CALL regime. Patterns and subjects come from a deployment's own profile (timestamped field
    // names and a whitespace trim), and the two `search` rows are the two regimes a reconciliation had to
    // separate: on a subject that IS the match an anchored pattern loses to std::regex, on a subject that
    // merely CONTAINS it the same pattern wins, because std::regex retries every position and this engine
    // refuses immediately. `match reject` is the row where a backtracker is furthest ahead: it answers NO on
    // the first byte where this engine pays a walk, and a fixed-width shape should be rejectable on its
    // LENGTH alone. `trim replace` is measured because it is the operation such a pass actually calls --
    // measuring `search` there described work the caller never does.
    {"short stamp search exact", R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2}$)", stamp_exact, surface::search},
    {"short stamp search in", R"([0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2})", stamp_in, surface::search},
    {"short stamp match hit", R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2}$)", stamp_exact, surface::match},
    {"short stamp match reject", R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2}$)", stamp_in, surface::match, false},
    {"short trim replace", R"(^[\t \n\r]+|[\t \n\r]+$)", pad_line, surface::replace},
  };

  std::string out {"{\"cases\":["};
  for (std::size_t i {0}; i < cases.size(); ++i) {
    const real::regex re {cases[i].pattern};
    const auto        samples {collect(re, cases[i].corpus, n, cases[i].api, cases[i].expect_hit)};
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
