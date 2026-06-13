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

# Forward CMAKE_CXX_COMPILER only when CXX is set on the command line;
# otherwise CMake selects the platform default.
ifeq ($(origin CXX),command line)
CMAKE_CXX := -DCMAKE_CXX_COMPILER=$(CXX)
endif

CXXSTD   := -std=c++20
INCLUDES := -Iinclude

.PHONY: all build test sanitize coverage lint misra fuzz doc format clean \
        python python-test bench-python bench-fuzz install uninstall help

.DEFAULT_GOAL := help

help:
	@echo "REAL — build orchestrator (CMake + QA tools)"
	@echo ""
	@echo "  make build      Configure and build the test binary (CMake)"
	@echo "  make test       Build and run the test suite (ctest)"
	@echo "  make sanitize   Build and run the tests under ASan + UBSan"
	@echo "  make coverage   Line-coverage report (Clang/LLVM)"
	@echo "  make lint       clang-tidy over the test sources"
	@echo "  make misra      MISRA C++:2023-oriented analysis"
	@echo "  make fuzz       libFuzzer robustness fuzzing (Clang; FUZZ_TIME=secs)"
	@echo "  make doc        Generate the API reference (Doxygen)"
	@echo "  make format     clang-format, in place"
	@echo "  make clean      Remove build artifacts"
	@echo ""
	@echo "  make python       Build the abi3 Python extension in place"
	@echo "  make python-test  Run the Python test suites"
	@echo "  make bench-python Comparative benchmark vs Python re"
	@echo "  make bench-fuzz   Randomized comparative benchmark over fuzzed input"
	@echo "  make install      Install the Python package (pip)"
	@echo "  make uninstall    Uninstall the Python package (pip)"
	@echo "  make release      Cut a calendar-versioned release (tag + push)"
	@echo ""
	@echo "  Override the compiler: make test CXX=g++-14"

all: build

# --- build / test (delegated to CMake) ------------------------------------

build:
	$(CMAKE) -S . -B $(BUILD) $(CMAKE_CXX) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD) -j

test: build
	$(CTEST) --test-dir $(BUILD) --output-on-failure

sanitize:
	$(CMAKE) -S . -B $(BUILD)/sanitize $(CMAKE_CXX) -DREAL_SANITIZE=ON
	$(CMAKE) --build $(BUILD)/sanitize -j
	$(CTEST) --test-dir $(BUILD)/sanitize --output-on-failure

# Coverage uses LLVM source-based instrumentation, so it pins a Clang
# toolchain end to end. On macOS the Apple toolchain is required: Homebrew
# clang links a profile runtime whose .profraw the Homebrew llvm-profdata
# cannot read. Override COV_CXX / PROFDATA / LLVM_COV on other platforms.
COV_CXX  ?= /usr/bin/clang++
PROFDATA ?= xcrun llvm-profdata
LLVM_COV ?= xcrun llvm-cov
COV_DIR  := $(BUILD)/coverage

coverage:
	$(CMAKE) -S . -B $(COV_DIR) -DREAL_COVERAGE=ON -DCMAKE_CXX_COMPILER=$(COV_CXX)
	$(CMAKE) --build $(COV_DIR) -j
	LLVM_PROFILE_FILE=$(COV_DIR)/tests.profraw $(COV_DIR)/real_tests_bin
	$(PROFDATA) merge -sparse $(COV_DIR)/tests.profraw -o $(COV_DIR)/tests.profdata
	$(LLVM_COV) report $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata

# --- QA tools (wrappers; no compilation policy here) ----------------------

lint:
	clang-tidy $(wildcard tests/*.cpp) -- $(CXXSTD) $(INCLUDES)

# Analyzes the library through a one-line translation unit; the line filter
# restricts diagnostics to the included headers.
misra:
	mkdir -p $(BUILD)
	printf '#include <real/real.hpp>\nint main(){ const real::regex r("a"); return r.search("a") ? 0 : 1; }\n' > $(BUILD)/misra_tu.cpp
	clang-tidy --config-file=.clang-tidy-misra \
	    --line-filter='[{"name":"misra_tu.cpp","lines":[[1,1]]}]' \
	    $(BUILD)/misra_tu.cpp -- $(CXXSTD) $(INCLUDES)

# libFuzzer is a Clang feature; this target always uses Clang regardless of CXX.
# FUZZ_TIME bounds a local run (CI uses a short smoke run); point the corpus at
# build/fuzz/corpus to accumulate findings across runs.
FUZZ_TIME ?= 30
FUZZ_DIR  := $(BUILD)/fuzz

fuzz:
	mkdir -p $(FUZZ_DIR)/corpus
	clang++ $(CXXSTD) -O1 -g $(INCLUDES) \
	    -fsanitize=fuzzer,address,undefined fuzz/fuzz_target.cpp -o $(FUZZ_DIR)/fuzz_target
	$(FUZZ_DIR)/fuzz_target -max_total_time=$(FUZZ_TIME) -timeout=10 \
	    $(FUZZ_DIR)/corpus fuzz/corpus

doc:
	mkdir -p $(BUILD)/doc
	doxygen Doxyfile
	@echo "API reference: $(BUILD)/doc/html/index.html"

format:
	clang-format -i $(shell find include tests -name '*.hpp' -o -name '*.cpp')

# --- Python binding -------------------------------------------------------

# Builds the abi3 extension in place against include/. Packaging lives in the
# root pyproject.toml / setup.py.
python:
	$(PYTHON) setup.py -q build_ext --inplace

python-test: python
	cd python && $(PYTHON) -m unittest discover -s tests

bench-python: python
	$(PYTHON) benchmarks/bench.py

bench-fuzz: python
	$(PYTHON) benchmarks/fuzz_bench.py

# Installs the package from the repository root (root pyproject.toml builds the
# abi3 extension against include/). uninstall removes it by distribution name.
install:
	$(PYTHON) -m pip install .

uninstall:
	$(PYTHON) -m pip uninstall -y real-regex

# Cuts a calendar-versioned release. Computes YYYY.M.PATCH with the patch reset
# each month (first release of a month is .0; PEP 440 drops leading zeros, so
# 2026.6.1, never 2026.06.001), bumps both version files, commits, tags and
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
	 sed -i.bak -E "s/^__version__ = \".*\"/__version__ = \"$$version\"/" python/real/__init__.py && rm -f python/real/__init__.py.bak; \
	 git add pyproject.toml python/real/__init__.py; \
	 git commit -m "release: v$$version"; \
	 git tag "v$$version"; \
	 git push origin HEAD "v$$version"

clean:
	rm -rf $(BUILD) python/build python/real/*.so python/*.egg-info *.egg-info dist
