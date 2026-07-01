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
# Parallelism: detected core count (override with JOBS=N).
JOBS   ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# SciForge owns the shared, dev-only benchmark substrate (sciforge.bench: dep-free stats,
# schema, collector), in its python/ dir. Same sibling default as SCIFORGE_INCLUDE/_LINT;
# CI checks SciForge out alongside as ../sciforge. Never a build/runtime dependency — the
# benches import it only when run (make bench-*), and it is never shipped in the wheel.
SCIFORGE_PYTHON ?= ../sciforge/python

# Run Python against the in-place build under python/, ahead of any installed
# copy (PYTHONPATH precedes site-packages, so an editable install elsewhere
# cannot shadow the freshly built extension). The SciForge sibling is appended so the
# benches can `import sciforge.bench` (the shared stats/schema substrate).
PYRUN := PYTHONPATH=$(CURDIR)/python:$(abspath $(SCIFORGE_PYTHON)) $(PYTHON)

# Forward CMAKE_CXX_COMPILER only when CXX is set on the command line;
# otherwise CMake selects the platform default.
ifeq ($(origin CXX),command line)
CMAKE_CXX := -DCMAKE_CXX_COMPILER=$(CXX)
endif

CXXSTD       := -std=c++20
INCLUDES     := -Iinclude
# The test harness (framework.hpp) is owned by SciForge; the test TUs include it
# as <sciforge/test/framework.hpp>. clang-tidy (make lint) needs that path too.
# Sibling checkout by default — matches the CMake SCIFORGE_INCLUDE_DIR default.
SCIFORGE_INCLUDE ?= ../sciforge/include
# SciForge also owns the shared lint config (the MISRA base + uncrustify.cfg), in
# its lint/ dir. Same sibling default; CI checks SciForge out alongside as ../sciforge.
SCIFORGE_LINT ?= ../sciforge/lint
# unicode_fold.hpp and unicode_props.hpp are generated (their scripts own the layout; the regen tests
# pin them), so they are excluded from the hand-written-code formatter.
FORMAT_FILES := $(shell find include tests -name '*.hpp' -o -name '*.cpp' | grep -vE 'include/real/unicode_(fold|props)\.hpp')

.PHONY: all build test sanitize coverage coverage-build coverage-html coverage-check \
        lint misra fuzz fuzz-compat tsan doc doc-no-coverage format format-check full-local-gate clean \
        python python-test bench-python bench-fuzz bench-engines \
        version-check install install-smoke uninstall release help

.DEFAULT_GOAL := help

help:
	@echo "REAL — build orchestrator (CMake + QA tools)"
	@echo ""
	@echo "  make build      Configure and build the test binary (CMake)"
	@echo "  make test       Build and run the test suite (ctest)"
	@echo "  make sanitize   Build and run the tests under ASan + UBSan"
	@echo "  make coverage   Line-coverage text summary + HTML report"
	@echo "  make lint       clang-tidy over the test sources"
	@echo "  make misra      MISRA C++:2023-oriented analysis"
	@echo "  make fuzz       libFuzzer robustness fuzzing (Clang; FUZZ_TIME=secs)"
	@echo "  make fuzz-compat  Differential fuzz: real::compat vs std::regex (Clang; FUZZ_TIME=secs)"
	@echo "  make tsan       ThreadSanitizer smoke of concurrent std_engine (Clang)"
	@echo "  make doc        Generate API reference (Doxygen) with embedded coverage"
	@echo "  make doc-no-coverage  Generate API reference without coverage report"
	@echo "  make format     Uncrustify, in place"
	@echo "  make format-check  Uncrustify, dry-run, exits non-zero on diff"
	@echo "  make clean      Remove build artifacts"
	@echo ""
	@echo "  make python       Build the abi3 Python extension in place"
	@echo "  make python-test  Run the Python test suites"
	@echo "  make version-check  Assert pyproject = __init__ = CMake-derived version"
	@echo "  make full-local-gate  Every pass/fail gate in one command (the macOS gate of record)"
	@echo "  make bench-python Comparative benchmark vs Python re"
	@echo "  make bench-fuzz   Randomized comparative benchmark over fuzzed input"
	@echo "  make bench-engines  C++ throughput vs std::regex/PCRE2/RE2 (if present)"
	@echo "  make install      Install the Python package (pip)"
	@echo "  make uninstall    Uninstall the Python package (pip)"
	@echo "  make release      Cut a calendar-versioned release (tag + push)"
	@echo ""
	@echo "  Override the compiler: make test CXX=g++-14"

all: build

# --- build / test (delegated to CMake) ------------------------------------

build:
	$(CMAKE) -S . -B $(BUILD) $(CMAKE_CXX) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD) --parallel $(JOBS)

test: build
	$(CTEST) --test-dir $(BUILD) --output-on-failure

sanitize:
	$(CMAKE) -S . -B $(BUILD)/sanitize $(CMAKE_CXX) -DREAL_SANITIZE=ON
	$(CMAKE) --build $(BUILD)/sanitize --parallel $(JOBS)
	$(CTEST) --test-dir $(BUILD)/sanitize --output-on-failure

# Coverage uses LLVM source-based instrumentation, so it pins a Clang
# toolchain end to end. On macOS the Apple toolchain is required: Homebrew
# clang links a profile runtime whose .profraw the Homebrew llvm-profdata
# cannot read. On Linux the bare llvm-profdata/llvm-cov tools work.
ifeq ($(shell uname -s),Darwin)
COV_CXX  ?= /usr/bin/clang++
PROFDATA ?= xcrun llvm-profdata
LLVM_COV ?= xcrun llvm-cov
else
COV_CXX  ?= clang++
PROFDATA ?= llvm-profdata
LLVM_COV ?= llvm-cov
endif
COV_DIR  := $(BUILD)/coverage

# Shared build/run/merge steps used by both the text summary and the HTML report.
coverage-build:
	$(CMAKE) -S . -B $(COV_DIR) -DREAL_COVERAGE=ON -DCMAKE_CXX_COMPILER=$(COV_CXX)
	$(CMAKE) --build $(COV_DIR) --parallel $(JOBS)
	LLVM_PROFILE_FILE=$(COV_DIR)/tests.profraw $(COV_DIR)/real_tests_bin
	$(PROFDATA) merge -sparse $(COV_DIR)/tests.profraw -o $(COV_DIR)/tests.profdata

coverage: coverage-build
	$(LLVM_COV) report $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata
	$(LLVM_COV) show $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata \
	    -format=html -output-dir=$(COV_DIR)/html -show-line-counts-or-regions
	@grep -q "REAL dark-coverage theme" $(COV_DIR)/html/style.css 2>/dev/null || \
	    cat docs/coverage-style.css >> $(COV_DIR)/html/style.css
	@echo "HTML coverage report: $(COV_DIR)/html/index.html"

# Minimum line coverage enforced by `coverage-check` (the CI gate). `make coverage` itself
# stays advisory for local iteration; CI fails the build if a change drops below the floor.
COV_FLOOR := 95.0

coverage-check: coverage-build
	@pct=$$($(LLVM_COV) report $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata \
	        | awk '$$1 == "TOTAL" { gsub(/%/, "", $$10); print $$10 }'); \
	  echo "Line coverage: $$pct% (floor $(COV_FLOOR)%)"; \
	  awk -v p="$$pct" -v f="$(COV_FLOOR)" 'BEGIN { exit !(p + 0 >= f + 0) }' || \
	    { echo "FAIL: line coverage $$pct% is below the $(COV_FLOOR)% floor"; exit 1; }

# Silent variant used by make doc: keeps the terminal focused on the doc output.
coverage-html:
	@mkdir -p $(COV_DIR)
	@$(MAKE) --silent coverage-build > $(COV_DIR)/build.log 2>&1 || (cat $(COV_DIR)/build.log; exit 1)
	@$(LLVM_COV) show $(COV_DIR)/real_tests_bin -instr-profile=$(COV_DIR)/tests.profdata \
	    -format=html -output-dir=$(COV_DIR)/html -show-line-counts-or-regions
	@grep -q "REAL dark-coverage theme" $(COV_DIR)/html/style.css 2>/dev/null || \
	    cat docs/coverage-style.css >> $(COV_DIR)/html/style.css
	@echo "HTML coverage report: $(COV_DIR)/html/index.html"

# --- QA tools (wrappers; no compilation policy here) ----------------------

# A static_regex over a text-mode Unicode shorthand (\d \s) builds a large constexpr program that
# exceeds clang's default constexpr-steps budget; raise it here as the CMake test build does.
lint:
	@ls tests/*.cpp | xargs -P $(JOBS) -I{} clang-tidy {} -- $(CXXSTD) -fconstexpr-steps=33554432 $(INCLUDES) -I$(SCIFORGE_INCLUDE)

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
# the shared file. See MISRA.md.
misra:
	mkdir -p $(BUILD)
	printf '#include <real/real.hpp>\nint main(){ try { const real::regex r("a"); return r.search("a") ? 0 : 1; } catch (...) { return 2; } }\n' > $(BUILD)/misra_tu.cpp
	clang-tidy --config-file=$(SCIFORGE_LINT)/clang-tidy-misra \
	    --checks='-cppcoreguidelines-pro-type-union-access' \
	    --header-filter='include/real/.*' \
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

# ThreadSanitizer smoke: a standalone multi-threaded program (tests/tsan_compat.cpp) that hammers the
# concurrent lazy std_engine() build on shared regex objects, proving the "concurrent const ops are
# race-free" claim reproducibly. Standalone (no framework / test_static.cpp non-atomic op-new counter,
# which would itself race). Clang feature; always uses Clang.
tsan:
	mkdir -p $(BUILD)
	clang++ $(CXXSTD) -O1 -g $(INCLUDES) -fsanitize=thread tests/tsan_compat.cpp -o $(BUILD)/tsan_compat
	$(BUILD)/tsan_compat

# Differential fuzzer: real::compat vs std::regex (search/replace/iterate/token/match-flags). This
# is the net that has caught every silent divergence in the compat layer, so it runs in CI too.
fuzz-compat:
	mkdir -p $(FUZZ_DIR)/corpus-compat
	clang++ $(CXXSTD) -O1 -g $(INCLUDES) \
	    -fsanitize=fuzzer,address,undefined fuzz/fuzz_compat.cpp -o $(FUZZ_DIR)/fuzz_compat
	$(FUZZ_DIR)/fuzz_compat -max_total_time=$(FUZZ_TIME) -timeout=10 \
	    $(FUZZ_DIR)/corpus-compat fuzz/corpus

doc: coverage-html
	mkdir -p $(BUILD)/doc
	doxygen Doxyfile
	@rm -rf $(BUILD)/doc/html/coverage
	@cp -R $(COV_DIR)/html $(BUILD)/doc/html/coverage
	@echo "API reference: $(BUILD)/doc/html/index.html"

# Doc target for environments without Clang/LLVM coverage tools (e.g. CI that only
# needs the API reference). It does not rebuild the coverage report.
doc-no-coverage:
	mkdir -p $(BUILD)/doc
	doxygen Doxyfile
	@echo "API reference: $(BUILD)/doc/html/index.html"

format:
	uncrustify -c $(SCIFORGE_LINT)/uncrustify.cfg --replace --no-backup $(FORMAT_FILES)

format-check:
	uncrustify -c $(SCIFORGE_LINT)/uncrustify.cfg --check $(FORMAT_FILES)

# --- Python binding -------------------------------------------------------

# Builds the abi3 extension in place against include/. Packaging lives in the
# root pyproject.toml / setup.py.
# build_ext compares only _real.cpp's timestamp against the built .so; it never sees the
# header-only engine the .cpp #includes, so a header-only change would leave a stale .so.
# Gate the rebuild on the headers through a stamp, and pass --force so the recompile
# actually happens when a header changed (build_ext would otherwise skip it).
HEADERS := $(wildcard include/real/*.hpp)

python: $(BUILD)/py_ext.stamp

$(BUILD)/py_ext.stamp: python/src/_real.cpp $(HEADERS)
	SCIFORGE_INCLUDE=$(SCIFORGE_INCLUDE) $(PYTHON) setup.py -q build_ext --inplace --force
	@mkdir -p $(BUILD)
	@touch $@

python-test: python
	$(PYRUN) -m unittest discover -s python/tests

# Version-consistency gate: pyproject.toml is the single source of truth — `make
# release` bumps __init__.py from it, CMakeLists.txt derives it. Asserts the three
# agree and that CMake still DERIVES (no hardcoded literal that could drift) — the
# invariant the CMake 2026.6.6-vs-.8 drift violated.
version-check:
	@py=$$(sed -nE 's/^version = "([0-9][0-9.]*)"/\1/p' pyproject.toml); \
	 ini=$$(sed -nE 's/^__version__ = "([0-9][0-9.]*)"/\1/p' python/real/__init__.py); \
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
	 echo "version-check: $$py (pyproject = __init__ = CMake-derived = version.hpp)"

# Every pass/fail gate this machine owns, in one command — the canonical pre-push check
# and, like the SciLang-era libraries, the macOS gate of record. REAL holds its own
# (>95% lines) coverage bar rather than the strict 100% 4D of the SciLang-era libraries,
# so coverage is NOT bundled here (run `make coverage` separately); this gate is the
# binary pass/fail checks. doc-no-coverage fails on any Doxygen warning (WARN_AS_ERROR).
# GXX defaults to the CI GCC (g++-14); override with `make full-local-gate GXX=g++-13`. If it is
# absent, the GCC leg is skipped with a warning (the g++-14 CI job is the backstop).
GXX ?= g++-14
full-local-gate:
	@$(MAKE) format-check
	@$(MAKE) version-check
	@$(MAKE) test
	@if command -v $(GXX) >/dev/null 2>&1; then $(MAKE) test CXX=$(GXX) BUILD=$(BUILD)/gcc; else echo "full-local-gate: WARN — $(GXX) absent, GCC leg skipped (CI covers it)"; fi
	@$(MAKE) sanitize
	@$(MAKE) misra
	@$(MAKE) doc-no-coverage
	@$(MAKE) python-test
	@$(MAKE) lint | tee $(BUILD)/lint.log; ! grep -qE 'warning:|error:' $(BUILD)/lint.log
	@echo "full-local-gate: ALL gates green (clang + $(GXX) when present, sanitize, MISRA, lint, doc, python, version-check)"

bench-python: python
	$(PYRUN) benchmarks/bench.py

bench-fuzz: python
	$(PYRUN) benchmarks/fuzz_bench.py

# Multi-engine C++ throughput benchmark (REAL vs std::regex vs PCRE2 vs RE2).
# Optional engines are compiled in only when pkg-config locates them.
# The C++ binary only measures and emits JSON; benchmarks/bench_engines.py (the consumer)
# applies the shared stats module to produce the table, CIs, and ASCII box-plots. Manual,
# not a CI gate. Tune samples via BENCH_SAMPLES / BENCH_BOOTSTRAP.
bench-engines:
	@mkdir -p $(BUILD)
	@flags=""; \
	 if pkg-config --exists libpcre2-8; then flags="$$flags -DHAVE_PCRE2 $$(pkg-config --cflags --libs libpcre2-8)"; fi; \
	 if pkg-config --exists re2; then flags="$$flags -DHAVE_RE2 $$(pkg-config --cflags --libs re2)"; fi; \
	 commit=$$(git rev-parse --short HEAD 2>/dev/null || echo unknown); \
	 echo "engines: REAL std::regex$${flags:+ +optional}"; \
	 c++ -std=c++20 -O2 -DBENCH_FLAGS='"-O2"' -DBENCH_COMMIT="\"$$commit\"" $(INCLUDES) -I$(SCIFORGE_INCLUDE) benchmarks/bench_engines.cpp $$flags -o $(BUILD)/bench_engines
	$(PYRUN) benchmarks/bench_engines.py $(BUILD)/bench_engines

# Proves the *system* install end to end, the exact packager path: install REAL to a temp prefix
# with -DBUILD_TESTING=OFF (noarch LIBDIR=lib, no SciForge — the library stands alone), then
# consume it the three supported C++ ways plus a negative check that the C++20 guard fires. CXX is
# honored (run under clang and g++). Used by the install-smoke CI job.
install-smoke:
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
	 "$$work/ex/hello"; "$$work/ex/redos_demo"; echo "      examples: OK"; \
	 echo "install-smoke: OK (find_package + pkg-config + direct-copy + negative guard + examples)"

# Installs the package from the repository root (root pyproject.toml builds the
# abi3 extension against include/). uninstall removes it by distribution name.
install:
	$(PYTHON) -m pip install .

uninstall:
	$(PYTHON) -m pip uninstall -y real-regex

# Cuts a calendar-versioned release. Computes YYYY.M.PATCH with the patch reset
# each month (first release of a month is .0; PEP 440 drops leading zeros, so
# 2026.6.1, never 2026.06.001), bumps pyproject.toml + __init__.py (CMakeLists.txt
# derives its version from pyproject.toml, so it follows automatically), commits, tags and
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
	 vmaj=$$(echo "$$version" | cut -d. -f1); vmin=$$(echo "$$version" | cut -d. -f2); vpat=$$(echo "$$version" | cut -d. -f3); \
	 sed -i.bak -E "s/^#define REAL_VERSION_MAJOR .*/#define REAL_VERSION_MAJOR $$vmaj/; \
	                s/^#define REAL_VERSION_MINOR .*/#define REAL_VERSION_MINOR $$vmin/; \
	                s/^#define REAL_VERSION_PATCH .*/#define REAL_VERSION_PATCH $$vpat/" include/real/version.hpp && rm -f include/real/version.hpp.bak; \
	 git add pyproject.toml python/real/__init__.py include/real/version.hpp; \
	 git commit -m "release: v$$version"; \
	 git tag "v$$version"; \
	 git push origin HEAD "v$$version"

clean:
	rm -rf $(BUILD) python/build python/real/*.so python/*.egg-info *.egg-info dist
