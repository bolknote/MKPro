#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace mkpro::core::detail {

struct JointLayoutWork;

struct JointLayoutWorkCounts {
  std::size_t builds = 0;
  std::size_t hits = 0;
  std::size_t waits = 0;
};

struct JointLayoutWorkStats {
  JointLayoutWorkCounts variants, literals, closures, order_proofs;
  JointLayoutWorkCounts frontiers;
};

std::shared_ptr<JointLayoutWork> make_joint_layout_work();
JointLayoutWorkStats joint_layout_work_stats(const JointLayoutWork& work);

inline thread_local std::shared_ptr<JointLayoutWork> active_joint_layout_work;

class JointLayoutWorkScope {
 public:
  explicit JointLayoutWorkScope(std::shared_ptr<JointLayoutWork> work)
      : previous_(std::exchange(active_joint_layout_work, std::move(work))) {}
  ~JointLayoutWorkScope() { active_joint_layout_work = std::move(previous_); }

  JointLayoutWorkScope(const JointLayoutWorkScope&) = delete;
  JointLayoutWorkScope& operator=(const JointLayoutWorkScope&) = delete;

 private:
  std::shared_ptr<JointLayoutWork> previous_;
};

} // namespace mkpro::core::detail
