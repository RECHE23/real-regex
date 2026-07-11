// Byte-identity net (structural). A corpus of patterns that use NO scoped flags is compiled and its
// programs are folded into one hash, pinned here. Adding scoped-flag support must leave every
// non-scoped program byte-for-byte unchanged, so this hash must not move; if it does, a refactor
// changed a compiled program it should not have. Later scoped-flag work reuses this exact net.
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  // A stable FNV-1a fold over a program's instruction stream (fixed-width fields → deterministic
  // across platforms). We hash every region (main + any lookaround sub-programs).
  std::uint64_t hash_program(const real::regex& rx)
  {
    std::uint64_t                                  h {0xcbf29ce484222325ULL};
    const auto                                     mix = [&h](std::uint64_t v) {
                                                           h = (h ^ v) * 0x100000001b3ULL;
                                                         };
    const std::vector<real::detail::program_view> views {rx.raw_program()};
    for (const real::detail::program_view& view : views) {
      mix(view.code.size());
      for (const real::detail::instr& in : view.code) {
        mix(static_cast<std::uint64_t>(in.op));
        mix(in.arg8);
        mix(in.arg16);
        mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(in.primary_target)));
        mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(in.secondary_target)));
      }
    }
    return h;
  }

  // Representative non-scoped patterns across the syntax. Do NOT add scoped-flag patterns here — this
  // corpus exists to prove the scoped-flags work does not perturb ordinary programs.
  struct corpus_entry
  {
    std::string_view pattern;
    real::flags      flags;
  };

  constexpr real::flags none {real::flags::none};

  const std::vector<corpus_entry>& corpus()
  {
    static const std::vector<corpus_entry> c {
      {.pattern = "abc", .flags = none},
      {.pattern = "a|bb|ccc", .flags = none},
      {.pattern = "[a-z0-9_]+", .flags = none},
      {.pattern = R"([^\d\s]*)", .flags = none},
      {.pattern = "a.c", .flags = none},
      {.pattern = "(a)(b)(c)", .flags = none},
      {.pattern = "(?:ab)+c?", .flags = none},
      {.pattern = R"((?P<year>\d{4})-(?P<mon>\d{2}))", .flags = none},
      {.pattern = R"(^\w+@\w+$)", .flags = none},
      {.pattern = "a{2,5}b{3}", .flags = none},
      {.pattern = "colou?r", .flags = none},
      {.pattern = R"(\bword\b)", .flags = none},
      {.pattern = R"((?=foo)f\w+)", .flags = none},
      {.pattern = R"((?<=\$)\d+)", .flags = none},
      {.pattern = R"([\x41-\x5a]+)", .flags = none},
      {.pattern = R"(\d{4}-\d{2}-\d{2})", .flags = none},
      {.pattern = "(cat|dog|bird)s?", .flags = none},
      {.pattern = R"(é+\w)", .flags = none},
      {.pattern = "(?i)Hello", .flags = none}, // a global inline flag prefix
      {.pattern = R"(a\N{U+0042}c)", .flags = none},
      {.pattern = "[[:alpha:]]+", .flags = none},
      {.pattern = ".*", .flags = real::flags::bytes},
      {.pattern = "[^;]+", .flags = real::flags::bytes},
      {.pattern = "foo", .flags = real::flags::icase},
    };
    return c;
  }

  // Update this ONLY when a deliberate program change is intended (and never as a side effect of the
  // scoped-flags work). It is printed by the test on mismatch so the new value is easy to read off.
  // Moved for wagon 4 (site 2): `.` (corpus entry "a.c") now compiles its non-ASCII branches via the
  // canonical utf8_range_sequences splitting (emit_class_codepoints), narrowing E0/ED/F0/F4's first
  // continuation byte to reject overlong/surrogate/out-of-range encodings -- more branches than the
  // old flat 4-branch emit_codepoint_class shape, a deliberate, intended program change.
  constexpr std::uint64_t kGolden {0x4fdd7d76f94da5f9ULL};

  TEST(non_scoped_programs_are_byte_identical)
  {
    std::uint64_t aggregate {0xcbf29ce484222325ULL};
    for (const corpus_entry& e : corpus()) {
      const real::regex rx {e.pattern, e.flags};
      aggregate ^= hash_program(rx);
    }
    if (aggregate != kGolden) {
      std::printf("  byte-identity: aggregate program hash is 0x%016llx (golden 0x%016llx)\n",
                  static_cast<unsigned long long>(aggregate), static_cast<unsigned long long>(kGolden));
    }
    EXPECT_EQ(aggregate, kGolden);
  }
} // namespace
