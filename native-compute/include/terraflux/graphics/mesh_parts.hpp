#pragma once

#include <terraflux/graphics/render_device.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/math.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace terraflux::graphics {

// Built geometry (roads, bridges, buildings) as flat-coloured triangle
// parts drawn with the Structure program (the Object geometry and uniform,
// lit with a sky ambient so shaded walls stay light): position and normal
// per vertex, a colour per part. The builders below are what the infrastructure is made
// of: oriented boxes, quads and triangles with outward normals.
// What a part is made of: the Structure program textures each material
// procedurally in world space (no image assets): grain, joints, courses,
// strands, pores, panes and their relief, fading out where it would shimmer.
enum class SurfaceMaterial : std::uint8_t {
  Plain,      // a flat colour
  Asphalt,    // fine aggregate, patched
  Concrete,   // rough, pored, faintly streaked
  Thatch,     // bundles of straw laid down the slope in courses
  Planks,     // horizontal boards with grain, knots and seams (wood that grew as it could)
  Stone,      // coursed rubble masonry with mortar joints
  Tiles,      // overlapping rows of roof tiles
  Glass,      // panes with a sky gradient
  Metal,      // corrugated sheet
  Paint,      // road markings: flat, a little worn
  Brick,      // brick masonry in stretcher bond
  Timber,     // vertical boards (doors)
  Pavement,   // concrete slabs with joints
  Window,     // a single pane: glass without the facade grid
  Count
};
const char* surface_material_name(SurfaceMaterial material);

struct MeshPart {
  std::vector<TerrainGpuVertex> vertices;  // position and normal are used
  std::vector<std::uint32_t> indices;
  math::Vec3 color;
  const char* what = "";        // surface, marking, deck, railing, pier, rubble, wall, roof, window ...
  SurfaceMaterial material = SurfaceMaterial::Plain;
};
// A box with its own orthonormal basis (along, up, across) and half sizes.
void add_box(MeshPart& part, math::Vec3 centre, math::Vec3 along, math::Vec3 up, math::Vec3 across, math::Vec3 half);
// A quad (two triangles) whose normal is its own plane's, turned upward when it has an upward component.
void add_quad(MeshPart& part, math::Vec3 p0, math::Vec3 p1, math::Vec3 p2, math::Vec3 p3);
// A quad or a triangle with the given normal.
void add_quad(MeshPart& part, math::Vec3 p0, math::Vec3 p1, math::Vec3 p2, math::Vec3 p3, math::Vec3 normal);
void add_triangle(MeshPart& part, math::Vec3 p0, math::Vec3 p1, math::Vec3 p2, math::Vec3 normal);
std::size_t part_triangles(std::span<const MeshPart> parts);

// Draws mesh parts with the Structure program (one draw per part, identity model).
class MeshPartRenderer {
 public:
  explicit MeshPartRenderer(RenderDevice& device);
  MeshPartRenderer(const MeshPartRenderer&) = delete;
  MeshPartRenderer& operator=(const MeshPartRenderer&) = delete;
  // Uploads the parts (replacing the previous ones); a part with a partial
  // triangle or an index out of range is rejected.
  void set_parts(std::span<const MeshPart> parts);
  std::size_t draw(BufferHandle camera);  // the number of draws
  std::size_t triangles() const { return triangles_; }
  std::size_t parts() const { return parts_.size(); }

 private:
  struct Part { BufferHandle vb, ib, uniform; std::uint32_t indices = 0; };
  RenderDevice& device_;
  RenderResourceOwner resources_;
  std::unique_ptr<RenderResourceOwner> part_resources_;
  PipelineHandle pipeline_;
  std::vector<Part> parts_;
  std::size_t triangles_ = 0;
};

}  // namespace terraflux::graphics
