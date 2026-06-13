# Builds the abi3 C++ extension for the REAL Python binding. Run from the
# repository root (the sdist/wheel build entry); metadata lives in
# pyproject.toml. The C++ engine is header-only — headers are in include/.
import sys

from setuptools import Extension, setup

try:  # setuptools >= 70.1 vendors bdist_wheel; older installs get it from wheel
    from setuptools.command.bdist_wheel import bdist_wheel
except ImportError:  # pragma: no cover
    from wheel.bdist_wheel import bdist_wheel


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
    cmdclass={"bdist_wheel": abi3_wheel},
    ext_modules=[
        Extension(
            "real._real",
            sources=["python/src/_real.cpp"],
            include_dirs=["include"],
            extra_compile_args=compile_args,
            define_macros=[("Py_LIMITED_API", "0x030A0000")],
            py_limited_api=True,
        )
    ],
)
