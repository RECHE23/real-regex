// The Features-matrix CI probe: makes "executable claims" literal by
// turning every docs/site/data/features.yaml row with a non-null `pattern` into a
// compiled + run assertion. Native real::regex ONLY -- never real::compat/policy::fallback,
// which would delegate a rejected pattern to std::regex and mask the excluded-by-design
// throw this probe exists to prove.
//
// This file is the stable, hand-written half (includes + framework wiring only); every
// TEST() case is GENERATED from features.yaml by tools/gen_features.py into
// features_probe_generated.inc (drift-checked via `python3 tools/gen_features.py
// --check`, wired into `make docs-site-gate` -- see docs/Makefile). Never hand-edit the
// .inc; regenerate it instead.
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

#include "features_probe_generated.inc"
