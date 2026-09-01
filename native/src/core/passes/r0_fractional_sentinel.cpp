#include "mkpro/core/passes/r0_fractional_sentinel.hpp"

#include "mkpro/core/passes/liveness_analysis.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

enum class R0Fact {
  Unknown,
  PositiveFractional,
  Sentinel,
};

enum class XFact {
  Unknown,
  Sentinel,
};

const std::map<std::string, int> kIndirectCondBases = {
    {"!=0", 0x70},
    {">=0", 0x90},
    {"<0", 0xc0},
    {"==0", 0xe0},
};

bool is_fractional_r0_literal_before_store(const std::vector<IrOp>& ops, std::size_t store_index) {
  if (store_index == 0)
    return false;

  int index = static_cast<int>(store_index) - 1;
  bool has_non_zero_fraction_digit = false;
  while (index >= 0) {
    const IrOp& digit = ops.at(static_cast<std::size_t>(index));
    if (digit.kind != IrKind::Plain || digit.opcode < 0x00 || digit.opcode > 0x09)
      break;
    if (digit.opcode > 0)
      has_non_zero_fraction_digit = true;
    --index;
  }

  if (!has_non_zero_fraction_digit)
    return false;
  if (index < 0)
    return false;

  const IrOp& dot = ops.at(static_cast<std::size_t>(index));
  if (dot.kind != IrKind::Plain || dot.opcode != 0x0a)
    return false;

  const int zero_index = index - 1;
  if (zero_index < 0)
    return true;

  const IrOp& zero = ops.at(static_cast<std::size_t>(zero_index));
  return zero.kind == IrKind::Plain && zero.opcode == 0x00;
}

bool is_sentinel_preload_recall(const IrOp* op) {
  if (op == nullptr || op->kind != IrKind::Recall || !op->meta.comment.has_value())
    return false;
  static const std::regex pattern(R"(\bpreload const -99999999\b)");
  return std::regex_search(*op->meta.comment, pattern);
}

bool is_sentinel_direct_literal_at(const std::vector<IrOp>& ops, std::size_t sign_index) {
  const IrOp& sign = ops.at(sign_index);
  if (sign.kind != IrKind::Plain || sign.opcode != 0x0b)
    return false;
  if (sign_index < 8U)
    return false;

  const std::size_t first_digit_index = sign_index - 8U;
  for (std::size_t index = first_digit_index; index < sign_index; ++index) {
    const IrOp& digit = ops.at(index);
    if (digit.kind != IrKind::Plain || digit.opcode != 0x09)
      return false;
  }

  if (first_digit_index == 0)
    return true;

  const IrOp& before = ops.at(first_digit_index - 1U);
  if (before.kind == IrKind::Plain && ((before.opcode >= 0x00 && before.opcode <= 0x09) ||
                                       before.opcode == 0x0a || before.opcode == 0x0c)) {
    return false;
  }
  return true;
}

bool is_sentinel_direct_literal_before_store(const std::vector<IrOp>& ops,
                                             std::size_t store_index) {
  if (store_index == 0)
    return false;
  return is_sentinel_direct_literal_at(ops, store_index - 1U);
}

bool is_sentinel_materialized_before_store(const std::vector<IrOp>& ops, std::size_t store_index) {
  const IrOp* previous = store_index == 0 ? nullptr : &ops.at(store_index - 1U);
  return is_sentinel_preload_recall(previous) ||
         is_sentinel_direct_literal_before_store(ops, store_index);
}

bool preserves_r0_fact(const IrOp& op) {
  return op.kind == IrKind::Plain || op.kind == IrKind::Recall ||
         (op.kind == IrKind::Store && op.register_name != "0") ||
         (op.kind == IrKind::IndirectStore && op.register_name != "0");
}

IrMeta clone_meta(IrMeta meta, const std::string& comment) {
  if (meta.comment.has_value() && !meta.comment->empty()) {
    meta.comment = *meta.comment + "; " + comment;
  } else {
    meta.comment = comment;
  }
  return meta;
}

IrOp fractional_r0_jump(const IrOp& op) {
  IrOp result;
  result.kind = IrKind::IndirectJump;
  result.register_name = "0";
  result.opcode = 0x80;
  IrMeta meta = op.meta;
  meta.mnemonic = "К БП 0";
  result.meta = clone_meta(std::move(meta), "fractional R0 jump to 99");
  return result;
}

IrOp fractional_r0_call(const IrOp& op) {
  IrOp result;
  result.kind = IrKind::IndirectCall;
  result.register_name = "0";
  result.opcode = 0xa0;
  IrMeta meta = op.meta;
  meta.mnemonic = "К ПП 0";
  result.meta = clone_meta(std::move(meta), "fractional R0 call to 99");
  return result;
}

std::string condition_mnemonic_name(const std::string& condition) {
  if (condition == "==0")
    return "x=0";
  if (condition == "!=0")
    return "x!=0";
  return "x" + condition;
}

IrOp fractional_r0_cond_jump(const IrOp& op) {
  IrOp result;
  result.kind = IrKind::IndirectCondJump;
  result.condition = op.condition;
  result.register_name = "0";
  const auto base = kIndirectCondBases.find(op.condition);
  result.opcode = base == kIndirectCondBases.end() ? 0xe0 : base->second;
  IrMeta meta = op.meta;
  meta.mnemonic = "К " + condition_mnemonic_name(op.condition) + " 0";
  result.meta = clone_meta(std::move(meta), "fractional R0 conditional jump to 99");
  return result;
}

std::string optimization_detail(int direct_r3_accesses, int sentinel_stores, int sentinel_recalls,
                                int fractional_jumps) {
  std::vector<std::string> parts;
  if (direct_r3_accesses > 0) {
    parts.push_back(std::to_string(direct_r3_accesses) + " redundant direct R3 access(es)");
  }
  if (sentinel_stores > 0) {
    parts.push_back(std::to_string(sentinel_stores) + " redundant sentinel R0 store(s)");
  }
  if (sentinel_recalls > 0) {
    parts.push_back(std::to_string(sentinel_recalls) + " redundant sentinel R0 recall(s)");
  }
  if (fractional_jumps > 0) {
    parts.push_back(std::to_string(fractional_jumps) +
                    " direct flow op(s) to 99 via fractional R0");
  }

  std::string joined;
  for (std::size_t index = 0; index < parts.size(); ++index) {
    if (index > 0)
      joined += " and ";
    joined += parts.at(index);
  }
  return "Reused fractional-R0 side effects for " + joined + ".";
}

bool is_direct_flow_to_address_99(const IrOp& op) {
  if (op.kind != IrKind::Jump && op.kind != IrKind::Call && op.kind != IrKind::CondJump)
    return false;
  const auto* address = std::get_if<int>(&op.target);
  return address != nullptr && *address == 99;
}

struct ZeroPathSentinelFold {
  std::vector<IrOp> ops;
  int folded = 0;
  int saved_cells = 0;
};

bool program_observes_last_x(const std::vector<IrOp>& ops) {
  return std::any_of(ops.begin(), ops.end(), [](const IrOp& op) {
    return op.kind == IrKind::Plain && op.opcode == 0x0f;
  });
}

IrOp sentinel_scratch_store(const IrOp& destination_store) {
  IrOp result = destination_store;
  result.kind = IrKind::Store;
  result.register_name = "3";
  result.opcode = 0x43;
  result.meta.mnemonic = "X->П 3";
  result.meta.comment = "seed proved zero for R3 underflow sentinel";
  return result;
}

IrOp sentinel_scratch_recall(const IrOp& destination_store) {
  IrOp result = destination_store;
  result.kind = IrKind::IndirectRecall;
  result.register_name = "3";
  result.opcode = 0xd3;
  result.meta.mnemonic = "К П->X 3";
  result.meta.comment = "form and self-recall -99999999 by R3 underflow";
  return result;
}

ZeroPathSentinelFold fold_zero_path_sentinel_literals(const std::vector<IrOp>& ops) {
  ZeroPathSentinelFold fold{.ops = ops};
  const bool trace = std::getenv("MKPRO_NATIVE_TRACE_R0_SENTINEL") != nullptr;
  if (ops.size() < 11U || program_observes_last_x(ops))
    return fold;

  const LivenessInfo liveness =
      compute_liveness(ops, LivenessOptions{.unresolved_direct_flow_to_all = false});
  if (!liveness.control_flow_targets_are_exact) {
    if (trace)
      std::cerr << "[zero-path-sentinel] incomplete control-flow proof\n";
    return fold;
  }

  std::set<std::size_t> remove;
  std::map<std::size_t, std::vector<IrOp>> replace;
  for (std::size_t store_index = 10U; store_index < ops.size(); ++store_index) {
    const IrOp& destination = ops.at(store_index);
    if (destination.kind != IrKind::Store || has_rewrite_barrier(destination) ||
        !is_sentinel_direct_literal_before_store(ops, store_index)) {
      continue;
    }

    const std::size_t first_digit = store_index - 9U;
    if (first_digit == 0U)
      continue;
    const IrOp& guard = ops.at(first_digit - 1U);
    if (trace) {
      std::cerr << "[zero-path-sentinel] candidate store=" << store_index
                << " destination=" << destination.register_name
                << " guard-kind=" << static_cast<int>(guard.kind)
                << " guard-condition=" << guard.condition
                << " r3-live=" << liveness.live_in.at(first_digit).contains("3")
                << "\n";
    }
    if (guard.kind != IrKind::CondJump || guard.condition != "==0" ||
        has_rewrite_barrier(guard) ||
        (destination.register_name != "3" &&
         liveness.live_in.at(first_digit).contains("3"))) {
      continue;
    }
    if (std::any_of(ops.begin() + static_cast<std::ptrdiff_t>(first_digit),
                    ops.begin() + static_cast<std::ptrdiff_t>(store_index),
                    [](const IrOp& op) { return has_rewrite_barrier(op); })) {
      continue;
    }

    std::vector<IrOp> replacement;
    replacement.push_back(sentinel_scratch_store(destination));
    replacement.push_back(sentinel_scratch_recall(destination));
    if (destination.register_name != "3")
      replacement.push_back(destination);
    replace.emplace(first_digit, std::move(replacement));
    for (std::size_t index = first_digit; index <= store_index; ++index)
      remove.insert(index);
    ++fold.folded;
    fold.saved_cells += destination.register_name == "3" ? 8 : 7;
  }

  if (fold.folded == 0)
    return fold;

  fold.ops.clear();
  fold.ops.reserve(ops.size() - static_cast<std::size_t>(fold.saved_cells));
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const auto replacement = replace.find(index);
    if (replacement != replace.end()) {
      fold.ops.insert(fold.ops.end(), replacement->second.begin(), replacement->second.end());
    }
    if (!remove.contains(index))
      fold.ops.push_back(ops.at(index));
  }
  return fold;
}

} // namespace

PassResult r0_fractional_sentinel(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  if (ops.empty())
    return PassResult{.ops = {}, .applied = 0, .optimizations = {}};

  // Address 99 is the explicit external target this pass is designed to
  // materialize. When it is outside the current fragment, keep it as an exit
  // rather than feeding its unresolved edge back into every local command.
  // The liveness source is still a full-register barrier; only live_out uses
  // the pass-specific external-exit contract.
  const LivenessInfo liveness =
      compute_liveness(ops, LivenessOptions{.unresolved_direct_flow_to_all = false});
  std::set<std::size_t> remove;
  std::map<std::size_t, IrOp> replace;
  R0Fact r0_fact = R0Fact::Unknown;
  XFact x_fact = XFact::Unknown;
  int direct_r3_accesses = 0;
  int sentinel_stores = 0;
  int sentinel_recalls = 0;
  int fractional_jumps = 0;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (has_rewrite_barrier(op)) {
      r0_fact = R0Fact::Unknown;
      x_fact = XFact::Unknown;
      continue;
    }

    if (op.kind == IrKind::Store && op.register_name == "0") {
      const bool stores_sentinel =
          x_fact == XFact::Sentinel || is_sentinel_materialized_before_store(ops, index);
      if (r0_fact == R0Fact::Sentinel && stores_sentinel) {
        remove.insert(index);
        ++sentinel_stores;
        x_fact = XFact::Sentinel;
        continue;
      }
      if (is_fractional_r0_literal_before_store(ops, index)) {
        r0_fact = R0Fact::PositiveFractional;
      } else if (stores_sentinel) {
        r0_fact = R0Fact::Sentinel;
      } else {
        r0_fact = R0Fact::Unknown;
      }
      x_fact = stores_sentinel ? XFact::Sentinel : XFact::Unknown;
      continue;
    }

    if (op.kind == IrKind::Label) {
      if (r0_fact == R0Fact::Sentinel)
        r0_fact = R0Fact::Unknown;
      x_fact = XFact::Unknown;
      continue;
    }

    if (op.kind == IrKind::Recall) {
      if (op.register_name == "0" && r0_fact == R0Fact::Sentinel) {
        if (x_fact == XFact::Sentinel) {
          remove.insert(index);
          ++sentinel_recalls;
          continue;
        }
        x_fact = XFact::Sentinel;
        continue;
      }
      x_fact = is_sentinel_preload_recall(&op) ? XFact::Sentinel : XFact::Unknown;
      continue;
    }

    if (op.kind == IrKind::Plain) {
      x_fact = is_sentinel_direct_literal_at(ops, index) ? XFact::Sentinel : XFact::Unknown;
      continue;
    }

    if (op.kind == IrKind::IndirectRecall && op.register_name != "0") {
      x_fact = XFact::Unknown;
      continue;
    }

    if (preserves_r0_fact(op))
      continue;

    if (r0_fact != R0Fact::PositiveFractional) {
      r0_fact = R0Fact::Unknown;
      x_fact = XFact::Unknown;
      continue;
    }

    if (op.kind == IrKind::IndirectRecall && op.register_name == "0") {
      if (index + 1U < ops.size()) {
        const IrOp& next = ops.at(index + 1U);
        if (next.kind == IrKind::Recall && next.register_name == "3" &&
            !liveness.live_out.at(index).contains("0")) {
          remove.insert(index + 1U);
          ++direct_r3_accesses;
        }
      }
      r0_fact = R0Fact::Sentinel;
      x_fact = XFact::Unknown;
      continue;
    }

    if (op.kind == IrKind::IndirectStore && op.register_name == "0") {
      if (index + 1U < ops.size()) {
        const IrOp& next = ops.at(index + 1U);
        if (next.kind == IrKind::Store && next.register_name == "3" &&
            !liveness.live_out.at(index).contains("0")) {
          remove.insert(index + 1U);
          ++direct_r3_accesses;
        }
      }
      r0_fact = R0Fact::Sentinel;
      continue;
    }

    if (is_direct_flow_to_address_99(op) && !liveness.live_out.at(index).contains("0")) {
      if (op.kind == IrKind::Jump) {
        replace[index] = fractional_r0_jump(op);
      } else if (op.kind == IrKind::Call) {
        replace[index] = fractional_r0_call(op);
      } else {
        replace[index] = fractional_r0_cond_jump(op);
      }
      ++fractional_jumps;
      r0_fact = R0Fact::Unknown;
      x_fact = XFact::Unknown;
      continue;
    }

    r0_fact = R0Fact::Unknown;
    x_fact = XFact::Unknown;
  }

  std::vector<IrOp> result;
  result.reserve(ops.size() - remove.size());
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (remove.contains(index))
      continue;
    const auto replacement = replace.find(index);
    result.push_back(replacement == replace.end() ? ops.at(index) : replacement->second);
  }

  ZeroPathSentinelFold folded = fold_zero_path_sentinel_literals(result);
  const int existing_applied = static_cast<int>(remove.size() + replace.size());
  const int total_applied = existing_applied + folded.folded;
  if (total_applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<AppliedOptimization> optimizations;
  if (existing_applied > 0) {
    optimizations.push_back(AppliedOptimization{
        .name = "r0-fractional-sentinel",
        .detail = optimization_detail(direct_r3_accesses, sentinel_stores,
                                      sentinel_recalls, fractional_jumps),
    });
  }
  if (folded.folded > 0) {
    optimizations.push_back(AppliedOptimization{
        .name = "zero-path-underflow-sentinel-materialization",
        .detail = "Replaced " + std::to_string(folded.folded) +
                  " direct -99999999 literal materialization(s) on proved X=0 "
                  "fallthrough paths with dead-R3 underflow, saving " +
                  std::to_string(folded.saved_cells) + " cell(s).",
    });
  }

  return PassResult{
      .ops = std::move(folded.ops),
      .applied = total_applied,
      .optimizations = std::move(optimizations),
  };
}

IrPass r0_fractional_sentinel_pass() {
  return IrPass{
      .name = "r0-fractional-sentinel",
      .run = r0_fractional_sentinel,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
