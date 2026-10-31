#include <terraflux/terrain_water.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace terraflux::terrain {
namespace {
constexpr double kMaxGradient = 19.08;  // tan(87 degrees)
}

WaterMesh build_water_mesh(const TerrainMesh& terrain, double level_meters) {
  if (!std::isfinite(level_meters)) throw std::invalid_argument("water level must be finite");
  if (terrain.indices.size() % 3 != 0) throw std::invalid_argument("terrain mesh has a partial triangle");

  WaterMesh water;
  water.level_meters = level_meters;
  std::unordered_map<std::uint32_t, std::uint32_t> local_index;
  const auto emit = [&](std::uint32_t source) -> std::uint32_t {
    const auto found = local_index.find(source);
    if (found != local_index.end()) return found->second;
    const auto& v = terrain.vertices[source];
    if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z))
      throw std::invalid_argument("terrain vertex position must be finite");
    WaterVertex out;
    out.position = {v.position.x, level_meters, v.position.z};
    out.bed_height_meters = v.position.y;
    out.source_vertex = source;
    const double ny = v.normal.y;
    if (std::isfinite(ny) && ny > 1e-9 && std::isfinite(v.normal.x) && std::isfinite(v.normal.z)) {
      out.bed_gradient = {-v.normal.x / ny, -v.normal.z / ny};
      const double magnitude = out.bed_gradient.length();
      if (magnitude > kMaxGradient) out.bed_gradient = out.bed_gradient * (kMaxGradient / magnitude);
    }
    const double depth = level_meters - v.position.y;
    if (depth > 0.0) {
      ++water.submerged_vertices;
      water.max_depth_meters = std::max(water.max_depth_meters, depth);
    }
    const auto index = static_cast<std::uint32_t>(water.vertices.size());
    water.vertices.push_back(out);
    local_index.emplace(source, index);
    return index;
  };

  for (std::size_t t = 0; t < terrain.indices.size(); t += 3) {
    const std::uint32_t corners[3] = {terrain.indices[t], terrain.indices[t + 1], terrain.indices[t + 2]};
    bool submerged = false;
    for (const auto corner : corners) {
      if (corner >= terrain.vertices.size())
        throw std::invalid_argument("terrain mesh contains an out-of-range triangle index");
      submerged = submerged || terrain.vertices[corner].position.y < level_meters;
    }
    if (!submerged) continue;
    for (const auto corner : corners) water.indices.push_back(emit(corner));
  }
  return water;
}

}  // namespace terraflux::terrain
