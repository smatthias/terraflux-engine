#pragma once
#include <terraflux/assets/obj.hpp>
#include <terraflux/scene.hpp>
#include <filesystem>
#include <map>

namespace terraflux::assets {
struct SceneObjAssets {
  std::vector<Mesh> meshes;
  std::vector<std::filesystem::path> sources;
  std::map<std::string,std::uint32_t> mesh_indices;
};
// Load named OBJ assets once per canonical source path, beneath the scene
// directory. Includes symlink containment checks. Materials are resolved by
// the renderer relative to each entry in sources, not the process directory.
SceneObjAssets load_scene_obj_assets(const scene::Scene& scene,
                                     const std::filesystem::path& scene_file);
} // namespace terraflux::assets
