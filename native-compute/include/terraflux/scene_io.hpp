#pragma once

#include <terraflux/scene.hpp>
#include <string>

namespace terraflux::scene {

// Versioned, portable JSON scene graph containing node names, parent IDs,
// local TRS values, components and named OBJ/glTF/GLB asset references (version 2).
// Legacy primary-asset flags remain version 1. No file IO here;
// caller controls storage, path validation and asset resolution.
std::string serialize_scene(const Scene& scene);
Scene deserialize_scene(const std::string& json_text);

}  // namespace terraflux::scene
