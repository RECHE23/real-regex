# Thin orchestrator over CMake (build/test/sanitize/coverage) and the QA
# tools (clang-tidy, doxygen, libFuzzer, Python). CMake owns all compilation
# policy (CMakeLists.txt); this file only wires the frequent commands.
#
# Override the compiler with CXX on the command line, e.g.
#   make test CXX=g++-14
# A CXX environment variable is not forwarded; switching compilers reuses a
# cached build dir, so run `make clean` first.

CMAKE  ?= cmake
CTEST  ?= ctest
PYTHON ?= python3
BUILD  := build
# Parallelism: detected core count (override with JOBS=N).
JOBS   ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# SciForge owns the shared, dev-only benchmark substrate (sciforge.bench: dep-free stats,
# schema, collector), in its python/ dir. Same sibling default as SCIFORGE_INCLUDE/_LINT;
# CI checks SciForge out alongside as ../sciforge. Never a build/runtime dependency — the
# benches import it only when run (make bench-*), and it is never shipped in the wheel.
SCIFORGE_PYTHON ?= ../sciforge/python

# Run Python against the in-place build under bindings/python/, ahead of any installed
# copy (PYTHONPATH precedes site-packages, so an editable install elsewhere
# cannot shadow the freshly built extension). The SciForge sibling is appended so the
# benches can `import sciforge.bench` (the shared stats/schema substrate).
PYRUN := PYTHONPATH=$(CURDIR)/bindings/python:$(abspath $(SCIFORGE_PYTHON)) $(PYTHON)

# Forward CMAKE_CXX_COMPILER only when CXX is set on the command line;
# otherwise CMake selects the platform default.
ifeq ($(origin CXX),command line)
CMAKE_CXX := -DCMAKE_CXX_COMPILER=$(CXX)
endif

CXXSTD       := -std=c++20
# What gate-bump/gate-doc/gate-test diff against to detect their change category (see the block
# comment above those targets). Override to compare against a single commit instead of the whole
# unpushed stack on a train of several already-committed wagons -- e.g.
# `GATE_BASE=HEAD~1 make gate-bump` diffs just the latest commit, not everything since origin/main.
# Default unchanged (fail-closed stays the behavior for anyone who doesn't override it).
GATE_BASE ?= origin/main
INCLUDES     := -Iinclude
# The test harness (framework.hpp) is owned by SciForge; the test TUs include it
# as <sciforge/test/framework.hpp>. clang-tidy (make lint) needs that path too.
# Sibling checkout by default — matches the CMake SCIFORGE_INCLUDE_DIR default.
SCIFORGE_INCLUDE ?= ../sciforge/include
# SciForge also owns the shared lint config (the MISRA base + uncrustify.cfg), in
# its lint/ dir. Same sibling default; CI checks SciForge out alongside as ../sciforge.
SCIFORGE_LINT ?= ../sciforge/lint
SCIFORGE_TOOLS ?= ../sciforge/tools
# unicode_fold.hpp and unicode_props.hpp are generated (their scripts own the layout; the regen tests
# pin them), so they are excluded from the hand-written-code formatter.
FORMAT_FILES := $(shell find include tests -name '*.hpp' -o -name '*.cpp' | grep -vE 'include/real/unicode/unicode_(fold|props|property|script|binprop|scx)\.hpp')

.PHONY: all build test sanitize coverage coverage-build coverage-html coverage-check \
        lint misra fuzz fuzz-compat fuzz-re2 exhaustive-compat fowler-compat check-pins tsan tsan-core doc doc-no-coverage doc-check format format-check full-local-gate gate-bump gate-doc gate-test clean \
        bench-engines bench-multipattern bench-duel bench-matrix matrix-gate \
        profile-sample-build profile-sample profile-callgrind \
        version-check install install-smoke uninstall release help check-layers

.DEFAULT_GOAL := help

help:
	@echo "REAL — build orchestrator (CMake + QA tools)"
	@echo ""
	@echo "  make build      Configure and build the test binary (CMake)"
	@echo "  make test       Build and run the test suite (ctest)"
	@echo "  make sanitize   Build and run the tests under ASan + UBSan"
	@echo "  make coverage   Line-coverage text summary + HTML report"
	@echo "  make lint       clang-tidy over the test sources"
	@echo "  make misra      MISRA C++:2023-oriented analysis"
	@echo "  make fuzz       libFuzzer robustness fuzzing (Clang; FUZZ_TIME=secs)"
	@echo "  make fuzz-compat  Differential fuzz: real::compat vs std::regex (Clang; FUZZ_TIME=secs)"
	@echo "  make fuzz-re2   Differential: real::compat::re2 vs true libre2 (needs pkg-config re2)"
	@echo "  make tsan       ThreadSanitizer smoke of concurrent std_engine (Clang)"
	@echo "  make tsan-core  ThreadSanitizer smoke of core shared-confirm / immut caches (Clang)"
	@echo "  make doc        Generate API reference (Doxygen) with embedded coverage"
	@echo "  make doc-no-coverage  Generate API reference without coverage report"
	@echo "  make format     Uncrustify, in place"
	@echo "  make format-check  Uncrustify, dry-run, exits non-zero on diff"
	@echo "  make clean      Remove build artifacts"
	@echo ""
	@echo "  make version-check  Assert pyproject = __init__ = CMake-derived version"
	@echo "  make full-local-gate  Every pass/fail gate in one command (the macOS gate of record)"
	@echo "  make gate-bump  Calibrated gate for a version bump only (version-check + build)"
	@echo "  make gate-doc   Calibrated gate for a doc-only change (doc-check/format-check as needed)"
	@echo "  make gate-test  Calibrated gate for a tests/-only change (test + sanitize + coverage-check)"
	@echo "    all 3 diff against GATE_BASE (default origin/main); on a stacked train where later"
	@echo "    commits accumulate earlier wagons in that diff, GATE_BASE=HEAD~1 make gate-bump"
	@echo "    scopes the category check to just the latest commit"
	@echo "  make bench-duel   REAL vs the regex crate, ns/byte (needs a Rust toolchain)"
	@echo "  make profile-sample  P0 2-pass profile grid (JSONL + markdown; not a CI gate)"
	@echo ""
	@echo "  Per-binding targets: make {python,c,rust}-<target> (build/test/bench/fuzz/...)"
	@echo "    see: make python-help | make c-help | make rust-help"
	@echo "  make bench-engines  C++ throughput vs std::regex/PCRE2/RE2 (if present)"
	@echo "  make bench-multipattern  multi-pattern which-matched / extraction (RE2/HS optional)"
	@echo "  make install      Install the Python package (pip)"
	@echo "  make uninstall    Uninstall the Python package (pip)"
	@echo "  make release      Cut a calendar-versioned release (tag + push)"
	@echo ""
	@echo "  Override the compiler: make test CXX=g++-14"

all: build

# --- build / test (delegated to CMake) ------------------------------------

build:
	$(CMAKE) -S . -B $(BUILD) $(CMAKE_CXX) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD) --parallel $(JOBS)

test: build
	$(CTEST) --test-dir $(BUILD) --output-on-failure

# ASan/UBSan only here -- LeakSanitizer is CI-Linux-only. `ASAN_OPTIONS=detect_leaks=1` aborts
# immediately on macOS ("detect_leaks is not supported on this platform", confirmed empirically on
# this Darwin toolchain -- Apple's ASan runtime does not ship LSan), so it cannot be the default
# here without breaking every local sanitize run. A real leak (wagon 4c's own `bare_heap_ending_in`
# test helper first shipped with a `new[]`/`.release()` that never freed) therefore passes locally
# and is caught only in CI's Linux leg, invisible until then -- same shape as `doc-check`'s
# Docker-optional skip: visible in the CI job that IS the backstop, never a false green here.
sanitize:
	$(CMAKE) -S . -B $(BUILD)/sanitize $(CMAKE_CXX) -DREAL_SANITIZE=ON
	$(CMAKE) --build $(BUILD)/sanitize --parallel $(JOBS)
	$(CTEST) --test-dir $(BUILD)/sanitize --output-on-failure

# Coverage uses LLVM source-based instrumentation, so it pins a Clang
# toolchain end to end. On macOS the Apple toolchain is required: Homebrew
# clang links a profile runtime whose .profraw the Homebrew llvm-profdata
# cannot read. On Linux the bare llvm-profdata/llvm-cov tools work.
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

# Shared build/run/merge steps used by both the text summary and the HTML report.
coverage-build:
	$(CMAKE) -S . -B $(COV_DIR) -DREAL_COVERAGE=ON -DCMAKE_CXX_COMPILER=$(COV_CXX)
	$(CMAKE) --build $(COV_DIR) --parallel $(JOBS)
	LLVM_PROFILE_FILE=$(COV_DIR)/tests.profraw $(COV_DIR)/real_tests_bin
	$(PROFDATA) merge -sparse $(COV_DIR)/tests.profraw -o $(COV_DIR)/tests.profdata

coverage: coverage-build
	$(LLVM_COV) report $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata
	$(LLVM_COV) show $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata \
	    -format=html -output-dir=$(COV_DIR)/html -show-line-counts-or-regions
	@grep -q "REAL dark-coverage theme" $(COV_DIR)/html/style.css 2>/dev/null || \
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
# the bar on what remains in scope.
COV_FLOOR := 95.0
COV_FLOOR_IGNORE := bindings/c|include/real/engine/simd.hpp|include/real/engine/cpclass_gcc

coverage-check: coverage-build
	@pct=$$($(LLVM_COV) report $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata \
	        -ignore-filename-regex='$(COV_FLOOR_IGNORE)' \
	        | awk '$$1 == "TOTAL" { gsub(/%/, "", $$10); print $$10 }'); \
	  echo "Line coverage: $$pct% (floor $(COV_FLOOR)%, engine logic; bindings/c guarded by sanitize+fuzz)"; \
	  awk -v p="$$pct" -v f="$(COV_FLOOR)" 'BEGIN { exit !(p + 0 >= f + 0) }' || \
	    { echo "FAIL: line coverage $$pct% is below the $(COV_FLOOR)% floor"; exit 1; }

# Silent variant used by make doc: keeps the terminal focused on the doc output.
coverage-html:
	@mkdir -p $(COV_DIR)
	@$(MAKE) --silent coverage-build > $(COV_DIR)/build.log 2>&1 || (cat $(COV_DIR)/build.log; exit 1)
	@$(LLVM_COV) show $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata \
	    -format=html -output-dir=$(COV_DIR)/html -show-line-counts-or-regions
	@grep -q "REAL dark-coverage theme" $(COV_DIR)/html/style.css 2>/dev/null || \
	    cat docs/coverage-style.css >> $(COV_DIR)/html/style.css
	@echo "HTML coverage report: $(COV_DIR)/html/index.html"

# --- QA tools (wrappers; no compilation policy here) ----------------------

lint:
	@find tests -name '*.cpp' | xargs -P $(JOBS) -I{} clang-tidy {} -- $(CXXSTD) $(INCLUDES) -Ibindings/c -I$(SCIFORGE_INCLUDE)

# Analyzes the library's own headers through a synthetic translation unit that
# includes the umbrella header and exercises the engine (so templates get
# instantiated and checked). --header-filter scopes diagnostics to include/real/;
# the TU's body is wrapped in try/catch so main cannot let an exception escape
# (otherwise bugprone-exception-escape fires on the scaffolding, not the library).
# NB: no --line-filter — it suppresses every diagnostic outside the TU file, which
# silently hides ALL header findings (the gate was vacant before this).
# The MISRA profile is the shared base owned by SciForge (lint/clang-tidy-misra);
# REAL's one extra deviation — the SBO union in storage.hpp — is appended on the
# command line with --checks (which appends to the config's Checks), not by forking
# the shared file. See docs/MISRA.md.
misra:
	mkdir -p $(BUILD)
	printf '#include <real/real.hpp>\nint main(){ try { const real::regex r("a"); return r.search("a") ? 0 : 1; } catch (...) { return 2; } }\n' > $(BUILD)/misra_tu.cpp
	clang-tidy --config-file=$(SCIFORGE_LINT)/clang-tidy-misra \
	    --checks='-cppcoreguidelines-pro-type-union-access' \
	    --header-filter='include/real/.*' \
	    $(BUILD)/misra_tu.cpp -- $(CXXSTD) $(INCLUDES)

# libFuzzer is a Clang feature; this target always uses Clang regardless of CXX.
# FUZZ_TIME bounds a local run (CI uses a short smoke run); point the corpus at
# build/fuzz/corpus to accumulate findings across runs.
FUZZ_TIME ?= 30
FUZZ_DIR  := $(BUILD)/fuzz

# Per-binding delegation. Each binding owns a standardized Makefile (bindings/{python,c,rust,go}/Makefile);
# `make python-test`, `make c-fuzz`, `make rust-vendor`, `make go-test` ... forward to it, and
# `make <binding>-help` lists a binding's targets. This is THE form — there are no duplicate root targets
# for the binding work.
python-%:
	@$(MAKE) -C bindings/python $*
c-%:
	@$(MAKE) -C bindings/c $*
rust-%:
	@$(MAKE) -C bindings/rust $*
go-%:
	@$(MAKE) -C bindings/go $*

fuzz:
	mkdir -p $(FUZZ_DIR)/corpus
	clang++ $(CXXSTD) -O1 -g $(INCLUDES) \
	    -fsanitize=fuzzer,address,undefined fuzz/fuzz_target.cpp -o $(FUZZ_DIR)/fuzz_target
	$(FUZZ_DIR)/fuzz_target -max_total_time=$(FUZZ_TIME) -timeout=10 \
	    $(FUZZ_DIR)/corpus fuzz/corpus

# ThreadSanitizer smoke: a standalone multi-threaded program (tests/compat/tsan_compat.cpp) that hammers the
# concurrent lazy std_engine() build on shared regex objects, proving the "concurrent const ops are
# race-free" claim reproducibly. Standalone (no framework / test_static.cpp non-atomic op-new counter,
# which would itself race). Clang feature; always uses Clang.
tsan:
	mkdir -p $(BUILD)
	clang++ $(CXXSTD) -O1 -g $(INCLUDES) -fsanitize=thread tests/compat/tsan_compat.cpp -o $(BUILD)/tsan_compat
	$(BUILD)/tsan_compat

# ThreadSanitizer smoke for the CORE concurrent caches (7.45 shared-confirm / immutables / call_once),
# not the compat layer. Barrier-synchronized first search on a FRESH const regex each iteration —
# without the barrier+fresh pattern, call_once fills once and late threads never race the warm path
# (false-negative risk). Harness proof (must go red):
#   TSAN_OPTIONS=halt_on_error=1 REAL_TSAN_INJECT_RACE=1 make tsan-core
# ASLR: some Linux kernels hit TSan "FATAL: unexpected memory mapping" under high ASLR — prefer
# setarch -R when present (Linux CI/devbox); fall back to a direct run (macOS has no setarch).
tsan-core:
	mkdir -p $(BUILD)
	clang++ $(CXXSTD) -O1 -g $(INCLUDES) -fsanitize=thread \
	    tests/engine/tsan_core.cpp -o $(BUILD)/tsan_core
	setarch $$(uname -m) -R $(BUILD)/tsan_core 2>/dev/null || $(BUILD)/tsan_core

# Differential fuzzer: real::compat vs std::regex (search/replace/iterate/token/match-flags). This
# is the net that has caught every silent divergence in the compat layer, so it runs in CI too.
fuzz-compat:
	mkdir -p $(FUZZ_DIR)/corpus-compat
	clang++ $(CXXSTD) -O1 -g $(INCLUDES) \
	    -fsanitize=fuzzer,address,undefined fuzz/fuzz_compat.cpp -o $(FUZZ_DIR)/fuzz_compat
	$(FUZZ_DIR)/fuzz_compat -max_total_time=$(FUZZ_TIME) -timeout=10 \
	    $(FUZZ_DIR)/corpus-compat fuzz/corpus

# Differential: real::compat::re2 (drop-in) vs true libre2 (oracle). Curated harness + can-fail
# (REAL_RE2_DIFF_CANFAIL=1 must trip). Requires pkg-config re2; skips cleanly when absent so
# full-local-gate / hosts without libre2 are not blocked. CI installs libre2 and runs this.
# Reproduce a finding: make fuzz-re2  (exit 1 = drop-in parity bug; ENG \w/UCD is allowlisted only).
fuzz-re2:
	@if ! pkg-config --exists re2; then \
	  echo "fuzz-re2: SKIP — pkg-config re2 not found (install libre2 to enable the oracle)"; \
	  exit 0; \
	fi
	@mkdir -p $(FUZZ_DIR)
	@echo "fuzz-re2: building drop-in vs libre2 differential ($$(pkg-config --modversion re2))"
	@$(CXX) $(CXXSTD) -O1 -g $(INCLUDES) fuzz/fuzz_re2.cpp \
	    $$(pkg-config --cflags --libs re2) -o $(FUZZ_DIR)/fuzz_re2
	@echo "fuzz-re2: curated differential (must be green — exit 0)"
	@$(FUZZ_DIR)/fuzz_re2
	@echo "fuzz-re2: can-fail inject (must trip, exit 0 in inject mode)"
	@REAL_RE2_DIFF_CANFAIL=1 $(FUZZ_DIR)/fuzz_re2 >/dev/null
	@echo "fuzz-re2: PASS (parity green + can-fail intact)"

doc: coverage-html
	mkdir -p $(BUILD)/doc
	doxygen Doxyfile
	@rm -rf $(BUILD)/doc/html/coverage
	@cp -R $(COV_DIR)/html $(BUILD)/doc/html/coverage
	@echo "API reference: $(BUILD)/doc/html/index.html"

# Doc target for environments without Clang/LLVM coverage tools (e.g. CI that only
# needs the API reference). It does not rebuild the coverage report.
doc-no-coverage:
	mkdir -p $(BUILD)/doc
	doxygen Doxyfile
	@echo "API reference: $(BUILD)/doc/html/index.html"

format:
	uncrustify -c $(SCIFORGE_LINT)/uncrustify.cfg --replace --no-backup $(FORMAT_FILES)

format-check:
	uncrustify -c $(SCIFORGE_LINT)/uncrustify.cfg --check $(FORMAT_FILES)

# --- Python binding -------------------------------------------------------

# Builds the abi3 extension in place against include/. Packaging lives in the
# root pyproject.toml / setup.py.
# build_ext compares only _real.cpp's timestamp against the built .so; it never sees the
# header-only engine the .cpp #includes, so a header-only change would leave a stale .so.
# Gate the rebuild on the headers through a stamp, and pass --force so the recompile
# actually happens when a header changed (build_ext would otherwise skip it).
HEADERS := $(shell find include/real -name '*.hpp')

# Version-consistency gate: pyproject.toml is the single source of truth — `make
# release` bumps __init__.py from it, CMakeLists.txt derives it. Asserts the three
# agree and that CMake still DERIVES (no hardcoded literal that could drift) — the
# invariant the CMake 2026.6.6-vs-.8 drift violated.
version-check:
	@py=$$(sed -nE 's/^version = "([0-9][0-9.]*)"/\1/p' pyproject.toml); \
	 ini=$$(sed -nE 's/^__version__ = "([0-9][0-9.]*)"/\1/p' bindings/python/real/__init__.py); \
	 lit=$$(sed -nE 's/^project\([A-Za-z_]+ VERSION ([0-9][0-9.]*).*/\1/p' CMakeLists.txt); \
	 if [ -z "$$py" ]; then echo "version-check: no version found in pyproject.toml"; exit 1; fi; \
	 if [ "$$py" != "$$ini" ]; then echo "version-check: DRIFT pyproject=$$py vs __init__=$$ini"; exit 1; fi; \
	 if [ -n "$$lit" ]; then echo "version-check: CMakeLists.txt hardcodes VERSION $$lit (must derive from pyproject.toml = $$py)"; exit 1; fi; \
	 if ! grep -q 'file(READ.*pyproject\.toml' CMakeLists.txt; then echo "version-check: CMakeLists.txt must derive its version from pyproject.toml"; exit 1; fi; \
	 vmaj=$$(sed -nE 's/^#define REAL_VERSION_MAJOR ([0-9]+).*/\1/p' include/real/version.hpp); \
	 vmin=$$(sed -nE 's/^#define REAL_VERSION_MINOR ([0-9]+).*/\1/p' include/real/version.hpp); \
	 vpat=$$(sed -nE 's/^#define REAL_VERSION_PATCH ([0-9]+).*/\1/p' include/real/version.hpp); \
	 hdr="$$vmaj.$$vmin.$$vpat"; \
	 if [ "$$hdr" != "$$py" ]; then echo "version-check: DRIFT version.hpp=$$hdr vs pyproject=$$py"; exit 1; fi; \
	 crate=$$(sed -nE 's/^version = "([0-9][0-9.]*)"/\1/p' bindings/rust/Cargo.toml | head -1); \
	 if [ "$$crate" != "$$py" ]; then echo "version-check: DRIFT Cargo.toml=$$crate vs pyproject=$$py"; exit 1; fi; \
	 cff=$$(sed -nE 's/^version: "([0-9][0-9.]*)"/\1/p' CITATION.cff); \
	 if [ "$$cff" != "$$py" ]; then echo "version-check: DRIFT CITATION.cff=$$cff vs pyproject=$$py (it drifted for ~10 releases unchecked)"; exit 1; fi; \
	 rme=$$(sed -nE 's/.*GIT_TAG v([0-9][0-9.]*).*/\1/p' README.md | head -1); \
	 if [ -n "$$rme" ] && [ "$$rme" != "$$py" ]; then echo "version-check: DRIFT README FetchContent GIT_TAG=$$rme vs pyproject=$$py"; exit 1; fi; \
	 bench=$$(sed -nE 's/.*REAL `([0-9][0-9.]+)`.*/\1/p' docs/BENCHMARKS.md | head -1); \
	 if [ -n "$$bench" ] && [ "$$bench" != "$$py" ]; then echo "version-check: WARN — docs/BENCHMARKS.md is stamped against REAL $$bench, current is $$py; benchmarks may be stale (re-run 'make bench-engines' / 'make bench', or proceed knowingly)"; fi; \
	 echo "version-check: $$py (pyproject = __init__ = CMake-derived = version.hpp = Cargo.toml = CITATION.cff = README; bench-stamp = $$bench)"

# Every pass/fail gate this machine owns, in one command — the canonical pre-push check
# and, like the SciLang-era libraries, the macOS gate of record. REAL holds its own
# (>95% lines) coverage bar rather than the strict 100% 4D of the SciLang-era libraries,
# so coverage is NOT bundled here (run `make coverage` separately); this gate is the
# binary pass/fail checks. doc-no-coverage fails on any Doxygen warning (WARN_AS_ERROR).
# GXX defaults to the CI GCC (g++-14); override with `make full-local-gate GXX=g++-13`. If it is
# absent, the GCC leg is skipped with a warning (the g++-14 CI job is the backstop).
GXX ?= g++-14
# Exhaustive compat routing check: real::compat vs the LOCAL std::regex over the shared enumerator's
# small tier-1 space (fuzz/exhaustive_compat.cpp). The oracle is the local std (compat's philosophy). The
# Python enumerator emits patterns/inputs; the C++ runner consumes them. Passes when there is no SERIOUS
# (span / accept-reject) divergence — a routing/screen bug. The nullable-loop group-capture class (real's
# RE2/Rust/Go lineage vs std's empty-final iteration) is counted separately and reported. Tune the tier
# with EC_K / EC_N; the default is the ~10 s PR tier, the nightly widens it.
EC_K ?= 4
EC_N ?= 6
exhaustive-compat:
	@mkdir -p $(BUILD)
	@$(PYRUN) -c "from sciforge.corpus.exhaustive import enumerate_patterns as P, enumerate_inputs as I; open('$(BUILD)/ec_pats.txt','w').write(chr(10).join(P($(EC_K), tier=2))); open('$(BUILD)/ec_inps.txt','w').write(chr(10).join(I($(EC_N))))"
	@$(CXX) $(CXXSTD) -O2 $(INCLUDES) fuzz/exhaustive_compat.cpp -o $(BUILD)/exhaustive_compat
	@$(BUILD)/exhaustive_compat $(BUILD)/ec_pats.txt $(BUILD)/ec_inps.txt

# The Fowler / AT&T POSIX conformance of the compat layer: the three vendored testregex corpora through
# real::compat vs the local std, three-way-arbitrated against the corpus's POSIX expectation and bucketed.
# Hard invariants (lib-stable): b3 == b4 == std_only == 0, the b1 perfect count, and the per-file parsed-case
# counts (a "no silent caps" pin). See fuzz/fowler_compat.cpp.
fowler-compat:
	@mkdir -p $(BUILD)
	@$(CXX) $(CXXSTD) -O2 $(INCLUDES) fuzz/fowler_compat.cpp -o $(BUILD)/fowler_compat
	@$(BUILD)/fowler_compat tests/corpora/fowler

# Pin-drift lint: fail if this repo's workflows pin more than one SciForge version (the shared
# tools/check-pins.sh, owned by SciForge). Skipped with a warning when the sibling tool is absent.
check-pins:
	@if test -x $(SCIFORGE_TOOLS)/check-pins.sh; then $(SCIFORGE_TOOLS)/check-pins.sh .; \
	 else echo "check-pins: WARN — $(SCIFORGE_TOOLS)/check-pins.sh absent, skipped (CI covers it)"; fi


check-layers:
	@python3 tools/check_layers.py

# Fail-fast gate of record: CHEAP / FAST first, expensive last. First non-zero aborts
# the rest (no -k). Order is intentional — a Doxygen param miss or format drift must not
# wait for sanitize/python. Compound steps (lint | tee) use `set -euo pipefail`.
# --- calibrated local gating (dev-tooling; CI stays FULL and uncalibrated -- the real net) -
#
# full-local-gate (18/18) is the right call for engine code, but wasteful to run in full for a
# pure version bump or a doc-only change. These 3 targets run the SUBSET matched to the diff's
# own category, detected via `git diff --name-only origin/main` -- and each one REFUSES to run
# (pointing back to full-local-gate) if that diff touches a file outside its own category, so a
# miscategorized change cannot quietly get an under-powered gate. Safe ONLY because CI itself
# stays full and uncalibrated before any tag: this is a local-loop velocity optimization, never
# a coverage reduction. Doubt about the category -> the guard fails closed; never guess.
#
#   change type (files)                                | target    | runs
#   bump: version.hpp/pyproject/__init__/Cargo/CITATION/| gate-bump | version-check + build
#     README + docs/release-notes/*.md, docs/BENCHMARKS |           |
#   doc-only: *.md, *.dox, or comment-only .hpp diffs   | gate-doc  | doc-check if headers/
#                                                        |           | Doxyfile touched;
#                                                        |           | format-check if .cpp
#                                                        |           | touched; verify_unicode_
#                                                        |           | ratios.py if BENCHMARKS.md
#                                                        |           | touched
#   test-only: tests/, or the Makefile itself            | gate-test | test + sanitize +
#                                                        |           | coverage-check
#
# Anything under include/real/{engine,core,automata,frontend}, storage.hpp, real.hpp: none of
# these targets accept it -- full-local-gate is the only gate for engine code, unconditionally.

gate-bump:
	@set -euo pipefail; \
	 files="$$(git diff --name-only $(GATE_BASE) -- .)"; \
	 bad="$$(printf '%s\n' "$$files" | grep -vE '^(include/real/version\.hpp|pyproject\.toml|bindings/python/real/__init__\.py|bindings/rust/Cargo\.toml|CITATION\.cff|README\.md|docs/release-notes/.*\.md|docs/BENCHMARKS\.md)$$' | grep -v '^$$' || true)"; \
	 if [ -n "$$bad" ]; then \
	   echo "gate-bump: REFUSED — out-of-category file(s) vs $(GATE_BASE), use full-local-gate:"; \
	   printf '%s\n' "$$bad" | sed 's/^/  /'; \
	   exit 1; \
	 fi; \
	 echo "gate-bump: category OK (version-bump files only)"
	@echo "── [1/2] version-check"
	@$(MAKE) version-check
	@echo "── [2/2] build (compile-smoke)"
	@$(MAKE) build
	@echo "gate-bump: PASS (version-check + build)"

gate-doc:
	@set -euo pipefail; \
	 files="$$(git diff --name-only $(GATE_BASE) -- .)"; \
	 for f in $$files; do \
	   case "$$f" in \
	     *.md|*.dox) continue ;; \
	   esac; \
	   if [ ! -f "$$f" ]; then \
	     echo "gate-doc: REFUSED — $$f deleted/renamed outside .md/.dox, use full-local-gate"; exit 1; \
	   fi; \
	   noncomment="$$(git diff -U0 $(GATE_BASE) -- "$$f" | grep -E '^[+-][^+-]' | grep -vE '^[+-][[:space:]]*(//|/\*|\*/|\*[[:space:]])' | grep -vE '^[+-][[:space:]]*$$' || true)"; \
	   if [ -n "$$noncomment" ]; then \
	     echo "gate-doc: REFUSED — $$f has non-comment changes, use full-local-gate:"; \
	     printf '%s\n' "$$noncomment" | head -5 | sed 's/^/  /'; \
	     exit 1; \
	   fi; \
	   echo "gate-doc: $$f is comment-only, treating as doc"; \
	 done; \
	 echo "gate-doc: category OK (doc-only / comment-only diff)"
	@set -euo pipefail; \
	 files="$$(git diff --name-only $(GATE_BASE) -- .)"; \
	 if printf '%s\n' "$$files" | grep -qE '\.hpp$$|Doxyfile$$'; then \
	   echo "── doc-check (headers or Doxyfile touched)"; $(MAKE) doc-check; \
	 else echo "gate-doc: skip doc-check (no headers/Doxyfile touched)"; fi
	@set -euo pipefail; \
	 files="$$(git diff --name-only $(GATE_BASE) -- .)"; \
	 if printf '%s\n' "$$files" | grep -qE '\.cpp$$'; then \
	   echo "── format-check (.cpp touched)"; $(MAKE) format-check; \
	 else echo "gate-doc: skip format-check (no .cpp touched)"; fi
	@set -euo pipefail; \
	 files="$$(git diff --name-only $(GATE_BASE) -- .)"; \
	 if printf '%s\n' "$$files" | grep -qE '^docs/BENCHMARKS\.md$$'; then \
	   echo "── verify_unicode_ratios.py (BENCHMARKS.md touched)"; python3 benchmarks/verify_unicode_ratios.py; \
	 else echo "gate-doc: skip verify_unicode_ratios.py (BENCHMARKS.md untouched)"; fi
	@echo "gate-doc: PASS"

gate-test:
	@set -euo pipefail; \
	 files="$$(git diff --name-only $(GATE_BASE) -- .)"; \
	 bad="$$(printf '%s\n' "$$files" | grep -vE '^(tests/|Makefile$$)' | grep -v '^$$' || true)"; \
	 if [ -n "$$bad" ]; then \
	   echo "gate-test: REFUSED — out-of-category file(s) vs $(GATE_BASE), use full-local-gate:"; \
	   printf '%s\n' "$$bad" | sed 's/^/  /'; \
	   exit 1; \
	 fi; \
	 echo "gate-test: category OK (tests/ or Makefile only)"
	@echo "── [1/3] test"
	@$(MAKE) test
	@echo "── [2/3] sanitize"
	@$(MAKE) sanitize
	@echo "── [3/3] coverage-check"
	@$(MAKE) coverage-check
	@echo "gate-test: PASS (test + sanitize + coverage-check)"

full-local-gate:
	@echo "full-local-gate: start (fail-fast — cheap first, first red stops the train)"
	@echo "── [1/18] format-check"
	@$(MAKE) format-check
	@echo "── [2/18] version-check"
	@$(MAKE) version-check
	@echo "── [3/18] check-layers"
	@$(MAKE) check-layers
	@echo "── [4/18] check-pins"
	@$(MAKE) check-pins
	@echo "── [5/18] doc-no-coverage (Doxygen WARN_AS_ERROR — fast, high signal)"
	@$(MAKE) doc-no-coverage
	@echo "── [6/18] doc-check (CI-pinned Doxygen when Docker is available)"
	@$(MAKE) doc-check
	@echo "── [7/18] misra (single synthetic TU)"
	@$(MAKE) misra
	@echo "── [8/18] c-test"
	@$(MAKE) c-test
	@echo "── [9/18] matrix-gate"
	@$(MAKE) matrix-gate
	@echo "── [10/18] fowler-compat"
	@$(MAKE) fowler-compat
	@echo "── [11/18] exhaustive-compat"
	@$(MAKE) exhaustive-compat
	@echo "── [12/18] test (default CXX)"
	@$(MAKE) test
	@echo "── [13/18] rust-test"
	@$(MAKE) rust-test
	@echo "── [14/18] rust-publish-check"
	@$(MAKE) rust-publish-check
	@echo "── [15/18] python-test"
	@$(MAKE) python-test
	@echo "── [16/18] lint"
	@set -euo pipefail; \
	  mkdir -p $(BUILD); \
	  $(MAKE) lint 2>&1 | tee $(BUILD)/lint.log; \
	  if grep -qE 'warning:|error:' $(BUILD)/lint.log; then \
	    echo "full-local-gate: FAIL at lint (see $(BUILD)/lint.log)"; exit 1; \
	  fi
	@echo "── [17/18] test (GCC leg) + sanitize (slowest last)"
	@if command -v $(GXX) >/dev/null 2>&1; then $(MAKE) test CXX=$(GXX) BUILD=$(BUILD)/gcc; else echo "full-local-gate: WARN — $(GXX) absent, GCC leg skipped (CI covers it)"; fi
	@$(MAKE) sanitize
	@echo "── [18/18] coverage-check (line floor $(COV_FLOOR)% — closes the P0 gate hole)"
	@$(MAKE) coverage-check
	@echo "full-local-gate: ALL gates green (cheap→doc→tests→lint→sanitize→coverage; first red would have stopped the train)"

# doc-check builds the docs under the EXACT CI Doxygen (1.9.8, in Docker) via the shared SciForge tool,
# so a warning the developer's newer local Doxygen tolerates cannot slip past to CI or a release (the
# gate-hole that shipped a broken Docs build before). Skipped with a warning when Docker or the tool is
# absent — visible, never a false green; the Docs CI job is the backstop.
doc-check:
	@if command -v docker >/dev/null 2>&1 && test -x $(SCIFORGE_TOOLS)/doxygen-check.sh; then \
	   $(SCIFORGE_TOOLS)/doxygen-check.sh . Doxyfile; \
	 else \
	   echo "doc-check: WARN — Docker or $(SCIFORGE_TOOLS)/doxygen-check.sh absent, CI-Doxygen check skipped (the Docs CI job is the backstop)"; \
	 fi

# REAL-vs-rust duel: the §E table generator (ns/byte, match-count cross-checked, non-cherry-picked).
# Builds the REAL harness; the rust harness needs a Rust toolchain (cargo build --release in
# benchmarks/duel/rust_bench). Manual, not a CI gate.
bench-duel:
	@c++ $(CXXSTD) -O2 $(INCLUDES) benchmarks/duel/real_bench.cpp -o benchmarks/duel/real_bench
	@$(PYTHON) benchmarks/duel/run_duel.py

# P0 profiling substrate: clean + instrumented binaries, 2-pass JSONL, markdown grid.
# Not a CI gate (informational; timings are host-noise). See benchmarks/profile/ and
# .recovery/p0-profile-substrate-fiche.md.
PROF_DIR := $(BUILD)/profile
profile-sample-build:
	@mkdir -p $(PROF_DIR)
	@c++ $(CXXSTD) -O3 -DNDEBUG $(INCLUDES) \
	    -Ibenchmarks/profile \
	    benchmarks/profile/profile_runner.cpp -o $(PROF_DIR)/profile_runner_clean
	@c++ $(CXXSTD) -O3 -DNDEBUG -DREAL_PROFILE $(INCLUDES) \
	    -Ibenchmarks/profile \
	    benchmarks/profile/profile_runner.cpp -o $(PROF_DIR)/profile_runner_inst
	@echo "profile binaries: $(PROF_DIR)/profile_runner_{clean,inst}"

profile-sample: profile-sample-build
	@$(PYTHON) benchmarks/profile/run_profile.py

profile-callgrind: profile-sample-build
	@bash benchmarks/profile/callgrind_runs.sh

# The 4-D veto matrix (pattern x size x match/no-match x density): a COMMITTED regression gate for the
# inner-literal route, born from repeated fixes that each missed a dimension. Mechanical verdict, non-zero exit
# on a red cell. `bench-matrix` is the full matrix (for an arc's veto); `matrix-gate` (in full-local-gate) is a
# fast 64 KB subset.
bench-matrix:
	@mkdir -p $(BUILD)
	@c++ $(CXXSTD) -O2 $(INCLUDES) benchmarks/matrix4d/matrix4d.cpp -o $(BUILD)/matrix4d
	@$(BUILD)/matrix4d

matrix-gate:
	@mkdir -p $(BUILD)
	@c++ $(CXXSTD) -O2 $(INCLUDES) benchmarks/matrix4d/matrix4d.cpp -o $(BUILD)/matrix4d
	@$(BUILD)/matrix4d --short

# Multi-engine C++ throughput benchmark (REAL vs std::regex vs PCRE2 vs RE2).
# Optional engines are compiled in only when pkg-config locates them.
# The C++ binary only measures and emits JSON; benchmarks/bench_engines.py (the consumer)
# applies the shared stats module to produce the table, CIs, and ASCII box-plots. Manual,
# not a CI gate. Tune samples via BENCH_SAMPLES / BENCH_BOOTSTRAP.
bench-engines:
	@mkdir -p $(BUILD)
	@flags=""; \
	 if pkg-config --exists libpcre2-8; then flags="$$flags -DHAVE_PCRE2 $$(pkg-config --cflags --libs libpcre2-8)"; fi; \
	 if pkg-config --exists re2; then flags="$$flags -DHAVE_RE2 $$(pkg-config --cflags --libs re2)"; fi; \
	 commit=$$(git rev-parse --short HEAD 2>/dev/null || echo unknown); \
	 echo "engines: REAL std::regex$${flags:+ +optional}"; \
	 c++ -std=c++20 -O2 -DBENCH_FLAGS='"-O2"' -DBENCH_COMMIT="\"$$commit\"" $(INCLUDES) -I$(SCIFORGE_INCLUDE) benchmarks/bench_engines.cpp $$flags -o $(BUILD)/bench_engines
	$(PYRUN) benchmarks/bench_engines.py $(BUILD)/bench_engines

# Multi-pattern which-matched (TABLE A) + extraction count (TABLE B). Informational only —
# not a CI gate. RE2 and Hyperscan optional via pkg-config (libhs / hyperscan names vary).
bench-multipattern:
	@mkdir -p $(BUILD)
	@flags=""; \
	 if pkg-config --exists re2; then flags="$$flags -DHAVE_RE2 $$(pkg-config --cflags --libs re2)"; fi; \
	 if pkg-config --exists libhs; then flags="$$flags -DHAVE_HS $$(pkg-config --cflags --libs libhs)"; \
	 elif pkg-config --exists libhyperscan; then flags="$$flags -DHAVE_HS $$(pkg-config --cflags --libs libhyperscan)"; \
	 elif [ -f /usr/include/hs/hs.h ] || [ -f /usr/local/include/hs/hs.h ] || [ -f /opt/homebrew/include/hs/hs.h ]; then \
	   flags="$$flags -DHAVE_HS -lhs"; fi; \
	 echo "mp_bench flags:$$flags"; \
	 c++ -std=c++20 -O2 $(INCLUDES) benchmarks/mp_bench.cpp $$flags -o $(BUILD)/mp_bench
	@$(BUILD)/mp_bench

# Proves the *system* install end to end, the exact packager path: install REAL to a temp prefix
# with -DBUILD_TESTING=OFF (noarch LIBDIR=lib, no SciForge — the library stands alone), then
# consume it the three supported C++ ways plus a negative check that the C++20 guard fires. CXX is
# honored (run under clang and g++). Used by the install-smoke CI job.
install-smoke:
	@set -e; \
	 pfx=$$(mktemp -d); cfg=$$(mktemp -d); work=$$(mktemp -d); \
	 trap 'rm -rf "$$pfx" "$$cfg" "$$work"' EXIT; \
	 cxx="$${CXX:-c++}"; \
	 echo "install-smoke: install REAL -> $$pfx (LIBDIR=lib), consumer cxx=$$cxx"; \
	 $(CMAKE) -S . -B "$$cfg" -DCMAKE_INSTALL_PREFIX="$$pfx" -DCMAKE_INSTALL_LIBDIR=lib \
	          -DBUILD_TESTING=OFF >/dev/null; \
	 $(CMAKE) --install "$$cfg" >/dev/null; \
	 expected=$$(sed -nE 's/^version = "([0-9][0-9.]*)"/\1/p' pyproject.toml); \
	 printf '#include <real/real.hpp>\n#include <real/version.hpp>\nstatic_assert(REAL_VERSION_MAJOR >= 2026, "version macro visible");\nint main(){ const real::regex r("[0-9]+"); return r.search("x42").matched() ? 0 : 1; }\n' > "$$work/smoke.cpp"; \
	 printf '#include <real/dfa.hpp>\nint main(){ return 0; }\n' > "$$work/negative.cpp"; \
	 echo "  (a) find_package(real CONFIG REQUIRED)"; \
	 printf 'cmake_minimum_required(VERSION 3.16)\nproject(s CXX)\nfind_package(real CONFIG REQUIRED)\nadd_executable(s smoke.cpp)\ntarget_link_libraries(s PRIVATE real::real)\nset_target_properties(s PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)\n' > "$$work/CMakeLists.txt"; \
	 $(CMAKE) -S "$$work" -B "$$work/b" -DCMAKE_PREFIX_PATH="$$pfx" >/dev/null; \
	 $(CMAKE) --build "$$work/b" >/dev/null; \
	 "$$work/b/s"; echo "      find_package: OK"; \
	 echo "  (b) pkg-config --cflags real + --modversion"; \
	 export PKG_CONFIG_PATH="$$pfx/lib/pkgconfig"; \
	 modv=$$(pkg-config --modversion real); \
	 test "$$modv" = "$$expected" || { echo "      modversion $$modv != pyproject $$expected"; exit 1; }; \
	 $$cxx -std=c++20 $$(pkg-config --cflags real) "$$work/smoke.cpp" -o "$$work/s_pc"; \
	 "$$work/s_pc"; echo "      pkg-config ($$modv): OK"; \
	 echo "  (c) direct-copy: -I<prefix>/include"; \
	 $$cxx -std=c++20 -I"$$pfx/include" "$$work/smoke.cpp" -o "$$work/s_dc"; \
	 "$$work/s_dc"; echo "      direct-copy: OK"; \
	 echo "  (d) negative: <real/dfa.hpp> under -std=c++17 must fail with a C++20 message"; \
	 if err=$$($$cxx -std=c++17 -I"$$pfx/include" "$$work/negative.cpp" -o "$$work/neg" 2>&1); then \
	   echo "      ERROR: the -std=c++17 compile SUCCEEDED — the guard did not fire"; exit 1; \
	 else \
	   case "$$err" in \
	     *C++20*) echo "      negative: OK (rejected, message mentions C++20)";; \
	     *) echo "      ERROR: failed, but the message does not mention C++20:"; printf '%s\n' "$$err" | head -3; exit 1;; \
	   esac; \
	 fi; \
	 echo "  (e) examples/ build + run against the installed package (proves the showcase examples never rot)"; \
	 $(CMAKE) -S examples -B "$$work/ex" -DCMAKE_PREFIX_PATH="$$pfx" >/dev/null; \
	 $(CMAKE) --build "$$work/ex" >/dev/null; \
	 "$$work/ex/hello"; "$$work/ex/redos_demo"; echo "      examples: OK"; \
	 echo "install-smoke: OK (find_package + pkg-config + direct-copy + negative guard + examples)"

# Installs the package from the repository root (root pyproject.toml builds the
# abi3 extension against include/). uninstall removes it by distribution name.
install:
	$(PYTHON) -m pip install .

uninstall:
	$(PYTHON) -m pip uninstall -y real-regex

# Cuts a calendar-versioned release. Computes YYYY.M.PATCH with the patch reset
# each month (first release of a month is .0; PEP 440 drops leading zeros, so
# 2026.6.1, never 2026.06.001), bumps pyproject.toml + __init__.py (CMakeLists.txt
# derives its version from pyproject.toml, so it follows automatically), commits, tags and
# pushes. Pushing the tag drives the Release workflow, which builds the wheels
# and sdist and publishes to PyPI. Run from a clean main.
release:
	@test "$$(git symbolic-ref --short HEAD)" = main || { echo "release from main only"; exit 1; }
	@test -z "$$(git status --porcelain)" || { echo "working tree not clean"; exit 1; }
	@git fetch --tags --quiet origin
	@year=$$(date -u +%Y); month=$$(date -u +%m | sed 's/^0//'); \
	 patch=$$(git tag -l "v$$year.$$month.*" | wc -l | tr -d ' '); \
	 version="$$year.$$month.$$patch"; \
	 echo "Releasing v$$version"; \
	 sed -i.bak -E "s/^version = \".*\"/version = \"$$version\"/" pyproject.toml && rm -f pyproject.toml.bak; \
	 sed -i.bak -E "s/^__version__ = \".*\"/__version__ = \"$$version\"/" bindings/python/real/__init__.py && rm -f bindings/python/real/__init__.py.bak; \
	 vmaj=$$(echo "$$version" | cut -d. -f1); vmin=$$(echo "$$version" | cut -d. -f2); vpat=$$(echo "$$version" | cut -d. -f3); \
	 sed -i.bak -E "s/^#define REAL_VERSION_MAJOR .*/#define REAL_VERSION_MAJOR $$vmaj/; \
	                s/^#define REAL_VERSION_MINOR .*/#define REAL_VERSION_MINOR $$vmin/; \
	                s/^#define REAL_VERSION_PATCH .*/#define REAL_VERSION_PATCH $$vpat/" include/real/version.hpp && rm -f include/real/version.hpp.bak; \
	 git add pyproject.toml bindings/python/real/__init__.py include/real/version.hpp; \
	 git commit -m "release: v$$version"; \
	 git tag "v$$version"; \
	 git push origin HEAD "v$$version"

clean:
	rm -rf $(BUILD) bindings/python/build bindings/python/real/*.so bindings/python/*.egg-info *.egg-info dist
