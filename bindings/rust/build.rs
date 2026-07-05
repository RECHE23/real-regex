// Compiles the header-only REAL engine + the C ABI shim into a static lib the crate links.
// Uses the vendored copy when packaged (crates.io), the sibling repo when built in-tree.
use std::path::Path;

fn main() {
    let (include, capi) = if Path::new("vendor/include").exists() {
        ("vendor/include", "vendor/c")
    } else {
        ("../../include", "../c") // in-tree dev build
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
