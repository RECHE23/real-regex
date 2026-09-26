// A consumer of the compiled library, built with bindings/c alone on its include path: include/ (the engine)
// is deliberately absent, so this file compiles only while real_compiled.hpp reaches no engine header.
#include <real_compiled.hpp>

#include <cstdio>
#include <string_view>

int main()
{
  try {
    const real::compiled::regex re {R"((?P<user>\w+)@(\w+))"};
    const auto                  m {re.search("mail bob@host now")};
    const bool                  ok {m && m.str(1) == "bob" && m.str(2) == "host" && re.group_index("user") == 1U
                   && re.count("a@b c@d") == 2U && real::compiled::abi_matches()};
    std::printf("compiled consumer: %s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
  }
  catch (const real::compiled::error& e) {
    std::printf("compiled consumer: FAIL (%s)\n", e.what());
    return 1;
  }
}
