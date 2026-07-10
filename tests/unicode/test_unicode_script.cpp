// Structural + invariant guard for the generated Script table (unicode_script.hpp). The full exhaustive-vs-UCD
// oracle lives in the Python regen guard (test_unicode_script_regen, byte-identity from the bundled Scripts.txt,
// cross-checked against the `regex` module at generation) — here we check what is checkable in C++: the table is
// a sorted, disjoint partition, and known code points land in the right Script. Wired at
// ast.hpp::resolve_property; the parser-level tests live in tests/frontend/test_unicode_property_class.cpp.
#include <cstddef>

#include <sciforge/test/framework.hpp>
#include "real/unicode/unicode_script.hpp"

using namespace real::detail;

TEST(script_partition_is_sorted_and_disjoint)
{
  const std::size_t n {sizeof(script_ranges) / sizeof(script_ranges[0])};
  for (std::size_t i = 0; i < n; ++i) {
    EXPECT(script_ranges[i].lo <= script_ranges[i].hi);
    if (i > 0) {
      EXPECT(script_ranges[i - 1].hi < script_ranges[i].lo); // strictly increasing, non-overlapping partition
    }
  }
}

TEST(script_probes_known_code_points)
{
  EXPECT(is_script_cp(script::Latin, U'A'));
  EXPECT(is_script_cp(script::Latin, U'é'));
  EXPECT(is_script_cp(script::Greek, U'α'));
  EXPECT(is_script_cp(script::Common, U' '));  // space is Common
  EXPECT(is_script_cp(script::Common, U'0'));  // ASCII digits are Common, not a "digit script"
  EXPECT(is_script_cp(script::Han, U'中'));
  EXPECT(is_script_cp(script::Hebrew, U'א'));
  // negatives — a script rejects code points of another
  EXPECT(!is_script_cp(script::Greek, U'A'));
  EXPECT(!is_script_cp(script::Latin, U'α'));
  EXPECT(!is_script_cp(script::Han, U'A'));
  // an unassigned code point has the Unknown script (the gaps in the partition)
  EXPECT(script_of(U'͸') == script::Unknown);
  EXPECT(is_script_cp(script::Unknown, U'͸'));
}
