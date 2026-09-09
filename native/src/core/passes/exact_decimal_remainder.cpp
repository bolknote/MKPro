#include "mkpro/core/passes/exact_decimal_remainder.hpp"

#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>

namespace mkpro::core::passes {

std::optional<ExactDecimalRemainderProof>
prove_exact_decimal_remainder_precision(int divisor) {
  if (divisor <= 0)
    return std::nullopt;
  int remaining = divisor;
  int twos = 0;
  int fives = 0;
  while (remaining % 2 == 0) {
    remaining /= 2;
    ++twos;
  }
  while (remaining % 5 == 0) {
    remaining /= 5;
    ++fives;
  }
  const int digits = std::max(twos, fives);
  if (remaining != 1 || digits > 7)
    return std::nullopt;
  int scale = 1;
  for (int digit = 0; digit < digits; ++digit)
    scale *= 10;
  if (static_cast<std::int64_t>(divisor) * scale > 100000000)
    return std::nullopt;
  return ExactDecimalRemainderProof{digits, scale};
}

namespace {

bool plain(const IrOp& op, int opcode) {
  return op.kind == IrKind::Plain && op.opcode == opcode;
}

bool movable(const IrOp& op) {
  return !has_rewrite_barrier(op) && op.meta.roles.empty() &&
         op.target_meta.roles.empty() && !op.target_meta.formal_opcode.has_value() &&
         !op.meta.tactic.has_value() && !op.procedure_boundary.has_value() &&
         op.register_name.empty() && op.counter.empty() && op.semantic.empty() &&
         !op.meta.indirect_flow_targets.has_value() &&
         !op.meta.indirect_memory_targets.has_value() &&
         !op.meta.logical_register_name.has_value() &&
         !op.meta.logical_indirect_memory_targets.has_value() &&
         !op.meta.logical_register_analysis && !op.meta.discarded_indirect_recall_value &&
         !op.meta.borrowed_entry_phase_selector && op.meta.semantic_call_origins.empty();
}

bool symbolic_geometry(const std::vector<IrOp>& ops) {
  for (const IrOp& op : ops) {
    if (op.meta.raw || op.target_meta.formal_opcode.has_value() ||
        !op.target_meta.roles.empty() || op.meta.indirect_flow_targets.has_value() ||
        op.kind == IrKind::OrphanAddress || op.kind == IrKind::IndirectJump ||
        op.kind == IrKind::IndirectCall || op.kind == IrKind::IndirectCondJump ||
        plain(op, 0x2a))
      return false;
    if ((op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
         op.kind == IrKind::Call || op.kind == IrKind::Loop) &&
        !std::holds_alternative<std::string>(op.target))
      return false;
    const bool typed_stop =
        (op.kind == IrKind::Stop || op.kind == IrKind::Plain) &&
        (op.opcode == 0x50 || op.opcode == 0x29) &&
        op.meta.stop_disposition != StopDisposition::Unknown;
    for (const auto& role : op.meta.roles)
      if (!role.starts_with(kRetunableNaturalFractionalSelectorRolePrefix) &&
          !(op.kind == IrKind::Call &&
            (role == "statement-proc-call" || role == "x-argument-call")) &&
          !(typed_stop && role == kTypedDisplayObservationRole))
        return false;
  }
  return true;
}

struct Match {
  std::size_t divisor_begin = 0;
  std::size_t divisor_end = 0;
  std::size_t fraction = 0;
  std::size_t done = 0;
};

std::optional<Match> match_at(const std::vector<IrOp>& ops, std::size_t start,
                             const std::map<std::string, int>& references,
                             const std::map<std::string, int>& definitions) {
  if (!plain(ops[start], 0x34))
    return std::nullopt;
  std::size_t cursor = start + 1;
  const std::size_t divisor_begin = cursor;
  int divisor = 0;
  while (cursor < ops.size() && ops[cursor].kind == IrKind::Plain &&
         ops[cursor].opcode >= 0 && ops[cursor].opcode <= 9) {
    if (cursor - divisor_begin >= 8 || (cursor == divisor_begin && ops[cursor].opcode == 0))
      return std::nullopt;
    divisor = divisor * 10 + ops[cursor++].opcode;
  }
  const std::size_t divisor_end = cursor;
  if (divisor_begin == divisor_end || !prove_exact_decimal_remainder_precision(divisor))
    return std::nullopt;
  const auto consume = [&](int opcode) {
    if (cursor >= ops.size() || !plain(ops[cursor], opcode))
      return false;
    ++cursor;
    return true;
  };
  const auto consume_divisor = [&] {
    for (std::size_t digit = divisor_begin; digit < divisor_end; ++digit)
      if (!consume(ops[digit].opcode))
        return false;
    return true;
  };
  if (!consume(0x13) || !consume(0x35))
    return std::nullopt;
  const std::size_t fraction = cursor - 1;
  if (!consume_divisor() || !consume(0x12) || cursor + 2 >= ops.size())
    return std::nullopt;
  const IrOp& negative = ops[cursor++];
  const IrOp& positive = ops[cursor++];
  const auto* adjust = std::get_if<std::string>(&negative.target);
  const auto* done = std::get_if<std::string>(&positive.target);
  if (negative.kind != IrKind::CondJump || negative.opcode != 0x59 ||
      negative.condition != ">=0" || positive.kind != IrKind::CondJump ||
      positive.opcode != 0x5e || positive.condition != "==0" ||
      adjust == nullptr || done == nullptr || *adjust == *done ||
      !references.contains(*adjust) || references.at(*adjust) != 1 ||
      !definitions.contains(*adjust) || definitions.at(*adjust) != 1 ||
      !definitions.contains(*done) || definitions.at(*done) != 1 ||
      ops[cursor].kind != IrKind::Label || ops[cursor].name != *adjust)
    return std::nullopt;
  ++cursor;
  if (!consume_divisor() || !consume(0x10) || cursor >= ops.size() ||
      ops[cursor].kind != IrKind::Label || ops[cursor].name != *done)
    return std::nullopt;
  for (std::size_t index = start; index < cursor; ++index)
    if (!movable(ops[index]) || ops[index].procedure_name != ops[start].procedure_name)
      return std::nullopt;
  return Match{divisor_begin, divisor_end, fraction, cursor};
}

IrOp arithmetic(int opcode, const IrOp& source) {
  IrOp result;
  result.kind = IrKind::Plain;
  result.opcode = opcode;
  result.procedure_name = source.procedure_name;
  result.hidden = source.hidden;
  result.meta.source_line = source.meta.source_line;
  result.meta.mnemonic = opcode_by_code(opcode).name;
  result.meta.comment = "exact decimal signed remainder correction";
  return result;
}

} // namespace

PassResult exact_decimal_remainder_correction(const std::vector<IrOp>& ops,
                                             const PassContext& context) {
  (void)context;
  PassResult result{.ops = ops};
  if (!symbolic_geometry(ops))
    return result;
  std::map<std::string, int> references;
  std::map<std::string, int> definitions;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label)
      ++definitions[op.name];
    if (const auto* label = std::get_if<std::string>(&op.target))
      ++references[*label];
  }
  result.ops.clear();
  result.ops.reserve(ops.size());
  int saved = 0;
  for (std::size_t index = 0; index < ops.size();) {
    const auto match = match_at(ops, index, references, definitions);
    if (!match) {
      result.ops.push_back(ops[index++]);
      continue;
    }
    // For r = frac(int(x) / m), replace the sign/zero diamond by
    // m * (frac(r - 1) + 1). Precision proof makes both evaluation orders exact
    // after the common (possibly rounded) quotient. Raw/error and frozen
    // address artifacts are excluded; the adjustment has no outside entry.
    result.ops.insert(result.ops.end(), ops.begin() + static_cast<std::ptrdiff_t>(index),
                      ops.begin() + static_cast<std::ptrdiff_t>(match->fraction + 1));
    for (int opcode : {1, 0x11, 0x35, 1, 0x10})
      result.ops.push_back(arithmetic(opcode, ops[match->fraction]));
    result.ops.insert(result.ops.end(),
                      ops.begin() + static_cast<std::ptrdiff_t>(match->divisor_begin),
                      ops.begin() + static_cast<std::ptrdiff_t>(match->divisor_end));
    result.ops.push_back(arithmetic(0x12, ops[match->fraction]));
    saved += static_cast<int>(match->divisor_end - match->divisor_begin);
    ++result.applied;
    index = match->done; // Keep the join label and every incoming edge to it.
  }
  if (result.applied)
    result.optimizations.push_back({
        .name = "exact-decimal-remainder-correction",
        .detail = "Replaced " + std::to_string(result.applied) +
                  " signed remainder correction diamond(s) with exact decimal arithmetic (" +
                  std::to_string(saved) +
                  " cells saved); proved terminating quotient precision, single-entry "
                  "correction, symbolic geometry, stack/X1/X2 and closed numeric entry.",
    });
  return result;
}

IrPass exact_decimal_remainder_correction_pass() {
  return IrPass{.name = "exact-decimal-remainder-correction",
                .run = exact_decimal_remainder_correction,
                .layout_safe = false};
}

} // namespace mkpro::core::passes
