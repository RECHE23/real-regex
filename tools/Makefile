# tools/ -- lint (clang-tidy), MISRA C++:2023-oriented analysis, uncrustify formatting,
# the include/real/ header-layering contract, and the frozen C ABI golden check.
# Standardized per-section Makefile: invoked from the repo root as `make lint` /
# `misra` / `format` / `format-check` / `check-layers` / `check-capi-abi` (via the
# root's thin delegations, which preserve these 6 names -- the CI invariant: ci.yml's
# preflight job runs format-check with a SCIFORGE_LINT override (l.52), check-layers
# (l.56) and check-capi-abi (l.62); the SciForge reusable spine lint-cpp.yml runs this
# repo's own `make format-check` + `make misra` against the shared lint/ config;
# full-local-gate runs format-check/check-layers/check-capi-abi as steps 1/3/5 and
# lint/misra further down) or directly here with `make -C tools <target>`.
#
# INCLUDES stays the RELATIVE `-Iinclude` from mk/common.mk -- LOAD-BEARING here.
# .clang-tidy's HeaderFilterRegex ('.*/(include/real|tests)/.*') requires a path
# component BEFORE `include/real`: the relative spelling produces header paths that
# read `include/real/...` (no leading component -> no match -> header diagnostics
# suppressed, the historical always-green lint). An absolute `-I$(ROOT)/include`
# makes every header path match and floods the log (measured A/B on one TU: 0 vs
# 24409 warnings -- the wagon 6 gate red). Safe here because every recipe below runs
# under `cd $(ROOT) &&`, matching how the root Makefile's own former lint/misra ran
# with CURDIR == ROOT. DO NOT re-anchor INCLUDES to absolute.
ROOT := $(abspath $(CURDIR)/..)
include $(ROOT)/mk/common.mk
# HELP_PREFIX stays empty (the default in mk/help.mk): this section's target names are
# the same at the root as here (thin delegations, not prefixed aliases like c-build).
include $(ROOT)/mk/help.mk

# unicode_fold.hpp and unicode_props.hpp are generated (their scripts own the layout;
# the regen tests pin them), so they are excluded from the hand-written-code formatter.
#
# `$(shell cd $(ROOT) && find ...)`, NOT `find $(ROOT)/...` and NOT a bare
# `find include tests ...`: FORMAT_FILES is parsed at LOAD time (like PYRUN in
# mk/common.mk), so a bare `find include tests` here would resolve against
# $(CURDIR) == tools/ (find tools/include tools/tests -- wrong/empty; a recipe-line
# `cd $(ROOT) &&` cannot fix a variable, only a recipe). Running `find` itself under
# `cd $(ROOT) &&` at load time produces ROOT-relative paths (`include/...`,
# `tests/...`), matching the historical uncrustify --check/--replace output
# byte-for-byte; an absolute `find $(ROOT)/...` would work too but change every
# message's path spelling.
FORMAT_FILES := $(shell cd $(ROOT) && find include tests -name '*.hpp' -o -name '*.cpp' | grep -vE 'include/real/unicode/unicode_(fold|props|property|script|binprop|scx)\.hpp')

.PHONY: lint misra format format-check check-layers check-capi-abi help
.DEFAULT_GOAL := help

lint: ## clang-tidy over the test sources
	@cd $(ROOT) && find tests -name '*.cpp' | xargs -P $(JOBS) -I{} clang-tidy {} -- $(CXXSTD) $(INCLUDES) -Ibindings/c -I$(SCIFORGE_INCLUDE)

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
misra: ## MISRA C++:2023-oriented analysis
	mkdir -p $(BUILD)
	printf '#include <real/real.hpp>\nint main(){ try { const real::regex r("a"); return r.search("a") ? 0 : 1; } catch (...) { return 2; } }\n' > $(BUILD)/misra_tu.cpp
	cd $(ROOT) && clang-tidy --config-file=$(SCIFORGE_LINT)/clang-tidy-misra \
	    --checks='-cppcoreguidelines-pro-type-union-access' \
	    --header-filter='include/real/.*' \
	    $(BUILD)/misra_tu.cpp -- $(CXXSTD) $(INCLUDES)

format: ## Uncrustify, in place
	cd $(ROOT) && uncrustify -c $(SCIFORGE_LINT)/uncrustify.cfg --replace --no-backup $(FORMAT_FILES)

format-check: ## Uncrustify, dry-run, exits non-zero on diff
	cd $(ROOT) && uncrustify -c $(SCIFORGE_LINT)/uncrustify.cfg --check $(FORMAT_FILES)

check-layers: ## Enforce the include/real/ header layering contract (no tier climbing)
	@cd $(ROOT) && python3 tools/check_layers.py

# C ABI frozen-surface pin (hardening #4): golden GENERATED from bindings/c/real_capi.h
# (never hand-edited). --check fails if header and golden diverge. Can-fail proof:
#   python3 tools/gen_capi_abi_golden.py --inject-enum REAL_ERR_SYNTAX=99 --stdout > tests/bindings/capi_abi_golden.txt
#   make check-capi-abi   # must exit non-zero; then: python3 tools/gen_capi_abi_golden.py
# Enum/flag value pins live in tests/bindings/test_capi_abi.cpp (real::flags cross-check).
check-capi-abi: ## C ABI golden vs real_capi.h + enum/flag pins (hardening #4)
	@cd $(ROOT) && python3 tools/gen_capi_abi_golden.py --check
	@echo "check-capi-abi: PASS (golden matches real_capi.h)"

help: ## Show these targets
	@$(HELP_RECIPE)
