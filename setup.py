# Builds the abi3 C++ extension for the REAL Python binding. Run from the
# repository root (the sdist/wheel build entry); metadata lives in
# pyproject.toml. The C++ engine is header-only — headers are in include/.
import os
import shutil
import sys

from setuptools import Extension, setup
from setuptools.command.build_py import build_py

try:  # setuptools >= 70.1 vendors bdist_wheel; older installs get it from wheel
    from setuptools.command.bdist_wheel import bdist_wheel
except ImportError:  # pragma: no cover
    from wheel.bdist_wheel import bdist_wheel


def _sciforge_include():
    """Locate SciForge's binding headers: an explicit SCIFORGE_INCLUDE override, then a
    sibling checkout (local dev wins over a possibly stale pip-installed package), then the
    sciforge-build package."""
    header = os.path.join("sciforge", "binding", "error.hpp")
    env = os.environ.get("SCIFORGE_INCLUDE")
    if env and os.path.exists(os.path.join(env, header)):
        return env
    sibling = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sciforge", "include")
    if os.path.exists(os.path.join(sibling, header)):
        return sibling
    try:
        import sciforge_build
        return sciforge_build.get_include()
    except ImportError:
        raise SystemExit("sciforge headers introuvables : checkout RECHE23/sciforge en sibling, "
                         "ou pip install sciforge-build, ou définir SCIFORGE_INCLUDE")


class build_py_with_headers(build_py):
    """Ships the header-only C++ library inside the package so an installed
    wheel can be located by real.get_include() for C++ integration."""

    def run(self):
        super().run()
        here = os.path.dirname(os.path.abspath(__file__))
        src = os.path.join(here, "include", "real")
        dst = os.path.join(self.build_lib, "real", "include", "real")
        os.makedirs(dst, exist_ok=True)
        for name in sorted(os.listdir(src)):
            if name.endswith(".hpp"):
                shutil.copy2(os.path.join(src, name), os.path.join(dst, name))


class abi3_wheel(bdist_wheel):
    """Forces the stable-ABI tag so one cp310-abi3 wheel serves CPython 3.10+.

    The extension is built against Py_LIMITED_API 3.10; without this the wheel
    would be tagged for the building interpreter only (e.g. cp314), defeating
    the point and breaking the cibuildwheel `build = "cp310-*"` strategy.
    """

    def finalize_options(self):
        super().finalize_options()
        self.py_limited_api = "cp310"


# Compiler flags differ between MSVC and GCC/Clang.
if sys.platform == "win32":
    compile_args = ["/std:c++20", "/O2", "/EHsc", "/Zc:__cplusplus"]
else:
    compile_args = ["-std=c++20", "-O2", "-fvisibility=hidden"]

setup(
    cmdclass={"bdist_wheel": abi3_wheel, "build_py": build_py_with_headers},
    ext_modules=[
        Extension(
            "real._real",
            sources=["python/src/_real.cpp"],
            include_dirs=["include", _sciforge_include()],
            extra_compile_args=compile_args,
            define_macros=[("Py_LIMITED_API", "0x030A0000")],
            py_limited_api=True,
        )
    ],
)
