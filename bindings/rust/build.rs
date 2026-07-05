// Compiles the header-only REAL engine + the C ABI shim into a static lib the crate links.
// In-tree (the monorepo) always uses the LIVE sibling sources; the vendored copy is used only when packaged
// (off crates.io, where the sibling does not exist). Checking the sibling first — not vendor/ first — means a
// stale vendor/ left by `make crate-vendor` can never silently shadow the live shim during development.
use std::path::Path;

// Emit a rerun-if-changed for every header under `dir`. The shim is a thin .cpp that #includes the whole
// header-only engine, so an engine-header edit does not change the .cpp — without this, cc reuses its cached
// object and the crate silently tests stale engine behaviour (the trap that hid the dollar_endonly change).
fn watch_headers(dir: &Path) {
    let Ok(entries) = std::fs::read_dir(dir) else { return };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            watch_headers(&path);
        } else if path.extension().is_some_and(|e| e == "hpp" || e == "h") {
            println!("cargo:rerun-if-changed={}", path.display());
        }
    }
}

fn main() {
    let (include, capi) = if Path::new("../../include/real").exists() {
        ("../../include", "../c") // in-tree: the live sibling sources
    } else {
        ("vendor/include", "vendor/c") // packaged: the vendored copy
    };
    println!("cargo:rerun-if-changed={capi}/real_capi.cpp");
    println!("cargo:rerun-if-changed={capi}/real_capi.h");
    watch_headers(Path::new(&format!("{include}/real")));
    cc::Build::new()
        .cpp(true)
        .std("c++20")
        .include(include)
        .include(capi)
        .file(format!("{capi}/real_capi.cpp"))
        .compile("real_capi");
}
