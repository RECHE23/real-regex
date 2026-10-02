# The per-call counts of tools/check_fixed_cost.py where the host is not CI's runner: CI's Linux job runs
# ubuntu-latest, Ubuntu 24.04 with its g++ 13 and glibc, and the counts include both, so this is that image.
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends g++ valgrind && rm -rf /var/lib/apt/lists/*
