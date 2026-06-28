// Multi-engine throughput collector: REAL vs std::regex vs PCRE2 (JIT) vs RE2.
//
// This program ONLY measures and emits JSON — no statistics, no formatting, no
// google-benchmark. The Python consumer (bench_engines.py) parses the JSON and applies the
// shared, dependency-free stats module (benchmarks/real_bench) to produce the table,
// confidence intervals, and ASCII box-plots.
//
// In-process and apples-to-apples: every engine compiles the pattern once, then counts all
// non-overlapping matches over the same corpus. A warm-up repetition is discarded (the first
// scan is cache-cold); then N raw per-scan samples are emitted (not a median). Match counts
// are emitted per engine so a semantic divergence is visible. PCRE2/RE2 are conditionally
// compiled and reported only when present.
//
//   c++ -std=c++20 -O2 -Iinclude benchmarks/bench_engines.cpp \
//       -DHAVE_PCRE2 $(pkg-config --cflags --libs libpcre2-8) \
//       -DHAVE_RE2 $(pkg-config --cflags --libs re2 absl_strings ...) -o bench_engines
//   BENCH_SAMPLES=30 ./bench_engines > engines.json

#include <real/real.hpp>
#include <sciforge/bench.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#if defined(HAVE_PCRE2)
#  define PCRE2_CODE_UNIT_WIDTH 8
#  include <pcre2.h>
#endif
#if defined(HAVE_RE2)
#  include <re2/re2.h>
#endif

#ifndef BENCH_FLAGS
#  define BENCH_FLAGS "unknown"
#endif
#ifndef BENCH_COMMIT
#  define BENCH_COMMIT "unknown"
#endif

namespace {

// The timing primitives and the comma-safe JSON emitter now live in SciForge's shared C++
// collector (include/sciforge/bench.hpp, sibling via SCIFORGE_INCLUDE). The using-declarations
// keep every json_* call site below unchanged; this file keeps its own custom multi-section
// JSON shape ({meta,cases,scaling,redos}), so it does NOT use emit_case/emit_run.
using sciforge::bench::json_array;
using sciforge::bench::json_join;
using sciforge::bench::json_number;
using sciforge::bench::json_object;
using sciforge::bench::json_string;

int sample_count()
{
  const char* env = std::getenv("BENCH_SAMPLES");
  const int   n   = (env != nullptr) ? std::atoi(env) : 30;
  return n > 0 ? n : 30;
}

// Discards one warm-up scan, then collects `n` raw per-scan samples (nanoseconds). The match
// count from the last scan is captured for the cross-engine check.
struct result
{
  std::vector<double> samples;
  std::size_t         count {};
};

template <typename Scan>
result collect(Scan&& scan, int n)
{
  // Manual warm-up (discarded): with inner=1, sciforge::bench::collect does NOT calibrate and
  // so does NOT warm up — this line keeps the original "first scan is cache-cold, discard it"
  // behavior, giving exactly 1 warm + n timed scans.
  scan();
  std::size_t count = 0;
  // inner=1 keeps "1 sample = 1 scan" (the distribution real's box-plots need); the shared
  // collector returns seconds, so scale to nanoseconds — real's JSON and bench_engines.py are
  // unchanged.
  const std::vector<double> secs = sciforge::bench::collect([&] { return count = scan(); }, n, 1);
  result                    r;
  r.count = count;
  r.samples.reserve(secs.size());
  for (const double s : secs) {
    r.samples.push_back(s * 1e9);
  }
  return r;
}

// One engine's measurement, or the literal "unsupported" when the engine cannot run it.
std::string engine_result(const result& r)
{
  return json_object({{"samples", json_array(r.samples)},
                      {"count", json_number(static_cast<double>(r.count))}});
}

// --- corpora ---------------------------------------------------------------

std::string repeat_to(const std::string& unit, std::size_t target)
{
  std::string s;
  s.reserve(target + unit.size());
  while (s.size() < target) {
    s += unit;
  }
  return s;
}

std::string corpus_words(std::size_t bytes = 200000)
{
  return repeat_to("the quick brown fox jumps over a lazy dog and then rests ", bytes);
}
std::string corpus_hex()
{
  return repeat_to("info 2026-06-13 req=a3f9c1d8 status=200 took=42ms path=/x\n", 200000);
}
std::string corpus_mixed()
{
  return repeat_to("order 4821 placed on 2026-06-13 total 199 items 7 ref ab12 ", 200000);
}
std::string corpus_csv()
{
  return repeat_to("alpha,bravo,charlie,delta,echo,foxtrot,golf,hotel,india,juliet,", 200000);
}

// --- engines (each counts non-overlapping matches) ------------------------

std::size_t real_count(const std::string& pat, const std::string& text)
{
  const real::regex rx(pat);
  std::size_t       n {};
  for (const auto& m : rx.find_iter(text)) {
    (void) m;
    ++n;
  }
  return n;
}

std::size_t std_count(const std::string& pat, const std::string& text)
{
  const std::regex re(pat, std::regex::ECMAScript | std::regex::optimize);
  return static_cast<std::size_t>(
    std::distance(std::sregex_iterator(text.begin(), text.end(), re), std::sregex_iterator()));
}

#if defined(HAVE_PCRE2)
std::size_t pcre2_count(const std::string& pat, const std::string& text)
{
  int         errc {};
  PCRE2_SIZE  erroff {};
  pcre2_code* re = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pat.c_str()),
                                 PCRE2_ZERO_TERMINATED, 0, &errc, &erroff, nullptr);
  if (re == nullptr) {
    return 0;
  }
  pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
  pcre2_match_data* md = pcre2_match_data_create_from_pattern(re, nullptr);
  std::size_t       n {};
  PCRE2_SIZE        pos {};
  while (pos <= text.size()) {
    const int rc = pcre2_jit_match(re, reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(),
                                   pos, 0, md, nullptr);
    if (rc < 0) {
      break;
    }
    const PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
    ++n;
    pos = ov[1] > ov[0] ? ov[1] : ov[1] + 1;
  }
  pcre2_match_data_free(md);
  pcre2_code_free(re);
  return n;
}
#endif

#if defined(HAVE_RE2)
std::size_t re2_count(const std::string& pat, const std::string& text)
{
  const RE2        re(pat);
  re2::StringPiece m;
  std::size_t      n {};
  std::size_t      pos {};
  while (pos <= text.size() && re.Match(text, pos, text.size(), RE2::UNANCHORED, &m, 1)) {
    const std::size_t s = static_cast<std::size_t>(m.data() - text.data());
    const std::size_t e = s + m.size();
    ++n;
    pos = e > s ? e : e + 1;
  }
  return n;
}
#endif

// Build the "engines" object for one (pattern, corpus). `lookaround` marks RE2 unsupported.
std::string engines_object(const std::string& pat, const std::string& text, int n,
                           bool lookaround = false)
{
  std::vector<std::pair<std::string, std::string>> fields;
  fields.emplace_back("real", engine_result(collect([&] { return real_count(pat, text); }, n)));
  fields.emplace_back("std", engine_result(collect([&] { return std_count(pat, text); }, n)));
#if defined(HAVE_PCRE2)
  fields.emplace_back("pcre2", engine_result(collect([&] { return pcre2_count(pat, text); }, n)));
#endif
#if defined(HAVE_RE2)
  if (lookaround) {
    fields.emplace_back("re2", json_string("unsupported")); // RE2 has no lookaround
  }
  else {
    fields.emplace_back("re2", engine_result(collect([&] { return re2_count(pat, text); }, n)));
  }
#else
  (void) lookaround;
#endif
  return json_object(fields);
}

struct bench_case
{
  std::string name;
  std::string family;
  std::string pattern;
  std::string corpus;
  bool        lookaround = false;
};

std::vector<bench_case> cases()
{
  return {
    {"words [a-z]+", "class-scan", "[a-z]+", corpus_words()},
    {"fields [^,]+", "class-scan", "[^,]+", corpus_csv()},
    {"alt the|fox|dog", "alternation", "the|fox|dog", corpus_words()},
    {"hex [0-9a-f]{8}", "quantifier", "[0-9a-f]{8}", corpus_hex()},
    {"date {4}-{2}-{2}", "quantifier", "[0-9]{4}-[0-9]{2}-[0-9]{2}", corpus_mixed()},
    {"digits [0-9]+", "density", "[0-9]+", corpus_mixed()},
    {"literal", "literal", "charlie", corpus_csv()},
    // Differentiator: a bounded lookahead. REAL/std/PCRE2 support it; RE2 has no lookaround.
    {"lookahead [a-z]+(?=[a-z])", "differentiator", "[a-z]+(?=[a-z])", corpus_words(), true},
  };
}

std::string emit_cases(int n)
{
  std::vector<std::string> entries;
  for (const auto& c : cases()) {
    const auto bytes = static_cast<double>(c.corpus.size());
    entries.push_back(json_object({
      {"name", json_string(c.name)},
      {"family", json_string(c.family)},
      {"pattern", json_string(c.pattern)},
      {"corpus_bytes", json_number(bytes)},
      {"engines", engines_object(c.pattern, c.corpus, n, c.lookaround)},
    }));
  }
  return "[" + json_join(entries) + "]";
}

// Scaling sweep: one class-scan pattern across sizes (the linear-throughput headline).
std::string emit_scaling(int n)
{
  const std::string        pattern = "[a-z]+";
  std::vector<std::string> entries;
  for (const std::size_t size : {std::size_t(1000), std::size_t(10000),
                                 std::size_t(100000), std::size_t(1000000)}) {
    const std::string corpus = corpus_words(size);
    entries.push_back(json_object({
      {"size", json_number(static_cast<double>(corpus.size()))},
      {"engines", engines_object(pattern, corpus, n)},
    }));
  }
  return "[" + json_join(entries) + "]";
}

// ReDoS, folded into the same schema: (a+)+b over "a"*N with no 'b'. The linear engines
// (REAL, RE2) run a large N; std::regex (a backtracker) only tiny N. Emitted as a list of
// {engine, n, samples}.
std::string emit_redos(int n)
{
  std::vector<std::string> entries;
  const std::string        big(100000, 'a');

  entries.push_back(json_object({
    {"engine", json_string("real")}, {"n", json_number(100000)},
    {"samples", json_array(collect([&] {
      const real::regex rx("(a+)+b");
      return rx.search(big).matched() ? 1U : 0U;
    }, n).samples)}}));

#if defined(HAVE_RE2)
  entries.push_back(json_object({
    {"engine", json_string("re2")}, {"n", json_number(100000)},
    {"samples", json_array(collect([&] {
      const RE2 re("(a+)+b");
      return RE2::PartialMatch(big, re) ? 1U : 0U;
    }, n).samples)}}));
#endif

  for (const int small : {22, 24, 26}) {
    const std::string s(static_cast<std::size_t>(small), 'a');
    std::vector<double> samples;
    try {
      samples = collect([&] {
        const std::regex re("(a+)+b");
        return std::regex_search(s, re) ? 1U : 0U;
      }, 3).samples; // a backtracker: keep reps small
    }
    catch (const std::exception&) {
      // libc++ may abort catastrophic backtracking; leave the samples empty.
    }
    entries.push_back(json_object({
      {"engine", json_string("std")}, {"n", json_number(static_cast<double>(small))},
      {"samples", json_array(samples)}}));
  }
  return "[" + json_join(entries) + "]";
}

std::string present_engines()
{
  std::vector<std::string> names = {json_string("real"), json_string("std")};
#if defined(HAVE_PCRE2)
  names.push_back(json_string("pcre2"));
#endif
#if defined(HAVE_RE2)
  names.push_back(json_string("re2"));
#endif
  return "[" + json_join(names) + "]";
}

const char* arch()
{
#if defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#else
  return "unknown";
#endif
}

std::string utc_date()
{
  const std::time_t now = std::time(nullptr);
  char              buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  return buf;
}

std::string emit_meta(int n)
{
  return json_object({
    {"bench", json_string("engines")},
    {"cpu", json_string(arch())},
    {"compiler", json_string(__VERSION__)},
    {"flags", json_string(BENCH_FLAGS)},
    {"engines", present_engines()},
    {"date", json_string(utc_date())},
    {"commit", json_string(BENCH_COMMIT)},
    {"samples", json_number(n)},
  });
}

} // namespace

int main()
{
  const int n = sample_count();
  const std::string doc = json_object({
    {"meta", emit_meta(n)},
    {"cases", emit_cases(n)},
    {"scaling", emit_scaling(n)},
    {"redos", emit_redos(n)},
  });
  std::printf("%s\n", doc.c_str());
  return 0;
}
