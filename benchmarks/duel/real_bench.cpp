#include <real/real.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
using clk = std::chrono::steady_clock;
int main(int argc, char** argv) {
    std::ifstream f(argv[2], std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    const std::string text = ss.str();
    try {
        const real::regex re(argv[1]);
        std::size_t cnt = 0;
        for (int w = 0; w < 3; ++w) { cnt = 0; for (const auto& m : re.find_iter(text)) { (void)m; ++cnt; } }
        double best = 1e30;
        for (int r = 0; r < 15; ++r) {
            auto t0 = clk::now();
            std::size_t c = 0; for (const auto& m : re.find_iter(text)) { (void)m; ++c; }
            double dt = std::chrono::duration<double, std::nano>(clk::now() - t0).count();
            asm volatile("" :: "r"(c) : "memory");
            if (dt < best) best = dt;
        }
        std::printf("%.4f %zu\n", best / static_cast<double>(text.size()), cnt);
    } catch (...) { std::printf("unsupported 0\n"); }
    return 0;
}
