#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"

#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace mkpro::core::emit::lowering {

struct CoordListIndirectContext {
  Expression cell;
  int count = 0;
  int pointer_start = 0;
  std::vector<int> item_registers;
  std::vector<std::string> item_names;
  std::string pointer;
  std::string counter;
};

bool is_preincrement_indirect_register(int index);

std::optional<std::vector<std::string>> coord_list_items_for(
    const std::map<std::string, const V2StateField*>& state_fields,
    const std::string& list_name);

std::optional<CoordListIndirectContext> coord_list_indirect_context(
    const std::map<std::string, const V2StateField*>& state_fields,
    const std::map<std::string, int>& register_index_by_name, const Expression& call);

struct CoordListEmitApi {
  MachineEmitter& emitter;
  std::function<bool(const Expression&)> lower_expression_to_x;
  std::function<void(const std::string&)> emit_recall;
  std::function<void(const std::string&, std::string)> emit_store;
  std::function<void(const std::string&, std::optional<std::string>, std::optional<int>)>
      emit_number_or_preload;
  std::function<int(const std::string&)> register_index_for;
  std::function<std::string(const std::string&)> register_text_for;
  std::function<std::optional<std::pair<int, std::string>>(int)> fl_loop_opcode_for_register;
};

void emit_coord_list_loop_setup(CoordListEmitApi& api,
                                const CoordListIndirectContext& context, int source_line);
void emit_coord_list_indirect_recall(CoordListEmitApi& api,
                                     const CoordListIndirectContext& context, int source_line,
                                     const std::string& comment);
void emit_coord_list_counter_loop(CoordListEmitApi& api, const std::string& counter,
                                  const std::string& target, int source_line,
                                  const std::string& comment);
bool lower_coord_list_remove(CoordListEmitApi& api, const CoordListIndirectContext& context,
                             int source_line);
bool lower_coord_list_line_count_assignment(CoordListEmitApi& api,
                                            const CoordListIndirectContext& context,
                                            const std::string& target, int source_line);

} // namespace mkpro::core::emit::lowering
