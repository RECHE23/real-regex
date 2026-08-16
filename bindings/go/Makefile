# The Go binding (bindings/go). Standardized per-binding targets, invoked from the repo root as
# `make go-<target>` (via the top-level `go-%` alias) or directly with `make -C bindings/go <target>`.
# cgo compiles the vendored C++20 TU (real_capi.cpp) directly; go build/test run in this directory.
ROOT := $(abspath $(CURDIR)/../..)

.PHONY: build test vet vendor check-vendor clean help
.DEFAULT_GOAL := help

build: ## Build the module
	go build ./...

test: ## Run the module's tests (race detector on)
	go vet ./...
	go test -race ./...

vet: ## go vet only
	go vet ./...

vendor: ## Regenerate vendor_include/ and the vendored C-ABI shim FROM the source of truth
	rm -rf vendor_include
	mkdir -p vendor_include
	cp -R $(ROOT)/include/real vendor_include/
	cp $(ROOT)/bindings/c/real_capi.h $(ROOT)/bindings/c/real_capi.cpp .

# Compare include/ + bindings/c against the committed vendor copy. Does NOT
# write the tree: a check that regenerated first, then `git diff`, only proved
# the copy was deterministic, and left the tree dirty on failure. The count is
# the denominator — OK without it is the fourth silent green of the week.
check-vendor: ## Fails if the vendored tree has drifted from include/ + bindings/c (does not write)
	@set -eu; \
	 tmp=$$(mktemp -d); \
	 trap 'rm -rf "$$tmp"' EXIT; \
	 mkdir -p "$$tmp/real"; \
	 cp -R $(ROOT)/include/real/. "$$tmp/real/"; \
	 cp $(ROOT)/bindings/c/real_capi.h $(ROOT)/bindings/c/real_capi.cpp "$$tmp/"; \
	 exp=$$(find "$$tmp/real" -name '*.hpp' | wc -l | tr -d ' '); \
	 got=$$(find vendor_include/real -name '*.hpp' | wc -l | tr -d ' '); \
	 if [ "$$exp" != "$$got" ]; then \
	   echo "check-vendor: FAILED -- $$exp headers + 2 shim files compared; vendor_include has $$got headers"; \
	   echo "  run 'make go-vendor' and commit"; \
	   exit 1; \
	 fi; \
	 if ! diff -rq "$$tmp/real" vendor_include/real >/dev/null \
	    || ! diff -q "$$tmp/real_capi.h" real_capi.h >/dev/null \
	    || ! diff -q "$$tmp/real_capi.cpp" real_capi.cpp >/dev/null; then \
	   echo "check-vendor: FAILED -- $$exp headers + 2 shim files compared; vendored tree is stale"; \
	   echo "  run 'make go-vendor' and commit"; \
	   exit 1; \
	 fi; \
	 echo "check-vendor: OK -- $$exp headers + 2 shim files compared"

clean: ## Remove build artifacts (NOT vendor_include -- that is committed, not generated-and-discarded)
	go clean

help: ## Show these targets
	@echo "bindings/go — make go-<target> from the root (or make -C bindings/go <target>):"
	@grep -hE '^[a-z-]+:.*##' $(firstword $(MAKEFILE_LIST)) | sort | sed -E 's/^([a-z-]+):.*## /  go-\1\t/'
