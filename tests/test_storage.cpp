// small_vec storage: the counters (size_/capacity_) must be std::size_t, never a
// type narrowed on InlineCapacity. small_vec spills to the heap and routinely holds
// far more than its inline capacity, so a uint8_t/uint16_t counter would truncate at
// 256/65536 — capacity wraps to 0, reserve() no-ops, the buffer is rewritten in place
// and back() indexes out of bounds. These tests pin the three runtime overflows that a
// truncating counter caused (deep eps stack, a wide-alternation thread list, many
// capture slots) and run them through the dynamic engine (real::regex), which is the
// path that uses small_vec; they are exercised under ASan/UBSan in `make sanitize`.
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

namespace {

  // A wide alternation of distinct, equal-length capturing branches:
  // "(z000)|(z001)|...|(z{n-1})". No branch is a prefix of another, so for input
  // "zDDD" exactly one branch (group DDD+1) matches — an unambiguous oracle that
  // mirrors Python re. All branches share the first byte 'z', so at the seed
  // position the engine parks one live thread per branch (> n threads at once).
  std::string wide_capturing_alternation(int branch_count)
  {
    std::string pattern;
    for (int i = 0; i < branch_count; ++i) {
      if (i != 0) {
        pattern += '|';
      }
      pattern += "(z";
      pattern += static_cast<char>('0' + ((i / 100) % 10));
      pattern += static_cast<char>('0' + ((i / 10) % 10));
      pattern += static_cast<char>('0' + (i % 10));
      pattern += ')';
    }
    return pattern;
  }
} // namespace

// ─── A. small_vec directly: the spill must not truncate the counters ───────────

// The inline buffer is value-initialized only at compile time (run time leaves it
// uninitialized and writes each slot via placement-new). This constexpr exercise pins
// that the compile-time path stays correct: the union's inline std::array must be the
// active, initialized member so push_back (assignment) and assign work in a constant
// expression. A regression in the Storage activation makes this fail to compile.
constexpr bool small_vec_constexpr_roundtrip()
{
  real::detail::small_vec<std::size_t, 8> v;
  v.push_back(3);
  v.push_back(7);
  if (v.size() != 2U || v[0] != 3U || v[1] != 7U || v.back() != 7U) { return false; }
  v.assign(5, 42U); // constexpr assign path: fill the (alive) inline buffer
  if (v.size() != 5U) { return false; }
  for (std::size_t i = 0; i < 5; ++i) {
    if (v[i] != 42U) { return false; }
  }
  v.clear();
  v.push_back(9);
  return v.size() == 1U && v[0] == 9U;
}

static_assert(small_vec_constexpr_roundtrip(), "small_vec must work in a constant expression");

TEST(small_vec_constexpr_roundtrip_runtime)
{
  EXPECT(small_vec_constexpr_roundtrip()); // same body, also at run time (uninitialized buffer path)
}

TEST(small_vec_spills_past_inline_buffer_preserving_all_elements)
{
  real::detail::small_vec<int, 32> v;
  for (int i = 0; i < 300; ++i) {
    v.push_back(i);
  }
  EXPECT_EQ(v.size(), 300U); // not 44 (300 mod 256): a truncating counter is the bug
  EXPECT_EQ(v.back(), 299);
  bool all_present {true};
  for (int i = 0; i < 300; ++i) {
    all_present = all_present && v[static_cast<std::size_t>(i)] == i;
  }
  EXPECT(all_present);
}

TEST(small_vec_counter_does_not_wrap_at_exactly_256)
{
  // 256 is the trap multiple: a uint8_t size_ wraps to 0 here, so back() would read
  // heap_ptr[size_ - 1] == heap_ptr[-1] (heap-buffer-underflow under ASan) and empty()
  // would lie. With std::size_t counters the buffer simply spans 256 elements.
  real::detail::small_vec<std::size_t, 32> v;
  for (std::size_t i = 0; i < 256; ++i) {
    v.push_back(i);
  }
  EXPECT_EQ(v.size(), 256U);
  EXPECT(!v.empty());
  EXPECT_EQ(v.back(), 255U);
  EXPECT_EQ(v[0], 0U);
  EXPECT_EQ(v[128], 128U);
}

TEST(small_vec_handles_512_elements)
{
  real::detail::small_vec<int, 64> v;
  for (int i = 0; i < 512; ++i) {
    v.push_back(i * 2);
  }
  EXPECT_EQ(v.size(), 512U); // 512 mod 256 == 0 would also wrap a narrow counter
  EXPECT_EQ(v.back(), 1022);
  EXPECT_EQ(v[256], 512);
}

TEST(small_vec_copy_after_spill_preserves_size_and_contents)
{
  real::detail::small_vec<int, 32> v;
  for (int i = 0; i < 300; ++i) {
    v.push_back(i);
  }
  const real::detail::small_vec<int, 32> copied {v}; // copy constructor, heap path
  EXPECT_EQ(copied.size(), 300U);
  EXPECT_EQ(copied.back(), 299);
  EXPECT_EQ(copied[150], 150);

  real::detail::small_vec<int, 32> assigned;
  assigned = v; // copy assignment, heap path
  EXPECT_EQ(assigned.size(), 300U);
  EXPECT_EQ(assigned[299], 299);
}

TEST(small_vec_move_after_spill_preserves_size_and_contents)
{
  real::detail::small_vec<int, 32> v;
  for (int i = 0; i < 300; ++i) {
    v.push_back(i);
  }
  real::detail::small_vec<int, 32> moved {std::move(v)}; // move constructor, heap steal
  EXPECT_EQ(moved.size(), 300U);
  EXPECT_EQ(moved.back(), 299);

  real::detail::small_vec<int, 32> target;
  target = std::move(moved); // move assignment, heap steal
  EXPECT_EQ(target.size(), 300U);
  EXPECT_EQ(target[299], 299);
}

TEST(small_vec_assign_past_inline_capacity)
{
  real::detail::small_vec<std::size_t, 32> v;
  v.assign(400, 7U);
  EXPECT_EQ(v.size(), 400U); // the witness: a uint8_t size_ would store 400 mod 256 = 144
  EXPECT(!v.empty());
  EXPECT_EQ(v[0], 7U);       // the spilled fill ran; reads of the spilled region are covered above
}

TEST(small_vec_inline_copy_assignment)
{
  // Copy-assignment from a source that has NOT spilled takes the inline branch (no
  // heap), the counterpart to the spilled assignments above.
  real::detail::small_vec<int, 32> source;
  source.push_back(11);
  source.push_back(22);
  real::detail::small_vec<int, 32> dest;
  dest = source;
  EXPECT_EQ(dest.size(), 2U);
  EXPECT_EQ(dest[0], 11);
  EXPECT_EQ(dest[1], 22);
}

TEST(small_vec_of_eps_entry_spills)
{
  // The VM's epsilon-closure stack is small_vec<eps_entry, 32>; its depth reaches
  // ~3·code_size, so it spills well past 255 on a large program.
  real::detail::small_vec<real::detail::eps_entry, 32> stack;
  for (int i = 0; i < 300; ++i) {
    stack.push_back({.pc = i, .block = 0});
  }
  EXPECT_EQ(stack.size(), 300U);
  EXPECT_EQ(stack.back().pc, 299);
  EXPECT_EQ(stack[256].pc, 256);
}

// ─── B. End-to-end: > 255 live threads, staying on the general Pike VM ──────────

TEST(wide_alternation_exceeds_255_threads_stays_on_pike_vm)
{
  const std::string pattern {wide_capturing_alternation(300)};

  // White-box guard: this pattern MUST run on the general Pike VM (the thread-list
  // path that uses small_vec), not on any fast path that would never build a large
  // thread list. If a future dispatch change swallows this pattern, these assertions
  // fire so the stress below cannot silently rot into a no-op. (Field names per
  // detail::pattern_hints in program.hpp.)
  const auto hints {real::detail::dynamic_storage::compile(pattern, real::flags::none).program.view().hints};
  EXPECT(hints.greedy_class_loop < 0);
  EXPECT(hints.exact_literal_len == 0);
  EXPECT(!hints.fixed_shape);
  EXPECT(hints.codepoint_class_ascii < 0);
  EXPECT(!hints.fixed_alternation);

  const real::regex rx {pattern};
  EXPECT_EQ(rx.group_count(), 300U);

  // All 300 branches park a live thread at the seed position (> 255), overflowing a
  // uint8_t-counted thread list; with size_t counters the list spills and the one
  // matching branch wins. Parity with Python re: re.fullmatch picks branch (z150).
  const auto match {rx.fullmatch("z150")};
  EXPECT(static_cast<bool>(match));
  EXPECT_EQ(match.size(), 301U);         // group 0 + 300 groups
  EXPECT_EQ(match[0], "z150"sv);
  EXPECT_EQ(match[151], "z150"sv);       // (z150) is the 151st group
  EXPECT_EQ(match.start(1), real::npos); // every other branch is unset, like re
  EXPECT_EQ(match.start(300), real::npos);
}

// ─── C. End-to-end: > 127 groups (slot_count > 255), all captured ──────────────

TEST(many_capturing_groups_exceed_255_slots_all_captured)
{
  // 200 groups → slot_count = 2·201 = 402 > 255, overflowing a uint8_t-counted slot
  // container (the dynamic regex's result slots are a small_vec). Each "(.)" captures
  // one character; parity with re is that every group is captured.
  constexpr int group_count {200};
  std::string   pattern;
  std::string   text;
  for (int i = 0; i < group_count; ++i) {
    pattern += "(.)";
    text    += static_cast<char>('A' + (i % 26));
  }

  const real::regex rx {pattern};
  EXPECT_EQ(rx.group_count(), static_cast<std::size_t>(group_count));

  const auto match {rx.fullmatch(text)};
  EXPECT(static_cast<bool>(match));
  EXPECT_EQ(match.size(), static_cast<std::size_t>(group_count + 1)); // 201; a narrow counter collapses this
  EXPECT_EQ(match[0].size(), static_cast<std::size_t>(group_count));

  bool all_captured {true};
  for (int i = 1; i <= group_count; ++i) {
    const auto group {match[static_cast<std::size_t>(i)]};
    all_captured = all_captured && group.size() == 1 &&
                   group[0] == text[static_cast<std::size_t>(i - 1)];
  }
  EXPECT(all_captured);
  EXPECT_EQ(match.start(1), 0U);
  EXPECT_EQ(match[group_count][0], text[static_cast<std::size_t>(group_count - 1)]);
  EXPECT_EQ(match.end(group_count), static_cast<std::size_t>(group_count));
}

TEST(small_vec_transfer_non_trivially_copyable)
{
  // small_vec::transfer_range has a memcpy fast path for trivially-copyable elements (the
  // only kind the VM uses) and an element-wise construct_at loop otherwise. This pins the
  // loop at run time with a type that is non-trivially-copyable (a user-provided copy
  // constructor) yet trivially destructible (so it satisfies small_vec's contract).
  struct boxed
  {
    int v {};
    constexpr boxed() = default;
    constexpr explicit boxed(int value) : v(value)
    {}
    // Deliberately user-provided (NOT '= default'): that is exactly what makes boxed
    // non-trivially-copyable, which forces transfer_range's element-wise loop instead of
    // its memcpy fast path. Defaulting it (as the lint suggests) would defeat the test.
    // NOLINTNEXTLINE(modernize-use-equals-default)
    constexpr boxed(const boxed& other) : v(other.v)
    {}
    constexpr boxed& operator=(const boxed&) = default;
  };
  static_assert(std::is_trivially_destructible_v<boxed>);
  static_assert(!std::is_trivially_copyable_v<boxed>);

  // Grow past the inline capacity: spill then regrow, copying elements each time.
  real::detail::small_vec<boxed, 2> grown;
  for (int i = 0; i < 8; ++i) {
    grown.push_back(boxed(i));
  }
  EXPECT_EQ(grown.size(), 8U);
  EXPECT_EQ(grown[7].v, 7);

  // Copy-construct a spilled vector (transfer_range<false> over the heap block).
  auto copied = grown;
  EXPECT_EQ(copied.size(), 8U);
  EXPECT_EQ(copied[0].v, 0);
  EXPECT_EQ(copied[7].v, 7);

  // Move-construct an inline (not spilled) vector (transfer_inline_from<true> -> Move branch).
  real::detail::small_vec<boxed, 2> inline_one;
  inline_one.push_back(boxed(5));
  inline_one.push_back(boxed(6));
  auto moved = std::move(inline_one);
  EXPECT_EQ(moved.size(), 2U);
  EXPECT_EQ(moved[1].v, 6);
}
