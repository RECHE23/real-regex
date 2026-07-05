// libFuzzer target for the C ABI shim (bindings/c) — the raw-pointer surface every non-C++ binding sits on,
// with no direct fuzzing until now. No oracle: the contract is that no sequence of calls on arbitrary bytes
// crashes, triggers UB (ASan/UBSan) or leaks (LSan), and that no C++ exception ever crosses into C. Drives
// real_compile / real_group_name / real_find_iter[_at] / real_iter_next / real_free.
//
// Build & run: make fuzz-capi   (requires clang with -fsanitize=fuzzer)
//
// A unity build: including the shim's translation unit keeps this a single object, sidestepping a macOS ld
// quirk ("invalid r_symbolnum") that only bites a multi-object libFuzzer link. Linux links either form; this
// works on both, and it fuzzes the actual shim code (real_capi.cpp), not a copy.
#include "real_capi.cpp"

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size < 2) {
        return 0;
    }
    const std::uint32_t flags = data[0] & 0x7FU; // real::flags bits
    const std::size_t remaining = size - 2;
    const std::size_t pattern_len = data[1] % (remaining + 1U);
    const auto* body = reinterpret_cast<const char*>(data + 2);
    const char* pattern = body;
    const char* text = body + pattern_len;
    const std::size_t text_len = remaining - pattern_len;

    char err[256];
    int code = 0;
    real_regex* re = real_compile(pattern, pattern_len, flags, err, sizeof err, &code);
    if (re == nullptr) {
        return 0; // rejected — expected for many random inputs
    }
    const std::size_t ng = real_group_count(re);

    char name[64];
    for (std::size_t g = 0; g < ng; ++g) {
        (void) real_group_name(re, g, name, sizeof name);
    }

    std::vector<std::size_t> spans(2 * ng);
    for (int variant = 0; variant < 2; ++variant) {
        const std::size_t start = (text_len != 0U) ? (text_len / 2U) : 0U;
        real_iter* it = (variant == 0) ? real_find_iter(re, text, text_len)
                                       : real_find_iter_at(re, text, text_len, start);
        if (it != nullptr) {
            int guard = 0;
            while (real_iter_next(it, spans.data()) == 1 && ++guard < 100000) {
            }
            real_iter_free(it);
        }
    }
    real_free(re);
    return 0;
}
