//! Route differential: every pike_vm route against the general Pike VM, over generated patterns and subjects.
//!
//! A route answers a search from a shortcut -- a literal it scans for, a start it reverses to, a table that
//! fills the groups -- and each shortcut holds only under conditions its guard has to state exactly. The test
//! suite pins the shapes someone thought of; this net generates the ones nobody did, over alphabets so small
//! that a literal, the classes around it and its own repetitions overlap all the time, which is where a
//! guard's conditions break. Two modes:
//!
//!   * `general`: patterns over classes, quantifiers (lazy ones too), groups, alternations, anchors, `(?i)`
//!     and UTF-8. At every start position, `search` against the general VM run on the same program with
//!     its hints blanked; then every match and group of `find_iter` against the same walk with every
//!     route's disable knob set, which also takes the batched walks off; then `search` again with one knob,
//!     drawn per pattern, set alone.
//!   * `inner`: patterns built around a literal, the inner-literal route's own shape, over {a, b, x}. Every
//!     match and group of `find_iter` with the route on against the route off.
//!
//! Deterministic: the seed fixes the patterns and the subjects. Exit status 1 on the first divergence of
//! each pattern (up to ten printed), 0 otherwise. Reproduce a reported line by its seed and count.
//!
//! Usage: route_differential <general|inner> <seed> <patterns>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <real/real.hpp>

namespace {

  using real::detail::dynamic_storage;
  using slots = std::vector<std::size_t>;

  class generator
  {
  public:

    explicit generator(unsigned seed) : rng_ {seed}
    {}

    int below(int n)
    {
      return static_cast<int>(rng_() % static_cast<unsigned>(n));
    }

    std::string literal(const char* alphabet)
    {
      std::string s;
      const int   n {1 + below(3)};
      const int   width {static_cast<int>(std::string_view {alphabet}.size())};
      for (int i {0}; i < n; ++i) {
        s += alphabet[below(width)];
      }
      return s;
    }

    std::string quantifier()
    {
      static const char* const q[] {"", "", "", "+", "*", "?", "{1,2}", "{2}", "+?", "*?", "{0,2}"};
      return q[below(11)];
    }

    //! An atom of the general mode; nesting stops at depth 2.
    std::string general_atom(int depth)
    {
      switch (below(depth > 1 ? 10 : 13)) {
        case 0:
        case 1:
          return literal("abx_1");
        case 2:
          return "[ab]" + quantifier();
        case 3:
          return "[^a ]" + quantifier();
        case 4:
          return "\\w" + quantifier();
        case 5:
          return "\\d" + quantifier();
        case 6:
          return "." + quantifier();
        case 7:
          return below(2) != 0 ? "\\b" : "\\s";
        case 8:
          return (below(2) != 0 ? std::string {"x"} : std::string {"\xC3\xA9"}) + quantifier();
        case 9:
          return below(3) == 0 ? "$" : "a";
        case 10:
          return "(" + general_atom(depth + 1) + general_atom(depth + 1) + ")" + quantifier();
        case 11:
          return "(" + general_atom(depth + 1) + "|" + general_atom(depth + 1) + ")" + quantifier();
        default:
          return "(?:" + literal("abx_1") + "|" + literal("abx_1") + "|" + general_atom(depth + 1) + ")" + quantifier();
      }
    }

    std::string general_pattern()
    {
      std::string p {below(3) == 0 ? "(?i)" : ""};
      const int   atoms {1 + below(4)};
      for (int i {0}; i < atoms; ++i) {
        p += general_atom(0);
      }
      return p;
    }

    std::string general_subject()
    {
      static const char* const tokens[] {"a", "b", "x", " ", "_", "1", "A", "B", "\xC3\xA9"};
      std::string              s;
      const int                n {below(24)};
      for (int i {0}; i < n; ++i) {
        s += tokens[below(9)];
      }
      return s;
    }

    //! An atom of the inner mode: literals, alternations of them, and the loops around a literal.
    std::string inner_atom()
    {
      switch (below(14)) {
        case 0:
          return literal("abx");
        case 1:
          return "(" + literal("abx") + "|" + literal("abx") + ")";
        case 2:
          return "(" + literal("abx") + "|" + literal("abx") + "|" + literal("abx") + ")";
        case 3:
          return "[ab]+";
        case 4:
          return "[abx]+";
        case 5:
          return "(" + literal("abx") + "){1,2}";
        case 6:
          return "(" + literal("abx") + "|" + literal("abx") + ")+";
        case 7:
          return "(" + literal("abx") + ")+";
        case 8:
          return "[ab]{1,3}";
        case 9:
          return "[^b]+";
        case 10:
          return ".";
        case 11:
          return "(" + literal("abx") + ")?";
        case 12:
          return "[ax]*";
        default:
          return "[ax]";
      }
    }

    std::string captured(const std::string& atom)
    {
      return below(2) != 0 ? "(" + atom + ")" : atom;
    }

    std::string inner_pattern()
    {
      std::string p {captured(inner_atom()) + captured(inner_atom()) + literal("abx")};
      if (below(2) != 0) {
        p += captured(inner_atom());
      }
      if (below(3) == 0) {
        p += inner_atom();
      }
      return p;
    }

    std::string inner_subject()
    {
      std::string s;
      const int   n {below(20)};
      for (int i {0}; i < n; ++i) {
        s += "abx"[below(3)];
      }
      return s;
    }

  private:

    std::mt19937 rng_;
  };

  using knob = bool& (*)();

  //! Every route's disable knob. One set alone leaves the others' routes to answer around it, which is where a
  //! route's fallback runs: a guard that misbehaves only there is invisible with every knob set at once.
  constexpr knob knobs[] {real::detail::ac_density_gate_disabled,        real::detail::aho_corasick_route_disabled,
                          real::detail::bounded_backtrack_route_disabled, real::detail::class_fastpath_disabled,
                          real::detail::fixed_shape_pair_route_disabled, real::detail::fixed_shape_route_disabled,
                          real::detail::inner_literal_guard_disabled,    real::detail::inner_literal_route_disabled,
                          real::detail::lazy_dfa_route_disabled,         real::detail::possessive_fastpath_disabled,
                          real::detail::rare_disc_route_disabled,        real::detail::trailing_la_route_disabled};

  //! Every route's disable knob, set or cleared together: with all of them set a walk takes no route.
  void routes_off(bool off)
  {
    real::detail::ac_density_gate_disabled()        = off;
    real::detail::aho_corasick_route_disabled()     = off;
    real::detail::bounded_backtrack_route_disabled() = off;
    real::detail::class_fastpath_disabled()         = off;
    real::detail::fixed_shape_pair_route_disabled() = off;
    real::detail::fixed_shape_route_disabled()      = off;
    real::detail::inner_literal_guard_disabled()    = off;
    real::detail::inner_literal_route_disabled()    = off;
    real::detail::lazy_dfa_route_disabled()         = off;
    real::detail::possessive_fastpath_disabled()    = off;
    real::detail::rare_disc_route_disabled()        = off;
    real::detail::trailing_la_route_disabled()      = off;
  }

  //! Every match of a walk, each with every group's start and end.
  slots walk(const real::regex& re,
             const std::string& subject)
  {
    slots out;
    for (const auto& m : re.find_iter(subject)) {
      for (std::size_t g {0}; g < m.size(); ++g) {
        out.push_back(m.start(g));
        out.push_back(m.end(g));
      }
    }
    return out;
  }

  //! The first position where `search` and the hint-blanked VM disagree, as a description, or empty.
  std::string search_divergence(const real::regex&     re,
                                const dynamic_storage& general,
                                const std::string&     subject)
  {
    for (std::size_t pos {0}; pos <= subject.size(); ++pos) {
      const real::detail::program_view pv {general.view()};
      real::detail::pike_state         state;
      real::detail::pike_vm            vm(pv, state);
      slots                            want;
      const bool                       found {vm.run(subject, pos, real::detail::run_mode::search, want)};
      const auto                       m {re.search(subject, pos)};
      if (static_cast<bool>(m) != found) {
        return "search from " + std::to_string(pos) + ": found " + (found ? "by the VM only" : "by the route only");
      }
      for (std::size_t s {0}; found && s < want.size(); s += 2) {
        if (m.start(s / 2) != want[s] || m.end(s / 2) != want[s + 1]) {
          return "search from " + std::to_string(pos) + ": group " + std::to_string(s / 2);
        }
      }
    }
    return {};
  }

} // namespace

int main(int argc,
         char** argv)
{
  if (argc != 4 || (std::string_view {argv[1]} != "general" && std::string_view {argv[1]} != "inner")) {
    std::fprintf(stderr, "usage: route_differential <general|inner> <seed> <patterns>\n");
    return 2;
  }
  const bool     general_mode {std::string_view {argv[1]} == "general"};
  const unsigned seed {static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10))};
  const long     count {std::strtol(argv[3], nullptr, 10)};
  generator      gen {seed};
  std::set<std::string> diverged;
  long                  compiled {0};
  long                  armed {0};
  for (long i {0}; i < count; ++i) {
    const std::string        pattern {general_mode ? gen.general_pattern() : gen.inner_pattern()};
    std::vector<std::string> subjects;
    for (int k {0}; k < (general_mode ? 12 : 30); ++k) {
      subjects.push_back(general_mode ? gen.general_subject() : gen.inner_subject());
    }
    std::optional<real::regex> holder;
    try {
      holder.emplace(pattern);
    }
    catch (const real::regex_error&) {
      continue; // generated outside the grammar: not a divergence
    }
    const real::regex& re {*holder};
    const auto&        hints {re.raw_program().hints};
    const bool         on_inner {hints.inner_literal_len > 0 && hints.inner_literal_prefix >= 1};
    if (!general_mode && !on_inner) {
      continue; // the inner mode compares that route alone
    }
    ++compiled;
    armed += on_inner ? 1 : 0;
    (void) re.fullmatch("warm"); // builds the immutables the routes read, as a second call would
    std::optional<dynamic_storage> general;
    if (general_mode) {
      general.emplace(dynamic_storage::compile(pattern, real::flags::none));
      general->program.hints = {};
    }
    const knob one {knobs[gen.below(static_cast<int>(std::size(knobs)))]}; // this pattern's single knob
    for (const std::string& subject : subjects) {
      std::string why {general_mode ? search_divergence(re, *general, subject) : std::string {}};
      if (why.empty()) {
        const slots with {walk(re, subject)};
        if (general_mode) {
          routes_off(true);
        }
        else {
          real::detail::inner_literal_route_disabled() = true;
        }
        const slots without {walk(re, subject)};
        routes_off(false);
        if (with != without) {
          why = "find_iter";
        }
      }
      if (why.empty() && general_mode) {
        one() = true;
        why   = search_divergence(re, *general, subject);
        one() = false;
        if (!why.empty()) {
          why = "with one knob set, " + why;
        }
      }
      if (!why.empty() && diverged.insert(pattern).second && diverged.size() <= 10U) {
        (void) std::printf("  DIVERGE %-34s over \"%s\": %s\n", pattern.c_str(), subject.c_str(), why.c_str());
      }
    }
  }
  (void) std::printf("route_differential %s seed %u: %ld patterns (%ld on the inner-literal route), %zu diverge\n", argv[1],
              seed, compiled, armed, diverged.size());
  return diverged.empty() ? 0 : 1;
}
