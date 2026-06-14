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
 * \brief Recognizes the byte-level UTF-8 expansion the compiler emits for `.`
 *        and negated classes (see `emit_codepoint_class`).
 *
 * The 16-instruction block at \p b is one ASCII class plus the four
 * lead/continuation byte branches. The shape and the UTF-8 byte ranges are
 * both checked, so the match is unambiguous.
 *
 * \param[in] code    The instruction stream.
 * \param[in] classes The interned classes.
 * \param[in] b       Index of the block's leading split.
 * \return The ASCII-class index of the codepoint class, or -1 if \p b does not
 *         begin such a block.
 */
constexpr std::int32_t codepoint_class_at(std::span<const instr>      code,
                                          std::span<const char_class> classes,
                                          std::size_t                 b)
{
  if (b + 16 > code.size()) {
    return -1;
  }
  const auto is_range = [&](std::uint16_t idx, int lo, int hi) {
    char_class want;
    want.set_range(static_cast<std::uint8_t>(lo), static_cast<std::uint8_t>(hi));
    return idx < classes.size() && classes[idx] == want;
  };
  const auto bi = [&](std::size_t off) { return static_cast<std::int32_t>(b + off); };
  const bool shape {
    code[b].op == opcode::split && code[b].x == bi(1) && code[b].y == bi(3) &&
    code[b + 1].op == opcode::klass && code[b + 2].op == opcode::jump &&
    code[b + 3].op == opcode::split && code[b + 3].x == bi(4) && code[b + 3].y == bi(7) &&
    code[b + 4].op == opcode::klass && code[b + 5].op == opcode::klass &&
    code[b + 6].op == opcode::jump && code[b + 7].op == opcode::split &&
    code[b + 7].x == bi(8) && code[b + 7].y == bi(12) && code[b + 8].op == opcode::klass &&
    code[b + 9].op == opcode::klass && code[b + 10].op == opcode::klass &&
    code[b + 11].op == opcode::jump && code[b + 12].op == opcode::klass &&
    code[b + 13].op == opcode::klass && code[b + 14].op == opcode::klass &&
    code[b + 15].op == opcode::klass};
  const bool ranges {
    is_range(code[b + 4].arg16, 0xC2, 0xDF) && is_range(code[b + 5].arg16, 0x80, 0xBF) &&
    is_range(code[b + 8].arg16, 0xE0, 0xEF) && is_range(code[b + 12].arg16, 0xF0, 0xF4)};
  if (!shape || !ranges) {
    return -1;
  }
  const std::uint16_t ascii {code[b + 1].arg16};
  for (int x = 0x80; x <= 0xFF; ++x) {
    if (classes[ascii].test(static_cast<std::uint8_t>(x))) {
      return -1; // the ASCII branch must hold ASCII bytes only
    }
  }
  return static_cast<std::int32_t>(ascii);
}

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

  // "fixed shape": a straight-line run of byte/klass with no branches or
  // assertions and no captures (exactly one leading and one trailing save).
  // The whole match is fixed width, so one walk verifies it. Covers class{n}
  // and mixed sequences such as \d{4}-\d{2}-\d{2}; pure literals are caught by
  // the exact-literal path first. Negated classes and `.` expand to byte-level
  // branches, so they never form this shape.
  {
    std::size_t  i {};
    std::int32_t lead_saves {};
    while (i < code.size() && code[i].op == opcode::save) {
      ++lead_saves;
      ++i;
    }
    std::int32_t width {};
    while (i < code.size() && (code[i].op == opcode::byte || code[i].op == opcode::klass)) {
      ++width;
      ++i;
    }
    std::int32_t trail_saves {};
    while (i < code.size() && code[i].op == opcode::save) {
      ++trail_saves;
      ++i;
    }
    if (lead_saves == 1 && trail_saves == 1 && width >= 1 && i + 1 == code.size() &&
        code[i].op == opcode::match) {
      h.fixed_shape = true;
    }
  }

  // Whole pattern is a single codepoint class (`.`/negated class), optionally a
  // greedy `+`. Layout: save 0, the 16-instruction codepoint block (at 1..16),
  // then either save 1, match (bare, 19 instructions) or split(loop, exit),
  // save 1, match (the `+`, 20 instructions). No captures; `*` is excluded
  // because its empty match rules out a consuming fast path.
  if (code.size() == 19 && code[0].op == opcode::save &&
      codepoint_class_at(code, classes, 1) >= 0 && code[17].op == opcode::save &&
      code[18].op == opcode::match) {
    h.codepoint_class_ascii = codepoint_class_at(code, classes, 1);
    h.codepoint_class_plus  = false;
  }
  else if (code.size() == 20 && code[0].op == opcode::save &&
           codepoint_class_at(code, classes, 1) >= 0 && code[17].op == opcode::split &&
           code[17].x == 1 && code[18].op == opcode::save && code[19].op == opcode::match) {
    h.codepoint_class_ascii = codepoint_class_at(code, classes, 1);
    h.codepoint_class_plus  = true;
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
