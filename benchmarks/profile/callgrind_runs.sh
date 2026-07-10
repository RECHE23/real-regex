#!/usr/bin/env bash
# P0 callgrind targets (devbox x86 preferred; Ir/byte is the deterministic column).
# Usage: from repo root, after `make profile-sample-build`:
#   bash benchmarks/profile/callgrind_runs.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build/profile"
CLEAN="$BUILD/profile_runner_clean"
OUT="$BUILD/callgrind"
mkdir -p "$OUT" "$BUILD/corpora"

if ! command -v valgrind >/dev/null 2>&1; then
  echo "valgrind not found — skip callgrind (devbox 106 pve2 is the source of truth)"
  exit 0
fi

# Small prose for Ir/byte (still multi-KB so routes engage)
python3 - <<'PY'
from pathlib import Path
root = Path("build/profile/corpora")
root.mkdir(parents=True, exist_ok=True)
(root / "prose.txt").write_text("the quick brown fox jumps over the lazy dog " * 2000)
(root / "ident.txt").write_text("id_42 name_foo val_7 key_bar ok_9 " * 2000)
(root / "sparse.txt").write_text(("words " * 40 + "dog ") * 500)
print("corpora ok")
PY

run_cg() {
  local name="$1"; shift
  echo "=== callgrind $name ==="
  valgrind --tool=callgrind --callgrind-out-file="$OUT/$name.out" \
    --instr-atstart=yes --collect-jumps=yes \
    "$@" >/dev/null 2>"$OUT/$name.valgrind.log" || true
  if command -v callgrind_annotate >/dev/null 2>&1 && [[ -f "$OUT/$name.out" ]]; then
    callgrind_annotate --auto=yes "$OUT/$name.out" > "$OUT/$name.annotate.txt" || true
    # Ir total from header
    head -40 "$OUT/$name.annotate.txt" | tee "$OUT/$name.summary.txt"
  fi
}

# 1) \w+ prose
run_cg w_plus_prose "$CLEAN" time '\w+' "$BUILD/corpora/prose.txt" count none
# 2) \w+ forced off class path (lazy-DFA / general)
run_cg w_plus_class_off "$CLEAN" time '\w+' "$BUILD/corpora/prose.txt" count class_off
# 3) ident dense
run_cg ident_dense "$CLEAN" time '(\w+)_(\w+)' "$BUILD/corpora/ident.txt" count none
# 4) alt sparse
run_cg alt_sparse "$CLEAN" time 'dog|fox|cat' "$BUILD/corpora/sparse.txt" count none

echo "callgrind outputs in $OUT"
echo "NOTE: rust \\w+ callgrind needs benchmarks/duel/rust_bench release binary + same prose.txt — run manually on devbox:"
echo "  valgrind --tool=callgrind benchmarks/duel/rust_bench/target/release/rust_bench '\\\\w+' build/profile/corpora/prose.txt find"
