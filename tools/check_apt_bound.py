#!/usr/bin/env python3
"""Workflows refresh only Ubuntu apt indexes — via tools/ci-apt-ubuntu.sh.

A raw `apt-get update &&` on the GitHub Ubuntu image also hits third-party
indexes (microsoft-prod, docker, …). A Hash Sum mismatch on any of those
failed docs.yml / docs-site.yml twice on 2026-09-09, even though every
package this repo installs from apt is Ubuntu's. The script drops those
sources before updating; this check refuses a workflow that goes around it.

Two claims:

  1. No workflow line runs `apt-get update` except by calling the script.
  2. The script still performs the bound (keeps `ubuntu*` sources, then
     `apt-get update`). Emptying it, or turning it into a bare update,
     is a red — otherwise the workflows would stay green while the bound
     vanished.

`--self-test` drives each arm on its own and then exercises the verbose
failure path, so a green here is not a hand-run that nobody will repeat.
The script claim has three arms (no `apt-get update`, no `ubuntu*` keep-list,
no find). A bare update trips two of those at once, so that shape is not
used — each is a separate case, and each case must name only its own arm.
The live tree is checked by a second invocation without the flag, same
shape as check_doc_mirror / check_tolerated_count / check_stdlib_attribution
/ check_go_version_labels.
"""

from __future__ import annotations

import contextlib
import io
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
WORKFLOWS = REPO / ".github" / "workflows"
SCRIPT = REPO / "tools" / "ci-apt-ubuntu.sh"


def workflow_paths() -> list[pathlib.Path]:
    return sorted(WORKFLOWS.glob("*.yml"))


def run(*,
        workflow_texts: dict[pathlib.Path, str] | None = None,
        script_text: str | None = None,
        quiet: bool = False) -> int:
    """Both claims, against the live tree or injected texts."""
    problems: list[str] = []
    calls = 0

    if script_text is None and not SCRIPT.is_file():
        problems.append(f"{SCRIPT.relative_to(REPO)} is gone — the bound has nowhere to live")
        text = ""
    else:
        text = SCRIPT.read_text() if script_text is None else script_text
        if "apt-get update" not in text:
            problems.append(f"{SCRIPT.name}: no `apt-get update` — it no longer refreshes indexes")
        if "ubuntu.*" not in text and "ubuntu*" not in text:
            problems.append(f"{SCRIPT.name}: no `ubuntu*` keep-list — the bound is gone")
        if "find /etc/apt/sources.list.d" not in text:
            problems.append(f"{SCRIPT.name}: no find over sources.list.d — nothing is dropped")

    texts = workflow_texts if workflow_texts is not None else {p: p.read_text()
                                                              for p in workflow_paths()}
    for path, body in texts.items():
        for lineno, line in enumerate(body.splitlines(), 1):
            if "apt-get update" in line and "ci-apt-ubuntu.sh" not in line:
                problems.append(f"{path.name}:{lineno}: unbounded `apt-get update` — use "
                                f"tools/ci-apt-ubuntu.sh")
            calls += len(re.findall(r"ci-apt-ubuntu\.sh", line))

    if calls == 0 and (script_text is not None or SCRIPT.is_file()):
        problems.append("no workflow calls ci-apt-ubuntu.sh — the script is dead, the bound is theatre")

    if problems:
        if not quiet:
            print("check-apt-bound: FAIL")
            for p in problems:
                print(f"    {p}")
        return 1

    if not quiet:
        print(f"check-apt-bound: OK — {calls} call(s) in {len(texts)} "
              f"workflow(s), all through {SCRIPT.relative_to(REPO)}")
    return 0


def self_test() -> int:
    """Replay each arm on its own, then the verbose failure path.

    The script check has THREE arms. A script that only `apt-get update`s trips
    the keep-list and the find together, so each is driven on its own below —
    and the printed diagnostic of each case must name that arm alone. An exit
    status of 1 with two problems still listed would leave the self-test green
    after one of those arms went blind.
    """
    if not SCRIPT.is_file():
        print("check-apt-bound: SELF-TEST INCONCLUSIVE — tools/ci-apt-ubuntu.sh is gone")
        return 1
    paths = workflow_paths()
    if not paths:
        print("check-apt-bound: SELF-TEST INCONCLUSIVE — no workflow files")
        return 1
    live_script = SCRIPT.read_text()
    live_wf = {p: p.read_text() for p in paths}

    # Workflow arm — one line, live script intact.
    target = next((p for p in paths if p.name == "release.yml"), paths[0])
    dirty_wf = dict(live_wf)
    dirty_wf[target] = live_wf[target] + (
        "\n      - run: sudo apt-get update && sudo apt-get install -y wget\n")
    if run(workflow_texts=dirty_wf, script_text=live_script, quiet=True) == 0:
        print("check-apt-bound: SELF-TEST FAILED — an unbounded `apt-get update &&` in "
              f"{target.name} did NOT trip the check.")
        return 1

    # Three ways to cease to bound. Each script carries the other two tokens so
    # blinding one arm cannot be hidden by a sibling still firing.
    script_cases: list[tuple[str, str, str, tuple[str, ...]]] = [
        ("bounds but no longer updates",
         "#!/bin/sh\n"
         "sudo find /etc/apt/sources.list.d -maxdepth 1 -type f "
         "! -name 'ubuntu.*' -delete\n",
         "no `apt-get update`",
         ("keep-list", "no find")),
        ("updates and finds, no ubuntu* keep-list",
         "#!/bin/sh\n"
         "sudo find /etc/apt/sources.list.d -maxdepth 1 -type f -delete\n"
         "sudo apt-get update\n",
         "keep-list",
         ("no `apt-get update`", "no find")),
        ("updates and keeps ubuntu*, no find",
         "#!/bin/sh\n"
         "# keep ubuntu.* ubuntu-*\n"
         "sudo apt-get update\n",
         "no find",
         ("no `apt-get update`", "keep-list")),
    ]
    for label, script, must, must_not in script_cases:
        out, err = io.StringIO(), io.StringIO()
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = run(workflow_texts=live_wf, script_text=script, quiet=False)
        except Exception as exc:  # noqa: BLE001 — any exception here IS the defect
            print(f"check-apt-bound: SELF-TEST FAILED — {label} raised "
                  f"{type(exc).__name__}: {exc}")
            return 1
        printed = out.getvalue()
        if code == 0:
            print(f"check-apt-bound: SELF-TEST FAILED — {label} did NOT trip the check.")
            return 1
        if err.getvalue().strip():
            print(f"check-apt-bound: SELF-TEST FAILED — {label} wrote to stderr: "
                  f"{err.getvalue().strip()[:80]!r}")
            return 1
        if must not in printed:
            print(f"check-apt-bound: SELF-TEST FAILED — {label} tripped but never "
                  f"named {must!r}:")
            for line in printed.strip().split("\n"):
                print(f"      {line}")
            return 1
        extras = [token for token in must_not if token in printed]
        if extras:
            print(f"check-apt-bound: SELF-TEST FAILED — {label} also named "
                  f"{extras}; the arms are not isolated, so blinding one would "
                  "still leave this case red:")
            for line in printed.strip().split("\n"):
                print(f"      {line}")
            return 1

    # The live tree must still be green, or the cases above prove nothing about THIS tree.
    if run(workflow_texts=live_wf, script_text=live_script, quiet=True) != 0:
        print("check-apt-bound: SELF-TEST FAILED — the live tree is already red, so the "
              "synthetic trips cannot be trusted.")
        return 1

    # The print path: a green-only guard can leave a stale message. Run the
    # workflow sabotage loud and require the diagnostic to name the file and
    # the unbounded form.
    out, err = io.StringIO(), io.StringIO()
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = run(workflow_texts=dirty_wf, script_text=live_script, quiet=False)
    except Exception as exc:  # noqa: BLE001 — any exception here IS the defect
        print(f"check-apt-bound: SELF-TEST FAILED — the failure path raised "
              f"{type(exc).__name__}: {exc}")
        return 1
    printed, stderr = out.getvalue(), err.getvalue()
    problems = [f"exit {code}, expected 1"] if code != 1 else []
    if stderr.strip():
        problems.append(f"wrote to stderr: {stderr.strip()[:80]!r}")
    for token in (target.name, "unbounded", "apt-get update"):
        if token not in printed:
            problems.append(f"the message never names {token!r}")
    if problems:
        print("check-apt-bound: SELF-TEST FAILED — the verbose failure path is wrong:")
        for problem in problems:
            print(f"    {problem}")
        for line in printed.strip().split("\n"):
            print(f"      {line}")
        return 1

    print("check-apt-bound: self-test OK — an unbounded workflow line trips; a "
          "script that no longer updates, that drops the ubuntu* keep-list, or "
          "that drops the find each trips on that arm alone; the live tree "
          f"stays green; and the failure path names {target.name} and the "
          "unbounded form.")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else run())
