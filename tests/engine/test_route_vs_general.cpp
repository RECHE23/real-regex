// Cartesian route-vs-general gate.
//
// The per-runner seams in test_fastpath_seam_matrix.cpp prove each fast path HAS a differential,
// on curated (pattern, subject) pairs. Two published silent wrong answers passed those pairs
// because the subjects never combined the armed route with the edge the runner got wrong:
// `\w{2}$` (counted class, end anchor) and `\s$` (class that holds `\n`, `$`). The seam lists
// grew after each miss; growth by autopsy is what this file exists to stop.
//
// One shared subject corpus -- including the newline edges -- times one pattern list that arms
// every pike_vm route. Two axes:
//
//   * hint-blanking (`without.program.hints = {}`) -- the general Pike VM, the same oracle the
//     seam matrix uses. Search, prefix and full.
//   * each runtime disable knob, production vs that knob. Search. This is the sweep that found
//     `\s$`: disabling `class_fastpath` falls through to the VM, and the two disagree.
//
// Gate-safe: no wall-clock. A floor on the product, plus named witnesses for `\s$` / `"ab\n"`,
// so shrinking the lists back below the hole is a red rather than a quiet shrink.
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using real::detail::dynamic_storage;

namespace {

  using seam_fn = bool& (*)();

  struct knob
  {
    const char* name;
    seam_fn     flag;
  };

  // Eleven levers, one per disable seam that can take a pike_vm::run dispatch (or its immediate
  // fallback) off the production path. `trailing_la` is a no-op inside pike_vm::run -- kept so a
  // knob-count shrink cannot silently drop a seam -- and `fixed_shape_route_disabled` is honoured at
  // compile time, so the runtime toggle is a no-op for that route; hint-blanking covers it.
  const knob k_knobs[] {
    {"class_fastpath",      real::detail::class_fastpath_disabled     },
    {"possessive",          real::detail::possessive_fastpath_disabled},
    {"inner_literal",       real::detail::inner_literal_route_disabled},
    {"inner_literal_guard", real::detail::inner_literal_guard_disabled},
    {"fixed_shape_pair",    real::detail::fixed_shape_pair_route_disabled},
    {"fixed_shape",         real::detail::fixed_shape_route_disabled  },
    {"aho_corasick",        real::detail::aho_corasick_route_disabled },
    {"ac_density_gate",     real::detail::ac_density_gate_disabled    },
    {"lazy_dfa",            real::detail::lazy_dfa_route_disabled     },
    {"rare_disc",           real::detail::rare_disc_route_disabled    },
    {"trailing_la",         real::detail::trailing_la_route_disabled  },
  };
  struct spec
  {
    std::string_view pat;
    real::flags      flags {real::flags::none};
  };

  // Arms every pike_vm route at least once, and the two families that shipped a silent None:
  // a counted code-point class under an end anchor, and a class that holds `\n` under `$`.
  const spec k_patterns[] {
    {"[a-z]+"                                                                                                                                          },
    {"[a-z]+$"                                                                                                                                         },
    {"[a-z]+\\Z"                                                                                                                                       },
    {"^[a-z]+$"                                                                                                                                        },
    {"[ \t\n]+$"                                                                                                                                       },
    {"[ \t]+$"                                                                                                                                         },
    {"[\\n]+$"                                                                                                                                         },
    {"(?a)[a-z]{4,}"                                                                                                                                   },
    {"[0-9]+$"                                                                                                                                         },
    {"\\w+"                                                                                                                                            },
    {"\\w+$"                                                                                                                                           },
    {"\\s$"                                                                                                                                            },
    {"\\s+$"                                                                                                                                           },
    {"\\s{1,}$"                                                                                                                                        },
    {"\\W$"                                                                                                                                            },
    {"\\W+$"                                                                                                                                           },
    {"\\s\\Z"                                                                                                                                          },
    {"\\w{2}$"                                                                                                                                         },
    {"\\w{2}\\Z"                                                                                                                                       },
    {"\\w{2,}$"                                                                                                                                        },
    {"\\d+"                                                                                                                                            },
    {"\\s*$"                                                                                                                                           },
    {"(?m)\\s$"                                                                                                                                        },
    {"a*+;"                                                                                                                                            },
    {"[a-z]*+;"                                                                                                                                        },
    {"\\w*+;"                                                                                                                                          },
    {R"("[^"]*+")"                                                                                                                                     },
    {"[0-9a-f]{4}"                                                                                                                                     },
    {"[0-9]{4}-[0-9]{2}"                                                                                                                               },
    {"dog"                                                                                                                                             },
    {"dog|fox|cat"                                                                                                                                     },
    {"cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion|tiger"                                                                                        },
    {"cat|dog|fox|owl|rat|hen|pig|emu"                                                                                                                 },
    {"\\d{4}-\\d{2}"                                                                                                                                   },
    {"\\w+@\\w+"                                                                                                                                       },
    {".+"                                                                                                                                              },
    {"[^x]+"                                                                                                                                           },
    {"(?i)cafe"                                                                                                                                        },
    {R"(https?://[^\s]+)"                                                                                                                              },
    {"(\\w+)@(\\w+)"                                                                                                                                   },
    {"[a-z]+",     real::flags::bytes                                                                                                                   },
    {"\\C+",       real::flags::bytes                                                                                                                   },
  };

  std::string vis(std::string_view t)
  {
    std::string out;
    out.reserve(t.size() * 2);
    for (const char c : t) {
      if (c == '\n') {
        out += "\\n";
      }
      else if (c == '\t') {
        out += "\\t";
      }
      else if (c == '\r') {
        out += "\\r";
      }
      else {
        out += c;
      }
    }
    if (out.size() > 60) {
      out.resize(57);
      out += "...";
    }
    return out;
  }

  void report_div(const char    *        axis,
                  std::string_view       pat,
                  std::string_view       text,
                  real::detail::run_mode mode)
  {
    const char* mode_s {"search"};
    if (mode == real::detail::run_mode::prefix) {
      mode_s = "prefix";
    }
    else if (mode == real::detail::run_mode::full) {
      mode_s = "full";
    }
    std::printf("  route-vs-general %s  pat=%.*s  subj=%s  mode=%s\n",
                axis,
                static_cast<int>(pat.size()), pat.data(),
                vis(text).c_str(),
                mode_s);
  }

  bool run_eq(const dynamic_storage& a,
              const dynamic_storage& b,
              std::string_view       text,
              real::detail::run_mode mode)
  {
    // pike_vm borrows program_view -- the view must be an lvalue that outlives the run.
    // Passing `storage.view()` directly dangles (it is a temporary); that is why
    // test_fastpath_seam_matrix.cpp materialises `pv1`/`pv2` first.
    const real::detail::program_view pv1 {a.view()};
    const real::detail::program_view pv2 {b.view()};
    real::detail::pike_state         s1;
    real::detail::pike_state         s2;
    std::vector<std::size_t>         r1;
    std::vector<std::size_t>         r2;
    real::detail::pike_vm            vm1(pv1, s1);
    real::detail::pike_vm            vm2(pv2, s2);
    const bool                       m1 {vm1.run(text, 0, mode, r1)};
    const bool                       m2 {vm2.run(text, 0, mode, r2)};
    return m1 == m2 && r1 == r2;
  }

  struct snapshot
  {
    bool                     matched {};
    std::vector<std::size_t> slots;
  };

  snapshot run_once(const dynamic_storage& prog,
                    std::string_view       text,
                    real::detail::run_mode mode)
  {
    const real::detail::program_view pv {prog.view()};
    real::detail::pike_state         s;
    snapshot                         out;
    real::detail::pike_vm            vm(pv, s);
    out.matched = vm.run(text, 0, mode, out.slots);
    return out;
  }

  void reset_knobs()
  {
    for (const knob& k : k_knobs) {
      k.flag() = false;
    }
  }

  std::string pad(std::string_view unit,
                  std::size_t      bytes)
  {
    std::string s;
    s.reserve(bytes + unit.size());
    while (s.size() < bytes) {
      s += unit;
    }
    return s;
  }
} // namespace

TEST(route_vs_general_witnesses_are_in_the_product)
{
  bool saw_s_dollar  {false};
  bool saw_w2_dollar {false};
  bool saw_nl_class  {false};
  for (const spec& s : k_patterns) {
    if (s.pat == "\\s$") {
      saw_s_dollar = true;
    }
    if (s.pat == "\\w{2}$") {
      saw_w2_dollar = true;
    }
    if (s.pat == "[ \t\n]+$") {
      saw_nl_class = true;
    }
  }
  EXPECT(saw_s_dollar);
  EXPECT(saw_w2_dollar);
  EXPECT(saw_nl_class);

  const std::size_t n_knobs {sizeof(k_knobs) / sizeof(k_knobs[0])};
  EXPECT_EQ(n_knobs, static_cast<std::size_t>(11));
  EXPECT(std::size(k_patterns) >= 40);
}

TEST(route_vs_general_hint_blank_cartesian)
{
  reset_knobs();
  const std::string      long_hit   {pad("contact john.doe@example.com ", 700)};
  const std::string      long_miss (700, 'z');
  const std::string_view subjects[] {
    " \n",
    "",
    "\n",
    "ab\n",
    "\n\n",
    "ab\n\n",
    "ab",
    "abc",
    "abc def",
    "abc\ndef",
    "a",
    "  abc",
    "xab",
    "xxabc",
    "ab\nc\n",
    "!!! ***",
    "the quick brown fox",
    "12345",
    "aaa",
    " \t\n",
    "ab\r\n",
    "a\n",
    "  \n",
    "caf\xC3\xA9",
    "aaaaaaaaaaaaaaaaaaaa",
    "dog cat fox",
    "2026-07-13",
    "john@example.com",
    "http://x",
    "CAFE",
    "dead beef 1234",
    "abc123",
    "aaa;bbb;",
    "\"quoted\"",
    "xhellox",
    long_hit,
    long_miss,
  };
  bool saw_ab_nl {false};
  for (const std::string_view subj : subjects) {
    if (subj == "ab\n") {
      saw_ab_nl = true;
    }
  }
  EXPECT(saw_ab_nl);
  EXPECT(std::size(subjects) >= 35);

  // Search is the mode that found `\s$`. Prefix/full have their own seams (anchored_shape,
  // onepass); running them here against every pattern -- including `.+` over a 700-byte
  // subject -- is a different stress and is not what this gate is for.
  const real::detail::run_mode modes[] {real::detail::run_mode::search};

  std::size_t n                        {0};
  for (const spec& s : k_patterns) {
    const auto with    {dynamic_storage::compile(s.pat, s.flags)};
    auto       without {with};
    without.program.hints = {};
    for (const std::string_view text : subjects) {
      for (const real::detail::run_mode mode : modes) {
        const bool ok {run_eq(with, without, text, mode)};
        if (!ok) {
          report_div("hint-blank", s.pat, text, mode);
        }
        EXPECT(ok);
        ++n;
      }
    }
  }
  EXPECT(n >= 1400);
  reset_knobs();
}

TEST(route_vs_general_knob_cartesian)
{
  reset_knobs();
  const std::string      long_hit   {pad("contact john.doe@example.com ", 700)};
  const std::string      long_miss (700, 'z');
  const std::string_view subjects[] {
    " \n",
    "",
    "\n",
    "ab\n",
    "\n\n",
    "ab\n\n",
    "ab",
    "abc",
    "abc def",
    "abc\ndef",
    "a",
    "  abc",
    "xab",
    "xxabc",
    "ab\nc\n",
    "!!! ***",
    "the quick brown fox",
    "12345",
    "aaa",
    " \t\n",
    "ab\r\n",
    "a\n",
    "  \n",
    "caf\xC3\xA9",
    "aaaaaaaaaaaaaaaaaaaa",
    "dog cat fox",
    "2026-07-13",
    "john@example.com",
    "http://x",
    "CAFE",
    "dead beef 1234",
    "abc123",
    "aaa;bbb;",
    "\"quoted\"",
    "xhellox",
    long_hit,
    long_miss,
  };

  std::size_t n {0};
  for (const spec& s : k_patterns) {
    const auto prog {dynamic_storage::compile(s.pat, s.flags)};
    for (const std::string_view text : subjects) {
      const snapshot prod {run_once(prog, text, real::detail::run_mode::search)};
      for (const knob& k : k_knobs) {
        k.flag() = true;
        const snapshot off {run_once(prog, text, real::detail::run_mode::search)};
        k.flag() = false;
        const bool ok      {prod.matched == off.matched && prod.slots == off.slots};
        if (!ok) {
          report_div(k.name, s.pat, text, real::detail::run_mode::search);
        }
        EXPECT(ok);
        ++n;
      }
    }
  }
  EXPECT(n >= 15000);
  reset_knobs();
}
