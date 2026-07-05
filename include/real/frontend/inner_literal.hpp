/*!
 * \file frontend/inner_literal.hpp
 * \brief Extracts a *required inner literal* from a pattern's AST — the substring that every match must
 *        contain (`(\w+)@(\w+)` -> `@`, `key=(\w+)` -> `key=`, `\d{4}-\d{2}` -> `-`). It is the memmem
 *        candidate an inner-literal prefilter scans for: find the literal, then confirm the surrounding
 *        pattern from that candidate. Pure over the node pool; **inert** — nothing routes on it yet.
 */
#ifndef REAL_FRONTEND_INNER_LITERAL_HPP
#define REAL_FRONTEND_INNER_LITERAL_HPP

#include <real/version.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <real/engine/prefilter.hpp> // byte_frequency (a lower tier — frontend may use the runtime)
#include <real/frontend/ast.hpp>

namespace real::detail {

  //! \brief The best required inner literal of a pattern (the memmem candidate). `len == 0` means the pattern
  //!        declined: an alternation, an optional (`?`/`*`/`{0,n}`), a lookaround or an anchor at the level
  //!        walked, or simply no literal run — anything that would make a required literal unsound.
  struct inner_literal
  {
    std::array<std::uint8_t, 16> bytes {};
    std::uint8_t                 len   {0};
    std::uint32_t                score {0}; //!< Selectivity: higher = rarer/longer = fewer memmem candidates.

    [[nodiscard]] constexpr bool found() const
    {
      return len > 0;
    }
  };

  //! \brief The most bytes an inner literal keeps (a longer memmem target is diminishing returns and storage).
  inline constexpr std::size_t inner_literal_max {16};

  namespace inner_literal_detail {

    //! \brief Selectivity of a byte run: the sum of per-byte rarity (`2000 - byte_frequency`). A sum (rather
    //!        than the rarest byte alone) approximates the product of per-byte match probabilities, so it
    //!        rewards both a rare byte and a longer literal — the two things that shrink the candidate count.
    constexpr std::uint32_t score_run(std::span<const std::uint8_t> run)
    {
      std::uint32_t s {0};
      for (std::uint8_t b : run) {
        s += static_cast<std::uint32_t>(2000U - byte_frequency(b));
      }
      return s;
    }

    //! \brief Score the current byte run and keep it if it beats `best`, then clear it (capped at
    //!        \ref inner_literal_max).
    constexpr void flush(std::vector<std::uint8_t>& run,
                         inner_literal&             best)
    {
      if (!run.empty()) {
        const std::size_t   len {run.size() < inner_literal_max ? run.size() : inner_literal_max};
        const std::uint32_t s   {score_run(std::span<const std::uint8_t>(run.data(), len))};
        if (s > best.score) {
          best.len   = static_cast<std::uint8_t>(len);
          best.score = s;
          for (std::size_t i = 0; i < len; ++i) {
            best.bytes[i] = run[i];
          }
        }
        run.clear();
      }
    }

    //! \brief Walk one node, appending guaranteed-present literal bytes to `run`. Returns `false` to DECLINE
    //!        the whole extraction — an alternation, an optional (`repeat` min 0), a lookaround or an anchor
    //!        would make a required inner literal unsound (a path could bypass it) or the pattern VM-routed.
    //!        Every byte appended is present in *every* match of the pattern (the soundness invariant); the
    //!        confirming scan then verifies the surrounding context around the candidate.
    constexpr bool walk(const ast&                 tree,
                        std::int32_t               idx,
                        std::vector<std::uint8_t>& run,
                        inner_literal&             best)
    {
      if (idx < 0) {
        return true;
      }
      const ast_node& n {tree.nodes[static_cast<std::size_t>(idx)]};
      switch (n.kind) {
        case node_kind::empty:
          return true;
        case node_kind::byte:
          run.push_back(n.byte);
          return true;
        case node_kind::concat:
          for (std::int32_t c = n.child; c >= 0; c = tree.nodes[static_cast<std::size_t>(c)].next) {
            if (!walk(tree, c, run, best)) {
              return false;
            }
          }
          return true;
        case node_kind::group:
          // A non-optional group (its optionality would be a `repeat` parent): descend, and let the run
          // continue across the group boundary (the bytes are contiguous in the match).
          return walk(tree, n.child, run, best);
        case node_kind::repeat: {
            if (n.min == 0) {
              return false; // ? * {0,n}: optional -> DECLINE (conservative v1)
            }
            flush(run, best);              // the repeat's width is variable; break the run around it
            std::vector<std::uint8_t> sub; // a guaranteed literal inside the first (min) copy
            if (!walk(tree, n.child, sub, best)) {
              return false;
            }
            flush(sub, best);
            return true;
          }
        case node_kind::klass:
        case node_kind::any:
          flush(run, best); // a non-byte guaranteed segment breaks the run
          return true;
        case node_kind::alternation:
        case node_kind::lookaround:
        case node_kind::anchor:
          return false; // DECLINE
      }
      return false;
    }
  } // namespace inner_literal_detail

  //! \brief Extract the best required inner literal from a pattern's AST (a pure function on the node pool).
  //!        Returns an empty \ref inner_literal when the pattern declines. Inert: nothing routes on it yet.
  constexpr inner_literal extract_inner_literal(const ast& tree)
  {
    inner_literal             best;
    std::vector<std::uint8_t> run;
    if (!inner_literal_detail::walk(tree, tree.root, run, best)) {
      return inner_literal {}; // declined
    }
    inner_literal_detail::flush(run, best); // the final run
    return best;
  }
} // namespace real::detail

#endif // REAL_FRONTEND_INNER_LITERAL_HPP
