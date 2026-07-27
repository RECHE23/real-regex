// static_regex vs regex, same pattern, same corpus: what does knowing the pattern at COMPILE TIME buy
// today, and where could it buy more?
//
// The two storages take different code paths, and the point of this bench is to make that visible before
// anyone optimises for it. static_storage carries no `regex_immutables` -- no byte program, no lazy-DFA
// alphabet, no one-pass table -- so a pattern whose dynamic route needs those takes a different route
// here, and its row will say so by being SLOWER rather than faster. Conversely static_storage allocates
// nothing at all and sizes its scratch exactly, so short-subject rows should favour it.
//
// The corpus unit is byte-identical to bindings/rust/benches/engines.rs, so rows here are comparable with
// the criterion tables in docs/BENCHMARKS.md §E.4.
//
// Inner-literal patterns are measured on TWO corpora, because one is not enough to see them: the §E.4
// corpus holds no date, so a `\d{4}-\d{2}-\d{2}` row there only ever exercises the no-candidate sweep and
// says nothing about what happens once candidates appear. A first version of this bench had exactly that
// blind spot and reported a large win for a change that cost 20% on a haystack full of matches.
//
// Not a gate: timings are host noise. Run with `make bench-static`.
#include <real/real.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

  //! \brief The §E.4 corpus: mixed words / digits / an email / commas, ~64 KiB.
  std::string corpus(std::size_t want)
  {
    const std::string unit {"the quick brown fox jumps over 12 lazy dogs, cat9 and root@localhost meet fox42, "};
    std::string       t;
    while (t.size() + unit.size() <= want) {
      t += unit;
    }
    return t;
  }

  constexpr int trials {15};

  //! \brief Best-of-`trials` wall time for one full find_iter walk, in microseconds.
  template <typename Re>
  double walk_us(const Re&          re,
                 const std::string& text,
                 std::size_t&       matches)
  {
    double best {1e30};
    for (int k = 0; k < trials; ++k) {
      const auto  t0 {std::chrono::steady_clock::now()};
      std::size_t n {0};
      for (const auto& m : re.find_iter(text)) {
        n += m.end(0) > m.start(0) ? 1U : 0U;
      }
      const double us {std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count()};
      if (us < best) {
        best = us;
      }
      matches = n;
    }
    return best;
  }

  //! \brief Nanoseconds per single-shot search — the row where static_storage's zero-allocation, exactly
  //!        sized scratch should show, since a walk amortises it away.
  //!
  //! `inner` searches inside one timed region, then divided: `steady_clock` resolves to ~41 ns on this
  //! host, so timing ONE search quantises every result onto a multiple of that and the ratio becomes an
  //! artefact of the clock. Batching moves the measurement well clear of the tick.
  template <typename Re>
  double single_ns(const Re&          re,
                   const std::string& subject)
  {
    constexpr int inner {2000};
    double        best {1e30};
    for (int k = 0; k < trials; ++k) {
      const auto  t0 {std::chrono::steady_clock::now()};
      std::size_t hits {0};
      for (int i = 0; i < inner; ++i) {
        hits += re.search(subject).matched() ? 1U : 0U;
      }
      const double ns {std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count()
                       / static_cast<double>(inner)};
      if (ns < best) {
        best = ns;
      }
      if (hits == 123456789U) {
        std::printf("x"); // keeps the loop from being optimised away
      }
    }
    return best;
  }

  bool any_row_diverged {false};

  //! \brief One row: the same pattern through both storages, walk and single-shot, with a match-count
  //!        cross-check — a ratio means nothing if the two paths disagree on what they found.
  template <typename StaticRe>
  void row(const char*        label,
           const char*        dyn_pattern,
           const std::string& text,
           const std::string& subject)
  {
    constexpr StaticRe srx {};
    const real::regex  drx {dyn_pattern};

    std::size_t sm {0};
    std::size_t dm {0};
    const double sw {walk_us(srx, text, sm)};
    const double dw {walk_us(drx, text, dm)};
    const double ss {single_ns(srx, subject)};
    const double ds {single_ns(drx, subject)};

    const bool diverged {sm != dm};
    any_row_diverged = any_row_diverged || diverged;
    std::printf("  %-20s %9.2f %9.2f %7.2fx | %8.1f %8.1f %7.2fx | %6zu%s\n",
                label, sw, dw, dw / sw, ss, ds, ds / ss, sm,
                diverged ? "  <-- COMPTES DIVERGENTS" : "");
  }
  //! \brief One inner-literal row on one corpus. `route` is the compile-time criterion
  //!        (`wants_inner_literal`): whether this pattern's `run()` carries the literal-sweep route at all.
  template <typename StaticRe, typename Storage>
  void il_row(const char*        label,
              const char*        dyn_pattern,
              const char*        corpus_kind,
              const std::string& text,
              bool               expect_matches)
  {
    constexpr StaticRe srx {};
    const real::regex  drx {dyn_pattern};

    std::size_t sm {0};
    std::size_t dm {0};
    const double sw {walk_us(srx, text, sm)};
    const double dw {walk_us(drx, text, dm)};

    // The corpus label is checked, not trusted: a row claiming "no match" on a haystack that in fact
    // matches is how the blind spot in the header note happened.
    const bool diverged   {sm != dm};
    const bool mislabelled {(sm > 0) != expect_matches};
    any_row_diverged = any_row_diverged || diverged || mislabelled;
    std::printf("  %-26s %-9s %4s %9.2f %9.2f %7.2fx | %6zu%s%s\n",
                label, corpus_kind, Storage::wants_inner_literal ? "yes" : "no", sw, dw, dw / sw, sm,
                diverged ? "  <-- COMPTES DIVERGENTS" : "",
                mislabelled ? "  <-- CORPUS MAL ÉTIQUETÉ" : "");
  }
} // namespace

int main()
{
  const std::string text {corpus(64U * 1024U)};
  const std::string subject {"the quick brown fox jumps over 12 lazy dogs, cat9 and root@localhost meet fox42, "};

  std::printf("static_regex vs regex — same pattern, same corpus (%zu B), best of %d\n", text.size(), trials);
  std::printf("Ratio > 1 means the COMPILE-TIME regex is faster.\n\n");
  std::printf("  %-20s %9s %9s %7s | %8s %8s %7s | %6s\n",
              "pattern", "stat us", "dyn us", "walk", "stat ns", "dyn ns", "single", "match");
  std::printf("  %-20s %9s %9s %7s | %8s %8s %7s | %6s\n",
              "--------------------", "---------", "---------", "-------", "--------", "--------", "-------",
              "------");

  row<real::static_regex<"[a-z]+">>("[a-z]+", "[a-z]+", text, subject);
  row<real::static_regex<"[0-9]+">>("[0-9]+", "[0-9]+", text, subject);
  row<real::static_regex<"[^,]+">>("[^,]+", "[^,]+", text, subject);
  row<real::static_regex<"dog">>("dog", "dog", text, subject);
  row<real::static_regex<"fox|dog|cat">>("fox|dog|cat", "fox|dog|cat", text, subject);
  row<real::static_regex<"[0-9a-f]{8}">>("[0-9a-f]{8}", "[0-9a-f]{8}", text, subject);
  row<real::static_regex<"(?i)cafe">>("(?i)cafe", "(?i)cafe", text, subject);
  row<real::static_regex<"\\w+">>("\\w+", "\\w+", text, subject);
  row<real::static_regex<"\\b\\w+\\b">>("\\b\\w+\\b", "\\b\\w+\\b", text, subject);
  row<real::static_regex<"\\d{4}-\\d{2}-\\d{2}">>("\\d{4}-\\d{2}-\\d{2}", "\\d{4}-\\d{2}-\\d{2}", text, subject);
  row<real::static_regex<"(\\w+)@(\\w+)">>("(\\w+)@(\\w+)", "(\\w+)@(\\w+)", text, subject);

  // --- inner-literal rows: the same pattern with and without candidates present -------------------
  std::string dates;
  while (dates.size() + 48U <= 64U * 1024U) {
    dates += "on 2026-06-10 root@localhost paid 19.99 then x, ";
  }
  std::string nolit; // no date, no decimal, no '@': the no-candidate corpus for every row below
  while (nolit.size() + 62U <= 64U * 1024U) {
    nolit += "the quick brown fox jumps over lazy dogs and cats meet again; ";
  }

  std::printf("\n  Inner-literal rows — `route` is the compile-time criterion (static_storage::wants_inner_literal).\n\n");
  std::printf("  %-26s %-9s %4s %9s %9s %7s | %6s\n",
              "pattern", "corpus", "rte", "stat us", "dyn us", "walk", "match");
  std::printf("  %-26s %-9s %4s %9s %9s %7s | %6s\n",
              "--------------------------", "---------", "----", "---------", "---------", "-------",
              "------");
#define IL_PAIR(pat)                                                                                  \
  il_row<real::static_regex<pat>, real::detail::static_storage<pat>>(pat, pat, "no match", nolit, false); \
  il_row<real::static_regex<pat>, real::detail::static_storage<pat>>(pat, pat, "matches", dates, true)
  IL_PAIR("\\d{4}-\\d{2}-\\d{2}");
  IL_PAIR("[0-9]{4}-[0-9]{2}-[0-9]{2}");
  IL_PAIR("\\d+\\.\\d+");
  IL_PAIR("[a-z]+@[a-z]+");
  IL_PAIR("(\\w+)@(\\w+)");
#undef IL_PAIR

  std::printf("\n  sizeof(static_regex<\"[a-z]+\">) = %zu B, sizeof(regex) = %zu B\n",
              sizeof(real::static_regex<"[a-z]+">), sizeof(real::regex));
  if (any_row_diverged) {
    std::printf("  *** at least one row's match counts disagree — the ratio is not a comparison there\n");
    return 1;
  }
  return 0;
}
