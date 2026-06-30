#pragma once

#include "mkpro/core/result.hpp"

namespace mkpro {

// Test hook for the optimizer candidate acceptance boundary. Production
// selection calls the same static proof gate internally; this exposes the direct
// verifier path without routing tests through compile_source, ProofReport, or
// the emulator-backed behavior digest.
//
// Keep this out of the public compiler wrapper: it is a verifier test surface,
// not a production optimizer input.
bool optimizer_static_proof_gate_accepts_for_testing(const CompileOptions& candidate_options,
                                                     const CompileResult& result);

}  // namespace mkpro
