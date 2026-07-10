// Dev-only: map compiled hints → predicted route / recognized tags for the profile harness.
// Not a public API — included only by benchmarks/profile/*.
#pragma once

#include <real/real.hpp>

#include <string>
#include <vector>

namespace real_profile {

  struct recognized
  {
    std::vector<std::string> hints;
    std::string              route_predicted;
  };

  inline recognized inspect(const real::regex& re)
  {
    recognized out;
    const auto prog {re.raw_program()};
    const auto& h {prog.hints};
    if (h.greedy_class_loop >= 0) {
      out.hints.push_back("greedy_class_loop");
      out.route_predicted = "class_loop";
    }
    if (h.greedy_cp_class >= 0) {
      out.hints.push_back("greedy_cp");
      if (h.greedy_cp_class_plus) {
        out.hints.push_back("greedy_cp_plus");
      }
      out.route_predicted = "cp_class_loop";
    }
    if (h.exact_literal_len > 0) {
      out.hints.push_back("exact_literal");
      out.route_predicted = "exact_literal";
    }
    if (h.fixed_shape) {
      out.hints.push_back("fixed_shape");
      if (out.route_predicted.empty()) {
        out.route_predicted = "fixed_shape";
      }
    }
    if (h.fixed_alternation) {
      out.hints.push_back("fixed_alternation");
      out.route_predicted = "alternation";
    }
    if (h.codepoint_class_ascii >= 0) {
      out.hints.push_back("codepoint_class");
      if (out.route_predicted.empty()) {
        out.route_predicted = "codepoint_class";
      }
    }
    if (h.inner_literal_len > 0) {
      out.hints.push_back("inner_literal");
    }
    if (h.first_bytes_valid) {
      out.hints.push_back("first_bytes_valid");
    }
    if (h.wb_lead == 0 && h.wb_trail == 0 && h.greedy_cp_class >= 0) {
      // May be bare \w+ or B-1-dropped \b\w+\b — caller tags intended.wb.
    }
    if (h.wb_lead != 0 || h.wb_trail != 0) {
      out.hints.push_back("wb_wrap");
      if (h.wb_lead == 1 || h.wb_trail == 1) {
        out.hints.push_back("wb_b2_wrap");
      }
    }
    if (h.trailing_lookaround >= 0) {
      out.hints.push_back("trailing_la");
      out.route_predicted = "trailing_la";
    }
    if (out.route_predicted.empty()) {
      out.route_predicted = "lazy_dfa_or_general";
    }
    return out;
  }

} // namespace real_profile
