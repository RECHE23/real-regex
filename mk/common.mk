# mk/common.mk — shared build config for section Makefiles (compartimentalisation).
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
