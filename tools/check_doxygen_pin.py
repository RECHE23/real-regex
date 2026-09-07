#!/usr/bin/env python3
"""The Doxygen version the publishing workflows use is PINNED, and the pin has a witness.

WHY THIS EXISTS. `make doc-check` reproduces "the CI Doxygen" by running apt inside `ubuntu:24.04`
and prints the version it got. The two publishing workflows ran `apt-get install -y doxygen` on
`ubuntu-latest`. Those agreed only while the label MEANT 24.04 -- an accident, never a declaration,
and measured as such (run 34167997850 installed 1.9.8+ds-2ubuntu0.1). The day the label moves, the
tool a developer runs locally and the tool that PUBLISHES stop describing the same Doxygen, and a
docs-only commit skips ci.yml entirely (paths-ignore), so those two workflows are the only gates a
`.dox` edit gets.

Two claims, checked separately because they are separate:

  1. The pin: the job that installs Doxygen runs on `ubuntu-24.04`, so apt's version is bounded.
  2. The witness: the step that asserts the version accepts 1.9.8 and REFUSES anything else.

The assertion is not copied here -- it is EXTRACTED from the YAML that CI executes and run against
stubbed `doxygen` binaries, so editing the workflow reaches this check. Refusing two wrong versions
is its own self-test: a witness that accepts everything is what this file exists to catch, and there
is no quiet mode to hide it in.

WHAT IT CANNOT CATCH, said rather than implied: removing the `runs-on` pin while keeping the
assertion stays GREEN in CI today, because `ubuntu-latest` currently IS 24.04. Only claim 1 above
notices that, which is why the pin is asserted structurally instead of being inferred from a green
run.
"""

import os, pathlib, re, subprocess, sys, tempfile
import yaml

REPO = pathlib.Path(__file__).resolve().parents[1]
FILES = ("docs.yml", "docs-site.yml")
problems = []
#: What was actually exercised, so the OK line can name it instead of asserting a fixed count.
#: A success sentence that claims a skipped input is the same defect as a guard with no witness.
exercised: list[str] = []

for name in FILES:
    doc = yaml.safe_load((REPO / ".github" / "workflows" / name).read_text())
    steps, runs_on = None, None
    for job in doc["jobs"].values():
        for step in job.get("steps", []) or []:
            if "Assert the pinned Doxygen" in (step.get("name") or ""):
                steps, runs_on = step, job.get("runs-on")
    if steps is None:
        problems.append(f"{name}: no 'Assert the pinned Doxygen' step — the witness is gone")
        continue
    if runs_on != "ubuntu-24.04":
        problems.append(f"{name}: the doxygen job runs on {runs_on!r}, not 'ubuntu-24.04' — the "
                        f"install is unbounded again, so apt's version is whatever the label means")
    script = steps["run"]
    exercised.append(name)
    for version, must_pass in (("1.9.8", True), ("1.10.0", False), ("1.9.7", False)):
        with tempfile.TemporaryDirectory() as tmp:
            stub = pathlib.Path(tmp) / "doxygen"
            stub.write_text(f"#!/bin/sh\necho '{version} (abc123)'\n")
            stub.chmod(0o755)
            env = dict(os.environ, PATH=f"{tmp}:{os.environ['PATH']}")
            done = subprocess.run(["sh", "-c", script], env=env, capture_output=True, text=True)
            passed = done.returncode == 0
            if passed != must_pass:
                problems.append(f"{name}: with doxygen {version} the assertion "
                                f"{'passed' if passed else 'failed'}, expected "
                                f"{'pass' if must_pass else 'fail'}")
            if not must_pass and passed:
                problems.append(f"{name}: {version} accepted — the witness is blind")

# The third place the same number lives: SciForge's doxygen-check.sh, which `make doc-check` runs
# from the SIBLING WORKING TREE (SCIFORGE_TOOLS ?= ../sciforge/tools) -- no workflow invokes it, so
# it is the DEVELOPER's tool while the workflows above are the PUBLISHER's. One number, two owners,
# and the failure mode is asymmetric: a moved image package that only one side notices leaves a
# developer green while publication goes red, or the reverse.
#
# Absent sibling: skipped with a visible line, never a false green -- the same rule `doc-check`
# itself follows. Its assertion FRAGMENT is extracted and exercised against stubs, exactly as the
# workflow steps are; the surrounding container command is not run, since apt inside Docker is not
# what is under test here.
SCRIPT = REPO.parent / "sciforge" / "tools" / "doxygen-check.sh"
FRAGMENT = re.compile(r"(got=\$\(doxygen --version.*?\n\s*fi\n)", re.S)

if not SCRIPT.is_file():
    print(f"check-doxygen-pin: SKIPPED for {SCRIPT} — sibling absent (the workflow half above ran)")
else:
    script = SCRIPT.read_text()
    want = re.search(r'want_version="([^"]+)"', script)
    if want is None:
        problems.append(f"{SCRIPT.name}: no `want_version=` — the script reports the version again "
                        f"instead of asserting it")
    elif want.group(1) != "1.9.8":
        problems.append(f"{SCRIPT.name}: wants {want.group(1)!r}, the workflows want '1.9.8' — one "
                        f"number, and it moved in one place only")
    fragment = FRAGMENT.search(script)
    if fragment is None:
        problems.append(f"{SCRIPT.name}: no version assertion found; it prints the version but "
                        f"cannot fail on it, which is the state this wagon changed")
    else:
        exercised.append(SCRIPT.name)
        for version, must_pass in (("1.9.8", True), ("1.10.0", False), ("1.9.7", False)):
            with tempfile.TemporaryDirectory() as tmp:
                stub = pathlib.Path(tmp) / "doxygen"
                stub.write_text(f"#!/bin/sh\necho '{version}'\n")
                stub.chmod(0o755)
                env = dict(os.environ, PATH=f"{tmp}:{os.environ['PATH']}",
                           WANT="1.9.8", IMAGE="ubuntu:24.04")
                done = subprocess.run(["sh", "-c", fragment.group(1)], env=env,
                                      capture_output=True, text=True)
                if (done.returncode == 0) != must_pass:
                    problems.append(f"{SCRIPT.name}: with doxygen {version} the assertion "
                                    f"{'passed' if done.returncode == 0 else 'failed'}, expected "
                                    f"{'pass' if must_pass else 'fail'}")

if problems:
    print("check-doxygen-pin: FAIL")
    for p in problems:
        print(f"    {p}")
    sys.exit(1)
if not exercised:
    print("check-doxygen-pin: FAIL — nothing was exercised. Every input was missing or unreadable, "
          "and a green here would mean only that there was nothing to check.")
    sys.exit(1)
print(f"check-doxygen-pin: OK — both workflows pin ubuntu-24.04, and the shipped assertion in "
      f"{', '.join(exercised)} accepts 1.9.8 while refusing 1.10.0 / 1.9.7 "
      f"({len(exercised)} exercised)")
