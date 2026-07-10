// Structural + invariant guard for the generated binary-property tables (unicode_binprop.hpp). The full
// exhaustive-vs-UCD oracle lives in the Python regen guard (test_unicode_binprop_regen, parsed straight
// from the bundled DerivedCoreProperties.txt / PropList.txt / emoji-data.txt) -- here we check what is
// checkable in C++: every property's own table is sorted and disjoint, known code points land where the
// UCD says (ASCII boundaries, an astral emoji, a codepoint that is several properties at once -- unlike
// General_Category/Script, binary properties are NOT a partition), and the loose alias resolver works.
// Wired at ast.hpp::resolve_property; the parser-level tests live in
// tests/frontend/test_unicode_property_class.cpp.
#include <cstddef>

#include <sciforge/test/framework.hpp>
#include "real/unicode/unicode_binprop.hpp"

using namespace real::detail;

TEST(binprop_tables_are_sorted_and_disjoint)
{
  // Not a partition (a code point can be several properties, or none) -- so each property's OWN table
  // must be sorted/disjoint, unlike script_ranges' single whole-code-space invariant.
  for (const std::span<const code_range>& ranges : binprop_ranges) {
    for (std::size_t i = 0; i < ranges.size(); ++i) {
      EXPECT(ranges[i].lo <= ranges[i].hi);
      if (i > 0) {
        EXPECT(ranges[i - 1].hi < ranges[i].lo);
      }
    }
  }
}

TEST(binprop_probes_known_code_points)
{
  EXPECT(is_binprop_cp(binprop::Alphabetic, U'A'));
  EXPECT(!is_binprop_cp(binprop::Alphabetic, U'3'));
  EXPECT(is_binprop_cp(binprop::White_Space, U' '));
  EXPECT(!is_binprop_cp(binprop::White_Space, U'x'));
  EXPECT(is_binprop_cp(binprop::Hex_Digit, U'F'));
  EXPECT(!is_binprop_cp(binprop::Hex_Digit, U'G'));
  EXPECT(is_binprop_cp(binprop::ASCII_Hex_Digit, U'a'));
  EXPECT(!is_binprop_cp(binprop::ASCII_Hex_Digit, U'g')); // 'g' is not hex, ASCII or otherwise
  EXPECT(is_binprop_cp(binprop::Uppercase, U'A'));
  EXPECT(!is_binprop_cp(binprop::Uppercase, U'a'));
  EXPECT(is_binprop_cp(binprop::Lowercase, U'a'));
  EXPECT(is_binprop_cp(binprop::Dash, U'-'));
  EXPECT(is_binprop_cp(binprop::Math, U'+'));
  EXPECT(is_binprop_cp(binprop::Quotation_Mark, U'"'));
  // ID_Start vs ID_Continue: '_' continues an identifier but cannot start one -- proves these are two
  // genuinely distinct tables, not aliases of each other.
  EXPECT(!is_binprop_cp(binprop::ID_Start, U'_'));
  EXPECT(is_binprop_cp(binprop::ID_Continue, U'_'));
  // Astral plane: an emoji beyond the BMP (U+1F600 GRINNING FACE).
  EXPECT(is_binprop_cp(binprop::Emoji, 0x1F600));
  EXPECT(!is_binprop_cp(binprop::Emoji, U'A'));
  // Not a partition: 'A' satisfies several properties simultaneously.
  EXPECT(is_binprop_cp(binprop::Alphabetic, U'A'));
  EXPECT(is_binprop_cp(binprop::Uppercase, U'A'));
  EXPECT(is_binprop_cp(binprop::ID_Start, U'A'));
  EXPECT(is_binprop_cp(binprop::Cased, U'A'));
  // And a code point can satisfy none of a given property (a digit is none of the above four).
  EXPECT(!is_binprop_cp(binprop::Alphabetic, U'5'));
  EXPECT(!is_binprop_cp(binprop::Uppercase, U'5'));
  EXPECT(!is_binprop_cp(binprop::ID_Start, U'5'));
  EXPECT(!is_binprop_cp(binprop::Cased, U'5'));
}

TEST(binprop_resolve_and_aliases)
{
  // resolve_binprop takes an ALREADY loose-normalized key (lowercase, no _/-/space) -- same contract as
  // resolve_gc/resolve_script; the case-/hyphen-/space-insensitivity itself is the parser's loose_key(),
  // exercised at tests/frontend/test_unicode_property_class.cpp's `\p{ WHITE-space }` case.
  EXPECT(resolve_binprop("alphabetic") == binprop::Alphabetic);
  EXPECT(resolve_binprop("whitespace") == binprop::White_Space); // loose key drops the underscore
  EXPECT(resolve_binprop("emoji") == binprop::Emoji);
  EXPECT(resolve_binprop("nosuchbinaryproperty") == binprop::count);
  EXPECT(resolve_binprop("") == binprop::count);
}
