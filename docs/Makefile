# docs/ -- the API-reference / docs-site section (Doxygen + Sphinx/Breathe).
# Standardized per-section Makefile: invoked from the repo root as `make doc` /
# `doc-no-coverage` / `doc-check` / `docs-site` / `docs-site-gate` (via the root's thin
# delegations, which preserve these 5 names -- the CI invariant) or directly here with
# `make -C docs <target>`. Paths are $(ROOT)-anchored so a recipe runs identically from
# either place.
#
# `coverage-html`/COV_DIR stay in the root Makefile (tests/coverage domain, not
# migrated here). The two targets below that need coverage output (`doc`,
# `docs-site-gate`) rely on the ROOT orchestrating `coverage-html` before delegating --
# see the root Makefile's own `doc`/`docs-site-gate` targets for that half.
ROOT := $(abspath $(CURDIR)/..)
include $(ROOT)/mk/common.mk
# HELP_PREFIX stays empty (the default in mk/help.mk): this section's target names are
# the same at the root as here (thin delegations, not prefixed aliases like c-build).
include $(ROOT)/mk/help.mk

# Doxygen resolves every relative path INSIDE Doxyfile (OUTPUT_DIRECTORY, INPUT, ...)
# against the invoking process's CURRENT WORKING DIRECTORY, not against the Doxyfile's
# own location -- so `doxygen Doxyfile` must run with CWD=$(ROOT) even when this
# Makefile itself runs from docs/ (`make -C docs ...`). Each recipe line below `cd
# $(ROOT) &&`s into it for that one shell invocation only (each recipe line is its own
# shell; nothing leaks to the next line).

# Coverage HTML output path -- the same computed value as the root's $(COV_DIR)/html,
# recomputed locally so this Makefile does not need COV_DIR's full definition
# (COV_CXX/PROFDATA/LLVM_COV/COV_FLOOR etc., which stay root-only/tests-domain).
COV_HTML := $(BUILD)/coverage/html

.PHONY: doc doc-no-coverage doc-check docs-site docs-site-gate help
.DEFAULT_GOAL := help

doc: ## Generate API reference (Doxygen) with embedded coverage (root runs coverage-html first)
	mkdir -p $(BUILD)/doc
	cd $(ROOT) && doxygen Doxyfile
	@rm -rf $(BUILD)/doc/html/coverage
	@cp -R $(COV_HTML) $(BUILD)/doc/html/coverage
	@echo "API reference: $(BUILD)/doc/html/index.html"

# Doc target for environments without Clang/LLVM coverage tools (e.g. CI that only
# needs the API reference). It does not rebuild the coverage report.
doc-no-coverage: ## Generate API reference without coverage report
	mkdir -p $(BUILD)/doc
	cd $(ROOT) && doxygen Doxyfile
	@echo "API reference: $(BUILD)/doc/html/index.html"

# doc-check builds the docs under the EXACT CI Doxygen (1.9.8, in Docker) via the shared
# SciForge tool, so a warning the developer's newer local Doxygen tolerates cannot slip
# past to CI or a release (the gate-hole that shipped a broken Docs build before).
# Skipped with a warning when Docker or the tool is absent -- visible, never a false
# green; the Docs CI job is the backstop.
doc-check: ## Build docs under the exact CI Doxygen (Docker) to catch version-drift warnings
	@if command -v docker >/dev/null 2>&1 && test -x $(SCIFORGE_TOOLS)/doxygen-check.sh; then \
	   $(SCIFORGE_TOOLS)/doxygen-check.sh $(ROOT) Doxyfile; \
	 else \
	   echo "doc-check: WARN — Docker or $(SCIFORGE_TOOLS)/doxygen-check.sh absent, CI-Doxygen check skipped (the Docs CI job is the backstop)"; \
	 fi

# --- docs/site (Sphinx + Breathe + pydata-sphinx-theme, doc-site P1) -------
#
# Additive to `doc`/`doc-no-coverage` above, not a replacement: docs.yml still
# deploys build/doc/html (the live Doxygen site) untouched. This builds the
# themed landing + Breathe-powered API scaffold into build/site/html, entirely
# separate output.
#
# sphinx-build comes from docs/requirements.txt (pinned), installed into an
# isolated venv -- never the system interpreter:
#   python3 -m venv .venv-docs && .venv-docs/bin/pip install -r docs/requirements.txt
#   source .venv-docs/bin/activate   # sphinx-build now on PATH
SPHINXBUILD ?= sphinx-build
SPHINXOPTS  ?=

docs-site: doc-no-coverage ## Build the themed Sphinx/Breathe site (docs/site) -> build/site/html
	@command -v $(SPHINXBUILD) >/dev/null 2>&1 || { \
	  echo "docs-site: '$(SPHINXBUILD)' not found -- install docs/requirements.txt into a venv" \
	       "and put it on PATH (or pass SPHINXBUILD=/path/to/sphinx-build)"; exit 1; }
	$(SPHINXBUILD) $(SPHINXOPTS) -b html $(ROOT)/docs/site $(BUILD)/site/html
	@if [ -d $(COV_HTML) ]; then \
	  rm -rf $(BUILD)/site/html/coverage && cp -R $(COV_HTML) $(BUILD)/site/html/coverage; \
	fi
	@rm -rf $(BUILD)/site/html/api && cp -R $(BUILD)/doc/html $(BUILD)/site/html/api
	@echo "docs-site: $(BUILD)/site/html/index.html"

# The site's own gate: -W (every warning fatal) + --keep-going (report all of them,
# not just the first) + linkcheck, mirroring doc-no-coverage's WARN_AS_ERROR
# discipline for the Doxygen side. Separate output dirs from docs-site's release
# build (_gate / _linkcheck) so a gate run never staleness-poisons a real build.
#
# Two nets added for doc-site P1c (the bespoke landing template, docs/site/_templates/
# landing.html, served via html_additional_pages -- see conf.py):
#
#   no-inline-quickstart, adapted: the quickstart panels are no longer {literalinclude}
#   directives in a source document (P1b-A's shape) -- they are `{{ quickstart_* }}`
#   placeholders injected by conf.py's html-page-context hook (P1c). The check now scans
#   the TEMPLATE SOURCE for (a) any of the 4 placeholders missing (the injection points
#   themselves were deleted) and (b) known verbatim strings from the actual snippet
#   bodies (static_regex/MustCompile(/etc.) -- their presence in the template SOURCE
#   would mean someone pasted real code in over the hook instead of using it. Rendered
#   OUTPUT necessarily contains those same strings (that's the whole point of the
#   injection), so this greps docs/site/_templates/landing.html, never build/site/html.
#
#   built-HTML link check (tools/check_site_links.py): linkcheck (above) walks SOURCE
#   documents only -- landing.html is a Jinja2 template, invisible to it, so a broken
#   href inside the bespoke landing could ship undetected (the exact "linkcheck doesn't
#   see html_additional_pages" gap the P1c review caught); linkcheck also never
#   resolves a URL #fragment at all (linkcheck_anchors = False, conf.py), so a stale
#   forward-ref anchor into build/doc/html (doc-site P2a: the Drop-in pages' @ref
#   links into /api/ until those targets migrate into the site) would be invisible to
#   it on both counts. Needs the REAL release output (api/ + coverage/ copied in, not
#   just _gate's bare sphinx-build), so this builds docs-site before checking it.
#   Site-wide since doc-site P2a (no PAGE argument -> every build/site/html/*.html,
#   file + #anchor both verified, excluding api/+coverage/ as scan ROOTS -- see the
#   script's own docstring for why those two stay Doxygen's/llvm-cov's own gate).
#
# NB: coverage-html itself is NOT invoked from this recipe -- it is root-only (tests/
# coverage domain, not migrated here). The root Makefile's `docs-site-gate` delegation
# runs it (as a recipe line, not a prerequisite -- matching how this recipe always
# invoked it pre-migration) BEFORE calling `$(MAKE) -C docs docs-site-gate`, so
# $(COV_HTML) is already populated by the time `docs-site` (below) runs.
docs-site-gate: ## Sphinx -W --keep-going + linkcheck + built-HTML link check for docs/site (the site's net)
	@echo "── check-features-probe (features.yaml -> tests/frontend/features_probe_generated.inc drift; the paths-ignore blind spot -- features.yaml lives under docs/**, which ci.yml's push trigger ignores, so THIS step -- not ci.yml -- is what a yaml-edited-without-regen push actually trips: docs-site.yml has no paths filter)"
	@$(MAKE) -C $(ROOT)/tools check-features-probe
	@command -v $(SPHINXBUILD) >/dev/null 2>&1 || { \
	  echo "docs-site-gate: '$(SPHINXBUILD)' not found -- install docs/requirements.txt into a venv" \
	       "and put it on PATH (or pass SPHINXBUILD=/path/to/sphinx-build)"; exit 1; }
	@mkdir -p $(BUILD)/doc && cd $(ROOT) && doxygen Doxyfile
	$(SPHINXBUILD) $(SPHINXOPTS) -W --keep-going -b html $(ROOT)/docs/site $(BUILD)/site/_gate
	$(SPHINXBUILD) $(SPHINXOPTS) -b linkcheck $(ROOT)/docs/site $(BUILD)/site/_linkcheck
	@echo "── no-inline-quickstart (landing.html: injected {{ quickstart_* }} placeholders only, zero hardcoded snippet code)"
	@tmpl=$(ROOT)/docs/site/_templates/landing.html; \
	 missing=""; \
	 for ph in quickstart_cpp quickstart_py quickstart_rs quickstart_go; do \
	   grep -qE '\{\{[[:space:]]*'"$$ph"'[[:space:]]*\}\}' "$$tmpl" || missing="$$missing $$ph"; \
	 done; \
	 if [ -n "$$missing" ]; then \
	   echo "docs-site-gate: FAIL -- $$tmpl is missing injection point(s):$$missing (must stay {{ quickstart_* }}, never hardcoded)"; \
	   exit 1; \
	 fi; \
	 hit=$$(grep -nE 'static_regex|regex_search\(line|import real as re|re\.search\(r|real_regex::Regex|real\.MustCompile\(|FindSubmatchIndex\(' "$$tmpl" || true); \
	 if [ -n "$$hit" ]; then \
	   echo "docs-site-gate: FAIL -- hardcoded quickstart code found in $$tmpl (must be injected via {{ quickstart_* }}, not pasted):"; \
	   printf '%s\n' "$$hit" | sed 's/^/  /'; \
	   exit 1; \
	 fi
	@echo "── built-HTML link check (build/site/html/*.html, site-wide -- linkcheck can't see the bespoke landing template or any #anchor)"
	@$(MAKE) docs-site
	@python3 $(ROOT)/tools/check_site_links.py $(BUILD)/site/html
	@echo "docs-site-gate: PASS (sphinx -W --keep-going + linkcheck + no-inline-quickstart + built-HTML link check)"

help: ## Show these targets
	@$(HELP_RECIPE)
