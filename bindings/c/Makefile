# The C ABI shim (bindings/c). Standardized per-binding targets, invoked from the repo root as `make c-<target>`
# (via the top-level `c-%` alias) or directly here with `make -C bindings/c <target>`. Paths are $(ROOT)-anchored
# so a recipe runs identically from either place.
ROOT      := $(abspath $(CURDIR)/../..)
BUILD     ?= $(ROOT)/build
CXX       ?= c++
CXXSTD    := -std=c++20
INCLUDES  := -I$(ROOT)/include
FUZZ_DIR  := $(BUILD)/fuzz
FUZZ_TIME ?= 60

.PHONY: build test fuzz clean help
.DEFAULT_GOAL := help

build: ## Source-only shim (real_capi.cpp / .h) — built by its consumers (c-test, the rust crate), nothing standalone
	@echo "bindings/c: the C ABI shim is source-only; c-test and the crate build it."

test: ## Build and run the C ABI tests (test_capi.c linked against the shim)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXSTD) -O2 $(INCLUDES) -c $(ROOT)/bindings/c/real_capi.cpp -o $(BUILD)/real_capi.o
	cc -I$(ROOT)/bindings/c -c $(ROOT)/bindings/c/test_capi.c -o $(BUILD)/test_capi.o
	$(CXX) $(BUILD)/test_capi.o $(BUILD)/real_capi.o -o $(BUILD)/capi_test   # C++ driver links the right stdlib
	$(BUILD)/capi_test

fuzz: ## libFuzzer over the C ABI shim under ASan/UBSan (FUZZ_TIME=secs)
	@mkdir -p $(FUZZ_DIR)/corpus-capi
	clang++ $(CXXSTD) -O1 -g $(INCLUDES) -I$(ROOT)/bindings/c \
	    -fsanitize=fuzzer,address,undefined $(ROOT)/fuzz/fuzz_capi.cpp -o $(FUZZ_DIR)/fuzz_capi
	$(FUZZ_DIR)/fuzz_capi -max_total_time=$(FUZZ_TIME) -timeout=10 $(FUZZ_DIR)/corpus-capi

clean: ## Remove this binding's build artifacts
	rm -f $(BUILD)/real_capi.o $(BUILD)/test_capi.o $(BUILD)/capi_test $(FUZZ_DIR)/fuzz_capi

help: ## Show these targets
	@echo "bindings/c — make c-<target> from the root (or make -C bindings/c <target>):"
	@grep -hE '^[a-z-]+:.*##' $(firstword $(MAKEFILE_LIST)) | sort | sed -E 's/^([a-z-]+):.*## /  c-\1\t/'
