# mk/common.mk — shared build config for section Makefiles.
#
# Usage from a section Makefile (e.g. docs/Makefile): define ROOT FIRST, then include
# this file --
#   ROOT := $(abspath $(CURDIR)/..)
#   include $(ROOT)/mk/common.mk
#
# Minimal on purpose: only what the docs/ section (the pilot) needs today. Extend as
# later sections migrate (tests/, fuzz/, tools/, benchmarks/) -- do not pre-fill for
# hypothetical consumers.

BUILD  ?= $(ROOT)/build
PYTHON ?= python3

# Where an optional gate leg records that it SKIPPED. $(ROOT)-anchored and NOT $(BUILD)-relative on
# purpose: full-local-gate's GCC leg re-enters make with BUILD=$(BUILD)/gcc, so a $(BUILD)-relative
# path would send that leg's skips to a file the summary never reads. Truncated once at the start of
# a gate run; appended by the branch that skips (via `tee -a`), so the end-of-run summary cannot
# drift from the conditions it reports -- the alternative, re-probing each tool in the summary, is a
# second copy of every condition and would go stale exactly the way §A's prose did.
GATE_SKIPS := $(ROOT)/build/.gate-skips

# The gate's optional python tools, taken from `make gate-venv`'s venv when it exists so a developer
# who ran that command stops silently skipping two steps. Plain `?=` still wins: an explicit
# SPHINXBUILD=... or MYPY_PYTHON=... on the command line or in the environment overrides both.
GATE_VENV        ?= $(ROOT)/.venv-gate
SPHINXBUILD      ?= $(if $(wildcard $(GATE_VENV)/bin/sphinx-build),$(GATE_VENV)/bin/sphinx-build,sphinx-build)
MYPY_PYTHON      ?= $(if $(wildcard $(GATE_VENV)/bin/python),$(GATE_VENV)/bin/python,$(PYTHON))

# EXPORTED, because the delegation that consumes them cannot carry them. `python-%: $(MAKE) -C
# bindings/python $*` is one generic rule for four bindings, so it has no place to pass MYPY_PYTHON,
# and a plain make variable does not cross into a sub-make. Without this the root resolves the venv,
# announces that step 20 will run, and the sub-make then invokes bare `python3 -m mypy.stubtest` and
# FAILS -- worse than the skip it replaced. Both section Makefiles read them with `?=`, so an
# environment or command-line value still wins.
export MYPY_PYTHON
export SPHINXBUILD
# Parallelism: detected core count (override with JOBS=N). Same detection as the root
# Makefile's own JOBS.
JOBS   ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# SciForge sibling paths, anchored at $(ROOT) -- NOT CWD-relative. The root Makefile's
# own defaults (../sciforge/...) only resolve correctly when make runs from the repo
# root; from a section directory (e.g. docs/), a CWD-relative "../sciforge" would
# resolve to real-regex/sciforge, which does not exist. SciForge is never a
# build/runtime dependency of this repo (its absence is tolerated everywhere --
# check-pins/doc-check WARN-and-skip), so these stay optional (?=) overridable
# defaults, never a hard include.
SCIFORGE_INCLUDE ?= $(ROOT)/../sciforge/include
SCIFORGE_LINT    ?= $(ROOT)/../sciforge/lint
SCIFORGE_TOOLS   ?= $(ROOT)/../sciforge/tools
SCIFORGE_PYTHON  ?= $(ROOT)/../sciforge/python

# Run Python against the in-place build under bindings/python/, ahead of any installed
# copy, with the SciForge sibling appended so a bench can `import sciforge.bench` (the
# shared dep-free stats/schema substrate) -- $(ROOT)-anchored so a section Makefile
# (e.g. benchmarks/) gets the right PYTHONPATH regardless of the invoking CWD. The root
# Makefile keeps its own $(CURDIR)-relative PYRUN (Makefile:27) rather than including
# this file wholesale: CURDIR == ROOT there by construction (the root Makefile IS
# $(ROOT)), so the two stay identical in value; this is the ROOT-anchored twin for
# consumers below the root (benchmarks/Makefile's bench-engines is the first).
PYRUN := PYTHONPATH=$(ROOT)/bindings/python:$(abspath $(SCIFORGE_PYTHON)) $(PYTHON)

# Coverage floor -- the CI gate `tests/coverage-check` enforces. Promoted here rather
# than kept tests/Makefile-local because the ROOT
# Makefile's own `full-local-gate` (step 22/22, a root recipe, not a delegation) echoes
# $(COV_FLOOR) directly in its own progress line -- if COV_FLOOR lived only in
# tests/Makefile, that root echo would silently print an EMPTY value (make does not
# error on an unset variable inside a string). COV_FLOOR_IGNORE travels with it (same
# sole consumer, tests/coverage-check). `?=`, the style of this file: an override stays
# possible without editing it.
COV_FLOOR        ?= 95.0
COV_FLOOR_IGNORE ?= bindings/c|include/real/engine/simd.hpp|include/real/engine/cpclass_gcc

# C++ standard + engine header search path, shared by every section that compiles
# REAL's own headers directly.
#
# INCLUDES stays the RELATIVE `-Iinclude` on purpose -- the spelling is LOAD-BEARING
# for `make lint`. .clang-tidy's `HeaderFilterRegex: '.*/(include/real|tests)/.*'`
# requires a path component BEFORE `include/real`: with the relative spelling, header
# paths read `include/real/...` (no leading component -> no match -> header
# diagnostics suppressed, lint scopes to the test TUs -- the historical, always-green
# behavior). An absolute `-I$(ROOT)/include` makes every header path match and floods
# the lint log (measured A/B on one TU: 0 vs 24409 warnings), which reds
# full-local-gate's lint step. The relative form is safe because EVERY consumer runs
# from ROOT: the root Makefile's lint/misra (CURDIR == ROOT) and the section recipes
# via their `cd $(ROOT) &&` idiom (tests/' tsan lines, fuzz/ and benchmarks/' own
# local `:=` copies which shadow this one harmlessly under `?=`).
CXXSTD   ?= -std=c++20
INCLUDES ?= -Iinclude

# Forward CMAKE_CXX_COMPILER only when CXX is set on the command line; otherwise CMake
# selects the platform default. Promoted alongside CXXSTD/INCLUDES: tests/Makefile's
# `sanitize` and `coverage-build` are CMake-driving
# consumers, same as the root Makefile's own `build`.
ifeq ($(origin CXX),command line)
CMAKE_CXX := -DCMAKE_CXX_COMPILER=$(CXX)
endif
