/*!
 * \file lazy_dfa.hpp
 * \brief A lazy, priority-preserving forward DFA over the Pike program (the kFirstMatch forward pass + cache).
 *
 * DISTINCT from \ref real::dfa (`<real/dfa.hpp>`): that one is a capture-free *maximal-munch* recognizer
 * over unordered NFA-state sets (a lexer's rule dispatch). This one memoizes the *leftmost-first* Pike
 * closure — its DFA states are **ordered** NFA-state sets, so a `kFirstMatch` forward pass reports the same
 * match boundary the Pike VM would. It reuses only the byte-class idea (an alphabet smaller than 256), not
 * that engine's subset construction.
 *
 * \note The forward pass (`forward_end`), the reverse start-finder (`reverse_dfa`) and the byte-program
 *       that makes a Unicode `klass_cp` DFA-representable are wired into the matcher: pike.hpp routes an
 *       eligible search through them (forward end + reverse start, then the Pike VM on the located window).
 *       Dynamic only: the cache is mutable, so it never participates in constant evaluation.
 */
#ifndef REAL_LAZY_DFA_HPP
#define REAL_LAZY_DFA_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "program.hpp"
#include "utf8_ranges.hpp"

namespace real::detail {

  //! \brief Test seam: force the matcher off the lazy-DFA route onto the pure Pike VM, so a differential can
  //!        assert that routed and unrouted searches give identical results within one binary. Not for
  //!        production use — the routing is transparent by contract, and this only exists to prove it.
  inline bool& lazy_dfa_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  //! \brief A byte-level program derived from a Pike program for the DFA passes: every `klass_cp` construct
  //!        is expanded into UTF-8 byte-range split/klass chains, so the whole thing is byte-transition-only
  //!        and a forward DFA can represent it. The Pike program itself is untouched (byte-identity); this
  //!        is a private recognition view the DFAs own. `eligible` is false when an op no DFA can represent
  //!        (a position assertion or a lookaround) is present — the caller then keeps the Pike VM.
  struct byte_program
  {
    std::vector<instr>      code;
    std::vector<char_class> classes;
    bool                    eligible {true};
  };

  //! \brief The UTF-8 byte-range branches recognising one code-point class: an optional ASCII step plus,
  //!        per non-ASCII range, one branch per canonical byte-sequence (each a chain of byte-range steps).
  inline std::vector<std::vector<char_class>> klass_cp_branches(const cp_class&             cc,
                                                                std::span<const code_range> cp_ranges)
  {
    std::vector<std::vector<char_class>> branches;
    if (!cc.ascii.empty()) {
      branches.push_back({cc.ascii}); // one byte < 0x80, tested against the ASCII bitmap
    }
    for (std::uint32_t k = 0; k < cc.range_count; ++k) {
      const code_range& range {cp_ranges[cc.range_begin + k]};
      for (const utf8_byte_seq& seq : utf8_range_sequences(range.lo, range.hi)) {
        std::vector<char_class> branch;
        for (std::size_t j = 0; j < seq.length; ++j) {
          char_class step;
          step.set_range(seq.parts[j].lo, seq.parts[j].hi);
          branch.push_back(step);
        }
        branches.push_back(branch);
      }
    }
    return branches;
  }

  //! \brief The instruction count a \ref klass_cp_branches expansion emits: every branch is its steps plus a
  //!        converging jump, and every branch but the last is fronted by a split.
  inline std::size_t klass_cp_expansion_size(const std::vector<std::vector<char_class>>& branches)
  {
    std::size_t n {0};
    for (const std::vector<char_class>& b : branches) {
      n += b.size() + 1; // the byte-range steps + one jump to the continuation
    }
    return n + (branches.empty() ? 0 : branches.size() - 1); // a split fronts every branch but the last
  }

  /*!
   * \brief Builds the byte-level DFA program for \p prog (see \ref byte_program). A `klass_cp` at P (a four-
   *        instruction construct: the op plus three `utf8_cont` continuation slots) is replaced by the byte
   *        alternation recognising its code-point class, converging on the mapped P+4; every other op is
   *        copied with its branch targets remapped. Two passes: the first sizes the expansions to build the
   *        old→new pc map, the second emits.
   */
  inline byte_program build_byte_program(const program_view& prog)
  {
    byte_program bp;
    for (const instr& in : prog.code) {
      if (in.op == opcode::assert_position || in.op == opcode::assert_lookaround) {
        bp.eligible = false; // no byte-DFA can carry a position assertion or a lookaround
        return bp;
      }
    }
    bp.classes.assign(prog.classes.begin(), prog.classes.end()); // original classes keep their indices

    const std::size_t          n   {prog.code.size()};
    std::vector<std::int32_t>  map(n + 1, 0);                    // old pc -> new pc (n = the one-past end)
    std::size_t                cur {0};
    for (std::size_t pc = 0; pc < n; ++pc) {
      map[pc] = static_cast<std::int32_t>(cur);
      if (prog.code[pc].op == opcode::klass_cp) {
        const auto branches {klass_cp_branches(prog.cp_classes[prog.code[pc].arg16], prog.cp_ranges)};
        cur        += klass_cp_expansion_size(branches);
        map[pc + 1] = map[pc + 2] = map[pc + 3] = static_cast<std::int32_t>(cur); // continuation slots absorbed
        pc         += 3;                                                          // skip the construct's tail
      }
      else {
        ++cur;
      }
    }
    map[n] = static_cast<std::int32_t>(cur);

    const auto remap {[&](std::int32_t t) {
                        return (t >= 0 && static_cast<std::size_t>(t) <= n) ? map[static_cast<std::size_t>(t)] : t;
                      }};
    for (std::size_t pc = 0; pc < n; ++pc) {
      const instr& in {prog.code[pc]};
      if (in.op == opcode::klass_cp) {
        const auto         branches {klass_cp_branches(prog.cp_classes[in.arg16], prog.cp_ranges)};
        const std::int32_t after    {map[pc + 4]}; // where a thread lands after the whole construct
        for (std::size_t i = 0; i < branches.size(); ++i) {
          const auto         base {static_cast<std::int32_t>(bp.code.size())};
          const std::int32_t len  {static_cast<std::int32_t>(branches[i].size())};
          if (i + 1 < branches.size()) {
            bp.code.push_back({.op               = opcode::split,
                               .primary_target   = base + 1,         // this branch's first step
                               .secondary_target = base + 2 + len}); // the next branch's split/first step
          }
          for (const char_class& step : branches[i]) {
            bp.code.push_back({.op = opcode::klass, .arg16 = static_cast<std::uint16_t>(bp.classes.size())});
            bp.classes.push_back(step);
          }
          bp.code.push_back({.op = opcode::jump, .primary_target = after});
        }
        pc += 3;
      }
      else {
        instr out {in};
        out.primary_target   = remap(in.primary_target);
        out.secondary_target = remap(in.secondary_target);
        bp.code.push_back(out);
      }
    }
    return bp;
  }

  /*!
   * \brief Byte-class alphabet over a Pike program: bytes that satisfy exactly the same `byte`/`klass`
   *        predicates share a class, so the DFA transitions over classes instead of 256 raw bytes. The
   *        same reduction \ref real::dfa uses, computed here from the Pike program's own ops.
   */
  struct lazy_byte_alphabet
  {
    std::array<std::uint8_t, 256> of    {};    //!< byte -> class index.
    std::uint16_t                 count {0};   //!< number of distinct classes.
  };

  //! \brief Partition 0..255 by the program's consuming predicates (every `klass` test, every `byte`
  //!        literal). Bytes with an identical signature collapse to one class.
  inline constexpr lazy_byte_alphabet compute_lazy_alphabet(std::span<const instr>      code,
                                                            std::span<const char_class> classes)
  {
    std::vector<char_class>    class_preds;
    std::vector<std::uint16_t> literal_preds;
    const auto                 push_unique {[](auto& vec, const auto& value) {
                                              for (const auto& existing : vec) {
                                                if (existing == value) {
                                                  return;
                                                }
                                              }
                                              vec.push_back(value);
                                            }};
    for (const instr& in : code) {
      if (in.op == opcode::klass && in.arg16 < classes.size()) {
        push_unique(class_preds, classes[in.arg16]);
      }
      else if (in.op == opcode::byte) {
        push_unique(literal_preds, static_cast<std::uint16_t>(in.arg8));
      }
    }
    const auto sig_equal {[&](unsigned a, unsigned b) {
                            for (const char_class& cc : class_preds) {
                              if (cc.test(static_cast<std::uint8_t>(a)) != cc.test(static_cast<std::uint8_t>(b))) {
                                return false;
                              }
                            }
                            for (const std::uint16_t lit : literal_preds) {
                              if ((a == lit) != (b == lit)) {
                                return false;
                              }
                            }
                            return true;
                          }};
    lazy_byte_alphabet            alpha;
    std::array<std::uint8_t, 256> rep {};
    for (unsigned b = 0; b < 256U; ++b) {
      bool assigned {false};
      for (std::uint16_t c = 0; c < alpha.count; ++c) {
        if (sig_equal(b, rep[c])) {
          alpha.of[b] = static_cast<std::uint8_t>(c);
          assigned    = true;
          break;
        }
      }
      if (!assigned) {
        rep[alpha.count] = static_cast<std::uint8_t>(b);
        alpha.of[b]      = static_cast<std::uint8_t>(alpha.count);
        ++alpha.count;
      }
    }
    return alpha;
  }

  /*!
   * \brief A tiny open-chaining hash set of interned PC-set state ids, keyed by their pc-set. Replaces a
   *        `std::unordered_map` so the DFAs stay **literal types** (a constexpr `real::regex` embeds one in
   *        its scratch state); all-`std::vector` storage is constexpr-constructible in C++20. Maps a
   *        candidate pc-set to its existing state id, or \ref not_found, comparing against the owner's pcs.
   */
  struct pc_set_cache
  {
    static constexpr std::size_t   bucket_count {2048};
    static constexpr std::uint32_t not_found    {0xFFFFFFFFU};

    std::vector<std::vector<std::uint32_t>> buckets;

    constexpr pc_set_cache()
      : buckets(bucket_count)
    {}

    static constexpr std::size_t hash(const std::vector<std::int32_t>& v)
    {
      // FNV-1a computed in a fixed 64-bit accumulator, truncated to size_t on return: the 64-bit
      // offset-basis brace-initialised straight into size_t narrows (an error) where size_t is 32-bit
      // (win32). Truncating a full FNV-64 keeps a well-distributed hash without width-specific constants.
      std::uint64_t h {1469598103934665603ULL};
      for (const std::int32_t x : v) {
        h = (h ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))) * 1099511628211ULL;
      }
      return static_cast<std::size_t>(h);
    }

    [[nodiscard]] constexpr std::uint32_t find(const std::vector<std::int32_t>&               pcs,
                                               const std::vector<std::vector<std::int32_t>>&  state_pcs) const
    {
      for (const std::uint32_t id : buckets[hash(pcs) % bucket_count]) {
        if (state_pcs[id] == pcs) {
          return id;
        }
      }
      return not_found;
    }

    constexpr void insert(const std::vector<std::int32_t>& pcs,
                          std::uint32_t                    id)
    {
      buckets[hash(pcs) % bucket_count].push_back(id);
    }

    constexpr void clear()
    {
      for (std::vector<std::uint32_t>& b : buckets) {
        b.clear();
      }
    }
  };

  /*!
   * \brief A lazy priority-preserving forward DFA over a Pike program (the kFirstMatch forward pass).
   *
   * A DFA state is the ordered epsilon-closure of a set of program counters (the Pike thread list's PCs,
   * in split priority). \ref step transitions on a byte by consuming it from each PC and re-closing, then
   * interns the resulting ordered set into a cached state — the subset construction, memoized on demand.
   *
   * The cache is bounded: once it reaches \ref state_budget states it is flushed and rebuilt (states are
   * cheap to recompute; a bounded cache keeps memory flat). `state_budget` flushes crossed within one
   * scan (see \ref begin_scan) trips \ref thrashing — the signal an eventual caller uses to abandon the
   * DFA and finish that one search on the Pike VM, per-scan and linear, never re-attempting per position.
   *
   * A program with an op no forward DFA can represent — a position assertion (`\b`, `^`, `$`), a
   * `klass_cp`, or a lookaround — is \ref eligible "ineligible"; this only builds the machinery, it does
   * not decide policy.
   */
  class lazy_dfa
  {
  public:

    static constexpr std::uint32_t dead_state     {0};           //!< The empty state: every transition from it stays here.
    static constexpr std::uint32_t no_transition  {0xFFFFFFFFU}; //!< A not-yet-computed cached transition.
    static constexpr std::uint32_t no_match_idx   {0xFFFFFFFFU}; //!< A state whose ordered set holds no accept.
    static constexpr std::size_t   state_budget   {4096};        //!< Cached states before a flush (the memory cap).
    static constexpr std::size_t   thrash_flushes {2};           //!< Flushes within one scan that trip \ref thrashing.

    //! \brief Cache-behaviour counters, for the policy tests and later tuning.
    struct counters
    {
      std::size_t hits         {0};
      std::size_t misses       {0};
      std::size_t flushes      {0};
      std::size_t scan_flushes {0};   //!< flushes in the current scan (reset by \ref begin_scan).
    };

    /*!
     * \brief Builds the (initially empty) lazy DFA over a Pike program.
     * \param[in] code    The program's instruction stream (must outlive this object — held as a span).
     * \param[in] classes The program's interned character classes (likewise held as a span).
     * \param[in] budget  Cached states before a flush; defaults to \ref state_budget. A smaller value is a
     *                    test hook to exercise eviction and thrash without a state-exploding pattern.
     */
    explicit constexpr lazy_dfa(std::span<const instr>      code,
                                std::span<const char_class> classes,
                                std::size_t                 budget = state_budget)
      : code_ {code}, classes_ {classes}, alpha_ {compute_lazy_alphabet(code, classes)}, eligible_ {compute_eligibility(code)},
        budget_ {budget}
    {
      flush();                 // seeds the dead state (0) and the start state (1)
    }

    [[nodiscard]] bool eligible() const
    {
      return eligible_;
    }

    [[nodiscard]] std::uint16_t num_classes() const
    {
      return alpha_.count;
    }

    [[nodiscard]] std::uint32_t start_state() const
    {
      return start_state_;
    }

    //! \brief Begin a search: clear the per-scan flush counter and the thrash flag. (No cache flush — the
    //!        states carry over between searches on the same text, which is where the cache pays.)
    void begin_scan()
    {
      stats_.scan_flushes = 0;
      thrashing_          = false;
    }

    [[nodiscard]] bool thrashing() const
    {
      return thrashing_;
    }

    [[nodiscard]] const counters& stats() const
    {
      return stats_;
    }

    /*!
     * \brief The end offset of the leftmost-first match in \p text (`kFirstMatch`), or \ref real::npos.
     *
     * The forward pass the contract in the design guide (§7.6) specifies: an unanchored priority-ordered
     * closure that seeds a fresh thread at every position (at the lowest priority) until a match is found,
     * then reports the end of the **highest-priority** thread that reaches `match` — a lower-priority accept
     * is suppressed while a higher one lives. It is a single left-to-right pass over the ordered PC-sets, so
     * it is linear per search regardless of how the state space would explode under memoization. Eligible
     * programs only (an ineligible one returns \ref real::npos; the caller keeps the Pike VM). No captures:
     * this reports the end; the windowed Pike pass fills the span and applies the empty-match rule.
     */
    [[nodiscard]] std::size_t forward_end(std::string_view text)
    {
      if (!eligible_) {
        return npos;
      }
      begin_scan();
      std::uint32_t state    {start_state_}; // the seed at position 0 (a re-seeding state)
      std::size_t   best_end {npos};
      bool          matched  {false};
      std::size_t   pos      {0};
      while (true) {
        const std::uint32_t midx {state_match_idx_[state]};
        if (midx != no_match_idx) {
          best_end = pos;               // the highest-priority accept lives at index midx; a higher thread may extend it
          matched  = true;
          state    = cut_cached(state); // drop the accept and every lower-priority thread after it (memoized)
          if (state == dead_state) {
            break; // nothing higher-priority survives to extend the match
          }
        }
        if (pos >= text.size() || state == dead_state) {
          break;
        }
        const std::uint8_t byte {static_cast<std::uint8_t>(text[pos])};
        // pre-match transitions re-seed (unanchored search continues); post-match ones do not (leftmost).
        state = matched ? step(state, byte) : step_seeded(state, byte);
        ++pos;
      }
      return best_end;
    }

    //! \brief Whether \p state accepts here (its ordered set contains a `match` PC).
    [[nodiscard]] bool is_match(std::uint32_t state) const
    {
      return state_match_[state] != 0;
    }

    /*!
     * \brief Transition \p state on \p byte to the next DFA state, computing and caching it on first use.
     *        Returns \ref dead_state when no thread survives the byte.
     */
    std::uint32_t step(std::uint32_t state,
                       std::uint8_t  byte)
    {
      const std::uint8_t  cls    {alpha_.of[byte]};
      const std::uint32_t cached {state_trans_[state][cls]};   // by value: intern() below may realloc
      if (cached != no_transition) {
        ++stats_.hits;
        return cached;
      }
      ++stats_.misses;
      std::vector<std::int32_t> next;
      std::vector<char>         seen(code_.size(), 0);
      for (const std::int32_t pc : state_pcs_[state]) {
        if (consumes(pc, byte)) {
          close_into(pc + consumed_width(pc), next, seen);
        }
      }
      const std::size_t   flushes_before {stats_.flushes};
      const std::uint32_t result         {intern(next)}; // may grow/flush the tables — do not hold a reference
      if (stats_.flushes == flushes_before) {
        state_trans_[state][cls] = result;   // no flush: `state` is still valid, so cache the edge
      }
      // On a flush mid-step the caller's `state` id is stale; `result` is a fresh post-flush id, and the
      // caller re-seeds. (An eventual forward pass falls back to Pike once \ref thrashing trips.)
      return result;
    }

  private:

    //! \brief Like \ref step, but re-seeds: the unanchored-search variant appends pc 0's closure at the
    //!        lowest priority, so a fresh thread starts at every position until a match is found. Cached in
    //!        its own transition row (the pre-match state family).
    std::uint32_t step_seeded(std::uint32_t state,
                              std::uint8_t  byte)
    {
      const std::uint8_t  cls    {alpha_.of[byte]};
      const std::uint32_t cached {state_trans_seeded_[state][cls]};
      if (cached != no_transition) {
        ++stats_.hits;
        return cached;
      }
      ++stats_.misses;
      const std::vector<std::int32_t> pcs {state_pcs_[state]}; // copy: intern() below may realloc state_pcs_
      std::vector<std::int32_t>       next;
      std::vector<char>               seen(code_.size(), 0);
      for (const std::int32_t pc : pcs) {
        if (consumes(pc, byte)) {
          close_into(pc + consumed_width(pc), next, seen);
        }
      }
      close_into(0, next, seen); // re-seed at the lowest priority (deduped against the advanced threads)
      const std::size_t   flushes_before {stats_.flushes};
      const std::uint32_t result         {intern(next)};
      if (stats_.flushes == flushes_before) {
        state_trans_seeded_[state][cls] = result;
      }
      return result;
    }

    //! \brief The priority-cut at an accept: intern the prefix of \p state's ordered pc-set before index
    //!        \p m (dropping the accept and every lower-priority thread). Returns \ref dead_state if empty.
    std::uint32_t cut(std::uint32_t state,
                      std::uint32_t m)
    {
      // Copy the prefix element-by-element before interning, which may reallocate state_pcs_ (the intern
      // dangling-ref trap). A loop rather than an iterator-range copy — the latter trips a g++ false
      // -Werror=free-nonheap-object here.
      std::vector<std::int32_t> prefix;
      prefix.reserve(m);
      for (std::uint32_t i = 0; i < m; ++i) {
        prefix.push_back(state_pcs_[state][i]);
      }
      return intern(prefix);
    }

    //! \brief The priority-cut of \p state at its own accept, memoized. `cut` is O(state size) — it rebuilds
    //!        and re-interns the prefix — and a Unicode `klass_cp` byte-program makes states thousands of PCs
    //!        wide, so recomputing it once per match (per find_iter step) dominated. Cached per state, it is
    //!        computed once and then O(1). The state's accept index is fixed, so the cut is deterministic.
    std::uint32_t cut_cached(std::uint32_t state)
    {
      const std::uint32_t memo {state_cut_[state]};
      if (memo != no_transition) {
        return memo;
      }
      const std::size_t   flushes_before {stats_.flushes};
      const std::uint32_t result         {cut(state, state_match_idx_[state])}; // intern may flush/realloc
      if (stats_.flushes == flushes_before) {
        state_cut_[state] = result; // no flush: `state` is still valid, memoise the edge
      }
      return result;
    }

    static constexpr bool compute_eligibility(std::span<const instr> code)
    {
      for (const instr& in : code) {
        if (in.op == opcode::assert_position || in.op == opcode::assert_lookaround
            || in.op == opcode::klass_cp) {
          return false;   // position assertions / variable-width classes: no forward-DFA representation
        }
      }
      return true;
    }

    [[nodiscard]] bool consumes(std::int32_t pc,
                                std::uint8_t byte) const
    {
      const instr& in {code_[static_cast<std::size_t>(pc)]};
      if (in.op == opcode::byte) {
        return static_cast<std::uint8_t>(in.arg8) == byte;
      }
      if (in.op == opcode::klass) {
        return classes_[in.arg16].test(byte);
      }
      return false;   // match / anything else: not a consuming edge
    }

    [[nodiscard]] static std::int32_t consumed_width(std::int32_t /*pc*/)
    {
      return 1;
    }

    //! \brief Append the ordered epsilon-closure of \p pc to \p out (split priority; save/jump crossed),
    //!        collecting the consuming and `match` PCs. Uses \p seen to dedup within this closure.
    constexpr void close_into(std::int32_t               pc,
                              std::vector<std::int32_t>& out,
                              std::vector<char>&         seen) const
    {
      if (pc < 0 || static_cast<std::size_t>(pc) >= code_.size()) {
        return;
      }
      std::vector<std::int32_t> stack {pc};
      while (!stack.empty()) {
        const std::int32_t cur {stack.back()};
        stack.pop_back();
        if (cur < 0 || static_cast<std::size_t>(cur) >= code_.size() || seen[static_cast<std::size_t>(cur)] != 0) {
          continue;
        }
        seen[static_cast<std::size_t>(cur)] = 1;
        const instr& in {code_[static_cast<std::size_t>(cur)]};
        switch (in.op) {
          case opcode::byte:
          case opcode::klass:
          case opcode::match:
            out.push_back(cur);
            break;
          case opcode::split:
            stack.push_back(in.secondary_target);   // secondary pushed first -> primary explored first
            stack.push_back(in.primary_target);
            break;
          case opcode::jump:
            stack.push_back(in.primary_target);
            break;
          case opcode::save:
            stack.push_back(cur + 1);
            break;
          default:
            break;   // assert / klass_cp / lookaround: only reachable for ineligible programs (not built)
        }
      }
    }

    //! \brief Intern an ordered pc-set into a state id (cached). Flushes the cache when the budget is hit.
    constexpr std::uint32_t intern(const std::vector<std::int32_t>& pcs)
    {
      if (pcs.empty()) {
        return dead_state;
      }
      const std::uint32_t found {cache_.find(pcs, state_pcs_)};
      if (found != pc_set_cache::not_found) {
        return found;
      }
      if (state_pcs_.size() >= budget_) {
        flush();
        return intern_fresh(pcs);   // rebuild from empty; the seeded start remains reachable
      }
      return intern_fresh(pcs);
    }

    constexpr std::uint32_t intern_fresh(const std::vector<std::int32_t>& pcs)
    {
      const auto id {static_cast<std::uint32_t>(state_pcs_.size())};
      state_pcs_.push_back(pcs);
      state_trans_.emplace_back(alpha_.count, no_transition);
      state_trans_seeded_.emplace_back(alpha_.count, no_transition);
      std::uint32_t match_idx {no_match_idx};
      for (std::size_t i = 0; i < pcs.size(); ++i) {
        if (code_[static_cast<std::size_t>(pcs[i])].op == opcode::match) {
          match_idx = static_cast<std::uint32_t>(i); // the highest-priority accept in this state
          break;
        }
      }
      state_match_idx_.push_back(match_idx);
      state_match_.push_back(match_idx != no_match_idx ? 1 : 0);
      state_cut_.push_back(no_transition); // the priority-cut result, memoized lazily on first use
      cache_.insert(pcs, id);
      return id;
    }

    //! \brief Empty the cache back to the dead + start states (the eviction: bounded memory).
    constexpr void flush()
    {
      const bool first {state_pcs_.empty()};
      if (!first) {
        ++stats_.flushes;
        ++stats_.scan_flushes;
        ++evictions_;
        if (stats_.scan_flushes >= thrash_flushes) {
          thrashing_ = true;
        }
      }
      state_pcs_.clear();
      state_trans_.clear();
      state_trans_seeded_.clear();
      state_match_.clear();
      state_match_idx_.clear();
      state_cut_.clear();
      cache_.clear();
      // state 0 = dead (empty, self-looping), state 1 = start (closure of pc 0).
      state_pcs_.emplace_back();
      state_trans_.emplace_back(alpha_.count, dead_state);
      state_trans_seeded_.emplace_back(alpha_.count, dead_state);
      state_match_.push_back(0);
      state_match_idx_.push_back(no_match_idx);
      state_cut_.push_back(no_transition);
      std::vector<std::int32_t> start;
      std::vector<char>         seen(code_.size(), 0);
      close_into(0, start, seen);
      start_state_ = intern_fresh(start);
    }

    std::span<const instr>      code_;
    std::span<const char_class> classes_;
    lazy_byte_alphabet          alpha_;
    bool                        eligible_    {false};
    std::uint32_t               start_state_ {0};

    std::vector<std::vector<std::int32_t>>                                    state_pcs_;          //!< state id -> ordered pc-set.
    std::vector<std::vector<std::uint32_t>>                                   state_trans_;        //!< state id -> [class] -> next, unseeded (post-match).
    std::vector<std::vector<std::uint32_t>>                                   state_trans_seeded_; //!< state id -> [class] -> next, re-seeding (pre-match).
    std::vector<char>                                                         state_match_;        //!< state id -> accepts here.
    std::vector<std::uint32_t>                                                state_match_idx_;    //!< state id -> index of its first accept, or no_match_idx.
    std::vector<std::uint32_t>                                                state_cut_;          //!< state id -> memoized priority-cut result (no_transition = not yet computed).
    pc_set_cache                                                              cache_;

    std::size_t budget_    {state_budget};
    counters    stats_     {};
    std::size_t evictions_ {0};
    bool        thrashing_ {false};
  };

  /*!
   * \brief The start-finder companion to lazy_dfa. Given a match end, it finds the leftmost start (the design
   *        guide §7.6 contract). It runs the *inverted* program — the forward program's edges transposed,
   *        its consuming bytes kept — as a cached DFA over the text scanned right-to-left from the end,
   *        recording an accept each time it reaches the original start (`reverse-kLongest`: the furthest-back
   *        accept is the start). It needs no priority ordering — its states are plain unordered (sorted) PC
   *        sets and its rule is longest — so it is simpler than the forward pass. Dynamic only.
   */
  class reverse_dfa
  {
  public:

    static constexpr std::uint32_t dead_state    {0};
    static constexpr std::uint32_t no_transition {0xFFFFFFFFU};
    static constexpr std::size_t   state_budget  {4096};

    explicit constexpr reverse_dfa(std::span<const instr>      code,
                                   std::span<const char_class> classes,
                                   std::size_t                 budget = state_budget)
      : code_ {code}, classes_ {classes}, alpha_ {compute_lazy_alphabet(code, classes)}, eligible_ {compute_eligibility(code)},
        budget_ {budget}
    {
      // Transpose the program: rev_eps_[x] = the pcs with a forward epsilon edge to x; rev_consume_[x] = the
      // consuming pcs whose successor is x (a byte/klass at pc goes to pc+1).
      rev_eps_.assign(code.size(), {});
      rev_consume_.assign(code.size(), {});
      for (std::int32_t pc = 0; pc < static_cast<std::int32_t>(code.size()); ++pc) {
        const instr& in {code[static_cast<std::size_t>(pc)]};
        switch (in.op) {
          case opcode::byte:
          case opcode::klass:
            rev_consume_[static_cast<std::size_t>(pc) + 1].push_back(pc);
            break;
          case opcode::split:
            rev_eps_[static_cast<std::size_t>(in.primary_target)].push_back(pc);
            rev_eps_[static_cast<std::size_t>(in.secondary_target)].push_back(pc);
            break;
          case opcode::jump:
            rev_eps_[static_cast<std::size_t>(in.primary_target)].push_back(pc);
            break;
          case opcode::save:
            rev_eps_[static_cast<std::size_t>(pc) + 1].push_back(pc);
            break;
          case opcode::match:
            match_pc_ = pc; // the reverse start
            break;
          default:
            break;
        }
      }
      flush();
    }

    [[nodiscard]] bool eligible() const
    {
      return eligible_;
    }

    /*!
     * \brief The leftmost start of the match ending at \p e, not before \p resume. Scans the text backward
     *        from \p e over the inverted program, keeping the furthest-back position that reaches the
     *        program start (reverse-`kLongest`). Precondition: a match ends at \p e; eligible programs only.
     */
    [[nodiscard]] std::size_t reverse_start(std::string_view text,
                                            std::size_t      e,
                                            std::size_t      resume)
    {
      std::uint32_t state {start_state_}; // rev-closure of the forward `match`
      std::size_t   best  {npos};
      std::size_t   pos   {e};
      while (true) {
        if (state_has_start_[state] != 0) {
          best = pos; // reached the original start: [pos, e] matches; kLongest keeps the smallest pos
        }
        if (pos <= resume || state == dead_state) {
          break;
        }
        --pos;
        state = step(state, static_cast<std::uint8_t>(text[pos]));
      }
      return best;
    }

  private:

    constexpr void rev_closure(std::vector<std::int32_t>& set,
                               std::vector<char>&         seen) const
    {
      std::vector<std::int32_t> stack {set};
      while (!stack.empty()) {
        const std::int32_t pc {stack.back()};
        stack.pop_back();
        for (const std::int32_t pred : rev_eps_[static_cast<std::size_t>(pc)]) {
          if (seen[static_cast<std::size_t>(pred)] == 0) {
            seen[static_cast<std::size_t>(pred)] = 1;
            set.push_back(pred);
            stack.push_back(pred);
          }
        }
      }
      std::sort(set.begin(), set.end()); // unordered: a canonical (sorted) key, no priority
    }

    std::uint32_t step(std::uint32_t state,
                       std::uint8_t  byte)
    {
      const std::uint8_t  cls    {alpha_.of[byte]};
      const std::uint32_t cached {state_trans_[state][cls]};
      if (cached != no_transition) {
        return cached;
      }
      const std::vector<std::int32_t> pcs {state_pcs_[state]}; // copy: intern may realloc
      std::vector<std::int32_t>       next;
      std::vector<char>               seen(code_.size(), 0);
      for (const std::int32_t pc : pcs) {
        for (const std::int32_t pred : rev_consume_[static_cast<std::size_t>(pc)]) {
          if (consumes(pred, byte) && seen[static_cast<std::size_t>(pred)] == 0) {
            seen[static_cast<std::size_t>(pred)] = 1;
            next.push_back(pred);
          }
        }
      }
      rev_closure(next, seen);
      const std::uint32_t result {intern(next)};
      state_trans_[state][cls] = result;
      return result;
    }

    [[nodiscard]] bool consumes(std::int32_t pc,
                                std::uint8_t byte) const
    {
      const instr& in {code_[static_cast<std::size_t>(pc)]};
      if (in.op == opcode::byte) {
        return static_cast<std::uint8_t>(in.arg8) == byte;
      }
      return in.op == opcode::klass && classes_[in.arg16].test(byte);
    }

    static constexpr bool compute_eligibility(std::span<const instr> code)
    {
      for (const instr& in : code) {
        if (in.op == opcode::assert_position || in.op == opcode::assert_lookaround
            || in.op == opcode::klass_cp) {
          return false;
        }
      }
      return true;
    }

    constexpr std::uint32_t intern(const std::vector<std::int32_t>& pcs)
    {
      if (pcs.empty()) {
        return dead_state;
      }
      const std::uint32_t found {cache_.find(pcs, state_pcs_)};
      if (found != pc_set_cache::not_found) {
        return found;
      }
      if (state_pcs_.size() >= budget_) {
        flush();
      }
      const auto id {static_cast<std::uint32_t>(state_pcs_.size())};
      state_pcs_.push_back(pcs);
      state_trans_.emplace_back(alpha_.count, no_transition);
      bool has_start {false};
      for (const std::int32_t pc : pcs) {
        if (pc == 0) { // pc 0 is the program's save-0 start
          has_start = true;
          break;
        }
      }
      state_has_start_.push_back(has_start ? 1 : 0);
      cache_.insert(pcs, id);
      return id;
    }

    constexpr void flush()
    {
      state_pcs_.clear();
      state_trans_.clear();
      state_has_start_.clear();
      cache_.clear();
      state_pcs_.emplace_back();                          // dead state 0
      state_trans_.emplace_back(alpha_.count, dead_state);
      state_has_start_.push_back(0);
      std::vector<std::int32_t> start;
      std::vector<char>         seen(code_.size(), 0);
      if (match_pc_ >= 0) {
        seen[static_cast<std::size_t>(match_pc_)] = 1;
        start.push_back(match_pc_);
        rev_closure(start, seen);
      }
      start_state_ = intern(start);
    }

    std::span<const instr>                                                    code_;
    std::span<const char_class>                                               classes_;
    lazy_byte_alphabet                                                        alpha_;
    bool                                                                      eligible_    {false};
    std::int32_t                                                              match_pc_    {-1};
    std::uint32_t                                                             start_state_ {0};
    std::size_t                                                               budget_      {state_budget};
    std::vector<std::vector<std::int32_t>>                                    rev_eps_;         //!< transposed epsilon edges.
    std::vector<std::vector<std::int32_t>>                                    rev_consume_;     //!< transposed consuming edges (the pred consuming pcs).
    std::vector<std::vector<std::int32_t>>                                    state_pcs_;
    std::vector<std::vector<std::uint32_t>>                                   state_trans_;
    std::vector<char>                                                         state_has_start_; //!< state -> reaches the program start (an accept).
    pc_set_cache                                                              cache_;
  };
} // namespace real::detail

#endif // REAL_LAZY_DFA_HPP
