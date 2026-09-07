#pragma once

#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

namespace mkpro::core {

// Prove that introducing/removing selector writeback at the named command
// cannot change a later data observation. Command and return identities are
// logical; no physical address is required during this lifetime proof.
bool selector_writeback_is_unobserved(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control_flow,
    std::size_t command_item, const PreloadReport& preload,
    AddressSpaceModel model = AddressSpaceModel::Standard,
    std::string* failure = nullptr);

} // namespace mkpro::core
