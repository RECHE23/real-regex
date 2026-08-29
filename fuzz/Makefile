# fuzz/ -- libFuzzer robustness + differential fuzzers, and the exhaustive/Fowler
# conformance nets for the compat layer. Standardized per-section Makefile: invoked
# from the repo root as `make fuzz` / `fuzz-compat` / `fuzz-re2` / `exhaustive-compat` /
# `fowler-compat` (via the root's thin delegations, which preserve these 5 names -- the
# CI invariant: ci.yml's fuzz job runs fuzz/fuzz-compat/fuzz-re2 (l.120/126/145), the
# conformance job runs exhaustive-compat (l.178), and full-local-gate runs
# fowler-compat/exhaustive-compat as steps 13-14/22) or directly here with
# `make -C fuzz <target>`.
#
# tsan/tsan-core stay OUT of this section ("follow
# the sources"): their sources live in tests/ (tests/compat/tsan_compat.cpp,
# tests/engine/tsan_core.cpp), so they live with tests/, not here -- they
# remain root-only targets for now, untouched by this file.
#
# CWD idiom -- read before touching a recipe: every recipe below runs its original,
# UNCHANGED command line under `cd $(ROOT) &&` on the lines that reference a
# repo-relative path, rather than $(ROOT)-prefixing each path piecemeal. That matters
# here specifically because the *seed corpus* arguments are bare-relative directories
# baked straight into the original recipe text -- `fuzz/corpus` (fuzz, fuzz-compat) and
# `tests/corpora/fowler` (fowler-compat) -- not just the source file being compiled; a
# piecemeal $(ROOT)/-prefix pass over the compiler invocation alone could plausibly miss
# a positional runtime argument like that (confirmed by reading fuzz/fowler_compat.cpp:
# it defaults argv[1] to the bare-relative "tests/corpora/fowler", and the root recipe
# passes that same bare string explicitly). Lines with no repo-relative token at all (an
# mkdir on the already-$(ROOT)-anchored $(FUZZ_DIR)/$(BUILD), or running an
# already-built $(FUZZ_DIR)/binary with only absolute-path arguments) are left as-is,
# unchanged from the original -- the same selectivity benchmarks/Makefile uses (see its
# own header comment for the precedent).
ROOT := $(abspath $(CURDIR)/..)
include $(ROOT)/mk/common.mk
# HELP_PREFIX stays empty (the default in mk/help.mk): this section's target names are
# the same at the root as here (thin delegations, not prefixed aliases like c-build).
include $(ROOT)/mk/help.mk

# CXXSTD/INCLUDES: verified NOT in mk/common.mk today (it carries only BUILD/PYTHON/
# JOBS/SCIFORGE_*/PYRUN). Redefined locally here, UNANCHORED (`-Iinclude`, not
# `-I$(ROOT)/include`) -- matching benchmarks/Makefile's own precedent exactly, not
# just in spirit: every recipe line below that consumes them already runs under
# `cd $(ROOT) &&`, so the bare, original-unchanged spelling resolves correctly by
# construction -- $(ROOT)-anchoring it would be a behavioral no-op (identical resolved
# path once CWD is already $(ROOT)) while giving up the true byte-for-byte "recette
# d'origine inchangee" property the CWD idiom is built around. Kept local rather than
# promoted into common.mk because the proven precedent (benchmarks/Makefile)
# already made this same call the same way for the same reason, and mk/common.mk's own
# header says "extend as later sections migrate ... do not pre-fill for hypothetical
# consumers" -- a second local redefinition here is not drift, it is the established
# pattern repeating once more. common.mk is therefore untouched here (no root
# `make -n` re-verification needed for it).
#
# No CXX-forwarding block (`ifeq ($(origin CXX),command line)` -> CMAKE_CXX) here:
# none of the 5 recipes below drive CMake. fuzz/fuzz-compat hardcode `clang++`
# (libFuzzer is a Clang feature, always Clang regardless of CXX -- unchanged from the
# root original); fuzz-re2/exhaustive-compat/fowler-compat call bare $(CXX), GNU Make's
# own implicit variable -- a command-line override (`make fuzz-re2 CXX=g++-14`, or the
# CI form `CXX=clang++`) already propagates through MAKEFLAGS to this sub-make with
# zero forwarding code required, the same way FUZZ_TIME below does.
CXXSTD   := -std=c++20
INCLUDES := -Iinclude

# libFuzzer is a Clang feature; fuzz/fuzz-compat always use Clang regardless of CXX.
# FUZZ_TIME bounds a local run (CI uses a short smoke run); point the corpus at
# build/fuzz/corpus to accumulate findings across runs. FUZZ_TIME stays `?=` (not
# `:=`) -- the frozen variable interface (plan Section VI): CI passes `FUZZ_TIME=60`
# on the command line to `make fuzz`/`fuzz-compat`, and a `?=` here lets that override
# survive the delegation instead of being clobbered by a hard local default.
FUZZ_TIME ?= 30
FUZZ_DIR  := $(BUILD)/fuzz

.PHONY: fuzz fuzz-compat fuzz-compat-known fuzz-re2 exhaustive-compat fowler-compat help
.DEFAULT_GOAL := help

fuzz: ## libFuzzer robustness fuzzing (Clang; FUZZ_TIME=secs)
	mkdir -p $(FUZZ_DIR)/corpus
	cd $(ROOT) && clang++ $(CXXSTD) -O1 -g $(INCLUDES) \
	    -fsanitize=fuzzer,address,undefined fuzz/fuzz_target.cpp -o $(FUZZ_DIR)/fuzz_target
	cd $(ROOT) && $(FUZZ_DIR)/fuzz_target -max_total_time=$(FUZZ_TIME) -timeout=10 \
	    $(FUZZ_DIR)/corpus fuzz/corpus

# Differential fuzzer: real::compat vs std::regex (search/replace/iterate/token/match-flags). This
# is the net that has caught every silent divergence in the compat layer, so it runs in CI too.
fuzz-compat: ## Differential fuzz: real::compat vs std::regex (Clang; FUZZ_TIME=secs)
	mkdir -p $(FUZZ_DIR)/corpus-compat
	cd $(ROOT) && clang++ $(CXXSTD) -O1 -g $(INCLUDES) \
	    -fsanitize=fuzzer,address,undefined fuzz/fuzz_compat.cpp -o $(FUZZ_DIR)/fuzz_compat
	cd $(ROOT) && $(FUZZ_DIR)/fuzz_compat -max_total_time=$(FUZZ_TIME) -timeout=10 \
	    $(FUZZ_DIR)/corpus-compat fuzz/corpus

# A filed divergence, kept reproducible without blinding anything. A crash unit left in fuzz/corpus
# is read by ReadAndExecuteSeedCorpora BEFORE the mutation loop, so the target dies in ~40 ms having
# explored nothing -- and because a failed `run:` step aborts the rest of its job, the five nets
# after the compat smoke (C ABI shim, both ThreadSanitizer smokes, the RE2 differential) never ran
# either. Six nets dark for one filed finding. So the unit lives here, out of both timed corpora,
# and this target asserts it STILL reproduces: it goes red when the divergence is FIXED, which is
# exactly when the file must move to a regression test and leave this directory. That has happened
# once (the nullable-alternation inner-literal divergence: pinned in tests/frontend/
# test_inner_literal.cpp, the input kept as a corpus seed), which is why an EMPTY directory now
# passes with its count printed rather than failing. Zero filed is the goal state of the practice,
# not an unarmed check -- the decision this target makes is still live the moment a unit is filed,
# and the count is published either way so "nothing outstanding" can never be read as "not run".
fuzz-compat-known: ## Assert every filed compat divergence still reproduces (red once it is fixed)
	mkdir -p $(FUZZ_DIR)
	cd $(ROOT) && clang++ $(CXXSTD) -O1 -g $(INCLUDES) \
	    -fsanitize=fuzzer,address,undefined fuzz/fuzz_compat.cpp -o $(FUZZ_DIR)/fuzz_compat
	@cd $(ROOT) && n=0; for unit in fuzz/known-crashes/*; do \
	   test -f "$$unit" || continue; \
	   n=$$((n + 1)); \
	   if $(FUZZ_DIR)/fuzz_compat -runs=1 -timeout=10 \
	        -artifact_prefix=$(FUZZ_DIR)/ "$$unit" >/dev/null 2>&1; then \
	     echo "fuzz-compat-known: FAIL -- $$unit no longer reproduces."; \
	     echo "  The divergence it files is fixed: pin it as a regression test and delete it here."; \
	     exit 1; \
	   fi; \
	 done; \
	 test "$$n" -gt 0 \
	   || { echo "fuzz-compat-known: OK — 0 filed unit(s): nothing outstanding"; exit 0; }; \
	 echo "fuzz-compat-known: OK — $$n filed unit(s) still reproduce (filed, not fixed)"

# Differential: real::compat::re2 (drop-in) vs true libre2 (oracle). Curated harness + can-fail
# (REAL_RE2_DIFF_CANFAIL=1 must trip). Requires pkg-config re2; skips cleanly when absent so
# full-local-gate / hosts without libre2 are not blocked. CI installs libre2 and runs this.
# Reproduce a finding: make fuzz-re2  (exit 1 = drop-in parity bug; ENG \w/UCD is allowlisted only).
fuzz-re2: ## Differential: real::compat::re2 vs true libre2 (needs pkg-config re2)
	@if ! pkg-config --exists re2; then \
	  echo "fuzz-re2: SKIP — pkg-config re2 not found (install libre2 to enable the oracle)"; \
	  exit 0; \
	fi
	@mkdir -p $(FUZZ_DIR)
	@echo "fuzz-re2: building drop-in vs libre2 differential ($$(pkg-config --modversion re2))"
	@cd $(ROOT) && $(CXX) $(CXXSTD) -O1 -g $(INCLUDES) fuzz/fuzz_re2.cpp \
	    $$(pkg-config --cflags --libs re2) -o $(FUZZ_DIR)/fuzz_re2
	@echo "fuzz-re2: curated differential (must be green — exit 0)"
	@$(FUZZ_DIR)/fuzz_re2
	@echo "fuzz-re2: can-fail inject (must trip, exit 0 in inject mode)"
	@REAL_RE2_DIFF_CANFAIL=1 $(FUZZ_DIR)/fuzz_re2 >/dev/null
	@echo "fuzz-re2: PASS (parity green + can-fail intact)"

# Exhaustive compat routing check: real::compat vs the LOCAL std::regex over the shared enumerator's
# small tier-1 space (fuzz/exhaustive_compat.cpp). The oracle is the local std (compat's philosophy). The
# Python enumerator emits patterns/inputs; the C++ runner consumes them. Passes when there is no SERIOUS
# (span / accept-reject) divergence — a routing/screen bug. The nullable-loop group-capture class (real's
# RE2/Rust/Go lineage vs std's empty-final iteration) is counted separately and reported. Tune the tier
# with EC_K / EC_N; the default is the ~10 s PR tier, the nightly widens it. Both stay `?=` -- the frozen
# variable interface (plan Section VI): the nightly overrides them on the command line.
EC_K ?= 4
EC_N ?= 6
exhaustive-compat: ## Exhaustive compat routing check: real::compat vs local std::regex
	@mkdir -p $(BUILD)
	@$(PYRUN) -c "from sciforge.corpus.exhaustive import enumerate_patterns as P, enumerate_inputs as I; open('$(BUILD)/ec_pats.txt','w').write(chr(10).join(P($(EC_K), tier=2))); open('$(BUILD)/ec_inps.txt','w').write(chr(10).join(I($(EC_N))))"
	@cd $(ROOT) && $(CXX) $(CXXSTD) -O2 $(INCLUDES) fuzz/exhaustive_compat.cpp -o $(BUILD)/exhaustive_compat
	@$(BUILD)/exhaustive_compat $(BUILD)/ec_pats.txt $(BUILD)/ec_inps.txt

# The Fowler / AT&T POSIX conformance of the compat layer: the three vendored testregex corpora through
# real::compat vs the local std, three-way-arbitrated against the corpus's POSIX expectation and bucketed.
# Hard invariants (lib-stable): b3 == b4 == std_only == 0, the b1 perfect count, and the per-file parsed-case
# counts (a "no silent caps" pin). See fuzz/fowler_compat.cpp.
fowler-compat: ## Fowler/AT&T POSIX conformance of the compat layer (vendored testregex corpora)
	@mkdir -p $(BUILD)
	@cd $(ROOT) && $(CXX) $(CXXSTD) -O2 $(INCLUDES) fuzz/fowler_compat.cpp -o $(BUILD)/fowler_compat
	@cd $(ROOT) && $(BUILD)/fowler_compat tests/corpora/fowler

help: ## Show these targets
	@$(HELP_RECIPE)
