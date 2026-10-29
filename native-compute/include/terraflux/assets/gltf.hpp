#pragma once

#include <terraflux/assets/material.hpp>
#include <terraflux/assets/obj.hpp>

#include <cstdint>
#include <optional>
#include <functional>
#include <string>
#include <vector>

namespace terraflux::assets {

// Limited but spec-aligned static glTF 2.0 .gltf importer.
// Supports embedded base64 binary or externally resolved .bin data, FLOAT
// POSITION/NORMAL/TEXCOORD_0, 8/16/32-bit indices and TRIANGLES primitives.
// Combines primitives of mesh[0] into one shared geometry buffer and preserves
// scene nodes and their world matrices as mesh instances. Current subset has
// one reusable mesh, but can instantiate it many times in the selected scene.
struct GltfNode {
  std::string name;
  math::Mat4 local = math::Mat4::identity();
  math::Mat4 world = math::Mat4::identity();
  std::optional<std::uint32_t> parent;
  std::optional<std::uint32_t> mesh_index;
  std::vector<std::uint32_t> children;
  bool in_scene = false;
};
struct GltfMeshInstance {
  std::uint32_t node_index = 0;
  math::Mat4 world = math::Mat4::identity();
};
struct GltfModel {
  Mesh mesh;
  MaterialLibrary materials;
  std::vector<GltfNode> nodes;
  std::vector<GltfMeshInstance> instances;
};

using GltfBufferResolver = std::function<std::vector<std::uint8_t>(const std::string& uri)>;

GltfModel import_gltf(const std::string& json_text,
                      const GltfBufferResolver& external_buffer = {});

} // namespace terraflux::assets
