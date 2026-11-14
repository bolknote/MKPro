#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace mkpro::core::detail {

struct PostLayoutProofCache;

struct PostLayoutProofCacheStats {
  std::size_t cfg_builds = 0;
  std::size_t cfg_hits = 0;
  std::size_t selector_builds = 0;
  std::size_t selector_hits = 0;
  std::size_t shared_in_flight = 0;
};

std::shared_ptr<PostLayoutProofCache> make_post_layout_proof_cache();
PostLayoutProofCacheStats post_layout_proof_cache_stats(const PostLayoutProofCache& cache);

inline thread_local std::shared_ptr<PostLayoutProofCache> active_post_layout_proof_cache;

// One compilation owns the cache; its layout workers inherit the same
// bounded snapshots. Nested compilations reuse the owner.
class PostLayoutProofCacheScope {
 public:
  explicit PostLayoutProofCacheScope(std::shared_ptr<PostLayoutProofCache> cache)
      : previous_(std::exchange(active_post_layout_proof_cache, std::move(cache))) {}
  ~PostLayoutProofCacheScope() { active_post_layout_proof_cache = std::move(previous_); }

  PostLayoutProofCacheScope(const PostLayoutProofCacheScope&) = delete;
  PostLayoutProofCacheScope& operator=(const PostLayoutProofCacheScope&) = delete;

 private:
  std::shared_ptr<PostLayoutProofCache> previous_;
};

} // namespace mkpro::core::detail
