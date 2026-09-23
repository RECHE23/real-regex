"""Type-checked, never executed: what a caller of ``re`` writes against ``real`` must type-check.

``mypy.stubtest`` compares the stub with the runtime object by object, and it cannot see a base class
that is too general: ``class error(Exception)`` satisfies it while the runtime class derives from
``re.error``. So the drop-in promise is stated here as code a type checker reads — catching
``re.error``, reading its fields, passing the exception where ``re.error`` is expected — and
``make python-stubtest`` runs ``mypy --strict`` over it.
"""

import re

import real


def reads_the_fields_re_documents(exc: real.error) -> tuple[str, str | bytes | None, int | None, int | None, int | None]:
    return exc.msg, exc.pattern, exc.pos, exc.lineno, exc.colno


def is_an_re_error(exc: real.error) -> re.error:
    return exc


def the_alias_is_the_same_class(exc: real.PatternError) -> real.error:
    return exc


def caught_as_re_error(pattern: str) -> int | None:
    try:
        real.compile(pattern)
    except re.error as exc:
        return exc.pos
    return None
