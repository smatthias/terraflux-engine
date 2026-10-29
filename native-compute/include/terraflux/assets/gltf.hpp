#pragma once

#include <terraflux/assets/material.hpp>
#include <terraflux/assets/obj.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace terraflux::assets {

// Limited but spec-aligned static glTF 2.0 .gltf importer.
// Supports embedded base64 binary or externally resolved .bin data, FLOAT
// POSITION/NORMAL/TEXCOORD_0, 8/16/32-bit indices and TRIANGLES primitives.
// Flattens primitives of mesh[0] to an OBJ-compatible Mesh with named material
// draw ranges. Does not import scene nodes, skins, morphs, animations, images,
// GLB, compressed/sparse accessors, or texture samplers.
struct GltfModel {
  Mesh mesh;
  MaterialLibrary materials;
};

using GltfBufferResolver = std::function<std::vector<std::uint8_t>(const std::string& uri)>;

GltfModel import_gltf(const std::string& json_text,
                      const GltfBufferResolver& external_buffer = {});

} // namespace terraflux::assets
