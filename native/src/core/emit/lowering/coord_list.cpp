#include "mkpro/core/emit/lowering/coord_list.hpp"

#include "mkpro/core/emit/lowering_helpers.hpp"
#include "mkpro/core/register_allocator.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace mkpro::core::emit::lowering {

namespace {

std::string lower_ascii(std::string value) {
  for (char& ch : value)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return value;
}

} // namespace

bool is_preincrement_indirect_register(int index) {
  return index >= 4 && index <= 6;
}

std::string coord_list_indirect_targets_suffix(const CoordListIndirectContext& context) {
  if (context.item_registers.empty())
    return "";
  std::vector<int> targets = context.item_registers;
  std::sort(targets.begin(), targets.end());
  targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  std::string suffix = "; indirect-memory-targets=";
  for (std::size_t index = 0; index < targets.size(); ++index) {
    if (index > 0)
      suffix += ",";
    suffix += register_name_for_index(targets.at(index));
  }
  return suffix;
}

std::vector<int> coord_list_indirect_targets(const CoordListIndirectContext& context) {
  std::vector<int> targets = context.item_registers;
  std::sort(targets.begin(), targets.end());
  targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  return targets;
}

std::optional<std::vector<std::string>> coord_list_items_for(
    const std::map<std::string, const V2StateField*>& state_fields,
    const std::string& list_name) {
  const auto field_it = state_fields.find(list_name);
  if (field_it == state_fields.end() || field_it->second->type != "coord_list")
    return std::nullopt;
  if (!field_it->second->count.has_value())
    return std::vector<std::string>{};

  std::vector<std::string> items;
  for (int index = 0; index < *field_it->second->count; ++index)
    items.push_back(coord_list_item_name(list_name, index));
  return items;
}

std::optional<CoordListIndirectContext> coord_list_indirect_context(
    const std::map<std::string, const V2StateField*>& state_fields,
    const std::map<std::string, int>& register_index_by_name, const Expression& call) {
  if (call.kind != "call" || lower_ascii(call.callee) != "line_count" || call.args.size() != 2)
    return std::nullopt;
  const Expression& collection = call.args.at(0);
  if (collection.kind != "identifier")
    return std::nullopt;
  const std::optional<std::vector<std::string>> items =
      coord_list_items_for(state_fields, collection.name);
  if (!items.has_value() || items->empty())
    return std::nullopt;

  const std::string pointer_name(k_coord_list_pointer);
  const std::string counter_name(k_coord_list_counter);
  const auto pointer_it = register_index_by_name.find(pointer_name);
  const auto counter_it = register_index_by_name.find(counter_name);
  if (pointer_it == register_index_by_name.end() ||
      counter_it == register_index_by_name.end() ||
      !is_preincrement_indirect_register(pointer_it->second)) {
    return std::nullopt;
  }

  std::vector<int> item_indices;
  item_indices.reserve(items->size());
  for (const std::string& item : *items) {
    const auto item_it = register_index_by_name.find(item);
    if (item_it == register_index_by_name.end())
      return std::nullopt;
    item_indices.push_back(item_it->second);
  }
  for (std::size_t index = 1; index < item_indices.size(); ++index) {
    if (item_indices.at(index) != item_indices.front() + static_cast<int>(index))
      return std::nullopt;
  }
  if (item_indices.front() <= 0 || item_indices.front() == pointer_it->second)
    return std::nullopt;

  return CoordListIndirectContext{
      .cell = call.args.at(1),
      .count = static_cast<int>(items->size()),
      .pointer_start = item_indices.front() - 1,
      .item_registers = item_indices,
      .item_names = *items,
      .pointer = pointer_name,
      .counter = counter_name,
  };
}

void emit_coord_list_loop_setup(CoordListEmitApi& api,
                                const CoordListIndirectContext& context, int source_line) {
  api.emit_number_or_preload(std::to_string(context.pointer_start), std::nullopt, source_line);
  api.emit_store(context.pointer, "coord_list pointer");
  api.emit_number_or_preload(std::to_string(context.count), std::nullopt, source_line);
  api.emit_store(context.counter, "coord_list counter");
}

void emit_coord_list_indirect_recall(CoordListEmitApi& api,
                                     const CoordListIndirectContext& context, int source_line,
                                     const std::string& comment) {
  const int pointer_index = api.register_index_for(context.pointer);
  api.emitter.emit_op(0xd0 + pointer_index, "К П->X " + api.register_text_for(context.pointer),
                      comment + coord_list_indirect_targets_suffix(context), source_line);
  api.emitter.items.back().indirect_memory_targets = coord_list_indirect_targets(context);
  api.emitter.items.back().logical_register_name = context.pointer;
  api.emitter.items.back().logical_indirect_memory_targets = context.item_names;
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
}

void emit_coord_list_counter_loop(CoordListEmitApi& api, const std::string& counter,
                                  const std::string& target, int source_line,
                                  const std::string& comment) {
  const int counter_index = api.register_index_for(counter);
  if (const auto loop_opcode = api.fl_loop_opcode_for_register(counter_index)) {
    api.emitter.emit_jump(loop_opcode->first, loop_opcode->second, target, comment,
                          source_line);
    api.emitter.items.back().logical_register_name = counter;
    api.emitter.coord_list_counter_known_one = true;
    return;
  }

  api.emit_recall(counter);
  api.emit_number_or_preload("1", std::nullopt, source_line);
  api.emitter.emit_op(0x11, "-", "coord_list decrement", source_line);
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  api.emit_store(counter, "coord_list counter");
  api.emit_recall(counter);
  api.emitter.emit_jump(0x5e, "F x=0", target, comment, source_line);
  api.emitter.coord_list_counter_known_one = false;
}

bool emit_coord_digit_ones_to_x(CoordListEmitApi& api, const Expression& expression,
                                int source_line) {
  if (!api.lower_expression_to_x(expression))
    return false;
  api.emit_number_or_preload("10", std::nullopt, source_line);
  api.emitter.emit_op(0x13, "/", "coord quotient", source_line);
  api.emitter.emit_op(0x35, "К {x}", "coord fractional part", source_line);
  api.emit_number_or_preload("10", std::nullopt, source_line);
  api.emitter.emit_op(0x12, "*", "coord ones digit", source_line);
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  return true;
}

bool emit_coord_digit_tens_to_x(CoordListEmitApi& api, const Expression& expression,
                                int source_line) {
  if (!api.lower_expression_to_x(expression))
    return false;
  api.emit_number_or_preload("10", std::nullopt, source_line);
  api.emitter.emit_op(0x13, "/", "coord quotient", source_line);
  api.emitter.emit_op(0x34, "К [x]", "coord tens digit", source_line);
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  return true;
}

bool lower_coord_list_line_count_assignment(CoordListEmitApi& api,
                                            const CoordListIndirectContext& context,
                                            const std::string& target, int source_line) {
  api.emitter.emit_number("0");
  api.emit_store(target, "coord_list line_count total");
  emit_coord_list_loop_setup(api, context, source_line);

  const std::string start_label = api.emitter.fresh_label("coord_list_line_loop");
  const std::string visible_label = api.emitter.fresh_label("coord_list_visible");
  const std::string count_next_label = api.emitter.fresh_label("coord_list_count_next");
  const Expression current = identifier_expression(std::string(k_coord_list_current));

  api.emitter.emit_label(start_label);
  emit_coord_list_indirect_recall(api, context, source_line, "coord_list candidate");
  api.emit_store(std::string(k_coord_list_current), "coord_list current");

  if (!emit_coord_digit_ones_to_x(api, current, source_line))
    return false;
  if (!emit_coord_digit_ones_to_x(api, context.cell, source_line))
    return false;
  api.emitter.emit_op(0x11, "-", "coord_list dx", source_line);
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  api.emitter.emit_jump(0x57, "F x!=0", visible_label, "coord_list same column",
                        source_line);

  if (!emit_coord_digit_tens_to_x(api, current, source_line))
    return false;
  if (!emit_coord_digit_tens_to_x(api, context.cell, source_line))
    return false;
  api.emitter.emit_op(0x11, "-", "coord_list dy", source_line);
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  api.emitter.emit_jump(0x57, "F x!=0", visible_label, "coord_list same row",
                        source_line);
  api.emitter.emit_op(0x31, "К |x|", "coord_list |dy|", source_line);
  api.emitter.emit_op(0x14, "<->", "coord_list dx", source_line);
  api.emitter.emit_op(0x31, "К |x|", "coord_list |dx|", source_line);
  api.emitter.emit_op(0x11, "-", "coord_list diagonal compare", source_line);
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  api.emitter.emit_jump(0x57, "F x!=0", visible_label, "coord_list same diagonal",
                        source_line);
  api.emitter.emit_jump(0x51, "БП", count_next_label, "coord_list not visible",
                        source_line);

  api.emitter.emit_label(visible_label, {.hidden = true});
  api.emit_recall(target);
  api.emit_number_or_preload("1", std::nullopt, source_line);
  api.emitter.emit_op(0x10, "+", "coord_list add visible", source_line);
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  api.emit_store(target, "coord_list line_count total");

  api.emitter.emit_label(count_next_label, {.hidden = true});
  emit_coord_list_counter_loop(api, context.counter, start_label, source_line,
                               "coord_list line_count loop");
  api.emit_recall(target);
  if (!api.emitter.items.empty())
    api.emitter.items.back().comment = "coord_list line_count result";
  return true;
}

bool lower_coord_list_remove(CoordListEmitApi& api, const CoordListIndirectContext& context,
                             int source_line) {
  const std::string found_label = api.emitter.fresh_label("coord_list_remove_hit");
  const std::string done_label = api.emitter.fresh_label("coord_list_remove_done");
  const std::string start_label = api.emitter.fresh_label("coord_list_remove_loop");
  emit_coord_list_loop_setup(api, context, source_line);
  api.emitter.emit_label(start_label);
  if (!api.lower_expression_to_x(context.cell))
    return false;
  emit_coord_list_indirect_recall(api, context, source_line, "coord_list remove candidate");
  api.emitter.emit_op(0x11, "-", "coord_list remove compare", source_line);
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  api.emitter.emit_jump(0x57, "F x!=0", found_label, "coord_list remove hit", source_line);
  emit_coord_list_counter_loop(api, context.counter, start_label, source_line,
                               "coord_list remove loop");
  api.emitter.emit_jump(0x51, "БП", done_label, "coord_list remove miss", source_line);

  api.emitter.emit_label(found_label, {.hidden = true});
  api.emit_recall(std::string(k_coord_list_pointer));
  api.emitter.items.back().comment = "coord_list remove pointer";
  api.emit_number_or_preload("1", std::nullopt, source_line);
  api.emitter.emit_op(0x11, "-", "coord_list remove current pointer", source_line);
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  api.emit_store(std::string(k_coord_list_pointer), "coord_list remove current pointer");
  api.emitter.emit_number("-1");
  const int pointer_index = api.register_index_for(context.pointer);
  api.emitter.emit_op(0xb0 + pointer_index, "К X->П " + api.register_text_for(context.pointer),
                      "coord_list remove current", source_line);
  api.emitter.items.back().indirect_memory_targets = coord_list_indirect_targets(context);
  api.emitter.items.back().logical_register_name = context.pointer;
  api.emitter.items.back().logical_indirect_memory_targets = context.item_names;
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  api.emitter.current_x_known_zero = false;
  api.emitter.emit_label(done_label, {.hidden = true});
  return true;
}

} // namespace mkpro::core::emit::lowering
