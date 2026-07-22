# Thin orchestrator over CMake (build/test/sanitize/coverage) and the QA
# tools (clang-tidy, doxygen, libFuzzer, Python). CMake owns all compilation
# policy (CMakeLists.txt); this file only wires the frequent commands.
#
# Override the compiler with CXX on the command line, e.g.
#   make test CXX=g++-14
# A CXX environment variable is not forwarded; switching compilers reuses a
# cached build dir, so run `make clean` first.

CMAKE  ?= cmake
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
# $(CURDIR)-relative here, not $(ROOT)-anchored -- CURDIR == this Makefile's own
# directory (the repo root) by construction, so the two spellings are identical in
# value. mk/common.mk carries a $(ROOT)-anchored twin of this same variable for section
# Makefiles below the root (e.g. benchmarks/Makefile), which cannot rely on CURDIR.
PYRUN := PYTHONPATH=$(CURDIR)/bindings/python:$(abspath $(SCIFORGE_PYTHON)) $(PYTHON)

# What gate-bump/gate-doc/gate-test diff against to detect their change category (see the block
# comment above those targets). Override to compare against a single commit instead of the whole
# unpushed stack of several commits -- e.g.
# `GATE_BASE=HEAD~1 make gate-bump` diffs just the latest commit, not everything since origin/main.
# Default unchanged (fail-closed stays the behavior for anyone who doesn't override it).
GATE_BASE ?= origin/main
# The test harness (framework.hpp) is owned by SciForge; the test TUs include it
# as <sciforge/test/framework.hpp>. clang-tidy (make lint) needs that path too.
# Sibling checkout by default — matches the CMake SCIFORGE_INCLUDE_DIR default.
SCIFORGE_INCLUDE ?= ../sciforge/include
# SciForge also owns the shared lint config (the MISRA base + uncrustify.cfg), in
# its lint/ dir. Same sibling default; CI checks SciForge out alongside as ../sciforge.
SCIFORGE_LINT ?= ../sciforge/lint
SCIFORGE_TOOLS ?= ../sciforge/tools
# FORMAT_FILES lives in tools/Makefile with format/format-check.

# ROOT-anchor + inherit the shared vars every above-the-root section Makefile already
# gets from mk/common.mk: CXXSTD, INCLUDES (the engine
# header search path), the CMAKE_CXX CXX-forwarding guard, and the coverage floor
# (COV_FLOOR/COV_FLOOR_IGNORE -- full-local-gate's own step 22 echoes $(COV_FLOOR)
# directly below, see mk/common.mk's own comment for why it must live there and not
# tests/Makefile-only). Included AFTER every var this file defines above: their `?=`
# (and PYRUN's `:=`, identical by value since ROOT == CURDIR here) are no-ops against
# an already-set variable, so this changes nothing about CMAKE/PYTHON/BUILD/JOBS/
# SCIFORGE_*/PYRUN/GATE_BASE -- it only supplies the 4 names above that this file does not
# define itself (CXXSTD/INCLUDES/CMAKE_CXX's ifeq/COV_FLOOR*), verified with `make -n`:
# the only diff is $(INCLUDES) itself, now `-I$(ROOT)/include` (was the bare
# `-Iinclude`) -- both resolve to the same directory from CURDIR == ROOT, an accepted
# path-prefix equivalence, not a behavior change (lint/misra below are the consumers).
ROOT := $(abspath $(CURDIR))
include $(ROOT)/mk/common.mk

# Vendored in-repo (see mk/help.mk for why this is never a hard include toward sciforge).
include mk/help.mk

.PHONY: all build test sanitize coverage coverage-build coverage-html coverage-check \
        lint misra fuzz fuzz-compat fuzz-re2 check-capi-abi check-features-probe exhaustive-compat fowler-compat check-pins tsan tsan-core doc doc-no-coverage doc-check docs-site docs-site-gate format format-check full-local-gate gate-bump gate-doc gate-test clean \
        example-check \
        bench-engines bench-multipattern bench-duel bench-matrix matrix-gate \
        profile-sample profile-callgrind \
        version-check install install-smoke uninstall release help check-layers

.DEFAULT_GOAL := help

SECTIONS := bindings/c bindings/go bindings/python bindings/rust fuzz tests tools benchmarks docs
# Display order for the top-level help groups (workflow-first, not alphabetical). The
# group tag lives in each target's `## [group] ...` comment -- nothing else moves.
# "bench" dropped: every [bench]-tagged target moved to
# benchmarks/Makefile (thin delegations, untagged, the docs/ precedent), so the group
# would otherwise print a permanently empty "── bench ──" header here -- the real
# listing is the auto-appended "── benchmarks ──" section below (SECTIONS loop).
HELP_GROUPS := daily gates nets release
help: ## [daily] Aggregated help: top-level targets, then each section's own help
	@echo "Start here:  make test              — run the test suite (daily loop)"
	@echo "             make full-local-gate   — every gate, macOS record (pre-push)"
	@echo "             make {python,c,go,rust}-help"
	@groups_re=$$(echo "$(HELP_GROUPS)" | sed 's/ /|/g'); \
	 for g in $(HELP_GROUPS); do \
	   echo; \
	   echo "── $$g ──"; \
	   grep -hE "^[a-zA-Z0-9_-]+:.*## \[$$g\]" $(firstword $(MAKEFILE_LIST)) | \
	     sed -E 's/^([a-zA-Z0-9_-]+):.*## \[[a-zA-Z0-9_-]+\] /make \1\t/' | \
	     awk -F'\t' '{ printf "  %-24s — %s\n", $$1, $$2 }'; \
	 done; \
	 other=$$(grep -hE '^[a-zA-Z0-9_-]+:.*##' $(firstword $(MAKEFILE_LIST)) | grep -vE "## \[($$groups_re)\]"); \
	 if [ -n "$$other" ]; then \
	   echo; echo "── Other ──"; \
	   echo "$$other" | sed -E 's/^([a-zA-Z0-9_-]+):.*## /make \1\t/' | awk -F'\t' '{ printf "  %-24s — %s\n", $$1, $$2 }'; \
	 fi
	@for s in $(SECTIONS); do if [ -f $$s/Makefile ]; then echo; echo "── $$s ──"; $(MAKE) -s -C $$s help; fi; done

all: build

# --- build / test (delegated to CMake) ------------------------------------

build: ## [daily] Configure and build the test binary (CMake)
	$(CMAKE) -S . -B $(BUILD) $(CMAKE_CXX) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD) --parallel $(JOBS)

# --- tests/ (ctest[via `test`]/sanitize/coverage toolchain/tsan smokes) ---------
#
# Live in tests/Makefile -- these names stay invocable
# at the root via thin delegations below (the CI invariant: ci.yml's coverage job runs
# coverage-check at l.274, its fuzz job runs tsan/tsan-core at l.134/138, docs.yml runs
# coverage-html at l.47, and full-local-gate below runs sanitize/coverage-check as its
# last 2 of 22 steps; the cross-repo invariant --
# ../sciforge/.github/workflows/ecosystem.yml:58 runs `make -C real-regex test`).
# `build` (the CMake config, `-S .`-rooted) stays HERE, unmigrated: `test`'s own
# delegation keeps `build` as its prerequisite, then hands off just the ctest run.
# COV_CXX/PROFDATA/LLVM_COV/COV_DIR/CTEST moved into tests/Makefile (tests-only, nothing
# else at root consumed them); COV_FLOOR/COV_FLOOR_IGNORE promoted to mk/common.mk
# instead -- this file's own full-local-gate step 22 echoes $(COV_FLOOR) directly below
# (see mk/common.mk's own comment for why it could not stay tests/Makefile-only).
# `coverage-build` has no delegation here (matching benchmarks/Makefile's own
# profile-sample-build precedent): nothing outside tests/Makefile invokes it by name.
#
# BUILD=$(abspath $(BUILD)) on every delegation below: a bare command-line override
# (e.g. full-local-gate step 21's `$(MAKE) test CXX=$(GXX) BUILD=$(BUILD)/gcc`, a
# RELATIVE "build/gcc") is forwarded to the `-C tests` sub-make verbatim via MAKEFLAGS
# -- command-line-set variables cannot be reset by mk/common.mk's `BUILD ?= $(ROOT)/build`
# (command-line origin beats any in-makefile assignment) -- and the sub-make's own CWD is
# tests/, not $(ROOT), so an unresolved relative override would silently resolve against
# the WRONG directory (tests/build/gcc instead of $(ROOT)/build/gcc, confirmed empirically
# with `make -n test CXX=g++-14 BUILD=build/gcc` before this fix). Re-passing it explicitly,
# already $(abspath)-resolved against ROOT's own CURDIR (evaluated here, before the `-C`
# changes directory), fixes it regardless of whether BUILD was overridden -- a no-op in the
# common case (abspath(build) == $(ROOT)/build either way).
test: build
	@$(MAKE) -C tests test BUILD=$(abspath $(BUILD))

sanitize:
	@$(MAKE) -C tests sanitize BUILD=$(abspath $(BUILD))

coverage:
	@$(MAKE) -C tests coverage BUILD=$(abspath $(BUILD))

coverage-check:
	@$(MAKE) -C tests coverage-check BUILD=$(abspath $(BUILD))

coverage-html:
	@$(MAKE) -C tests coverage-html BUILD=$(abspath $(BUILD))

tsan:
	@$(MAKE) -C tests tsan BUILD=$(abspath $(BUILD))

tsan-core:
	@$(MAKE) -C tests tsan-core BUILD=$(abspath $(BUILD))

# --- tools/ (lint clang-tidy, MISRA, uncrustify format, header layering, C ABI golden) ---
#
# Live in tools/Makefile -- lint/misra/format/
# format-check/check-layers/check-capi-abi all live there now; these 6 names stay
# invocable at the root (the CI invariant: ci.yml's preflight job runs format-check
# with a SCIFORGE_LINT override (l.52), check-layers (l.56), check-capi-abi (l.62);
# the SciForge reusable spine lint-cpp.yml runs this repo's own `make format-check` +
# `make misra` against the shared lint/ config; full-local-gate below runs
# format-check/check-layers/check-capi-abi as steps 1/3/5 and lint/misra further down)
# via thin delegations. SCIFORGE_LINT/CXXSTD/INCLUDES/FORMAT_FILES stay defined via
# mk/common.mk (`?=`) / tools/Makefile-local now -- nothing else at root consumes
# FORMAT_FILES. See tools/Makefile's own header for the INCLUDES-must-stay-relative
# rationale (an absolute -I floods clang-tidy's header diagnostics -- measured). So
# format/format-check (near the docs/ section), check-capi-abi (near fuzz-re2) and
# check-layers (near full-local-gate) each keep their original root position below
# rather than being reshuffled here.
lint:
	@$(MAKE) -C tools lint

misra:
	@$(MAKE) -C tools misra

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

# --- fuzz/ (libFuzzer robustness + differential fuzzers + exhaustive/Fowler conformance) ---
#
# Live in fuzz/Makefile -- these 5 names stay
# invocable at the root (the CI invariant: ci.yml's fuzz job runs fuzz/fuzz-compat/
# fuzz-re2 at l.120/126/145, the conformance job runs exhaustive-compat at l.178, and
# full-local-gate below runs fowler-compat/exhaustive-compat as steps 13-14/22) via
# thin delegations. tsan/tsan-core live in tests/Makefile (their sources live there);
# their root delegations sit with the rest of the tests/ ones, above (near `build`).
# FUZZ_TIME/FUZZ_DIR/EC_K/EC_N moved into fuzz/Makefile (fuzz-only, nothing else at root
# consumes them).
fuzz:
	@$(MAKE) -C fuzz fuzz

fuzz-compat:
	@$(MAKE) -C fuzz fuzz-compat

fuzz-re2:
	@$(MAKE) -C fuzz fuzz-re2

# Lives in tools/Makefile -- see the "--- tools/ ---" comment above for the
# CI-invariant rationale. Golden GENERATED from bindings/c/real_capi.h, never hand-edited:
#   python3 tools/gen_capi_abi_golden.py --inject-enum REAL_ERR_SYNTAX=99 --stdout > tests/bindings/capi_abi_golden.txt
#   make check-capi-abi   # must exit non-zero; then: python3 tools/gen_capi_abi_golden.py
# Enum/flag value pins live in tests/bindings/test_capi_abi.cpp (real::flags cross-check).
check-capi-abi:
	@$(MAKE) -C tools check-capi-abi

# Features-probe .inc drift vs docs/site/data/features.yaml. Thin
# delegation, same shape as check-capi-abi just above — see tools/Makefile's own
# check-features-probe for the can-fail proof and the paths-ignore rationale (why this
# also needs its own root-invocable name rather than living only inside docs-site-gate).
check-features-probe:
	@$(MAKE) -C tools check-features-probe

# --- docs/ (Doxygen + Sphinx/Breathe site) ---------------------------------
#
# Live in docs/Makefile -- these
# names stay invocable at the root (the CI invariant: docs.yml/docs-site.yml/ci.yml
# invoke them as `make -C real-regex <name>`) via thin delegations below.
# `coverage-html` delegates to tests/Makefile (COV_DIR lives there too);
# `doc` and `docs-site-gate` still orchestrate it from here, root-first, before
# delegating to docs/ -- see docs/Makefile's own `doc`/`docs-site-gate` comments for the
# other half of each recipe (paths there are ré-ancrées $(ROOT) so they run identically
# from either place).
#
# SPHINXBUILD stays defined here too (not just in docs/Makefile): full-local-gate's own
# "is sphinx-build on PATH" guard (below) reads $(SPHINXBUILD) directly, so this
# repo-level default must exist independently of docs/Makefile's own copy.
SPHINXBUILD ?= sphinx-build

doc: coverage-html
	@$(MAKE) -C docs doc

doc-no-coverage:
	@$(MAKE) -C docs doc-no-coverage

# python-build first: conf.py autodocs the real package (reference/python), so the
# abi3 .so must exist before sphinx imports it.
docs-site: python-build
	@$(MAKE) -C docs docs-site

docs-site-gate: python-build
	@$(MAKE) coverage-html
	@$(MAKE) -C docs docs-site-gate

# Lives in tools/Makefile -- see the "--- tools/ ---" comment above (ci.yml's
# preflight passes SCIFORGE_LINT on the command line to format-check; it propagates
# through MAKEFLAGS to this delegation's `-C tools` sub-make unchanged).
format:
	@$(MAKE) -C tools format

format-check:
	@$(MAKE) -C tools format-check

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
version-check: ## [gates] Assert pyproject = __init__ = CMake-derived version
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
# exhaustive-compat/fowler-compat live in fuzz/Makefile, EC_K/EC_N with them
# (fuzz-only). Thin delegations preserve both names at
# the root: the conformance CI job calls exhaustive-compat directly (ci.yml l.178),
# full-local-gate calls both below as steps 13-14/22.
exhaustive-compat:
	@$(MAKE) -C fuzz exhaustive-compat

fowler-compat:
	@$(MAKE) -C fuzz fowler-compat

# Pin-drift lint: fail if this repo's workflows pin more than one SciForge version (the shared
# tools/check-pins.sh, owned by SciForge). Skipped with a warning when the sibling tool is absent.
check-pins: ## [gates] Pin-drift lint: fail if workflows pin more than one SciForge version
	@if test -x $(SCIFORGE_TOOLS)/check-pins.sh; then $(SCIFORGE_TOOLS)/check-pins.sh .; \
	 else echo "check-pins: WARN — $(SCIFORGE_TOOLS)/check-pins.sh absent, skipped (CI covers it)"; fi


# Lives in tools/Makefile -- see the "--- tools/ ---" comment above.
check-layers:
	@$(MAKE) -C tools check-layers

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
#     README + docs/release-notes/*.md, docs/BENCHMARKS, CHANGELOG.md |   |
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

gate-bump: ## [gates] Calibrated gate for a version bump only (version-check + build)
	@set -euo pipefail; \
	 files="$$(git diff --name-only $(GATE_BASE) -- .)"; \
	 bad="$$(printf '%s\n' "$$files" | grep -vE '^(include/real/version\.hpp|pyproject\.toml|bindings/python/real/__init__\.py|bindings/rust/Cargo\.toml|CITATION\.cff|README\.md|docs/release-notes/.*\.md|docs/BENCHMARKS\.md|CHANGELOG\.md)$$' | grep -v '^$$' || true)"; \
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

gate-doc: ## [gates] Calibrated gate for a doc-only change (doc-check/format-check as needed)
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

gate-test: ## [gates] Calibrated gate for a tests/-only change (test + sanitize + coverage-check)
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

full-local-gate: ## [gates] Every pass/fail gate in one command (the macOS gate of record)
	@echo "full-local-gate: start (fail-fast — cheap first, first red stops the train)"
	@echo "── [1/22] format-check"
	@$(MAKE) format-check
	@echo "── [2/22] version-check"
	@$(MAKE) version-check
	@echo "── [3/22] check-layers"
	@$(MAKE) check-layers
	@echo "── [4/22] check-pins"
	@$(MAKE) check-pins
	@echo "── [5/22] check-capi-abi (C ABI golden vs real_capi.h)"
	@$(MAKE) check-capi-abi
	@echo "── [6/22] doc-no-coverage (Doxygen WARN_AS_ERROR — fast, high signal)"
	@$(MAKE) doc-no-coverage
	@echo "── [7/22] doc-check (CI-pinned Doxygen when Docker is available)"
	@$(MAKE) doc-check
	# docs/site's own net (-W --keep-going + linkcheck). Same shape as the
	# GXX/go legs below: skipped with a warning when sphinx-build is absent (a dev
	# without the docs venv on PATH doesn't need to rougir tout le gate) -- the docs-site
	# CI job (ci.yml) is the backstop, so this is never the ONLY net on the site.
	@echo "── [8/22] docs-site-gate (sphinx -W --keep-going + linkcheck; skipped if sphinx-build absent)"
	@if command -v $(SPHINXBUILD) >/dev/null 2>&1; then $(MAKE) docs-site-gate; else echo "full-local-gate: WARN — $(SPHINXBUILD) absent, docs-site-gate skipped (CI covers it)"; fi
	@echo "── [9/22] misra (single synthetic TU)"
	@$(MAKE) misra
	@echo "── [10/22] c-test"
	@$(MAKE) c-test
	# examples/cpp/*.cpp direct compile+run -- unconditional, not
	# skip-if-absent: unlike the OPTIONAL alternate-compiler/toolchain legs below (GXX, go,
	# sphinx-build), a default C++ compiler is already a hard prerequisite of this entire gate
	# (build/test/misra/c-test above assume one unconditionally), so example-check rides the
	# same assumption instead of the "warn and skip" shape reserved for genuinely optional tools.
	@echo "── [11/22] example-check (examples/cpp/*.cpp direct compile+run)"
	@$(MAKE) example-check
	@echo "── [12/22] matrix-gate"
	@$(MAKE) matrix-gate
	@echo "── [13/22] fowler-compat"
	@$(MAKE) fowler-compat
	@echo "── [14/22] exhaustive-compat"
	@$(MAKE) exhaustive-compat
	@echo "── [15/22] test (default CXX)"
	@$(MAKE) test
	@echo "── [16/22] rust-test"
	@$(MAKE) rust-test
	@echo "── [17/22] rust-publish-check"
	@$(MAKE) rust-publish-check
	@echo "── [18/22] python-test + python-stubtest (.pyi ≡ runtime; skipped if mypy absent)"
	@$(MAKE) python-test
	@if $${MYPY_PYTHON:-$(PYTHON)} -c "import mypy" >/dev/null 2>&1; then \
	   $(MAKE) python-stubtest; \
	 else echo "   (stubtest skipped: mypy absent — pip install mypy, or set MYPY_PYTHON)"; fi
	# Go leg: go-check-vendor regenerates the committed vendor tree from the live headers and fails
	# on drift — the one binding NOT otherwise in this gate (its sources are vendored, not built from
	# include/ here). Skipped with a warning when go is absent (the CI go job is the backstop), same
	# shape as the GCC leg. Closes the gap where an include/ change that stales vendor_include/ was
	# caught only in CI.
	@echo "── [19/22] go-check-vendor + go-test (Go leg; skipped if go absent)"
	@if command -v go >/dev/null 2>&1; then $(MAKE) go-check-vendor && $(MAKE) go-test; else echo "full-local-gate: WARN — go absent, Go leg skipped (CI covers it)"; fi
	@echo "── [20/22] lint"
	@set -euo pipefail; \
	  mkdir -p $(BUILD); \
	  $(MAKE) lint 2>&1 | tee $(BUILD)/lint.log; \
	  if grep -qE 'warning:|error:' $(BUILD)/lint.log; then \
	    echo "full-local-gate: FAIL at lint (see $(BUILD)/lint.log)"; exit 1; \
	  fi
	@echo "── [21/22] test (GCC leg) + sanitize (slowest last)"
	@if command -v $(GXX) >/dev/null 2>&1; then $(MAKE) test CXX=$(GXX) BUILD=$(BUILD)/gcc; else echo "full-local-gate: WARN — $(GXX) absent, GCC leg skipped (CI covers it)"; fi
	@$(MAKE) sanitize
	@echo "── [22/22] coverage-check (line floor $(COV_FLOOR)% — closes the P0 gate hole)"
	@$(MAKE) coverage-check
	@echo "full-local-gate: ALL gates green (cheap→doc→tests→lint→sanitize→coverage; first red would have stopped the train)"

# doc-check lives in docs/Makefile; thin delegation
# below preserves the root-invocable name (full-local-gate step 7/22 above, and the
# CI invariant, both call it by this name).
doc-check:
	@$(MAKE) -C docs doc-check

# --- benchmarks/ (throughput/duel/matrix/profile — dev-only; matrix-gate is the one CI-relevant gate) ---
#
# Live in benchmarks/Makefile -- these names stay
# invocable at the root (matrix-gate is full-local-gate's own step 12/22 below; the
# rest are developer-invoked directly, never from CI) via thin delegations below.
# Paths inside benchmarks/Makefile are ré-ancrées $(ROOT) (via mk/common.mk) or run
# under `cd $(ROOT) &&` per recipe line -- see that file's own header for why
# (callgrind_runs.sh reads a bare-relative "build/profile/corpora", so it needs
# CWD=$(ROOT) regardless of where `make` was invoked from). The internal
# profile-sample-build helper has no root delegation (nothing outside benchmarks/
# invokes it by name); see benchmarks/Makefile.
bench-duel:
	@$(MAKE) -C benchmarks bench-duel

profile-sample:
	@$(MAKE) -C benchmarks profile-sample

profile-callgrind:
	@$(MAKE) -C benchmarks profile-callgrind

bench-matrix:
	@$(MAKE) -C benchmarks bench-matrix

matrix-gate:
	@$(MAKE) -C benchmarks matrix-gate

bench-engines:
	@$(MAKE) -C benchmarks bench-engines

bench-multipattern:
	@$(MAKE) -C benchmarks bench-multipattern

# The light C++ net for examples/cpp/*.cpp: compile+run DIRECTLY against the source tree
# (-Iinclude, no install step) so a showcase example that stops compiling/running reds here
# fast, before the slower install-smoke cycle. Complementary, not redundant, with
# install-smoke's step (e), which proves the same files against the INSTALLED package
# (find_package) — one net catches a header regression early, the other catches a packaging
# regression. CXX is honored (run under both clang and g++ in ci.yml's install-smoke job,
# which already has both) so a compiler-specific regression in a showcase snippet cannot hide.
# The landing quickstart contract: every example
# under examples/cpp/ is now also an injection source for the landing (docs/site/_templates/
# landing.html, conf.py's html-page-context hook), so a red here means the page's
# own code sample stopped working.
example-check: ## [nets] Compile + run every examples/cpp/*.cpp directly against include/ (CXX honored)
	@set -e; \
	 cxx="$${CXX:-c++}"; \
	 mkdir -p $(BUILD)/examples; \
	 n=0; \
	 for src in examples/cpp/*.cpp; do \
	   name=$$(basename "$$src" .cpp); \
	   echo "example-check: $$cxx -std=c++20 -Iinclude $$src"; \
	   $$cxx -std=c++20 $(INCLUDES) "$$src" -o "$(BUILD)/examples/$$name"; \
	   "$(BUILD)/examples/$$name"; \
	   n=$$((n + 1)); \
	 done; \
	 echo "example-check: OK ($$n example(s) compiled + run with $$cxx)"

# Proves the *system* install end to end, the exact packager path: install REAL to a temp prefix
# with -DBUILD_TESTING=OFF (noarch LIBDIR=lib, no SciForge — the library stands alone), then
# consume it the three supported C++ ways plus a negative check that the C++20 guard fires. CXX is
# honored (run under clang and g++). Used by the install-smoke CI job.
install-smoke: ## [release] System install end to end: find_package + pkg-config + direct-copy + C++20 guard
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
	 "$$work/ex/hello"; "$$work/ex/redos_demo"; "$$work/ex/quickstart"; echo "      examples: OK"; \
	 echo "install-smoke: OK (find_package + pkg-config + direct-copy + negative guard + examples)"

# Installs the package from the repository root (root pyproject.toml builds the
# abi3 extension against include/). uninstall removes it by distribution name.
install: ## [release] Install the Python package (pip)
	$(PYTHON) -m pip install .

uninstall: ## [release] Uninstall the Python package (pip)
	$(PYTHON) -m pip uninstall -y real-regex

# Cuts the complete calendar release in one invocation. Computes YYYY.M.PATCH with
# the patch reset each month (first release of a month is .0; PEP 440 drops leading
# zeros, so 2026.6.1, never 2026.06.001), bumps every version-checked file
# (pyproject + __init__ + version.hpp + Cargo.toml + CITATION.cff + README GIT_TAG;
# CMakeLists.txt derives from pyproject and follows), re-vendors Go, folds the human
# inputs (docs/release-notes/v<version>.md must exist; BENCHMARKS re-stamp optional),
# commits with the BODY=<file> body, tags the engine AND the co-located decoupled Go
# module (bindings/go/v0.1.<n+1>, so pkg.go.dev never lags the engine), and pushes.
# Pushing the engine tag drives release.yml (wheels/sdist -> PyPI, crate, GitHub
# release, tap bumps, pkg.go.dev nudge). BODY lives OUTSIDE the tree (the clean-tree
# check tolerates only the human inputs). DRY_RUN=1 stops after bump+vendor+stage —
# nothing committed, tagged or pushed; revert with `git reset --hard` (the throwaway
# notes file stays untracked — remove it too).
release: ## [release] Cut the complete calendar release (bump all, tag engine + Go, push) — BODY=<file outside the tree>; DRY_RUN=1 to rehearse
	@test "$$(git symbolic-ref --short HEAD)" = main || { echo "release from main only"; exit 1; }
	@test -n "$(BODY)" || { echo "release: pass BODY=<file> (the commit body, a file OUTSIDE the tree)"; exit 1; }
	@test -f "$(BODY)" || { echo "release: BODY file '$(BODY)' not found"; exit 1; }
	@dirty="$$(git status --porcelain | grep -vE '^.. docs/release-notes/|^.. docs/BENCHMARKS\.md$$' || true)"; \
	 test -z "$$dirty" || { echo "release: tree not clean beyond the human inputs (docs/release-notes/, docs/BENCHMARKS.md):"; echo "$$dirty"; exit 1; }
	@git fetch --tags --quiet origin
	@year=$$(date -u +%Y); month=$$(date -u +%m | sed 's/^0//'); \
	 patch=$$(git tag -l "v$$year.$$month.*" | wc -l | tr -d ' '); \
	 version="$$year.$$month.$$patch"; \
	 echo "Releasing v$$version"; \
	 test -f "docs/release-notes/v$$version.md" || { echo "release: docs/release-notes/v$$version.md missing -- write the notes first"; exit 1; }; \
	 sed -i.bak -E "s/^version = \".*\"/version = \"$$version\"/" pyproject.toml && rm -f pyproject.toml.bak; \
	 sed -i.bak -E "s/^__version__ = \".*\"/__version__ = \"$$version\"/" bindings/python/real/__init__.py && rm -f bindings/python/real/__init__.py.bak; \
	 vmaj=$$(echo "$$version" | cut -d. -f1); vmin=$$(echo "$$version" | cut -d. -f2); vpat=$$(echo "$$version" | cut -d. -f3); \
	 sed -i.bak -E "s/^#define REAL_VERSION_MAJOR .*/#define REAL_VERSION_MAJOR $$vmaj/; \
	                s/^#define REAL_VERSION_MINOR .*/#define REAL_VERSION_MINOR $$vmin/; \
	                s/^#define REAL_VERSION_PATCH .*/#define REAL_VERSION_PATCH $$vpat/" include/real/version.hpp && rm -f include/real/version.hpp.bak; \
	 sed -i.bak -E "1,/^version = /s/^version = \".*\"/version = \"$$version\"/" bindings/rust/Cargo.toml && rm -f bindings/rust/Cargo.toml.bak; \
	 sed -i.bak -E "s/^version: \".*\"/version: \"$$version\"/" CITATION.cff && rm -f CITATION.cff.bak; \
	 sed -i.bak -E "s/GIT_TAG v[0-9][0-9.]*/GIT_TAG v$$version/" README.md && rm -f README.md.bak; \
	 $(MAKE) go-vendor; \
	 git add pyproject.toml bindings/python/real/__init__.py include/real/version.hpp \
	         bindings/rust/Cargo.toml CITATION.cff README.md \
	         bindings/go/vendor_include docs/BENCHMARKS.md docs/release-notes/; \
	 if [ "$(DRY_RUN)" = "1" ]; then \
	   echo "release: DRY RUN -- tree bumped+staged for v$$version; STOPPED before commit/tag/push."; \
	   echo "release: verify with 'make version-check go-check-vendor', then revert: git reset --hard (and remove the untracked notes file)"; \
	   exit 0; \
	 fi; \
	 body=$$(mktemp); printf 'release: v%s\n\n' "$$version" > "$$body"; cat "$(BODY)" >> "$$body"; \
	 git commit -F "$$body"; rm -f "$$body"; \
	 git tag "v$$version"; \
	 gp=$$(git tag -l 'bindings/go/v0.1.*' | sed 's|.*/v0\.1\.||' | sort -n | tail -1); gp=$$((gp + 1)); \
	 git tag "bindings/go/v0.1.$$gp"; \
	 git push origin HEAD "v$$version" "bindings/go/v0.1.$$gp"

clean: ## [daily] Remove build artifacts
	rm -rf $(BUILD) bindings/python/build bindings/python/real/*.so bindings/python/*.egg-info *.egg-info dist
