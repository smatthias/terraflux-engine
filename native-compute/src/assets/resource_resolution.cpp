#include <terraflux/assets/resource_resolution.hpp>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace terraflux::assets {
std::filesystem::path resolve_asset_path(const std::filesystem::path& base,
    const std::string& reference,std::uintmax_t max_bytes) {
  const std::filesystem::path relative(reference);
  if(reference.empty() || reference.find('\0')!=std::string::npos ||
     reference.find('\\')!=std::string::npos || reference.find(':')!=std::string::npos ||
     relative.has_root_path())throw std::invalid_argument("asset resource must be a relative path");
  for(const auto& part:relative)
    if(part=="..")throw std::invalid_argument("asset resource parent traversal forbidden");
  const auto directory=std::filesystem::canonical(base.empty()?std::filesystem::path("."):base);
  const auto source=std::filesystem::canonical(directory/relative);
  const auto within=source.lexically_relative(directory);
  if(within.empty() || within.has_root_path())throw std::invalid_argument("asset is outside its directory");
  for(const auto& part:within)
    if(part=="..")throw std::invalid_argument("asset resource symlink escapes directory");
  if(!std::filesystem::is_regular_file(source) || std::filesystem::file_size(source)>max_bytes)
    throw std::invalid_argument("asset resource is not a bounded regular file");
  return source;
}
LocatedMaterials load_obj_materials(Mesh& mesh,const std::filesystem::path& folder,
    const std::string& prefix,bool nonmetallic) {
  LocatedMaterials result;
  for(const auto& reference:mesh.material_libraries) {
    const auto path=resolve_asset_path(folder,reference);
    std::ifstream input(path);
    if(!input)throw std::runtime_error("cannot open material library: "+path.string());
    for(auto [name,material]:parse_mtl(input)) {
      if(nonmetallic)material.metallic_factor=0;
      if(!result.emplace(prefix+name,LocatedMaterial{material,path.parent_path()}).second)
        throw std::invalid_argument("duplicate material in libraries: "+name);
    }
  }
  // Preserve the caller's mesh if a library fails to load.
  for(auto& section:mesh.sections)
    if(!section.material_name.empty())section.material_name=prefix+section.material_name;
  return result;
}
std::string texture_cache_key(const std::filesystem::path& source,
    const TextureSampling& s,bool pbr) {
  const std::string path=source.empty()?(pbr?"__white__":"__checkerboard__"):
      source.lexically_normal().generic_string();
  return path+"@"+std::to_string(static_cast<int>(s.min))+":"+
      std::to_string(static_cast<int>(s.mag))+":"+
      std::to_string(static_cast<int>(s.wrap_s))+":"+
      std::to_string(static_cast<int>(s.wrap_t));
}
ImageRgba8 resolve_texture_image(const std::filesystem::path& source,bool pbr,
    const std::map<std::string,ImageRgba8>& embedded) {
  const auto key=source.generic_string();
  if(source.empty())return pbr?ImageRgba8{1,1,{255,255,255,255}}:checkerboard_image();
  if(key=="__default_normal__")return {1,1,{128,128,255,255}};
  if(key=="__default_mr__" || key=="__default_occlusion__" || key=="__default_emissive__")
    return {1,1,{255,255,255,255}};
  if(pbr && embedded.contains(key))return embedded.at(key);
  if(!std::filesystem::is_regular_file(source) || std::filesystem::file_size(source)>32'000'000)
    throw std::invalid_argument("texture image exceeds file bounds");
  std::ifstream input(source,std::ios::binary);
  if(!input)throw std::runtime_error("cannot open texture: "+source.string());
  const std::string encoded{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
  if(encoded.starts_with("P3")) {std::istringstream ppm(encoded);return read_ppm_p3(ppm);}
  return decode_image(std::span(reinterpret_cast<const std::uint8_t*>(encoded.data()),encoded.size()));
}
} // namespace terraflux::assets
