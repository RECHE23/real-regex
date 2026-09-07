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

if problems:
    print("check-doxygen-pin: FAIL")
    for p in problems:
        print(f"    {p}")
    sys.exit(1)
print(f"check-doxygen-pin: OK — both workflows pin ubuntu-24.04 and their shipped assertion "
      f"accepts 1.9.8 and refuses 1.10.0 / 1.9.7")
