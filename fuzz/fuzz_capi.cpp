// libFuzzer target for the C ABI shim (bindings/c) — the raw-pointer surface every non-C++ binding sits on.
// No oracle: the contract is that no sequence of calls on arbitrary bytes crashes, triggers UB (ASan/UBSan)
// or leaks (LSan), and that no C++ exception ever crosses into C. Drives real_compile / real_group_name /
// real_find_iter[_at/_between] / real_iter_next / real_count_matches / real_match (all 3 modes) / real_sub
// / every real_set_* / real_free — the full R4 surface, the same boundary that caught the wagon-4c OOB.
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
    if (size < 3) {
        return 0;
    }
    const std::uint32_t flags = data[0] & 0x7FU; // real::flags bits
    const std::size_t remaining = size - 3;
    const std::size_t pattern_len = data[1] % (remaining + 1U);
    const std::size_t rest_after_pattern = remaining - pattern_len;
    const std::size_t repl_len = data[2] % (rest_after_pattern + 1U);
    const std::size_t text_len = rest_after_pattern - repl_len;
    const auto* body = reinterpret_cast<const char*>(data + 3);
    const char* pattern = body;
    const char* text = body + pattern_len;
    const char* repl = body + pattern_len + text_len;

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

    const std::size_t mid = (text_len != 0U) ? (text_len / 2U) : 0U;
    const std::size_t lo = text_len / 3U;
    const std::size_t hi = (2U * text_len) / 3U;

    std::vector<std::size_t> spans(2 * ng);
    for (int variant = 0; variant < 3; ++variant) {
        real_iter* it = nullptr;
        if (variant == 0) {
            it = real_find_iter(re, text, text_len);
        } else if (variant == 1) {
            it = real_find_iter_at(re, text, text_len, mid);
        } else {
            it = real_find_iter_between(re, text, text_len, lo, hi);
        }
        if (it != nullptr) {
            int guard = 0;
            while (real_iter_next(it, spans.data()) == 1 && ++guard < 100000) {
            }
            real_iter_free(it);
        }
    }

    (void) real_count_matches(re, text, text_len);

    for (int mode = REAL_MODE_SEARCH; mode <= REAL_MODE_FULLMATCH; ++mode) {
        (void) real_match(re, text, text_len, 0, text_len, mode, spans.data());
        (void) real_match(re, text, text_len, mid, text_len, mode, spans.data());
    }

    char sub_err[256];
    std::size_t n_subs = 0;
    const std::size_t need =
      real_sub(re, text, text_len, repl, repl_len, 0, nullptr, 0, &n_subs, sub_err, sizeof sub_err);
    if (need != static_cast<std::size_t>(-1)) {
        std::vector<char> buf(need);
        (void) real_sub(re, text, text_len, repl, repl_len, 0, buf.data(), buf.size(), &n_subs, sub_err,
                        sizeof sub_err);
    }

    // (NULL, 0) empty-subject convention: dedicated, unconditional coverage (not left to chance —
    // the fuzzed text_len/repl_len are rarely exactly 0) on every function that accepts a
    // (text, len) or (repl, repl_len) pair. Must never crash and must never return the -1/NULL
    // error sentinel purely because the pointer was null at length 0.
    (void) real_match(re, nullptr, 0, 0, 0, REAL_MODE_SEARCH, spans.data());
    (void) real_count_matches(re, nullptr, 0);
    char nb_err[256];
    std::size_t nb_subs = 0;
    (void) real_sub(re, nullptr, 0, nullptr, 0, 0, nullptr, 0, &nb_subs, nb_err, sizeof nb_err);

    real_free(re);

    // real_set_* — a 2-member set from the same fuzzed pattern bytes exercises the whole set surface
    // (construction, size, is_match, matches, free) without needing a second independent pattern.
    const char* set_patterns[2] = {pattern, pattern};
    const std::size_t set_lens[2] = {pattern_len, pattern_len};
    char set_err[256];
    int set_code = 0;
    real_regex_set* set =
      real_set_compile(set_patterns, set_lens, 2, flags, set_err, sizeof set_err, &set_code);
    if (set != nullptr) {
        const std::size_t set_size = real_set_size(set);
        (void) real_set_is_match(set, text, text_len);
        std::vector<std::uint8_t> hits(set_size);
        (void) real_set_matches(set, text, text_len, hits.data());
        (void) real_set_is_match(set, nullptr, 0);
        std::vector<std::uint8_t> nb_hits(set_size);
        (void) real_set_matches(set, nullptr, 0, nb_hits.data());
        real_set_free(set);
    }
    return 0;
}
