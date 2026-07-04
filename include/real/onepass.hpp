/*!
 * \file onepass.hpp
 * \brief The one-pass builder: decides whether a pattern is *one-pass* and, if so, tabulates a deterministic
 *        capture-writing automaton over the byte-program.
 *
 * A pattern is **one-pass** (Brüggemann-Klein & Wood, "One-unambiguous regular languages"; RE2 `onepass.cc`)
 * when, matched anchored, at most one thread crosses any byte — the non-determinism is contained. For such a
 * pattern the capture slots can be filled in a single left-to-right pass with no thread lists at all: at each
 * node, the byte read selects exactly one outgoing edge, whose recorded conditions say which slots take the
 * current position. `(\w+)@(\w+)` is one-pass (inside `\w+` an `@` cannot extend the run, so there is no
 * ambiguity); `(\w+)_(\w+)` is not (`_` is itself a `\w`, so a `_` both extends group 1 and starts the
 * separator — a genuine conflict).
 *
 * This header is the **builder only** (the arc's first slice): it classifies a pattern and, when one-pass,
 * produces the node table. Nothing runs matching through it yet. It builds over the L2.5 byte_program
 * (Unicode `\w \d \s` already expanded to byte ranges), so the one-pass check runs at the byte level. The
 * table format here is a readable struct, not RE2's packed `uint32`; packing is a runtime concern (a later
 * slice) that the differential would catch either way.
 */
#ifndef REAL_ONEPASS_HPP
#define REAL_ONEPASS_HPP

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "lazy_dfa.hpp"
#include "program.hpp"

namespace real::detail {

  //! \brief One outgoing edge of a one-pass node, for a byte-class: the next node and the capture slots that
  //!        take the current position as the byte is consumed. Two epsilon paths reaching the same class with
  //!        a different edge is the one-pass conflict — the pattern is then rejected.
  struct onepass_edge
  {
    std::uint32_t next     {0};     //!< Next node id (valid only when \ref assigned).
    std::uint64_t cap_mask {0};     //!< Bit i set => write the current position into slot i on this edge.
    bool          assigned {false}; //!< Whether this byte-class has an edge from this node.
  };

  //! \brief A one-pass node: one edge per byte-class, plus whether the run may end here and with what
  //!        captures. Nodes are the points the automaton can be in *between* byte reads.
  struct onepass_node
  {
    std::vector<onepass_edge> edge;                   //!< Indexed by byte-class.
    bool                      matches        {false}; //!< Reaching `match` from here (via epsilon).
    std::uint64_t             match_cap_mask {0};     //!< Slots written when the match is taken.
  };

  /*!
   * \brief Builds and holds the one-pass classification (and table, when eligible) of a byte-program.
   *
   * Construction floods the program from the start: for each node it walks the epsilon-closure (`split`,
   * `jump`, `save`) accumulating the capture mask, and every consuming instruction (`byte`, `klass`) writes
   * the edge for its byte-class(es). A byte-class written twice with a different edge, a second reachable
   * `match` with different captures, or an epsilon cycle (a nullable loop) each means *not one-pass* and the
   * build bails with a human-readable reason. Node and slot counts are capped, so a pathological program is
   * rejected rather than explored without bound.
   */
  class onepass
  {
  public:

    static constexpr std::uint32_t no_node   {0xFFFFFFFFU}; //!< "No node yet" sentinel in the pc->node map.
    static constexpr std::size_t   max_nodes {65000};       //!< Node cap (RE2's), a memory/So-DoS bound.
    static constexpr std::size_t   max_slots {10};          //!< Slot-pointer cap: group 0 + four user groups.

    explicit onepass(const byte_program& bp)
    {
      if (!bp.eligible) {
        bail("the byte-program is itself ineligible (a position assertion or lookaround)");
        return;
      }
      build(bp);
    }

    [[nodiscard]] bool eligible() const
    {
      return eligible_;
    }

    [[nodiscard]] const std::string& bail_reason() const
    {
      return bail_reason_;
    }

    [[nodiscard]] std::size_t node_count() const
    {
      return nodes_.size();
    }

    [[nodiscard]] std::uint16_t num_classes() const
    {
      return alpha_.count;
    }

    [[nodiscard]] const std::vector<onepass_node>& nodes() const
    {
      return nodes_;
    }

    //! \brief The byte-class of \p byte, for a runtime that walks this table.
    [[nodiscard]] std::uint8_t class_of(std::uint8_t byte) const
    {
      return alpha_.of[byte];
    }

    //! \brief The number of capture slots (group 0 start/end plus each group's).
    [[nodiscard]] std::size_t slot_count() const
    {
      return slot_count_;
    }

    /*!
     * \brief Fills \p out with the capture slots of the one-pass match on `text[s, e)` — the single left-to-
     *        right pass the whole arc is for, no thread lists. \p text is the **full** subject (never a
     *        substring: assertions look at `s - 1` and `e`). Anchored at \p s; this is *fullmatch-on-span*
     *        (the span the router located): it consumes to \p e and requires the run to accept exactly there.
     *        `\ref real::npos` marks a slot no edge wrote. Returns `false` (leaving \p out unspecified) if the
     *        pattern is ineligible or the span does not in fact match — which the caller has already ruled
     *        out for a router-supplied span.
     *
     * \param[in]  text The full subject.
     * \param[in]  s    Match start (anchor).
     * \param[in]  e    Match end (the run must accept here).
     * \param[out] out  Capture slots, sized to \ref slot_count.
     */
    [[nodiscard]] bool extract(std::string_view          text,
                               std::size_t               s,
                               std::size_t               e,
                               std::vector<std::size_t>& out) const
    {
      if (!eligible_) {
        return false;
      }
      out.assign(slot_count_, npos);
      std::uint32_t node {0}; // node 0 is the start (the closure of pc 0)
      for (std::size_t pos = s; pos < e; ++pos) {
        const std::uint8_t  cls  {alpha_.of[static_cast<std::uint8_t>(text[pos])]};
        const onepass_edge& edge {nodes_[node].edge[cls]};
        if (!edge.assigned) {
          return false;                                             // no outgoing edge for this byte — the span does not match
        }
        for (std::uint64_t m = edge.cap_mask; m != 0; m &= m - 1) {
          out[static_cast<std::size_t>(std::countr_zero(m))] = pos; // saves crossed before this byte take pos
        }
        node = edge.next;
      }
      if (!nodes_[node].matches) {
        return false;                                           // reached e but not at an accept
      }
      for (std::uint64_t m = nodes_[node].match_cap_mask; m != 0; m &= m - 1) {
        out[static_cast<std::size_t>(std::countr_zero(m))] = e; // saves crossed to the match take e
      }
      return true;
    }

  private:

    void bail(std::string reason)
    {
      eligible_    = false;
      bail_reason_ = std::move(reason);
    }

    //! \brief Get-or-create the node whose entry pc is \p pc, enqueueing a fresh one for the flood.
    std::uint32_t node_of(std::int32_t               pc,
                          std::vector<std::int32_t>& queue)
    {
      std::uint32_t& id {pc_to_node_[static_cast<std::size_t>(pc)]};
      if (id == no_node) {
        id = static_cast<std::uint32_t>(nodes_.size());
        onepass_node fresh;
        fresh.edge.assign(alpha_.count, onepass_edge {});
        nodes_.push_back(std::move(fresh));
        queue.push_back(pc);
      }
      return id;
    }

    void build(const byte_program& bp)
    {
      code_    = bp.code;
      classes_ = bp.classes;
      alpha_   = compute_lazy_alphabet(bp.code, bp.classes);

      // A representative byte per class: all bytes of a class satisfy the same predicates, so testing one
      // decides the whole class. rep[c] is the first byte mapped to class c.
      rep_.assign(alpha_.count, 0);
      std::vector<char> seen_cls(alpha_.count, 0);
      for (unsigned b = 0; b < 256U; ++b) {
        const std::uint8_t c {alpha_.of[b]};
        if (seen_cls[c] == 0) {
          seen_cls[c] = 1;
          rep_[c]     = static_cast<std::uint8_t>(b);
        }
      }

      std::size_t max_slot {0};
      for (const instr& in : bp.code) {
        if (in.op == opcode::save) {
          max_slot = std::max(max_slot, static_cast<std::size_t>(in.arg16));
        }
      }
      slot_count_ = max_slot + 1;
      if (slot_count_ > max_slots) {
        bail("too many capture slots (" + std::to_string(slot_count_) + " > " + std::to_string(max_slots)
             + "): the general Pike VM keeps these");
        return;
      }

      pc_to_node_.assign(bp.code.size(), no_node);
      std::vector<std::int32_t> queue;
      node_of(0, queue); // the start node
      while (!queue.empty() && eligible_) {
        const std::int32_t pc {queue.back()};
        queue.pop_back();
        std::vector<char> on_path(bp.code.size(), 0);
        build_edges(pc, 0, on_path, pc_to_node_[static_cast<std::size_t>(pc)], queue);
        if (nodes_.size() > max_nodes) {
          bail("node cap exceeded (" + std::to_string(nodes_.size()) + ")");
          return;
        }
      }
    }

    //! \brief Whether instruction \p in consumes a byte of class \p cls (tested via the class representative).
    [[nodiscard]] bool consumes_class(const instr&  in,
                                      std::uint16_t cls) const
    {
      const std::uint8_t b {rep_[cls]};
      if (in.op == opcode::byte) {
        return static_cast<std::uint8_t>(in.arg8) == b;
      }
      return in.op == opcode::klass && classes_[in.arg16].test(b);
    }

    //! \brief Walk the epsilon-closure from \p pc, writing this node's edges. \p on_path detects epsilon
    //!        cycles (a nullable loop => not one-pass). \p cap_mask accumulates the slots crossed so far.
    void build_edges(std::int32_t               pc,
                     std::uint64_t              cap_mask,
                     std::vector<char>&         on_path,
                     std::uint32_t              node_id,
                     std::vector<std::int32_t>& queue)
    {
      if (!eligible_) {
        return;
      }
      if (on_path[static_cast<std::size_t>(pc)] != 0) {
        bail("epsilon cycle (a nullable loop) at pc " + std::to_string(pc) + ": not one-pass");
        return;
      }
      on_path[static_cast<std::size_t>(pc)] = 1;
      const instr& in {code_[static_cast<std::size_t>(pc)]};
      switch (in.op) {
        case opcode::byte:
        case opcode::klass: {
            const std::uint32_t next {node_of(pc + 1, queue)};
            onepass_node&       node {nodes_[node_id]};
            for (std::uint16_t cls = 0; cls < alpha_.count; ++cls) {
              if (!consumes_class(in, cls)) {
                continue;
              }
              onepass_edge& slot {node.edge[cls]};
              if (slot.assigned && (slot.next != next || slot.cap_mask != cap_mask)) {
                bail("byte-class conflict at node " + std::to_string(node_id) + ", class "
                     + std::to_string(cls) + " (pc " + std::to_string(pc) + "): not one-pass");
                return;
              }
              slot = onepass_edge {.next = next, .cap_mask = cap_mask, .assigned = true};
            }
            break; // a consuming instruction ends this epsilon path
          }
        case opcode::match: {
            onepass_node& node {nodes_[node_id]};
            if (node.matches && node.match_cap_mask != cap_mask) {
              bail("second distinct match at node " + std::to_string(node_id) + ": not one-pass");
              return;
            }
            node.matches        = true;
            node.match_cap_mask = cap_mask;
            break;
          }
        case opcode::split:
          build_edges(in.primary_target, cap_mask, on_path, node_id, queue);
          build_edges(in.secondary_target, cap_mask, on_path, node_id, queue);
          break;
        case opcode::jump:
          build_edges(in.primary_target, cap_mask, on_path, node_id, queue);
          break;
        case opcode::save:
          build_edges(pc + 1, cap_mask | (std::uint64_t {1} << in.arg16), on_path, node_id, queue);
          break;
        default:
          bail("unexpected op in byte-program (assertions/lookaround/klass_cp should be absent)");
          return;
      }
      on_path[static_cast<std::size_t>(pc)] = 0; // backtrack: only a cycle bails, a diamond is fine
    }

    std::span<const instr>      code_;
    std::span<const char_class> classes_;
    lazy_byte_alphabet          alpha_;
    std::vector<std::uint8_t>   rep_;         //!< class -> a representative byte.
    std::vector<std::uint32_t>  pc_to_node_;  //!< pc -> node id (or no_node).
    std::vector<onepass_node>   nodes_;
    std::size_t                 slot_count_ {0};
    bool                        eligible_   {true};
    std::string                 bail_reason_;
  };
} // namespace real::detail

#endif // REAL_ONEPASS_HPP
