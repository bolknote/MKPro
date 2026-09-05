#include "mkpro/compiler.hpp"
#include "mkpro/core/passes/exact_decimal_remainder.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace mkpro::tests {
namespace {

using core::passes::exact_decimal_remainder_correction;
using core::passes::prove_exact_decimal_remainder_precision;

std::vector<MachineItem> remainder_fixture(int width, bool replacement = false,
                                          int scale_override = 0) {
  std::vector<MachineItem> items;
  const auto op = [&](int code) { items.push_back(MachineItem::op(code, "")); };
  const auto number = [&](int value) {
    for (char digit : std::to_string(value))
      op(digit - '0');
  };
  const auto jump = [&](int code, const std::string& label) {
    op(code);
    items.push_back(MachineItem::address(label));
  };
  op(0x50);
  jump(0x53, "outer");
  op(0x50);
  op(0x0a); // Observe hidden X2 through a decimal-entry continuation.
  op(0x50);
  items.push_back(MachineItem::label("outer"));
  jump(0x53, "inner");
  op(0x52);
  items.push_back(MachineItem::label("inner"));
  op(0x34);
  number(width);
  op(0x13);
  op(0x35);
  if (replacement) {
    for (int code : {1, 0x11, 0x35, 1, 0x10})
      op(code);
    number(width);
    op(0x12);
  } else {
    number(scale_override ? scale_override : width);
    op(0x12);
    jump(0x59, "adjust");
    jump(0x5e, "join");
    items.push_back(MachineItem::label("adjust"));
    number(width);
    op(0x10);
  }
  items.push_back(MachineItem::label("join"));
  op(0x52);
  return items;
}

std::vector<int> resolve_fixture(const std::vector<MachineItem>& items) {
  std::map<std::string, int> labels;
  int address = 0;
  for (const auto& item : items) {
    if (item.kind == MachineItemKind::Label)
      labels.emplace(item.name, address);
    else
      ++address;
  }
  require(address <= 105, "remainder emulator fixture must fit the real program window");
  std::vector<int> codes;
  for (const auto& item : items) {
    if (item.kind == MachineItemKind::Op)
      codes.push_back(item.opcode);
    else if (item.kind == MachineItemKind::Address) {
      const auto* label = std::get_if<std::string>(&item.target);
      require(label != nullptr && labels.contains(*label), "fixture target must be symbolic");
      const int target = labels.at(*label);
      codes.push_back((target / 10) * 16 + target % 10);
    }
  }
  return codes;
}

std::vector<std::string> remainder_observation(const std::vector<int>& codes,
                                              std::string input) {
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "remainder fixture should load");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(400, 5).stopped, "fixture should prompt for input");
  const bool negative = input.front() == '-';
  if (negative)
    input.erase(0, 1);
  calc.input_number(input, true);
  if (negative)
    calc.press("/-/");
  calc.set_register("Y", "73");
  calc.set_register("Z", "82");
  calc.set_register("T", "91");
  std::vector<std::string> result;
  for (int continuation = 0; continuation < 2; ++continuation) {
    calc.press("С/П");
    require(calc.run_until_stable(1500, 6).stopped,
            "remainder fixture should return through both nested calls");
    result.push_back(calc.display_text());
    result.push_back(calc.program_counter());
    for (const auto* reg : {"X", "Y", "Z", "T", "X1"})
      result.push_back(calc.read_register(reg));
  }
  return result;
}

} // namespace

void exact_decimal_remainder_correction_preserves_observations() {
  CompileOptions options;
  const core::passes::PassContext context{options};
  const std::vector<std::string> inputs{
      "-99999999", "-9999999", "-9999997", "-1000001", "-999999", "-1407",
      "-127", "-64", "-9.9", "-4.9", "-1", "-0.5", "0", "0.5", "1",
      "3.7", "7", "63", "64", "125", "999999", "1000001", "9999997",
      "9999999", "99999999"};
  for (int width : {2, 4, 5, 8, 10, 16, 20, 25, 32, 40, 50, 64, 100, 125,
                   200, 250, 500, 625, 1000, 10000}) {
    const auto proof = prove_exact_decimal_remainder_precision(width);
    require(proof.has_value(), "terminating divisor should have an exact precision proof");
    const auto original = remainder_fixture(width);
    const auto result = exact_decimal_remainder_correction(raise_machine_to_ir(original), context);
    require(result.applied == 1,
            "generic signed remainder diamond should be reduced for " + std::to_string(width));
    const auto before = resolve_fixture(original);
    const auto after = resolve_fixture(lower_ir_to_machine(result.ops));
    require(after == resolve_fixture(remainder_fixture(width, true)),
            "remainder pass should emit the independently specified arithmetic sequence");
    require(before.size() - after.size() == std::to_string(width).size(),
            "remainder correction should save the eliminated divisor literal and branches");
    for (const auto& input : inputs)
      require(remainder_observation(before, input) == remainder_observation(after, input),
              "remainder rewrite must preserve X/Y/Z/T/X1, display/X2, nested returns and "
              "decimal continuation: width=" + std::to_string(width) + " input=" + input);
  }

  for (int width : {-1, 0, 3, 7, 9, 128, 256, 3125, 100000,
                   std::numeric_limits<int>::max()})
    require(!prove_exact_decimal_remainder_precision(width),
            "nonterminating or over-precision divisor must fail closed");
  for (int width : {3, 128}) {
    const auto original = remainder_fixture(width);
    require(exact_decimal_remainder_correction(raise_machine_to_ir(original), context).applied == 0,
            "unproved precision must prevent the actual IR rewrite");
    const auto input = width == 3 ? "-1" : "-1407";
    require(remainder_observation(resolve_fixture(original), input) !=
                remainder_observation(resolve_fixture(remainder_fixture(width, true)), input),
            "precision guard must exclude a real machine rounding counterexample");
  }

  const auto source = raise_machine_to_ir(remainder_fixture(4));
  const auto reject = [&](const std::vector<IrOp>& blocked, const std::string& reason) {
    const auto result = exact_decimal_remainder_correction(blocked, context);
    require(result.applied == 0 && ir_ops_to_json(result.ops) == ir_ops_to_json(blocked), reason);
  };
  auto raw = source;
  raw.front().meta.raw = true;
  reject(raw, "raw program geometry must not be moved");
  auto numeric = source;
  numeric.at(1).target = 6;
  reject(numeric, "numeric address geometry must remain frozen");
  auto formal = source;
  formal.at(1).target_meta.formal_opcode = 0x06;
  reject(formal, "formal address caches must not enter the symbolic rewrite");
  auto ordinary_call = source;
  ordinary_call.at(1).meta.roles = {"statement-proc-call", "x-argument-call"};
  const auto with_call_contract = exact_decimal_remainder_correction(ordinary_call, context);
  require(with_call_contract.applied == 1 &&
              with_call_contract.ops.at(1).meta.roles == ordinary_call.at(1).meta.roles,
          "untouched symbolic calls retain their argument ABI without freezing unrelated code");
  auto protected_call = source;
  protected_call.at(1).meta.roles = {"unknown-layout-contract"};
  reject(protected_call, "unknown command roles remain a conservative geometry barrier");
  auto external = source;
  external.push_back(IrOp{.kind = IrKind::Call, .target = std::string("adjust")});
  reject(external, "an independently entered correction cannot be removed");
  auto manual = source;
  const auto first_int = std::find_if(manual.begin(), manual.end(), [](const IrOp& op) {
    return op.kind == IrKind::Plain && op.opcode == 0x34;
  });
  require(first_int != manual.end(), "fixture should contain its dividend truncation");
  first_int->meta.manual_interaction = ManualInteractionAnchor{};
  reject(manual, "manual stepping inside the diamond must not change");
  reject(raise_machine_to_ir(remainder_fixture(4, false, 5)),
         "different divisor and scale must not match the identity");
  auto with_join_entry = source;
  with_join_entry.push_back(IrOp{.kind = IrKind::Call, .target = std::string("join")});
  require(exact_decimal_remainder_correction(with_join_entry, context).applied == 1,
          "independent entries to the unchanged join remain valid");
  auto renamed = source;
  for (auto& op : renamed) {
    op.meta.comment.reset();
    op.meta.mnemonic.clear();
    if (op.kind == IrKind::Label)
      op.name = "opaque_" + op.name;
    if (auto* label = std::get_if<std::string>(&op.target))
      *label = "opaque_" + *label;
  }
  require(exact_decimal_remainder_correction(renamed, context).applied == 1,
          "the pass must depend on numeric structure, not comments or label names");
}

void compiler_exact_decimal_remainder_correction_is_generic() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  for (const auto* name : {"BucketInput", "LedgerIndex"}) {
    const auto result = compile_source(std::string("program ") + name + R"mkpro( {
  state { value: packed = 0 }
  loop {
    show(0)
    value = entered()
    halt(grid_norm(value))
  }
}
)mkpro", options);
    require(result.implemented && result.diagnostics.empty(), "normalization source should compile");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                         [](const auto& report) {
                           return report.name == "exact-decimal-remainder-correction";
                         }),
            "ordinary compilation should select the generic exact decimal correction");
    require(std::none_of(result.steps.begin(), result.steps.end(), [](const auto& step) {
              return step.comment == "grid_norm negative correction" ||
                     step.comment == "grid_norm positive result";
            }),
            "the emitted normalization should not retain the sign/zero control diamond");
  }
  const auto shared = compile_source(R"mkpro(
program ProcedureNormalization {
  state {
    input_value: packed = 0
    normalized: packed = 0
  }
  loop {
    show(0)
    input_value = entered()
    canonicalize(input_value)
    halt(grid_norm(normalized))
  }
  fn canonicalize(argument) {
    normalized = grid_norm(argument)
  }
}
)mkpro", options);
  require(shared.implemented && shared.diagnostics.empty() &&
              std::any_of(shared.optimizations.begin(), shared.optimizations.end(),
                           [](const auto& report) {
                             return report.name == "exact-decimal-remainder-correction";
                           }),
          "a shared normalization body should be reduced across ordinary procedure calls");
}

} // namespace mkpro::tests
