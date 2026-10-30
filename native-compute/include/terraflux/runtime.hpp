#pragma once

#include <cstdint>
#include <terraflux/scene.hpp>
#include <vector>
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

// Fixed-step component system: integrate Spin and Motion in scene-local space.
// Returns the number of updated nodes (a node with both components counts once).
// Unlike the OBJ demo's hard-coded animation, any serialized scene node may
// opt into these behaviors; nodes without components remain static.
std::size_t update_scene_components(scene::Scene& scene,double fixed_dt);

struct ProfileSample {
  double update_seconds=0;
  double render_seconds=0;
  std::uint32_t draw_calls=0;
};
struct ProfileSummary {
  std::size_t samples=0;
  double mean_update_ms=0,mean_render_ms=0,max_frame_ms=0;
  double mean_draw_calls=0;
};
// CPU-side rolling profiler, no GPU timestamp claims. Keeps bounded memory.
class FrameProfiler {
 public:
  explicit FrameProfiler(std::size_t history=120);
  void record(ProfileSample sample);
  ProfileSummary summary() const;
  void clear();
 private:
  std::vector<ProfileSample> history_;
  std::size_t next_=0,count_=0;
};

} // namespace terraflux::runtime
