#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/helper_semantic_alias.hpp"

#include <map>
#include <set>
#include <string>

namespace mkpro::core {

// A conservative, path-sensitive proof for the integral arguments delivered
// to direct rule invocations.  `valid` is false unless every syntactic call
// site for the rule is reachable from the program entry and every value seen
// at that site has a finite integral interval.
struct FlowSensitiveCallDomainProof {
  bool valid = false;
  int call_sites = 0;
  ExactIntegralDomain domain;
  // Every arithmetic step used to derive the call argument is exact in the
  // conservative eight-significant-digit MK-61 model.
  bool decimal_derivation_exact = false;
  // Relevant only when `domain` contains numeric zero. This distinguishes the
  // canonical +0 produced by proved arithmetic from an entered/refined -0.
  bool zero_canonical_positive = false;
};

std::map<std::string, FlowSensitiveCallDomainProof>
prove_flow_sensitive_call_domains(const V2Program& program,
                                  const std::set<std::string>& rule_names);

} // namespace mkpro::core
