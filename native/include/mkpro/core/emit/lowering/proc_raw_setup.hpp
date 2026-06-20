#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/emit/lowering_helpers.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/result.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::core::emit::lowering {

struct DecimalSeriesProgram {
  int digits = 0;
  int counter_start = 0;
  int line = 0;
};

std::optional<DecimalSeriesProgram> match_decimal_series_program(const V2Program& program);

bool lower_decimal_series_program(MachineEmitter& emitter, std::vector<Diagnostic>& diagnostics,
                                  const DecimalSeriesProgram& program);

std::string random_unique_coord_list_value(const std::string& domain, const std::string& list_name,
                                           int index, int count,
                                           bool scaled_decimal = false);
std::string segmented_bitplane_random_unique_value(const std::string& collection,
                                                   const std::string& count_source,
                                                   int plane_index);

SetupProgramReport
compile_setup_program_with_preloads(const std::map<std::string, const V2Board*>& boards,
                                    const std::map<std::string, std::string>& registers,
                                    const std::vector<PreloadReport>& preloads,
                                    const CompileOptions& options);

} // namespace mkpro::core::emit::lowering
