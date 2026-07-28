// The per-regex membership-row "filled" flags live in one atomic word, one bit per row, with a vector
// carrying whatever does not fit (regex_immutables::row_ready / set_row_ready). Patterns with few byte
// classes — nearly all of them — never touch the vector, which is the point: it costs no allocation.
//
// These probes drive a pattern PAST that word so the overflow half runs, and pin that both halves answer
// the same thing: rows are filled on first use of their class, so a pattern whose classes are indexed
// either side of the boundary must still match exactly what the same pattern matches with the routes
// unaided. A flag read from the wrong half would either re-fill a live row or hand back an unfilled one.
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  // The two members of branch `i`'s class: an ordered pair (lo, hi) drawn from 'a'..'z', so every branch
  // interns a distinct two-byte class and none of them is an inverted range.
  constexpr char class_lo(std::size_t i)
  {
    return static_cast<char>('a' + (i / 25));
  }

  constexpr char class_hi(std::size_t i)
  {
    return static_cast<char>('a' + (i / 25) + 1 + (i % 25));
  }

  // A pattern with `n` DISTINCT byte classes, [ab] [ac] [ad] …, each interning separately. Alternation
  // keeps them all reachable, so a search visits classes on both sides of the bit-word boundary.
  //
  // One interned class per branch, verified rather than assumed: 64 branches compile to 64 byte classes
  // and 96 to 96, so the 96 these probes use put flag indices 64..95 in the overflow vector. If interning
  // ever collapses these bodies the probes go vacuous rather than failing, which is why the count is
  // stated here — re-check it before trusting a green run after a frontend change.
  std::string many_distinct_classes(std::size_t n)
  {
    std::string pat;
    for (std::size_t i {0}; i < n; ++i) {
      if (i != 0) {
        pat += '|';
      }
      pat += '[';
      pat += class_lo(i);
      pat += class_hi(i);
      pat += ']';
      pat += static_cast<char>('0' + static_cast<char>(i % 10)); // a literal keeps each branch distinct
    }
    return pat;
  }
} // namespace

// Probe 1 — a pattern whose class count exceeds the bit word. The last branch's class necessarily sits in
// the overflow vector, and it must still match; a flag read from the wrong half would drop it.
TEST(membership_row_overflow_beyond_bit_word_still_matches)
{
  constexpr std::size_t branches {96}; // > row_ready_bit_capacity (64)
  const real::regex     rx(many_distinct_classes(branches));

  // The final branch is the deepest into the overflow run.
  const std::string subject {std::string("...") + class_lo(branches - 1)
                             + static_cast<char>('0' + static_cast<char>((branches - 1) % 10)) + "..."};
  const auto        m       {rx.search(subject)};
  EXPECT(m.matched());
  EXPECT(m.start(0) == 3);
}

// Probe 2 — the two halves agree. Every branch is driven in turn, so classes indexed below the boundary
// (bit word) and at or above it (vector) are each filled and read on their own path.
TEST(membership_row_overflow_every_branch_across_the_boundary)
{
  constexpr std::size_t branches {96};
  const real::regex     rx(many_distinct_classes(branches));

  for (std::size_t i {0}; i < branches; ++i) {
    const std::string subject {std::string("..") + class_lo(i)
                               + static_cast<char>('0' + static_cast<char>(i % 10)) + ".."};
    const auto        m       {rx.search(subject)};
    EXPECT(m.matched());
    EXPECT(m.start(0) == 2);
  }
}

// Probe 3 — a walk over one regex reuses the flags across searches, which is what makes them worth
// caching. Same answer whether a row is being filled for the first time or read back on a later match.
TEST(membership_row_overflow_walk_reuses_filled_rows)
{
  constexpr std::size_t branches {96};
  const real::regex     rx(many_distinct_classes(branches));

  std::string haystack;
  for (std::size_t i {0}; i < branches; ++i) {
    haystack += class_lo(i);
    haystack += static_cast<char>('0' + static_cast<char>(i % 10));
    haystack += '.';
  }

  std::size_t found {0};
  for ([[maybe_unused]] const auto& m : rx.find_iter(haystack)) {
    ++found;
  }
  EXPECT(found == branches);
}

// Probe 4 — a copied regex gets a fresh, unbuilt cache, so its flags must start clear even though the
// source's were set. Copying a warmed regex and matching through the copy would answer from unfilled rows
// if the bit word were carried over.
TEST(membership_row_overflow_copy_starts_with_clear_flags)
{
  constexpr std::size_t branches {96};
  const real::regex     rx(many_distinct_classes(branches));

  const std::string subject {std::string("..") + class_lo(branches - 1)
                             + static_cast<char>('0' + static_cast<char>((branches - 1) % 10)) + ".."};
  EXPECT(rx.search(subject).matched()); // warm the source's rows

  // The copy is the subject under test, not an incidental one: what is being pinned is that copying a
  // warmed regex yields clear flags. NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
  const real::regex copy {rx};
  const auto        m    {copy.search(subject)};
  EXPECT(m.matched());
  EXPECT(m.start(0) == 2);
}
