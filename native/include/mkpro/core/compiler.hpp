#pragma once

#include "mkpro/core/result.hpp"

#include <string>

namespace mkpro {

CompileResult compile_source(std::string source, const CompileOptions& options = {});

// Exact compiler-owned cache identity, including runtime contract, lowering,
// target model and search policy. A key is bookkeeping, never a proof.
std::string compiler_options_fingerprint(const CompileOptions& options);

}  // namespace mkpro
