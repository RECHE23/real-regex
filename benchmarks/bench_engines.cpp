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

// DO NOT INSTRUMENT THIS FILE TO MEASURE FASTER. Adding a runtime "time only REAL" switch here --
// one function, two branches and a getenv, with every engine still compiled and linked so the
// translation unit would be bit-identical in shape -- moved §A rows it cannot touch by 12 % on
// gcc/x86-64. Measured, not feared: the same engine tree read `digits` 1.788/1.791/1.789 ns/B with
// this file as it ships and 1.999/1.998/2.008 with the switch added, against a 1.786 baseline. This
// unit includes <regex>, PCRE2, RE2 and real.hpp together and sits on gcc's per-unit inline budget
// (docs/design.dox §10.1); anything added here spends from the same pot the engine's own inlining
// comes out of.
//
// A DATA ROW COSTS THE SAME, which is worse and was not obvious. Adding one entry to the Unicode
// case list -- no new code, one more brace-initialized struct in a table -- moved gcc/x86-64 `words`
// +27.7 % and its ASCII witness +26.9 %, `digits` +23.7 %, `[à-ÿ]+` +16.5 %, on the SAME engine tree
// with both binaries built and interleaved five times. arm64/clang did not move at all. So the
// budget is spent by anything this unit contains, not merely by anything it executes, and the table
// of cases is not a free place to put a new shape.
//
// The consequence is a rule, not a warning: numbers taken with a MODIFIED harness cannot be compared
// against numbers taken with the shipped one, in either direction. If this file must change, re-take
// both arms of every comparison afterwards -- and if the change is a row, expect to be re-taking a
// table that no longer compares to any stamp before it.
//
// THE TWO FIGURES ABOVE ARE SINGLE BUILDS, and docs/MEASUREMENT.md now says what that is worth: the
// same method was measured, on this harness, to report +16.7 % on a row that provably cannot be
// affected. So read "+27.7 % from adding a row" as one draw, not as the cost of a row. The rule it
// motivates survives intact and is if anything stronger -- but the way to keep it now is
// benchmarks/bench_layout.py, which puts the SAME harness on both arms of a comparison so a row
// addition cancels instead of being argued about.

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

// --- Layout draw ------------------------------------------------------------------------------
//
// A SEMANTICS-FREE size knob, used by benchmarks/bench_layout.py to compile the same source into
// several different code layouts. Nothing below is ever called; the point is only that `used` keeps
// N small functions in the object file, which shifts the address of everything emitted after them.
//
// Why this exists: this library is header-only, so every route body is inlined into the consumer's
// translation unit, and its address, alignment and I-cache colour are decided by code we do not
// control. A single build is therefore one sample from a distribution, not "the" performance — and
// a single-build A/B has been shown here to report double-digit deltas on rows that provably cannot
// be affected (deleting 21 lines of compile-time-only code moved `digits [0-9]+` by +16.7 %).
// Averaging over draws is what makes a comparison mean something: see docs/MEASUREMENT.md and
// Curtsinger & Berger, "STABILIZER: Statistically Sound Performance Evaluation" (ASPLOS 2013).
//
// Default 0 — the reference tables in docs/BENCHMARKS.md build with no padding at all, so this knob
// changes nothing unless a sweep asks for it.
#ifndef BENCH_LAYOUT_PAD
#  define BENCH_LAYOUT_PAD 0
#endif
#if BENCH_LAYOUT_PAD > 0
namespace {
  volatile int layout_sink {0}; //!< Keeps the pad bodies from folding away.

  //! One pad body. `noinline` + `used` so it survives to occupy address space.
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

  //! Forces the instantiation without a call site in any hot path.
  [[maybe_unused]] void (*const layout_anchor)() {&layout_pads<BENCH_LAYOUT_PAD>};
} // namespace
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

  // Anchored validation: a whole subject that IS one class run, which is the shape `^X$` exists for
  // (an identifier, a token, a field being checked end to end). Every engine must cross the whole
  // 200 KB to answer, so the row measures the anchored scan rather than a prefilter.
  std::string corpus_lower()
  {
    return repeat_to("abcdefghijklmnopqrstuvwxyz", 200000);
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
  // Whether PCRE2-JIT ANSWERS this pattern on this subject, as opposed to refusing it. A backtracker
  // bounded by match_limit returns PCRE2_ERROR_MATCHLIMIT, which is neither "match" nor "no match" --
  // the caller has to tell it from a genuine no-match, and code that treats any negative rc as "no
  // match" silently accepts a non-answer. Only a real match / no-match counts as completing here.
  bool pcre2_redos_completes(const std::string& pat,
                             const std::string& text)
  {
    int         errc   {};
    PCRE2_SIZE  erroff {};
    pcre2_code* re = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pat.c_str()),
                                   PCRE2_ZERO_TERMINATED, 0, &errc, &erroff, nullptr);
    if (re == nullptr) {
      return false;
    }
    pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
    pcre2_match_data* md {pcre2_match_data_create_from_pattern(re, nullptr)};
    const int         rc {pcre2_jit_match(re, reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(),
                                          0, 0, md, nullptr)};
    pcre2_match_data_free(md);
    pcre2_code_free(re);
    return rc >= 0 || rc == PCRE2_ERROR_NOMATCH;
  }

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
                             bool               lookaround   = false,
                             bool               std_overflow = false)
  {
    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("real", engine_result(collect([&] { return real_count(pat, text); }, n)));
    // `std_overflow` is not a capability gap, it is a CRASH, and it cannot be caught: libstdc++'s
    // std::regex recurses once per input character, so the anchored 200 KB row overflows the stack --
    // ~98 000 frames of _M_dfs before SIGSEGV, which takes the whole harness with it. libc++ survives
    // the same row (62.9 ns/B on arm64), so this is an implementation property rather than a language
    // one, and skipping is the only way to report it at all rather than losing every other row too.
    if (std_overflow) {
      fields.emplace_back("std", json_string("unsupported"));
    }
    else {
      fields.emplace_back("std", engine_result(collect([&] { return std_count(pat, text); }, n)));
    }
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
    //! Skip std::regex on this row: libstdc++ recurses once per input character and SIGSEGVs.
    bool        std_overflow = false;
  };

  std::vector<bench_case> cases()
  {
    return {
      {"words [a-z]+", "class-scan", "[a-z]+", corpus_words()},
      {"fields [^,]+", "class-scan", "[^,]+", corpus_csv()},
      {"alt the|fox|dog", "alternation", "the|fox|dog", corpus_words()},
      {"hex [0-9a-f]{8}", "quantifier", "[0-9a-f]{8}", corpus_hex()},
      {"date {4}-{2}-{2}", "quantifier", "[0-9]{4}-[0-9]{2}-[0-9]{2}", corpus_mixed()},
      // An ANCHORED row, added because this table had none: every case above is free-floating, so a
      // 17-94x change on `^X$`/`X$` moved nothing here and an unmeasured row is one that regresses in
      // silence -- the anchored path broke twice while being built, caught by a differential rather
      // than by any table.
      {"anchored ^[a-z]+$", "anchored", "^[a-z]+$", corpus_lower(), false, true},
      {"digits [0-9]+", "density", "[0-9]+", corpus_mixed()},
      // An UNQUANTIFIED class row, added for the reason the anchored row above was: every class-scan
      // case here carries a `+`, so the bare form -- a different route, and for a long time the slowest
      // per-byte class scan in the engine at 7.882 ns/B against `.`'s 8.518 while producing fewer
      // matches -- moved nothing in this table. It is the shape `re.findall(r'[a-z]', s)` and
      // `re.sub(r'[^a-z]', '', s)` actually run.
      {"single [a-z]", "class-scan", "[a-z]", corpus_words()},
      // A `{k,}` COUNTED-MINIMUM row, added for the reason the anchored, unquantified and `\b`-wrapped
      // rows were: it is its own batching case (a maximal run shorter than k cannot satisfy the
      // pattern and must be skipped), it is what `\w{3,}`-style validators actually run, and every
      // other quantifier row here is an EXACT count (`{8}`, `{4}-{2}-{2}`), which is a different shape.
      {"words [a-z]{4,}", "class-scan", "[a-z]{4,}", corpus_words()},
      // A BARE POSSESSIVE row. It now measures the same as `words [a-z]+` above, which is the point:
      // the recognizer redirects the bare form to the greedy selector because they are the same
      // language, so a row that reads DIFFERENTLY means that redirect has been lost. `[a-z]+` cannot
      // catch that; only this row can. PCRE2 supports `++`; std::regex (ECMAScript) and RE2 do not and
      // report `unsupported` by auto-detection.
      {"words [a-z]++", "class-scan", "[a-z]++", corpus_words()},
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
        {"engines", engines_object(c.pattern, c.corpus, n, c.lookaround, c.std_overflow)},
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
      // The `\b`-WRAPPED twin of the row above, added for the reason the anchored and unquantified
      // rows were: it is a different route, it is the canonical word tokenizer, and this table
      // measured only the bare form. It matches exactly what `\w+` matches here -- the recognizer
      // proves a leading `\b` redundant on a maximal run -- so any gap between the two rows is pure
      // routing, which is what makes the pair worth publishing side by side.
      {"unicode \\b\\w+\\b (mixed-script)", "\\b\\w+\\b", corpus_mixed_script()},
      // The bare POSSESSIVE twin of the `\w+` row. Like `words [a-z]++` in the ASCII table, it must
      // read the same as `\w+`: the recognizer redirects it to the same selector because it is the
      // same language, so a row that reads differently means that redirect has been lost.
      {"unicode \\w++ (mixed-script)", "\\w++", corpus_mixed_script()},
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
    // The RESISTANT shape. `(a+)+b` alone flatters every engine with an optimiser: PCRE2
    // auto-possessifies it (`a` and `b` are disjoint) AND prefilters the required `b`, so it answers
    // instantly and its backtracking never shows. Anchored at both ends with a breaking suffix, there
    // is no required literal to scan for and nothing to possessify -- which is what actually
    // distinguishes a linear engine from a bounded backtracker.
    const std::string big_broken {std::string(100000, 'a') + "b"};

    entries.push_back(json_object({
      {"engine", json_string("real")}, {"pattern", json_string("(a+)+b")},
      {"n", json_number(100000)},
      {"samples", json_array(collect([&] {
                                       const real::regex rx("(a+)+b");
                                       return rx.search(big).matched() ? 1U : 0U;
                                     }, n).samples)}}));

    entries.push_back(json_object({
      {"engine", json_string("real")}, {"pattern", json_string("^(a+)+$")},
      {"n", json_number(100000)},
      {"samples", json_array(collect([&] {
                                       const real::regex rx("^(a+)+$");
                                       return rx.search(big_broken).matched() ? 1U : 0U;
                                     }, n).samples)}}));

#if defined(HAVE_PCRE2)
    // Both shapes, deliberately. On `(a+)+b` PCRE2-JIT answers in microseconds -- a real result, and a
    // real property of its optimiser. On `^(a+)+$` over the same length it exceeds its default
    // match_limit and returns PCRE2_ERROR_MATCHLIMIT: an ERROR, not "no match", which a caller must
    // distinguish. Empty samples render as "refused" below, which is exactly what happened.
    for (const auto& shape : {std::pair<const char*, const std::string*> {"(a+)+b", &big},
                              std::pair<const char*, const std::string*> {"^(a+)+$", &big_broken}}) {
      std::vector<double> samples;
      if (pcre2_redos_completes(shape.first, *shape.second)) {
        samples = collect([&] { return pcre2_redos_completes(shape.first, *shape.second) ? 1U : 0U; },
                          3).samples;
      }
      entries.push_back(json_object({
        {"engine", json_string("pcre2")}, {"pattern", json_string(shape.first)},
        {"n", json_number(100000)}, {"samples", json_array(samples)}}));
    }
#endif

#if defined(HAVE_RE2)
    // BOTH shapes, like REAL and PCRE2 above. Asking RE2 only the easy one left the resistant shape
    // -- the one this block's own comment calls the distinguishing case -- measured for two engines
    // out of four, which is not a comparison. RE2 is a linear-time engine and is expected to answer;
    // the point is that the table says so from a measurement rather than from its architecture.
    entries.push_back(json_object({
      {"engine", json_string("re2")}, {"pattern", json_string("(a+)+b")}, {"n", json_number(100000)},
      {"samples", json_array(collect([&] {
                                       const RE2 re("(a+)+b");
                                       return RE2::PartialMatch(big, re) ? 1U : 0U;
                                     }, n).samples)}}));
    entries.push_back(json_object({
      {"engine", json_string("re2")}, {"pattern", json_string("^(a+)+$")}, {"n", json_number(100000)},
      {"samples", json_array(collect([&] {
                                       const RE2 re("^(a+)+$");
                                       return RE2::FullMatch(big_broken, re) ? 1U : 0U;
                                     }, n).samples)}}));
#endif

    // Same two shapes for std::regex, at the tiny N a backtracker survives. The resistant shape is
    // asked here too rather than assumed: "it would obviously also blow up" is the reasoning this
    // whole section exists to replace.
    for (const char* shape : {"(a+)+b", "^(a+)+$"}) {
      // Straddle libc++'s cutoff instead of sitting above it. Swept 8..26: the time DOUBLES per
      // added character -- 0.072, 0.139, 0.282, 0.558, 1.116 ms at N = 8..12 on `(a+)+b` -- and
      // then libc++ refuses outright from N = 13, on both shapes, on a complexity counter rather
      // than a clock. Three points above the cutoff all read "refused" and show none of that;
      // these show the exponential, the last measurable point, and that it stays refused.
      for (const int small : {8, 10, 12, 13, 26}) {
        // Each shape needs the subject that DEFEATS it -- the same pairing the large subjects use
        // above (`big` for one, `big_broken` for the other). Give either shape the other's subject
        // and it matches on the first path in microseconds, measuring a trivial success instead of
        // backtracking: that is a measurement of nothing, and both directions of the mistake were
        // made here before this comment existed.
        const std::string   s {std::string(static_cast<std::size_t>(small), 'a')
                               + (std::string_view {shape} == "(a+)+b" ? "" : "b")};
        std::vector<double> samples;
        try {
          samples = collect([&] {
                              const std::regex re(shape);
                              return std::regex_search(s, re) ? 1U : 0U;
                            }, 3).samples; // a backtracker: keep reps small
        }
        catch (const std::exception&) {
          // libc++ may abort catastrophic backtracking; leave the samples empty.
        }
        entries.push_back(json_object({
          {"engine", json_string("std")}, {"pattern", json_string(shape)},
          {"n", json_number(static_cast<double>(small))},
          {"samples", json_array(samples)}}));
      }
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

  // The LIBRARY version of each optional engine actually LINKED, which is not the same question as
  // unicode_versions() above and not answerable from the build line: the recipe resolves PCRE2 and
  // RE2 through pkg-config, so whichever one is first on PKG_CONFIG_PATH wins, and a from-source
  // build installed outside that path is silently not the one measured. docs/BENCHMARKS.md named a
  // pinned PCRE2 version its x86-64 leg had not linked for exactly that reason, and nothing in the
  // output could have contradicted it. RE2 has no runtime version API, so it stays absent here
  // rather than being guessed.
  std::string engine_versions()
  {
    std::vector<std::pair<std::string, std::string>> fields;
#if defined(HAVE_PCRE2)
    {
      char buf[64] {};
      pcre2_config(PCRE2_CONFIG_VERSION, buf);
      fields.emplace_back("pcre2", json_string(buf));
    }
#endif
    if (fields.empty()) {
      return "{}";
    }
    return json_object(fields);
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
      {"engine_versions", engine_versions()},
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
