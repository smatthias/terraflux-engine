#pragma once

#include <terraflux/assets/material.hpp>
#include <terraflux/assets/obj.hpp>

#include <cstdint>
#include <span>
#include <optional>
#include <functional>
#include <string>
#include <vector>

namespace terraflux::assets {

// Limited but spec-aligned static glTF 2.0 .gltf importer.
// Supports embedded base64 binary or externally resolved .bin data, FLOAT
// POSITION/NORMAL/TEXCOORD_0, 8/16/32-bit indices and TRIANGLES primitives.
// Imports each mesh separately, retaining its primitive material ranges;
// nodes can instance any imported mesh with shared GPU buffers per mesh.

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
  std::uint32_t mesh_index = 0;
  math::Mat4 world = math::Mat4::identity();
};
struct GltfModel {
  std::vector<Mesh> meshes;
  MaterialLibrary materials;
  std::map<std::string,ImageRgba8> images; // decoded shared glTF image indices
  std::vector<GltfNode> nodes;
  std::vector<GltfMeshInstance> instances;
};

using GltfBufferResolver = std::function<std::vector<std::uint8_t>(const std::string& uri)>;

GltfModel import_gltf(const std::string& json_text,
                      const GltfBufferResolver& external_buffer = {},
                      const std::vector<std::uint8_t>* embedded_binary = nullptr);
// Strict glTF 2.0 binary container: JSON chunk first and an optional BIN
// chunk second. Resource resolver handles external image/buffer URIs.
GltfModel import_glb(std::span<const std::uint8_t> bytes,
                     const GltfBufferResolver& external_buffer = {});

} // namespace terraflux::assets
