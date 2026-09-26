//! basic_regex::can_extend: whether match(text, pos) could come out differently with more text. A caller
//! lexing text that arrives in pieces commits to a token only on a false, so a false where more text does
//! change the match is a wrong token; a true where it does not only delays one.
#include <sciforge/test/framework.hpp>

#include <real/real.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

  //! Every span of match(text, pos), or empty when it does not match.
  std::vector<std::size_t> spans(const real::regex& re,
                                 std::string_view   text,
                                 std::size_t        pos)
  {
    std::vector<std::size_t> out;
    if (const auto m {re.match(text, pos)}) {
      for (std::size_t g {0}; g < m.size(); ++g) {
        out.push_back(m.start(g));
        out.push_back(m.end(g));
      }
    }
    return out;
  }
} // namespace

// Soundness against brute force: whenever some continuation of up to two pieces changes the match (any
// group), can_extend said so. Patterns cover consuming loops, alternation priority, captures, every
// assertion that looks right, bounded and unbounded lookaheads, lookbehinds, possessive loops (one that ends
// the pattern, where nothing after it reads the end), and
// code-point classes whose code points the end of the text can cut in half.
TEST(can_extend_is_true_whenever_more_text_changes_the_match)
{
  const std::string_view patterns[] {"[a-z]+", "a|ab", "(a)(b)?", "\\w+\\b", "\\w+$", "a$", "x\\Z", "\\bfoo",
                                     "\\w+(?=\\()", "a(?!b)", "(?=.*z)a", "(?<=a)b", "a*+b?", "é+", "\\w+",
                                     "[^ ]+", "(?m)a$", "a\\B", "\\d{2,3}", "(ab|a)(c?)", "", "\\s*",
                                     "a*+", "[a-z]++", "é*+"};
  const std::string_view texts[]  {"", "a", "ab", "ab ", "abc", "foo", "fo", "x", "é", "\xC3", "a\n", "12", "a(",
                                   "ab(", "za"};
  const std::string_view pieces[] {"a", "b", " ", "z", "(", "\n", "1", "\xA9", "\xC3", "é", "_"};
  int                    changed  {0};
  int                    compared {0};
  int                    waited   {0}; // true where no two-piece continuation changes the match
  for (const std::string_view pattern : patterns) {
    const real::regex re {pattern};
    for (const std::string_view text : texts) {
      for (std::size_t pos {0}; pos <= text.size(); ++pos) {
        const std::vector<std::size_t> now     {spans(re, text, pos)};
        bool                           changes {false};
        for (const std::string_view first : pieces) {
          for (const std::string_view second : pieces) {
            std::string longer {text};
            longer += first;
            changes = changes || spans(re, longer, pos) != now;
            longer += second;
            changes = changes || spans(re, longer, pos) != now;
          }
        }
        if (changes) {
          ++changed;
          if (!re.can_extend(text, pos)) {
            std::printf("can_extend said false: /%s/ on \"%s\" at %zu\n", std::string(pattern).c_str(),
                        std::string(text).c_str(), pos);
          }
          EXPECT(re.can_extend(text, pos));
        }
        else if (re.can_extend(text, pos)) {
          ++waited;
        }
        ++compared;
      }
    }
  }
  std::printf("can_extend: %d of %d anchors changed by more text; %d more answered true\n", changed, compared,
              waited);
  EXPECT(changed >= 200); // the corpus does reach texts more input changes
  EXPECT(compared >= 900);
}

// Tightness where it matters to a lexer: a token that the next byte already ends is final.
TEST(can_extend_is_false_once_the_text_decides)
{
  EXPECT(!real::regex {"[a-z]+"}.can_extend("ab ", 0));
  EXPECT(!real::regex {"[a-z]+"}.can_extend("ab ", 2)); // no match at all, and no text can make one
  EXPECT(!real::regex {"\\d+"}.can_extend("12x", 0));
  EXPECT(!real::regex {"\"[^\"]*\""}.can_extend("\"ab\" x", 0));
  EXPECT(!real::regex {"\\w+(?=\\()"}.can_extend("abc(   ", 0));
  EXPECT(!real::regex {"(?<=a)b"}.can_extend("ab c", 1));
  EXPECT(!real::regex {"é+"}.can_extend("éé!", 0));
  // And true while it does not.
  EXPECT(real::regex {"[a-z]+"}.can_extend("ab", 0));
  EXPECT(real::regex {"ab|a"}.can_extend("a", 0));     // the preferred alternative is still live
  EXPECT(!real::regex {"a|ab"}.can_extend("a", 0));    // leftmost-first: `a` wins whatever follows
  EXPECT(real::regex {"\\w+\\b"}.can_extend("ab", 0)); // the boundary reads the next character
  EXPECT(real::regex {"é+"}.can_extend("é\xC3", 0));   // a code point cut in half
  EXPECT(real::regex {"x"}.can_extend("", 3));         // an anchor in text still to come
}
