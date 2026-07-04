/*!
 * \file lazy_dfa.hpp
 * \brief A lazy, priority-preserving forward DFA over the Pike program (the inert scaffolding).
 *
 * DISTINCT from \ref real::dfa (`<real/dfa.hpp>`): that one is a capture-free *maximal-munch* recognizer
 * over unordered NFA-state sets (a lexer's rule dispatch). This one memoizes the *leftmost-first* Pike
 * closure — its DFA states are **ordered** NFA-state sets, so a `kFirstMatch` forward pass reports the same
 * match boundary the Pike VM would. It reuses only the byte-class idea (an alphabet smaller than 256), not
 * that engine's subset construction.
 *
 * \note **This scaffolding is INERT.** The state cache, the budget/eviction policy, the thrash detector
 *       and the counters are built and exercised by tests, but nothing in the matcher routes through this
 *       yet — the forward pass and its differential property-net against Pike arrive when it is wired in.
 *       Dynamic only: the cache is mutable, so it never participates in constant evaluation.
 */
#ifndef REAL_LAZY_DFA_HPP
#define REAL_LAZY_DFA_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "program.hpp"

namespace real::detail {

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
  inline lazy_byte_alphabet compute_lazy_alphabet(std::span<const instr>      code,
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
   * \brief A lazy priority-preserving forward DFA over a Pike program (inert scaffolding).
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
    explicit lazy_dfa(std::span<const instr>      code,
                      std::span<const char_class> classes,
                      std::size_t                 budget = state_budget)
      : code_ {code}, classes_ {classes}, alpha_ {compute_lazy_alphabet(code, classes)}, budget_ {budget}
    {
      eligible_ = compute_eligibility(code);
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

    static bool compute_eligibility(std::span<const instr> code)
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
    void close_into(std::int32_t               pc,
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
    std::uint32_t intern(const std::vector<std::int32_t>& pcs)
    {
      if (pcs.empty()) {
        return dead_state;
      }
      const auto it {cache_.find(pcs)};
      if (it != cache_.end()) {
        return it->second;
      }
      if (state_pcs_.size() >= budget_) {
        flush();
        return intern_fresh(pcs);   // rebuild from empty; the seeded start remains reachable
      }
      return intern_fresh(pcs);
    }

    std::uint32_t intern_fresh(const std::vector<std::int32_t>& pcs)
    {
      const auto id {static_cast<std::uint32_t>(state_pcs_.size())};
      state_pcs_.push_back(pcs);
      state_trans_.emplace_back(alpha_.count, no_transition);
      bool match {false};
      for (const std::int32_t pc : pcs) {
        if (code_[static_cast<std::size_t>(pc)].op == opcode::match) {
          match = true;
          break;
        }
      }
      state_match_.push_back(match ? 1 : 0);
      cache_.emplace(pcs, id);
      return id;
    }

    //! \brief Empty the cache back to the dead + start states (the eviction: bounded memory).
    void flush()
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
      state_match_.clear();
      cache_.clear();
      // state 0 = dead (empty, self-looping), state 1 = start (closure of pc 0).
      state_pcs_.emplace_back();
      state_trans_.emplace_back(alpha_.count, dead_state);
      state_match_.push_back(0);
      std::vector<std::int32_t> start;
      std::vector<char>         seen(code_.size(), 0);
      close_into(0, start, seen);
      start_state_ = intern_fresh(start);
    }

    struct pc_set_hash
    {
      std::size_t operator()(const std::vector<std::int32_t>& v) const
      {
        std::size_t h {1469598103934665603ULL};
        for (const std::int32_t x : v) {
          h = (h ^ static_cast<std::size_t>(static_cast<std::uint32_t>(x))) * 1099511628211ULL;
        }
        return h;
      }
    };

    std::span<const instr>      code_;
    std::span<const char_class> classes_;
    lazy_byte_alphabet          alpha_;
    bool                        eligible_    {false};
    std::uint32_t               start_state_ {0};

    std::vector<std::vector<std::int32_t>>                                    state_pcs_;   //!< state id -> ordered pc-set.
    std::vector<std::vector<std::uint32_t>>                                   state_trans_; //!< state id -> [class] -> next (no_transition = lazy).
    std::vector<char>                                                         state_match_; //!< state id -> accepts here.
    std::unordered_map<std::vector<std::int32_t>, std::uint32_t, pc_set_hash> cache_;

    std::size_t budget_    {state_budget};
    counters    stats_     {};
    std::size_t evictions_ {0};
    bool        thrashing_ {false};
  };
} // namespace real::detail

#endif // REAL_LAZY_DFA_HPP
