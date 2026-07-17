# mk/help.mk — canonical recipe for a SECTION Makefile's `help:` target: the
# `target: ## desc` + grep|sed convention already used by bindings/{c,go,python,rust}.
#
# Vendored in-repo, NOT included from sciforge: sciforge is never a build/runtime
# dependency of this repo (check-pins/doc-check WARN-and-skip when the sciforge sibling
# is absent). A hard `include ../sciforge/mk/help.mk` would break a bare `make` on a
# clone without sciforge checked out alongside (.DEFAULT_GOAL := help). This file is the
# canonical copy, a candidate to be mirrored at sciforge/mk/help.mk for other
# SciForge-ecosystem repos to vendor the same way, in a later wagon.
#
# Usage from a section Makefile (e.g. bindings/c/Makefile), HELP_PREFIX preserves the
# root-alias convention (targets show as `c-build`, not `build`):
#   HELP_PREFIX := c-
#   help: ## Show these targets
#   	@$(HELP_RECIPE)
# NB: `#` starts a Make comment on a non-recipe line (like this one) unless escaped as
# `\#` -- unlike a recipe line, where `#` is passed through to the shell verbatim.
HELP_PREFIX ?=
HELP_RECIPE = grep -hE '^[a-zA-Z0-9_-]+:.*\#\#' $(firstword $(MAKEFILE_LIST)) | sort | sed -E 's/^([a-zA-Z0-9_-]+):.*\#\# /  $(HELP_PREFIX)\1\t— /'
