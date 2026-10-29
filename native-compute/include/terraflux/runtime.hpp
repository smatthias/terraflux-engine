#pragma once

#include <cstdint>
#include <functional>

namespace terraflux::runtime {

struct FrameReport {
  std::uint32_t updates=0;
  double interpolation_alpha=0.0; // [0,1) towards the next fixed update
  double discarded_seconds=0.0;   // cap elapsed time or spiral-of-death backlog
  double simulated_seconds=0.0;
};

// Deterministic update scheduler. Consume elapsed wall-clock time, invoke
// fixed-duration updates, leave fractional remainder for render interpolation.
// Explicit pause/reset can be implemented by callers not calling advance().
class FixedStepScheduler {
 public:
  explicit FixedStepScheduler(double fixed_step_seconds=1.0/60.0,
                              double max_frame_seconds=0.25,
                              std::uint32_t max_updates_per_frame=8);
  FrameReport advance(double elapsed_seconds,
                      const std::function<void(double)>& update);
  double simulated_seconds() const { return simulated_seconds_; }
  void reset();

 private:
  double fixed_step_seconds_=0.0;
  double max_frame_seconds_=0.0;
  std::uint32_t max_updates_per_frame_=0;
  double accumulator_=0.0;
  double simulated_seconds_=0.0;
};

} // namespace terraflux::runtime
