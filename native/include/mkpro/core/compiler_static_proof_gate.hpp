#pragma once

#include "mkpro/core/result.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mkpro {

// Test hook for the optimizer candidate acceptance boundary. Production
// selection calls the same static proof gate internally; this exposes the direct
// verifier path without routing tests through compile_source, ProofReport, or
// the emulator-backed behavior digest.
//
// Keep this out of the public compiler wrapper: it is a verifier test surface,
// not a production optimizer input.
std::vector<std::string> demotable_indirect_flow_preload_values_for_testing(
    const CompileResult& result, const std::set<std::string>& suppressed);

bool optimizer_static_proof_gate_accepts_for_testing(const CompileOptions& candidate_options,
                                                     const CompileResult& result);

std::optional<std::string> optimizer_static_proof_gate_rejection_reason_for_testing(
    const CompileOptions& candidate_options, const CompileResult& result);

}  // namespace mkpro
