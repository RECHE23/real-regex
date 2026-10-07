// Every public top-level header, in one translation unit: the vector-ISA gates (check-no-simd,
// check-sse2-floor) and CI's 32-bit syntax leg compile it. `make check-public-tu` refuses an
// include/real/*.hpp this list does not name.
#include "real/dfa.hpp"
#include "real/real.hpp"
#include "real/regex.hpp"
#include "real/regex_set.hpp"
#include "real/storage.hpp"
#include "real/version.hpp"

int main() {}
