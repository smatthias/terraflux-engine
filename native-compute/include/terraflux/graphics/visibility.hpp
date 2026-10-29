#pragma once

#include <terraflux/graphics/mesh_upload.hpp>
#include <terraflux/graphics/render_device.hpp>
#include <terraflux/math.hpp>

#include <cstddef>
#include <cstdint>
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
};
class OpaqueRenderQueue {
 public:
  void add(PreparedDraw item);
  QueueStats submit(RenderDevice& device);
  std::size_t size() const {return pending_.size();}

 private:
  std::vector<PreparedDraw> pending_;
};

} // namespace terraflux::graphics
