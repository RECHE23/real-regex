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

.PHONY: bench-duel bench-engines bench-multipattern bench-matrix matrix-gate bench-ac-gate bench-route-cliff bench-census \
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
# The AC gate's THREE regimes through normal dispatch. ac_regime.cpp beside it cannot answer this: it
# forces the automaton on and off to compare the two strategies, so the gate's own choice never runs --
# and the gate's choice is what ac_completion_pct changed. Informational, not a gate: the numbers are
# host noise, and what matters is the difference between two TREES (see the file's header for the
# interleaved form).
# Route identity, per-byte cost and MATCH COUNT in one table. route-probe reports routes without a
# timing (deliberately, for reachability); this one pairs them, because a route label cannot say whether
# falling off it is expensive. Informational, never a gate -- pinning a figure per row would produce reds
# indistinguishable from host noise.
# What a bit-parallel engine could actually take, weighted by cost. Informational, never a gate.
bench-census: ## [bench] Shift-OR eligibility census: four bits, ns/B weighting, VM-vs-DFA share
	@mkdir -p $(BUILD)
	@cd $(ROOT) && c++ $(CXXSTD) -O2 -DREAL_PROFILE $(INCLUDES) -I benchmarks -I$(SCIFORGE_INCLUDE) benchmarks/shift_or_census.cpp -o $(BUILD)/shift_or_census
	@$(BUILD)/shift_or_census $(if $(N),$(N),11)

# Counts, never times: the population that reaches the general VM, split by what would block a DFA.
# Two traps are documented in the source and both must be read before any percentage: the expander's
# `eligible` is NOT lazy_dfa's, and the generator injects a lookaround into 3 of 4 free compositions
# while a lookaround forces the general VM. Informational, never a gate.
bench-dfa-census: ## [bench] What could a DFA take of the general-VM population? (counting, not timing)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && c++ $(CXXSTD) -O2 -DREAL_PROFILE $(INCLUDES) -I benchmarks -I$(SCIFORGE_INCLUDE) benchmarks/dfa_census.cpp -o $(BUILD)/dfa_census
	@$(BUILD)/dfa_census $(if $(N),$(N),8000)

bench-route-cliff: ## [bench] Where a pattern loses its route, and what that costs (route + ns/B + matches)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && c++ $(CXXSTD) -O2 -DREAL_PROFILE $(INCLUDES) -I benchmarks -I$(SCIFORGE_INCLUDE) benchmarks/route_cliff.cpp -o $(BUILD)/route_cliff
	@$(BUILD)/route_cliff $(if $(N),$(N),15)

bench-ac-gate: ## [bench] The AC gate's completing / false-start / prose regimes, via normal dispatch
	@mkdir -p $(BUILD)
	@cd $(ROOT) && c++ $(CXXSTD) -O2 $(INCLUDES) benchmarks/ac_gate_probe.cpp -o $(BUILD)/ac_gate_probe
	@$(BUILD)/ac_gate_probe $(if $(REPS),$(REPS),15)

bench-static: ## static_regex vs regex, same pattern (compile-time storage's routes and scratch)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && c++ $(CXXSTD) -O2 $(INCLUDES) benchmarks/bench_static.cpp -o $(BUILD)/bench_static
	@$(BUILD)/bench_static

# Layout-robust judging. `bench-engines` above publishes the reference tables; these two decide
# whether a CHANGE moved anything, which is a different question and needs a different instrument --
# see docs/MEASUREMENT.md for why a single-build A/B does not answer it.
#
# bench-layout-null must be run ONCE PER MACHINE before any verdict: it runs the tree against itself
# (true delta exactly zero) to measure each row's noise floor. Floors land in $(BUILD)/floors.json.
bench-layout-null: ## [bench] Calibrate this machine's per-row noise floor (run before bench-layout)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && $(PYTHON) benchmarks/bench_layout.py --base . --null \
	   --sciforge $(abspath $(SCIFORGE_INCLUDE)) --save-floors $(BUILD)/floors.json

# BASE and CAND are trees (git worktrees are the easy way to get two). Requires bench-layout-null.
# The MINIMAL unit (real.hpp only, no competitor engines) -- the instrument that answers "does this
# help a consumer", where bench-layout answers "what does this project publish". They disagree: three
# changes refused on the four-engine harness for costing `\p{L}+` 5-9 % cost it NOTHING here. See
# docs/MEASUREMENT.md §5.5 and benchmarks/bench_minimal.cpp's own header. Needs its OWN null run.
bench-layout-min-null: ## [bench] Calibrate the minimal (real.hpp-only) unit's noise floor
	@mkdir -p $(BUILD)
	@cd $(ROOT) && $(PYTHON) benchmarks/bench_layout.py --base . --null --reps 3 \
	   --source bench_minimal.cpp --sciforge $(abspath $(SCIFORGE_INCLUDE)) \
	   --save-floors $(BUILD)/floors_min.json

bench-layout-min: ## [bench] Judge BASE/CAND in the minimal unit (needs bench-layout-min-null)
	@test -n "$(BASE)" -a -n "$(CAND)" || { echo "usage: make bench-layout-min BASE=<tree> CAND=<tree>"; exit 2; }
	@test -f $(BUILD)/floors_min.json || { echo "run 'make bench-layout-min-null' on this machine first"; exit 2; }
	@cd $(ROOT) && $(PYTHON) benchmarks/bench_layout.py --base $(BASE) --cand $(CAND) --reps 3 \
	   --source bench_minimal.cpp --sciforge $(abspath $(SCIFORGE_INCLUDE)) \
	   --floors $(BUILD)/floors_min.json --save-deltas $(BUILD)/deltas_min.json

bench-layout: ## [bench] Judge BASE=<tree> CAND=<tree> against the calibrated floors
	@test -n "$(BASE)" -a -n "$(CAND)" || { echo "usage: make bench-layout BASE=<tree> CAND=<tree>"; exit 2; }
	@test -f $(BUILD)/floors.json || { echo "run 'make bench-layout-null' on this machine first"; exit 2; }
	@cd $(ROOT) && $(PYTHON) benchmarks/bench_layout.py --base $(BASE) --cand $(CAND) \
	   --sciforge $(abspath $(SCIFORGE_INCLUDE)) --floors $(BUILD)/floors.json \
	   --save-deltas $(BUILD)/deltas.json

bench-engines: ## C++ throughput vs std::regex/PCRE2/RE2 (if present)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && flags=""; \
	 if pkg-config --exists libpcre2-8; then flags="$$flags -DHAVE_PCRE2 $$(pkg-config --cflags --libs libpcre2-8)"; fi; \
	 if pkg-config --exists re2; then flags="$$flags -DHAVE_RE2 $$(pkg-config --cflags --libs re2)"; fi; \
	 commit=$$(git rev-parse --short HEAD 2>/dev/null || echo unknown); \
	 echo "engines: REAL std::regex$${flags:+ +optional}"; \
	 c++ -std=c++20 -O2 -DBENCH_FLAGS='"-O2"' -DBENCH_COMMIT="\"$$commit\"" $(INCLUDES) -I$(SCIFORGE_INCLUDE) benchmarks/bench_engines.cpp $$flags -o $(BUILD)/bench_engines
	cd $(ROOT) && $(PYRUN) benchmarks/bench_engines.py $(BUILD)/bench_engines

# Per-call duel against std::regex. Its own translation unit, NOT part of bench-engines: that unit
# measures 100 KB corpora, which divides a fixed per-call cost away, and it carries its own standing
# prohibition against being extended. Every compiler found is timed, because a per-call cost one
# compiler elides and another does not is exactly what this regime exposes.
bench-percall: ## [bench] REAL's std::regex swap-in vs std::regex, per call (short subjects)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && for cc in c++ g++ g++-15 g++-14 clang++; do \
	   command -v $$cc >/dev/null 2>&1 || continue; \
	   ver=$$($$cc --version 2>/dev/null | head -1); \
	   case "$$seen" in *"$$ver"*) continue;; esac; seen="$$seen$$ver"; \
	   echo "== $$cc: $$ver"; \
	   $$cc -std=c++20 -O2 -DNDEBUG $(INCLUDES) -I$(SCIFORGE_INCLUDE) benchmarks/bench_percall.cpp \
	     -o $(BUILD)/bench_percall_$$cc 2>/dev/null || { echo "   (build failed, skipped)"; continue; }; \
	   $(BUILD)/bench_percall_$$cc $(or $(SAMPLES),15); echo; \
	 done

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
