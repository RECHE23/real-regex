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
    process start cancel; and one walk of a sixth, `iterate` (find_iter over 64 KiB reading a group of a
    pattern that is not one-pass, so each confirmed window's groups are filled), as the difference between
    four walks and two. Counted, not timed: the count is exact, where a stopwatch's floor is the size of
    these effects. Only the probe's own instructions count -- REAL's code, inlined there -- and not the C
    library's: glibc picks its memchr and memcmp by the CPU's vector features, which differ between an
    emulated host and a runner, and between runners. The count is still the compiler's, so the stamp is keyed by system
    and GCC major version, and the one CI judges is taken on CI's own: natively on its runner, or here in
    tools/fixed_cost.Dockerfile, which is that runner's Ubuntu (amd64, emulated where the host is not:
    valgrind counts the guest's instructions, so emulation changes the time, not the count). Within
    TOLERANCE either way. A system without a stamp fails rather than passes: restamp from its image.

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
IMAGE = "real-regex-fixed-cost:ubuntu-24.04"
SHAPES = ("literal", "alternation", "anchored", "class", "miss", "iterate")
TOLERANCE = 0.02  # within it, a stamp holds: GCC patch releases move a count by less than this
CALLS = (1000, 2000)
WALKS = (2, 4)  # "iterate" counts whole walks over 64 KiB, not short calls

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
#include <string>
#include <string_view>
#include <real/real.hpp>
// One function per shape, kept out of line, so that adding a shape does not move another's inlining.
namespace {
  [[gnu::noinline]] long calls_of(const real::regex& re, std::string_view subject, bool anchored, long calls)
  {
    long hits {0};
    for (long i = 0; i < calls; ++i) {
      hits += (anchored ? re.match(subject) : re.search(subject)).matched() ? 1 : 0;
    }
    return hits;
  }
  [[gnu::noinline]] long literal(long n)     { return calls_of(real::regex {"abc"}, "xxxxabcxxxx", false, n); }
  [[gnu::noinline]] long alternation(long n) { return calls_of(real::regex {"cat|dog|eel"}, "a dog here", false, n); }
  [[gnu::noinline]] long anchored(long n)    { return calls_of(real::regex {"[a-z]+[0-9]"}, "abc1", true, n); }
  [[gnu::noinline]] long klass(long n)       { return calls_of(real::regex {"\\d+"}, "ab 42 cd", false, n); }
  [[gnu::noinline]] long miss(long n)        { return calls_of(real::regex {"zq+"}, "a subject of forty bytes, no hit in it", false, n); }
  // A walk that reads a group of a pattern that is not one-pass, over a subject long enough for the
  // inner-literal route: the groups of each confirmed window.
  [[gnu::noinline]] long iterate(long walks)
  {
    std::string subject;
    while (subject.size() < 65536) { subject += "some filler words here and there, error 2026-06-13 req=a3f9c1d8 x\n"; }
    const real::regex re {"(info|error|warn)\\s+\\d{4}-\\d{2}-\\d{2}\\s+req=[a-f0-9]+"};
    long hits {0};
    for (long i = 0; i < walks; ++i) {
      for (const auto& m : re.find_iter(subject)) { hits += m.str(1).empty() ? 0 : 1; }
    }
    return hits;
  }
}
int main(int argc, char** argv)
{
  if (argc != 3) { return 2; }
  const std::string_view shape {argv[1]};
  const long             n     {std::atol(argv[2])};
  long                   hits  {-1};
  long                   want  {n};
  if (shape == "literal")          { hits = literal(n); }
  else if (shape == "alternation") { hits = alternation(n); }
  else if (shape == "anchored")    { hits = anchored(n); }
  else if (shape == "class")       { hits = klass(n); }
  else if (shape == "miss")        { hits = miss(n); want = 0; }
  else if (shape == "iterate")     { hits = iterate(n); want = hits > 0 && hits % n == 0 ? hits : -2; }
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
. /etc/os-release
echo "os $ID-$VERSION_ID"
for shape in SHAPES; do
  counts="CALLS"
  [ "$shape" = iterate ] && counts="WALKS"
  for calls in $counts; do
    valgrind --tool=callgrind --callgrind-out-file=/tmp/fixed_cost.cg /tmp/fixed_cost_probe "$shape" "$calls" \
      >/dev/null 2>&1
    callgrind_annotate --threshold=100 --show-percs=no /tmp/fixed_cost.cg 2>/dev/null \
      | awk -v shape="$shape" -v calls="$calls" \
          '/\[\/tmp\/fixed_cost_probe\]$/ { gsub(",", "", $1); sum += $1 } END { print shape, calls, sum + 0 }'
  done
done
"""


def parse_counts(text):
    """{'calls/x86_64-<os>-gcc<N>': {shape: per-call}} from COUNT_SCRIPT's output."""
    major = re.search(r"^gcc (\d+)$", text, re.M)
    system = re.search(r"^os (\S+)$", text, re.M)
    if major is None or system is None:
        raise RuntimeError(f"no compiler version or system in the count output:\n{text}")
    totals = {}
    for shape, calls, refs in re.findall(r"^(\w+) (\d+) (\d+)$", text, re.M):
        totals.setdefault(shape, {})[int(calls)] = int(refs)
    per_call = {}
    for s, t in totals.items():
        lo, hi = WALKS if s == "iterate" else CALLS
        if lo in t and hi in t:
            per_call[s] = (t[hi] - t[lo]) // (hi - lo)
    missing = [s for s in SHAPES if s not in per_call]
    if missing:
        raise RuntimeError(f"no count for {missing}:\n{text}")
    out = {f"calls/x86_64-{system.group(1)}-gcc{major.group(1)}": per_call}
    sizes = re.search(r"^sizes (\S+) (\d+) (\d+) (\d+)$", text, re.M)
    if sizes is not None:
        out[f"sizes/x86_64-{sizes.group(1)}"] = {"regex": int(sizes.group(2)), "immutables": int(sizes.group(3)),
                                                 "state": int(sizes.group(4))}
    return out


def measure_calls():
    """Per-call counts natively on x86-64 Linux, else in the container, else None (and why)."""
    script = (COUNT_SCRIPT.replace("SHAPES", " ".join(SHAPES)).replace("WALKS", " ".join(map(str, WALKS)))
              .replace("CALLS", " ".join(map(str, CALLS))))
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
    stamps = {"sizes/arm64-libc++": {"regex": 1904}, "calls/x86_64-ubuntu-24.04-gcc13": {"literal": 1000}}
    assert judge({"sizes/arm64-libc++": {"regex": 1904}}, stamps) == []
    assert judge({"sizes/arm64-libc++": {"regex": 1905}}, stamps)              # a byte more fails
    assert judge({"sizes/arm64-libc++": {"regex": 1888}}, stamps)              # and a byte less
    key = "calls/x86_64-ubuntu-24.04-gcc13"
    assert judge({key: {"literal": 1020}}, stamps) == []                       # within tolerance
    assert judge({key: {"literal": 1021}}, stamps)                             # past it, growing
    assert judge({key: {"literal": 979}}, stamps)                              # past it, shrinking
    assert judge({"calls/x86_64-ubuntu-24.04-gcc14": {"literal": 1000}}, stamps)  # an unstamped compiler
    assert judge({"calls/x86_64-debian-12-gcc13": {"literal": 1000}}, stamps)     # an unstamped system
    assert judge({key: {"class": 1000}}, stamps)                               # an unstamped shape
    text = "gcc 13\nos ubuntu-24.04\n" + "".join(
        f"{s} {(WALKS if s == 'iterate' else CALLS)[0]} 5000\n{s} {(WALKS if s == 'iterate' else CALLS)[1]} 9000\n"
        for s in SHAPES)
    want = {s: 4 for s in SHAPES}
    want["iterate"] = 4000 // (WALKS[1] - WALKS[0])
    assert parse_counts(text) == {key: want}
    assert parse_counts("sizes libstdc++ 10 20 30\n" + text)["sizes/x86_64-libstdc++"] == {
        "regex": 10, "immutables": 20, "state": 30}
    try:
        parse_counts("gcc 13\nos ubuntu-24.04\nliteral 1000 5000\n")
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
