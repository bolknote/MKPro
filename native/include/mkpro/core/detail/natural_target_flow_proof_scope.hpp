#pragma once

#include <memory>
#include <utility>

namespace mkpro::core {

struct NaturalTargetFlowProofCache;

namespace detail {

// A compilation owns the cache. Its independent layout jobs inherit the
// same owner, not an unbounded process-wide table or a per-worker duplicate.
inline thread_local std::shared_ptr<NaturalTargetFlowProofCache> active_flow_proof_cache;

class FlowProofCacheScope {
 public:
  explicit FlowProofCacheScope(std::shared_ptr<NaturalTargetFlowProofCache> cache)
      : previous_(std::exchange(active_flow_proof_cache, std::move(cache))) {}

  ~FlowProofCacheScope() { active_flow_proof_cache = std::move(previous_); }

  FlowProofCacheScope(const FlowProofCacheScope&) = delete;
  FlowProofCacheScope& operator=(const FlowProofCacheScope&) = delete;

 private:
  std::shared_ptr<NaturalTargetFlowProofCache> previous_;
};

} // namespace detail
} // namespace mkpro::core
