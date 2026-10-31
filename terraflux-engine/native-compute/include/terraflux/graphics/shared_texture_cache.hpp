#pragma once
#include <terraflux/graphics/resource_owner.hpp>
#include <map>
#include <memory>
#include <string>

namespace terraflux::graphics {
// Device/context must outlive every lease, even if the cache is cleared.
class SharedTexture {
 public:
  SharedTexture(RenderDevice& device,TextureDesc desc,std::span<const std::byte> bytes)
      :resources_(device),handle_(resources_.create_texture2d(desc,bytes)){}
  TextureHandle handle() const {return handle_;}
 private:
  RenderResourceOwner resources_;
  TextureHandle handle_;
};
using TextureLease=std::shared_ptr<const SharedTexture>;
// The cache holds only weak references. Materials retain leases; the last
// material release frees the texture, independent of cache entry lifetime.
class SharedTextureCache {
 public:
  explicit SharedTextureCache(RenderDevice& device):device_(device){}
  TextureLease find(const std::string& key) const {
    const auto it=entries_.find(key);
    return it==entries_.end()?TextureLease{}:it->second.lock();
  }
  TextureLease acquire(const std::string& key,TextureDesc desc,std::span<const std::byte> bytes) {
    if(auto existing=find(key))return existing;
    auto texture=std::make_shared<const SharedTexture>(device_,desc,bytes);
    entries_.insert_or_assign(key,texture);
    return texture;
  }
  void clear() {entries_.clear();} // Never invalidates active material leases.
  void prune() {std::erase_if(entries_,[](const auto& entry){return entry.second.expired();});}
  std::size_t size() const {
    std::size_t live=0;
    for(const auto& [key,value]:entries_)if(!value.expired())++live;
    return live;
  }
 private:
  RenderDevice& device_;
  std::map<std::string,std::weak_ptr<const SharedTexture>> entries_;
};
} // namespace terraflux::graphics
