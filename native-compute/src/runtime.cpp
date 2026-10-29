#include <terraflux/runtime.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <numbers>

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

std::size_t update_scene_components(scene::Scene& graph,double dt) {
  if(!std::isfinite(dt) || !(dt>0) || dt>0.25)
    throw std::invalid_argument("scene component timestep must be (0, 0.25]");
  std::size_t updated=0;
  for(std::size_t i=1;i<graph.size();++i) {
    const auto id=static_cast<scene::NodeId>(i);
    const auto& node=graph.get(id);
    if(!node.angular_velocity)continue;
    const auto velocity=*node.angular_velocity;
    auto pose=node.local;
    constexpr double period=2.0*std::numbers::pi;
    pose.rotation_radians.x=std::remainder(pose.rotation_radians.x+velocity.x*dt,period);
    pose.rotation_radians.y=std::remainder(pose.rotation_radians.y+velocity.y*dt,period);
    pose.rotation_radians.z=std::remainder(pose.rotation_radians.z+velocity.z*dt,period);
    graph.set_transform(id,pose);
    ++updated;
  }
  return updated;
}

FrameProfiler::FrameProfiler(std::size_t history):history_(history) {
  if(history==0 || history>10000)
    throw std::invalid_argument("profiler history capacity must be 1..10000");
}
void FrameProfiler::record(ProfileSample sample){
  if(!std::isfinite(sample.update_seconds) ||
     !std::isfinite(sample.render_seconds) ||
     sample.update_seconds<0 || sample.render_seconds<0)
    throw std::invalid_argument("invalid CPU frame profiling sample");
  history_[next_]=sample;
  next_=(next_+1)%history_.size();
  count_=std::min(count_+1,history_.size());
}
ProfileSummary FrameProfiler::summary() const{
  ProfileSummary result;
  result.samples=count_;
  if(!count_)return result;
  double update=0,render=0,draws=0;
  for(std::size_t i=0;i<count_;++i){
    const auto& p=history_[i];
    update+=p.update_seconds;
    render+=p.render_seconds;
    draws+=p.draw_calls;
    result.max_frame_ms=std::max(result.max_frame_ms,
                                 (p.update_seconds+p.render_seconds)*1000);
  }
  result.mean_update_ms=1000*update/count_;
  result.mean_render_ms=1000*render/count_;
  result.mean_draw_calls=draws/count_;
  return result;
}
void FrameProfiler::clear(){count_=next_=0;}

} // namespace terraflux::runtime
