#pragma once

#include "mkpro/core/result.hpp"

#include <string>

namespace mkpro {

// Stable, human-diffable digest of the compiled program's observable behavior
// across built-in emulator scenarios. Intended for CLI/debug/regression checks:
// optimizer candidate acceptance must use local static proof obligations instead
// of this emulator-backed digest. Implemented outside compiler.cpp so the
// optimizer translation unit does not depend on the emulator harness.
//
// Include this header only from explicit debug/regression surfaces such as the
// CLI or tests. Optimizer/core passes must not include it to justify candidate
// selection; they should expose a local proof obligation instead. Returns one
// line per scenario.
std::string program_behavior_digest(const CompileResult& result);
std::string program_behavior_digest(const CompileResult& result, FeatureProfile feature_profile);

}  // namespace mkpro
