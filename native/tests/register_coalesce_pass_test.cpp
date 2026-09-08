#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp store(std::string register_name) {
  return make_store(std::move(register_name));
}

IrOp recall(std::string register_name) {
  return make_recall(std::move(register_name));
}

IrOp indirect_store(std::string selector, std::optional<std::string> targets = std::nullopt) {
  IrOp op;
  op.kind = IrKind::IndirectStore;
  op.register_name = std::move(selector);
  op.opcode = 0xb0 + register_index(op.register_name);
  op.meta.mnemonic = "К X->П " + op.register_name;
  if (targets.has_value())
    op.meta.comment = "indirect-memory-targets=" + *targets;
  return op;
}

IrOp indirect_recall(std::string selector, bool discarded) {
  IrOp op;
  op.kind = IrKind::IndirectRecall;
  op.register_name = std::move(selector);
  op.opcode = 0xd0 + register_index(op.register_name);
  op.meta.mnemonic = "К П->X " + op.register_name;
  op.meta.discarded_indirect_recall_value = discarded;
  return op;
}

IrOp indirect_jump(std::string selector) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(selector);
  op.opcode = 0x80 + register_index(op.register_name);
  op.meta.mnemonic = "К БП " + op.register_name;
  return op;
}

IrOp halt() {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "halt";
  op.meta.mnemonic = "С/П";
  return op;
}

core::passes::PassResult run_register_coalesce(const std::vector<IrOp>& ops,
                                               CompileOptions options = CompileOptions{}) {
  return core::passes::register_coalesce(ops, core::passes::PassContext{.options = options});
}

} // namespace

void register_coalesce_matches_typescript_contract() {
  {
    const auto logical_selector = [](IrKind kind, int opcode, std::string name) {
      IrOp op;
      op.kind = kind;
      op.opcode = opcode;
      op.register_name = std::move(name);
      op.meta.logical_register_analysis = true;
      return op;
    };
    IrOp counter;
    counter.kind = IrKind::Loop;
    counter.counter = "L1";
    counter.opcode = 0x5b;
    counter.meta.logical_register_analysis = true;
    counter.meta.logical_register_name = "rounds";
    const std::vector<IrOp> ops{
        counter,
        logical_selector(IrKind::IndirectRecall, 0xd1, "b"),
        logical_selector(IrKind::IndirectStore, 0xb4, "up"),
        logical_selector(IrKind::IndirectCall, 0xa7, "next")};
    const auto domains = core::passes::logical_register_instruction_class_domains(ops);
    require(domains.has_value() && domains->size() == 4 &&
                domains->at("rounds") == std::set<int>({0, 1, 2, 3}) &&
                domains->at("b") == std::set<int>({0, 1, 2, 3}) &&
                domains->at("up") == std::set<int>({4, 5, 6}) &&
                domains->at("next") == std::set<int>({7, 8, 9, 10, 11, 12, 13, 14}),
            "instruction classes must derive from opcodes, not logical names or old colors");
    auto compatible = ops;
    compatible.push_back(logical_selector(IrKind::IndirectStore, 0xb3, "rounds"));
    require(core::passes::logical_register_instruction_class_domains(compatible) == domains,
            "multiple compatible roles must intersect to the same domain");
    auto conflicting = ops;
    conflicting.push_back(logical_selector(IrKind::IndirectRecall, 0xd4, "rounds"));
    require(!core::passes::logical_register_instruction_class_domains(conflicting).has_value(),
            "contradictory counter/selector classes must fail closed");
    auto malformed = ops;
    malformed[0].counter = "L0";
    require(!core::passes::logical_register_instruction_class_domains(malformed).has_value(),
            "an inconsistent loop opcode/name must not establish an allocation domain");
    malformed = ops;
    malformed[0].meta.logical_register_name.reset();
    require(!core::passes::logical_register_instruction_class_domains(malformed).has_value(),
            "a loop without its logical identity must not constrain an unrelated value");
    malformed = ops;
    malformed[1].opcode = 0xb1;
    require(!core::passes::logical_register_instruction_class_domains(malformed).has_value(),
            "a mismatched indirect kind/opcode must fail closed");
    malformed = ops;
    malformed[1].meta.logical_register_analysis = false;
    require(!core::passes::logical_register_instruction_class_domains(malformed).has_value(),
            "physical identities must not be guessed in a logical-domain analysis");
    malformed = ops;
    malformed[1].opcode = 0xdf;
    require(!core::passes::logical_register_instruction_class_domains(malformed).has_value(),
            "opcode-F aliases must not be guessed to mean an ordinary stable RF selector");
    const auto empty = core::passes::logical_register_instruction_class_domains({halt()});
    require(empty.has_value() && empty->empty(),
            "code without class-sensitive operations needs no speculative constrained variant");

    core::passes::RegisterInterferenceGraph graph;
    std::vector<std::string> values{"rounds"};
    for (int index = 0; index < 15; ++index)
      values.push_back("value_" + std::to_string(index));
    for (const auto& left : values)
      for (const auto& right : values)
        if (left != right)
          graph.neighbors[left].insert(right);
    core::passes::PrecoloredRegisterAllocationOptions allocation;
    allocation.greedy_only = true;
    allocation.prioritize_constrained_domains = true;
    allocation.allowed_colors["rounds"] = {0, 1, 2, 3};
    require(!core::passes::color_precolored_register_graph(graph, allocation).has_value(),
            "class-aware coloring must not invent a sixteenth stock register");
    allocation.color_count = 16;
    const auto expanded = core::passes::color_precolored_register_graph(graph, allocation);
    require(expanded.has_value() && expanded->at("rounds") <= 3 &&
                std::any_of(expanded->begin(), expanded->end(),
                            [](const auto& entry) { return entry.second == 15; }),
            "an expanded palette may place ordinary data in RF while retaining the FL class");
  }
  {
    core::passes::RegisterInterferenceGraph graph;
    graph.neighbors["a"].insert("b");
    graph.neighbors["b"].insert("a");
    core::passes::PrecoloredRegisterAllocationOptions options;
    options.color_count = 2;
    options.allowed_colors = {{"a", {0, 1}}, {"b", {0}}};
    const auto result = core::passes::color_precolored_register_graph(graph, options);
    require(result.has_value() && result->at("a") == 1 && result->at("b") == 0,
            "register domains must distinguish unused colors during exact backtracking");
    options.greedy_only = true;
    require(!core::passes::color_precolored_register_graph(graph, options).has_value(),
            "a speculative domain coloring failure must not start exhaustive search");
    options.prioritize_constrained_domains = true;
    const auto constrained = core::passes::color_precolored_register_graph(graph, options);
    require(constrained.has_value() && constrained->at("a") == 1 && constrained->at("b") == 0,
            "bounded MRV coloring must reserve the only admissible color before a broad domain");
    require(core::passes::color_precolored_register_graph(graph, options) == constrained,
            "constrained greedy coloring must be deterministic");
    options.fixed_colors["b"] = 1;
    require(!core::passes::color_precolored_register_graph(graph, options).has_value(),
            "fixed assignments must obey hardware register domains");
    options.fixed_colors.clear();
    options.allowed_colors["isolated"] = {};
    require(!core::passes::color_precolored_register_graph({}, options).has_value(),
            "an empty domain is not an unconstrained or absent value");
  }
  {
    CompileOptions options;
    options.coalesce_copies = true;
    const auto plain = [](int opcode) {
      IrOp op;
      op.kind = IrKind::Plain;
      op.opcode = opcode;
      return op;
    };
    const auto stop = [](bool terminal) {
      IrOp op = halt();
      op.meta.stop_disposition = terminal ? StopDisposition::Terminal : StopDisposition::Resumable;
      return op;
    };
    const auto label = [](const std::string& name) {
      IrOp op;
      op.kind = IrKind::Label;
      op.name = name;
      return op;
    };
    const auto call = [](const std::string& target) {
      IrOp op;
      op.kind = IrKind::Call;
      op.opcode = 0x53;
      op.target = target;
      return op;
    };
    IrOp finish;
    finish.kind = IrKind::Return;
    finish.opcode = 0x52;
    IrOp indexed = indirect_recall("d", false);
    indexed.meta.indirect_memory_targets = std::vector<int>{7};
    const std::vector<IrOp> epochs{
        recall("1"), store("d"), call("observe"), stop(false),
        plain(7), store("d"), indexed, stop(true),
        label("observe"), recall("d"), finish};
    const auto run = [&](const std::vector<IrOp>& code) {
      return core::passes::register_web_copy_coalesce(code, {options});
    };
    const auto optimized = run(epochs);
    require(optimized.applied == 1 && optimized.ops.size() + 1 == epochs.size(),
            "a direct-value web must coalesce independently of a later indirect-selector epoch");
    require(std::any_of(optimized.ops.begin(), optimized.ops.end(), [](const IrOp& op) {
              return op.kind == IrKind::IndirectRecall && op.register_name == "d";
            }), "web splitting must preserve the selector's physical register");

    auto reused_helper = epochs;
    reused_helper.insert(reused_helper.begin() + 7, call("observe"));
    require(run(reused_helper).applied == 0,
            "one shared read instruction must keep the same operand across all caller contexts");
    auto hardware_observer = epochs;
    hardware_observer[2] = indexed;
    require(run(hardware_observer).applied == 0,
            "an indirect use in the copied web must pin that web, not just its later epoch");
    auto unknown = epochs;
    unknown[6].meta.indirect_memory_targets.reset();
    require(run(unknown).applied == 0, "unresolved indirect memory must reject web coalescing");
    auto opaque = epochs;
    opaque[1].meta.raw = true;
    require(run(opaque).applied == 0, "raw register contracts must remain fixed");
    auto extended = epochs;
    extended[0] = recall("f");
    require(run(extended).applied == 0, "Rf must not become a standard allocation color");
    const std::vector<IrOp> diverging{
        recall("1"), store("d"), plain(9), store("1"), recall("d"), recall("1"), stop(true)};
    const auto recolored = run(diverging);
    require(recolored.applied == 1 && recolored.ops.size() + 1 == diverging.size() &&
                std::any_of(recolored.optimizations.begin(), recolored.optimizations.end(),
                            [](const auto& item) { return item.name == "register-web-joint-coloring"; }),
            "a movable overwrite epoch must be recolored instead of blocking an equal-value copy");
    require(recolored.ops[2].kind == IrKind::Store && recolored.ops[2].register_name != "1",
            "joint coloring must actually move the conflicting overwrite, not lose its value");

    IrOp loop = call("after-counter");
    loop.kind = IrKind::Loop;
    loop.opcode = 0x5b;
    loop.counter = "L1";
    loop.meta.manual_interaction.emplace();
    const std::vector<IrOp> anchored_overwrite{
        recall("1"), store("d"), plain(1), store("1"), loop,
        label("after-counter"), recall("d"), recall("1"), stop(true)};
    require(run(anchored_overwrite).applied == 0,
            "an operator-anchored counter epoch must not move to remove a copy");
    for (const char reg : std::string("023456789abcde"))
      options.preloaded_constant_registers[std::string(1, reg)] = "5";
    require(run(diverging).applied == 0,
            "joint coloring must not borrow a compiler-owned constant pool as mutable storage");
    options.preloaded_constant_registers.clear();

    IrOp choose = call("otherwise");
    choose.kind = IrKind::CondJump;
    choose.opcode = 0x57;
    IrOp join = call("join");
    join.kind = IrKind::Jump;
    join.opcode = 0x51;
    const std::vector<IrOp> merged_definitions{
        recall("3"), choose, recall("1"), store("d"), join,
        label("otherwise"), plain(9), store("d"), label("join"), recall("d"), stop(true)};
    require(run(merged_definitions).applied == 1,
            "a joined web may move ordinary definitions when the old source is dead");
    options.preloaded_constant_registers["1"] = "5";
    require(run(epochs).applied == 1,
            "a read-only constant copy can disappear without introducing a constant write");
    require(run(merged_definitions).applied == 0,
            "web coalescing must not make a compiler-owned constant pool register mutable");
    options.preloaded_constant_registers.clear();

    const auto observe = [&](const std::vector<IrOp>& code, const std::string& input) {
      const auto labels = core::passes::calculate_label_addresses(code);
      auto resolved = code;
      for (auto& op : resolved)
        if (opcode_by_code(op.opcode).takes_address && op.kind != IrKind::Label) {
          const auto* target = std::get_if<std::string>(&op.target);
          require(target != nullptr && labels.contains(*target), "web fixture needs symbolic targets");
          op.target_meta.formal_opcode = official_address_to_opcode(labels.at(*target));
        }
      std::vector<int> bytes;
      for (const auto& cell : lower_ir_to_layout(resolved).cells)
        bytes.push_back(cell.opcode);
      require(bytes.size() <= 105, "web fixture must fit an ordinary MK-61");
      emulator::MK61 calc;
      require(calc.load_program(bytes).diagnostics.empty(), "web fixture must load");
      calc.set_register("1", input);
      calc.set_register("7", "2.5");
      calc.set_register("Y", "73");
      calc.set_register("Z", "29");
      calc.set_register("T", "17");
      calc.press_sequence({"В/О", "С/П"});
      std::vector<std::string> result;
      for (int phase = 0; phase < 2; ++phase) {
        require(calc.run_until_stable(2000, 6).stopped, "web fixture must preserve stops and returns");
        result.push_back(calc.display_text());
        for (const char* reg : {"X", "Y", "Z", "T", "X1"})
          result.push_back(calc.read_register(reg));
        if (phase == 0)
          calc.press("С/П");
        else
          result.push_back(calc.read_register("d"));
      }
      return result;
    };
    for (const auto& input : {"0", "-7", "0.125", "12345"})
      require(observe(epochs, input) == observe(optimized.ops, input),
              "web coalescing must preserve stack, displays, return continuations and selector use");

    const std::vector<IrOp> nested_recoloring{
        recall("1"), store("d"), call("overwrite"), call("combine"), stop(false),
        plain(0x0c), plain(2), stop(true),
        label("overwrite"), plain(9), store("1"), finish,
        label("combine"), recall("d"), recall("1"), plain(0x10), finish};
    const auto nested_optimized = run(nested_recoloring);
    require(nested_optimized.applied == 1 &&
                nested_optimized.ops.size() + 1 == nested_recoloring.size(),
            "joint epoch allocation must cross matched nested calls without adding spills");
    const auto observe_recoloring = [&](const std::vector<IrOp>& code,
                                         const std::string& input,
                                         const std::string& input_register = "1",
                                         const std::string& watched_counter = "") {
      const auto labels = core::passes::calculate_label_addresses(code);
      auto resolved = code;
      for (auto& op : resolved)
        if (op.kind != IrKind::Label && opcode_by_code(op.opcode).takes_address)
          op.target_meta.formal_opcode = official_address_to_opcode(
              labels.at(std::get<std::string>(op.target)));
      std::vector<int> bytes;
      for (const auto& cell : lower_ir_to_layout(resolved).cells)
        bytes.push_back(cell.opcode);
      require(bytes.size() <= 105, "recolored fixture must fit stock MK-61 memory");
      emulator::MK61 calc;
      require(calc.load_program(bytes).diagnostics.empty(), "recolored fixture must load");
      calc.set_register(input_register, input);
      calc.set_register("Y", "73");
      calc.set_register("Z", "29");
      calc.set_register("T", "17");
      calc.press_sequence({"В/О", "С/П"});
      std::vector<std::string> observations;
      for (int phase = 0; phase < 2; ++phase) {
        require(calc.run_until_stable(2000, 6).stopped,
                "joint recoloring must preserve every return and stop");
        observations.push_back(calc.display_text());
        for (const char* reg : {"X", "Y", "Z", "T", "X1"})
          observations.push_back(calc.read_register(reg));
        if (!watched_counter.empty())
          observations.push_back(calc.read_register(watched_counter));
        if (phase == 0)
          calc.press("С/П");
      }
      return observations;
    };
    for (const auto& input : {"0", "-7", "0.125", "12345"})
      require(observe_recoloring(nested_recoloring, input) ==
                  observe_recoloring(nested_optimized.ops, input),
              "joint coloring must preserve stack, X1, VP-observed X2 and matched returns");
    const auto counter_fixture = [&](int source, int destination) {
      IrOp count = call("count");
      count.kind = IrKind::Loop;
      count.counter = "L" + std::to_string(destination);
      count.opcode = std::vector<int>{0x5d, 0x5b, 0x58, 0x5a}.at(
          static_cast<std::size_t>(destination));
      return std::vector<IrOp>{
          plain(0), store("e"), recall(std::to_string(source)),
          store(std::to_string(destination)), call("count"), stop(false),
          plain(0x0c), plain(2), stop(true),
          label("count"), recall("e"), plain(1), plain(0x10), store("e"), count,
          recall("e"), finish};
    };
    for (int source = 0; source < 4; ++source) {
      for (int destination = 0; destination < 4; ++destination) {
        if (source == destination)
          continue;
        const auto fixture = counter_fixture(source, destination);
        const auto colored = run(fixture);
        require(colored.applied == 1 && colored.ops.size() + 1 == fixture.size(),
                "a dead source value must supply a counter epoch without a copy store");
        require(std::any_of(colored.optimizations.begin(), colored.optimizations.end(),
                            [](const auto& item) {
                              return item.name == "register-web-counter-role-coalescing";
                            }), "counter role reuse must explain its constrained allocation");
        require(std::any_of(colored.ops.begin(), colored.ops.end(), [&](const IrOp& op) {
                  return op.kind == IrKind::Loop &&
                         op.counter == "L" + std::to_string(source);
                }), "the executable FL operand must follow the selected counter web");
        for (const auto& input : {"1", "2", "4", "3.75"})
          require(observe_recoloring(fixture, input, std::to_string(source)) ==
                      observe_recoloring(colored.ops, input, std::to_string(source)),
                  "all twelve FL relocations must preserve iteration count, stack, X1, "
                  "VP-observed X2 and return/stop continuations");

        // Zero and negative FL counters do not terminate. Observe matching
        // loop iterations, not a wall-time-dependent number of instructions.
        // Insert the same checkpoint into the already proved original/rewrite.
        const auto checkpoints = [&](std::vector<IrOp> code) {
          const auto branch = std::find_if(code.begin(), code.end(), [](const IrOp& op) {
            return op.kind == IrKind::Loop;
          });
          require(branch != code.end(), "counter fixture must contain its FL branch");
          code.insert(branch, stop(false));
          return code;
        };
        const auto observed_original = checkpoints(fixture);
        const auto observed_colored = checkpoints(colored.ops);
        for (const auto& input : {"0", "-2"})
          require(observe_recoloring(observed_original, input, std::to_string(source),
                                     std::to_string(destination)) ==
                      observe_recoloring(observed_colored, input, std::to_string(source),
                                         std::to_string(source)),
                  "nonterminating FL counters must preserve each observed iteration and stack");

        auto live_source = fixture;
        live_source.insert(live_source.begin() + 5, recall(std::to_string(source)));
        require(run(live_source).applied == 0,
                "a source observed after the loop cannot become a destructive counter");
      }
    }
    const auto counter = counter_fixture(0, 1);
    require(run(counter_fixture(4, 1)).applied == 0,
            "FL counters must never be retargeted outside R0..R3");
    for (const bool manual : {false, true}) {
      auto anchored = counter;
      if (manual) anchored[14].meta.manual_interaction.emplace();
      else anchored[14].meta.raw = true;
      require(run(anchored).applied == 0,
              "manual and raw counter operands retain their physical register");
    }
    auto malformed = counter;
    malformed[14].counter = "L2";
    require(run(malformed).applied == 0,
            "inconsistent opcode/counter metadata must fail closed");
    options.preloaded_constant_registers["0"] = "4";
    require(run(counter).applied == 0,
            "a compiler-owned pool entry cannot be borrowed as a mutable counter");
    options.preloaded_constant_registers.clear();
    auto later_selector = counter;
    IrOp select = indirect_recall("1", false);
    select.meta.indirect_memory_targets = std::vector<int>{6};
    later_selector.insert(later_selector.begin() + 5, {plain(7), store("1"), select});
    const auto separate_epochs = run(later_selector);
    require(separate_epochs.applied == 1 &&
                std::any_of(separate_epochs.ops.begin(), separate_epochs.ops.end(),
                            [](const IrOp& op) {
                              return op.kind == IrKind::IndirectRecall && op.register_name == "1";
                            }), "moving a counter phase must leave a later selector phase anchored");
    require(observe_recoloring(later_selector, "4", "0") ==
                observe_recoloring(separate_epochs.ops, "4", "0"),
            "phase-local counter relocation must preserve a subsequent indirect access");
    auto large_counter = counter;
    large_counter.insert(large_counter.begin() + 9, 110, plain(0x54));
    require(run(large_counter).applied == 1,
            "counter domains must operate on symbolic regions above 105 cells");

    const auto repeated = run(nested_recoloring);
    require(repeated.ops.size() == nested_optimized.ops.size(),
            "joint coloring must have deterministic size");
    for (std::size_t i = 0; i < repeated.ops.size(); ++i)
      require(repeated.ops[i].opcode == nested_optimized.ops[i].opcode &&
                  repeated.ops[i].register_name == nested_optimized.ops[i].register_name,
              "joint coloring must have deterministic assignments");

    auto logical = epochs;
    logical.insert(logical.begin() + 8, 110, plain(0x54));
    require(run(logical).applied == 1,
            "logical helper addresses above 105 must not hide a relocatable copy opportunity");
    auto large_recoloring = nested_recoloring;
    large_recoloring.insert(large_recoloring.begin() + 8, 110, plain(0x54));
    require(run(large_recoloring).applied == 1,
            "joint coloring must also analyze symbolic helper addresses above 105");
  }

  {
    const std::string source = R"mkpro(program ValueEpochs {
      state {
        input: packed = 2
        remembered: packed
      }
      loop {
        remembered = input
        advance()
        show(remembered)
        show(input)
        halt(0)
      }
      fn advance() {
        input += 9
      }
    })mkpro";
    for (const bool coalesce : {false, true}) {
      CompileOptions options;
      options.disable_candidate_search = true;
      options.coalesce_copies = coalesce;
      const auto result = compile_source(source, options);
      require(result.implemented && result.diagnostics.empty() && result.steps.size() <= 105,
              "the source-level value epoch fixture must compile for stock MK-61");
      std::vector<int> codes;
      for (const auto& step : result.steps)
        codes.push_back(step.opcode);
      emulator::MK61 calc;
      require(calc.load_program(codes).diagnostics.empty(), "compiled value epochs must load");
      for (const auto& preload : result.preloads)
        calc.set_register(preload.register_name, preload.value);
      calc.press_sequence({"В/О", "С/П"});
      for (const int expected : {2, 11, 0}) {
        require(calc.run_until_stable(2000, 6).stopped &&
                    std::stod(calc.display_text()) == expected,
                "compiler allocation must preserve values retained across the overwritten epoch");
        calc.press("С/П");
      }
    }
  }

  {
    const auto label = [](const std::string& name) {
      IrOp op;
      op.kind = IrKind::Label;
      op.name = name;
      return op;
    };
    const auto flow = [](IrKind kind, int opcode, const std::string& target = "") {
      IrOp op;
      op.kind = kind;
      op.opcode = opcode;
      op.target = target;
      return op;
    };
    const auto digit = [](int value) {
      IrOp op;
      op.kind = IrKind::Plain;
      op.opcode = value;
      return op;
    };
    const std::vector<IrOp> program = {
        digit(5), store("1"), flow(IrKind::Call, 0x53, "outer"), recall("1"), halt(),
        digit(7), store("2"), flow(IrKind::Call, 0x53, "outer"), recall("2"), halt(),
        flow(IrKind::Jump, 0x51, "done"), label("outer"),
        flow(IrKind::Call, 0x53, "inner"), flow(IrKind::Return, 0x52), label("inner"),
        digit(3), store("3"), flow(IrKind::Return, 0x52), label("done"), halt(),
    };
    const auto mapping = core::passes::compute_non_overlapping_register_mapping(program);
    require(mapping.contains("2") && mapping.at("2") == "1",
            "matched caller lifetimes did not free a physical register");
    const auto optimized = run_register_coalesce(program);
    require(optimized.applied > 0, "matched-call allocation did not reach the ordinary IR pass");
    const auto codes = [](const std::vector<IrOp>& ops) {
      const auto addresses = core::passes::calculate_label_addresses(ops);
      std::vector<IrOp> resolved = ops;
      for (IrOp& operation : resolved) {
        if (operation.kind != IrKind::Call && operation.kind != IrKind::Jump)
          continue;
        const auto* target = std::get_if<std::string>(&operation.target);
        require(target != nullptr && addresses.contains(*target),
                "matched-call fixture must resolve every forward label");
        operation.target_meta.formal_opcode = official_address_to_opcode(addresses.at(*target));
      }
      std::vector<int> result;
      for (const LayoutIrCell& cell : lower_ir_to_layout(resolved).cells)
        result.push_back(cell.opcode);
      return result;
    };
    const auto observe = [&](const std::vector<IrOp>& ops) {
      emulator::MK61 calc;
      require(calc.load_program(codes(ops)).diagnostics.empty(),
              "matched-call fixture did not load on the real emulator");
      calc.press_sequence({"В/О", "С/П"});
      std::vector<std::string> observations;
      for (int stop = 0; stop < 3; ++stop) {
        require(calc.run_until_stable(2000, 5).stopped,
                "matched-call fixture lost a stop or return continuation");
        observations.push_back(calc.display_text(true));
        observations.push_back(calc.program_counter());
        observations.push_back(calc.read_register("3"));
        for (const char* reg : {"x", "y", "z", "t", "x1"})
          observations.push_back(calc.read_register(reg));
        if (stop != 2)
          calc.press("С/П");
      }
      return observations;
    };
    require(observe(program) == observe(optimized.ops),
            "register reuse changed a display, nested return, or callee register");
  }

  {
    const std::string source = R"mkpro(program CallerPhases {
      state {
        epoch: counter 0..99 = 0
        first: counter 0..99 = 0
        second: counter 0..99 = 0
      }
      loop {
        first = epoch + 3
        tick()
        show(first)
        second = epoch + 5
        tick()
        show(second)
      }
      fn tick() {
        epoch += 1
        show(epoch)
      }
    })mkpro";
    CompileOptions options;
    options.disable_candidate_search = true;
    const CompileResult result = compile_source(source, options);
    require(result.implemented && result.diagnostics.empty(),
            "ordinary source with phased caller values must compile");
    std::vector<int> codes;
    for (const ResolvedStep& step : result.steps)
      codes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(),
            "phased-caller compiler fixture did not load");
    for (const PreloadReport& preload : result.preloads)
      calc.set_register(preload.register_name, preload.value);
    calc.press_sequence({"В/О", "С/П"});
    for (const int expected : {1, 3, 2, 6, 3, 5, 4, 8}) {
      require(calc.run_until_stable(2000, 5).stopped,
              "phased-caller source lost an observable stop");
      require(std::stod(calc.display_text()) == expected,
              "phased-caller source changed a value retained across a call");
      calc.press("С/П");
    }
  }

  {
    core::passes::RegisterInterferenceGraph graph;
    for (int index = 0; index < 20; ++index)
      graph.neighbors.try_emplace("logical_" + std::to_string(index));
    const auto allocation = core::passes::color_precolored_register_graph(
        graph, core::passes::PrecoloredRegisterAllocationOptions{.color_count = 15});

    require(allocation.has_value() && allocation->size() == 20U,
            "logical coloring incorrectly limited the number of disjoint live ranges");
    require(std::all_of(allocation->begin(), allocation->end(),
                        [](const auto& entry) { return entry.second >= 0 && entry.second < 15; }),
            "logical coloring emitted a color outside the standard R0..Re profile");
  }

  {
    core::passes::RegisterInterferenceGraph graph;
    graph.neighbors["hardware_0"].insert("live");
    graph.neighbors["live"].insert("hardware_0");
    graph.neighbors.try_emplace("late");
    const auto allocation = core::passes::color_precolored_register_graph(
        graph, core::passes::PrecoloredRegisterAllocationOptions{
                   .color_count = 2,
                   .fixed_colors = {{"hardware_0", 0}},
                   .preferred_colors = {{"live", 0}, {"late", 0}},
               });

    require(allocation.has_value() && allocation->at("hardware_0") == 0 &&
                allocation->at("live") == 1 && allocation->at("late") == 0,
            "precolored logical allocation did not preserve a physical anchor while reusing it "
            "outside the anchor's lifetime");
  }

  {
    core::passes::RegisterInterferenceGraph graph;
    for (int left = 0; left < 16; ++left) {
      const std::string left_name = "live_" + std::to_string(left);
      graph.neighbors.try_emplace(left_name);
      for (int right = left + 1; right < 16; ++right) {
        const std::string right_name = "live_" + std::to_string(right);
        graph.neighbors[left_name].insert(right_name);
        graph.neighbors[right_name].insert(left_name);
      }
    }
    const auto allocation = core::passes::color_precolored_register_graph(
        graph, core::passes::PrecoloredRegisterAllocationOptions{.color_count = 15});

    require(!allocation.has_value(),
            "logical coloring accepted sixteen simultaneously live values on R0..Re");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), halt(), store("2"), recall("2")};
    const core::passes::PassResult result = run_register_coalesce(program);

    require(result.applied == 1,
            "register-coalesce did not rewrite non-overlapping direct live ranges");
    require(!std::any_of(result.ops.begin(), result.ops.end(),
                         [](const IrOp& op) {
                           return (op.kind == IrKind::Store || op.kind == IrKind::Recall) &&
                                  op.register_name == "2";
                         }),
            "register-coalesce left a direct R2 access after coalescing");
    require(result.optimizations.size() == 1,
            "register-coalesce did not report non-overlap optimization");
    require(result.optimizations.at(0).name == "register-coalesce",
            "register-coalesce reported wrong optimization name");
  }

  {
    IrOp manual_store = store("1");
    manual_store.meta.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 2,
        .phase = 0,
        .kind = ManualInteractionAnchorKind::SingleStepCommand,
    };
    const std::vector<IrOp> program = {manual_store, recall("1"), halt(), store("2"), recall("2")};
    const core::passes::PassResult result = run_register_coalesce(program);

    require(result.applied == 1,
            "manual interaction blocked a proved disjoint register lifetime");
    require(result.ops.front().register_name == "1" &&
                result.ops.front().meta.manual_interaction.has_value(),
            "register coalescing rewrote the manually entered register command");
    require(result.ops.at(3).register_name == "1" && result.ops.at(4).register_name == "1",
            "disjoint post-interaction lifetime did not reuse the anchored register");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), store("2"),
                                       store("1"), recall("2"), halt()};
    const std::map<std::string, std::string> plain =
        core::passes::compute_non_overlapping_register_mapping(program);
    const std::map<std::string, std::string> def_aware =
        core::passes::compute_non_overlapping_register_mapping(
            program, core::passes::RegisterCoalesceMappingOptions{.def_aware = true});

    require(plain.empty(),
            "register-coalesce ignored a direct definition that clobbers a live register");
    require(def_aware.empty(),
            "register-coalesce def-aware mapping failed to block dead-store clobber");
    const core::passes::RegisterInterferenceGraph graph =
        core::passes::build_register_interference_graph(program);
    require(graph.interferes("1", "2"),
            "interference graph omitted a definition-versus-live-value conflict");
  }

  {
    const std::vector<IrOp> program = {store("1"),  recall("1"), halt(),     store("2"),
                                       recall("2"), halt(),      store("3"), recall("3")};
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(program);

    require(mapping.size() == 2U && mapping.at("2") == "1" && mapping.at("3") == "1",
            "interference coloring did not merge three pairwise-disjoint lifetimes into one "
            "register");
  }

  {
    const std::vector<IrOp> program = {
        store("f"), store("0"), recall("0"), store("1"),
        recall("1"), recall("f"), store("e"), recall("e"),
    };
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(program);

    require(mapping.contains("f") && mapping.at("f") == "e",
            "register coloring retained temporary Rf as the representative of a movable color");
  }

  {
    const std::vector<IrOp> program = {recall("1"), store("2"), recall("2")};
    const core::passes::PassResult result = run_register_coalesce(program);
    const std::map<std::string, std::string> allocator_mapping =
        core::passes::compute_non_overlapping_register_mapping(
            program, core::passes::RegisterCoalesceMappingOptions{
                         .allow_live_at_entry_anchor_reuse = true,
                     });

    require(result.applied == 0,
            "late IR register pass reused an entry value without updating setup metadata");
    require(allocator_mapping.size() == 1U && allocator_mapping.at("2") == "1",
            "source-level allocator did not reuse a live-at-entry register after its final use");
  }

  {
    const std::vector<IrOp> program = {recall("1")};
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(
            program, core::passes::RegisterCoalesceMappingOptions{
                         .allow_live_at_entry_anchor_reuse = true,
                         .allocated_registers = {"1", "2"},
                     });

    require(mapping.size() == 1U && mapping.at("2") == "1",
            "source-level allocator did not reclaim an allocated register absent from the body");
  }

  {
    const std::vector<IrOp> program = {recall("1"), recall("2")};
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(program);

    require(mapping.empty(),
            "register-coalesce merged two values that are simultaneously live at entry");
  }

  {
    const std::vector<IrOp> program = {store("0"), indirect_store("0", "4,5,6,7"), halt(),
                                       store("3"), recall("3")};
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(
            program, core::passes::RegisterCoalesceMappingOptions{.def_aware = true});

    require(mapping.contains("3") && mapping.at("3") == "0",
            "def-aware register mapping did not reuse an indirect keep register with proved "
            "disjoint targets and live ranges");
  }

  {
    const std::vector<IrOp> program = {store("0"), indirect_store("0"), halt(), store("3"),
                                       recall("3")};
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(
            program, core::passes::RegisterCoalesceMappingOptions{.def_aware = true});

    require(mapping.empty(),
            "def-aware register mapping accepted an unknown indirect-memory target set");
  }

  {
    const std::vector<IrOp> observed = {store("0"), recall("0"), indirect_recall("4", false),
                                        store("3"), recall("3")};
    require(core::passes::compute_non_overlapping_register_mapping(observed).empty(),
            "register mapping ignored an observed unknown indirect recall");

    const std::vector<IrOp> discarded = {store("0"), recall("0"), indirect_recall("4", true),
                                         store("3"), recall("3")};
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(discarded);
    require(mapping.contains("3") && mapping.at("3") == "0",
            "register mapping treated a proved-discarded indirect recall as a memory read");
  }

  {
    const std::vector<IrOp> program = {store("1"), store("3"), recall("3"),
                                       indirect_store("e", "1,2"), recall("1")};
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(program);

    require(mapping.contains("3") && mapping.at("3") == "2",
            "may-def liveness did not preserve the old possible-target value across an indirect "
            "store");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), indirect_jump("e"), store("2"),
                                       recall("2")};
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(program);

    require(mapping.empty(),
            "register coloring crossed an unknown indirect-flow full-register barrier");
  }

  {
    IrOp display_store = store("2");
    display_store.meta.comment = "display rendered digit";
    IrOp display_recall = recall("2");
    display_recall.meta.comment = "display rendered digit";
    const std::vector<IrOp> program = {store("1"), recall("1"), halt(), display_store,
                                       display_recall};
    const core::passes::PassResult result = run_register_coalesce(program);

    require(result.applied == 0,
            "register-coalesce ignored display-focus-sensitive register exclusion");
    require(result.ops.back().kind == IrKind::Recall && result.ops.back().register_name == "2",
            "register-coalesce rewrote display-focus-sensitive recall");
  }

  {
    CompileOptions options;
    options.coalesce_copies = true;
    const std::vector<IrOp> program = {recall("1"), store("2"), recall("2")};
    const core::passes::PassResult result = run_register_coalesce(program, options);

    require(result.applied == 1, "copy-coalesce did not merge a sole non-diverging copy");
    require(result.ops.size() == 2, "copy-coalesce did not drop the redundant store");
    require(result.ops.at(0).kind == IrKind::Recall && result.ops.at(0).register_name == "1",
            "copy-coalesce changed source recall unexpectedly");
    require(result.ops.at(1).kind == IrKind::Recall && result.ops.at(1).register_name == "1",
            "copy-coalesce did not rewrite destination recall to source register");
    require(result.optimizations.size() == 1 && result.optimizations.at(0).name == "copy-coalesce",
            "copy-coalesce reported wrong optimization metadata");
  }

  {
    CompileOptions options;
    options.coalesce_copies = true;
    const std::vector<IrOp> program = {
        recall("1"), store("2"), store("1"), recall("2"),
    };
    const core::passes::PassResult result = run_register_coalesce(program, options);

    require(result.applied == 0,
            "copy-coalesce merged registers that interfere after the source is overwritten");
    require(result.ops.size() == program.size(),
            "rejected interfering copy changed the program");
  }

  {
    IrOp raw_store = store("1");
    raw_store.meta.raw = true;
    const std::vector<IrOp> program = {raw_store, recall("1"), halt(), store("2"), recall("2")};
    const core::passes::PassResult result = run_register_coalesce(program);

    require(result.applied == 0, "register-coalesce crossed a raw rewrite barrier");
    require(result.ops.size() == program.size(), "register-coalesce changed raw-barrier program");
    require(core::passes::compute_non_overlapping_register_mapping(program).empty(),
            "register mapping exposed a forced-share candidate across a raw command");
  }

  {
    IrOp raw_noop;
    raw_noop.kind = IrKind::Plain;
    raw_noop.opcode = 0x54;
    raw_noop.meta.mnemonic = "К НОП";
    raw_noop.meta.raw = true;
    const std::vector<IrOp> program = {
        store("1"), recall("1"), raw_noop, store("2"), recall("2"),
    };
    const core::passes::PassResult result = run_register_coalesce(program);

    require(result.applied > 0,
            "register-coalesce treated a register-free raw command as a rewrite barrier");
    require(!core::passes::compute_non_overlapping_register_mapping(program).empty(),
            "register mapping rejected a register-free raw command");
  }
}

} // namespace mkpro::tests
