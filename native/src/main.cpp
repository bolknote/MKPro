#include "mkpro/compiler.hpp"
#include "mkpro/core/compiler_behavior_digest.hpp"
#include "mkpro/core/format.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/result.hpp"
#include "mkpro/version.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_usage(std::ostream& out) {
  out << R"(Usage:
  mkpro-native compile <file.mkpro> [options]
  mkpro-native explain <file.mkpro> [options]
  mkpro-native --help
  mkpro-native --version

Commands:
  compile                         Compile an MKPRO source file.
  explain                         Compile with --analysis and --out all, including
                                  a human-readable optimizer report.

Global options:
  -h, --help                      Show this help.
  --version                       Show native compiler version.

Output and execution model:
  --out listing|hex|mk61s|dot|json|keys|all
                                  Select output format. listing is the default.
                                  keys prints a space-separated manual key stream
                                  derived from setup and main resolved opcodes.
  --delivery manual|loader|hex    Select opcode delivery assumptions for raw or
                                  not-normally-enterable commands. manual is the
                                  default.
  --budget N                      Set the maximum accepted compiled program size.
  --analysis                      Include optimizer analysis/report data. With
                                  --out all, also print a human-readable report.
  --strict                        Treat undeclared allocation as an error.
  --behavior-digest               Print a stable behavior digest instead of the
                                  selected output format.

Candidate search:
  --disable-candidate-search      Compile only the requested option set.
  --fast                          Enable fast candidate search.
  --fast-threshold-ms N           Enable fast search and set per-candidate limit.
  --collect-coalesce-shares       Collect register coalesce-share suggestions and
                                  enable --analysis.

IR and lowering optimizer switches:
  --x-param-value-functions       Recognize X-parameter value functions.
  --x-param-y-stack-stored-entry  Use Y-stack stored entry for X-parameter paths.
  --canonicalize-repeated-unary-update-args
                                  Canonicalize repeated unary update arguments.
  --coalesce-copies               Coalesce copy-only register moves.
  --shared-straight-line-call-bodies
                                  Share duplicate straight-line call bodies.
  --segmented-bitplanes           Use segmented bitplane lowering.
  --segmented-line-count-scan     Use segmented bitplanes with line-count scan.
  --compact-bit-mask-helper-body  Compact generated bit-mask helper bodies.
  --shared-bit-mask-helper-calls  Share calls to generated bit-mask helpers.
  --general-constant-preloads     Allow general setup-time constant preloads.
  --dual-use-constant-indirect-flow
                                  Reuse constant preloads for indirect flow.
  --indirect-underflow-decrement  Use indirect decrement flow for underflow paths.
  --tail-branch-inversion         Invert tail branches when that shortens flow.
  --invert-branch-order           Invert branch order in eligible conditionals.
  --conditional-branch-trampoline Insert trampolines for eligible conditionals.
  --preloaded-indirect-flow       Enable preloaded indirect-flow rewrite.
  --forward-indirect-flow         Allow indirect-flow rewrites to forward targets.
  --runtime-indirect-call-flow    Enable runtime indirect-call flow rewrite.
  --aggressive-post-layout-indirect-flow
                                  Enable aggressive post-layout indirect-flow pass.
  --return-stack-script           Enable return-stack script post-layout rewrite.
  --no-return-stack-script        Disable return-stack script rewrite.
  --dead-source-residual-temp-reuse
                                  Reuse residual temporaries with dead sources.
  --free-residual-dispatch-scratch
                                  Free scratch register in residual dispatch.
  --domain-error-guards           Emit domain-error guards for eligible formulas.
  --unroll-counted-loops          Unroll eligible counted loops.
  --setup-only-counted-loop-init  Move counted-loop initialization into setup.
  --show-read-guarded-transfer    Guard read-driven transfers for display safety.
  --stack-resident-temps          Keep eligible temporaries on the stack.
  --inline-floor-packed-row-expressions
                                  Inline floor-packed row expressions.
  --order-procs-by-call-count     Layout procedures by call frequency.
  --proc-layout-strategy reverse|size-asc|size-desc
                                  Select explicit procedure layout strategy.
  --aggressive-indirect-call      Use aggressive indirect-call lowering.
  --share-random-cell             Share the random-cell state slot when safe.
  --hoist-shared-helpers          Hoist shared helper bodies.
  --hoist-procs                   Hoist procedure bodies.

Manual optimizer overrides:
  --suppress-constant-preload VALUE
                                  Do not preload the normalized constant VALUE.
  --fractional-selector VALUE:TARGET
                                  Force a fractional selector plan for TARGET.
  --force-fractional-selector-preload VALUE
                                  Force setup preload for fractional VALUE.
  --force-register-share FREE:KEEP
                                  Force a register coalesce share from FREE to KEEP.
)";
}

std::optional<mkpro::OutputFormat> parse_output_format(const std::string& value) {
  if (value == "listing")
    return mkpro::OutputFormat::Listing;
  if (value == "hex")
    return mkpro::OutputFormat::Hex;
  if (value == "mk61s")
    return mkpro::OutputFormat::Mk61s;
  if (value == "dot")
    return mkpro::OutputFormat::Dot;
  if (value == "json")
    return mkpro::OutputFormat::Json;
  if (value == "keys")
    return mkpro::OutputFormat::Keys;
  if (value == "all")
    return mkpro::OutputFormat::All;
  return std::nullopt;
}

std::optional<mkpro::DeliveryMode> parse_delivery_mode(const std::string& value) {
  if (value == "manual")
    return mkpro::DeliveryMode::Manual;
  if (value == "loader")
    return mkpro::DeliveryMode::Loader;
  if (value == "hex")
    return mkpro::DeliveryMode::Hex;
  return std::nullopt;
}

bool is_proc_layout_strategy(std::string_view value) {
  return value == "reverse" || value == "size-asc" || value == "size-desc";
}

std::string read_source(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open source file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void print_diagnostics(const mkpro::CompileResult& result) {
  for (const auto& diagnostic : result.diagnostics) {
    std::cerr << mkpro::diagnostic_severity_name(diagnostic.severity) << "[" << diagnostic.code
              << "]: " << diagnostic.message << "\n";
  }
}

void print_json_string(std::ostream& out, const std::string& value) {
  out << '"';
  for (char ch : value) {
    if (ch == '\\') {
      out << "\\\\";
    } else if (ch == '"') {
      out << "\\\"";
    } else if (ch == '\n') {
      out << "\\n";
    } else {
      out << ch;
    }
  }
  out << '"';
}

void print_json_steps(const std::vector<mkpro::ResolvedStep>& steps, std::string indent) {
  std::cout << "[\n";
  for (std::size_t index = 0; index < steps.size(); ++index) {
    const auto& step = steps[index];
    std::cout << indent << "  {\"address\": " << step.address << ", \"opcode\": " << step.opcode
              << ", \"hex\": ";
    print_json_string(std::cout, step.hex);
    std::cout << ", \"mnemonic\": ";
    print_json_string(std::cout, step.mnemonic);
    if (step.comment.has_value()) {
      std::cout << ", \"comment\": ";
      print_json_string(std::cout, *step.comment);
    }
    std::cout << "}" << (index + 1U == steps.size() ? "" : ",") << "\n";
  }
  std::cout << indent.substr(0, indent.size() - 2U) << "]";
}

void print_json_string_map(const std::map<std::string, std::string>& values, std::string indent) {
  std::cout << "{\n";
  std::size_t index = 0;
  for (const auto& [key, value] : values) {
    std::cout << indent << "  ";
    print_json_string(std::cout, key);
    std::cout << ": ";
    print_json_string(std::cout, value);
    std::cout << (++index == values.size() ? "" : ",") << "\n";
  }
  std::cout << indent.substr(0, indent.size() - 2U) << "}";
}

void print_json_string_array(const std::vector<std::string>& values, std::string indent) {
  std::cout << "[";
  if (!values.empty())
    std::cout << "\n";
  for (std::size_t index = 0; index < values.size(); ++index) {
    std::cout << indent << "  ";
    print_json_string(std::cout, values[index]);
    std::cout << (index + 1U == values.size() ? "" : ",") << "\n";
  }
  if (!values.empty())
    std::cout << indent.substr(0, indent.size() - 2U);
  std::cout << "]";
}

void print_json_reference(const mkpro::ReferenceReport& reference, std::string indent) {
  std::cout << "{\n";
  std::cout << indent << "  \"name\": ";
  print_json_string(std::cout, reference.name);
  std::cout << ",\n" << indent << "  \"referenceSteps\": " << reference.reference_steps;
  std::cout << ",\n" << indent << "  \"referenceSpan\": " << reference.reference_span;
  std::cout << ",\n" << indent << "  \"referenceEntries\": " << reference.reference_entries;
  std::cout << ",\n" << indent << "  \"referenceGaps\": ";
  print_json_string_array(reference.reference_gaps, indent + "  ");
  std::cout << ",\n" << indent << "  \"compiledSteps\": " << reference.compiled_steps;
  std::cout << ",\n" << indent << "  \"delta\": " << reference.delta;
  std::cout << ",\n" << indent << "  \"parity\": ";
  print_json_string(std::cout, reference.parity);
  std::cout << "\n" << indent.substr(0, indent.size() - 2U) << "}";
}

void print_json_optimizer(const mkpro::OptimizerReport& optimizer, std::string indent) {
  std::cout << "{\n";
  std::cout << indent << "  \"automatic\": " << (optimizer.automatic ? "true" : "false");
  std::cout << ",\n" << indent << "  \"active\": " << optimizer.active;
  std::cout << ",\n" << indent << "  \"considered\": " << optimizer.considered;
  std::cout << ",\n" << indent << "  \"candidate\": " << optimizer.candidate;
  std::cout << ",\n" << indent << "  \"planned\": " << optimizer.planned;
  std::cout << ",\n" << indent << "  \"capabilities\": [\n";
  for (std::size_t index = 0; index < optimizer.capabilities.size(); ++index) {
    const auto& capability = optimizer.capabilities[index];
    std::cout << indent << "    {\"id\": ";
    print_json_string(std::cout, capability.id);
    std::cout << ", \"category\": ";
    print_json_string(std::cout, capability.category);
    std::cout << ", \"source\": ";
    print_json_string(std::cout, capability.source);
    std::cout << ", \"status\": ";
    print_json_string(std::cout, capability.status);
    std::cout << ", \"detail\": ";
    print_json_string(std::cout, capability.detail);
    std::cout << ", \"requires\": ";
    print_json_string_array(capability.required_features, indent + "    ");
    std::cout << "}" << (index + 1U == optimizer.capabilities.size() ? "" : ",") << "\n";
  }
  std::cout << indent << "  ]\n";
  std::cout << indent.substr(0, indent.size() - 2U) << "}";
}

void print_json_candidate_reports(const std::vector<mkpro::CandidateReport>& candidates,
                                  std::string indent) {
  std::cout << "[\n";
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const auto& candidate = candidates[index];
    std::cout << indent << "  {\"site\": ";
    print_json_string(std::cout, candidate.site);
    std::cout << ", \"variant\": ";
    print_json_string(std::cout, candidate.variant);
    std::cout << ", \"steps\": " << candidate.steps;
    std::cout << ", \"selected\": " << (candidate.selected ? "true" : "false");
    std::cout << ", \"reason\": ";
    print_json_string(std::cout, candidate.reason);
    std::cout << "}" << (index + 1U == candidates.size() ? "" : ",") << "\n";
  }
  std::cout << indent.substr(0, indent.size() - 2U) << "]";
}

void print_json_string_map(const std::map<std::string, std::string>& values) {
  std::cout << "{";
  std::size_t index = 0;
  for (const auto& [key, value] : values) {
    if (index++ > 0)
      std::cout << ", ";
    print_json_string(std::cout, key);
    std::cout << ": ";
    print_json_string(std::cout, value);
  }
  std::cout << "}";
}

void print_json_size_attribution(const mkpro::SizeAttributionReport& report, std::string indent) {
  std::cout << "{\n";
  std::cout << indent << "  \"totalCells\": " << report.total_cells;
  std::cout << ",\n" << indent << "  \"entries\": [\n";
  for (std::size_t index = 0; index < report.entries.size(); ++index) {
    const auto& entry = report.entries.at(index);
    std::cout << indent << "    {\"kind\": ";
    print_json_string(std::cout, entry.kind);
    std::cout << ", \"label\": ";
    print_json_string(std::cout, entry.label);
    std::cout << ", \"cells\": " << entry.cells;
    std::cout << ", \"occurrences\": " << entry.occurrences;
    std::cout << ", \"firstAddress\": " << entry.first_address;
    std::cout << ", \"lastAddress\": " << entry.last_address;
    if (!entry.detail.empty()) {
      std::cout << ", \"detail\": ";
      print_json_string(std::cout, entry.detail);
    }
    std::cout << "}" << (index + 1U == report.entries.size() ? "" : ",") << "\n";
  }
  std::cout << indent << "  ],\n";
  std::cout << indent << "  \"helpers\": [\n";
  for (std::size_t index = 0; index < report.helpers.size(); ++index) {
    const auto& helper = report.helpers.at(index);
    std::cout << indent << "    {\"label\": ";
    print_json_string(std::cout, helper.label);
    std::cout << ", \"totalCells\": " << helper.total_cells;
    std::cout << ", \"bodyCells\": " << helper.body_cells;
    std::cout << ", \"callSiteCells\": " << helper.call_site_cells;
    std::cout << ", \"bodyOccurrences\": " << helper.body_occurrences;
    std::cout << ", \"callOccurrences\": " << helper.call_occurrences;
    std::cout << ", \"firstAddress\": " << helper.first_address;
    std::cout << ", \"lastAddress\": " << helper.last_address;
    if (!helper.details.empty()) {
      std::cout << ", \"details\": ";
      print_json_string_map(helper.details);
    }
    std::cout << "}" << (index + 1U == report.helpers.size() ? "" : ",") << "\n";
  }
  std::cout << indent << "  ],\n";
  std::cout << indent << "  \"helperSpills\": [\n";
  for (std::size_t index = 0; index < report.helper_spills.size(); ++index) {
    const auto& spill = report.helper_spills.at(index);
    std::cout << indent << "    {\"helperLabel\": ";
    print_json_string(std::cout, spill.helper_label);
    std::cout << ", \"name\": ";
    print_json_string(std::cout, spill.name);
    std::cout << ", \"totalCells\": " << spill.total_cells;
    std::cout << ", \"recallCells\": " << spill.recall_cells;
    std::cout << ", \"storeCells\": " << spill.store_cells;
    std::cout << ", \"recallOccurrences\": " << spill.recall_occurrences;
    std::cout << ", \"storeOccurrences\": " << spill.store_occurrences;
    std::cout << ", \"firstAddress\": " << spill.first_address;
    std::cout << ", \"lastAddress\": " << spill.last_address;
    std::cout << "}" << (index + 1U == report.helper_spills.size() ? "" : ",") << "\n";
  }
  std::cout << indent << "  ],\n";
  std::cout << indent << "  \"abiBlockers\": [\n";
  for (std::size_t index = 0; index < report.abi_blockers.size(); ++index) {
    const auto& blocker = report.abi_blockers.at(index);
    std::cout << indent << "    {\"kind\": ";
    print_json_string(std::cout, blocker.kind);
    std::cout << ", \"label\": ";
    print_json_string(std::cout, blocker.label);
    std::cout << ", \"line\": " << blocker.line;
    std::cout << ", \"materializeCells\": " << blocker.materialize_cells;
    if (!blocker.details.empty()) {
      std::cout << ", \"details\": ";
      print_json_string_map(blocker.details);
    }
    std::cout << ", \"reason\": ";
    print_json_string(std::cout, blocker.reason);
    std::cout << "}" << (index + 1U == report.abi_blockers.size() ? "" : ",") << "\n";
  }
  std::cout << indent << "  ],\n";
  std::cout << indent << "  \"opportunities\": [\n";
  for (std::size_t index = 0; index < report.opportunities.size(); ++index) {
    const auto& opportunity = report.opportunities.at(index);
    std::cout << indent << "    {\"site\": ";
    print_json_string(std::cout, opportunity.site);
    std::cout << ", \"variant\": ";
    print_json_string(std::cout, opportunity.variant);
    std::cout << ", \"currentSteps\": " << opportunity.current_steps;
    std::cout << ", \"candidateSteps\": " << opportunity.candidate_steps;
    std::cout << ", \"savings\": " << opportunity.savings;
    if (!opportunity.blocker_kind.empty()) {
      std::cout << ", \"blockerKind\": ";
      print_json_string(std::cout, opportunity.blocker_kind);
    }
    if (!opportunity.details.empty()) {
      std::cout << ", \"details\": ";
      print_json_string_map(opportunity.details);
    }
    std::cout << ", \"reason\": ";
    print_json_string(std::cout, opportunity.reason);
    std::cout << "}" << (index + 1U == report.opportunities.size() ? "" : ",") << "\n";
  }
  std::cout << indent << "  ],\n";
  std::cout << indent << "  \"spills\": [\n";
  for (std::size_t index = 0; index < report.spills.size(); ++index) {
    const auto& spill = report.spills.at(index);
    std::cout << indent << "    {\"name\": ";
    print_json_string(std::cout, spill.name);
    std::cout << ", \"totalCells\": " << spill.total_cells;
    std::cout << ", \"recallCells\": " << spill.recall_cells;
    std::cout << ", \"storeCells\": " << spill.store_cells;
    std::cout << ", \"recallOccurrences\": " << spill.recall_occurrences;
    std::cout << ", \"storeOccurrences\": " << spill.store_occurrences;
    std::cout << ", \"firstAddress\": " << spill.first_address;
    std::cout << ", \"lastAddress\": " << spill.last_address;
    std::cout << "}" << (index + 1U == report.spills.size() ? "" : ",") << "\n";
  }
  std::cout << indent << "  ],\n";
  std::cout << indent << "  \"blockers\": [\n";
  for (std::size_t index = 0; index < report.blockers.size(); ++index) {
    const auto& blocker = report.blockers.at(index);
    std::cout << indent << "    {\"blockerKind\": ";
    print_json_string(std::cout, blocker.blocker_kind);
    std::cout << ", \"opportunities\": " << blocker.opportunities;
    std::cout << ", \"potentialSavings\": " << blocker.potential_savings;
    std::cout << ", \"bestSavings\": " << blocker.best_savings;
    if (!blocker.best_site.empty()) {
      std::cout << ", \"bestSite\": ";
      print_json_string(std::cout, blocker.best_site);
    }
    if (!blocker.best_variant.empty()) {
      std::cout << ", \"bestVariant\": ";
      print_json_string(std::cout, blocker.best_variant);
    }
    if (!blocker.best_reason.empty()) {
      std::cout << ", \"bestReason\": ";
      print_json_string(std::cout, blocker.best_reason);
    }
    if (!blocker.best_details.empty()) {
      std::cout << ", \"bestDetails\": {";
      std::size_t detail_index = 0;
      for (const auto& [key, value] : blocker.best_details) {
        if (detail_index++ > 0)
          std::cout << ", ";
        print_json_string(std::cout, key);
        std::cout << ": ";
        print_json_string(std::cout, value);
      }
      std::cout << "}";
    }
    std::cout << "}" << (index + 1U == report.blockers.size() ? "" : ",") << "\n";
  }
  std::cout << indent << "  ],\n";
  std::cout << indent << "  \"nextActions\": [\n";
  for (std::size_t index = 0; index < report.next_actions.size(); ++index) {
    const auto& action = report.next_actions.at(index);
    std::cout << indent << "    {\"source\": ";
    print_json_string(std::cout, action.source);
    std::cout << ", \"action\": ";
    print_json_string(std::cout, action.action);
    std::cout << ", \"opportunities\": " << action.opportunities;
    std::cout << ", \"potentialSavings\": " << action.potential_savings;
    std::cout << ", \"bestSavings\": " << action.best_savings;
    if (!action.best_site.empty()) {
      std::cout << ", \"bestSite\": ";
      print_json_string(std::cout, action.best_site);
    }
    if (!action.best_variant.empty()) {
      std::cout << ", \"bestVariant\": ";
      print_json_string(std::cout, action.best_variant);
    }
    if (!action.best_blocker_kind.empty()) {
      std::cout << ", \"bestBlockerKind\": ";
      print_json_string(std::cout, action.best_blocker_kind);
    }
    if (!action.best_reason.empty()) {
      std::cout << ", \"bestReason\": ";
      print_json_string(std::cout, action.best_reason);
    }
    if (!action.best_details.empty()) {
      std::cout << ", \"bestDetails\": {";
      std::size_t detail_index = 0;
      for (const auto& [key, value] : action.best_details) {
        if (detail_index++ > 0)
          std::cout << ", ";
        print_json_string(std::cout, key);
        std::cout << ": ";
        print_json_string(std::cout, value);
      }
      std::cout << "}";
    }
    std::cout << "}" << (index + 1U == report.next_actions.size() ? "" : ",") << "\n";
  }
  std::cout << indent << "  ]\n";
  std::cout << indent.substr(0, indent.size() - 2U) << "}";
}

void print_json_machine_features(const std::vector<mkpro::MachineFeatureUseReport>& features,
                                 std::string indent) {
  std::cout << "[\n";
  for (std::size_t index = 0; index < features.size(); ++index) {
    const auto& feature = features[index];
    std::cout << indent << "  {\"id\": ";
    print_json_string(std::cout, feature.id);
    std::cout << ", \"source\": ";
    print_json_string(std::cout, feature.source);
    std::cout << ", \"detail\": ";
    print_json_string(std::cout, feature.detail);
    std::cout << "}" << (index + 1U == features.size() ? "" : ",") << "\n";
  }
  std::cout << indent.substr(0, indent.size() - 2U) << "]";
}

void print_json_proofs(const std::vector<mkpro::ProofReport>& proofs, std::string indent) {
  std::cout << "[\n";
  for (std::size_t index = 0; index < proofs.size(); ++index) {
    const auto& proof = proofs[index];
    std::cout << indent << "  {\"id\": ";
    print_json_string(std::cout, proof.id);
    std::cout << ", \"status\": ";
    print_json_string(std::cout, proof.status);
    std::cout << ", \"detail\": ";
    print_json_string(std::cout, proof.detail);
    std::cout << "}" << (index + 1U == proofs.size() ? "" : ",") << "\n";
  }
  std::cout << indent.substr(0, indent.size() - 2U) << "]";
}

void print_json_emulator_facts(const std::vector<mkpro::EmulatorFactReport>& facts,
                               std::string indent) {
  std::cout << "[\n";
  for (std::size_t index = 0; index < facts.size(); ++index) {
    const auto& fact = facts[index];
    std::cout << indent << "  {\"id\": ";
    print_json_string(std::cout, fact.id);
    std::cout << ", \"status\": ";
    print_json_string(std::cout, fact.status);
    std::cout << ", \"detail\": ";
    print_json_string(std::cout, fact.detail);
    std::cout << "}" << (index + 1U == facts.size() ? "" : ",") << "\n";
  }
  std::cout << indent.substr(0, indent.size() - 2U) << "]";
}

void print_json_report(const mkpro::CompileResult& result) {
  std::cout << "{\n";
  std::cout << "    \"steps\": " << result.steps.size();
  std::cout << ",\n    \"machine\": \"mk61\"";
  std::cout << ",\n    \"registers\": ";
  print_json_string_map(result.registers, "      ");
  std::cout << ",\n    \"labels\": ";
  print_json_string_map(result.labels, "      ");
  std::cout << ",\n    \"optimizations\": [\n";
  for (std::size_t index = 0; index < result.optimizations.size(); ++index) {
    const auto& optimization = result.optimizations[index];
    std::cout << "      {\"name\": ";
    print_json_string(std::cout, optimization.name);
    std::cout << ", \"detail\": ";
    print_json_string(std::cout, optimization.detail);
    std::cout << "}" << (index + 1U == result.optimizations.size() ? "" : ",") << "\n";
  }
  std::cout << "    ]";
  std::cout << ",\n    \"warnings\": ";
  print_json_string_array(result.warnings, "      ");
  std::cout << ",\n    \"optimizer\": ";
  print_json_optimizer(result.optimizer, "      ");
  std::cout << ",\n    \"ir\": {\"lowered\": " << (result.ir.lowered ? "true" : "false")
            << ", \"v2\": " << (result.ir.v2 ? "true" : "false")
            << ", \"intentNodes\": " << result.ir.intent_nodes
            << ", \"effectOps\": " << result.ir.effect_ops
            << ", \"layoutCells\": " << result.ir.layout_cells << "}";
  std::cout << ",\n    \"sizeAttribution\": ";
  print_json_size_attribution(result.size_attribution, "      ");
  std::cout << ",\n    \"candidates\": ";
  print_json_candidate_reports(result.candidates, "      ");
  std::cout << ",\n    \"machineFeaturesUsed\": ";
  print_json_machine_features(result.machine_features_used, "      ");
  std::cout << ",\n    \"proofs\": ";
  print_json_proofs(result.proofs, "      ");
  std::cout << ",\n    \"emulatorFacts\": ";
  print_json_emulator_facts(result.emulator_facts, "      ");
  std::cout << ",\n    \"rejectedCandidates\": ";
  print_json_candidate_reports(result.rejected_candidates, "      ");
  if (result.reference.has_value()) {
    std::cout << ",\n    \"reference\": ";
    print_json_reference(*result.reference, "      ");
  }
  std::cout << "\n  }";
}

void print_json(const mkpro::CompileResult& result) {
  std::cout << "{\n  \"steps\": ";
  print_json_steps(result.steps, "    ");
  std::cout << ",\n  \"registers\": ";
  print_json_string_map(result.registers, "    ");
  std::cout << ",\n  \"preloads\": [\n";
  for (std::size_t index = 0; index < result.preloads.size(); ++index) {
    const auto& preload = result.preloads[index];
    std::cout << "    {\"register\": ";
    print_json_string(std::cout, preload.register_name);
    std::cout << ", \"value\": ";
    print_json_string(std::cout, preload.value);
    std::cout << ", \"countsAgainstProgram\": "
              << (preload.counts_against_program ? "true" : "false") << "}"
              << (index + 1U == result.preloads.size() ? "" : ",") << "\n";
  }
  std::cout << "  ]";
  std::cout << ",\n  \"optimizations\": [\n";
  for (std::size_t index = 0; index < result.optimizations.size(); ++index) {
    const auto& optimization = result.optimizations[index];
    std::cout << "    {\"name\": ";
    print_json_string(std::cout, optimization.name);
    std::cout << ", \"detail\": ";
    print_json_string(std::cout, optimization.detail);
    std::cout << "}" << (index + 1U == result.optimizations.size() ? "" : ",") << "\n";
  }
  std::cout << "  ]";
  std::cout << ",\n  \"warnings\": [\n";
  for (std::size_t index = 0; index < result.warnings.size(); ++index) {
    std::cout << "    ";
    print_json_string(std::cout, result.warnings[index]);
    std::cout << (index + 1U == result.warnings.size() ? "" : ",") << "\n";
  }
  std::cout << "  ]";
  if (!result.coalesce_shares.empty()) {
    std::cout << ",\n  \"coalesceShares\": [\n";
    for (std::size_t index = 0; index < result.coalesce_shares.size(); ++index) {
      const auto& share = result.coalesce_shares[index];
      std::cout << "    {\"freeRegister\": ";
      print_json_string(std::cout, share.free_register);
      std::cout << ", \"keepRegister\": ";
      print_json_string(std::cout, share.keep_register);
      std::cout << "}" << (index + 1U == result.coalesce_shares.size() ? "" : ",") << "\n";
    }
    std::cout << "  ]";
  }
  if (result.setup_program.has_value()) {
    std::cout << ",\n  \"setupProgram\": {\"reason\": ";
    print_json_string(std::cout, result.setup_program->reason);
    std::cout << ", \"steps\": ";
    print_json_steps(result.setup_program->steps, "    ");
    std::cout << "}";
  }
  std::cout << ",\n  \"report\": ";
  print_json_report(result);
  std::cout << ",\n  \"diagnostics\": " << result.diagnostics.size() << "\n}\n";
}

std::string format_address_operand_key(int opcode) {
  const int address = mkpro::code_to_address(opcode);
  if (address >= 0 && address <= 104)
    return mkpro::format_address(address);
  return mkpro::format_formal_address_opcode(opcode);
}

void append_step_keys(std::vector<std::string>& tokens, const std::vector<mkpro::ResolvedStep>& steps) {
  std::optional<std::string> previous_cell_key;
  for (std::size_t index = 0; index < steps.size(); ++index) {
    const mkpro::ResolvedStep& step = steps[index];
    const mkpro::OpcodeInfo& opcode = mkpro::opcode_by_code(step.opcode);
    if (step.opcode == 0x3e && previous_cell_key.has_value()) {
      tokens.push_back("ШГ←");
      tokens.push_back("БП");
      tokens.push_back("3");
      tokens.push_back("В↑");
      tokens.push_back("ШГ←");
      tokens.push_back("ШГ←");
      tokens.push_back(*previous_cell_key);
      tokens.push_back("ШГ→");
      previous_cell_key.reset();
      continue;
    }
    if (step.opcode == 0x3e) {
      tokens.push_back("[3E:requires-previous-editable-step;use-hex-or-mk61s]");
      previous_cell_key.reset();
      continue;
    }
    tokens.push_back(opcode.keys);
    if (opcode.takes_address && index + 1U < steps.size()) {
      const std::string address_key = format_address_operand_key(steps[index + 1U].opcode);
      tokens.push_back(address_key);
      ++index;
      previous_cell_key = address_key;
    } else if (step.opcode == 0x3e) {
      previous_cell_key.reset();
    } else {
      previous_cell_key = opcode.keys;
    }
  }
}

std::string format_keys_result(const mkpro::CompileResult& result) {
  std::vector<std::string> tokens;
  if (result.setup_program.has_value())
    append_step_keys(tokens, result.setup_program->steps);
  append_step_keys(tokens, result.steps);

  std::ostringstream out;
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (index != 0)
      out << ' ';
    out << tokens[index];
  }
  return out.str();
}

std::string join_strings(const std::vector<std::string>& values, std::string_view separator) {
  std::ostringstream out;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0)
      out << separator;
    out << values[index];
  }
  return out.str();
}

const char* yes_no(bool value) {
  return value ? "yes" : "no";
}

void print_human_analysis_report(const mkpro::CompileResult& result) {
  std::cout << "\n## Optimizer Report\n";
  std::cout << "automatic: " << yes_no(result.optimizer.automatic)
            << ", active: " << result.optimizer.active
            << ", considered: " << result.optimizer.considered
            << ", candidate: " << result.optimizer.candidate
            << ", planned: " << result.optimizer.planned << "\n";

  std::cout << "\n### Capabilities\n";
  if (result.optimizer.capabilities.empty()) {
    std::cout << "(none)\n";
  } else {
    for (const auto& capability : result.optimizer.capabilities) {
      std::cout << "- " << capability.id << " [" << capability.status << "]";
      if (!capability.category.empty())
        std::cout << " category=" << capability.category;
      if (!capability.source.empty())
        std::cout << " source=" << capability.source;
      if (!capability.required_features.empty())
        std::cout << " requires=" << join_strings(capability.required_features, ",");
      if (!capability.detail.empty())
        std::cout << " - " << capability.detail;
      std::cout << "\n";
    }
  }

  std::cout << "\n### Applied Optimizations\n";
  if (result.optimizations.empty()) {
    std::cout << "(none)\n";
  } else {
    for (const auto& optimization : result.optimizations) {
      std::cout << "- " << optimization.name;
      if (!optimization.detail.empty())
        std::cout << " - " << optimization.detail;
      std::cout << "\n";
    }
  }

  std::cout << "\n### Candidate Paths\n";
  if (result.candidates.empty()) {
    std::cout << "(none)\n";
  } else {
    for (const auto& candidate : result.candidates) {
      std::cout << "- " << candidate.site << " :: " << candidate.variant
                << " steps=" << candidate.steps
                << " selected=" << yes_no(candidate.selected);
      if (!candidate.reason.empty())
        std::cout << " - " << candidate.reason;
      std::cout << "\n";
    }
  }

  std::cout << "\n### Rejected Candidate Paths\n";
  if (result.rejected_candidates.empty()) {
    std::cout << "(none)\n";
  } else {
    for (const auto& candidate : result.rejected_candidates) {
      std::cout << "- " << candidate.site << " :: " << candidate.variant
                << " steps=" << candidate.steps;
      if (!candidate.reason.empty())
        std::cout << " - " << candidate.reason;
      std::cout << "\n";
    }
  }

  std::cout << "\n### Size Attribution\n";
  std::cout << "total cells: " << result.size_attribution.total_cells << "\n";
  if (result.size_attribution.entries.empty()) {
    std::cout << "(none)\n";
  } else {
    std::size_t printed = 0;
    for (const auto& entry : result.size_attribution.entries) {
      if (printed >= 15U)
        break;
      std::cout << "- " << entry.kind << " :: " << entry.label << " cells=" << entry.cells
                << " occurrences=" << entry.occurrences << " range=" << entry.first_address
                << ".." << entry.last_address;
      if (!entry.detail.empty())
        std::cout << " - " << entry.detail;
      std::cout << "\n";
      ++printed;
    }
  }

  std::cout << "\n### Size Helpers\n";
  if (result.size_attribution.helpers.empty()) {
    std::cout << "(none)\n";
  } else {
    std::size_t printed = 0;
    for (const auto& helper : result.size_attribution.helpers) {
      if (printed >= 12U)
        break;
      std::cout << "- " << helper.label
                << " total=" << helper.total_cells
                << " body=" << helper.body_cells
                << " calls=" << helper.call_site_cells
                << " callOccurrences=" << helper.call_occurrences
                << " range=" << helper.first_address << ".." << helper.last_address;
      if (!helper.details.empty()) {
        std::cout << " details={";
        std::size_t detail_index = 0;
        for (const auto& [key, value] : helper.details) {
          if (detail_index++ > 0)
            std::cout << ", ";
          std::cout << key << "=" << value;
        }
        std::cout << "}";
      }
      std::cout << "\n";
      ++printed;
    }
  }

  std::cout << "\n### Size Helper Spills\n";
  if (result.size_attribution.helper_spills.empty()) {
    std::cout << "(none)\n";
  } else {
    std::size_t printed = 0;
    for (const auto& spill : result.size_attribution.helper_spills) {
      if (printed >= 12U)
        break;
      std::cout << "- " << spill.helper_label << " :: " << spill.name
                << " total=" << spill.total_cells
                << " recalls=" << spill.recall_cells
                << " stores=" << spill.store_cells
                << " occurrences=" << (spill.recall_occurrences + spill.store_occurrences)
                << " range=" << spill.first_address << ".." << spill.last_address << "\n";
      ++printed;
    }
  }

  std::cout << "\n### Size ABI Blockers\n";
  if (result.size_attribution.abi_blockers.empty()) {
    std::cout << "(none)\n";
  } else {
    std::size_t printed = 0;
    for (const auto& blocker : result.size_attribution.abi_blockers) {
      if (printed >= 12U)
        break;
      std::cout << "- " << blocker.kind << " :: " << blocker.label
                << " line=" << blocker.line
                << " materializeCells=" << blocker.materialize_cells;
      const auto action = blocker.details.find("requiredAction");
      if (action != blocker.details.end())
        std::cout << " requiredAction=" << action->second;
      if (!blocker.reason.empty())
        std::cout << " - " << blocker.reason;
      std::cout << "\n";
      ++printed;
    }
  }

  std::cout << "\n### Size Opportunities\n";
  if (result.size_attribution.opportunities.empty()) {
    std::cout << "(none)\n";
  } else {
    std::size_t printed = 0;
    for (const auto& opportunity : result.size_attribution.opportunities) {
      if (printed >= 12U)
        break;
      std::cout << "- " << opportunity.site << " :: " << opportunity.variant
                << " current=" << opportunity.current_steps
                << " candidate=" << opportunity.candidate_steps
                << " savings=" << opportunity.savings;
      if (!opportunity.blocker_kind.empty())
        std::cout << " blocker=" << opportunity.blocker_kind;
      if (!opportunity.reason.empty())
        std::cout << " - " << opportunity.reason;
      std::cout << "\n";
      ++printed;
    }
  }

  std::cout << "\n### Size Spills\n";
  if (result.size_attribution.spills.empty()) {
    std::cout << "(none)\n";
  } else {
    std::size_t printed = 0;
    for (const auto& spill : result.size_attribution.spills) {
      if (printed >= 12U)
        break;
      std::cout << "- " << spill.name
                << " total=" << spill.total_cells
                << " recalls=" << spill.recall_cells
                << " stores=" << spill.store_cells
                << " occurrences=" << (spill.recall_occurrences + spill.store_occurrences)
                << " range=" << spill.first_address << ".." << spill.last_address << "\n";
      ++printed;
    }
  }

  std::cout << "\n### Size Blockers\n";
  if (result.size_attribution.blockers.empty()) {
    std::cout << "(none)\n";
  } else {
    std::size_t printed = 0;
    for (const auto& blocker : result.size_attribution.blockers) {
      if (printed >= 12U)
        break;
      std::cout << "- " << blocker.blocker_kind
                << " opportunities=" << blocker.opportunities
                << " potentialSavings=" << blocker.potential_savings
                << " best=" << blocker.best_savings;
      if (!blocker.best_variant.empty())
        std::cout << " via " << blocker.best_variant;
      if (!blocker.best_site.empty())
        std::cout << " at " << blocker.best_site;
      if (!blocker.best_reason.empty())
        std::cout << " - " << blocker.best_reason;
      std::cout << "\n";
      ++printed;
    }
  }

  std::cout << "\n### Size Next Actions\n";
  if (result.size_attribution.next_actions.empty()) {
    std::cout << "(none)\n";
  } else {
    std::size_t printed = 0;
    for (const auto& action : result.size_attribution.next_actions) {
      if (printed >= 12U)
        break;
      std::cout << "- " << action.source << "=" << action.action
                << " opportunities=" << action.opportunities
                << " potentialSavings=" << action.potential_savings
                << " best=" << action.best_savings;
      if (!action.best_blocker_kind.empty())
        std::cout << " blocker=" << action.best_blocker_kind;
      if (!action.best_variant.empty())
        std::cout << " via " << action.best_variant;
      if (!action.best_site.empty())
        std::cout << " at " << action.best_site;
      if (!action.best_reason.empty())
        std::cout << " - " << action.best_reason;
      std::cout << "\n";
      ++printed;
    }
  }

  std::cout << "\n### Proofs\n";
  if (result.proofs.empty()) {
    std::cout << "(none)\n";
  } else {
    for (const auto& proof : result.proofs) {
      std::cout << "- " << proof.id << " [" << proof.status << "]";
      if (!proof.detail.empty())
        std::cout << " - " << proof.detail;
      std::cout << "\n";
    }
  }

  std::cout << "\n### Emulator Facts\n";
  if (result.emulator_facts.empty()) {
    std::cout << "(none)\n";
  } else {
    for (const auto& fact : result.emulator_facts) {
      std::cout << "- " << fact.id << " [" << fact.status << "]";
      if (!fact.detail.empty())
        std::cout << " - " << fact.detail;
      std::cout << "\n";
    }
  }
}

void print_result(const mkpro::CompileResult& result, mkpro::OutputFormat output, bool analysis) {
  if (output == mkpro::OutputFormat::Hex) {
    std::cout << result.hex << "\n";
  } else if (output == mkpro::OutputFormat::Mk61s) {
    std::cout << mkpro::format_mk61s_result(result) << "\n";
  } else if (output == mkpro::OutputFormat::Dot) {
    std::cout << mkpro::format_dot_result(result);
  } else if (output == mkpro::OutputFormat::Json) {
    print_json(result);
  } else if (output == mkpro::OutputFormat::Keys) {
    std::cout << format_keys_result(result) << "\n";
  } else if (output == mkpro::OutputFormat::All) {
    std::cout << "## Listing\n" << result.listing << "\n";
    if (!result.setup_hex.empty())
      std::cout << "\n## Setup Hex\n" << result.setup_hex << "\n";
    std::cout << "\n## Hex\n" << result.hex << "\n";
    if (analysis)
      print_human_analysis_report(result);
  } else {
    std::cout << result.listing << "\n";
  }
}

int run_compile_like(const std::string& command, std::vector<std::string> args) {
  if (args.empty()) {
    print_usage(std::cerr);
    return 64;
  }

  const std::filesystem::path source_path = args.front();
  args.erase(args.begin());

  mkpro::CompileOptions options;
  bool behavior_digest = false;
  if (command == "explain") {
    options.output = mkpro::OutputFormat::All;
    options.analysis = true;
  }

  for (std::size_t index = 0; index < args.size(); ++index) {
    const std::string& arg = args[index];
    if (arg == "--analysis") {
      options.analysis = true;
    } else if (arg == "--behavior-digest") {
      behavior_digest = true;
    } else if (arg == "--disable-candidate-search") {
      options.disable_candidate_search = true;
    } else if (arg == "--fast") {
      options.fast_candidate_search = true;
    } else if (arg == "--fast-threshold-ms") {
      if (index + 1 >= args.size()) {
        std::cerr << "missing value for --fast-threshold-ms\n";
        return 64;
      }
      try {
        options.fast_candidate_threshold_ms = std::stoi(args[++index]);
      } catch (const std::exception&) {
        std::cerr << "invalid --fast-threshold-ms value: " << args[index] << "\n";
        return 64;
      }
      if (options.fast_candidate_threshold_ms <= 0) {
        std::cerr << "invalid --fast-threshold-ms value: " << args[index] << "\n";
        return 64;
      }
      options.fast_candidate_search = true;
    } else if (arg == "--collect-coalesce-shares") {
      options.collect_coalesce_shares = true;
      options.analysis = true;
    } else if (arg == "--x-param-value-functions") {
      options.x_param_value_functions = true;
    } else if (arg == "--x-param-y-stack-stored-entry") {
      options.x_param_y_stack_stored_entry = true;
    } else if (arg == "--canonicalize-repeated-unary-update-args") {
      options.canonicalize_repeated_unary_update_args = true;
    } else if (arg == "--coalesce-copies") {
      options.coalesce_copies = true;
    } else if (arg == "--shared-straight-line-call-bodies") {
      options.shared_straight_line_call_bodies = true;
    } else if (arg == "--segmented-bitplanes") {
      options.segmented_bitplanes = true;
    } else if (arg == "--segmented-line-count-scan") {
      options.segmented_bitplanes = true;
      options.segmented_line_count_scan = true;
    } else if (arg == "--compact-bit-mask-helper-body") {
      options.compact_bit_mask_helper_body = true;
    } else if (arg == "--shared-bit-mask-helper-calls") {
      options.shared_bit_mask_helper_calls = true;
    } else if (arg == "--general-constant-preloads") {
      options.general_constant_preloads = true;
    } else if (arg == "--dual-use-constant-indirect-flow") {
      options.dual_use_constant_indirect_flow = true;
    } else if (arg == "--indirect-underflow-decrement") {
      options.indirect_underflow_decrement = true;
    } else if (arg == "--tail-branch-inversion") {
      options.tail_branch_inversion = true;
    } else if (arg == "--invert-branch-order") {
      options.invert_branch_order = true;
    } else if (arg == "--conditional-branch-trampoline") {
      options.conditional_branch_trampoline = true;
    } else if (arg == "--preloaded-indirect-flow") {
      options.preloaded_indirect_flow = true;
      options.aggressive_post_layout_indirect_flow = true;
    } else if (arg == "--forward-indirect-flow") {
      options.forward_indirect_flow = true;
    } else if (arg == "--runtime-indirect-call-flow") {
      options.runtime_indirect_call_flow = true;
    } else if (arg == "--aggressive-post-layout-indirect-flow") {
      options.aggressive_post_layout_indirect_flow = true;
    } else if (arg == "--return-stack-script") {
      options.return_stack_script = true;
      options.disable_return_stack_script = false;
    } else if (arg == "--no-return-stack-script") {
      options.disable_return_stack_script = true;
    } else if (arg == "--dead-source-residual-temp-reuse") {
      options.dead_source_residual_temp_reuse = true;
    } else if (arg == "--free-residual-dispatch-scratch") {
      options.free_residual_dispatch_scratch = true;
    } else if (arg == "--domain-error-guards") {
      options.domain_error_guards = true;
    } else if (arg == "--unroll-counted-loops") {
      options.unroll_counted_loops = true;
    } else if (arg == "--setup-only-counted-loop-init") {
      options.setup_only_counted_loop_init = true;
    } else if (arg == "--show-read-guarded-transfer") {
      options.show_read_guarded_transfer = true;
    } else if (arg == "--stack-resident-temps") {
      options.stack_resident_temps = true;
    } else if (arg == "--inline-floor-packed-row-expressions") {
      options.inline_floor_packed_row_expressions = true;
    } else if (arg == "--order-procs-by-call-count") {
      options.order_procs_by_call_count = true;
    } else if (arg == "--proc-layout-strategy") {
      if (index + 1 >= args.size()) {
        std::cerr << "missing value for --proc-layout-strategy\n";
        return 64;
      }
      const std::string value = args[++index];
      if (!is_proc_layout_strategy(value)) {
        std::cerr << "unknown --proc-layout-strategy value: " << value << "\n";
        return 64;
      }
      options.proc_layout_strategy = value;
    } else if (arg == "--aggressive-indirect-call") {
      options.aggressive_indirect_call = true;
    } else if (arg == "--share-random-cell") {
      options.share_random_cell = true;
    } else if (arg == "--hoist-shared-helpers") {
      options.hoist_shared_helpers = true;
    } else if (arg == "--hoist-procs") {
      options.hoist_procs = true;
    } else if (arg == "--suppress-constant-preload") {
      if (index + 1 >= args.size()) {
        std::cerr << "missing value for --suppress-constant-preload\n";
        return 64;
      }
      options.suppress_constant_preloads.insert(args[++index]);
    } else if (arg == "--fractional-selector") {
      if (index + 1 >= args.size()) {
        std::cerr << "missing value for --fractional-selector\n";
        return 64;
      }
      const std::string value = args[++index];
      const std::size_t colon = value.find(':');
      if (colon == std::string::npos || colon == 0 || colon + 1 >= value.size()) {
        std::cerr << "invalid --fractional-selector value: " << value << "\n";
        return 64;
      }
      int target = 0;
      try {
        target = std::stoi(value.substr(colon + 1));
      } catch (const std::exception&) {
        std::cerr << "invalid --fractional-selector target: " << value << "\n";
        return 64;
      }
      options.fractional_constant_selectors.push_back(mkpro::FractionalConstantSelectorPlan{
          .value = value.substr(0, colon),
          .target = target,
      });
    } else if (arg == "--force-fractional-selector-preload") {
      if (index + 1 >= args.size()) {
        std::cerr << "missing value for --force-fractional-selector-preload\n";
        return 64;
      }
      options.force_fractional_constant_selector_preloads.push_back(args[++index]);
    } else if (arg == "--force-register-share") {
      if (index + 1 >= args.size()) {
        std::cerr << "missing value for --force-register-share\n";
        return 64;
      }
      const std::string value = args[++index];
      const std::size_t colon = value.find(':');
      if (colon == std::string::npos || colon == 0 || colon + 1 >= value.size()) {
        std::cerr << "invalid --force-register-share value: " << value << "\n";
        return 64;
      }
      options.forced_register_shares.push_back(mkpro::RegisterShare{
          .free_register = value.substr(0, colon),
          .keep_register = value.substr(colon + 1),
      });
    } else if (arg == "--strict") {
      options.strict = true;
    } else if (arg == "--out") {
      if (index + 1 >= args.size()) {
        std::cerr << "missing value for --out\n";
        return 64;
      }
      auto output = parse_output_format(args[++index]);
      if (!output.has_value()) {
        std::cerr << "unknown --out value: " << args[index] << "\n";
        return 64;
      }
      options.output = *output;
    } else if (arg == "--delivery") {
      if (index + 1 >= args.size()) {
        std::cerr << "missing value for --delivery\n";
        return 64;
      }
      auto delivery = parse_delivery_mode(args[++index]);
      if (!delivery.has_value()) {
        std::cerr << "unknown --delivery value: " << args[index] << "\n";
        return 64;
      }
      options.delivery = *delivery;
    } else if (arg == "--budget") {
      if (index + 1 >= args.size()) {
        std::cerr << "missing value for --budget\n";
        return 64;
      }
      try {
        options.budget = std::stoi(args[++index]);
      } catch (const std::exception&) {
        std::cerr << "invalid --budget value: " << args[index] << "\n";
        return 64;
      }
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 64;
    }
  }

  try {
    const mkpro::CompileResult result = mkpro::compile_source(read_source(source_path), options);
    if (behavior_digest) {
      std::cout << mkpro::program_behavior_digest(result);
      return result.implemented ? 0 : 78;
    }
    print_diagnostics(result);
    if (result.implemented)
      print_result(result, options.output, options.analysis);
    return result.implemented ? 0 : 78;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 66;
  }
}

} // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty() || args.front() == "--help" || args.front() == "-h") {
    print_usage(std::cout);
    return 0;
  }
  if (args.front() == "--version") {
    std::cout << "mkpro-native " << mkpro::kNativeVersion << " (" << mkpro::kNativeStatus << ")\n";
    return 0;
  }

  const std::string command = args.front();
  args.erase(args.begin());
  if (command == "compile" || command == "explain") {
    return run_compile_like(command, args);
  }

  std::cerr << "unknown command: " << command << "\n";
  print_usage(std::cerr);
  return 64;
}
