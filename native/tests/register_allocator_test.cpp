#include "mkpro/core/register_allocator.hpp"

#include "mkpro/core/emit/lowering_helpers.hpp"

#include "test_support.hpp"

#include <set>
#include <stdexcept>

namespace mkpro::tests {

void register_allocator_matches_typescript_contract() {
  require(core::register_name_for_index(0x0) == "0", "register 0 text");
  require(core::register_name_for_index(0x9) == "9", "register 9 text");
  require(core::register_name_for_index(0x0a) == "a", "register a text");
  require(core::register_name_for_index(0x0e) == "e", "register e text");
  require(core::register_name_for_index(0x0f, FeatureProfile::Mk61SMiniExpanded) == "f",
          "expanded register f text");

  bool threw = false;
  try {
    (void)core::register_name_for_index(0x0f);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  require(threw, "register formatter should reject addresses outside 0..e");

  require(core::pick_register_index_for_variable("plain", {}).value() == 0x0,
          "ordinary variables should allocate from R0 upward");
  require(core::pick_register_index_for_variable("__dispatch_12", {}).value() == 0x0e,
          "dispatch scratch should allocate from high registers");
  require(core::pick_register_index_for_variable("__grid4_mask_12", {}).value() == 0x0e,
          "grid mask scratch should allocate from high registers");
  require(core::pick_register_index_for_variable("__bit_mask_quotient", {}).value() == 0x0e,
          "bit-mask scratch should allocate from high registers");
  require(core::pick_register_index_for_variable("__if_selector_12", {}).value() == 0x0e,
          "if-selector scratch should allocate from high registers");
  require(core::pick_register_index_for_variable("__bank_selector_passages", {}).value() == 0x0e,
          "state-bank selectors should allocate from high indirect registers");

  require(core::pick_register_index_for_variable("__display_loop_score", {}).value() == 0x1,
          "display loop scratch should prefer R1");
  require(core::pick_register_index_for_variable("__display_value_score", {}).value() == 0x2,
          "display value scratch should prefer R2");
  require(core::pick_register_index_for_variable("__display_mask_score", {}).value() == 0x0e,
          "display mask scratch should allocate from high registers");

  require(core::pick_register_index_for_variable(core::emit::k_coord_list_pointer, {}).value() ==
              0x5,
          "coord-list pointer should prefer R5");
  require(core::pick_register_index_for_variable(core::emit::k_coord_list_counter, {}).value() ==
              0x2,
          "coord-list counter should prefer R2");
  require(core::pick_register_index_for_variable(core::emit::k_coord_list_current, {}).value() ==
              0x0,
          "coord-list current should prefer R0");
  require(core::pick_register_index_for_variable(core::emit::k_coord_list_dx, {}).value() == 0x0,
          "coord-list dx should prefer R0 when free");

  const std::string spatial_counter = core::emit::spatial_count_counter_scratch_name();
  require(core::pick_register_index_for_variable(spatial_counter, {}).value() == 0x0,
          "spatial-count counter should prefer low loop registers");
  require(core::pick_register_index_for_variable(core::emit::spatial_count_total_scratch_name(),
                                                 {})
              .value() == 0x0e,
          "spatial-count non-counter scratch should allocate from high registers");

  require(core::pick_constant_register_index({}).value() == 0x0e,
          "constant preloads should allocate from high registers");
  require(core::pick_constant_register_index({0x0e, 0x0d}).value() == 0x0c,
          "constant preloads should skip used high registers");
  require(!core::pick_register_index_for_variable("full", {0x0, 0x1, 0x2, 0x3, 0x4, 0x5,
                                                           0x6, 0x7, 0x8, 0x9, 0x0a,
                                                           0x0b, 0x0c, 0x0d, 0x0e})
               .has_value(),
          "allocator should report exhaustion when all registers are used");
  require(core::pick_register_index_for_variable("full", {0x0, 0x1, 0x2, 0x3, 0x4, 0x5,
                                                          0x6, 0x7, 0x8, 0x9, 0x0a,
                                                          0x0b, 0x0c, 0x0d, 0x0e},
                                                 FeatureProfile::Mk61SMiniExpanded)
              .value() == 0x0f,
          "expanded allocator should use RF after R0..RE");
  require(core::pick_constant_register_index({}, FeatureProfile::Mk61SMiniExpanded).value() ==
              0x0f,
          "expanded constant preloads should prefer RF as the highest stable register");
}

}  // namespace mkpro::tests
