# The Rust crate (bindings/rust). Standardized per-binding targets, invoked from the repo root as
# `make rust-<target>` (via the top-level `rust-%` alias) or directly with `make -C bindings/rust <target>`.
# cargo runs in this directory; only the vendor snapshot reaches back to $(ROOT) for the engine sources.
ROOT      := $(abspath $(CURDIR)/../..)
FUZZ_TIME ?= 60

.PHONY: build test bench fuzz publish-check vendor clean help
.DEFAULT_GOAL := help

build: ## Build the crate (debug)
	cargo build

test: ## Run the crate tests in both feature configurations (default and `fallback`) + the quickstart example
	rm -rf vendor
	cargo test --quiet && cargo test --quiet --features fallback
	# examples/quickstart.rs is the landing page's Rust tab (docs/site/_templates/landing.html,
	# doc-site P1b-A gate-snippet, injected at build time from that file — doc-site P1c) —
	# building AND running it here means the code the page shows is exactly the code that's
	# tested, in-tree, every `rust-test` run.
	@echo "── quickstart example (in-tree)"
	cargo run --quiet --example quickstart

bench: ## Run the native criterion benchmark (real-regex vs the regex crate); dev-only, long
	cargo bench --bench engines

fuzz: ## Run the differential fuzzer vs the regex crate (cargo-fuzz, nightly). FUZZ_TIME=secs
	cargo +nightly fuzz run differential -- -max_total_time=$(FUZZ_TIME) -timeout=10

vendor: ## Snapshot the engine sources into vendor/ so the crate builds standalone off crates.io
	rm -rf vendor
	mkdir -p vendor/include vendor/c
	cp -R $(ROOT)/include/real vendor/include/
	cp $(ROOT)/bindings/c/real_capi.h $(ROOT)/bindings/c/real_capi.cpp vendor/c/

publish-check: vendor ## Vendor, then let `cargo publish --dry-run` COMPILE the packaged tarball, INCLUDING the quickstart example (the real gate)
	cargo publish --dry-run --allow-dirty
	# `cargo publish --dry-run`'s own verify step only compiles the crate's LIB target from the
	# extracted tarball -- examples are not part of the implicit build it runs (confirmed
	# empirically: an example placed in a probe crate never appears in the verify step's rustc
	# invocations, with or without --all-features). So proving examples/quickstart.rs survives
	# PACKAGING -- the actual point of this check, since it's also the landing page's Rust tab
	# (docs/site/_templates/landing.html, doc-site P1b-A gate-snippet) -- needs one more explicit step: build
	# and run it from the exact extracted directory cargo publish just left behind
	# (target/package/<name>-<version>/), standing in for what crates.io would actually ship.
	@ver=$$(sed -nE 's/^version = "([^"]+)"/\1/p' Cargo.toml | head -1); \
	 pkgdir="target/package/real-regex-$$ver"; \
	 test -d "$$pkgdir" || { echo "publish-check: FAIL -- $$pkgdir missing (cargo publish --dry-run did not leave the extracted package behind)"; exit 1; }; \
	 echo "publish-check: quickstart example FROM THE PACKAGED TARBALL ($$pkgdir)"; \
	 cargo run --quiet --manifest-path "$$pkgdir/Cargo.toml" --example quickstart

clean: ## Remove the crate build artifacts and the vendor snapshot
	cargo clean
	rm -rf vendor

help: ## Show these targets
	@echo "bindings/rust — make rust-<target> from the root (or make -C bindings/rust <target>):"
	@grep -hE '^[a-z-]+:.*##' $(firstword $(MAKEFILE_LIST)) | sort | sed -E 's/^([a-z-]+):.*## /  rust-\1\t/'
