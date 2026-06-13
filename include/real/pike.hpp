/*!
 * \file pike.hpp
 * \brief The Pike VM — a Thompson NFA simulation — and its fast paths.
 *
 * Linear time in the input: every program counter is added to a list at most
 * once per position (generation-marked dedup), so no pattern can backtrack
 * catastrophically.
 *
 * The VM is generic over its container policy — \c std::vector for the
 * dynamic storage mode, fixed-capacity \c static_vec (storage.hpp) for
 * compile-time sized patterns, where a whole run performs zero heap
 * allocations.
 */
#ifndef REAL_PIKE_HPP
#define REAL_PIKE_HPP

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "charclass.hpp"
#include "prefilter.hpp"
#include "program.hpp"

namespace real::detail {

//! How a VM run is anchored.
enum class run_mode : std::uint8_t
{
  prefix, //!< Anchored at the start position (Python \c re.match).
  full,   //!< Anchored at both ends (Python \c re.fullmatch).
  search, //!< First match anywhere (Python \c re.search).
};

/*!
 * \brief One entry on the epsilon-closure DFS stack.
 *
 * Two kinds: explore a program counter (<tt>pc >= 0</tt>), or restore a
 * capture slot to its previous value once the subtree it covered is done
 * (<tt>pc == -1</tt>). This mutates one working slot array in place rather
 * than copying all slots per branch.
 */
struct eps_entry
{
  std::int32_t  pc;            //!< pc to explore, or -1 for a slot-restore entry.
  std::uint16_t slot;          //!< Slot to restore (restore entries).
  std::size_t   restore_value; //!< Value to restore the slot to.
};

/*!
 * \brief One priority-ordered list of NFA threads (leftmost-greedy semantics).
 *
 * \c mark is generation-stamped so clearing the list between positions is O(1).
 *
 * \tparam PcVec   Container of program counters.
 * \tparam SlotVec Flattened capture slots (pcs.size() * slot_count).
 * \tparam MarkVec Per-pc generation marks for O(1) dedup.
 */
template <typename PcVec, typename SlotVec, typename MarkVec>
struct basic_thread_list
{
  PcVec         pcs;             //!< Live program counters, in priority order.
  SlotVec       slots;           //!< Flattened capture slots, parallel to \ref pcs.
  MarkVec       mark;            //!< Per-pc generation stamp (see \ref seen).
  std::uint64_t generation = 0;  //!< Current generation; bumped by \ref reset.

  /*!
   * \brief Clears the list in O(1) by bumping the generation.
   * \param[in] code_size Number of instructions (sizes the mark table once).
   */
  constexpr void reset(std::size_t code_size)
  {
    if (mark.size() != code_size) {
      mark.assign(code_size, 0);
      generation = 0;
    }
    ++generation;
    pcs.clear();
    slots.clear();
  }

  //! \param[in] pc A program counter. \return \c true if \p pc is already in this generation.
  [[nodiscard]] constexpr bool seen(std::int32_t pc) const
  {
    return mark[static_cast<std::size_t>(pc)] == generation;
  }

  //! Marks \p pc as present in the current generation. \param[in] pc The program counter.
  constexpr void mark_seen(std::int32_t pc)
  {
    mark[static_cast<std::size_t>(pc)] = generation;
  }
};

/*!
 * \brief Reusable VM scratch state.
 *
 * One run allocates nothing once warm (and never allocates with static
 * containers); \c find_all-style loops reuse the same state across runs. The
 * two thread lists are flipped by index, never swapped.
 *
 * \tparam ThreadList The thread-list type (a \ref basic_thread_list).
 * \tparam WorkVec    Container for the working capture slots.
 * \tparam EpsVec     Container for the epsilon-closure stack.
 */
template <typename ThreadList, typename WorkVec, typename EpsVec>
struct basic_pike_state
{
  ThreadList lists[2];  //!< Current and next thread lists (flipped by index).
  WorkVec    working;   //!< Capture slots along the current DFS path.
  EpsVec     stack;     //!< Epsilon-closure DFS stack.
};

//! Thread list specialized on \c std::vector (the dynamic storage mode).
using thread_list = basic_thread_list<std::vector<std::int32_t>, std::vector<std::size_t>, std::vector<std::uint64_t>>;
//! VM scratch state for the dynamic storage mode.
using pike_state =
  basic_pike_state<thread_list, std::vector<std::size_t>, std::vector<eps_entry>>;

/*!
 * \brief The Pike VM, generic over the scratch-state container policy.
 * \tparam State A \ref basic_pike_state instantiation (vector- or static-backed).
 */
template <typename State>
class pike_vm
{
public:

  /*!
   * \brief Binds the VM to a program and caller-owned scratch state.
   * \param[in]     prog  The compiled program to execute.
   * \param[in,out] state Reusable scratch (borrowed; must outlive the VM).
   */
  constexpr pike_vm(program_view prog, State& state)
    : prog_(prog),
      state_(state)
  {}

  /*!
   * \brief Runs the VM over \p text starting at \p start.
   *
   * On success fills \p out_slots with byte offsets (npos for unset capture
   * slots; slots 0/1 are the whole match).
   *
   * \tparam OutSlots Output slot container (resized to the program's slot count).
   * \param[in]  text               The subject text.
   * \param[in]  start              Index to begin matching/searching from.
   * \param[in]  mode               Anchoring mode (\ref run_mode).
   * \param[out] out_slots          Receives the capture slots on success.
   * \param[in]  forbid_empty_until Reject an empty match whose start is below
   *             this offset (the iterator sets it to the next codepoint
   *             boundary so a non-empty match may follow an empty one without
   *             re-yielding it — CPython 3.7+ rule). 0 means no restriction.
   * \return \c true if a match was found.
   */
  template <typename OutSlots>
  constexpr bool run(std::string_view text, std::size_t start, run_mode mode, OutSlots& out_slots,
                     std::size_t forbid_empty_until = 0)
  {
    text_               = text;
    forbid_empty_until_ = forbid_empty_until;
    // Fast paths only fire for patterns that always consume (literal /
    // class+), which can never produce the empty match the flag guards.
    if (prog_.hints.greedy_class_loop >= 0) {
      return run_class_loop(text, start, mode, out_slots);
    }
    if (prog_.hints.exact_literal_len > 0) {
      return run_exact_literal(text, start, mode, out_slots);
    }
    const std::size_t code_size = prog_.code.size();
    auto*             clist     = &state_.lists[0];
    auto*             nlist     = &state_.lists[1];
    clist->reset(code_size);
    nlist->reset(code_size);
    state_.working.assign(prog_.slot_count, npos);
    out_slots.assign(prog_.slot_count, npos);

    bool        matched = false;
    std::size_t pos     = start;
    while (pos <= text.size()) {
      const bool seeding = (pos == start) || (mode == run_mode::search && !matched);
      if (seeding && mode == run_mode::search && !matched && clist->pcs.empty()) {
        // No thread is alive: jump straight to the next position
        // that could start a match (prefilter). Single pass, so the
        // linear-time guarantee is unaffected.
        pos = next_candidate(text, pos, start);
        if (pos > text.size()) {
          break; // no further start is possible (includes npos)
        }
        // Fresh generation before seeding at the jumped position: the list
        // may still carry `seen` marks from a previous position's epsilon
        // exploration whose threads all died, which would otherwise dedup
        // away (drop) the seed's own threads here.
        clist->reset(code_size);
      }
      if (seeding && seed_viable(text, pos, start)) {
        // A fresh thread must not inherit capture slots left in the
        // scratch by previously stepped threads.
        state_.working.assign(prog_.slot_count, npos);
        add_thread(*clist, 0, pos);
      }
      if (clist->pcs.empty()) {
        // The seed itself may die in the closure (failed assertion):
        // later positions must still be tried while searching. The
        // dead seed's seen-marks must not block the next one.
        if (matched || mode != run_mode::search || pos >= text.size()) {
          break;
        }
        clist->reset(code_size);
        ++pos;
        continue;
      }
      step(*clist, *nlist, pos, mode, matched, out_slots);
      auto* swap = clist;
      clist      = nlist;
      nlist      = swap;
      nlist->reset(code_size);
      ++pos;
    }
    return matched;
  }

private:

  program_view     prog_;   //!< The program being executed.
  State&           state_;  //!< Borrowed reusable scratch state.
  std::string_view text_;   //!< The subject text for the current run.

  /*!
   * \brief Reject empty matches whose start is below this offset.
   *
   * The CPython 3.7+ rule: after an empty match, the next match may not be
   * empty at the same spot, letting a non-empty match start there. The
   * iterator sets this to the next codepoint boundary so the skip stays
   * UTF-8 aligned. 0 means no restriction (single match/search/fullmatch
   * never restrict).
   */
  std::size_t      forbid_empty_until_ = 0;

  //! The concrete thread-list type taken from the bound \c State.
  using list_type = std::remove_reference_t<decltype(std::declval<State&>().lists[0])>;

  /*!
   * \brief Fast path for a whole-pattern "class+".
   *
   * Matches a maximal run of class bytes with one scan loop — exactly the
   * VM's greedy result, with no thread lists.
   *
   * \tparam OutSlots Output slot container.
   * \param[in]  text      The subject text.
   * \param[in]  start     Index to begin at.
   * \param[in]  mode      Anchoring mode.
   * \param[out] out_slots Receives the (start, end) span on success.
   * \return \c true if a non-empty run was found.
   */
  template <typename OutSlots>
  constexpr bool run_class_loop(std::string_view text, std::size_t start, run_mode mode, OutSlots& out_slots)
  {
    const char_class& k =
      prog_.classes[static_cast<std::size_t>(prog_.hints.greedy_class_loop)];
    const auto in_class = [&](std::size_t i) {
      return k.test(static_cast<std::uint8_t>(text[i]));
    };
    std::size_t s = start;
    if (mode == run_mode::search) {
      while (s < text.size() && !in_class(s)) {
        ++s;
      }
    }
    if (s >= text.size() || !in_class(s)) {
      out_slots.assign(2, npos);
      return false;
    }
    std::size_t e = s + 1;
    while (e < text.size() && in_class(e)) {
      ++e;
    }
    if (mode == run_mode::full && e != text.size()) {
      out_slots.assign(2, npos);
      return false;
    }
    out_slots.assign(2, npos);
    out_slots[0] = s;
    out_slots[1] = e;
    return true;
  }

  /*!
   * \brief Tests whether the fixed literal prefix occurs at \p cand.
   * \param[in] text The subject text.
   * \param[in] cand Candidate start offset.
   * \param[in] len  Length of the literal (\c hints.exact_literal_len).
   * \return \c true if <tt>text[cand : cand+len]</tt> equals the literal.
   */
  [[nodiscard]] constexpr bool literal_at(std::string_view text, std::size_t cand,
                                          std::size_t len) const
  {
    if (cand + len > text.size()) {
      return false;
    }
    const auto pfx = std::string_view(prog_.hints.prefix.data(), len);
    if (std::is_constant_evaluated()) {
      return text.substr(cand, len) == pfx;
    }
    return std::memcmp(text.data() + cand, pfx.data(), len) == 0;
  }

  /*!
   * \brief Fills capture slots for a literal match at \p cand.
   *
   * Replays \c save instructions at their consumed offsets and checks any
   * zero-width assertions in the chain at \p cand.
   *
   * \tparam OutSlots Output slot container.
   * \param[in]  cand      Start offset of the literal match.
   * \param[in]  len       Length of the literal.
   * \param[out] out_slots Receives the capture slots.
   * \return \c false (and clears \p out_slots) if an assertion fails here, so
   *         the caller tries the next occurrence; \c true otherwise.
   */
  template <typename OutSlots>
  constexpr bool replay_literal(std::size_t cand, std::size_t len, OutSlots& out_slots) const
  {
    out_slots.assign(prog_.slot_count, npos);
    std::size_t consumed = 0;
    for (std::size_t pc = 0; pc < prog_.code.size(); ++pc) {
      const instr& in = prog_.code[pc];
      if (in.op == opcode::save) {
        out_slots[in.arg16] = cand + consumed;
      } else if (in.op == opcode::assert_position) {
        if (!assertion_holds(static_cast<assert_kind>(in.arg8), cand + consumed)) {
          out_slots.assign(prog_.slot_count, npos);
          return false;
        }
      } else if ((in.op == opcode::byte || in.op == opcode::klass) && consumed < len) {
        ++consumed;
      } else if (in.op == opcode::match) {
        break;
      }
    }
    if (prog_.slot_count >= 2 && out_slots[1] == npos) {
      out_slots[1] = cand + len; // group 0 end, even if replay ended early
    }
    return true;
  }

  /*!
   * \brief Fast path for a pure-literal pattern.
   *
   * The prefilter locates the fixed bytes; this replays saves directly, with
   * no thread lists, epsilon stack or per-position stepping. A leading or
   * trailing zero-width assertion (\c \\b, \c ^, \c $ …) may make a given
   * occurrence fail, so in search mode it scans successive occurrences until
   * the assertions hold — the case a differential-fuzz finding (\c \\B2 on
   * \c "220") exposed.
   *
   * \tparam OutSlots Output slot container.
   * \param[in]  text      The subject text.
   * \param[in]  start     Index to begin at.
   * \param[in]  mode      Anchoring mode.
   * \param[out] out_slots Receives the capture slots on success.
   * \return \c true if a match was found.
   */
  template <typename OutSlots>
  constexpr bool run_exact_literal(std::string_view text, std::size_t start, run_mode mode, OutSlots& out_slots)
  {
    const std::size_t len = static_cast<std::size_t>(prog_.hints.exact_literal_len);
    if (len == 0) {
      out_slots.assign(prog_.slot_count, npos);
      return false;
    }
    if (mode != run_mode::search) {
      const bool full_ok = mode != run_mode::full || start + len == text.size();
      const bool ok = literal_at(text, start, len) && full_ok &&
                      replay_literal(start, len, out_slots);
      if (!ok) {
        out_slots.assign(prog_.slot_count, npos);
      }
      return ok;
    }
    std::size_t from = start;
    while (true) {
      const std::size_t cand = next_candidate(text, from, start);
      if (cand > text.size() || cand + len > text.size()) {
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }
      if (literal_at(text, cand, len) && replay_literal(cand, len, out_slots)) {
        return true;
      }
      from = cand + 1; // assertion failed here; try the next occurrence
    }
  }

  /*!
   * \brief First position >= \p pos that could start a match, per the hints.
   *
   * The prefilter step: jumps over positions that provably cannot start a
   * match (literal prefix search, unique first byte, line start, first-byte
   * set). Returns \p pos itself when no skipping applies.
   *
   * \param[in] text  The subject text.
   * \param[in] pos   Current position.
   * \param[in] start The run's start offset (for one-shot anchored patterns).
   * \return The next candidate offset, or \ref real::npos if none exists.
   */
  [[nodiscard]] constexpr std::size_t next_candidate(std::string_view text, std::size_t pos, std::size_t start) const
  {
    const pattern_hints& h = prog_.hints;
    if (h.anchored_start) {
      return pos == start ? pos : npos; // one shot at the start
    }
    if (h.prefix_size >= 2) {
      return find_prefix(text, pos, std::string_view(h.prefix.data(), h.prefix_size));
    }
    if (h.single_first >= 0) {
      return find_byte(text, pos, static_cast<char>(h.single_first));
    }
    if (h.line_anchored && pos != start) {
      const std::size_t nl = find_byte(text, pos - 1, '\n');
      return nl == npos ? npos : nl + 1;
    }
    if (h.first_bytes_valid) {
      while (pos < text.size() &&
             !h.first_bytes.test(static_cast<std::uint8_t>(text[pos]))) {
        ++pos;
      }
      return pos < text.size() ? pos : npos;
    }
    return pos;
  }

  /*!
   * \brief Cheap pre-check before seeding a new thread at \p pos.
   *
   * Live threads may force the loop through positions the prefilter would
   * have skipped; this avoids seeding where a match cannot start. It also
   * enforces codepoint alignment: in non-byte mode a UTF-8 continuation byte
   * is never a valid match start.
   *
   * \param[in] text  The subject text.
   * \param[in] pos   The candidate seed position.
   * \param[in] start The run's start offset.
   * \return \c true if a fresh thread should be seeded at \p pos.
   */
  [[nodiscard]] constexpr bool seed_viable(std::string_view text, std::size_t pos, std::size_t start) const
  {
    const pattern_hints& h = prog_.hints;
    if (h.anchored_start && pos != start) {
      return false;
    }
    // A match can never start inside a multi-byte codepoint: in non-byte mode
    // a UTF-8 continuation byte (10xxxxxx) is not a valid start position. This
    // keeps zero-width matches (\b, \B, ^, $, empty) codepoint-aligned, like a
    // codepoint-based engine — bytes mode seeds every byte.
    if (!prog_.byte_mode && pos < text.size() &&
        (static_cast<std::uint8_t>(text[pos]) & 0xC0U) == 0x80U) {
      return false;
    }
    if (!h.first_bytes_valid) {
      return true;
    }
    return pos < text.size() && h.first_bytes.test(static_cast<std::uint8_t>(text[pos]));
  }

  /*!
   * \brief Evaluates a zero-width assertion at \p pos in the current text.
   * \param[in] kind The assertion to evaluate.
   * \param[in] pos  The position at which to evaluate it.
   * \return \c true if the assertion holds there.
   */
  [[nodiscard]] constexpr bool assertion_holds(assert_kind kind, std::size_t pos) const
  {
    const std::size_t len     = text_.size();
    const auto        byte_at = [&](std::size_t i) { return static_cast<std::uint8_t>(text_[i]); };
    switch (kind) {
      case assert_kind::text_start:
        return pos == 0;
      case assert_kind::text_end:
        return pos == len;
      case assert_kind::text_end_or_final_newline:
        return pos == len || (pos + 1 == len && byte_at(pos) == '\n');
      case assert_kind::line_start:
        return pos == 0 || byte_at(pos - 1) == '\n';
      case assert_kind::line_end:
        return pos == len || byte_at(pos) == '\n';
      case assert_kind::word_boundary:
      case assert_kind::not_word_boundary:
        {
          const bool before = pos > 0 && is_ascii_word_byte(byte_at(pos - 1));
          const bool after  = pos < len && is_ascii_word_byte(byte_at(pos));
          return (before != after) == (kind == assert_kind::word_boundary);
        }
      case assert_kind::word_start:
      case assert_kind::word_end:
        {
          const bool before = pos > 0 && is_ascii_word_byte(byte_at(pos - 1));
          const bool after  = pos < len && is_ascii_word_byte(byte_at(pos));
          return kind == assert_kind::word_start ? (!before && after)
                                                 : (before && !after);
        }
    }
    return false; // unreachable
  }

  /*!
   * \brief Advances every thread of \p clist by the byte at \p pos.
   *
   * Survivors that consumed a byte land in \p nlist. A thread reaching
   * \c match records its slots and cuts all lower-priority threads, so
   * priority (leftmost-greedy) order is preserved.
   *
   * \tparam OutSlots Output slot container.
   * \param[in,out] clist     The current thread list (consumed).
   * \param[in,out] nlist     The next thread list (receives survivors).
   * \param[in]     pos        The current input position.
   * \param[in]     mode       Anchoring mode (affects \c match acceptance).
   * \param[in,out] matched    Set to \c true when a match is recorded.
   * \param[out]    out_slots  Receives the slots of an accepted match.
   */
  template <typename OutSlots>
  constexpr void step(list_type& clist, list_type& nlist, std::size_t pos, run_mode mode, bool& matched, OutSlots& out_slots)
  {
    const std::uint16_t slot_count = prog_.slot_count;
    for (std::size_t i = 0; i < clist.pcs.size(); ++i) {
      const std::int32_t pc   = clist.pcs[i];
      const instr&       in   = prog_.code[static_cast<std::size_t>(pc)];
      const std::size_t  base = i * slot_count;
      switch (in.op) {
        case opcode::byte:
          if (pos < text_.size() &&
              static_cast<std::uint8_t>(text_[pos]) == in.arg8) {
            load_working(clist, base);
            add_thread(nlist, pc + 1, pos + 1);
          }
          break;
        case opcode::klass:
          if (pos < text_.size() &&
              prog_.classes[in.arg16].test(static_cast<std::uint8_t>(text_[pos]))) {
            load_working(clist, base);
            add_thread(nlist, pc + 1, pos + 1);
          }
          break;
        case opcode::match:
          if (mode == run_mode::full && pos != text_.size()) {
            break; // must consume the whole text: thread dies
          }
          // Reject an empty match forbidden at this position; a lower-priority
          // thread may still consume a byte and win a non-empty match here.
          if (pos == clist.slots[base] && clist.slots[base] < forbid_empty_until_) {
            break;
          }
          for (std::uint16_t s = 0; s < slot_count; ++s) {
            out_slots[s] = clist.slots[base + s];
          }
          matched = true;
          return; // drop lower-priority threads
        case opcode::split:
        case opcode::jump:
        case opcode::save:
        case opcode::assert_position:
          break; // epsilon ops never appear in a stepped list
      }
    }
  }

  /*!
   * \brief Loads a thread's saved slots into the working slot array.
   * \param[in] clist The list holding the thread.
   * \param[in] base  Flattened offset of the thread's slots in \c clist.slots.
   */
  constexpr void load_working(const list_type& clist, std::size_t base)
  {
    for (std::uint16_t s = 0; s < prog_.slot_count; ++s) {
      state_.working[s] = clist.slots[base + s];
    }
  }

  /*!
   * \brief Adds \p pc0 and its whole epsilon closure to \p list.
   *
   * Threads are added in DFS (priority) order; the current \c working slots
   * are snapshotted into the list for each consuming thread. Saves and
   * assertions are handled during the closure walk.
   *
   * \param[in,out] list The thread list to populate.
   * \param[in]     pc0  The program counter to seed from.
   * \param[in]     pos  The current input position (for \c save / assertions).
   */
  constexpr void add_thread(list_type& list, std::int32_t pc0, std::size_t pos)
  {
    auto& stack = state_.stack;
    stack.clear();
    stack.push_back({.pc = pc0, .slot = 0, .restore_value = 0});
    while (!stack.empty()) {
      const auto entry = stack.back();
      stack.pop_back();
      if (entry.pc < 0) {
        state_.working[entry.slot] = entry.restore_value;
        continue;
      }
      const std::int32_t pc = entry.pc;
      if (list.seen(pc)) {
        continue;
      }
      list.mark_seen(pc);
      const instr& in = prog_.code[static_cast<std::size_t>(pc)];
      switch (in.op) {
        case opcode::jump:
          stack.push_back({.pc = in.x, .slot = 0, .restore_value = 0});
          break;
        case opcode::split:
          // x is preferred: push y first so x pops (explores) first.
          stack.push_back({.pc = in.y, .slot = 0, .restore_value = 0});
          stack.push_back({.pc = in.x, .slot = 0, .restore_value = 0});
          break;
        case opcode::save:
          stack.push_back({.pc            = -1,
                           .slot          = in.arg16,
                           .restore_value = state_.working[in.arg16]});
          state_.working[in.arg16] = pos;
          stack.push_back({.pc = pc + 1, .slot = 0, .restore_value = 0});
          break;
        case opcode::assert_position:
          if (assertion_holds(static_cast<assert_kind>(in.arg8), pos)) {
            stack.push_back({.pc = pc + 1, .slot = 0, .restore_value = 0});
          }
          break;
        case opcode::byte:
        case opcode::klass:
        case opcode::match:
          list.pcs.push_back(pc);
          for (std::uint16_t s = 0; s < prog_.slot_count; ++s) {
            list.slots.push_back(state_.working[s]);
          }
          break;
      }
    }
  }
};

} // namespace real::detail

#endif // REAL_PIKE_HPP
