#include "mkpro/core/super_dark_layout.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

LayoutIrCell cell(int address, int opcode = 0x00,
                  std::vector<std::string> roles = {"exec"}) {
  return LayoutIrCell{
      .address = address,
      .opcode = opcode,
      .roles = std::move(roles),
      .tactic = "",
  };
}

std::vector<LayoutIrCell> sixty_cell_layout() {
  std::vector<LayoutIrCell> layout;
  layout.reserve(61);
  for (int address = 0; address < 60; ++address)
    layout.push_back(cell(address, address % 10));
  return layout;
}

std::vector<std::vector<int>> pair_matrix(const SuperDarkLayoutProof& proof) {
  std::vector<std::vector<int>> matrix;
  matrix.reserve(proof.pairs.size());
  for (const SuperDarkLayoutPair& pair : proof.pairs) {
    matrix.push_back({pair.formal, pair.entry_address, pair.continuation_address});
  }
  return matrix;
}

std::string joined_reasons(const SuperDarkLayoutProof& proof) {
  std::string result;
  for (const std::string& reason : proof.reasons) {
    if (!result.empty())
      result += "\n";
    result += reason;
  }
  return result;
}

bool contains_text(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

} // namespace

void super_dark_layout_matches_typescript_contract() {
  {
    std::vector<LayoutIrCell> layout = sixty_cell_layout();
    layout.push_back(LayoutIrCell{
        .address = 60,
        .opcode = 0x87,
        .roles = {"exec"},
        .tactic = "super-dark indirect dispatch",
    });
    SuperDarkLayoutProof proof = verify_super_dark_suffix_layout(
        layout, {.selector_values = {{"7", "FA..FF"}}});

    require(proof.proved, "FA..FF super-dark matrix should be proved");
    require(proof.reasons.empty(), "proved FA..FF matrix should have no rejection reasons");
    require(proof.dispatch_cells.size() == 1, "FA..FF proof should find one dispatch cell");
    require(proof.dispatch_cells.front().address == 60 &&
                proof.dispatch_cells.front().opcode == 0x87 &&
                proof.dispatch_cells.front().register_name == "7" &&
                proof.dispatch_cells.front().tactic == "super-dark indirect dispatch" &&
                proof.dispatch_cells.front().selector_value == "FA..FF",
            "FA..FF proof should expose the dispatch cell metadata");
    require(pair_matrix(proof) == std::vector<std::vector<int>>({
                                      {0xfa, 48, 1},
                                      {0xfb, 49, 2},
                                      {0xfc, 50, 3},
                                      {0xfd, 51, 4},
                                      {0xfe, 52, 5},
                                      {0xff, 53, 6},
                                  }),
            "FA..FF proof should expose the exact entry/continuation matrix");
  }

  {
    std::vector<LayoutIrCell> layout = sixty_cell_layout();
    layout.push_back(LayoutIrCell{
        .address = 60,
        .opcode = 0x87,
        .roles = {"exec"},
        .tactic = "preloaded super-dark indirect flow",
    });
    SuperDarkLayoutProof proof =
        verify_super_dark_suffix_layout(layout, {.selector_values = {{"7", "FA"}}});

    require(proof.proved, "single FA super-dark selector should be proved");
    require(pair_matrix(proof) == std::vector<std::vector<int>>({{0xfa, 48, 1}}),
            "single FA selector should prove only the FA entry pair");
  }

  {
    const SuperDarkLayoutProof proof = verify_super_dark_suffix_layout(sixty_cell_layout());
    require(!proof.proved, "suffix-compatible matrix without dispatcher should be rejected");
    require(contains_text(joined_reasons(proof),
                          "no super-dark К БП R dispatch cell"),
            "missing dispatcher rejection should be precise");
  }

  {
    std::vector<LayoutIrCell> layout = sixty_cell_layout();
    layout.push_back(LayoutIrCell{
        .address = 60,
        .opcode = 0x87,
        .roles = {"exec"},
        .tactic = "super-dark indirect dispatch",
    });
    const SuperDarkLayoutProof proof =
        verify_super_dark_suffix_layout(layout, {.selector_values = {{"7", "12"}}});

    require(!proof.proved, "non-super-dark selector should be rejected");
    require(!proof.dispatch_cells.empty() &&
                proof.dispatch_cells.front().register_name == "7" &&
                proof.dispatch_cells.front().selector_value == "12",
            "rejected dispatch should still expose selector metadata");
    require(contains_text(joined_reasons(proof),
                          "no super-dark dispatch register has a proved FA..FF selector"),
            "unproved selector rejection should be precise");
  }

  {
    std::vector<LayoutIrCell> layout = sixty_cell_layout();
    layout.push_back(LayoutIrCell{
        .address = 60,
        .opcode = 0x87,
        .roles = {"exec"},
        .tactic = "super-dark indirect dispatch",
    });
    layout.at(50) = cell(50, 0x51);
    const SuperDarkLayoutProof proof = verify_super_dark_suffix_layout(
        layout, {.selector_values = {{"7", "FA..FF"}}});

    require(!proof.proved, "address-taking super-dark entry should be rejected");
    require(contains_text(joined_reasons(proof),
                          "FC entry 50 is a two-cell address-taking command"),
            "address-taking entry rejection should name the formal entry");
  }

  {
    std::vector<LayoutIrCell> layout = sixty_cell_layout();
    layout.push_back(LayoutIrCell{
        .address = 60,
        .opcode = 0x87,
        .roles = {"exec"},
        .tactic = "super-dark indirect dispatch",
    });
    layout.at(4) = cell(4, 0x04, {"address"});
    const SuperDarkLayoutProof proof = verify_super_dark_suffix_layout(
        layout, {.selector_values = {{"7", "FA..FF"}}});

    require(!proof.proved, "non-executable continuation should be rejected");
    require(contains_text(joined_reasons(proof),
                          "FD continuation 4 is not executable"),
            "non-executable continuation rejection should be precise");
  }
}

} // namespace mkpro::tests
