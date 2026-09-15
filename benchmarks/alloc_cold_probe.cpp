// Cold first-search allocations, attributed by CONSTRUCTION site.
//
// WHAT THIS MEASURES. benchmarks/alloc_probe.cpp answers "what does a search allocate per ROUTE" on a
// warm cache -- and the answer is zero everywhere, because the scaffolding a search can need (the byte
// program, the shared alphabet, the two DFA constructors) is built once per regex and cached. This probe
// is the other half: what a COLD first search of a freshly compiled regex pays, and where. It counts by
// constructing each suspect directly -- build_byte_program, compute_lazy_alphabet, the two DFA
// constructors -- and then re-measures the whole first search, so the sum of the parts can be checked
// against the total they must reconstitute. An attribution whose parts do not sum to the whole is
// incomplete, and this prints the remainder rather than rounding it away.
//
// WHY COUNTS, NOT TIMES. An allocation count is exact and identical run to run; it is not subject to
// the layout floor that makes a sub-3 % timing delta unresolvable (docs/BENCHMARKS.md). The question
// "where do the cold allocations go" is decidable precisely because this instrument is the one that can
// answer it.
//
// THE MEASURED CHAIN (2026.09.15, this tree, `\w+\d+` over an 8 KB ASCII subject): 3 317 cold
// allocations, 0 on every warm search. build_byte_program 3 171 (95.6 %) -- of which the UTF-8 tries
// are 3 125 (98.5 % of it: `\w` 2 757, `\d` 368) -- alphabet 40, forward DFA 23, reverse DFA 27,
// remainder 56 (1.7 %, named). The onepass op_table is NOT among them: it is built only when the
// pattern has capture groups (pike.hpp's slot_count guard), and this pattern has none. The root
// cause is written above build_byte_program: the expansion is blind to the subject, so `\w`'s trie
// mostly recognises code points an ASCII subject cannot contain.
//
// THE CONTRAST THAT KEEPS THE NUMBERS DATED. `\w+@\w+` takes the inner-literal route and pays
// 5 649 cold: TWO byte-program builds (the main program's 2 808 and the prefix's 2 796 -- each
// carries a `\w` trie), the alphabet 41, remainder 4 (0.1 %). Its prefix reverse DFA is never
// built here: the 8 KB subject sits under the route's cold floor (93 KB for this prefix), so the
// route abandons to the core VM. The sums are ROUTE-scoped because a route must not be charged
// for scaffolding it never builds.
//
// THE CONSUMER VERDICT. This cost is paid once per regex object, on the first search of a pattern
// with a wide code-point class that routes through the DFA/IL machinery. No benchmark row measures
// it (bench_engines discards the first scan as cache-cold and times steady state) and
// test_compile_scaling covers compile, not this lazy build. So the consumer is the
// compile-and-search-once workload -- a short-lived process, a CLI -- and the fix is already
// designed in utf8_trie_node's note (a stack-disciplined arena) and above build_byte_program (an
// ASCII-restricted expansion that DELETES the work instead of cheapening it). Both are their own
// train with a correctness obligation; this probe's job is to keep the attribution one command
// away, not to argue for either.
//
// WHAT WRITING IT CAUGHT. Route-scoping (a route must not be charged for another's scaffolding),
// the process-wide DFA slot keyed by address (a second pattern in one process can reuse a freed
// immutables address and find a warm slot -- hence one pattern per process), and the IL cold
// floor. An attribution that does not sum to its total is incomplete; this one prints the
// remainder rather than rounding it.
//
// THE ATTRIBUTION IS PATTERN-SPECIFIC, and the probe says so itself: `(\w+)\d+` -- one capture
// group added -- leaves 2 387 of 5 648 (42.3 %) unattributed, because the named sites cost
// exactly the same and the whole delta lands in the remainder. That delta positively LOCATES the
// onepass op_table rather than merely failing to find it elsewhere: the slot_count guard builds it
// for patterns WITH groups. Attributing those 2 387 by construction is the next measurement,
// named here rather than assumed. `make alloc-cold-probe` runs all three patterns, so the limit
// shows in the output, not only in this paragraph.
//
// BUILD: c++ -std=c++20 -O2 -I include -I benchmarks benchmarks/alloc_cold_probe.cpp
// or `make alloc-cold-probe`.

#define REAL_BENCH_ALLOCS
#include "measure.hpp"

#include <cstdio>
#include <string>

#include "real/real.hpp"
#include "real/automata/lazy_dfa.hpp"

namespace {

  using bench::detail::g_bytes;
  using bench::detail::g_count;
  using bench::detail::g_on;

  //! Count allocations during one construction, print the line, return the count.
  template <typename F>
  std::size_t site(const char* name, F&& build)
  {
    g_on    = true;
    g_count = 0;
    g_bytes = 0;
    build();
    g_on = false;
    std::printf("  %-26s %7zu allocs %9zu B\n", name, g_count, g_bytes);
    return g_count;
  }

  //! The cold first search of a freshly compiled regex over an 8 KB ASCII subject.
  std::size_t cold_first_search(const char* pat)
  {
    std::string text;
    while (text.size() < 8192) {
      text += "contact john.doe@example.com ";
    }
    real::regex rx {pat};
    g_on    = true;
    g_count = 0;
    g_bytes = 0;
    std::size_t matches {0};
    for (const auto& m : rx.find_iter(text)) {
      (void) m;
      ++matches;
    }
    g_on = false;
    std::printf("  %-26s %7zu allocs %9zu B  (%zu matches)\n", "TOTAL cold first search", g_count,
                g_bytes, matches);
    return g_count;
  }

  void attribute(const char* pat)
  {
    std::printf("%s\n", pat);
    auto storage {real::detail::dynamic_storage::compile(pat, real::flags::none)};
    const real::detail::program_view pv {storage.view()};

    real::detail::byte_program bp;
    const std::size_t          bp_allocs {
      site("build_byte_program", [&] { bp = real::detail::build_byte_program(pv); })};
    std::size_t tries {0};
    for (std::size_t ci = 0; ci < pv.cp_classes.size(); ++ci) {
      const std::string name {"  build_utf8_trie cp[" + std::to_string(ci) + "]"};
      std::size_t       nodes {0};
      tries += site(name.c_str(), [&] {
        real::detail::utf8_trie t {real::detail::build_utf8_trie(pv.cp_classes[ci], pv.cp_ranges)};
        nodes = t.nodes.size();
      });
      std::printf("  %-26s (%zu nodes)\n", "", nodes);
    }
    std::printf("  %-26s %7zu allocs (%.1f%% of the build)\n", "  = tries", tries,
                100.0 * static_cast<double>(tries) / static_cast<double>(bp_allocs));
    real::detail::lazy_byte_alphabet alpha;
    const std::size_t              alpha_allocs {
      site("compute_lazy_alphabet",
           [&] { alpha = real::detail::compute_lazy_alphabet(bp.code, bp.classes); })};
    // ensure_immutables always pays these two; the routes add their own below.
    const std::size_t base {bp_allocs + alpha_allocs};
    const std::size_t fwd_ctor {
      site("lazy_dfa ctor", [&] {
        real::detail::lazy_dfa fwd(bp.code, bp.classes, real::detail::lazy_dfa::state_budget, &alpha);
      })};
    const std::size_t rev_ctor {
      site("reverse_dfa ctor", [&] {
        real::detail::reverse_dfa rev(bp.code, bp.classes, real::detail::reverse_dfa::state_budget,
                                      &alpha);
      })};
    const std::size_t sum {base + fwd_ctor + rev_ctor};

    // The inner-literal route's own scaffolding, when the pattern has a prefix sub-program: its byte
    // expansion (a second trie build) and its reverse start-finder. The reverse is built on the first
    // candidate ONLY when the haystack clears the cold floor (il_min_haystack = clamp(prefix size x
    // 28, 64 KB, 512 KB) -- pike.hpp); below it the route abandons to the core VM and never builds it.
    std::size_t il_sites {base};
    bool        prefix_rev_built {false};
    if (!pv.prefix_code.empty()) {
      real::detail::byte_program prefix_bp;
      il_sites += site("build_byte_program (IL prefix)", [&] {
        real::detail::program_view pvw {};
        pvw.code         = pv.prefix_code;
        pvw.classes      = pv.prefix_classes;
        pvw.cp_classes   = pv.prefix_cp_classes;
        pvw.cp_ranges    = pv.prefix_cp_ranges;
        pvw.unicode_word = pv.unicode_word;
        prefix_bp        = real::detail::build_byte_program(pvw);
      });
      const std::size_t cold_floor {
        std::min<std::size_t>(512UL * 1024,
                              std::max<std::size_t>(64UL * 1024, prefix_bp.code.size() * 28))};
      prefix_rev_built = 8192 >= cold_floor; // the probe's subject is 8 KB
      if (prefix_rev_built) {
        il_sites += site("reverse_dfa ctor (IL prefix)", [&] {
          real::detail::reverse_dfa rev(prefix_bp.code, prefix_bp.classes);
        });
      }
      else {
        std::printf("  %-26s %7s  (subject 8 KB < cold floor %zu KB: never built)\n",
                    "reverse_dfa ctor (IL prefix)", "—", cold_floor / 1024);
      }
    }

    const std::size_t total {cold_first_search(pat)};
    // The sum must be ROUTE-scoped: ensure_immutables always builds the byte program(s) and the
    // shared alphabet, but the two search DFAs are built only when the DFA route runs, and the IL
    // prefix reverse only on the IL route's first candidate. Summing every site against every total
    // would charge a route for scaffolding it never builds.
    const bool il_route {!pv.prefix_code.empty()};
    const std::size_t route_sum {il_route ? il_sites : sum};
    std::printf("  route = %s; route sum = %zu; total = %zu; remainder = %ld (%.1f%%)\n\n",
                il_route ? "inner-literal" : "lazy DFA", route_sum, total,
                static_cast<long>(total) - static_cast<long>(route_sum),
                100.0 * static_cast<double>(total > route_sum ? total - route_sum : route_sum - total) /
                  static_cast<double>(total));
  }

} // namespace

int main(int argc, char** argv)
{
  // ONE PATTERN PER PROCESS, deliberately: the shared DFA slots are process-wide and keyed by
  // address, so a second pattern's regex can reuse a freed immutables address and find a warm slot
  // -- the first probe version under-counted its second pattern's cold search by exactly that.
  const char* pat {argc > 1 ? argv[1] : R"(\w+\d+)"};
  attribute(pat);
  return 0;
}
