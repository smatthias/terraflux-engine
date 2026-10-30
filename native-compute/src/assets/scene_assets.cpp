#include <terraflux/assets/scene_assets.hpp>
#include <fstream>
#include <stdexcept>

namespace terraflux::assets {
SceneObjAssets load_scene_obj_assets(const scene::Scene& scene,
                                     const std::filesystem::path& scene_file) {
  SceneObjAssets result;
  const auto directory=std::filesystem::canonical(
      std::filesystem::absolute(scene_file).parent_path());
  std::map<std::filesystem::path,std::uint32_t> loaded;
  for(const auto& [name,reference]:scene.mesh_assets()) {
    const auto source=std::filesystem::canonical(directory/reference);
    const auto relative=source.lexically_relative(directory);
    if(relative.empty() || relative.has_root_path())
      throw std::invalid_argument("cannot resolve scene asset beneath scene directory");
    for(const auto& part:relative)
      if(part=="..")throw std::invalid_argument("scene asset symlink escapes scene directory");
    if(const auto it=loaded.find(source);it!=loaded.end()) {
      result.mesh_indices.emplace(name,it->second);
      continue;
    }
    if(!std::filesystem::is_regular_file(source) ||
       std::filesystem::file_size(source)>64'000'000)
      throw std::invalid_argument("scene OBJ asset must be a file no larger than 64MB");
    std::ifstream input(source);
    if(!input)throw std::runtime_error("cannot open scene OBJ: "+source.string());
    auto mesh=import_obj(input);
    if(mesh.vertices.empty() || mesh.indices.empty())
      throw std::invalid_argument("scene OBJ asset contains no drawable geometry");
    const auto index=static_cast<std::uint32_t>(result.meshes.size());
    loaded.emplace(source,index);
    result.mesh_indices.emplace(name,index);
    result.sources.push_back(source);
    result.meshes.push_back(std::move(mesh));
  }
  return result;
}
} // namespace terraflux::assets
