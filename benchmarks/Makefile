# benchmarks/ -- throughput/profile/duel/matrix benches (dev-only; matrix-gate is the one
# CI-relevant target, a fast regression net -- everything else here is informational,
# never a pass/fail gate). Standardized per-section Makefile: invoked from the repo root
# as `make bench-duel` / `bench-engines` / `bench-multipattern` / `bench-matrix` /
# `matrix-gate` / `profile-sample` / `profile-callgrind` (via the root's thin
# delegations, which preserve these names -- matrix-gate is full-local-gate's own step
# 12/22) or directly here with `make -C benchmarks <target>`.
#
# CWD idiom -- read before touching a recipe: every recipe below runs its original,
# UNCHANGED command line under `cd $(ROOT) &&` rather than $(ROOT)-prefixing each path
# piecemeal. That is a deliberate choice, not laziness: benchmarks/profile/callgrind_runs.sh
# reads corpora via a bare-relative `Path("build/profile/corpora")` (no anchoring at all
# inside the script), so it hard-requires CWD=$(ROOT) regardless of where `make` was
# invoked from -- a $(ROOT)/-prefix on the Makefile side would not reach a path baked
# into the script's own Python heredoc. benchmarks/duel/run_duel.py and
# benchmarks/profile/run_profile.py are themselves __file__-anchored (safe from any CWD)
# and benchmarks/bench_engines.py takes its binary as an argument (also CWD-agnostic) --
# `cd $(ROOT) &&` is applied to them too anyway, for one uniform idiom across every
# recipe rather than a per-script exception that a future edit could silently invalidate.
ROOT := $(abspath $(CURDIR)/..)
include $(ROOT)/mk/common.mk
# HELP_PREFIX stays empty (the default in mk/help.mk): this section's target names are
# the same at the root as here (thin delegations, not prefixed aliases like c-build).
include $(ROOT)/mk/help.mk

CXXSTD   := -std=c++20
INCLUDES := -Iinclude

.PHONY: bench-duel bench-engines bench-multipattern bench-matrix matrix-gate \
        profile-sample-build profile-sample profile-callgrind help
.DEFAULT_GOAL := help

# REAL-vs-rust duel: the §E table generator (ns/byte, match-count cross-checked, non-cherry-picked).
# Builds the REAL harness; the rust harness needs a Rust toolchain (cargo build --release in
# benchmarks/duel/rust_bench). Manual, not a CI gate.
bench-duel: ## REAL vs the regex crate, ns/byte (needs a Rust toolchain)
	@cd $(ROOT) && c++ $(CXXSTD) -O2 $(INCLUDES) benchmarks/duel/real_bench.cpp -o benchmarks/duel/real_bench
	@cd $(ROOT) && $(PYTHON) benchmarks/duel/run_duel.py

# P0 profiling substrate: clean + instrumented binaries, 2-pass JSONL, markdown grid.
# Not a CI gate (informational; timings are host-noise). See benchmarks/profile/ and
# .recovery/p0-profile-substrate-fiche.md. No `##`/root delegation -- internal to this
# Makefile (profile-sample / profile-callgrind depend on it; nothing outside benchmarks/
# invokes it by name).
PROF_DIR := $(BUILD)/profile
profile-sample-build:
	@mkdir -p $(PROF_DIR)
	@cd $(ROOT) && c++ $(CXXSTD) -O3 -DNDEBUG $(INCLUDES) \
	    -Ibenchmarks/profile \
	    benchmarks/profile/profile_runner.cpp -o $(PROF_DIR)/profile_runner_clean
	@cd $(ROOT) && c++ $(CXXSTD) -O3 -DNDEBUG -DREAL_PROFILE $(INCLUDES) \
	    -Ibenchmarks/profile \
	    benchmarks/profile/profile_runner.cpp -o $(PROF_DIR)/profile_runner_inst
	@echo "profile binaries: $(PROF_DIR)/profile_runner_{clean,inst}"

profile-sample: profile-sample-build ## P0 2-pass profile grid (JSONL + markdown; not a CI gate)
	@cd $(ROOT) && $(PYTHON) benchmarks/profile/run_profile.py

profile-callgrind: profile-sample-build ## Callgrind instrumentation runs over the profile binaries (not a CI gate)
	@cd $(ROOT) && bash benchmarks/profile/callgrind_runs.sh

# The 4-D veto matrix (pattern x size x match/no-match x density): a COMMITTED regression gate for the
# inner-literal route, born from repeated fixes that each missed a dimension. Mechanical verdict, non-zero exit
# on a red cell. `bench-matrix` is the full matrix (for an arc's veto); `matrix-gate` (in full-local-gate) is a
# fast 64 KB subset.
bench-matrix: ## Full 4-D veto matrix (pattern x size x match x density) — inner-literal regression gate
	@mkdir -p $(BUILD)
	@cd $(ROOT) && c++ $(CXXSTD) -O2 $(INCLUDES) benchmarks/matrix4d/matrix4d.cpp -o $(BUILD)/matrix4d
	@$(BUILD)/matrix4d

matrix-gate: ## Fast 64 KB subset of the 4-D veto matrix (used by full-local-gate)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && c++ $(CXXSTD) -O2 $(INCLUDES) benchmarks/matrix4d/matrix4d.cpp -o $(BUILD)/matrix4d
	@$(BUILD)/matrix4d --short

# Multi-engine C++ throughput benchmark (REAL vs std::regex vs PCRE2 vs RE2).
# Optional engines are compiled in only when pkg-config locates them.
# The C++ binary only measures and emits JSON; benchmarks/bench_engines.py (the consumer)
# applies the shared stats module to produce the table, CIs, and ASCII box-plots. Manual,
# not a CI gate. Tune samples via BENCH_SAMPLES / BENCH_BOOTSTRAP.
# static_regex vs regex on the same patterns. Informational, not a gate: timings are host noise, and the
# point is the SHAPE of the difference -- static_storage carries no regex_immutables, so a pattern whose
# dynamic route needs the byte program / lazy-DFA alphabet / one-pass table cannot take it here and its row
# reads far slower rather than faster. Match counts are cross-checked per row; a divergence exits non-zero,
# because a ratio between two paths that disagree on what they found is not a comparison.
bench-static: ## static_regex vs regex, same pattern (compile-time storage's routes and scratch)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && c++ $(CXXSTD) -O2 $(INCLUDES) benchmarks/bench_static.cpp -o $(BUILD)/bench_static
	@$(BUILD)/bench_static

bench-engines: ## C++ throughput vs std::regex/PCRE2/RE2 (if present)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && flags=""; \
	 if pkg-config --exists libpcre2-8; then flags="$$flags -DHAVE_PCRE2 $$(pkg-config --cflags --libs libpcre2-8)"; fi; \
	 if pkg-config --exists re2; then flags="$$flags -DHAVE_RE2 $$(pkg-config --cflags --libs re2)"; fi; \
	 commit=$$(git rev-parse --short HEAD 2>/dev/null || echo unknown); \
	 echo "engines: REAL std::regex$${flags:+ +optional}"; \
	 c++ -std=c++20 -O2 -DBENCH_FLAGS='"-O2"' -DBENCH_COMMIT="\"$$commit\"" $(INCLUDES) -I$(SCIFORGE_INCLUDE) benchmarks/bench_engines.cpp $$flags -o $(BUILD)/bench_engines
	cd $(ROOT) && $(PYRUN) benchmarks/bench_engines.py $(BUILD)/bench_engines

# Multi-pattern which-matched (TABLE A) + extraction count (TABLE B). Informational only —
# not a CI gate. RE2 and Hyperscan optional via pkg-config (libhs / hyperscan names vary).
bench-multipattern: ## multi-pattern which-matched / extraction (RE2/HS optional)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && flags=""; \
	 if pkg-config --exists re2; then flags="$$flags -DHAVE_RE2 $$(pkg-config --cflags --libs re2)"; fi; \
	 if pkg-config --exists libhs; then flags="$$flags -DHAVE_HS $$(pkg-config --cflags --libs libhs)"; \
	 elif pkg-config --exists libhyperscan; then flags="$$flags -DHAVE_HS $$(pkg-config --cflags --libs libhyperscan)"; \
	 elif [ -f /usr/include/hs/hs.h ] || [ -f /usr/local/include/hs/hs.h ] || [ -f /opt/homebrew/include/hs/hs.h ]; then \
	   flags="$$flags -DHAVE_HS -lhs"; fi; \
	 echo "mp_bench flags:$$flags"; \
	 c++ -std=c++20 -O2 $(INCLUDES) benchmarks/mp_bench.cpp $$flags -o $(BUILD)/mp_bench
	@$(BUILD)/mp_bench

help: ## Show these targets
	@$(HELP_RECIPE)
