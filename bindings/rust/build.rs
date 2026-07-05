// Compiles the header-only REAL engine + the C ABI shim into a static lib the crate links.
// In-tree (the monorepo) always uses the LIVE sibling sources; the vendored copy is used only when packaged
// (off crates.io, where the sibling does not exist). Checking the sibling first — not vendor/ first — means a
// stale vendor/ left by `make crate-vendor` can never silently shadow the live shim during development.
use std::path::Path;

fn main() {
    let (include, capi) = if Path::new("../../include/real").exists() {
        ("../../include", "../c") // in-tree: the live sibling sources
    } else {
        ("vendor/include", "vendor/c") // packaged: the vendored copy
    };
    println!("cargo:rerun-if-changed={capi}/real_capi.cpp");
    println!("cargo:rerun-if-changed={capi}/real_capi.h");
    cc::Build::new()
        .cpp(true)
        .std("c++20")
        .include(include)
        .include(capi)
        .file(format!("{capi}/real_capi.cpp"))
        .compile("real_capi");
}
