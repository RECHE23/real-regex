// The /reference/ dfa page's Example region (the [reference] markers below):
// compiled and run by example-check, so the code the page shows is tested, never
// merely illustrative.
#include <real/dfa.hpp>

#include <array>
#include <iostream>
#include <string_view>

int main()
{
  // [reference]
  // A tiny lexer: three rules, longest match wins, a tie goes to the lowest index.
  // DFA rules must be byte-representable -- ASCII classes here (a Unicode \d
  // raises real::dfa_error at construction).
  const std::array<real::regex, 3> rules {
      real::regex {R"([0-9]+)"}, real::regex {R"([A-Za-z0-9]+)"}, real::regex {R"( +)"}};
  const real::dfa lex {rules};

  std::string_view rest {"if x1 42"};
  while (const auto tok = lex.match(rest)) {
    std::cout << tok->rule_index << ":" << rest.substr(0, tok->length) << "\n";
    rest.remove_prefix(tok->length);
  }
  // 1:if · 2:" " · 1:x1 · 2:" " · 0:42 -- "42" matches [0-9]+ and [A-Za-z0-9]+
  // at equal length; the tie goes to [0-9]+, the lower index.
  // [/reference]

  return rest.empty() ? 0 : 1;
}
