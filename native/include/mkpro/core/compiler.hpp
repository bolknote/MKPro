#pragma once

#include "mkpro/core/result.hpp"

#include <string>

namespace mkpro {

CompileResult compile_source(std::string source, const CompileOptions& options = {});

// Stable, human-diffable digest of the compiled program's observable behavior
// across the built-in equivalence scenarios (the same scenarios the optimizer's
// behavioral-equivalence gate trusts). Two sources that produce the same digest
// behave identically across those scenarios in the emulator. Intended for
// verifying that a source rewrite preserves behavior. Returns one line per
// scenario.
std::string program_behavior_digest(const CompileResult& result);

}  // namespace mkpro
