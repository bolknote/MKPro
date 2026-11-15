#pragma once

#include "mkpro/core/natural_target_component_layout.hpp"

#include <set>

namespace mkpro::core {

// An equal-width, whole-artifact alternative. The original register remains
// a selector in a data split; in a move it becomes a vacant stable selector.
struct SelectorVacancySeed {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::optional<std::string> vacant;
  std::optional<std::string> data_moved_to_rf;
  bool split_data_and_selector = false;
};

// Requires a complete setup environment and a rooted, authoritative CFG.
// Only direct Rf accesses are synthesized, and only under the expanded model.
std::vector<SelectorVacancySeed> prepare_selector_vacancy_layout_seeds(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& flow,
    const PostLayoutControlFlowOptions& control, bool allow_rf,
    const std::set<std::string>& reserved_registers = {});

// Close the new word together with placement and all existing selector
// bindings. A size-neutral result is an intermediate, never a new winner.
NaturalTargetComponentLayoutResult close_selector_vacancy_layout(
    const SelectorVacancySeed& seed, const AuthoritativePostLayoutControlFlow& flow,
    NaturalTargetComponentLayoutOptions options);

} // namespace mkpro::core
