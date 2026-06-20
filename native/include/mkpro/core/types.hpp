#pragma once

#include "mkpro/core/result.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mkpro {

enum class X2Effect {
  Affects,
  Restores,
  Preserves,
  Unknown,
};

enum class StackEffect {
  Preserves,
  Shifts,
  ConsumeYDrop,
  ConsumeYKeep,
  Exposes,
  Barrier,
  Unknown,
};

enum class OpcodeRisk {
  Documented,
  Undocumented,
  Dangerous,
};

struct ConditionalX2Effect {
  X2Effect fallthrough = X2Effect::Unknown;
  X2Effect jump = X2Effect::Unknown;
};

struct OpcodeInfo {
  int code = 0;
  std::string hex;
  std::string name;
  std::string keys;
  std::vector<DeliveryMode> enterable;
  bool takes_address = false;
  X2Effect x2_effect = X2Effect::Preserves;
  std::optional<ConditionalX2Effect> conditional_x2_effect;
  StackEffect stack_effect = StackEffect::Preserves;
  OpcodeRisk risk = OpcodeRisk::Documented;
};

} // namespace mkpro
