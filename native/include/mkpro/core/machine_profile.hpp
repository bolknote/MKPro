#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mkpro {

struct MachineFeatureUseReport {
  std::string id;
  std::string source;
  std::string detail;
};

struct EmulatorFactReport {
  std::string id;
  std::string status;
  std::string detail;
};

struct MachineProfile {
  std::string id;
  std::vector<MachineFeatureUseReport> features;
  // Static, probe-backed facts surfaced in reports for traceability. This field
  // is not a runtime emulator oracle and must not be used as an optimizer
  // candidate-acceptance proof gate; risky rewrites need local proof
  // obligations at their own boundary.
  std::vector<EmulatorFactReport> emulator_facts;
};

const MachineProfile& mk61_profile();
bool machine_supports(const MachineProfile& profile, std::string_view feature_id);

}  // namespace mkpro
