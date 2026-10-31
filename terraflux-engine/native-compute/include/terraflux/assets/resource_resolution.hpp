#pragma once
#include <terraflux/assets/material.hpp>
#include <terraflux/assets/obj.hpp>
#include <filesystem>

namespace terraflux::assets {
struct LocatedMaterial {Material material;std::filesystem::path folder;};
using LocatedMaterials=std::map<std::string,LocatedMaterial>;
// Resolve a bounded regular file beneath base, rejecting lexical and symlink escapes.
std::filesystem::path resolve_asset_path(const std::filesystem::path& base,
    const std::string& reference,std::uintmax_t max_bytes=64'000'000);
LocatedMaterials load_obj_materials(Mesh& mesh,const std::filesystem::path& folder,
    const std::string& prefix={},bool nonmetallic=false);
std::string texture_cache_key(const std::filesystem::path& source,
    const TextureSampling& sampling,bool pbr);
ImageRgba8 resolve_texture_image(const std::filesystem::path& source,bool pbr,
    const std::map<std::string,ImageRgba8>& embedded={});
} // namespace terraflux::assets
