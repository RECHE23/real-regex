# REAL examples

Small, copy-pasteable programs. Every `cpp/*.cpp` compiles against an installed
REAL via `find_package(real CONFIG REQUIRED)` — `CMakeLists.txt` GLOBs them;
`install-smoke` step (e) prints `N of M` and fails if they differ. A handwritten
target list is how seven of ten used to miss the installed-package proof.

| Example | What it shows |
| --- | --- |
| [`cpp/hello.cpp`](cpp/hello.cpp) | construct a `real::regex`, `search`, read the matched span |
| [`cpp/redos_demo.cpp`](cpp/redos_demo.cpp) | `(a+)+b` over 100k `'a'` stays linear — no ReDoS |
| [`cpp/reference_*.cpp`](cpp/) | the `/reference/` page examples (same glob, same proof) |
| [`python/dropin.py`](python/dropin.py) | `import real as re` — `search` / `findall` / `sub` (supported subset) |

## Build the C++ examples

Against an installed REAL (`brew`, `vcpkg`, or `cmake --install`):

    cmake -S . -B build -DCMAKE_PREFIX_PATH=<install-prefix>
    cmake --build build

Or compile one directly (header-only, C++20):

    c++ -std=c++20 $(pkg-config --cflags real) cpp/hello.cpp -o hello

## Run the Python example

    pip install real-regex
    python python/dropin.py
