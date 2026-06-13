/*!
 * \file prefilter.hpp
 * \brief Search acceleration: pattern analysis and candidate-finding.
 *
 * Extracts \ref real::detail::pattern_hints from a compiled program (required
 * literal prefix, start anchoring, possible-first-byte set, fast-path shapes)
 * and provides the primitives the engine uses to skip ahead when no thread is
 * alive. Uses `memchr` / the platform substring search at run time and plain
 * loops in constexpr. Hints never affect \e what matches — only how fast; an
 * equivalence test runs the engine with hints disabled to prove it.
 */
#ifndef REAL_PREFILTER_HPP
#define REAL_PREFILTER_HPP

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "charclass.hpp"
#include "program.hpp"

namespace real::detail {

/*!
 * \brief Walks a compiled program once to derive its search hints.
 * \param[in] code    The instruction stream.
 * \param[in] classes The interned character classes referenced by \p code.
 * \return The \ref pattern_hints (anchoring, literal prefix, first-byte set,
 *         and the `class+` / exact-literal fast-path flags).
 */
constexpr pattern_hints analyze_program(std::span<const instr>      code,
                                        std::span<const char_class> classes)
{
  pattern_hints h;

  // Start anchoring: the first non-save instruction tells whether every
  // match must begin at position 0 (or at a line start).
  std::size_t pc {};
  while (code[pc].op == opcode::save) {
    ++pc;
  }
  if (code[pc].op == opcode::assert_position) {
    const auto kind {static_cast<assert_kind>(code[pc].arg8)};
    h.anchored_start = kind == assert_kind::text_start;
    h.line_anchored  = kind == assert_kind::line_start;
  }

  // Required literal prefix: consecutive byte instructions from the start.
  // Saves and assertions do not consume, so they are crossed: every match
  // still has to begin with the collected bytes (hints only ever filter
  // candidate positions; the engine verifies).
  std::size_t p {};
  while (h.prefix_size < h.prefix.size()) {
    if (code[p].op == opcode::save || code[p].op == opcode::assert_position) {
      ++p;
      continue;
    }
    if (code[p].op != opcode::byte) {
      break;
    }
    h.prefix[h.prefix_size] = static_cast<char>(code[p].arg8);
    ++h.prefix_size;
    ++p;
  }

  // Exact literal fast-path hint: the collected prefix bytes are the entire
  // match (no asserts appear after the first byte instr up to match; only
  // saves may be crossed after the bytes). Trailing/inter asserts ($, \b after,
  // etc) are post-filters on the byte candidate and must go through normal
  // VM so they can reject bad cands and let search continue. Leading asserts
  // (before any byte) are ok (next_candidate + replay will handle).
  if (h.prefix_size > 0) {
    bool has_inter_or_trailing_assert {};
    bool seen_byte {};
    for (std::size_t i = 0; i < code.size() && !has_inter_or_trailing_assert; ++i) {
      if (code[i].op == opcode::byte) {
        seen_byte = true;
      }
      else if (seen_byte && code[i].op == opcode::assert_position) {
        has_inter_or_trailing_assert = true;
      }
      else if (seen_byte && code[i].op == opcode::match) {
        break;
      }
    }
    if (!has_inter_or_trailing_assert) {
      std::size_t q {p};
      while (q < code.size() && code[q].op == opcode::save) {
        ++q;
      }
      if (q < code.size() && code[q].op == opcode::match) {
        h.exact_literal_len = h.prefix_size;
      }
    }
  }

  // Possible first bytes: DFS over the epsilon closure of pc 0. Assertions
  // are crossed conservatively (they constrain positions, not bytes). If
  // `match` is reachable without consuming, an empty match is possible and
  // no byte-based skipping is sound.
  std::vector<bool>         visited(code.size(), false);
  std::vector<std::int32_t> stack;
  stack.push_back(0);
  bool empty_match_possible {};
  while (!stack.empty()) {
    const std::int32_t at {stack.back()};
    stack.pop_back();
    if (visited[static_cast<std::size_t>(at)]) {
      continue;
    }
    visited[static_cast<std::size_t>(at)] = true;
    const instr& in {code[static_cast<std::size_t>(at)]};
    switch (in.op) {
      case opcode::save:
      case opcode::assert_position:
        stack.push_back(at + 1);
        break;
      case opcode::jump:
        stack.push_back(in.x);
        break;
      case opcode::split:
        stack.push_back(in.x);
        stack.push_back(in.y);
        break;
      case opcode::byte:
        h.first_bytes.set(in.arg8);
        break;
      case opcode::klass:
        h.first_bytes.merge(classes[in.arg16]);
        break;
      case opcode::match:
        empty_match_possible = true;
        break;
    }
  }
  h.first_bytes_valid = !empty_match_possible && !h.first_bytes.empty();

  // "class+" shape: save 0, klass, split(back to the klass, exit),
  // save 1, match — greedy only (the lazy variant has different
  // semantics) and no capture groups.
  if (code.size() == 5 && code[0].op == opcode::save && code[1].op == opcode::klass &&
      code[2].op == opcode::split && code[2].x == 1 && code[2].y == 3 &&
      code[3].op == opcode::save && code[4].op == opcode::match) {
    h.greedy_class_loop = code[1].arg16;
  }

  if (h.prefix_size > 0) {
    h.single_first = static_cast<unsigned char>(h.prefix[0]);
  }
  else if (h.first_bytes_valid) {
    int found {-1};
    for (unsigned b = 0; b < 256 && found != -2; ++b) {
      if (h.first_bytes.test(static_cast<std::uint8_t>(b))) {
        found = found == -1 ? static_cast<int>(b) : -2;
      }
    }
    h.single_first = found >= 0 ? static_cast<std::int16_t>(found) : -1;
  }
  return h;
}

/*!
 * \brief Index of \p byte in `text[pos..)`, or \ref real::npos.
 *
 * Uses `memchr` at run time and a plain loop during constant evaluation.
 *
 * \param[in] text The subject text.
 * \param[in] pos  Index to start scanning from.
 * \param[in] byte The byte to find.
 * \return The index of the first occurrence at or after \p pos, else npos.
 */
constexpr std::size_t find_byte(std::string_view text, std::size_t pos, char byte)
{
  if (pos >= text.size()) {
    return npos;
  }
  if (!std::is_constant_evaluated()) {
    const void* hit {std::memchr(text.data() + pos, byte, text.size() - pos)};
    return hit == nullptr
             ? npos
             : static_cast<std::size_t>(static_cast<const char*>(hit) - text.data());
  }
  for (std::size_t i = pos; i < text.size(); ++i) {
    if (text[i] == byte) {
      return i;
    }
  }
  return npos;
}

/*!
 * \brief First position >= \p pos where \p prefix occurs in \p text, or npos.
 *
 * A thin wrapper over the platform's substring search, which is correct and
 * well tuned for the short prefixes (<= 16 bytes) the analyzer extracts.
 *
 * \param[in] text   The subject text.
 * \param[in] pos    Index to start searching from.
 * \param[in] prefix The literal to locate (empty matches at \p pos).
 * \return The index of the first occurrence at or after \p pos, else npos.
 */
constexpr std::size_t find_prefix(std::string_view text, std::size_t pos, std::string_view prefix)
{
  if (prefix.empty()) {
    return pos;
  }
  if (pos >= text.size()) {
    return npos;
  }
  const auto off {text.substr(pos).find(prefix)};
  if (off == std::string_view::npos) {
    return npos;
  }
  return pos + off;
}

} // namespace real::detail

#endif // REAL_PREFILTER_HPP
