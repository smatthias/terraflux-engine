#include <terraflux/assets/scene_assets.hpp>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace terraflux::assets {
namespace {
std::filesystem::path beneath(const std::filesystem::path& directory,const std::string& reference) {
  const std::filesystem::path relative(reference);
  if(reference.empty() || reference.find('\0')!=std::string::npos ||
     reference.find('\\')!=std::string::npos || reference.find(':')!=std::string::npos ||
     relative.has_root_path())throw std::invalid_argument("asset resource must be a relative path");
  for(const auto& part:relative)
    if(part=="..")throw std::invalid_argument("asset resource parent traversal forbidden");
  const auto source=std::filesystem::canonical(directory/relative);
  const auto within=source.lexically_relative(directory);
  if(within.empty() || within.has_root_path())
    throw std::invalid_argument("cannot resolve asset beneath its directory");
  for(const auto& part:within)
    if(part=="..")throw std::invalid_argument("asset resource symlink escapes directory");
  if(!std::filesystem::is_regular_file(source) || std::filesystem::file_size(source)>64'000'000)
    throw std::invalid_argument("asset resource must be a file no larger than 64MB");
  return source;
}
std::string read(const std::filesystem::path& source) {
  std::ifstream input(source,std::ios::binary);
  if(!input)throw std::runtime_error("cannot read scene asset: "+source.string());
  return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
}
SceneModelAssets load_scene_assets(const scene::Scene& scene,const std::filesystem::path& scene_file) {
  SceneModelAssets result;
  const auto directory=std::filesystem::canonical(std::filesystem::absolute(scene_file).parent_path());
  std::map<std::filesystem::path,std::uint32_t> loaded;
  for(const auto& [name,reference]:scene.mesh_assets()) {
    const auto source=beneath(directory,reference);
    if(const auto it=loaded.find(source);it!=loaded.end()) {
      result.model_indices.emplace(name,it->second);
      result.mesh_indices.emplace(name,result.model_instances.at(it->second).front().mesh_index);
      continue;
    }
    const auto model_index=static_cast<std::uint32_t>(result.model_instances.size());
    const auto mesh_base=static_cast<std::uint32_t>(result.meshes.size());
    std::vector<SceneAssetInstance> instances;
    const bool is_gltf=std::filesystem::path(reference).extension()!=".obj";
    if(!is_gltf) {
      std::ifstream input(source);
      auto mesh=import_obj(input);
      if(mesh.vertices.empty() || mesh.indices.empty())
        throw std::invalid_argument("scene OBJ asset contains no drawable geometry");
      result.meshes.push_back(std::move(mesh));
      instances.push_back({mesh_base,math::Mat4::identity()});
    }else{
      result.has_gltf=true;
      const auto resolver=[&](const std::string& uri) {
        const auto contents=read(beneath(source.parent_path(),uri));
        return std::vector<std::uint8_t>(contents.begin(),contents.end());
      };
      const auto contents=read(source);
      const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(contents.data()),contents.size());
      auto model=std::filesystem::path(reference).extension()==".glb"?
          import_glb(bytes,resolver):import_gltf(contents,resolver);
      if(model.meshes.empty() || model.instances.empty() || model.instances.size()>1024)
        throw std::invalid_argument("scene model must have meshes and 1..1024 active instances");
      const auto prefix="scene-model/"+std::to_string(model_index)+"/";
      for(auto& [key,image]:model.images)result.images.emplace(prefix+key,std::move(image));
      for(auto& [key,material]:model.materials) {
        for(auto* map:{&material.albedo_map,&material.metallic_roughness_map,&material.normal_map,
                       &material.occlusion_map,&material.emissive_map})
          if(!map->empty())*map=prefix+*map;
        result.material_folders.emplace(prefix+key,source.parent_path());
        result.materials.emplace(prefix+key,std::move(material));
      }
      const auto fallback=prefix+"default";
      result.materials.emplace(fallback,Material{});
      result.material_folders.emplace(fallback,source.parent_path());
      for(auto& mesh:model.meshes) {
        for(auto& section:mesh.sections)
          section.material_name=section.material_name.empty()?fallback:prefix+section.material_name;
        result.meshes.push_back(std::move(mesh));
      }
      for(const auto& instance:model.instances)
        instances.push_back({mesh_base+instance.mesh_index,instance.world});
    }
    if(result.meshes.size()>512)throw std::length_error("scene supports at most 512 mesh definitions");
    while(result.sources.size()<result.meshes.size()) {
      result.sources.push_back(source);
      result.gltf_meshes.push_back(is_gltf);
    }
    loaded.emplace(source,model_index);
    result.model_indices.emplace(name,model_index);
    result.mesh_indices.emplace(name,instances.front().mesh_index);
    result.model_instances.push_back(std::move(instances));
  }
  return result;
}
SceneObjAssets load_scene_obj_assets(const scene::Scene& scene,const std::filesystem::path& scene_file) {
  for(const auto& [name,path]:scene.mesh_assets())
    if(std::filesystem::path(path).extension()!=".obj")
      throw std::invalid_argument("OBJ-only scene loader cannot load glTF/GLB");
  return load_scene_assets(scene,scene_file);
}
} // namespace terraflux::assets
