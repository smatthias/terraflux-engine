#pragma once

#include <terraflux/scene.hpp>
#include <string>

namespace terraflux::scene {

// Versioned, portable JSON scene graph containing node names, parent IDs,
// local TRS values and primary-asset mesh-instance flags. No file IO here;
// caller controls storage, path validation and asset resolution.
std::string serialize_scene(const Scene& scene);
Scene deserialize_scene(const std::string& json_text);

}  // namespace terraflux::scene
