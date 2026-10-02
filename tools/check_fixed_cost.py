#!/usr/bin/env python3
"""Hold the costs every call and every regex pay, which no throughput benchmark can see.

WHY THIS EXISTS. The benchmarks time long subjects, so a cost paid once per call or once per regex
amortises into nothing there. Two such costs went through every gate in one month: fifteen members the
search state initialized on every call (a SciLex lexer, which calls search once per rule and position,
spent up to 1.4 % more instructions), and 1 069 bytes of alternation products carried in place by every
regex (2 960 bytes instead of 1 888; a lexer held 60-120 KiB more). Both were found by hand, measuring a
downstream project.

WHAT IT HOLDS, AND HOW. Two numbers, each against a stamp versioned in tools/fixed_cost_stamps.json:
  - sizes: `sizeof` of `real::regex`, of its immutables and of the search state, compiled by the host's
    C++ compiler and, where the counts below run, by that GCC and its libstdc++. They are exact per ISA and
    standard library, so a stamp is matched exactly.
  - per-call instructions: callgrind counts a short call of five shapes (a literal, an alternation, an
    anchored match, a class, a miss), as the difference between 2 000 and 1 000 calls, so construction and
    process start cancel. Counted, not timed: the count is exact, where a stopwatch's floor is the size of
    these effects. It needs x86-64 Linux with valgrind and a real GCC; elsewhere it runs the same probe in
    the container tools/fixed_cost.Dockerfile builds (amd64, emulated where the host is not: valgrind
    counts the guest's instructions, so emulation changes the time, not the count). The stamp is keyed by
    GCC major version and matched within TOLERANCE either way; the file carries one per major CI's runner
    image can compile with (13 on ubuntu-24.04, 15 on its successor), measured natively for those the
    container does not build.

A stamp that moves either way fails: growth is the regression this exists for, and a gain left unstamped
would let a later regression spend it unseen. `--stamp` prints the measured values in the stamp file's
shape, to commit with the change that moved them and the reason in its message.

With neither the toolchain nor docker, the per-call half SKIPS and says so; CI's Linux job runs it natively.
"""

import json
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
STAMPS = ROOT / "tools" / "fixed_cost_stamps.json"
DOCKERFILE = ROOT / "tools" / "fixed_cost.Dockerfile"
IMAGE = "real-regex-fixed-cost:gcc13"
SHAPES = ("literal", "alternation", "anchored", "class", "miss")
TOLERANCE = 0.02  # within it, a stamp holds: GCC patch releases move a count by less than this
CALLS = (1000, 2000)

SIZE_PROBE = r"""
#include <cstdio>
#include <real/real.hpp>
int main()
{
#if defined(_LIBCPP_VERSION)
  const char* lib = "libc++";
#elif defined(__GLIBCXX__)
  const char* lib = "libstdc++";
#elif defined(_MSC_VER)
  const char* lib = "msvc";
#else
  const char* lib = "unknown";
#endif
  std::printf("%s %zu %zu %zu\n", lib, sizeof(real::regex), sizeof(real::detail::regex_immutables),
              sizeof(real::detail::dynamic_storage::state_type));
}
"""

CALL_PROBE = r"""
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <real/real.hpp>
int main(int argc, char** argv)
{
  if (argc != 3) { return 2; }
  const std::string_view shape {argv[1]};
  const long             calls {std::atol(argv[2])};
  long                   hits  {0};
  const auto run {[&](const real::regex& re, std::string_view subject, bool anchored) {
    for (long i = 0; i < calls; ++i) {
      hits += (anchored ? re.match(subject) : re.search(subject)).matched() ? 1 : 0;
    }
  }};
  long want {calls};
  if (shape == "literal")          { run(real::regex {"abc"}, "xxxxabcxxxx", false); }
  else if (shape == "alternation") { run(real::regex {"cat|dog|eel"}, "a dog here", false); }
  else if (shape == "anchored")    { run(real::regex {"[a-z]+[0-9]"}, "abc1", true); }
  else if (shape == "class")       { run(real::regex {"\\d+"}, "ab 42 cd", false); }
  else if (shape == "miss")        { run(real::regex {"zq+"}, "a subject of forty bytes, no hit in it", false); want = 0; }
  else { return 2; }
  std::printf("%ld\n", hits);
  return hits == want ? 0 : 1;
}
"""


def judge(measured, stamps):
    """Problems of `measured` ({key: {name: value}}) against `stamps`; sizes exact, counts within TOLERANCE."""
    problems = []
    for key, values in measured.items():
        stamp = stamps.get(key)
        if stamp is None:
            problems.append(f"no stamp for {key}: run tools/check_fixed_cost.py --stamp and commit it")
            continue
        exact = key.startswith("sizes/")
        for name, got in values.items():
            want = stamp.get(name)
            if want is None:
                problems.append(f"{key}: no stamp for {name} (measured {got})")
            elif exact and got != want:
                problems.append(f"{key}: sizeof {name} is {got} bytes, stamped {want}")
            elif not exact and abs(got - want) > TOLERANCE * want:
                problems.append(f"{key}: {name} costs {got} instructions a call, stamped {want} "
                                f"({(got - want) / want:+.1%}, tolerance {TOLERANCE:.0%})")
    return problems


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, check=False, timeout=1800, **kw)


def measure_sizes(cxx):
    with tempfile.TemporaryDirectory() as tmp:
        src = pathlib.Path(tmp) / "sizes.cpp"
        exe = pathlib.Path(tmp) / "sizes"
        src.write_text(SIZE_PROBE, encoding="utf-8")
        built = run([cxx, "-std=c++20", "-O2", f"-I{ROOT / 'include'}", str(src), "-o", str(exe)])
        if built.returncode != 0:
            raise RuntimeError(f"size probe did not compile:\n{built.stderr}")
        lib, regex, immut, state = run([str(exe)]).stdout.split()
    machine = {"aarch64": "arm64", "AMD64": "x86_64"}.get(platform.machine(), platform.machine())
    return {f"sizes/{machine}-{lib}": {"regex": int(regex), "immutables": int(immut), "state": int(state)}}


COUNT_SCRIPT = r"""
set -eu
g++ -std=c++20 -O2 -DNDEBUG -I"$1/include" "$2" -o /tmp/fixed_cost_probe
g++ -std=c++20 -O2 -I"$1/include" "$3" -o /tmp/fixed_cost_sizes
echo "sizes $(/tmp/fixed_cost_sizes)"
major=$(g++ -dumpversion | cut -d. -f1)
echo "gcc $major"
for shape in SHAPES; do
  for calls in CALLS; do
    valgrind --tool=callgrind --callgrind-out-file=/dev/null /tmp/fixed_cost_probe "$shape" "$calls" 2>&1 \
      | sed -n 's/.*refs: *\([0-9,]*\).*/\1/p' | tr -d , | sed "s/^/$shape $calls /"
  done
done
"""


def parse_counts(text):
    """{'x86_64-gcc<N>': {shape: per-call}} from COUNT_SCRIPT's output."""
    major = re.search(r"^gcc (\d+)$", text, re.M)
    if major is None:
        raise RuntimeError(f"no compiler version in the count output:\n{text}")
    totals = {}
    for shape, calls, refs in re.findall(r"^(\w+) (\d+) (\d+)$", text, re.M):
        totals.setdefault(shape, {})[int(calls)] = int(refs)
    lo, hi = CALLS
    per_call = {s: (t[hi] - t[lo]) // (hi - lo) for s, t in totals.items() if lo in t and hi in t}
    missing = [s for s in SHAPES if s not in per_call]
    if missing:
        raise RuntimeError(f"no count for {missing}:\n{text}")
    out = {f"calls/x86_64-gcc{major.group(1)}": per_call}
    sizes = re.search(r"^sizes (\S+) (\d+) (\d+) (\d+)$", text, re.M)
    if sizes is not None:
        out[f"sizes/x86_64-{sizes.group(1)}"] = {"regex": int(sizes.group(2)), "immutables": int(sizes.group(3)),
                                                 "state": int(sizes.group(4))}
    return out


def measure_calls():
    """Per-call counts natively on x86-64 Linux, else in the container, else None (and why)."""
    script = COUNT_SCRIPT.replace("SHAPES", " ".join(SHAPES)).replace("CALLS", " ".join(map(str, CALLS)))
    with tempfile.TemporaryDirectory() as tmp:
        probe = pathlib.Path(tmp) / "calls.cpp"
        probe.write_text(CALL_PROBE, encoding="utf-8")
        sizes = pathlib.Path(tmp) / "sizes.cpp"
        sizes.write_text(SIZE_PROBE, encoding="utf-8")
        (pathlib.Path(tmp) / "count.sh").write_text(script, encoding="utf-8")
        native = (platform.system() == "Linux" and platform.machine() == "x86_64"
                  and shutil.which("valgrind") and shutil.which("g++"))
        if native:
            out = run(["sh", str(pathlib.Path(tmp) / "count.sh"), str(ROOT), str(probe), str(sizes)])
        elif shutil.which("docker"):
            if run(["docker", "image", "inspect", IMAGE]).returncode != 0:
                built = run(["docker", "build", "--platform", "linux/amd64", "-t", IMAGE, "-f", str(DOCKERFILE),
                             str(DOCKERFILE.parent)])
                if built.returncode != 0:
                    raise RuntimeError(f"the container did not build:\n{built.stderr}")
            out = run(["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{ROOT}:/src:ro",
                       "-v", f"{tmp}:/w", IMAGE, "sh", "/w/count.sh", "/src", "/w/calls.cpp", "/w/sizes.cpp"])
        else:
            return None, "per-call counts SKIPPED: not x86-64 Linux with valgrind and g++, and no docker"
        if out.returncode != 0:
            raise RuntimeError(f"the count failed:\n{out.stdout}\n{out.stderr}")
        return parse_counts(out.stdout), None


def self_test():
    stamps = {"sizes/arm64-libc++": {"regex": 1904}, "calls/x86_64-gcc13": {"literal": 1000}}
    assert judge({"sizes/arm64-libc++": {"regex": 1904}}, stamps) == []
    assert judge({"sizes/arm64-libc++": {"regex": 1905}}, stamps)              # a byte more fails
    assert judge({"sizes/arm64-libc++": {"regex": 1888}}, stamps)              # and a byte less
    assert judge({"calls/x86_64-gcc13": {"literal": 1020}}, stamps) == []      # within tolerance
    assert judge({"calls/x86_64-gcc13": {"literal": 1021}}, stamps)            # past it, growing
    assert judge({"calls/x86_64-gcc13": {"literal": 979}}, stamps)             # past it, shrinking
    assert judge({"calls/x86_64-gcc14": {"literal": 1000}}, stamps)            # an unstamped compiler
    assert judge({"calls/x86_64-gcc13": {"class": 1000}}, stamps)              # an unstamped shape
    text = "gcc 13\n" + "".join(f"{s} {CALLS[0]} 5000\n{s} {CALLS[1]} 9000\n" for s in SHAPES)
    assert parse_counts(text) == {"calls/x86_64-gcc13": {s: 4 for s in SHAPES}}
    assert parse_counts("sizes libstdc++ 10 20 30\n" + text)["sizes/x86_64-libstdc++"] == {
        "regex": 10, "immutables": 20, "state": 30}
    try:
        parse_counts("gcc 13\nliteral 1000 5000\n")
        raise AssertionError("a missing shape parsed")
    except RuntimeError:
        pass
    print("check_fixed_cost self-test: OK")
    return 0


def main():
    args = sys.argv[1:]
    if args == ["--self-test"]:
        return self_test()
    measured = measure_sizes(shutil.which("c++") or "c++")
    counts, skipped = measure_calls()
    if counts:
        measured.update(counts)
    if args == ["--stamp"]:
        stamps = json.loads(STAMPS.read_text(encoding="utf-8")) if STAMPS.exists() else {}
        stamps.update(measured)
        print(json.dumps(stamps, indent=2, sort_keys=True))
        return 0
    stamps = json.loads(STAMPS.read_text(encoding="utf-8"))
    problems = judge(measured, stamps)
    for key, values in sorted(measured.items()):
        print(f"check-fixed-cost: {key}: " + ", ".join(f"{k} {v}" for k, v in sorted(values.items())))
    if skipped:
        print(f"check-fixed-cost: {skipped}")
    for p in problems:
        print(f"check-fixed-cost: FAIL — {p}")
    if not problems:
        print("check-fixed-cost: OK — sizes and per-call counts hold their stamps")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
