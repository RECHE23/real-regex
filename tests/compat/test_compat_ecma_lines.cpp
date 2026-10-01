//! Under the drop-in's ECMAScript grammar a multiline `^` and `$` end a line at a carriage return as well as at a
//! line feed, as libstdc++ and libc++ do; REAL's own multiline anchors keep the line feed only. Every evaluator
//! decides these assertions itself -- the VM and its lookaround sub-VM, the bounded backtracker, the forward and
//! reverse lazy DFAs, the line-start skip, the one-pass extraction, the streaming probe -- so each one has a
//! witness here, and the drop-in is compared with the host's std::regex under every route configuration.
#include <cstddef>
#include <cstdint>
#include <regex>
#include <string>
#include <vector>

#include <sciforge/test/framework.hpp>
#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>
#include "real/compat/std/regex.hpp"

namespace rc = real::compat;

namespace {

  constexpr real::flags ecma_ml {real::flags::bytes | real::flags::ecma | real::flags::multiline};

  std::string rep(const std::string& s,
                  int                n)
  {
    std::string out;
    for (int i {0}; i < n; ++i) {
      out += s;
    }
    return out;
  }

  // Every group's position and length of every match, -1 for a group that did not take part.
  template <typename It, typename Re>
  std::vector<long> all_spans(const std::string& s,
                              const Re&          re)
  {
    std::vector<long> out;
    for (It it {s.begin(), s.end(), re}, end; it != end; ++it) {
      for (std::size_t g {0}; g < it->size(); ++g) {
        out.push_back((*it)[g].matched ? static_cast<long>(it->position(g)) : -1L);
        out.push_back((*it)[g].matched ? static_cast<long>(it->length(g)) : -1L);
      }
    }
    return out;
  }

  // all_spans for the std oracle, over a copy holding one `\n` before the subject: libc++'s regex_iterator reads the
  // byte before its range after an empty multiline match at its start. A `^` holds at the start already, so the
  // guard byte changes no answer.
  std::vector<long> std_spans(const std::string& s,
                              const std::regex&  re)
  {
    const std::string guarded {"\n" + s};
    std::vector<long> out;
    for (std::sregex_iterator it {guarded.begin() + 1, guarded.end(), re}, end; it != end; ++it) {
      for (std::size_t g {0}; g < it->size(); ++g) {
        out.push_back((*it)[g].matched ? static_cast<long>(it->position(g)) : -1L);
        out.push_back((*it)[g].matched ? static_cast<long>(it->length(g)) : -1L);
      }
    }
    return out;
  }

  // Restores the route seams however the test leaves.
  struct routes_restored {
    routes_restored()                                  = default;
    routes_restored(const routes_restored&)            = delete;
    routes_restored& operator=(const routes_restored&) = delete;
    routes_restored(routes_restored&&)                 = delete;
    routes_restored& operator=(routes_restored&&)      = delete;
    ~routes_restored()
    {
      real::detail::lazy_dfa_route_disabled()          = false;
      real::detail::bounded_backtrack_route_disabled() = false;
    }
  };
} // namespace

// Every subject over {a, \r, \n} up to 5 bytes, alone and behind a 600-byte prefix that puts the search on the
// lazy DFAs, with the lazy DFA and the bounded backtracker each on and off.
TEST(compat_ecma_multiline_lines_end_at_cr_as_std_on_every_route)
{
  const char* const        patterns[] {"^",      "$",      "^$",     "^a",     "a$",    "^a$",    "$\\r",      "$\\n",   "^\\r",   "\\r$",
                                       "^\\n",   "\\n$",   "a(?=$)", "(?=^)a", "(?!^)a", "a(?!$)", "^\\w*$",    "^.*$",   "^.+",    ".+$",
                                       "^(a)",   "(a)$",   "(^|a)a", "a($|b)", "^[^a]", "[^a]$",  "(?:^a|a$)+", "^a*$|^b", "^\\s*$"};
  const std::string        pad        {std::string(300, 'x') + "\n" + std::string(300, 'y') + "\r\n"};
  std::vector<std::string> subjects;
  const char               alphabet[] {'a', '\r', '\n'};
  for (unsigned len {0}, total {1}; len <= 5; ++len, total *= 3U) {
    for (unsigned k {0}; k < total; ++k) {
      std::string s;
      for (unsigned i {0}, x {k}; i < len; ++i, x /= 3U) {
        s += alphabet[x % 3U];
      }
      subjects.push_back(s);
      subjects.push_back(pad + s);
    }
  }
  const routes_restored restore;
  std::size_t           compared {0};
  std::size_t           diverged {0};
  for (const char* const p : patterns) {
    const rc::regex  compat {p, rc::regex_constants::ECMAScript | rc::regex_constants::multiline};
    const std::regex ref    {p, std::regex::ECMAScript | std::regex::multiline};
    EXPECT(compat.uses_real());
    for (const std::string& s : subjects) {
      const std::vector<long> want {std_spans(s, ref)};
      for (int cfg {0}; cfg < 4; ++cfg) {
        real::detail::lazy_dfa_route_disabled()          = (cfg & 1) != 0;
        real::detail::bounded_backtrack_route_disabled() = (cfg & 2) != 0;
        diverged                                        += all_spans<rc::sregex_iterator>(s, compat) != want ? 1U : 0U;
        ++compared;
      }
    }
  }
  EXPECT_EQ(compared, std::size_t {29} *728U * 4U);
  EXPECT_EQ(diverged, std::size_t {0});
}

// Short subjects run on the bounded backtracker and the VM; a lookaround's sub-VM decides the anchor inside it.
TEST(ecma_multiline_anchors_on_short_subjects_and_inside_lookarounds)
{
  EXPECT_EQ(real::regex("^b", ecma_ml).search("a\rb").start(), std::size_t {2});
  EXPECT_EQ(real::regex("a$", ecma_ml).search("a\rb").start(), std::size_t {0});
  EXPECT_EQ(real::regex("(?=^a)a", ecma_ml).search("b\ra").start(), std::size_t {2});
  EXPECT_EQ(real::regex("a(?=$)", ecma_ml).search("a\rb").start(), std::size_t {0});
}

// Long subjects run on the forward lazy DFA, anchored and unanchored, and on the reverse DFA for the starts;
// nullable or class-led shapes go forward then reverse, and the reverse DFA decides ^ and $ itself.
TEST(ecma_multiline_anchors_on_the_lazy_dfas)
{
  const std::string cr_lines {rep("ab\r", 300) + "cd"}; // lines end at a bare \r only
  EXPECT_EQ(real::regex("^\\w+", ecma_ml).count_matches(cr_lines), std::size_t {301});
  EXPECT_EQ(real::regex("\\w+$", ecma_ml).count_matches(cr_lines), std::size_t {301});
  EXPECT_EQ(real::regex("^ab$", ecma_ml).count_matches(cr_lines), std::size_t {300});
  EXPECT_EQ(real::regex("b$\\r^a", ecma_ml).count_matches(cr_lines), std::size_t {299});
  EXPECT_EQ(real::regex("\\w$", ecma_ml).count_matches(cr_lines), std::size_t {301});
  EXPECT_EQ(real::regex("^.*$", ecma_ml).count_matches(cr_lines), std::size_t {301});
  EXPECT_EQ(real::regex("[^a]$", ecma_ml).count_matches(cr_lines), std::size_t {301});
}

// The line-start skip takes a line after a bare \r as a candidate, and the empty line between the \r and the \n of
// a CRLF as one for a pattern that can start with \n.
TEST(ecma_multiline_line_start_skip_sees_cr)
{
  const real::regex                                                      re   {R"(^\w+)", ecma_ml};
  const real::detail::program_view                                       prog {re.raw_program()};
  real::detail::dynamic_storage::state_type                              state;
  real::detail::pike_vm<real::detail::dynamic_storage::state_type, true> vm   {prog, state};
  EXPECT_EQ(prog.hints.line_anchored, std::uint8_t {2});
  EXPECT_EQ(vm.next_candidate("x\r b\r c\r\ndd", 1, 0), std::size_t {9}); // " b", " c" and the CRLF's empty line passed over
  EXPECT_EQ(real::regex("^b", ecma_ml).count_matches(rep("a\rb\n", 300)), std::size_t {300});
  const std::string crlf {rep("ab\r\n", 300)};
  EXPECT_EQ(real::regex("^\\n", ecma_ml).count_matches(crlf), std::size_t {300});
  EXPECT_EQ(real::regex("^\\s", ecma_ml).count_matches(crlf), std::size_t {300});
}

// Group extraction on a window decides the anchors on the one-pass edges.
TEST(ecma_multiline_anchors_in_group_extraction)
{
  const std::string cr_lines {rep("ab\r", 300) + "cd"};
  const real::regex groups   {"^(a)(b)$", ecma_ml};
  std::size_t       seen     {0};
  for (const auto& m : groups.find_iter(cr_lines)) {
    seen += m.str(1) == "a" && m.str(2) == "b" ? 1U : 0U;
  }
  EXPECT_EQ(seen, std::size_t {300});
}

// Streaming: a `$` at the end of what has arrived may still be decided by a \r to come.
TEST(ecma_multiline_dollar_at_the_end_can_extend)
{
  EXPECT(real::regex("a$", ecma_ml).can_extend("a"));
  EXPECT(real::regex("a$b", ecma_ml).can_extend("a"));
}

// REAL's own multiline anchors keep the line feed only, on the VM and on the lazy DFA.
TEST(plain_multiline_anchors_ignore_cr)
{
  EXPECT(!real::regex("^a", real::flags::multiline).search("\ra").matched());
  EXPECT(!real::regex("a$", real::flags::multiline).search("a\r").matched());
  EXPECT_EQ(real::regex("^\\w+", real::flags::multiline).count_matches(rep("ab\r", 300)), std::size_t {1});
}
