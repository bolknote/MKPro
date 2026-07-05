#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mkpro {

const std::vector<OpcodeInfo>& opcode_catalog();
const OpcodeInfo& opcode_by_code(int code);
const OpcodeInfo* find_opcode_name(std::string_view name);

int register_index(std::string_view reg);
std::string register_from_text(std::string_view text);

int address_to_opcode(int address);
int address_to_opcode(int address, AddressSpaceModel model);
int code_to_address(int code);
std::string format_address(int address);
std::string format_address(int address, AddressSpaceModel model);

std::string x2_effect_name(X2Effect effect);
std::string stack_effect_name(StackEffect effect);
std::string opcode_risk_name(OpcodeRisk risk);

}  // namespace mkpro
