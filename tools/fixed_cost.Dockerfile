# The per-call counts of tools/check_fixed_cost.py, where the host is not x86-64 Linux with valgrind: GCC 13,
# the major version CI's Linux job compiles with, so a stamp taken here is the one CI judges.
FROM gcc:13
RUN apt-get update && apt-get install -y --no-install-recommends valgrind && rm -rf /var/lib/apt/lists/*
