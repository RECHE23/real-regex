r"""REAL — Regular Expression Algorithmic Library.

A linear-time (ReDoS-safe) regex engine with an `re`-compatible API:

    import real
    real.search(r"(\d{4})-(\d{2})", text)
    real.compile(r"\w+").findall(text)

Supported flags: IGNORECASE/I, MULTILINE/M, DOTALL/S (ASCII/A and UNICODE/U
are accepted no-ops: classes are ASCII, text is Unicode). Unsupported re
features raise real.error at compile time: lookarounds, backreferences,
re.X/re.L, and Match.expand/pos/endpos. See the project README.
"""

import functools
import os

from real._real import Match, Pattern, compile as _compile_core, error

__all__ = [
    "compile", "match", "fullmatch", "search", "findall", "finditer",
    "split", "sub", "subn", "escape", "purge", "error", "Pattern", "Match",
    "A", "ASCII", "I", "IGNORECASE", "M", "MULTILINE", "S", "DOTALL",
    "U", "UNICODE", "NOFLAG", "get_include", "get_config",
]

__version__ = "2026.6.2"

NOFLAG = 0
I = IGNORECASE = 2
M = MULTILINE = 8
S = DOTALL = 16
U = UNICODE = 32
A = ASCII = 256


def get_include():
    """Return the directory to add to a C++ include path so that
    ``#include <real/real.hpp>`` resolves.

    The header-only C++ library is shipped inside the installed package, so a
    project can compile against REAL located through its Python install:

        c++ -std=c++20 $(python -c "import real; print(real.get_include())") …

    Falls back to the repository's ``include/`` when imported from a source
    checkout.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    packaged = os.path.join(here, "include")
    if os.path.isdir(os.path.join(packaged, "real")):
        return packaged
    return os.path.normpath(os.path.join(here, os.pardir, os.pardir, "include"))


def get_config():
    """Return metadata for embedding the C++ library.

    Keys: ``version``, ``include`` (see :func:`get_include`) and
    ``cxx_standard`` (the language standard the headers require).
    """
    return {
        "version": __version__,
        "include": get_include(),
        "cxx_standard": "c++20",
    }


@functools.lru_cache(maxsize=512)
def _compile_cached(pattern, flags):
    return _compile_core(pattern, flags)


def compile(pattern, flags=0):  # noqa: A001 - mirrors re.compile
    """Compile a pattern (str or bytes) into a real.Pattern."""
    if isinstance(pattern, Pattern):
        if flags:
            raise ValueError("cannot process flags argument with a compiled pattern")
        return pattern
    return _compile_cached(pattern, flags)


def purge():
    """Clear the compiled-pattern cache (like re.purge)."""
    _compile_cached.cache_clear()


def match(pattern, string, flags=0):
    return compile(pattern, flags).match(string)


def fullmatch(pattern, string, flags=0):
    return compile(pattern, flags).fullmatch(string)


def search(pattern, string, flags=0):
    return compile(pattern, flags).search(string)


def findall(pattern, string, flags=0):
    return compile(pattern, flags).findall(string)


def finditer(pattern, string, flags=0):
    return compile(pattern, flags).finditer(string)


def split(pattern, string, maxsplit=0, flags=0):
    return compile(pattern, flags).split(string, maxsplit)


def sub(pattern, repl, string, count=0, flags=0):
    return compile(pattern, flags).sub(repl, string, count)


def subn(pattern, repl, string, count=0, flags=0):
    return compile(pattern, flags).subn(repl, string, count)


_SPECIALS = frozenset("()[]{}?*+-|^$\\.&~# \t\n\r\v\f")


def escape(pattern):
    """Escape special characters in a pattern (like re.escape)."""
    if isinstance(pattern, bytes):
        text = pattern.decode("latin1")
        escaped = "".join("\\" + c if c in _SPECIALS else c for c in text)
        return escaped.encode("latin1")
    return "".join("\\" + c if c in _SPECIALS else c for c in pattern)
