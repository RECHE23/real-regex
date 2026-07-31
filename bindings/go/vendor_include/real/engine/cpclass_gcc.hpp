// O2r-1b: gcc-only outline for real::detail::pike_vm::run_cp_class_loop's >= 0x80 byte handling.
//
// P0-callgrind (x86/gcc) charged 28% of cp_class_loop's Ir to two nested closures gcc never inlined
// (member_hi 23.1%, the width() call into it 13.3%) — gcc/manylinux builds the Linux PyPI wheels, so
// this cost ships to users. clang was already inlining the original nested-closure shape cleanly (M1
// baseline "healthy"); forcing the same outline unconditionally (O2r-1, no compiler guard) cost M1 +7%
// on \w+/\b\w+\b and was retired pre-commit (see .recovery/o2r1-rejected-m1-regression.patch).
//
// This file and cpclass_gcc_loop.hpp hold the gcc-only outline: member_hi's decode + page-bitmap /
// range membership (the 23.1% Ir line) becomes the outlined, cold cp_class_hi_width below; the ASCII
// fast path stays a plain asc[b] table load with no lambda/closure call. Elimination is partial by
// design: width()/extend_run() (cpclass_gcc_loop.hpp) remain lambdas, and gcc still doesn't fully
// inline them — a further ~23% Ir measured post-split, named as headroom for a future pass.
//
// O2r-1c took part of that headroom, and NOT by inlining: forcing extend_run inline was tried and
// refuted (x86 Ir -10% on \w+, wall clock +34..37% on \d+ and +8% on \w+ under an interleaved A/B,
// with the untouched [a-z]+ also +5% — byte-identical in Ir, so the cost was code layout, not the
// loop). What paid instead was making the call CHEAPER while leaving it out of line: extend_run
// captured `[&]`, so each call walked a closure of references that itself held `width`, another
// by-reference closure — two indirections per call. Explicit by-value capture of the scalars, with
// width's three lines inlined into it, removes both. x86 g++-14: \d+ -13% wall / -7.4% Ir over three
// interleaved rounds, \w+ within noise, [a-z]+ byte-identical in Ir. \d+ gains most because its
// class has ten ASCII members against \w's sixty-three, so runs are shorter and the per-call cost is
// paid more often per byte. M1 is unaffected by construction (clang never compiles this file:
// `c++ -E` finds zero occurrences of cp_class_hi_width) and by measurement (arm64 A/B identical).
//
// The `noinline, cold` pair below is load-bearing and must not be dropped, however wrong `cold` looks
// on a Unicode-dominant corpus where this is the only path taken. Removing both attributes lets gcc
// inline the body into all nine call sites (the symbol disappears entirely) and does buy the property/
// script band 12-16%, reproduced across g++ 13.3, 13.4 and 14 on two machines. It also costs `(?i)cafe`
// on an ASCII no-match scan 10.4 -> 33.0 us, +217%, and (\w+)@(\w+) +12.9% -- devbox g++ 13.3.0, eight
// interleaved rounds, no overlap between the two sample sets. g++ 13.3 builds the manylinux wheels, so
// that regression is the one that ships.
//
// The gain lives in `width`, the scan predicate: a variant keeping `width` outlined and inlining only
// inside extend_run's three call sites delivers NONE of it (\p{N}+ +7.4%, sc=Han +6.9%, scx=Cyrl
// +4.7% -- the wrong sign) while recovering most of the ASCII cost (+9.8% on (?i)cafe). The regression
// does NOT live in this path at all: `(?i)cafe` compiles to four BYTE classes and zero cp classes
// (raw_program().cp_classes.size() == 0), so it never enters this loop and never calls the function
// below. What it pays is collateral codegen -- a large inlinable body in a header included everywhere
// degrades layout for patterns that never touch it. The tell is the sign flip: that row read -25%
// under g++ 14 in a container and +217% on the devbox under 13.3, while the Unicode band held
// -12..-18% in every environment measured. A mechanism keeps its sign; a layout effect does not.
//
// So the trade is a real 12-16% mechanism against unbounded layout collateral on the toolchain that
// ships, and it is the collateral that refuses it. Buying that band means shrinking what gets inlined
// (cp_class::range_count is known when the program is built -- 2 ranges for (?i)cafe's accented twin
// against 675 for \p{L}, and a two-compare membership test would inline for free where a binary search
// cannot), not toggling an inlining hint on the general body.
//
// Measured x86 devbox A/B (land threshold >=10%, paid): \w+ -24.7%, \d+ -66%; witnesses ([a-z]+,
// trailing-LA) within noise. M1 is unaffected by construction: this file, and the branch selecting it
// (#if defined(__GNUC__) && !defined(__clang__); clang defines __GNUC__ too, hence the explicit
// exclusion), never compile under clang — confirmed by `c++ -E` (zero occurrences of
// cp_class_hi_width) and by an interleaved M1 A/B (+-1%, noise).
//
// Split out of pike.hpp (rather than kept inline under #if) so this compiler-exclusive code can be
// excluded from the coverage floor via COV_FLOOR_IGNORE (the simd.hpp precedent: code a single-
// toolchain coverage run can never line-cover by construction) instead of inflating pike.hpp's own
// line count with a branch clang never compiles — llvm-cov report was tallying those preprocessor-
// eliminated physical lines into the file's total: invisible to the compiler, not to the coverage
// line-accounting.
//
// Internal — do not include directly. A body fragment, not a standalone translation unit: valid only
// spliced into real::detail::pike_vm at class scope by pike.hpp, under the same
// #if defined(__GNUC__) && !defined(__clang__) that guards this #include.

// \brief Non-ASCII width for run_cp_class_loop (byte >= 0x80 only): decode + page-bitmap / range
//        membership. Outlined and cold — see the file-level rationale above.
// \param[in] text     The subject text.
// \param[in] i        Index of the lead byte (>= 0x80); must be < text.size().
// \param[in] cp_index Index into dynamic_program::cp_classes for the pattern's class.
// \return The code point's byte width if it is a class member, or 0.
__attribute__((noinline, cold))
constexpr std::size_t cp_class_hi_width(std::string_view text,
                                        std::size_t      i,
                                        std::size_t      cp_index)
{
  const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, i)};
  if (!dc.valid) {
    return 0;
  }
  // European page + sparse hi table (same split as run_cp_class_loop's member_hi).
  const bool m {dc.cp <= cp_page_max ? cp_member_page(cp_index, dc.cp)
                                     : cp_member_high(cp_index, dc.cp)};
  return m ? dc.length : 0;
}
