#pragma once
#include <terraflux/assets/gltf.hpp>
#include <terraflux/assets/cpu_recipe_budget.hpp>
#include <terraflux/scene.hpp>
#include <filesystem>
#include <map>

namespace terraflux::assets {
struct SceneAssetInstance {
  std::uint32_t mesh_index=0;
  math::Mat4 local=math::Mat4::identity(); // authored model hierarchy beneath scene node
};
struct SceneModelAssets {
  std::vector<Mesh> meshes;
  std::vector<std::filesystem::path> sources; // one per mesh
  std::vector<bool> gltf_meshes;
  std::map<std::string,std::uint32_t> mesh_indices; // first mesh, legacy OBJ API
  std::map<std::string,std::uint32_t> model_indices;
  std::vector<std::vector<SceneAssetInstance>> model_instances;
  MaterialLibrary materials; // names scoped by canonical model
  std::map<std::string,ImageRgba8> images;
  std::map<std::string,std::filesystem::path> material_folders;
  bool has_gltf=false;
};
using SceneObjAssets=SceneModelAssets;
// Each canonical OBJ/glTF/GLB model is loaded once, preserving active glTF
// hierarchy beneath scene-node transforms. References stay beneath their base
// directory, including symlinks; external glTF data is resolved by the loader.
SceneModelAssets load_scene_assets(const scene::Scene& scene,
                                   const std::filesystem::path& scene_file,
    std::uint64_t max_recipe_bytes=std::numeric_limits<std::uint64_t>::max());
// Compatibility entry point for callers explicitly requiring OBJ-only assets.
SceneObjAssets load_scene_obj_assets(const scene::Scene& scene,
                                     const std::filesystem::path& scene_file,
    std::uint64_t max_recipe_bytes=std::numeric_limits<std::uint64_t>::max());
} // namespace terraflux::assets
