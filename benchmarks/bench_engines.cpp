// Multi-engine throughput benchmark: REAL vs std::regex vs PCRE2 (JIT) vs RE2.
//
// In-process, apples-to-apples: every engine compiles the pattern once, then
// counts all non-overlapping matches over the same corpus; we time only the
// scan (median of repeats) and report ns per byte plus a ratio versus REAL.
// Engines other than REAL/std::regex are conditionally compiled:
//   c++ -std=c++20 -O2 -Iinclude benchmarks/bench_engines.cpp \
//       -DHAVE_PCRE2 $(pkg-config --cflags --libs libpcre2-8) \
//       -DHAVE_RE2 $(pkg-config --cflags --libs re2 absl_strings ...) -o bench_engines
//
// Match counts are printed per engine so semantic divergences are visible
// (the engines share ASCII-class semantics on these patterns, so counts agree).

#include <real/real.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

#if defined(HAVE_PCRE2)
#  define PCRE2_CODE_UNIT_WIDTH 8
#  include <pcre2.h>
#endif
#if defined(HAVE_RE2)
#  include <re2/re2.h>
#endif

namespace {

using clock_type = std::chrono::steady_clock;

// Runs scan() reps times, returns the median wall time in nanoseconds; writes
// the match count it produces into out_count (checked across engines).
template <typename Scan>
double median_ns(Scan&& scan, int reps, std::size_t& out_count)
{
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(reps));
  for (int i = 0; i < reps; ++i) {
    const auto t0 = clock_type::now();
    out_count     = scan();
    const auto t1 = clock_type::now();
    samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
  }
  std::ranges::sort(samples);
  return samples[samples.size() / 2];
}

// --- corpora --------------------------------------------------------------

std::string repeat_to(const std::string& unit, std::size_t target)
{
  std::string s;
  s.reserve(target + unit.size());
  while (s.size() < target) {
    s += unit;
  }
  return s;
}

// ~200 KB of lowercase words separated by spaces (word/class scans).
std::string corpus_words()
{
  return repeat_to("the quick brown fox jumps over a lazy dog and then rests ", 200000);
}

// ~200 KB of log lines each carrying an 8-hex id (the dense case REAL trailed).
std::string corpus_hex()
{
  return repeat_to("info 2026-06-13 req=a3f9c1d8 status=200 took=42ms path=/x\n", 200000);
}

// ~200 KB with sparse digit runs and dates.
std::string corpus_mixed()
{
  return repeat_to("order 4821 placed on 2026-06-13 total 199 items 7 ref ab12 ", 200000);
}

// ~200 KB of comma-separated fields.
std::string corpus_csv()
{
  return repeat_to("alpha,bravo,charlie,delta,echo,foxtrot,golf,hotel,india,juliet,", 200000);
}

struct bench_case
{
  const char* name;
  const char* pattern; // common subset: literals, classes, +, {n}, alternation
  std::string corpus;
};

std::vector<bench_case> cases()
{
  return {
    {.name = "words [a-z]+", .pattern = "[a-z]+", .corpus = corpus_words()},
    {.name = "alt the|fox|dog", .pattern = "the|fox|dog", .corpus = corpus_words()},
    {.name = "hex [0-9a-f]{8}", .pattern = "[0-9a-f]{8}", .corpus = corpus_hex()},
    {.name = "digits [0-9]+", .pattern = "[0-9]+", .corpus = corpus_mixed()},
    {.name = "date {4}-{2}-{2}", .pattern = "[0-9]{4}-[0-9]{2}-[0-9]{2}", .corpus = corpus_mixed()},
    {.name = "fields [^,]+", .pattern = "[^,]+", .corpus = corpus_csv()},
    {.name = "literal", .pattern = "charlie", .corpus = corpus_csv()},
  };
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
  auto             it  = std::sregex_iterator(text.begin(), text.end(), re);
  const auto       end = std::sregex_iterator();
  return static_cast<std::size_t>(std::distance(it, end));
}

#if defined(HAVE_PCRE2)
std::size_t pcre2_count(const std::string& pat, const std::string& text)
{
  int        errc {};
  PCRE2_SIZE erroff {};
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
    const int rc = pcre2_jit_match(re, reinterpret_cast<PCRE2_SPTR>(text.data()),
                                   text.size(), pos, 0, md, nullptr);
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
  const RE2       re(pat);
  re2::StringPiece input(text);
  re2::StringPiece m;
  std::size_t      n {};
  std::size_t      pos {};
  while (pos <= text.size() &&
         re.Match(text, pos, text.size(), RE2::UNANCHORED, &m, 1)) {
    const std::size_t s = static_cast<std::size_t>(m.data() - text.data());
    const std::size_t e = s + m.size();
    ++n;
    pos = e > s ? e : e + 1;
  }
  return n;
}
#endif

void run()
{
  constexpr int reps = 9;
  std::printf("%-22s %10s", "case", "REAL ns/B");
  std::printf(" %12s", "std ns/B(x)");
#if defined(HAVE_PCRE2)
  std::printf(" %14s", "pcre2jit ns/B(x)");
#endif
#if defined(HAVE_RE2)
  std::printf(" %12s", "re2 ns/B(x)");
#endif
  std::printf("   matches(R/s%s%s)\n", "", "");

  for (const auto& c : cases()) {
    const std::string pat {c.pattern};
    const double      bytes = static_cast<double>(c.corpus.size());

    std::size_t  rc {};
    const double rt = median_ns([&] { return real_count(pat, c.corpus); }, reps, rc);

    std::size_t  sc {};
    const double st = median_ns([&] { return std_count(pat, c.corpus); }, reps, sc);

    std::printf("%-22s %10.3f", c.name, rt / bytes);
    std::printf(" %9.3f(%4.2fx)", st / bytes, st / rt);

#if defined(HAVE_PCRE2)
    std::size_t  pc {};
    const double pt = median_ns([&] { return pcre2_count(pat, c.corpus); }, reps, pc);
    std::printf(" %10.3f(%4.2fx)", pt / bytes, pt / rt);
#endif
#if defined(HAVE_RE2)
    std::size_t  ec {};
    const double et = median_ns([&] { return re2_count(pat, c.corpus); }, reps, ec);
    std::printf(" %8.3f(%4.2fx)", et / bytes, et / rt);
#endif

    std::printf("   %zu/%zu", rc, sc);
#if defined(HAVE_PCRE2)
    std::printf("/%zu", pc);
#endif
#if defined(HAVE_RE2)
    std::printf("/%zu", ec);
#endif
    std::printf("\n");
  }
  std::printf("\n(x) = engine_time / REAL_time. >1 means REAL is faster. "
              "ns/B = nanoseconds per corpus byte (lower is better).\n");
}

// Safety axis: a nested quantifier that has no match is linear for REAL and
// RE2 but exponential for a backtracker. We scale the linear engines to a huge
// input and std::regex only to tiny inputs (it doubles per added character).
void run_redos()
{
  std::printf("\nReDoS safety — (a+)+b over \"a\"*N (no 'b', so no match):\n");
  std::size_t       sink {};
  const std::string big(100000, 'a');

  const real::regex rx("(a+)+b");
  const double      rt = median_ns([&] { return rx.search(big).matched() ? 1U : 0U; }, 3, sink);
  std::printf("  REAL        N=100000 : %8.3f ms (linear)\n", rt / 1e6);

#if defined(HAVE_RE2)
  const RE2    re("(a+)+b");
  const double et = median_ns([&] { return RE2::PartialMatch(big, re) ? 1U : 0U; }, 3, sink);
  std::printf("  RE2         N=100000 : %8.3f ms (linear)\n", et / 1e6);
#endif

  std::printf("  std::regex (backtracker, tiny N only):\n");
  for (const int n : {22, 24, 26, 28}) {
    const std::string s(static_cast<std::size_t>(n), 'a');
    const std::regex  re("(a+)+b");
    try {
      const double t = median_ns([&] { return std::regex_search(s, re) ? 1U : 0U; }, 3, sink);
      std::printf("    std::regex N=%-6d : %8.3f ms\n", n, t / 1e6);
    }
    catch (const std::exception& e) {
      // libc++ aborts catastrophic backtracking with error_complexity.
      std::printf("    std::regex N=%-6d : refused (%s)\n", n, e.what());
    }
  }
}

} // namespace

int main()
{
  run();
  run_redos();
  return 0;
}
