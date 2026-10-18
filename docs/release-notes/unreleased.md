# Unreleased — The development checks and the release through SciForge's shared tiers

## Changed

- **Development only: the checks run in three tiers, and the release is cut, through SciForge's shared mechanism**
  (`make check-commit`, `check-push`, `check-release`, `make release`). The engine, its bindings and their packages are
  unchanged. `check-release` runs every check the former `full-local-gate` ran (which is now its name for the release
  tier); the release's notes are written as the work lands, and its tags carry them.
