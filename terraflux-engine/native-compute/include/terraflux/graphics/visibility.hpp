#pragma once

#include <terraflux/graphics/mesh_upload.hpp>
#include <terraflux/graphics/render_device.hpp>
#include <terraflux/math.hpp>

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

namespace terraflux::graphics {

// Conservative local-space bounding volume, reused for all instances of a mesh.
struct BoundingSphere {
  math::Vec3 center;
  double radius=0.0;
};
BoundingSphere mesh_bounds(const assets::Mesh& mesh);
BoundingSphere world_bounds(BoundingSphere local,const math::Mat4& model);

// OpenGL-style clip depth [-w,+w]. All six planes point inwards, normalized.
class Frustum {
 public:
  static Frustum from_view_projection(const math::Mat4& view_projection);
  bool intersects(BoundingSphere world) const;
  // Conservative inclusive AABB test, with scale-aware float rounding margin.
  bool intersects_aabb(math::Vec3 lo,math::Vec3 hi) const;

 private:
  struct Plane { math::Vec3 normal; double distance=0.0; };
  Plane planes_[6]{};
};

// Opaque draw commands only. Sorts by shader, geometry and texture to group
// state changes, but does not merge draw calls or change translucent ordering.
// Object UBOs are refreshed immediately before each draw, preserving distinct
// per-material tints even when one scene node reuses the same uniform buffer.
struct PreparedDraw {
  DrawIndexed command;
  BufferHandle object_uniform;
  ObjectUniform material;
};
struct QueueStats {
  std::size_t submitted=0;
  std::size_t state_groups=0;
  std::size_t instances=0;
};
class OpaqueRenderQueue {
 public:
  void add(PreparedDraw item);
  QueueStats submit(RenderDevice& device);
  // All prepared commands must have Camera UBO slot 0 and Object slot 2,
  // and no other storage resources. Hardware instancing uploads the existing
  // 144-byte ObjectUniform records to binding 4 and merges equal opaque
  // mesh/material/index ranges into a single indexed instance draw.
  QueueStats submit_instanced(RenderDevice& device, PipelineHandle instance_pipeline,
                              BufferHandle instance_buffer,std::uint32_t capacity);
  class InstanceBufferRing;
  QueueStats submit_instanced_streamed(RenderDevice& device,
      PipelineHandle instance_pipeline,InstanceBufferRing& ring,
      std::uint32_t max_instances_per_draw=256);
  std::size_t size() const {return pending_.size();}

 private:
  std::vector<PreparedDraw> pending_;
};

// Three frame slots with per-draw-group storage buffers. Each group receives
// a distinct allocation in its frame, preventing earlier draws in that frame
// from reading overwritten instance records. A reused frame slot is orphaned
// (rather than waited on) before upload. Buffers grow only when necessary.
class OpaqueRenderQueue::InstanceBufferRing {
 public:
  explicit InstanceBufferRing(RenderDevice& device):device_(device){}
  ~InstanceBufferRing() noexcept;
  InstanceBufferRing(const InstanceBufferRing&)=delete;
  InstanceBufferRing& operator=(const InstanceBufferRing&)=delete;
  void begin_frame();
  BufferHandle acquire(std::uint32_t instance_count);
  std::size_t live_buffers() const;
  std::size_t orphan_count() const {return orphans_;}

 private:
  struct Slot {BufferHandle buffer; std::size_t bytes;};
  RenderDevice& device_;
  std::array<std::vector<Slot>,3> frames_;
  std::size_t current_frame_=0,used_=0,orphans_=0;
  bool started_=false;
};

// Draw alpha-blended primitives back-to-front with depth testing enabled
// and depth writes disabled at the pipeline level. Intentionally never
// batches/instances transparent geometry: correct blending needs ordering.
struct TransparentDraw {
  PreparedDraw draw;
  double distance_squared=0;
};
class TransparentRenderQueue {
 public:
  void add(TransparentDraw item);
  void sort_back_to_front();
  std::size_t submit(RenderDevice& device);
  const std::vector<TransparentDraw>& entries() const {return draws_;}
 private:
  std::vector<TransparentDraw> draws_;
};

} // namespace terraflux::graphics
