// Public first-byte dispatch accessors: has_first_byte_set / unique_first_byte /
// may_start_with. These are the sound surface a lexer's rule dispatch builds on
// (SciLex 5b replaces its textual leading_byte heuristic with this).
//
// Soundness contract: may_start_with(b) == false is a GUARANTEE that no non-empty
// match begins with b; true is a conservative superset. When no first-byte set is
// usable (an empty match is possible), every byte is allowed (foolproof).
#include <optional>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

// Probe 1 — the SQL pivot: an icase literal folds BOTH cases into the set, so a
// lexer can still bucket it by first byte. This is exactly the case SciLex's
// textual leading_byte gives up on (any flag -> general list); REAL knows better.
TEST(first_bytes_icase_literal_folds_both_cases)
{
  const real::regex rx("select", real::flags::icase);
  EXPECT(rx.has_first_byte_set());
  EXPECT(rx.may_start_with('s'));
  EXPECT(rx.may_start_with('S'));
  EXPECT(!rx.unique_first_byte().has_value()); // two possible first bytes
}

// Probe 2 — a plain literal has a unique first byte; other bytes are rejected.
TEST(first_bytes_plain_literal_is_unique)
{
  const real::regex                  rx("if");
  const std::optional<unsigned char> first {rx.unique_first_byte()};
  EXPECT(first.has_value() && *first == static_cast<unsigned char>('i'));
  EXPECT(rx.may_start_with('i'));
  EXPECT(!rx.may_start_with('f')); // 'f' never starts a match of "if"
}

// Probe 3 — a class loop yields the exact class as the first-byte set.
TEST(first_bytes_class_loop_is_the_class)
{
  const real::regex rx("[a-z]+");
  EXPECT(rx.has_first_byte_set());
  EXPECT(!rx.unique_first_byte().has_value());
  EXPECT(rx.may_start_with('a'));
  EXPECT(!rx.may_start_with('A'));
  EXPECT(!rx.may_start_with('0'));
}

// Probe 4 — a nullable pattern (empty match possible) disables the filter, so
// may_start_with is true everywhere (the foolproof degraded mode).
TEST(first_bytes_nullable_pattern_allows_everything)
{
  const real::regex rx("a?");
  EXPECT(!rx.has_first_byte_set());
  EXPECT(!rx.unique_first_byte().has_value());
  EXPECT(rx.may_start_with('a'));
  EXPECT(rx.may_start_with('z'));
}

// Probe 5 — the empty pattern: same degraded mode, every byte allowed.
TEST(first_bytes_empty_pattern_allows_everything)
{
  const real::regex rx("");
  EXPECT(!rx.has_first_byte_set());
  EXPECT(rx.may_start_with('\0'));
  EXPECT(rx.may_start_with('a'));
  EXPECT(rx.may_start_with(static_cast<unsigned char>(0xff)));
}

// Probe 6 — '.' poses a valid multi-byte set (not a degenerate), so it is
// dispatchable yet has no unique first byte.
TEST(first_bytes_dot_is_a_valid_multibyte_set)
{
  const real::regex rx(".");
  EXPECT(rx.has_first_byte_set());
  EXPECT(!rx.unique_first_byte().has_value());
  EXPECT(rx.may_start_with('a'));
}

// Probe 7 — the accessors are usable at compile time (static_regex / constexpr).
namespace {
  constexpr real::static_regex<"abc"> abc_rx;
  static_assert(abc_rx.has_first_byte_set());
  static_assert(abc_rx.unique_first_byte() == std::optional<unsigned char> {static_cast<unsigned char>('a')});
  static_assert(abc_rx.may_start_with('a'));
  static_assert(!abc_rx.may_start_with('b'));
} // namespace

// Probe 8 — empty_match_possible (the nullable gate, exposed for embedders). Conservative:
// assertions pass through, so a pattern that is only anchors (`^$`) is flagged nullable.
TEST(empty_match_possible_hint)
{
  for (const char* p : {"a*", "(abc)?", "x*y?", "^$"}) {
    EXPECT(real::regex(p).raw_program().hints.empty_match_possible);
  }
  for (const char* p : {"foo", "a+", R"(\d{4})", "[a-z]+"}) {
    EXPECT(!real::regex(p).raw_program().hints.empty_match_possible);
  }
}
