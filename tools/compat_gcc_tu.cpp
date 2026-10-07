// The compat layers under gcc -Werror (gcc-check): their templates only meet the compiler where something
// instantiates them, so each surface a caller reaches is called once here.
#include "real/compat/re2/re2.hpp"
#include "real/compat/std/regex.hpp"

#include <iterator>
#include <string>

int main()
{
  namespace c = real::compat;
  using real::compat::re2::RE2;
  const c::regex     re {"(\\w+)@(\\w+)"};
  const std::string  s {"ann@corp bob@host"};
  std::string        out {c::regex_replace(s, re, "[$2:$1|$&|$`|$']")};
  out += c::regex_replace(s, re, "<\\2&\\1>", c::regex_constants::format_sed);
  c::regex_replace(std::back_inserter(out), s.begin(), s.end(), re, std::string {"$1"});
  c::smatch m;
  if (c::regex_search(s, m, re)) {
    out += m.format("$2-$1");
    out += m.format(std::string {"\\1"}, c::regex_constants::format_sed);
  }
  for (c::sregex_iterator it {s.begin(), s.end(), re}, end; it != end; ++it) {
    out += it->str(1);
  }
  for (c::sregex_token_iterator it {s.begin(), s.end(), re, {1, 2}}, end; it != end; ++it) {
    out += it->str();
  }
  std::string user;
  out += RE2::FullMatch("ann@corp", R"((\w+)@\w+)", &user) ? user : "";
  std::string text {s};
  RE2::GlobalReplace(&text, R"(@)", "#");
  return static_cast<int>(out.size() + text.size()) & 1;
}
