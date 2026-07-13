// engine_version.cpp -- Go-binding-internal helper, NOT part of the frozen C ABI
// (bindings/c/real_capi.h/.cpp). Exposes the vendored engine's own version string to cgo
// (see real.go's EngineVersion) via a minimal extern "C" function, since real_capi.h has no
// version accessor and is not extended here.
#include "real/version.hpp"

extern "C" const char* real_go_engine_version(void)
{
  return REAL_VERSION_STRING;
}
