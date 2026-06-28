#pragma once

#include "mkpro/core/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mkpro {

std::string format_hex_steps(const std::vector<ResolvedStep>& steps);
std::string format_listing_steps(const std::vector<ResolvedStep>& steps);
std::string format_flow_steps(const std::vector<ResolvedStep>& steps, bool color = false);
std::string format_flow_steps(const std::vector<ResolvedStep>& steps,
                              const std::vector<PreloadReport>& preloads, bool color = false);
std::string format_flow_result(const CompileResult& result, bool color = false);
std::string format_setup_listing_steps(const std::vector<ManualSetupInput>& manual_inputs,
                                       const std::vector<ResolvedStep>& steps);
std::optional<std::string>
format_setup_preload_listing_steps(const std::vector<PreloadReport>& preloads);
std::string format_program_tokens(const std::vector<ResolvedStep>& steps);
std::optional<std::string> format_setup_block(const std::vector<PreloadReport>& preloads);
std::string to_keycaps(std::string mnemonic);

} // namespace mkpro
