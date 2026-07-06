# The Rust crate (bindings/rust). Standardized per-binding targets, invoked from the repo root as
# `make rust-<target>` (via the top-level `rust-%` alias) or directly with `make -C bindings/rust <target>`.
# cargo runs in this directory; only the vendor snapshot reaches back to $(ROOT) for the engine sources.
ROOT      := $(abspath $(CURDIR)/../..)
FUZZ_TIME ?= 60

.PHONY: build test bench fuzz publish-check vendor clean help
.DEFAULT_GOAL := help

build: ## Build the crate (debug)
	cargo build

test: ## Run the crate tests in both feature configurations (default and `fallback`)
	rm -rf vendor
	cargo test --quiet && cargo test --quiet --features fallback

bench: ## Run the native criterion benchmark (real-regex vs the regex crate); dev-only, long
	cargo bench --bench engines

fuzz: ## Run the differential fuzzer vs the regex crate (cargo-fuzz, nightly). FUZZ_TIME=secs
	cargo +nightly fuzz run differential -- -max_total_time=$(FUZZ_TIME) -timeout=10

vendor: ## Snapshot the engine sources into vendor/ so the crate builds standalone off crates.io
	rm -rf vendor
	mkdir -p vendor/include vendor/c
	cp -R $(ROOT)/include/real vendor/include/
	cp $(ROOT)/bindings/c/real_capi.h $(ROOT)/bindings/c/real_capi.cpp vendor/c/

publish-check: vendor ## Vendor, then let `cargo publish --dry-run` COMPILE the packaged tarball (the real gate)
	cargo publish --dry-run --allow-dirty

clean: ## Remove the crate build artifacts and the vendor snapshot
	cargo clean
	rm -rf vendor

help: ## Show these targets
	@echo "bindings/rust — make rust-<target> from the root (or make -C bindings/rust <target>):"
	@grep -hE '^[a-z-]+:.*##' $(firstword $(MAKEFILE_LIST)) | sort | sed -E 's/^([a-z-]+):.*## /  rust-\1\t/'
