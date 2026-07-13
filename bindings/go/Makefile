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

check-vendor: vendor ## Fails if the committed vendored tree has drifted from include/ + bindings/c (CI gate)
	@git diff --exit-code -- vendor_include real_capi.h real_capi.cpp || \
	  (echo "check-vendor: FAILED -- vendor_include/real_capi.{h,cpp} are stale, run 'make go-vendor' and commit" && exit 1)
	@echo "check-vendor: OK -- vendored tree matches include/ + bindings/c exactly"

clean: ## Remove build artifacts (NOT vendor_include -- that is committed, not generated-and-discarded)
	go clean

help: ## Show these targets
	@echo "bindings/go — make go-<target> from the root (or make -C bindings/go <target>):"
	@grep -hE '^[a-z-]+:.*##' $(firstword $(MAKEFILE_LIST)) | sort | sed -E 's/^([a-z-]+):.*## /  go-\1\t/'
