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

.PHONY: doc doc-no-coverage doc-check doc-site-xml doc-xml docs-site docs-site-gate help
.DEFAULT_GOAL := help

# HTML output is incremental: flipping SOURCE_BROWSER / VERBATIM_HEADERS /
# INTERACTIVE_SVG leaves the old *_source.html and *_org.svg until wiped.
doc: ## Generate API reference (Doxygen) with embedded coverage (root runs coverage-html first)
	mkdir -p $(BUILD)/doc
	rm -rf $(BUILD)/doc/html
	cd $(ROOT) && doxygen Doxyfile
	@rm -rf $(BUILD)/doc/html/coverage
	@cp -R $(COV_HTML) $(BUILD)/doc/html/coverage
	@echo "API reference: $(BUILD)/doc/html/index.html"

# Doc target for environments without Clang/LLVM coverage tools (e.g. CI that only
# needs the API reference). It does not rebuild the coverage report.
doc-no-coverage: ## Generate API reference without coverage report
	mkdir -p $(BUILD)/doc
	rm -rf $(BUILD)/doc/html
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
	   echo "step 8: doc-check (CI-pinned Doxygen) -- docker or $(SCIFORGE_TOOLS)/doxygen-check.sh absent" | tee -a $(GATE_SKIPS); \
	 fi

# --- docs/site (Sphinx + Breathe + pydata-sphinx-theme) -------
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

# User-facing XML for Breathe. Deleted first: Doxygen's XML output is incremental
# and will otherwise leave compounds for \internal members that just became hidden.
doc-site-xml: ## User-facing Doxygen XML (INTERNAL_DOCS=NO) -> build/doc/xml-site
	mkdir -p $(BUILD)/doc
	rm -rf $(BUILD)/doc/xml-site
	cd $(ROOT) && doxygen Doxyfile.site
	@echo "docs-site XML: $(BUILD)/doc/xml-site"

# Both profiles. `doc-site-xml` alone leaves build/doc/xml stale, and
# check-doc-style will refuse to verdict rather than lie -- that is the
# two-profile split working, not a broken check. This target is the one
# command for a session that then wants style + voice + curated-members.
doc-xml: doc-no-coverage doc-site-xml ## Both Doxygen XML trees (developer + user)

# -d keeps Sphinx's pickle cache out of the deployed tree (html/.doctrees
# was 4 MiB on Pages). rm is the leftover from a build that used the default.
# One llvm-cov tree, at /coverage. cp -R into api/ nests a third copy when
# api/coverage already exists, and Doxygen's own `make doc` copy rides along
# if that ran first -- delete it either way, then rewrite the iframe.
docs-site: doc-no-coverage doc-site-xml ## Build the themed Sphinx/Breathe site (docs/site) -> build/site/html
	@command -v $(SPHINXBUILD) >/dev/null 2>&1 || { \
	  echo "docs-site: '$(SPHINXBUILD)' not found -- install docs/requirements.txt into a venv" \
	       "and put it on PATH (or pass SPHINXBUILD=/path/to/sphinx-build)"; exit 1; }
	$(SPHINXBUILD) $(SPHINXOPTS) -d $(BUILD)/site/doctrees -b html $(ROOT)/docs/site $(BUILD)/site/html
	@rm -rf $(BUILD)/site/html/.doctrees
	@if [ -d $(COV_HTML) ]; then \
	  rm -rf $(BUILD)/site/html/coverage && cp -R $(COV_HTML) $(BUILD)/site/html/coverage; \
	fi
	@rm -rf $(BUILD)/site/html/api && cp -R $(BUILD)/doc/html $(BUILD)/site/html/api
	@rm -rf $(BUILD)/site/html/api/coverage
	@python3 $(ROOT)/tools/relocate_coverage_iframe.py $(BUILD)/site/html/api/coverage.html
	@echo "docs-site: $(BUILD)/site/html/index.html"

# The site's own gate: -W (every warning fatal) + --keep-going (report all of them,
# not just the first) + linkcheck, mirroring doc-no-coverage's WARN_AS_ERROR
# discipline for the Doxygen side. Separate output dirs from docs-site's release
# build (_gate / _linkcheck) so a gate run never staleness-poisons a real build.
#
# Both are DELETED before each run, because separating them is not enough to keep them
# clean. OBSERVED: this target failed with `breathe.file_state_cache.MTimeError: Cannot
# find file: .../structreal_1_1detail_1_1static__storage_1_1il__guard__fields.xml` --
# raised from `env-get-outdated`, before a single source was read, naming a struct that
# had been removed from the code weeks earlier. Deleting build/doc/xml did NOT clear it;
# deleting build/site did. So the surviving reference lived in the gate's own leftovers,
# and the run before it is what poisoned the run after.
#
# The mechanism is inferred and not proven: the traceback points at Breathe's record of
# the Doxygen compounds it last read, but THREE attempts to reproduce the state on demand
# (deleting a compound file, adding then removing a namespace-scope struct, the same with
# a nested one) all failed to raise it, so do not treat that reading as established. What
# is measured is the repair -- and one supporting fact found while trying: Doxygen's XML
# output is INCREMENTAL and does not remove compounds for entities deleted from the code.
# Two probe structs added and then reverted kept their compound files across a subsequent
# `doxygen Doxyfile`, and stale XML in that state is exactly what check-doc-style's
# freshness guard exists to refuse -- it tripped on those leftovers minutes later. `-E` was tried first and is NOT a substitute -- with a
# compound's XML gone while index.xml still lists it, the same MTimeError is raised on a
# freshly built environment.
#
# Worth doing regardless of the mechanism: a gate that a previous run of itself can turn
# red is reporting on its build directory rather than on the tree under test, and at the
# point it prints, that red is indistinguishable from a real one. The cost is a full
# re-read of ~29 pages, which this target pays in any case -- it regenerates the Doxygen
# XML on every run, so its environment can go stale on every run.
#
# Two nets for the bespoke landing template (docs/site/_templates/
# landing.html, served via html_additional_pages -- see conf.py):
#
#   no-inline-quickstart: the quickstart panels are not {literalinclude}
#   directives in a source document -- they are `{{ quickstart_* }}`
#   placeholders injected by conf.py's html-page-context hook. The check scans
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
#   see html_additional_pages" gap); linkcheck also never
#   resolves a URL #fragment at all (linkcheck_anchors = False, conf.py), so a stale
#   forward-ref anchor into build/doc/html (the Drop-in pages' @ref
#   links into /api/ until those targets migrate into the site) would be invisible to
#   it on both counts. Needs the REAL release output (api/ + coverage/ copied in, not
#   just _gate's bare sphinx-build), so this builds docs-site before checking it.
#   Site-wide (no PAGE argument -> every build/site/html/*.html,
#   file + #anchor both verified, excluding api/+coverage/ as scan ROOTS -- see the
#   script's own docstring for why those two stay Doxygen's/llvm-cov's own gate).
#
# NB: coverage-html itself is NOT invoked from this recipe -- it is root-only (tests/
# coverage domain, not migrated here). The root Makefile's `docs-site-gate` delegation
# runs it (as a recipe line, not a prerequisite -- matching how this recipe always
# invoked it pre-migration) BEFORE calling `$(MAKE) -C docs docs-site-gate`, so
# $(COV_HTML) is already populated by the time `docs-site` (below) runs.
docs-site-gate: ## Sphinx -W --keep-going + linkcheck + built-HTML link check for docs/site (the site's net)
	@echo "── check-features-probe (features.yaml -> tests/frontend/features_probe_generated.inc AND COMPATIBILITY.md's generated scorecard, both drift-checked; the paths-ignore blind spot -- features.yaml/COMPATIBILITY.md live under docs/**, which ci.yml's push trigger ignores, so THIS step -- not ci.yml -- is what a yaml-edited-without-regen push actually trips: docs-site.yml has no paths filter)"
	@$(MAKE) -C $(ROOT)/tools check-features-probe
	@command -v $(SPHINXBUILD) >/dev/null 2>&1 || { \
	  echo "docs-site-gate: '$(SPHINXBUILD)' not found -- install docs/requirements.txt into a venv" \
	       "and put it on PATH (or pass SPHINXBUILD=/path/to/sphinx-build)"; exit 1; }
	@mkdir -p $(BUILD)/doc && cd $(ROOT) && doxygen Doxyfile
	@rm -rf $(BUILD)/doc/xml-site && cd $(ROOT) && doxygen Doxyfile.site
	@echo "── check-doc-voice (no route vocabulary in published Doxygen XML)"
	@python3 $(ROOT)/tools/check_doc_voice.py
	@echo "── check-curated-members (allowlist omissions are explicit)"
	@python3 $(ROOT)/tools/check_curated_members.py
	@rm -rf $(BUILD)/site/_gate $(BUILD)/site/_linkcheck
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
	@echo "── published-tree hygiene (one coverage copy, no .doctrees, no source browser, no machine paths)"
	@test -f $(BUILD)/site/html/coverage/index.html || { echo "docs-site-gate: coverage/index.html missing -- the published /coverage tree (run make coverage-html first)"; exit 1; }
	@test ! -e $(BUILD)/site/html/api/coverage || { echo "docs-site-gate: FAIL -- api/coverage must not exist; one copy lives at /coverage"; exit 1; }
	@test ! -d $(BUILD)/site/html/.doctrees || { echo "docs-site-gate: FAIL -- .doctrees must not ship inside html/"; exit 1; }
	@if find $(BUILD)/site/html -name '*_source.html' | grep -q .; then \
	  echo "docs-site-gate: FAIL -- *_source.html must not ship (VERBATIM_HEADERS/SOURCE_BROWSER)"; \
	  find $(BUILD)/site/html -name '*_source.html' | sed 's/^/  /' | head; \
	  exit 1; \
	fi
	@if find $(BUILD)/site/html -name '*_org.svg' | grep -q .; then \
	  echo "docs-site-gate: FAIL -- *_org.svg must not ship (INTERACTIVE_SVG)"; \
	  find $(BUILD)/site/html -name '*_org.svg' | sed 's/^/  /' | head; \
	  exit 1; \
	fi
	@grep -q 'src="../coverage/index.html"' $(BUILD)/site/html/api/coverage.html || { echo "docs-site-gate: FAIL -- /api/coverage.html iframe was not relocated to ../coverage/"; exit 1; }
	@if grep -R --include='*.html' -l -E '/Users/|/home/' $(BUILD)/site/html >/dev/null; then \
	  echo "docs-site-gate: FAIL -- published HTML embeds machine paths"; \
	  grep -R --include='*.html' -l -E '/Users/|/home/' $(BUILD)/site/html | sed 's/^/  /' | head; \
	  exit 1; \
	fi
	@test ! -e $(BUILD)/site/html/coverage/coverage/Users || { echo "docs-site-gate: FAIL -- coverage tree still has coverage/Users"; exit 1; }
	@test ! -e $(BUILD)/site/html/coverage/coverage/home || { echo "docs-site-gate: FAIL -- coverage tree still has coverage/home"; exit 1; }
	@python3 $(ROOT)/tools/check_site_links.py $(BUILD)/site/html
	@echo "docs-site-gate: PASS (sphinx -W --keep-going + linkcheck + no-inline-quickstart + built-HTML link check + published-tree hygiene)"

help: ## Show these targets
	@$(HELP_RECIPE)
