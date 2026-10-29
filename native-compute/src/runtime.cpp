#include <terraflux/runtime.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::runtime {

FixedStepScheduler::FixedStepScheduler(double fixed,double max_frame,
                                       std::uint32_t max_updates)
    :fixed_step_seconds_(fixed),max_frame_seconds_(max_frame),
     max_updates_per_frame_(max_updates) {
  if(!std::isfinite(fixed) || !std::isfinite(max_frame) || fixed<=0 ||
     max_frame<fixed || max_updates==0 || max_updates>10000)
    throw std::invalid_argument("invalid fixed-update scheduler configuration");
}

FrameReport FixedStepScheduler::advance(double elapsed,
                                        const std::function<void(double)>& update) {
  if(!std::isfinite(elapsed) || elapsed<0)
    throw std::invalid_argument("frame delta must be finite and nonnegative");
  if(!update)throw std::invalid_argument("fixed update callback is missing");
  FrameReport report;
  const double accepted=std::min(elapsed,max_frame_seconds_);
  report.discarded_seconds=elapsed-accepted;
  accumulator_+=accepted;
  // Epsilon avoids losing a tick at common frame-rate floating boundaries.
  const double epsilon=fixed_step_seconds_*1e-9;
  while(accumulator_+epsilon>=fixed_step_seconds_ &&
        report.updates<max_updates_per_frame_) {
    update(fixed_step_seconds_);
    accumulator_-=fixed_step_seconds_;
    if(accumulator_<0)accumulator_=0;
    simulated_seconds_+=fixed_step_seconds_;
    ++report.updates;
  }
  if(accumulator_+epsilon>=fixed_step_seconds_) {
    const double old=accumulator_;
    accumulator_=std::fmod(accumulator_,fixed_step_seconds_);
    if(accumulator_<epsilon)accumulator_=0;
    report.discarded_seconds+=old-accumulator_;
  }
  report.interpolation_alpha=std::clamp(accumulator_/fixed_step_seconds_,0.0,1.0);
  report.simulated_seconds=simulated_seconds_;
  return report;
}

void FixedStepScheduler::reset() {
  accumulator_=0;
  simulated_seconds_=0;
}

} // namespace terraflux::runtime
