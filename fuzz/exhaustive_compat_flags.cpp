// Exhaustive compat check of the match flags REAL honors on one search or match: real::compat vs the LOCAL
// std::regex over the same enumerated space as exhaustive_compat, under each flag set below. Every case compares
// the verdict, the whole-match span and every group span of regex_search, and the verdict of regex_match.
//
// Tolerated, each by its exact signature and counted apart:
// - the documented nullable-loop capture (groups only, std's differing group zero-width), as exhaustive_compat;
// - on a std that does not honor match_prev_avail (libc++ ignores it for `^` and never lets `\b` hold on an attempt
//   at last), the prev_avail sets have no oracle and are skipped, counted.
// Anything else is serious. A run that compared nothing is a failure, not a pass. On a std that honors every flag,
// the default tier's counts are pinned under `pin`: a class that grows or a space that shrinks is a failure too.
//
// Usage: exhaustive_compat_flags <patterns-file> <inputs-file> [stride] [pin|nopin]   (stride N: every Nth pattern)

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "real/compat/std/regex.hpp"

namespace rc = real::compat;
namespace rx = real::compat::regex_constants;
namespace sc = std::regex_constants;

namespace {

  struct outcome
  {
    bool                               searched {};
    long                               pos {-1};
    long                               len {-1};
    std::vector<std::pair<long, long>> groups;
    bool                               matched {}; //!< regex_match over the same range.

    bool operator==(const outcome&) const = default;
  };

  struct flag_set
  {
    const char*         name;
    rx::match_flag_type ours;
    sc::match_flag_type theirs;
  };

  constexpr flag_set flag_sets[] {
    {.name = "continuous", .ours = rx::match_continuous, .theirs = sc::match_continuous},
    {.name = "prev_avail", .ours = rx::match_prev_avail, .theirs = sc::match_prev_avail},
    {.name = "not_null", .ours = rx::match_not_null, .theirs = sc::match_not_null},
    {.name   = "continuous|not_null",
     .ours   = rx::match_continuous | rx::match_not_null,
     .theirs = sc::match_continuous | sc::match_not_null},
    {.name   = "prev_avail|not_null",
     .ours   = rx::match_prev_avail | rx::match_not_null,
     .theirs = sc::match_prev_avail | sc::match_not_null},
    {.name = "not_eol", .ours = rx::match_not_eol, .theirs = sc::match_not_eol},
    {.name = "not_eow", .ours = rx::match_not_eow, .theirs = sc::match_not_eow},
    {.name = "not_eol|not_eow", .ours = rx::match_not_eol | rx::match_not_eow, .theirs = sc::match_not_eol | sc::match_not_eow},
  };


  template <typename Regex, typename Match, typename Flags>
  outcome run(const Regex& re, const std::string& input, std::size_t from, Flags flags)
  {
    outcome     out;
    Match       m;
    const auto  first {input.begin() + static_cast<std::ptrdiff_t>(from)};
    out.searched = regex_search(first, input.end(), m, re, flags);
    if (out.searched) {
      out.pos = static_cast<long>(m.position(0));
      out.len = static_cast<long>(m.length(0));
      for (std::size_t i {1}; i < m.size(); ++i) {
        out.groups.emplace_back(m[i].matched ? static_cast<long>(m.position(i)) : -1L,
                                m[i].matched ? static_cast<long>(m.length(i)) : -1L);
      }
    }
    out.matched = regex_match(first, input.end(), re, flags);
    return out;
  }

  // The documented nullable-loop capture: everything equal but groups, every differing group zero-width in std.
  bool is_empty_iteration_signature(const outcome& compat, const outcome& local)
  {
    if (compat.searched != local.searched || compat.pos != local.pos || compat.len != local.len
        || compat.matched != local.matched || compat.groups.size() != local.groups.size()) {
      return false;
    }
    bool differs {false};
    for (std::size_t i {0}; i < compat.groups.size(); ++i) {
      if (compat.groups[i] != local.groups[i]) {
        differs = true;
        if (local.groups[i].second != 0) {
          return false;
        }
      }
    }
    return differs;
  }

  // libc++ ignores match_prev_avail for `^` and never lets `\b` hold on an attempt at last.
  bool std_honors_prev_avail()
  {
    const std::string ba {"ba"};
    const std::string a {"a"};
    return !std::regex_search(ba.begin() + 1, ba.end(), std::regex {"^a"}, sc::match_prev_avail)
           && std::regex_search(a.end(), a.end(), std::regex {R"(\b)"}, sc::match_prev_avail);
  }

  std::vector<std::string> read_lines(const char* path)
  {
    std::vector<std::string> lines;
    std::ifstream            in(path);
    std::string              line;
    while (std::getline(in, line)) {
      lines.push_back(line);
    }
    return lines;
  }

} // namespace

int main(int argc, char** argv)
{
  if (argc < 3 || argc > 5) {
    static_cast<void>(std::fprintf(stderr, "usage: %s <patterns> <inputs> [stride] [pin|nopin]\n", argv[0]));
    return 2;
  }
  const std::size_t stride {argc >= 4 ? static_cast<std::size_t>(std::strtoul(argv[3], nullptr, 10)) : 1U};
  const bool        pin {argc < 5 || std::string_view {argv[4]} != "nopin"};
  const std::vector<std::string> patterns {read_lines(argv[1])};
  const std::vector<std::string> inputs {read_lines(argv[2])};
  const bool                     prev_oracle {std_honors_prev_avail()};
  long long compared {0};
  long long tolerated {0};
  long long no_oracle {0};
  long long serious {0};
  for (std::size_t p {0}; p < patterns.size(); p += stride == 0U ? 1U : stride) {
    const std::string& pattern {patterns[p]};
    rc::regex          compat;
    std::regex         local;
    try {
      compat = rc::regex(pattern);
      local  = std::regex(pattern);
    }
    catch (const std::regex_error&) {
      continue; // routing and acceptance are exhaustive_compat's; this checks the flags on what both accept
    }
    for (const std::string& input : inputs) {
      for (const flag_set& set : flag_sets) {
        const bool        prev {(set.ours & rx::match_prev_avail) != 0U};
        const std::size_t from {prev ? 1U : 0U};
        if (prev && (input.empty() || !prev_oracle)) {
          no_oracle += input.empty() ? 0 : 1;
          continue;
        }
        ++compared;
        outcome ours;
        outcome theirs;
        try {
          ours   = run<rc::regex, rc::smatch>(compat, input, from, set.ours);
          theirs = run<std::regex, std::smatch>(local, input, from, set.theirs);
        }
        catch (const std::regex_error&) {
          ++no_oracle; // std gave up mid-match (error_complexity / error_stack): no answer to compare
          continue;
        }
        if (ours == theirs) {
          continue;
        }
        if (is_empty_iteration_signature(ours, theirs)) {
          ++tolerated;
          continue;
        }
        if (serious < 25) {
          static_cast<void>(std::fprintf(stderr, "DIVERGE pattern=%s input=%s flags=%s | compat(%d %ld+%ld match=%d) std(%d %ld+%ld match=%d)\n",
                                         pattern.c_str(), input.c_str(), set.name, static_cast<int>(ours.searched), ours.pos, ours.len,
                                         static_cast<int>(ours.matched), static_cast<int>(theirs.searched), theirs.pos, theirs.len,
                                         static_cast<int>(theirs.matched)));
        }
        ++serious;
      }
    }
  }
  static_cast<void>(std::printf("exhaustive-compat-flags: %lld cases compared (stride %zu, %zu flag sets), "
                                "nullable-loop capture=%lld, no oracle=%lld, serious=%lld\n",
                                compared, stride, std::size(flag_sets), tolerated, no_oracle, serious));
  if (compared == 0) {
    static_cast<void>(std::fprintf(stderr, "exhaustive-compat-flags: FAIL -- nothing compared: a run that did not take place\n"));
    return 1;
  }
  if (pin && stride == 1U && prev_oracle) {
    static constexpr long long cases_at_default_tier {25722130}; //!< EC_K=4, EC_N=6, every flag set, libstdc++
    static constexpr long long capture_at_default_tier {22680};  //!< the nullable-loop capture class, same tier
    if (compared != cases_at_default_tier || tolerated != capture_at_default_tier) {
      static_cast<void>(std::fprintf(stderr,
                                     "exhaustive-compat-flags: FAIL -- %lld cases and %lld nullable-loop captures, pinned "
                                     "at %lld and %lld for the default tier (EC_K=4, EC_N=6): the space or the class "
                                     "changed; find out why before re-pinning\n",
                                     compared, tolerated, cases_at_default_tier, capture_at_default_tier));
      return 1;
    }
    static_cast<void>(std::printf("exhaustive-compat-flags: space and class pinned (default tier)\n"));
  }
  return serious == 0 ? 0 : 1;
}
