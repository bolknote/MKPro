#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/resolved_address.hpp"
#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

void require_item(const MachineItem& item, MachineItemKind kind, int opcode,
                  const std::string& message) {
  require(item.kind == kind, message + ": wrong item kind");
  require(item.opcode == opcode, message + ": wrong opcode");
}

} // namespace

void emitter_matches_initial_typescript_contract() {
  {
    const auto projected = [](const MachineItem& item) {
      return std::find(item.roles.begin(), item.roles.end(),
                       kTypedDisplayObservationRole) != item.roles.end();
    };
    MachineEmitter emitter;
    emitter.emit_stop(StopDisposition::Resumable);
    emitter.emit_error_stop(StopDisposition::Terminal);
    emitter.emit_stop(StopDisposition::Unknown);
    emitter.emit_error_stop(StopDisposition::Resumable, "raw", {}, {}, true);
    require(projected(emitter.items.at(0)) && projected(emitter.items.at(1)) &&
                !projected(emitter.items.at(2)) && !projected(emitter.items.at(3)),
            "only typed source stops may project internal deep-stack values");
    const auto roundtrip = lower_ir_to_machine(raise_machine_to_ir(emitter.items));
    require(machine_items_to_json(roundtrip) == machine_items_to_json(emitter.items),
            "the typed display observation contract must survive IR transport");
  }

  {
    MachineEmitter emitter;
    emitter.emit_jump(0x51, "BP", 109, "provisional over-window identity");
    while (emitter.items.size() < 110U)
      emitter.emit_op(0x50, "STOP");
    const ResolvedProgram strict = resolve_machine_items(emitter.items);
    require(std::any_of(strict.diagnostics.begin(), strict.diagnostics.end(),
                        [](const Diagnostic& diagnostic) {
                          return diagnostic.severity == DiagnosticSeverity::Error &&
                                 diagnostic.code == "address-out-of-range";
                        }),
            "a long artifact must not implicitly enable provisional address resolution");

    CompileOptions analysis;
    analysis.analysis = true;
    const ResolvedProgram provisional = resolve_machine_items(emitter.items, analysis);
    require(provisional.diagnostics.empty() && provisional.steps.size() == 110U &&
                provisional.steps.at(1).opcode == -1 &&
                resolved_logical_target(provisional.steps.at(1)) == 109 &&
                provisional.steps.at(1).address_target ==
                    ResolvedAddress{LogicalCodeAddress{109}},
            "explicit analysis must preserve over-window identities for later layout");
    require(physical_program_image_rejection(provisional.steps).has_value(),
            "a provisional logical identity must not escape into a physical image");
    require(!resolve_machine_items({MachineItem::address(-1)}, analysis).diagnostics.empty(),
            "analysis must still reject an invalid negative address");

    CompileOptions expanded;
    expanded.feature_profile = FeatureProfile::Mk61SMiniExpanded;
    const ResolvedProgram physical = resolve_machine_items(emitter.items, expanded);
    require(physical.diagnostics.empty() && physical.steps.size() == 110U &&
                physical.steps.at(1).opcode == 0xa9,
            "strict resolution must use the selected target's physical address space");
  }

  {
    MachineEmitter emitter;
    emitter.emit_number("-12.3e-4");
    const std::vector<int> expected = {1, 2, 0x0a, 3, 0x0b, 0x0c, 4, 0x0b};
    require(emitter.items.size() == expected.size(), "number emission item count mismatch");
    for (std::size_t index = 0; index < expected.size(); ++index) {
      require_item(emitter.items.at(index), MachineItemKind::Op, expected.at(index),
                   "number emission mismatch at " + std::to_string(index));
    }
    require(emitter.items.at(5).comment == "exponent", "exponent comment should be preserved");
    require(emitter.items.at(7).comment == "negative exponent",
            "negative exponent comment should be preserved");
    require(emitter.items.at(4).comment == "negative number",
            "negative mantissa sign must precede exponent entry");
  }


  {
    struct LiteralCase {
      std::string source;
      std::string expected;
    };
    const std::vector<LiteralCase> cases{
        {"-12.3e-4", "-0.00123"}, {"-2e-07", "-0.0000002"},
        {"-2e+07", "-20000000"}, {"-2e0", "-2"},
        {"2e-07", "0.0000002"}, {"2e+07", "20000000"},
        {"-12.3", "-12.3"}, {"12.3", "12.3"}};
    // Register text may preserve fixed or scientific input formatting.
    // Canonicalize the decimal coefficient/exponent exactly, without doubles.
    const auto canonical_number = [](std::string value) {
      std::replace(value.begin(), value.end(), ',', '.');
      std::istringstream input(value);
      std::string coefficient;
      std::string order;
      input >> coefficient;
      int exponent = (input >> order) ? std::stoi(order) : 0;
      const auto dot = coefficient.find('.');
      if (dot != std::string::npos) {
        exponent -= static_cast<int>(coefficient.size() - dot - 1U);
        coefficient.erase(dot, 1U);
      }
      const bool negative = !coefficient.empty() && coefficient.front() == '-';
      if (negative || (!coefficient.empty() && coefficient.front() == '+'))
        coefficient.erase(0, 1U);
      require(!coefficient.empty() &&
                  std::all_of(coefficient.begin(), coefficient.end(),
                              [](char digit) { return digit >= '0' && digit <= '9'; }),
              "ROM result must be a finite decimal number: " + value);
      const auto first = coefficient.find_first_not_of('0');
      if (first == std::string::npos)
        return std::pair<std::string, int>{"0", 0};
      coefficient.erase(0, first);
      while (coefficient.back() == '0') {
        coefficient.pop_back();
        ++exponent;
      }
      if (negative)
        coefficient.insert(coefficient.begin(), '-');
      return std::pair<std::string, int>{coefficient, exponent};
    };
    const auto observe = [&](const std::vector<ResolvedStep>& steps,
                            const std::vector<PreloadReport>& preloads,
                            const std::string& input) {
      std::vector<int> codes;
      for (const auto& step : steps)
        codes.push_back(step.opcode);
      emulator::MK61 calculator;
      require(calculator.load_program(codes).diagnostics.empty(),
              "signed literal ROM program must load");
      for (const auto& preload : preloads)
        calculator.set_register(preload.register_name, preload.value);
      calculator.set_register("X", input);
      calculator.set_register("Y", "13");
      calculator.set_register("Z", "23");
      calculator.set_register("T", "37");
      calculator.press_sequence({"В/О", "С/П"});
      require(calculator.run_until_stable(2000, 6).stopped,
              "signed literal ROM program must stop");
      return canonical_number(calculator.read_register("X"));
    };
    const auto expected_value = [&](const std::string& value) {
      return canonical_number(value);
    };
    for (const auto& item : cases) {
      MachineEmitter emitter;
      emitter.emit_number(item.source);
      emitter.emit_stop(StopDisposition::Terminal);
      const auto resolved = resolve_machine_items(emitter.items);
      require(resolved.diagnostics.empty(), "signed literal must resolve: " + item.source);
      const auto expected = expected_value(item.expected);
      require(observe(resolved.steps, {}, "7") == expected,
              "mantissa and exponent signs must be independent in ROM: " + item.source);
      for (const bool disable_search : {true, false}) {
        CompileOptions options;
        options.disable_candidate_search = disable_search;
        const auto result = compile_source(
            "program SignedLiteral { loop { halt(" + item.source + ") } }", options);
        require(result.implemented, "signed literal high-level fixture must compile: " + item.source);
        require(observe(result.steps, result.preloads, "7") == expected,
                "literal folding and layout must preserve signed scientific values: " + item.source);
      }
    }
    // Residual dispatch may fold a sign inversion into a fractional coefficient.
    // Assert source arithmetic, not equivalence to another generated listing.
    const std::string dispatch = R"(program SignedResidual {
      state { command: packed }
      loop {
        command = entered()
        match command {
          4 => halt(0.000001)
          6 => halt(-0.000001)
          otherwise => halt(sign(6 - command) * 0.0000002)
        }
      }
    })";
    for (const bool disable_search : {true, false}) {
      CompileOptions options;
      options.disable_candidate_search = disable_search;
      const auto result = compile_source(dispatch, options);
      require(result.implemented, "signed residual high-level fixture must compile");
      for (const auto& item : std::vector<LiteralCase>{
               {"1", "0.0000002"}, {"3", "0.0000002"}, {"-1", "0.0000002"},
               {"7", "-0.0000002"}, {"9", "-0.0000002"},
               {"4", "0.000001"}, {"6", "-0.000001"}})
        require(observe(result.steps, result.preloads, item.source) == expected_value(item.expected),
                "residual dispatch must retain the sign and magnitude for input " + item.source);
    }
  }

  {
    MachineEmitter emitter;
    emitter.emit_number("1");
    emitter.emit_number("3");
    require(emitter.items.size() == 3, "adjacent number emission should insert separator");
    require(emitter.items.at(1).opcode == 0x0e, "adjacent number separator should be В↑");
    require(emitter.items.at(1).comment == "separate adjacent number entry",
            "adjacent number separator comment mismatch");
  }

  {
    MachineEmitter emitter;
    emitter.current_x_variable = "score";
    emitter.current_x_aliases.insert("score");
    emitter.emit_jump(0x51, "БП", std::string("loop"), "jump loop", 7);
    require(emitter.items.size() == 2, "jump should emit op and address");
    require(emitter.label_edge_x.at("loop") == "score", "jump should record label X edge");
    require(emitter.items.at(1).kind == MachineItemKind::Address, "jump target should be address");
    require(emitter.items.at(1).comment == "jump loop", "jump address comment mismatch");
    require(emitter.items.at(1).source_line == 7, "jump address source line mismatch");
  }

  {
    MachineEmitter emitter;
    emitter.emit_formal_address(0xa5, "side jump", 9);
    require(emitter.items.size() == 1, "formal address should emit one item");
    require(emitter.items.at(0).kind == MachineItemKind::Address,
            "formal address should emit address item");
    require(std::get<int>(emitter.items.at(0).target) == 105,
            "formal address target should use ordinal");
    require(emitter.items.at(0).formal_opcode == 0xa5,
            "formal address should preserve formal opcode");
    require(emitter.items.at(0).comment == "side jump; formal A5->00",
            "formal address comment mismatch");
    require(emitter.items.at(0).source_line == 9, "formal address source line mismatch");
  }

  {
    MachineEmitter emitter;
    emitter.address_space_model = AddressSpaceModel::Mk61SMiniExpanded;
    emitter.emit_formal_address(0xa5, "expanded cell", 10);
    require(emitter.items.size() == 1, "expanded formal address should emit one item");
    require(std::get<int>(emitter.items.at(0).target) == 105,
            "expanded formal address target should keep ordinal 105");
    require(emitter.items.at(0).formal_opcode == 0xa5,
            "expanded formal address should preserve formal opcode");
    require(emitter.items.at(0).comment == "expanded cell; formal A5->A5",
            "expanded formal address comment should treat A5 as official");
  }

  {
    MachineEmitter emitter;
    emitter.emit_op(0x53, "ПП", "formal call");
    emitter.emit_formal_address(0xe1, "formal target");
    const ResolvedProgram resolved = resolve_machine_items(emitter.items);
    require(resolved.diagnostics.empty(),
            "resolving a high formal address should not validate its ordinal as official");
    require(resolved.steps.size() == 2, "formal call should resolve to two steps");
    require(resolved.steps.at(1).opcode == 0xe1, "formal address should preserve opcode E1");
    require(resolved.steps.at(1).mnemonic == "E1", "formal address mnemonic should be formal");
  }

  {
    MachineEmitter emitter;
    emitter.emit_jump(0x51, "БП", 105, "jump added cell");

    const ResolvedProgram stock = resolve_machine_items(emitter.items);
    require(stock.diagnostics.size() == 1,
            "stock resolver should reject a direct branch to physical address 105");

    CompileOptions expanded_options;
    expanded_options.feature_profile = FeatureProfile::Mk61SMiniExpanded;
    const ResolvedProgram expanded = resolve_machine_items(emitter.items, expanded_options);
    require(expanded.diagnostics.empty(),
            "expanded resolver should accept direct branch to physical address 105");
    require(expanded.steps.size() == 2, "expanded direct branch should resolve to two cells");
    require(expanded.steps.at(1).opcode == 0xa5,
            "expanded direct branch target 105 should encode as A5");
    require(expanded.steps.at(1).mnemonic == "A5",
            "expanded direct branch target 105 should render as A5");
  }

  {
    MachineEmitter emitter;
    const std::string first = emitter.fresh_label("loop");
    const std::string second = emitter.fresh_label("loop");
    require(first == "__loop_0" && second == "__loop_1", "fresh labels should be deterministic");
    emitter.current_x_variable = "x";
    emitter.current_x_aliases.insert("x");
    emitter.record_label_edge("join", std::string("y"));
    emitter.emit_label("join", {.procedure_boundary = "start", .procedure_name = "joinProc"});
    require(!emitter.current_x_variable.has_value(),
            "conflicting label edge should clear current X fact");
    require(emitter.current_x_aliases.empty(), "label merge should clear stale X aliases");
    require(emitter.items.at(0).procedure_boundary == "start",
            "label procedure boundary should be preserved");
    require(emitter.items.at(0).procedure_name == "joinProc",
            "label procedure name should be preserved");
  }

  {
    MachineEmitter emitter;
    emitter.emit_label("start");
    emitter.emit_op(0x41, "X->П 1", "store");
    emitter.emit_jump(0x51, "БП", std::string("start"), "again");
    emitter.emit_label("hidden", {.hidden = true});
    emitter.emit_op(0x50, "С/П", "halt");

    const ResolvedProgram resolved = resolve_machine_items(emitter.items);
    require(resolved.diagnostics.empty(), "simple layout should not produce diagnostics");
    require(resolved.steps.size() == 4, "simple layout step count mismatch");
    require(resolved.steps.at(0).address == 0 && resolved.steps.at(0).opcode == 0x41,
            "first resolved step mismatch");
    require(resolved.steps.at(1).address == 1 && resolved.steps.at(1).hex == "51",
            "jump resolved step mismatch");
    require(resolved.steps.at(2).address == 2 && resolved.steps.at(2).opcode == 0x00,
            "address resolved step should target start label");
    require(resolved.steps.at(2).mnemonic == "00", "address resolved mnemonic mismatch");
    require(resolved.labels.size() == 1 && resolved.labels.at(0).name == "start" &&
                resolved.labels.at(0).address == 0,
            "visible label table mismatch");
  }

  {
    const std::vector<MachineItem> items = {MachineItem::address(std::string("missing"))};
    const ResolvedProgram resolved = resolve_machine_items(items);
    require(resolved.steps.empty(), "unknown label should not produce a step");
    require(resolved.diagnostics.size() == 1, "unknown label should produce one diagnostic");
    require(resolved.diagnostics.at(0).severity == DiagnosticSeverity::Error,
            "unknown label diagnostic severity mismatch");
    require(resolved.diagnostics.at(0).message == "Unknown label 'missing'",
            "unknown label diagnostic text mismatch");
  }
}

} // namespace mkpro::tests
