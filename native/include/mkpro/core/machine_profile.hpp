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
  std::vector<EmulatorFactReport> emulator_facts;
};

const MachineProfile& mk61_profile();
bool machine_supports(const MachineProfile& profile, std::string_view feature_id);

}  // namespace mkpro
