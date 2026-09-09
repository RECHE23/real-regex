#!/bin/sh
# Refresh ONLY Ubuntu's apt indexes, then install the named packages.
#
# A bare `apt-get update` on GitHub's ubuntu-24.04 / ubuntu-latest image also
# hits every third-party source the image ships (microsoft-prod, docker, …).
# A Hash Sum mismatch on ANY of those fails the step, even though every package
# this repository installs from apt is an Ubuntu package. That blocked docs.yml
# and docs-site.yml twice on 2026-09-09 — two reds, then a green on retry, so
# the index was intermittent, not permanently broken. The publication of the
# docs still depended on the health of indexes we do not consume.
#
# This script deletes the third-party source files (the runner is ephemeral)
# and keeps Ubuntu's own (`ubuntu.sources`, `ubuntu.list`, `ubuntu-*`).
# `/etc/apt/sources.list` is left alone.
#
# Usage: ci-apt-ubuntu.sh <package>...
set -eu

if [ "$#" -eq 0 ]; then
  echo "ci-apt-ubuntu: usage: $0 <package>..." >&2
  exit 2
fi

if [ ! -d /etc/apt/sources.list.d ]; then
  echo "ci-apt-ubuntu: /etc/apt/sources.list.d missing — not a Debian/Ubuntu runner" >&2
  exit 1
fi

echo "ci-apt-ubuntu: dropping non-Ubuntu apt sources (the bound):"
# -print so the log names every source we refused. -maxdepth 1: do not walk
# fragments under a third-party subdirectory we have already decided to drop.
sudo find /etc/apt/sources.list.d -maxdepth 1 -type f \
  ! -name 'ubuntu' \
  ! -name 'ubuntu.*' \
  ! -name 'ubuntu-*' \
  -print \
  -delete

sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y "$@"
