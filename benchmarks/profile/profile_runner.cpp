// P0 profile harness binary: one (pattern, text-file, surface[, force]) → either timing or routes.
// Usage:
//   profile_runner time  <pattern> <text-file> <count|find> [force=none|class_off|ldfa_off]
//   profile_runner attr  <pattern> <text-file> <count|find> [force=...]   # requires -DREAL_PROFILE
// Prints one JSON object to stdout (no pretty-print).
#include <real/automata/lazy_dfa.hpp>
#include <real/core/profile.hpp>
#include <real/real.hpp>
#include <real/version.hpp>

#include "introspect.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using clk = std::chrono::steady_clock;

static std::string read_file(const char* path)
{
  std::ifstream     f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static void apply_force(std::string_view force)
{
  real::detail::class_fastpath_disabled()      = false;
  real::detail::lazy_dfa_route_disabled()      = false;
  real::detail::inner_literal_route_disabled() = false;
  if (force == "class_off") {
    real::detail::class_fastpath_disabled() = true;
  }
  else if (force == "ldfa_off") {
    real::detail::lazy_dfa_route_disabled() = true;
  }
  else if (force == "il_off") {
    real::detail::inner_literal_route_disabled() = true;
  }
  else if (force == "no_fastpath") {
    real::detail::class_fastpath_disabled()      = true;
    real::detail::lazy_dfa_route_disabled()      = true;
    real::detail::inner_literal_route_disabled() = true;
  }
}

static std::size_t run_surface(const real::regex& re,
                               std::string_view   text,
                               std::string_view   surface)
{
  if (surface == "find") {
    std::size_t n = 0;
    for (const auto& m : re.find_iter(text)) {
      (void)m;
      ++n;
    }
    return n;
  }
  return re.count_matches(text);
}

static void print_json_string(const char* s)
{
  std::fputc('"', stdout);
  for (const char* p = s; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '"' || c == '\\') {
      std::fputc('\\', stdout);
      std::fputc(static_cast<char>(c), stdout);
    }
    else if (c < 0x20) {
      std::fprintf(stdout, "\\u%04x", c);
    }
    else {
      std::fputc(static_cast<char>(c), stdout);
    }
  }
  std::fputc('"', stdout);
}

static int emit(std::string_view   mode,
                const real::regex& re,
                const std::string& pattern,
                const std::string& text,
                std::string_view   surface,
                std::string_view   force)
{
  const auto rec {real_profile::inspect(re)};

  std::printf("{");
  std::printf("\"pattern\":");
  print_json_string(pattern.c_str());
  std::printf(",\"surface\":");
  print_json_string(std::string(surface).c_str());
  std::printf(",\"force\":");
  print_json_string(std::string(force).c_str());
  std::printf(",\"corpus_bytes\":%zu", text.size());
  std::printf(",\"recognized\":{\"route_predicted\":");
  print_json_string(rec.route_predicted.c_str());
  std::printf(",\"hints\":[");
  for (std::size_t i = 0; i < rec.hints.size(); ++i) {
    if (i) {
      std::fputc(',', stdout);
    }
    print_json_string(rec.hints[i].c_str());
  }
  std::printf("]}");

#if defined(REAL_PROFILE)
  const bool instrumented = true;
#else
  const bool instrumented = false;
#endif
  std::printf(",\"instrumented\":%s", instrumented ? "true" : "false");
  std::printf(",\"engine_version\":");
  print_json_string(REAL_VERSION_STRING);

  if (mode == "time") {
    for (int w = 0; w < 3; ++w) {
      (void)run_surface(re, text, surface);
    }
    constexpr int       N = 7;
    std::vector<double> samples;
    samples.reserve(N);
    std::size_t matches = 0;
    for (int i = 0; i < N; ++i) {
      const auto   t0 = clk::now();
      matches         = run_surface(re, text, surface);
      const double dt =
        std::chrono::duration<double, std::nano>(clk::now() - t0).count();
      samples.push_back(dt / static_cast<double>(text.size()));
    }
    std::sort(samples.begin(), samples.end());
    const double p50 = samples[N / 2];
    const double p95 = samples[static_cast<std::size_t>(0.95 * (N - 1))];
    if (instrumented) {
      std::printf(",\"timing\":null,\"timing_disqualified\":true");
    }
    else {
      std::printf(",\"timing\":{\"ns_per_b_p50\":%.6f,\"p95\":%.6f,\"n\":%d}", p50, p95, N);
    }
    std::printf(",\"matches\":%zu", matches);
  }
  else if (mode == "attr") {
#if !defined(REAL_PROFILE)
    std::printf(",\"error\":\"attr_requires_REAL_PROFILE\"");
    std::printf("}\n");
    return 1;
#else
    real::detail::prof::reset();
    const std::size_t matches = run_surface(re, text, surface);
    const auto&       c       = real::detail::prof::snapshot();
    std::printf(",\"matches\":%zu", matches);
    std::printf(",\"routes\":{");
    bool first = true;
    for (std::size_t i = 0; i < static_cast<std::size_t>(real::detail::prof::route::count_); ++i) {
      if (c.routes[i] == 0) {
        continue;
      }
      if (!first) {
        std::fputc(',', stdout);
      }
      first = false;
      print_json_string(real::detail::prof::route_name(static_cast<real::detail::prof::route>(i)));
      std::printf(":%llu", static_cast<unsigned long long>(c.routes[i]));
    }
    std::printf("},\"events\":{");
    first = true;
    for (std::size_t i = 0; i < static_cast<std::size_t>(real::detail::prof::event::count_); ++i) {
      if (c.events[i] == 0) {
        continue;
      }
      if (!first) {
        std::fputc(',', stdout);
      }
      first = false;
      print_json_string(real::detail::prof::event_name(static_cast<real::detail::prof::event>(i)));
      std::printf(":%llu", static_cast<unsigned long long>(c.events[i]));
    }
    std::printf("},\"bytes_examined\":%llu",
                static_cast<unsigned long long>(c.bytes_examined));
#endif
  }
  else {
    std::fprintf(stderr, "unknown mode %.*s\n", static_cast<int>(mode.size()), mode.data());
    return 2;
  }
  std::printf("}\n");
  return 0;
}

int main(int argc, char** argv)
{
  if (argc < 5) {
    std::fprintf(stderr, "usage: %s time|attr <pattern> <text-file> count|find [force]\n", argv[0]);
    return 2;
  }
  const std::string_view mode    {argv[1]};
  const std::string      pattern {argv[2]};
  const std::string      text    {read_file(argv[3])};
  const std::string_view surface {argv[4]};
  const std::string_view force   {argc > 5 ? argv[5] : "none"};

  apply_force(force);

  try {
    const real::regex re {pattern};
    return emit(mode, re, pattern, text, surface, force);
  }
  catch (...) {
    std::printf("{\"error\":\"compile\"}\n");
    return 1;
  }
}
