// Case-insensitive classes fold their characters, ranges and properties, never a shorthand: `re` leaves
// `\w`, `\W`, `\d` as they are under IGNORECASE. Folding them changes a class
// through the odd orbits -- U+0345, a non-word combining mark in `\W`, folds to iota, so a folded
// `(?i)[^\W\d_]` loses ι, Ι and U+1FBE.
#include <sciforge/test/framework.hpp>

#include <real/automata/utf8_ranges.hpp>
#include <real/real.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

  //! Every scalar value, encoded, in order.
  std::string every_code_point()
  {
    std::string text;
    for (std::uint32_t cp {0}; cp < 0x110000U; ++cp) {
      if (cp >= 0xD800U && cp <= 0xDFFFU) {
        continue;
      }
      std::uint8_t      bytes[4] {};
      const std::size_t length   {real::detail::encode_utf8_bytes(cp, bytes)};
      for (std::size_t i {0}; i < length; ++i) {
        text.push_back(static_cast<char>(bytes[i]));
      }
    }
    return text;
  }

  //! Where \p pattern matches in \p text.
  std::vector<std::size_t> starts(std::string_view pattern,
                                  std::string_view text)
  {
    const real::regex        re {pattern};
    std::vector<std::size_t> out;
    for (const auto& m : re.find_iter(text)) {
      out.push_back(m.start());
    }
    return out;
  }
} // namespace

// Over every code point, an icase class equals its unfolded twin with the written members' partners
// spelled out.
TEST(icase_leaves_shorthands_unfolded)
{
  const std::string text {every_code_point()};
  const struct
  {
    std::string_view folded;
    std::string_view twin;
  } pairs[] {
    {.folded = "(?i)[^\\W\\d_]", .twin = "[^\\W\\d_]"},
    {.folded = "(?i)[^\\W]", .twin = "\\w"},
    {.folded = "(?i)\\w", .twin = "\\w"},
    {.folded = "(?i)\\W", .twin = "\\W"},
    {.folded = "(?i)[^\\Wa]", .twin = "[^\\WaA]"},
  };
  for (const auto& pair : pairs) {
    const std::vector<std::size_t> folded {starts(pair.folded, text)};
    EXPECT(!folded.empty());
    EXPECT(folded == starts(pair.twin, text));
  }
  // The orbit that broke it, by name: iota in all three spellings is a letter.
  for (const std::string_view iota : {"\xCE\xB9", "\xCE\x99", "\xE1\xBE\xBE"}) {
    EXPECT(real::regex {"(?i)[^\\W\\d_]"}.fullmatch(iota).matched());
  }
  EXPECT(!real::regex {"(?i)\\w"}.fullmatch(std::string_view {"\xCD\x85"}).matched()); // U+0345
}
