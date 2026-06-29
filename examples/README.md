# REAL examples

Small, copy-pasteable programs. The C++ ones compile against an installed REAL via
`find_package(real CONFIG REQUIRED)`.

| Example | What it shows |
| --- | --- |
| [`cpp/hello.cpp`](cpp/hello.cpp) | construct a `real::regex`, `search`, read the matched span |
| [`cpp/redos_demo.cpp`](cpp/redos_demo.cpp) | `(a+)+b` over 100k `'a'` stays linear — no ReDoS |
| [`python/dropin.py`](python/dropin.py) | `import real as re` — `search` / `findall` / `sub` (supported subset) |

## Build the C++ examples

Against an installed REAL (`brew`, `vcpkg`, or `cmake --install`):

    cmake -S . -B build -DCMAKE_PREFIX_PATH=<install-prefix>
    cmake --build build
    ./build/hello && ./build/redos_demo

Or compile one directly (header-only, C++20):

    c++ -std=c++20 $(pkg-config --cflags real) cpp/hello.cpp -o hello

## Run the Python example

    pip install real-regex
    python python/dropin.py
