#pragma once

#include "mkpro/core/detail/joint_layout_work.hpp"
#include "mkpro/core/detail/natural_target_flow_proof_scope.hpp"
#include "mkpro/core/detail/post_layout_proof_cache.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace mkpro::core::detail {

inline constexpr unsigned kMaximumLayoutWorkers = 16;
inline std::atomic<unsigned> active_layout_helpers{0};

inline unsigned layout_worker_count() {
  unsigned workers = std::max(1U, std::thread::hardware_concurrency());
  if (const char* value = std::getenv("MKPRO_NATIVE_LAYOUT_JOBS")) {
    const std::string_view requested(value);
    unsigned parsed = 0;
    const auto converted = std::from_chars(requested.data(), requested.data() + requested.size(), parsed);
    if (converted.ec == std::errc{} && converted.ptr == requested.data() + requested.size() && parsed)
      workers = parsed;
  }
  return std::min(workers, kMaximumLayoutWorkers);
}

class LayoutExecutor;
inline thread_local LayoutExecutor* active_layout_executor = nullptr;

class LayoutExecutorScope {
 public:
  explicit LayoutExecutorScope(LayoutExecutor& executor)
      : previous_(std::exchange(active_layout_executor, &executor)) {}
  ~LayoutExecutorScope() { active_layout_executor = previous_; }

  LayoutExecutorScope(const LayoutExecutorScope&) = delete;
  LayoutExecutorScope& operator=(const LayoutExecutorScope&) = delete;

 private:
  LayoutExecutor* previous_;
};

// The outer batch owns a worker group. Nested batches publish independent
// work to the same group instead of becoming permanently serial if the
// helpers happen to be busy at entry. Each waiting caller can run work.
class LayoutExecutor {
  struct Batch {
    const std::function<void(std::size_t)> evaluate;
    const std::shared_ptr<NaturalTargetFlowProofCache> flow = active_flow_proof_cache;
    const std::shared_ptr<PostLayoutProofCache> post_layout = active_post_layout_proof_cache;
    const std::shared_ptr<JointLayoutWork> joint = active_joint_layout_work;
    std::vector<std::exception_ptr> failures;
    std::size_t remaining;
    unsigned workers;
    Batch* parent = nullptr;
    std::size_t next = 0;
    unsigned running = 0;

    bool available() const { return next < failures.size() && running < workers; }
  };

  struct Task {
    Batch* batch = nullptr;
    std::size_t index = 0;
    explicit operator bool() const { return batch != nullptr; }
  };

 public:
  explicit LayoutExecutor(unsigned workers)
      : ceiling_(std::clamp(workers, 1U, kMaximumLayoutWorkers) - 1U) {
    helpers_.reserve(ceiling_);
  }
  ~LayoutExecutor() {
    {
      const std::lock_guard lock(mutex_);
      done_ = true;
    }
    ready_.notify_all();
    for (auto& helper : helpers_) helper.join();
  }

  void run(std::size_t count, std::function<void(std::size_t)> evaluate, unsigned workers) {
    Batch batch{.evaluate = std::move(evaluate), .failures = std::vector<std::exception_ptr>(count),
        .remaining = count, .workers = workers, .parent = executing_owner_ == this ? executing_ : nullptr};
    {
      const std::lock_guard lock(mutex_);
      pending_.push_back(&batch);
      start_helpers(count);
    }
    ready_.notify_all();
    for (;;) {
      Task task;
      {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [&] {
          return batch.remaining == 0 || bool(task = claim_within(batch));
        });
        if (batch.remaining == 0) break;
      }
      execute(task);
    }
    for (const auto& failure : batch.failures)
      if (failure) std::rethrow_exception(failure);
  }

 private:
  // All helpers are borrowed under one process-wide cap, also when two
  // unrelated library callers compile concurrently.
  void start_helpers(std::size_t count) {
    const auto wanted = std::min(static_cast<std::size_t>(ceiling_), helpers_.size() + count - 1U);
    while (helpers_.size() < wanted) {
      unsigned in_use = active_layout_helpers.load(std::memory_order_relaxed);
      while (in_use < ceiling_ && !active_layout_helpers.compare_exchange_weak(
                 in_use, in_use + 1U, std::memory_order_relaxed)) {}
      if (in_use >= ceiling_) return;
      try {
        helpers_.emplace_back([this] {
          const LayoutExecutorScope scope(*this);
          helper_loop();
          active_layout_helpers.fetch_sub(1U, std::memory_order_relaxed);
        });
      } catch (...) {
        active_layout_helpers.fetch_sub(1U, std::memory_order_relaxed);
        return; // running callers still drain the queue
      }
    }
  }

  Task claim(Batch& batch) {
    if (!batch.available()) return {};
    const auto index = batch.next++;
    ++batch.running;
    if (batch.next == batch.failures.size())
      pending_.erase(std::find(pending_.begin(), pending_.end(), &batch));
    return {&batch, index};
  }

  Task claim() {
    const auto found = std::find_if(pending_.rbegin(), pending_.rend(),
        [](const Batch* batch) { return batch->available(); });
    return found == pending_.rend() ? Task{} : claim(**found);
  }

  Task claim_within(Batch& root) {
    if (root.available()) return claim(root);
    // A descendant's enclosing task keeps every ancestor alive until the
    // descendant completes. Never help an unrelated batch while waiting:
    // a sibling could in turn wait on the task currently on this stack.
    for (auto it = pending_.rbegin(); it != pending_.rend(); ++it) {
      auto* candidate = *it;
      if (!candidate->available()) continue;
      for (auto* parent = candidate->parent; parent; parent = parent->parent)
        if (parent == &root) return claim(*candidate);
    }
    return {};
  }

  void execute(Task task) {
    auto& batch = *task.batch;
    {
      const FlowProofCacheScope inherit_flow(batch.flow);
      const PostLayoutProofCacheScope inherit_post_layout(batch.post_layout);
      const JointLayoutWorkScope inherit_joint(batch.joint);
      auto* previous = std::exchange(executing_, &batch);
      auto* previous_owner = std::exchange(executing_owner_, this);
      try {
        batch.evaluate(task.index);
      } catch (...) {
        batch.failures[task.index] = std::current_exception();
      }
      executing_ = previous;
      executing_owner_ = previous_owner;
    }
    {
      const std::lock_guard lock(mutex_);
      --batch.running;
      --batch.remaining;
    }
    ready_.notify_all();
  }

  void helper_loop() {
    for (;;) {
      Task task;
      {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [&] { return done_ || bool(task = claim()); });
        if (done_) return;
      }
      execute(task);
    }
  }

  const unsigned ceiling_;
  // Each thread can execute only one active executor's task at a time.
  // Separate executor scopes (e.g. tests) must not inherit stale parents.
  inline static thread_local Batch* executing_ = nullptr;
  inline static thread_local LayoutExecutor* executing_owner_ = nullptr;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::vector<Batch*> pending_;
  std::vector<std::thread> helpers_;
  bool done_ = false;
};

// Store results by index, not completion order. The owner publishes them in
// serial discovery order, using the same limits and tie rules with any job count.
template <class Evaluate>
void evaluate_layout_jobs(std::size_t count, Evaluate evaluate,
                          unsigned workers = layout_worker_count()) {
  workers = std::clamp(workers, 1U, kMaximumLayoutWorkers);
  if (count < 2 || workers == 1U) {
    for (std::size_t index = 0; index < count; ++index) evaluate(index);
    return;
  }
  if (active_layout_executor) {
    active_layout_executor->run(count, std::ref(evaluate), workers);
    return;
  }
  LayoutExecutor executor(workers);
  const LayoutExecutorScope scope(executor);
  executor.run(count, std::ref(evaluate), workers);
}

} // namespace mkpro::core::detail
