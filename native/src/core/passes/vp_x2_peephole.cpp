#include "mkpro/core/passes/vp_x2_peephole.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

namespace {

constexpr int kAbs = 0x31;
constexpr int kSign = 0x32;
constexpr int kInteger = 0x34;
constexpr int kFraction = 0x35;

bool has_roles(const IrOp& op) {
  return !op.meta.roles.empty();
}

// Faithful port of x2BoundaryText: comment + tactic joined by " " (dropping
// empty/absent fields) and lowercased. ASCII is lowercased directly; the only
// Cyrillic token the boundary regex cares about (ВП) is normalized to its
// lowercase byte sequence so /вп/ matches regardless of source case.
std::string x2_boundary_text(const IrOp& op) {
  std::vector<std::string> parts;
  if (op.meta.comment.has_value() && !op.meta.comment->empty())
    parts.push_back(*op.meta.comment);
  if (op.meta.tactic.has_value() && !op.meta.tactic->empty())
    parts.push_back(*op.meta.tactic);
  std::string joined;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0)
      joined.push_back(' ');
    joined += parts.at(i);
  }
  std::string lowered;
  lowered.reserve(joined.size());
  for (std::size_t i = 0; i < joined.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(joined.at(i));
    // Normalize uppercase Cyrillic В (U+0412, \xD0\x92) and П (U+041F,
    // \xD0\x9F) to lowercase в/п so the ВП token matches like JS toLowerCase.
    if (ch == 0xD0 && i + 1 < joined.size()) {
      const unsigned char next = static_cast<unsigned char>(joined.at(i + 1));
      if (next == 0x92) {  // В -> в (\xD0\xB2)
        lowered.push_back(static_cast<char>(0xD0));
        lowered.push_back(static_cast<char>(0xB2));
        ++i;
        continue;
      }
      if (next == 0x9F) {  // П -> п (\xD0\xBF)
        lowered.push_back(static_cast<char>(0xD0));
        lowered.push_back(static_cast<char>(0xBF));
        ++i;
        continue;
      }
    }
    lowered.push_back(static_cast<char>(std::tolower(ch)));
  }
  return lowered;
}

bool is_vp_x2_boundary(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return false;
  if (op.kind != IrKind::Plain || op.opcode != 0x0c)
    return false;
  const std::string text = x2_boundary_text(op);
  return text.find("display") != std::string::npos || text.find("x2") != std::string::npos ||
         text.find("\xD0\xB2\xD0\xBF") != std::string::npos;  // "вп"
}

bool is_fraction_after_x2_boundary(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return false;
  return op.kind == IrKind::Plain && op.opcode == kFraction;
}

bool is_free_standing_unary(const IrOp& op, int opcode) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  return op.kind == IrKind::Plain && op.opcode == opcode && !has_roles(op);
}

bool is_free_standing_noop_unary_op(const IrOp& op) {
  return is_free_standing_unary(op, kFraction) || is_free_standing_unary(op, kInteger) ||
         is_free_standing_unary(op, kAbs) || is_free_standing_unary(op, kSign);
}

bool is_free_standing_vp_restore(const IrOp& op) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  return op.kind == IrKind::Plain && op.opcode == 0x0c && !has_roles(op);
}

bool is_free_standing_empty_op(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return false;
  return op.kind == IrKind::Plain && op.opcode >= 0x54 && op.opcode <= 0x56 && !has_roles(op);
}

bool is_x_preserving_boundary_gap(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return false;
  switch (op.kind) {
    case IrKind::Store:
    case IrKind::IndirectStore:
    case IrKind::OrphanAddress:
      return true;
    case IrKind::Plain:
      return !has_roles(op) && (is_free_standing_empty_op(op) || plain_preserves_x_value(op));
    default:
      return false;
  }
}

bool simple_direct_return_preserves_x(const std::vector<IrOp>& ops, const IrOp& call,
                                      const DirectReturnAnalysisContext& context) {
  return known_return_call_returns_through_nested_transparent_range(ops, call, context,
                                                                    is_x_preserving_boundary_gap);
}

std::optional<int> immediate_vp_restore_after_noop_index(const std::vector<IrOp>& ops, int index,
                                                         const std::set<int>& label_entries) {
  for (int cursor = index + 1; cursor < static_cast<int>(ops.size()); ++cursor) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (op.kind == IrKind::Label) {
      if (label_entries.count(cursor) != 0)
        return std::nullopt;
      continue;
    }
    return is_free_standing_vp_restore(op) ? std::optional<int>(cursor) : std::nullopt;
  }
  return std::nullopt;
}

bool noop_unary_preserves_immediate_vp_source(
    const std::vector<IrOp>& ops, int index,
    const std::vector<std::optional<X2ValueDataflowState>>& states,
    const std::set<int>& label_entries) {
  const std::optional<int> vp_index = immediate_vp_restore_after_noop_index(ops, index, label_entries);
  if (!vp_index.has_value())
    return false;
  return x2_states_have_same_vp_entry_source(states.at(static_cast<std::size_t>(index)),
                                             states.at(static_cast<std::size_t>(*vp_index)));
}

bool is_known_noop_unary_op(const std::vector<IrOp>& ops, int index,
                            const std::vector<std::optional<X2ValueDataflowState>>& states,
                            const std::set<int>& label_entries) {
  const IrOp& op = ops.at(static_cast<std::size_t>(index));
  const std::optional<X2ValueDataflowState>& state = states.at(static_cast<std::size_t>(index));
  if (!is_free_standing_noop_unary_op(op))
    return false;
  const bool known_noop =
      op.kind == IrKind::Plain && x2_state_has_visible_unary_noop(state, op.opcode);
  if (!known_noop)
    return false;
  // К {x}, К [x], К |x|, and К ЗН preserve hidden X2. Once dataflow proves the
  // opcode is also a visible-X no-op, removing it cannot change a later restore
  // value. The exposure guard still keeps immediate restore boundaries where the
  // opcode itself could be the observable previous-command context.
  X2RestoreExposureOptions options;
  options.redundant_sync_value = true;
  options.redundant_sync_shape = true;
  if (!x2_sync_can_expose_context_sensitive_restore(ops, index, options))
    return true;
  return noop_unary_preserves_immediate_vp_source(ops, index, states, label_entries);
}

std::optional<int> fraction_after_boundary_index(const std::vector<IrOp>& ops, int boundary_index,
                                                 const std::set<int>& label_entries,
                                                 const DirectReturnAnalysisContext& context) {
  for (int index = boundary_index + 1; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      if (label_entries.count(index) != 0)
        return std::nullopt;
      continue;
    }
    if (is_x_preserving_boundary_gap(op))
      continue;
    if (is_known_return_call_op(op) && simple_direct_return_preserves_x(ops, op, context))
      continue;
    return is_fraction_after_x2_boundary(op) ? std::optional<int>(index) : std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

PassResult vp_x2_peephole(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  const bool has_fraction =
      std::any_of(ops.begin(), ops.end(), is_fraction_after_x2_boundary);
  const bool has_noop_unary =
      std::any_of(ops.begin(), ops.end(), is_free_standing_noop_unary_op);
  if (!has_fraction && !has_noop_unary)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::set<int> remove;
  const std::set<int> label_entries = compute_label_entry_indexes(ops);
  const DirectReturnAnalysisContext analysis = direct_return_analysis_context(ops);
  const std::vector<std::optional<X2ValueDataflowState>> states =
      compute_x2_value_states(ops, X2ValueStatesOptions{.track_register_memory = true});

  for (int i = 0; i < static_cast<int>(ops.size()); ++i) {
    if (!is_vp_x2_boundary(ops.at(static_cast<std::size_t>(i))))
      continue;
    const std::optional<int> fraction_index =
        fraction_after_boundary_index(ops, i, label_entries, analysis);
    if (fraction_index.has_value())
      remove.insert(*fraction_index);
  }
  for (int i = 0; i < static_cast<int>(ops.size()); ++i) {
    if (remove.count(i) == 0 && is_known_noop_unary_op(ops, i, states, label_entries))
      remove.insert(i);
  }

  if (remove.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size() - remove.size());
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    if (remove.count(index) == 0)
      result.push_back(ops.at(static_cast<std::size_t>(index)));
  }

  return PassResult{
      .ops = std::move(result),
      .applied = static_cast<int>(remove.size()),
      .optimizations = {
          AppliedOptimization{
              .name = "vp-fraction-restore",
              .detail = "Removed " + std::to_string(remove.size()) +
                        " redundant \xD0\x9A {x}/\xD0\x9A [x]/\xD0\x9A |x|/\xD0\x9A \xD0\x97\xD0\x9D"
                        " op(s) already supplied by a \xD0\x92\xD0\x9F/X2 boundary or proved no-op"
                        " X value."},
      },
  };
}

IrPass vp_x2_peephole_pass() {
  return IrPass{
      .name = "vp-x2-peephole",
      .run = vp_x2_peephole,
      .layout_safe = false,
  };
}

}  // namespace mkpro::core::passes
