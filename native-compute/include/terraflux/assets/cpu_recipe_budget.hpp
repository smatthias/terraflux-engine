#pragma once
#include <terraflux/assets/material.hpp>
#include <terraflux/assets/obj.hpp>
#include <limits>
#include <stdexcept>
#include <span>

namespace terraflux::assets {
inline std::uint64_t checked_recipe_add(std::uint64_t left,std::uint64_t right) {
  if(right>std::numeric_limits<std::uint64_t>::max()-left)
    throw std::overflow_error("CPU recipe payload byte overflow");
  return left+right;
}
inline std::uint64_t checked_recipe_multiply(std::uint64_t count,std::uint64_t stride) {
  if(stride && count>std::numeric_limits<std::uint64_t>::max()/stride)
    throw std::overflow_error("CPU recipe payload byte overflow");
  return count*stride;
}
struct CpuRecipeBytes {
  std::uint64_t vertex_bytes=0,index_bytes=0,image_bytes=0;
  std::uint64_t total() const {return checked_recipe_add(checked_recipe_add(vertex_bytes,index_bytes),image_bytes);}
  void include(const CpuRecipeBytes& value) {
    const CpuRecipeBytes combined{checked_recipe_add(vertex_bytes,value.vertex_bytes),
        checked_recipe_add(index_bytes,value.index_bytes),checked_recipe_add(image_bytes,value.image_bytes)};
    (void)combined.total();*this=combined;
  }
};
inline CpuRecipeBytes mesh_recipe_bytes(const Mesh& mesh) {
  return {checked_recipe_multiply(mesh.vertices.size(),sizeof(MeshVertex)),
          checked_recipe_multiply(mesh.indices.size(),sizeof(std::uint32_t)),0};
}
// Logical retained payload, not vector capacity, allocator overhead or peak RSS.
inline CpuRecipeBytes cpu_recipe_bytes(std::span<const Mesh> meshes,
                                       const std::map<std::string,ImageRgba8>& images={}) {
  CpuRecipeBytes result;
  for(const auto& mesh:meshes)result.include(mesh_recipe_bytes(mesh));
  for(const auto& [name,image]:images)result.include({0,0,image.pixels.size()});
  return result;
}
inline void check_cpu_recipe_budget(CpuRecipeBytes bytes,std::uint64_t limit) {
  if(bytes.total()>limit)throw std::length_error("CPU recipe payload budget exceeded");
}
} // namespace terraflux::assets
