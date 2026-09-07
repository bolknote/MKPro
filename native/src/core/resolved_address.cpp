#include "mkpro/core/resolved_address.hpp"

#include "mkpro/core/result.hpp"
#include "mkpro/core/opcodes.hpp"

#include <exception>

namespace mkpro {

namespace {

std::optional<int> checked_logical_target(const ResolvedStep& step,
                                         const LogicalCodeAddress& target,
                                         AddressSpaceModel model) {
  if (target.index < 0)
    return std::nullopt;
  if (target.index > official_program_last_address(model))
    return step.opcode == -1 ? std::optional<int>(target.index) : std::nullopt;
  if (step.opcode != official_address_to_opcode(target.index, model))
    return std::nullopt;
  return target.index;
}

std::optional<FormalAddressInfo> checked_formal_target(const ResolvedStep& step,
                                                     AddressSpaceModel model) {
  int opcode = step.opcode;
  if (step.address_target.has_value()) {
    const auto* target = std::get_if<FormalCodeAddress>(&*step.address_target);
    if (target == nullptr || target->opcode != opcode)
      return std::nullopt;
  }
  if (opcode < 0 || opcode > 0xff)
    return std::nullopt;
  try {
    return formal_address_info(opcode, model);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

} // namespace

bool resolved_step_takes_address(const ResolvedStep& step) {
  return step.opcode >= 0 && step.opcode <= 0xff && opcode_by_code(step.opcode).takes_address;
}

std::optional<int> resolved_step_target(const ResolvedStep& operand,
                                       AddressSpaceModel model) {
  if (operand.address_target.has_value()) {
    if (const auto* logical = std::get_if<LogicalCodeAddress>(&*operand.address_target))
      return checked_logical_target(operand, *logical, model);
  }
  const auto formal = checked_formal_target(operand, model);
  return formal.has_value() ? std::optional<int>(formal->actual) : std::nullopt;
}

std::optional<int> resolved_logical_target(const ResolvedStep& operand,
                                          AddressSpaceModel model) {
  if (operand.address_target.has_value()) {
    if (const auto* logical = std::get_if<LogicalCodeAddress>(&*operand.address_target))
      return checked_logical_target(operand, *logical, model);
  }
  const auto formal = checked_formal_target(operand, model);
  return formal.has_value() && formal->kind == FormalAddressKind::Official
             ? std::optional<int>(formal->actual)
             : std::nullopt;
}

std::optional<std::string> physical_program_image_rejection(
    const std::vector<ResolvedStep>& steps, AddressSpaceModel model) {
  const int limit = official_program_step_limit(model);
  if (steps.size() > static_cast<std::size_t>(limit))
    return "virtual program has " + std::to_string(steps.size()) +
           " cells; physical program memory has " + std::to_string(limit);
  for (const ResolvedStep& step : steps) {
    if (step.address < 0 || step.address >= limit)
      return "virtual cell " + std::to_string(step.address) + " has no physical placement";
    if (step.opcode < 0 || step.opcode > 0xff)
      return "unencoded logical target at cell " + std::to_string(step.address);
    if (step.address_target.has_value() && !resolved_step_target(step, model).has_value())
      return "address operand disagrees with its typed target at cell " +
             std::to_string(step.address);
  }
  return std::nullopt;
}

} // namespace mkpro
