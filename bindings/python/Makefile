# The Python binding (bindings/python). Standardized per-binding targets, invoked from the repo root as
# `make python-<target>` (via the top-level `python-%` alias) or directly with `make -C bindings/python <target>`.
# setup.py lives at the repo root and uses root-relative sources, so `build` runs there; all paths are
# $(ROOT)-anchored (in particular PYTHONPATH — a $(CURDIR) here would be bindings/python, breaking the import).
ROOT             := $(abspath $(CURDIR)/../..)
include $(ROOT)/mk/common.mk
MYPY_PYTHON      ?= $(PYTHON)
SCIFORGE_INCLUDE ?= $(abspath $(ROOT)/../sciforge/include)
SCIFORGE_PYTHON  ?= $(abspath $(ROOT)/../sciforge/python)
PYRUN            := PYTHONPATH=$(ROOT)/bindings/python:$(SCIFORGE_PYTHON) $(PYTHON)

.PHONY: build test stubtest syntax bench fuzz clean help
.DEFAULT_GOAL := help

# Compile every bindings/python/*.py at the floor declared by requires-python.
# A SyntaxError valid on the writer's 3.14 and invalid on CI's floor reached
# GitHub before any test ran. N files compiled; N == 0 is a fail. No floor
# interpreter is a named skip, not a green.
syntax: ## Syntax-check bindings/python at the requires-python floor
	@set -e; \
	 cd $(ROOT); \
	 min=$$(sed -nE 's/^requires-python = ">=([0-9]+)\.([0-9]+)"/\1.\2/p' pyproject.toml); \
	 if [ -z "$$min" ]; then \
	   echo "python-syntax: FAIL -- could not parse requires-python from pyproject.toml"; \
	   exit 1; \
	 fi; \
	 maj=$${min%.*}; \
	 mino=$${min#*.}; \
	 py=""; \
	 for c in python$$min python$$maj.$$mino $(PYTHON); do \
	   command -v $$c >/dev/null 2>&1 || continue; \
	   if $$c -c "import sys; raise SystemExit(0 if sys.version_info[:2] == (int('$$maj'), int('$$mino')) else 1)"; then \
	     py=$$c; \
	     break; \
	   fi; \
	 done; \
	 if [ -z "$$py" ]; then \
	   mkdir -p $(dir $(GATE_SKIPS)); \
	   echo "python-syntax: SKIPPED -- no interpreter at $$min (requires-python >= $$min)" \
	     | tee -a $(GATE_SKIPS); \
	   exit 0; \
	 fi; \
	 files=$$(find bindings/python -name '*.py' ! -path '*/__pycache__/*' ! -path '*.egg-info/*' 2>/dev/null || true); \
	 if [ -z "$$files" ]; then \
	   echo "python-syntax: FAIL -- 0 files under bindings/python/"; \
	   exit 1; \
	 fi; \
	 n=0; \
	 for f in $$files; do \
	   $$py -m py_compile "$$f" || { echo "python-syntax: FAIL -- $$f"; exit 1; }; \
	   n=$$((n + 1)); \
	 done; \
	 echo "python-syntax: $$n files compiled ($$py, floor $$min)"

build: ## Build the abi3 Python extension in place
	cd $(ROOT) && SCIFORGE_INCLUDE=$(SCIFORGE_INCLUDE) $(PYTHON) setup.py -q build_ext --inplace --force

test: build ## Run the Python test suites (unittest — includes the re-vs-REAL differential fuzzer)
	cd $(ROOT) && $(PYRUN) -m unittest discover -s bindings/python/tests

stubtest: build ## Verify real/__init__.pyi against the runtime surface (mypy.stubtest; MYPY_PYTHON carries mypy)
	cd $(ROOT) && PYTHONPATH=$(ROOT)/bindings/python:$(SCIFORGE_PYTHON) $(MYPY_PYTHON) -m mypy.stubtest real --allowlist bindings/python/tests/stubtest_allowlist.txt

bench: build ## Comparative benchmarks vs Python re (bench.py, then the fuzzed-corpus variant)
	$(PYRUN) $(ROOT)/benchmarks/bench.py
	$(PYRUN) $(ROOT)/benchmarks/fuzz_bench.py

fuzz: ## Where fuzzing lives for this binding
	@echo "python: the re-vs-REAL differential fuzzer runs under 'make python-test' (test_differential_fuzz.py);"
	@echo "        the engine libFuzzer targets are 'make fuzz' / 'make fuzz-compat' at the repo root."

clean: ## Remove the built extension and its stamp
	rm -f $(ROOT)/bindings/python/real/*.so $(BUILD)/py_ext.stamp

help: ## Show these targets
	@echo "bindings/python — make python-<target> from the root (or make -C bindings/python <target>):"
	@grep -hE '^[a-z-]+:.*##' $(firstword $(MAKEFILE_LIST)) | sort | sed -E 's/^([a-z-]+):.*## /  python-\1\t/'
