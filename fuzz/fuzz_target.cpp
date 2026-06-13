// libFuzzer robustness target for REAL — the kind of continuous fuzzing
// RE2 and rust-regex run on OSS-Fuzz. No oracle: the contract verified here
// is that the engine NEVER crashes, NEVER triggers UB (ASan/UBSan), and
// ALWAYS terminates (the linear-time guarantee; libFuzzer's timeout catches
// any accidental hang). Invalid patterns and over-limit programs are
// expected to raise real::regex_error — anything else escaping is a bug.
//
// Build & run: make fuzz   (requires clang with -fsanitize=fuzzer)
//
// The fuzzer drives the whole runtime pipeline — parser, compiler,
// prefilter, Pike VM, and every public operation — on adversarial input.
// Empty-match iteration, UTF-8 boundaries and anchors get hammered hardest,
// which is exactly where regex bugs hide.

#include <real/real.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

// Drives every public operation on a compiled pattern. Results are consumed
// (volatile sink) so nothing is optimized away.
void exercise(const real::regex& rx, std::string_view text) {
    volatile bool sink = false;
    sink = sink || rx.match(text).matched();
    sink = sink || rx.fullmatch(text).matched();
    sink = sink || rx.search(text).matched();

    std::size_t count = 0;
    for (const auto& m : rx.find_iter(text)) {
        // Touch the result so capture/offset paths are exercised.
        sink = sink || (m.start() <= m.end());
        if (++count > (1u << 20)) {
            break;  // a linear engine cannot exceed ~text.size()+1 matches
        }
    }
    (void)rx.replace(text, "$0-");  // $0 is always a valid reference
    (void)rx.split(text);
    (void)sink;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 2) {
        return 0;
    }

    // Byte 0: flag bits. Byte 1: split point between pattern and text.
    real::flags f = real::flags::none;
    if ((data[0] & 1U) != 0) {
        f = f | real::flags::icase;
    }
    if ((data[0] & 2U) != 0) {
        f = f | real::flags::multiline;
    }
    if ((data[0] & 4U) != 0) {
        f = f | real::flags::dotall;
    }
    if ((data[0] & 8U) != 0) {
        f = f | real::flags::bytes;
    }

    const std::size_t remaining = size - 2;
    const std::size_t pattern_len = data[1] % (remaining + 1U);
    const auto* body = reinterpret_cast<const char*>(data + 2);
    const std::string_view pattern(body, pattern_len);
    const std::string_view text(body + pattern_len, remaining - pattern_len);

    try {
        const real::regex rx(pattern, f);
        exercise(rx, text);
    } catch (const real::regex_error&) {
        // Expected: invalid syntax, or a program exceeding max_program_size.
    }
    return 0;
}
