#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  return op;
}

IrOp recall(std::string register_name) {
  IrOp op;
  op.kind = IrKind::Recall;
  op.register_name = std::move(register_name);
  op.opcode = 0x60;
  op.meta.mnemonic = "П->X";
  return op;
}

IrOp store(std::string register_name) {
  IrOp op;
  op.kind = IrKind::Store;
  op.register_name = std::move(register_name);
  op.opcode = 0x40;
  op.meta.mnemonic = "X->П";
  return op;
}

IrOp indirect_store(std::string selector, std::optional<std::vector<int>> targets) {
  IrOp op;
  op.kind = IrKind::IndirectStore;
  op.register_name = std::move(selector);
  op.opcode = 0xb0;
  op.meta.mnemonic = "К X->П";
  op.meta.indirect_memory_targets = std::move(targets);
  return op;
}

IrOp indirect_recall(std::string selector, std::optional<std::vector<int>> targets) {
  IrOp op;
  op.kind = IrKind::IndirectRecall;
  op.register_name = std::move(selector);
  op.opcode = 0xd0;
  op.meta.mnemonic = "К П->X";
  op.meta.indirect_memory_targets = std::move(targets);
  return op;
}

IrOp indirect_jump(std::string selector) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(selector);
  op.opcode = 0x80;
  op.meta.mnemonic = "К БП";
  return op;
}

IrOp plain(int opcode, std::string mnemonic) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  return op;
}

IrOp jump_to(std::string target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = std::move(target);
  op.meta.mnemonic = "БП";
  return op;
}

IrOp jump_to_address(int target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = target;
  op.meta.mnemonic = "БП";
  return op;
}

IrOp call_to(std::string target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.opcode = 0x53;
  op.target = std::move(target);
  op.meta.mnemonic = "ПП";
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

IrOp ret() {
  IrOp op;
  op.kind = IrKind::Return;
  op.opcode = 0x52;
  op.meta.mnemonic = "В/О";
  return op;
}

} // namespace

void liveness_analysis_matches_typescript_contract() {
  {
    // The proof needs selection, not mathematical ordering (zero is special
    // on the real machine), and it must not treat a rounded zero difference
    // as proof that the original inputs were equal.
    const std::vector<std::string> values{
        "-999", "-3", "-1", "-0.125", "0", "0.125", "1", "2", "999",
        "1e90", "-1e90", "1e-90"};
    const auto number = [](std::string text) {
      std::replace(text.begin(), text.end(), ',', '.');
      std::istringstream input(text);
      std::string mantissa;
      std::string exponent;
      std::string extra;
      input >> mantissa;
      if (input >> exponent)
        mantissa += "e" + exponent;
      require(!(input >> extra), "unexpected calculator number format: " + text);
      std::size_t used = 0;
      const double result = std::stod(mantissa, &used);
      require(used == mantissa.size(), "partial calculator number parse: " + text);
      return result;
    };
    for (const std::string& candidate : values) {
      for (const std::string& previous : values) {
        emulator::MK61 calc;
        require(calc.load_program({0x61, 0x62, 0x36, 0x43, 0x11, 0x44, 0x50})
                    .diagnostics.empty(), "select fact must load on stock MK-61");
        calc.set_register("1", candidate);
        calc.set_register("2", previous);
        calc.set_register("Z", "317");
        calc.press_sequence({"В/О", "С/П"});
        require(calc.run_until_stable(2000, 5).stopped, "select fact must reach its stop");
        const double selected = number(calc.read_register("3"));
        const double difference = number(calc.read_register("4"));
        require(selected == std::stod(candidate) || selected == std::stod(previous),
                "Kmax selection: candidate=" + candidate + " previous=" + previous +
                    " selected=" + calc.read_register("3") + " difference=" + calc.read_register("4"));
        require(difference == 0 || selected == std::stod(previous),
                "a nonzero candidate-minus-selection must retain the previous input");
      }
    }
  }

  {
    const auto condition = [](int opcode, const std::string& target) {
      IrOp op;
      op.kind = IrKind::CondJump;
      op.opcode = opcode;
      op.condition = opcode == 0x5e ? "==0" : opcode == 0x57 ? "!=0" : "<0";
      op.target = target;
      return op;
    };
    const auto terminal = [] {
      IrOp op = halt();
      op.meta.stop_disposition = StopDisposition::Terminal;
      return op;
    };
    const auto fixture = [&](bool inverted) {
      std::vector<IrOp> program{
          plain(7, "7"), store("a"), call_to("save_first"),
          plain(1, "1"), plain(0x0b, "negate"), store("0"), label("scan"),
          recall("9"), recall("0"), plain(0x36, "max"), store("0"), plain(0x11, "-"),
          condition(inverted ? 0x57 : 0x5e, inverted ? "update" : "next")};
      if (inverted) {
        program.push_back(jump_to("next"));
        program.push_back(label("update"));
      }
      const std::vector<IrOp> tail{
          recall("8"), store("b"), label("next"),
          recall("2"), plain(1, "1"), plain(0x11, "-"), store("2"), condition(0x5e, "scan"),
          recall("0"), condition(0x5c, "consume"), plain(0, "0"), terminal(),
          label("consume"), recall("b"), terminal(),
          label("save_first"), recall("a"), store("6"), ret()};
      program.insert(program.end(), tail.begin(), tail.end());
      return program;
    };
    const core::passes::LivenessOptions options{
        .equal_entry_value_classes = {{"a", "literal:0"}, {"b", "literal:0"}}};
    const auto proved = [&](const std::vector<IrOp>& program) {
      const auto info = core::passes::compute_liveness(program, options);
      return info.guarded_disjoint_pairs.contains({"a", "b"}) &&
          !core::passes::build_register_interference_graph(program, info).interferes("a", "b");
    };
    const auto observe = [&](std::vector<IrOp> program, const std::string& candidate, bool alias) {
      const auto addresses = core::passes::calculate_label_addresses(program);
      for (IrOp& op : program) {
        if (op.kind == IrKind::Recall || op.kind == IrKind::Store) {
          if (alias && op.register_name == "b")
            op.register_name = "a";
          op.opcode = (op.kind == IrKind::Recall ? 0x60 : 0x40) + register_index(op.register_name);
        }
        if (op.kind != IrKind::Label && opcode_by_code(op.opcode).takes_address) {
          const auto* target = std::get_if<std::string>(&op.target);
          require(target != nullptr && addresses.contains(*target), "guard fixture target missing");
          op.target_meta.formal_opcode = official_address_to_opcode(addresses.at(*target));
        }
      }
      std::vector<int> codes;
      for (const auto& cell : lower_ir_to_layout(program).cells)
        codes.push_back(cell.opcode);
      require(codes.size() <= 105U, "guard fixture may not use virtual physical memory");
      emulator::MK61 calc;
      require(calc.load_program(codes).diagnostics.empty(), "guard fixture must load without Rf");
      for (const char* reg : {"a", "b", "5"})
        calc.set_register(reg, "0");
      calc.set_register("2", "2");
      calc.set_register("8", "17");
      calc.set_register("9", candidate);
      calc.set_register("Y", "73");
      calc.set_register("Z", "29");
      calc.set_register("T", "19");
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(4000, 5).stopped, "guard fixture lost its return or terminal stop");
      std::vector<std::string> result{calc.display_text()};
      for (const char* reg : {"X", "Y", "Z", "T", "X1", "0", "2", "5", "6"})
        result.push_back(calc.read_register(reg));
      return result;
    };
    for (const bool inverted : {false, true}) {
      const auto program = fixture(inverted);
      require(core::passes::build_register_interference_graph(program).interferes("a", "b"),
              "guard fixture must expose a conflict missed by ordinary CFG liveness");
      require(proved(program), "payload-write cut must expose the retained sentinel on either branch form");
      for (const char* value : {"-3", "-1", "0", "2", "9"})
        require(observe(program, value, false) == observe(program, value, true),
                "guarded register sharing changed stack/X1, output, or matched returns");
    }
    auto no_calls = fixture(false);
    no_calls.erase(no_calls.end() - 4, no_calls.end());
    no_calls.erase(no_calls.begin() + 2);
    no_calls.insert(no_calls.begin() + 2, {recall("a"), store("6")});
    require(proved(no_calls), "inlining the last helper must not disable guarded lifetime analysis");
    auto detached = fixture(false);
    const std::vector<IrOp> detached_tail{
        label("detached"), plain(7, "7"), store("a"),
        plain(2, "2"), store("0"), jump_to("consume")};
    detached.insert(detached.end(), detached_tail.begin(), detached_tail.end());
    require(!proved(detached), "a standalone fragment must retain its arbitrary-entry contract");
    auto closed_options = options;
    closed_options.closed_program_entry = true;
    require(core::passes::compute_liveness(detached, closed_options)
                .guarded_disjoint_pairs.contains({"a", "b"}),
            "an unreachable context cannot lengthen a complete program's payload lifetime");
    for (const char* value : {"-3", "0", "2"})
      require(observe(detached, value, false) == observe(detached, value, true),
              "closed-entry coalescing changed the physical program's observations");
    detached.insert(detached.begin() + 3, {recall("3"), condition(0x5e, "detached")});
    require(core::passes::compute_liveness(detached, closed_options)
                .guarded_disjoint_pairs.empty(),
            "a reachable alternate entry must still prevent complete-program coalescing");
    auto pooled = fixture(false);
    pooled.at(3) = recall("e");
    pooled.erase(pooled.begin() + 4);
    pooled.insert(pooled.begin(), label("pooled_entry"));
    pooled.push_back(jump_to("pooled_entry"));
    auto pooled_options = options;
    pooled_options.equal_entry_value_classes["e"] = "literal:-1";
    require(core::passes::compute_liveness(pooled, pooled_options)
                .guarded_disjoint_pairs.contains({"a", "b"}),
            "an immutable setup constant must survive a conservative disconnected entry");
    pooled.insert(pooled.begin() + 4, {recall("3"), store("e")});
    require(core::passes::compute_liveness(pooled, pooled_options).guarded_disjoint_pairs.empty(),
            "a written setup register must not become an arbitrary-entry invariant");
    auto read_old = fixture(false);
    read_old.insert(read_old.begin() + 6, {recall("b"), store("5")});
    require(!proved(read_old) && observe(read_old, "-3", false) != observe(read_old, "-3", true),
            "a real read before the guarded definition must prevent coalescing");
    auto overwrite_guard = fixture(false);
    const auto consume = std::find_if(overwrite_guard.begin(), overwrite_guard.end(), [](const IrOp& op) {
      return op.kind == IrKind::CondJump && op.opcode == 0x5c;
    });
    overwrite_guard.insert(consume - 1, {plain(2, "2"), store("0")});
    require(!proved(overwrite_guard) &&
                observe(overwrite_guard, "-3", false) != observe(overwrite_guard, "-3", true),
            "an independent guard overwrite must invalidate the sentinel implication");
    auto manual = fixture(false);
    IrOp input = halt();
    input.meta.stop_disposition = StopDisposition::Resumable;
    manual.insert(manual.begin() + 5, input);
    require(!proved(manual), "manual input must not inherit the pre-stop literal X fact");
    auto manual_select = fixture(false);
    manual_select.at(10).meta.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 81, .phase = 0, .kind = ManualInteractionAnchorKind::PromptStop};
    require(!proved(manual_select), "a manual interaction cannot be summarized as select retention");
    auto extra_entry = fixture(false);
    extra_entry.insert(extra_entry.begin() + 10, label("unproved_store"));
    extra_entry.insert(extra_entry.begin() + 3, {recall("3"), condition(0x5e, "unproved_store")});
    require(!proved(extra_entry), "an extra entry into the select window must reject its summary");
    auto unknown = fixture(false);
    unknown.insert(unknown.begin() + 3, indirect_recall("d", std::nullopt));
    require(!proved(unknown), "unknown indirect memory cannot authorize lifetime splitting");
    auto opaque = fixture(false);
    opaque.front().meta.raw = true;
    require(!proved(opaque), "raw effects must preserve conservative lifetime analysis");
    auto different = options;
    different.equal_entry_value_classes["b"] = "literal:1";
    require(core::passes::compute_liveness(fixture(false), different).guarded_disjoint_pairs.empty(),
            "distinct setup values must not share a register through this proof");
    auto renamed = fixture(false);
    for (IrOp& op : renamed)
      if (op.register_name == "a") op.register_name = "c";
      else if (op.register_name == "b") op.register_name = "e";
    const auto renamed_info = core::passes::compute_liveness(renamed, {
        .equal_entry_value_classes = {{"c", "literal:0"}, {"e", "literal:0"}}});
    require(renamed_info.guarded_disjoint_pairs.contains({"c", "e"}),
            "guarded liveness must be independent of source/register names");
    auto oversized = fixture(false);
    oversized.insert(oversized.begin(), 110, plain(0x54, "nop"));
    require(proved(oversized), "logical addresses beyond 105 must not hide a conditional lifetime");
  }

  {
    const std::string source = R"mkpro(program ConditionalPayload {
      state {
        first: packed = 0
        saved: packed = 0
        value: packed
        guard: packed
        remaining: counter 0..2 = 2
      }
      loop {
        show(0)
        value = entered()
        first = value + 7
        remember()
        guard = -1
        while remaining >= 1 {
          guard = max(value, guard)
          if value == guard {
            saved = value * 2
          }
          remaining--
        }
        if guard < 0 { halt(0) }
        halt(saved)
      }
      fn remember() { show(first) }
    })mkpro";
    const auto compile_and_run = [&](const std::string& text, bool should_share) {
      CompileOptions probe_options;
      probe_options.disable_candidate_search = true;
      probe_options.collect_logical_register_allocation = true;
      const auto probe = compile_source(text, probe_options);
      require(probe.implemented && !probe.logical_register_assignments.empty(),
              "guarded compiler fixture must obtain a logical register assignment");
      const auto home = [&](const std::string& name) {
        const auto found = std::find_if(probe.logical_register_assignments.begin(),
            probe.logical_register_assignments.end(), [&](const auto& assignment) {
              return assignment.name == name;
            });
        require(found != probe.logical_register_assignments.end(), "guarded fixture lost source state");
        return found->register_name;
      };
      if ((home("first") == home("saved")) != should_share) {
        CompileOptions diagnostic_options;
        diagnostic_options.disable_candidate_search = true;
        const auto diagnostic = compile_source(text, diagnostic_options);
        require(false, "logical allocation ignored a guarded lifetime or merged a real overlap: " +
                  home("first") + " / " + home("saved") + "\n" + diagnostic.listing);
      }
      CompileOptions options;
      options.disable_candidate_search = true;
      options.forced_logical_register_assignments = probe.logical_register_assignments;
      const auto result = compile_source(text, options);
      require(result.implemented && result.diagnostics.empty() && result.steps.size() <= 105U,
              "guarded allocation must re-lower and validate on an ordinary MK-61");
      std::vector<int> codes;
      for (const auto& step : result.steps)
        codes.push_back(step.opcode);
      for (const int value : {-3, 0, 2}) {
        emulator::MK61 calc;
        require(calc.load_program(codes).diagnostics.empty(), "guarded compiler fixture must load");
        for (const auto& preload : result.preloads) {
          require(preload.register_name != "f", "guarded allocation must not use Rf");
          calc.set_register(preload.register_name, preload.value);
        }
        calc.press_sequence({"В/О", "С/П"});
        require(calc.run_until_stable(3000, 5).stopped && std::stod(calc.display_text()) == 0,
                "guarded source input prompt changed");
        calc.press(std::to_string(value < 0 ? -value : value));
        if (value < 0)
          calc.press("/-/");
        calc.press("С/П");
        require(calc.run_until_stable(3000, 5).stopped && std::stod(calc.display_text()) == value + 7,
                "caller-visible value changed before the selection loop");
        calc.press("С/П");
        require(calc.run_until_stable(4000, 5).stopped &&
                    std::stod(calc.display_text()) == (value < 0 ? 0 : value * 2),
                "guarded payload or its initial value changed after matched return");
      }
    };
    compile_and_run(source, true);
    std::string unguarded = source;
    unguarded.insert(unguarded.find("if guard < 0"), "guard = 2\n        ");
    compile_and_run(unguarded, false);
  }

  {
    IrOp terminal = halt();
    terminal.meta.stop_disposition = StopDisposition::Terminal;
    IrOp returning;
    returning.kind = IrKind::CondJump;
    returning.opcode = 0x5e;
    returning.condition = "==0";
    returning.target = std::string("returning");
    // The leaf's terminal arm is physically followed by its caller. Treating
    // the terminal stop as resumable invents recursive calls and defeats the
    // bounded return-context proof, although the source has no recursion.
    const std::vector<IrOp> program = {
        store("1"), call_to("outer"), recall("1"), store("2"), call_to("outer"),
        recall("2"), jump_to("done"), label("leaf"), returning, terminal,
        label("outer"), call_to("leaf"), ret(), label("returning"), store("3"),
        ret(), label("done"), terminal,
    };
    const auto info = core::passes::compute_liveness(program);
    const auto graph = core::passes::build_register_interference_graph(program, info);
    require(info.matched_call_contexts && info.control_flow_targets_are_exact,
            "typed termination must not create fictitious recursive return contexts");
    require(info.live_out.at(9).empty() && info.live_out.at(17).empty(),
            "a source terminal stop acquired live-out values from physical adjacency");
    require(!graph.interferes("1", "2") && graph.interferes("1", "3") &&
                graph.interferes("2", "3"),
            "terminal-arm analysis lost disjoint caller phases or real callee clobbers");

    for (const StopDisposition disposition : {StopDisposition::Unknown,
                                               StopDisposition::Resumable}) {
      auto resumable = program;
      resumable.at(9).meta.stop_disposition = disposition;
      const auto conservative = core::passes::compute_liveness(resumable);
      require(!conservative.matched_call_contexts && conservative.live_out.at(9).contains("1"),
              "a resumable stop must retain the recursive continuation and conservative fallback");
    }
    auto manual = program;
    manual.at(9).meta.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 4, .phase = 0, .kind = ManualInteractionAnchorKind::PromptStop};
    require(!core::passes::compute_liveness(manual).matched_call_contexts,
            "manual stop/resume was discarded by a terminal annotation");
    auto raw = program;
    raw.at(9).meta.raw = true;
    const auto raw_info = core::passes::compute_liveness(raw);
    require(!raw_info.matched_call_contexts && raw_info.live_out.at(9).contains("1"),
            "raw terminal metadata weakened the conservative continuation proof");

    const auto no_calls = core::passes::compute_liveness({store("1"), terminal, recall("1")});
    require(no_calls.live_out.at(1).empty() && no_calls.live_in.at(2).contains("1"),
            "ordinary fixed-point liveness must cut source termination without erasing unentered code");
  }

  {
    const std::string source = R"mkpro(program TerminatingCallee {
      state {
        phase: counter 0..9 = 0
        first: counter 0..99 = 0
        second: counter 0..99 = 0
      }
      loop {
        first = phase + 3
        tick()
        show(first)
        second = phase + 5
        tick()
        show(second)
      }
      fn tick() {
        phase += 1
        if phase == 3 {
          halt(phase)
        }
        show(phase)
      }
    })mkpro";
    CompileOptions options;
    options.disable_candidate_search = true;
    const CompileResult result = compile_source(source, options);
    require(result.implemented && result.diagnostics.empty(),
            "a callee with terminal and resumable arms must compile");
    std::vector<int> codes;
    for (const ResolvedStep& step : result.steps)
      codes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(),
            "terminal-callee compiler fixture must load without truncation");
    for (const PreloadReport& preload : result.preloads)
      calc.set_register(preload.register_name, preload.value);
    calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
    const std::vector<int> expected = {1, 3, 2, 6, 3};
    for (std::size_t index = 0; index < expected.size(); ++index) {
      require(calc.run_until_stable(2000, 5).stopped,
              "a callee stop or its matched return continuation was lost");
      require(std::stod(calc.display_text()) == expected.at(index),
              "callee termination changed a value retained across an ordinary stop/resume");
      if (index + 1U < expected.size())
        calc.press("\u0421/\u041f");
    }
  }

  {
    // The same helper runs in two disjoint caller phases. A union at its
    // return instruction is useful for DSE but is not an interference clique.
    const std::vector<IrOp> program = {
        store("1"), call_to("outer"), recall("1"), store("2"), call_to("outer"),
        recall("2"), jump_to("done"), label("outer"), call_to("leaf"), ret(),
        label("leaf"), store("3"), ret(), label("done"), halt(),
    };
    const auto info = core::passes::compute_liveness(program);
    const auto graph = core::passes::build_register_interference_graph(program, info);
    require(info.matched_call_contexts, "nested direct calls lost their matched return contexts");
    require(info.live_in.at(12).contains("1") && info.live_in.at(12).contains("2"),
            "per-instruction liveness must retain the union of both caller phases");
    require(!graph.interferes("1", "2"),
            "unrelated caller phases acquired a fictitious interference edge");
    require(graph.interferes("1", "3") && graph.interferes("2", "3"),
            "callee definitions must conflict with every live caller value");
    require(!info.live_out.at(1).contains("2") && !info.live_out.at(4).contains("1"),
            "return liveness escaped into another invocation's continuation");

    auto overlapping = program;
    overlapping.insert(overlapping.begin() + 6, recall("1"));
    require(core::passes::build_register_interference_graph(overlapping).interferes("1", "2"),
            "a real cross-call lifetime was incorrectly split into separate phases");

    auto indirect = program;
    for (const std::size_t index : {1U, 4U}) {
      indirect.at(index).kind = IrKind::IndirectCall;
      indirect.at(index).register_name = "e";
      indirect.at(index).opcode = 0xae;
      indirect.at(index).meta.indirect_flow_targets =
          std::vector<IrTarget>{std::string("outer"), std::string("leaf")};
    }
    const auto indirect_info = core::passes::compute_liveness(indirect);
    require(indirect_info.matched_call_contexts &&
                !core::passes::build_register_interference_graph(indirect, indirect_info)
                     .interferes("1", "2"),
            "proved multi-target callbacks did not retain matched return suffixes");
    indirect.at(1).meta.indirect_flow_targets.reset();
    const auto unknown = core::passes::compute_liveness(indirect);
    require(!unknown.matched_call_contexts && !unknown.control_flow_targets_are_exact &&
                unknown.live_in.at(1).contains("e"),
            "an unknown callback weakened the conservative fallback");

    auto recursive = program;
    recursive.at(8).target = std::string("outer");
    require(!core::passes::compute_liveness(recursive).matched_call_contexts,
            "recursive return depth must use the conservative fixed point");
    auto raw = program;
    raw.at(11).meta.raw = true;
    require(!core::passes::compute_liveness(raw).matched_call_contexts,
            "raw machine-state changes established a matched-call proof");

    auto stopped = program;
    IrOp interaction = halt();
    interaction.meta.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 3, .phase = 0, .kind = ManualInteractionAnchorKind::PromptStop};
    stopped.insert(stopped.begin() + 12, interaction);
    const auto stopped_info = core::passes::compute_liveness(stopped);
    require(stopped_info.matched_call_contexts && stopped_info.live_in.at(12).contains("1") &&
                stopped_info.live_in.at(12).contains("2"),
            "stop/resume discarded a live caller or its return context");

    auto unreachable = program;
    unreachable.push_back(jump_to("done"));
    unreachable.push_back(label("unentered"));
    unreachable.push_back(recall("a"));
    unreachable.push_back(ret());
    const auto unreachable_info = core::passes::compute_liveness(unreachable);
    require(unreachable_info.live_in.at(unreachable.size() - 2U).contains("a"),
            "physically present unreachable code disappeared from the allocation proof");
  }

  {
    const std::vector<IrOp> program = {label("loop"), recall("3"), plain(0x10, "+"),
                                       jump_to("loop")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_in.at(1).contains("3"),
            "liveness did not propagate use backwards through loop body");
    require(info.live_out.at(3).contains("3"),
            "liveness did not propagate use through loop back edge");
  }

  {
    const std::vector<IrOp> program = {call_to("terminal"), recall("3"), label("terminal"), halt()};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(!info.live_out.at(0).contains("3"),
            "liveness propagated through non-returning direct call continuation");
  }

  {
    const std::vector<IrOp> program = {call_to("returns"), recall("3"), halt(), label("returns"),
                                       ret()};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_out.at(0).contains("3"),
            "liveness did not propagate direct call continuation through return");
  }

  {
    const std::vector<IrOp> program = {
        call_to("outer"), recall("9"), halt(),         label("outer"),
        call_to("inner"), ret(),       label("inner"), ret()};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_out.at(0).contains("9") && info.live_out.at(4).contains("9"),
            "liveness lost a caller continuation through nested calls and returns");
  }

  {
    const std::vector<IrOp> program = {store("1"), halt(), recall("1")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_out.at(1).contains("1"),
            "liveness failed to preserve a register across manual stop/resume");
  }

  {
    IrOp interaction = halt();
    interaction.meta.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 1,
        .phase = 0,
        .kind = ManualInteractionAnchorKind::PromptStop,
    };
    const std::vector<IrOp> program = {store("1"), interaction, recall("1")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_in.at(1).contains("1"),
            "manual interaction lost a register needed after resume");
    require(!info.live_in.at(1).contains("e"),
            "typed manual interaction incorrectly became an all-register memory barrier");
  }

  {
    const std::vector<IrOp> program = {store("4"), indirect_store("0", std::vector<int>{4, 5}),
                                       recall("4")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_in.at(1).contains("4"),
            "a multi-target indirect store incorrectly killed every possible target");
    require(core::passes::register_effects(program.at(1)).may_defs.contains("4"),
            "multi-target indirect store was not represented as a may-def");
  }

  {
    const std::vector<IrOp> program = {store("4"), indirect_store("0", std::vector<int>{4}),
                                       recall("4")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(!info.live_in.at(1).contains("4"),
            "a singleton indirect store failed to kill its proven target");
    require(core::passes::register_effects(program.at(1)).must_defs.contains("4"),
            "typed singleton indirect-memory metadata was not represented as a must-def");
    require(core::passes::known_indirect_memory_target(program.at(1)) == "4",
            "typed singleton indirect-memory metadata was ignored by the singleton helper");
  }

  {
    const core::passes::RegisterEffects mutating_memory =
        core::passes::register_effects(indirect_recall("4", std::vector<int>{8}));
    const core::passes::RegisterEffects mutating_flow =
        core::passes::register_effects(indirect_jump("6"));
    const core::passes::RegisterEffects stable_memory =
        core::passes::register_effects(indirect_recall("7", std::vector<int>{8}));

    require(mutating_memory.uses.contains("4") && mutating_memory.must_defs.contains("4"),
            "pre-increment indirect-memory selector was not modeled as use plus definition");
    require(mutating_flow.uses.contains("6") && mutating_flow.must_defs.contains("6"),
            "mutating indirect-flow selector was not modeled as use plus definition");
    require(stable_memory.uses.contains("7") && !stable_memory.must_defs.contains("7"),
            "stable R7 indirect selector was incorrectly modeled as a definition");
  }

  {
    IrOp logical_mutating = indirect_recall("selector", std::vector<int>{8});
    logical_mutating.opcode = 0xd4;
    logical_mutating.meta.logical_register_analysis = true;
    IrOp logical_stable = logical_mutating;
    logical_stable.opcode = 0xd7;

    require(core::passes::register_effects(logical_mutating).must_defs.contains("selector"),
            "logical liveness ignored the mutating class encoded by an indirect opcode");
    require(!core::passes::register_effects(logical_stable).must_defs.contains("selector"),
            "logical liveness treated a stable indirect opcode as selector mutation");
  }

  {
    const std::vector<IrOp> program = {indirect_recall("0", std::nullopt)};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_in.at(0).contains("e"),
            "unknown indirect recall did not conservatively use every machine register");
  }

  {
    IrOp discarded = indirect_recall("0", std::nullopt);
    discarded.meta.discarded_indirect_recall_value = true;
    const core::passes::RegisterEffects effects = core::passes::register_effects(discarded);

    require(effects.uses.contains("0") && effects.must_defs.contains("0"),
            "discarded indirect recall lost its mutating selector effect");
    require(!effects.uses_all_registers && !effects.uses.contains("e"),
            "discarded indirect recall incorrectly kept its unobserved memory value live");
  }

  {
    const std::vector<IrOp> program = {store("e"), indirect_store("0", std::nullopt), recall("e")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_in.at(1).contains("e"),
            "unknown indirect store incorrectly killed a possible old target value");
    require(core::passes::register_effects(program.at(1)).may_define_any_register,
            "unknown indirect store was not represented as an all-register may-def");
  }

  {
    const std::vector<IrOp> program = {indirect_jump("7"), recall("2")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(!info.control_flow_targets_are_exact,
            "unknown indirect jump was reported as exact control flow");
    require(info.conservative_flow_sources == std::vector<int>{0},
            "unknown indirect jump did not identify its conservative source");
    require(info.live_in.at(0).size() >= 15U && info.live_in.at(0).contains("e"),
            "unknown indirect jump did not form a full-register liveness barrier");
  }

  {
    const std::vector<IrOp> program = {jump_to("missing"), recall("2")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(!info.control_flow_targets_are_exact,
            "unresolved direct target was reported as exact control flow");
    require(info.live_in.at(0).contains("e"),
            "unresolved direct target did not form a full-register liveness barrier");
  }

  {
    std::vector<IrOp> program{recall("e")};
    for (int index = 1; index < 260; ++index) {
      const int previous_address = index == 1 ? 0 : 1 + (2 * (index - 2));
      program.push_back(jump_to_address(previous_address));
    }
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.control_flow_targets_are_exact,
            "resolved long reverse graph was unexpectedly marked conservative");
    require(info.live_in.back().contains("e"),
            "liveness stopped before the fixed point on a graph needing over 200 propagations");
  }
}

} // namespace mkpro::tests
