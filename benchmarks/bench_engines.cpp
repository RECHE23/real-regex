// Multi-engine throughput collector: REAL vs std::regex vs PCRE2 (JIT) vs RE2.
//
// This program ONLY measures and emits JSON — no statistics, no formatting, no
// google-benchmark. The Python consumer (bench_engines.py) parses the JSON and applies the
// shared, dependency-free stats module (SciForge's sciforge.bench, on PYTHONPATH via the
// Makefile) to produce the table, confidence intervals, and ASCII box-plots.
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
  result collect(Scan&& scan,
                 int    n)
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

  std::string repeat_to(const std::string& unit,
                        std::size_t        target)
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

  // --- international corpora (Unicode comparative arc) -----------------------
  //
  // Each literal below is a UTF-8 hex-escape of codepoints resolved and name-verified via Python's
  // unicodedata before being pasted here (not typed as raw glyphs, to rule out a mojibake risk in an
  // editing pipeline) -- same size class (200 KB) as the ASCII corpora above, repeated to it.

  // CJK: Han "你好世界" (U+4F60 U+597D U+4E16 U+754C, "hello world") + hiragana "こんにちは"
  // (U+3053 U+3093 U+306B U+3061 U+306F, "konnichiwa") -- dense Han + a second script for scx=/sc=.
  std::string corpus_cjk()
  {
    return repeat_to("\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C\xE3\x80\x80"
                     "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF\x20",
                     200000);
  }

  // Arabic (RTL): the letters Meem/Reh/Hah/Beh/Alef + all ten Arabic-Indic digits (U+0660..U+0669 --
  // the same digits whose Script_Extensions {Arab, Thaa, Yezi} test_unicode_scx.cpp already pins).
  std::string corpus_arabic()
  {
    return repeat_to("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7\x20"
                     "\xD9\xA0\xD9\xA1\xD9\xA2\xD9\xA3\xD9\xA4\xD9\xA5\xD9\xA6\xD9\xA7\xD9\xA8\xD9\xA9\x20",
                     200000);
  }

  // Emoji: astral-plane singles (GRINNING FACE, PARTY POPPER, THUMBS UP) + a ZWJ family sequence
  // (MAN+ZWJ+WOMAN+ZWJ+GIRL+ZWJ+BOY) -- exercises \p{Emoji} across a 4-byte-UTF-8 codepoint and a
  // combining-sequence shape neither plain \w nor GC alone would.
  std::string corpus_emoji()
  {
    return repeat_to("\xF0\x9F\x98\x80\x20\xF0\x9F\x8E\x89\x20\xF0\x9F\x91\x8D\x20"
                     "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"
                     "\xE2\x80\x8D\xF0\x9F\x91\xA6\x20",
                     200000);
  }

  // Mixed-script: Latin + Han + Cyrillic ("Привет", U+041F U+0440 U+0438 U+0432 U+0435 U+0442,
  // "privet"/hello) + one emoji, interleaved -- the adversarial case for any per-script fast path.
  std::string corpus_mixed_script()
  {
    return repeat_to("Hello \xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C "
                     "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xF0\x9F\x98\x80 ",
                     200000);
  }

  // Dense-multibyte: Latin prose with a high 2-byte-UTF-8 density (café/résumé/naïve/façade-style
  // French orthography) -- the common case (Western European text), unlike the other four corpora,
  // so the sc=/scx=/binprop routes see both a "usually ASCII, occasionally 2-byte" shape and the
  // "large multi-byte runs" shapes above.
  std::string corpus_latin_accented()
  {
    return repeat_to("caf\xC3\xA9 r\xC3\xA9sum\xC3\xA9 na\xC3\xAFve fa\xC3\xA7" "ade "
                     "d\xC3\xA9j\xC3\xA0 v\xC3\xA9" "cu tr\xC3\xA8s \xC3\xA9l\xC3\xA8ve ",
                     200000);
  }

  // --- engines (each counts non-overlapping matches) ------------------------

  std::size_t real_count(const std::string& pat,
                         const std::string& text)
  {
    const real::regex rx(pat);
    // Matching-only (no Match vector) — equitable with std/pcre2/re2 counters.
    // Once-per-walk TrailingLA monomorphic path when eligible.
    return rx.count_matches(text);
  }

  std::size_t std_count(const std::string& pat,
                        const std::string& text)
  {
    const std::regex re(pat, std::regex::ECMAScript | std::regex::optimize);
    return static_cast<std::size_t>(
      std::distance(std::sregex_iterator(text.begin(), text.end(), re), std::sregex_iterator()));
  }

#if defined(HAVE_PCRE2)
  std::size_t pcre2_count(const std::string& pat,
                          const std::string& text)
  {
    int         errc   {};
    PCRE2_SIZE  erroff {};
    pcre2_code* re = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pat.c_str()),
                                   PCRE2_ZERO_TERMINATED, 0, &errc, &erroff, nullptr);
    if (re == nullptr) {
      return 0;
    }
    pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
    pcre2_match_data* md = pcre2_match_data_create_from_pattern(re, nullptr);
    std::size_t       n   {};
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
  std::size_t re2_count(const std::string& pat,
                        const std::string& text)
  {
    const RE2        re(pat);
    re2::StringPiece m;
    std::size_t      n   {};
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
  std::string engines_object(const std::string& pat,
                             const std::string& text,
                             int                n,
                             bool               lookaround = false)
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

  // --- Unicode comparative: per-engine support is DETECTED, not asserted -----
  //
  // The ASCII case set above hand-classifies the one axis that varies (RE2 lacks lookaround) via
  // `lookaround`. Unicode support varies on more axes per engine (std::regex has none at all; RE2 has
  // General_Category + Script but not Script_Extensions or most binary properties; PCRE2 is close to
  // complete) -- rather than hand-classify ~10 patterns x 3 non-REAL engines, each engine's own
  // compile step is the oracle: a pattern it fails to compile is "unsupported" for that engine, exactly
  // as REAL's own engine would report it via a binding. Measured, not hand-classified.

#if defined(HAVE_PCRE2)
  // PCRE2_UTF + PCRE2_UCP: subject text is UTF-8 (not raw bytes) and POSIX/Perl classes are Unicode-
  // aware -- required for \p{...} to mean anything in PCRE2 at all. Safe for the plain-ASCII case set
  // too (ASCII is valid UTF-8 by construction, and none of those patterns use \w/\d/\s where UCP could
  // change ASCII-mode semantics), but kept as a separate function rather than changing pcre2_count's
  // existing flags, to leave the already-measured ASCII cases byte-for-byte reproducible.
  std::size_t pcre2_count_utf(const std::string& pat,
                              const std::string& text,
                              bool             * compiled)
  {
    int         errc   {};
    PCRE2_SIZE  erroff {};
    pcre2_code* re = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pat.c_str()), PCRE2_ZERO_TERMINATED,
                                   PCRE2_UTF | PCRE2_UCP, &errc, &erroff, nullptr);
    if (re == nullptr) {
      *compiled = false;
      return 0;
    }
    *compiled = true;
    pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
    pcre2_match_data* md = pcre2_match_data_create_from_pattern(re, nullptr);
    std::size_t       n   {};
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

  // Same shape as engines_object, but every non-REAL engine's support is discovered by attempting the
  // compile (see the block comment above) instead of a hand-set flag.
  std::string engines_object_auto(const std::string& pat,
                                  const std::string& text,
                                  int                n)
  {
    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("real", engine_result(collect([&] { return real_count(pat, text); }, n)));

    bool std_ok = true;
    try {
      const std::regex probe(pat, std::regex::ECMAScript | std::regex::optimize);
    }
    catch (const std::regex_error&) {
      std_ok = false; // ECMAScript grammar: \p{...} is always a syntax error here, by construction
    }
    fields.emplace_back("std", std_ok
                              ? engine_result(collect([&] { return std_count(pat, text); }, n))
                              : json_string("unsupported"));

#if defined(HAVE_PCRE2)
    {
      bool pcre2_ok = false;
      (void) pcre2_count_utf(pat, text, &pcre2_ok); // compile-only probe; ok flag decides the real run
      fields.emplace_back("pcre2", pcre2_ok
                                  ? engine_result(collect([&] {
                                                            bool ok {};
                                                            return pcre2_count_utf(pat, text, &ok);
                                                          }, n))
                                  : json_string("unsupported"));
    }
#endif
#if defined(HAVE_RE2)
    {
      const RE2 probe(pat);
      fields.emplace_back("re2", probe.ok()
                                ? engine_result(collect([&] { return re2_count(pat, text); }, n))
                                : json_string("unsupported"));
    }
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

  // --- Unicode comparative cases ----------------------------------------------
  //
  // Word/token (\w+ Unicode mode, \p{L}+, \p{N}+), script (\p{sc=Han}, \p{scx=Grek}), case-fold
  // ((?i) on a non-ASCII literal), class/literal ([à-ÿ]+, a non-ASCII literal, `.` = one code point),
  // plus an ASCII witness on the SAME corpus shape for scale. Each case names the corpus it needs;
  // support is auto-detected per engine (engines_object_auto), not asserted here.
  struct unicode_bench_case
  {
    std::string name;
    std::string pattern;
    std::string corpus;
  };

  std::vector<unicode_bench_case> unicode_cases()
  {
    return {
      {"unicode \\w+ (mixed-script)", "\\w+", corpus_mixed_script()},
      {"unicode \\p{L}+ (CJK)", "\\p{L}+", corpus_cjk()},
      {"unicode \\p{N}+ (arabic digits)", "\\p{N}+", corpus_arabic()},
      {"unicode \\p{sc=Han} (CJK)", "\\p{sc=Han}", corpus_cjk()},
      {"unicode \\p{scx=Cyrl} (mixed-script)", "\\p{scx=Cyrl}", corpus_mixed_script()},
      {"unicode (?i)caf\xC3\xA9 (accented)", "(?i)caf\xC3\xA9", corpus_latin_accented()},
      {"unicode [\xC3\xA0-\xC3\xBF]+ (accented)", "[\xC3\xA0-\xC3\xBF]+", corpus_latin_accented()},
      {"unicode literal \xE4\xBD\xA0\xE5\xA5\xBD (CJK)", "\xE4\xBD\xA0\xE5\xA5\xBD", corpus_cjk()},
      {"unicode . (emoji, one codepoint)", ".", corpus_emoji()},
      {"ascii witness [a-z]+ (same corpus shape)", "[a-z]+", corpus_words()},
    };
  }

  std::string emit_unicode_cases(int n)
  {
    std::vector<std::string> entries;
    for (const auto& c : unicode_cases()) {
      const auto bytes = static_cast<double>(c.corpus.size());
      entries.push_back(json_object({
        {"name", json_string(c.name)},
        {"pattern", json_string(c.pattern)},
        {"corpus_bytes", json_number(bytes)},
        {"engines", engines_object_auto(c.pattern, c.corpus, n)},
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
      const std::string   s(static_cast<std::size_t>(small), 'a');
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

  // Each engine's Unicode DATA vintage, for the comparative section's confound check (different UCD
  // versions can match different code point sets, so a count divergence may be a version gap, not a
  // bug -- see docs/BENCHMARKS.md's Unicode section). REAL's own pin is a compile-time constant
  // (asserted identical across all the generated unicode_*.hpp tables, see tools/REGEN.md); PCRE2
  // exposes its Unicode data version at runtime; RE2 has no such API, so its vintage here is an
  // empirical finding (verified once: it accepts \p{Kawi}/\p{Nag_Mundari} (Unicode 15.0's new
  // scripts) but not \p{Todhri}/\p{Sunuwar} (16.0's), bounding it to UCD ~15.0/15.1); std::regex has
  // no Unicode property support at all (ECMAScript grammar, byte-level).
  std::string unicode_versions()
  {
    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("real", json_string(real::detail::unicode_script_unidata_version));
    fields.emplace_back("std", json_string("n/a (no \\p{} support, ECMAScript grammar)"));
#if defined(HAVE_PCRE2)
    {
      char buf[32] {};
      pcre2_config(PCRE2_CONFIG_UNICODE_VERSION, buf);
      fields.emplace_back("pcre2", json_string(buf));
    }
#endif
#if defined(HAVE_RE2)
    fields.emplace_back("re2", json_string("~15.0/15.1 (empirical bound, see comment above; "
                                           "no runtime API)"));
#endif
    return json_object(fields);
  }

  std::string emit_meta(int n)
  {
    return json_object({
      {"bench", json_string("engines")},
      {"cpu", json_string(arch())},
      {"compiler", json_string(__VERSION__)},
      {"flags", json_string(BENCH_FLAGS)},
      {"engines", present_engines()},
      {"unicode_versions", unicode_versions()},
      {"date", json_string(utc_date())},
      {"commit", json_string(BENCH_COMMIT)},
      {"samples", json_number(n)},
    });
  }
} // namespace

int main()
{
  const int         n   = sample_count();
  const std::string doc = json_object({
    {"meta", emit_meta(n)},
    {"cases", emit_cases(n)},
    {"unicode_cases", emit_unicode_cases(n)},
    {"scaling", emit_scaling(n)},
    {"redos", emit_redos(n)},
  });
  std::printf("%s\n", doc.c_str());
  return 0;
}
