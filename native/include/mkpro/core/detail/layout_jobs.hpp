#pragma once

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <string_view>
#include <thread>
#include <vector>

namespace mkpro::core::detail {

inline constexpr unsigned kMaximumLayoutWorkers = 12;
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

// Only call with independent jobs. Store outputs by index, not completion
// order; the owner commits them in the original serial order. A nested batch
// borrows available helpers without waiting for a slot held by its parent.
template <class Evaluate>
void evaluate_layout_jobs(std::size_t count, Evaluate evaluate,
                          unsigned workers = layout_worker_count()) {
  const unsigned ceiling = std::max(1U, std::min(workers, kMaximumLayoutWorkers)) - 1U;
  if (count < 2 || active_layout_helpers.load(std::memory_order_relaxed) >= ceiling) {
    for (std::size_t index = 0; index < count; ++index) evaluate(index);
    return;
  }

  std::atomic<std::size_t> next{0};
  std::vector<std::exception_ptr> failures(count);
  const auto run = [&]() noexcept {
    for (;;) {
      const auto index = next.fetch_add(1, std::memory_order_relaxed);
      if (index >= count) return;
      try {
        evaluate(index);
      } catch (...) {
        failures[index] = std::current_exception();
      }
    }
  };
  std::vector<std::thread> helpers;
  const auto wanted = std::min(count - 1, static_cast<std::size_t>(ceiling));
  helpers.reserve(wanted);
  for (std::size_t i = 0; i < wanted; ++i) {
    unsigned in_use = active_layout_helpers.load(std::memory_order_relaxed);
    while (in_use < ceiling && !active_layout_helpers.compare_exchange_weak(
               in_use, in_use + 1, std::memory_order_relaxed)) {}
    if (in_use >= ceiling) break;
    try {
      helpers.emplace_back([&]() {
        run();
        active_layout_helpers.fetch_sub(1, std::memory_order_relaxed);
      });
    } catch (...) {
      // A host unable to start another thread can still do the same work
      // with the caller and any already-started helpers.
      active_layout_helpers.fetch_sub(1, std::memory_order_relaxed);
      break;
    }
  }
  run();
  for (auto& thread : helpers) thread.join();
  for (const auto& failure : failures)
    if (failure) std::rethrow_exception(failure);
}

} // namespace mkpro::core::detail
