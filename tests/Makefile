# tests/ -- the C++ suite (ctest), the sanitize build, the coverage toolchain, and the
# ThreadSanitizer smokes. Standardized per-section Makefile: invoked from the repo root
# as `make sanitize` / `coverage` / `coverage-check` / `coverage-html` / `tsan` /
# `tsan-core` (via the root's thin delegations, which preserve these names -- the CI
# invariant: ci.yml's coverage job runs coverage-check (l.274), its fuzz job runs
# tsan/tsan-core (l.134/138), docs.yml runs coverage-html (l.47), and full-local-gate
# runs sanitize/coverage-check as its last 2 of 22 steps) or directly here with
# `make -C tests <target>`.
#
# `test` -- ctest ONLY: `build` (the CMake configure+compile step, `-S .`-rooted) stays
# a ROOT-only target, deliberately (a CMake config step is
# repo-wide, not a tests/-only concern). The root's own `test: build` keeps `build` as
# its prerequisite, then delegates just the ctest invocation here -- so
# `make -C real-regex test` (the sciforge ecosystem CI's cross-repo invocation,
# ../sciforge/.github/workflows/ecosystem.yml:58) still builds AND runs the suite, same
# byte-for-byte.
#
# CWD idiom -- read before touching a recipe: every recipe line below that touches a
# repo-relative path (a bare `-S .` CMake configure, or `docs/coverage-style.css`, or
# tsan/tsan-core's own source files `tests/compat/tsan_compat.cpp` /
# `tests/engine/tsan_core.cpp`) runs under `cd $(ROOT) &&`, unchanged from the original
# recipe text, the same idiom docs/fuzz/benchmarks already established. Lines that only
# ever touch a $(BUILD)- or $(COV_DIR)-prefixed path need no `cd $(ROOT) &&` at all: both
# are ROOT-anchored via mk/common.mk (BUILD ?= $(ROOT)/build) regardless of the invoking
# CWD, and the test/tsan binaries themselves do no relative-path file I/O at runtime
# (confirmed by inspection -- no ifstream/fopen/std::filesystem in tests/), so unlike
# benchmarks/profile's callgrind_runs.sh, running them does not require CWD == $(ROOT).
ROOT := $(abspath $(CURDIR)/..)
include $(ROOT)/mk/common.mk
# HELP_PREFIX stays empty (the default in mk/help.mk): this section's target names are
# the same at the root as here (thin delegations, not prefixed aliases like c-build).
include $(ROOT)/mk/help.mk

# CMake/ctest binaries -- tests-only (the root Makefile keeps its own independent
# CMAKE ?= cmake for `build`/`install-smoke`; CTEST becomes unused at the root once
# `test`/`sanitize` migrate here, so it moves here entirely rather than staying a
# root-only dead variable).
CMAKE ?= cmake
CTEST ?= ctest

# Coverage uses LLVM source-based instrumentation, so it pins a Clang toolchain end to
# end. On macOS the Apple toolchain is required: Homebrew clang links a profile runtime
# whose .profraw the Homebrew llvm-profdata cannot read. On Linux the bare
# llvm-profdata/llvm-cov tools work. tests-only: nothing outside this file consumes
# COV_CXX/PROFDATA/LLVM_COV/COV_DIR (mk/common.mk stays minimal -- COV_FLOOR/
# COV_FLOOR_IGNORE promoted there instead, see that file's own comment for why).
ifeq ($(shell uname -s),Darwin)
COV_CXX  ?= /usr/bin/clang++
PROFDATA ?= xcrun llvm-profdata
LLVM_COV ?= xcrun llvm-cov
else
COV_CXX  ?= clang++
PROFDATA ?= llvm-profdata
LLVM_COV ?= llvm-cov
endif
COV_DIR  := $(BUILD)/coverage

.PHONY: test sanitize coverage-build coverage coverage-check coverage-html tsan tsan-core help
.DEFAULT_GOAL := help

test: ## Run the test suite (ctest; `build` — the CMake configure+compile — stays at the repo root)
	$(CTEST) --test-dir $(BUILD) --output-on-failure

# ASan/UBSan only here -- LeakSanitizer is CI-Linux-only. `ASAN_OPTIONS=detect_leaks=1` aborts
# immediately on macOS ("detect_leaks is not supported on this platform", confirmed empirically on
# this Darwin toolchain -- Apple's ASan runtime does not ship LSan), so it cannot be the default
# here without breaking every local sanitize run. A real leak therefore passes locally and is
# caught only in CI's Linux leg, invisible until then -- same shape as `doc-check`'s Docker-optional
# skip: visible in the CI job that IS the backstop, never a false green here.
sanitize: ## Build and run the tests under ASan + UBSan
	cd $(ROOT) && $(CMAKE) -S . -B $(BUILD)/sanitize $(CMAKE_CXX) -DREAL_SANITIZE=ON
	$(CMAKE) --build $(BUILD)/sanitize --parallel $(JOBS)
	$(CTEST) --test-dir $(BUILD)/sanitize --output-on-failure

# Shared build/run/merge steps used by both the text summary and the HTML report.
coverage-build:
	cd $(ROOT) && $(CMAKE) -S . -B $(COV_DIR) -DREAL_COVERAGE=ON -DCMAKE_CXX_COMPILER=$(COV_CXX)
	$(CMAKE) --build $(COV_DIR) --parallel $(JOBS)
	LLVM_PROFILE_FILE=$(COV_DIR)/tests.profraw $(COV_DIR)/real_tests_bin
	$(PROFDATA) merge -sparse $(COV_DIR)/tests.profraw -o $(COV_DIR)/tests.profdata

coverage: coverage-build ## Line-coverage text summary + HTML report
	$(LLVM_COV) report $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata
	$(LLVM_COV) show $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata \
	    -format=html -output-dir=$(COV_DIR)/html -show-line-counts-or-regions
	@cd $(ROOT) && grep -q "REAL dark-coverage theme" $(COV_DIR)/html/style.css 2>/dev/null || \
	    cat docs/coverage-style.css >> $(COV_DIR)/html/style.css
	@echo "HTML coverage report: $(COV_DIR)/html/index.html"

# Minimum line coverage enforced by `coverage-check` (the CI gate). `make coverage` itself
# stays advisory for local iteration; CI fails the build if a change drops below the floor.
# The floor measures the engine's reachable logic. The C ABI shim (bindings/c/real_capi.cpp) is excluded from
# the FLOOR — not from the report (`make coverage` still shows it): it is a thin boundary of defensive
# exception catch-alls ("no C++ exception crosses into C") that are unreachable by construction (the engine's
# only exception type, real::regex_error, is caught by a specific handler), so they cannot be line-covered by
# a test. That surface is guarded instead by the sanitize build and the c-fuzz target, which exercises it at
# runtime. include/real/engine/simd.hpp is excluded for the same shape of reason: it holds ONLY the
# intrinsics-only 16-byte membership masks (no eligibility decision, no loop, no candidate/skip logic) behind
# the SIMD fast paths — by construction ISA-exclusive (the NEON body never compiles on x86 and vice versa), so
# a single-ISA CI runner can never line-cover both legs no matter how thorough the tests are. The decision/loop
# logic that CALLS these primitives stays in pike.hpp — the same C++ on every ISA — and is exercised by the
# ordinary suite regardless of which leg compiled; only the intrinsics themselves are excluded. Guarded instead
# by sanitize, the fuzz corpus, the correctness nets (test_quantifiers), and the twin ISA's own coverage of the
# identical contract. include/real/engine/cpclass_gcc.hpp and cpclass_gcc_loop.hpp (O2r-1b) are the same
# shape again, one compiler instead of one ISA: a gcc-only fast path for run_cp_class_loop's >= 0x80 byte
# handling, spliced into pike.hpp under #if defined(__GNUC__) && !defined(__clang__) (see that file for the
# measured P0-callgrind numbers). clang — the only compiler this local/CI coverage build ever runs — never
# compiles either file, by construction, so a clang-only coverage run can never line-cover them no matter how
# thorough the tests are; the #else they sit beside (pike.hpp's original nested-closure shape) is exercised by
# the ordinary suite exactly as before the split. Guarded instead by the gcc leg of full-local-gate (compiles
# and functionally runs the branch), the x86 devbox A/B + callgrind (attribution: the lambda symbols are gone
# from the gcc build), and clang's own coverage of the identical #else contract. llvm-cov has no per-line
# exclusion, so both exclusions are per-file. The floor itself does NOT move for this exclusion — 95.0 stays
# the bar on what remains in scope. COV_FLOOR/COV_FLOOR_IGNORE live in mk/common.mk —
# the root's own full-local-gate step 22 echoes $(COV_FLOOR) directly, see that file's comment.
coverage-check: coverage-build ## Enforce the line-coverage floor (CI gate)
	@pct=$$($(LLVM_COV) report $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata \
	        -ignore-filename-regex='$(COV_FLOOR_IGNORE)' \
	        | awk '$$1 == "TOTAL" { gsub(/%/, "", $$10); print $$10 }'); \
	  echo "Line coverage: $$pct% (floor $(COV_FLOOR)%, engine logic; bindings/c guarded by sanitize+fuzz)"; \
	  awk -v p="$$pct" -v f="$(COV_FLOOR)" 'BEGIN { exit !(p + 0 >= f + 0) }' || \
	    { echo "FAIL: line coverage $$pct% is below the $(COV_FLOOR)% floor"; exit 1; }

# Silent variant used by the root's `doc`/`docs-site-gate` (via their own coverage-html
# delegation): keeps the terminal focused on the doc output.
coverage-html:
	@mkdir -p $(COV_DIR)
	@$(MAKE) --silent coverage-build > $(COV_DIR)/build.log 2>&1 || (cat $(COV_DIR)/build.log; exit 1)
	@$(LLVM_COV) show $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata \
	    -format=html -output-dir=$(COV_DIR)/html -show-line-counts-or-regions
	@cd $(ROOT) && grep -q "REAL dark-coverage theme" $(COV_DIR)/html/style.css 2>/dev/null || \
	    cat docs/coverage-style.css >> $(COV_DIR)/html/style.css
	@echo "HTML coverage report: $(COV_DIR)/html/index.html"

# ThreadSanitizer smoke: a standalone multi-threaded program (tests/compat/tsan_compat.cpp) that hammers the
# concurrent lazy std_engine() build on shared regex objects, proving the "concurrent const ops are
# race-free" claim reproducibly. Standalone (no framework / test_static.cpp non-atomic op-new counter,
# which would itself race). Clang feature; always uses Clang.
tsan: ## ThreadSanitizer smoke of concurrent std_engine (Clang)
	mkdir -p $(BUILD)
	cd $(ROOT) && clang++ $(CXXSTD) -O1 -g $(INCLUDES) -fsanitize=thread tests/compat/tsan_compat.cpp -o $(BUILD)/tsan_compat
	$(BUILD)/tsan_compat

# ThreadSanitizer smoke for the CORE concurrent caches (shared-confirm / immutables / call_once),
# not the compat layer. Barrier-synchronized first search on a FRESH const regex each iteration —
# without the barrier+fresh pattern, call_once fills once and late threads never race the warm path
# (false-negative risk). Harness proof (must go red):
#   TSAN_OPTIONS=halt_on_error=1 REAL_TSAN_INJECT_RACE=1 make tsan-core
# ASLR: some Linux kernels hit TSan "FATAL: unexpected memory mapping" under high ASLR — prefer
# setarch -R when present (Linux CI/devbox); fall back to a direct run (macOS has no setarch).
tsan-core: ## ThreadSanitizer smoke of core shared-confirm / immut caches (Clang)
	mkdir -p $(BUILD)
	cd $(ROOT) && clang++ $(CXXSTD) -O1 -g $(INCLUDES) -fsanitize=thread \
	    tests/engine/tsan_core.cpp -o $(BUILD)/tsan_core
	setarch $$(uname -m) -R $(BUILD)/tsan_core 2>/dev/null || $(BUILD)/tsan_core

help: ## Show these targets
	@$(HELP_RECIPE)
