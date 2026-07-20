# The Python binding (bindings/python). Standardized per-binding targets, invoked from the repo root as
# `make python-<target>` (via the top-level `python-%` alias) or directly with `make -C bindings/python <target>`.
# setup.py lives at the repo root and uses root-relative sources, so `build` runs there; all paths are
# $(ROOT)-anchored (in particular PYTHONPATH — a $(CURDIR) here would be bindings/python, breaking the import).
ROOT             := $(abspath $(CURDIR)/../..)
PYTHON           ?= python3
MYPY_PYTHON      ?= $(PYTHON)
BUILD            ?= $(ROOT)/build
SCIFORGE_INCLUDE ?= $(abspath $(ROOT)/../sciforge/include)
SCIFORGE_PYTHON  ?= $(abspath $(ROOT)/../sciforge/python)
PYRUN            := PYTHONPATH=$(ROOT)/bindings/python:$(SCIFORGE_PYTHON) $(PYTHON)

.PHONY: build test stubtest bench fuzz clean help
.DEFAULT_GOAL := help

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
