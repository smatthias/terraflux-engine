#pragma once
#include <memory>
#include <stdexcept>
#include <utility>

namespace terraflux::graphics {
// A replaceable asset scope. Build a complete candidate before retiring the
// old scope: loader failures clean up partial resources and preserve the
// currently visible asset. Asset destructors must release their own resources
// before the RenderDevice/context is destroyed.
template<class Asset>class AssetSlot {
 public:
  AssetSlot()=default;
  AssetSlot(const AssetSlot&)=delete;
  AssetSlot& operator=(const AssetSlot&)=delete;
  explicit operator bool() const noexcept {return bool(asset_);}
  Asset* operator->() noexcept {return asset_.get();}
  const Asset* operator->() const noexcept {return asset_.get();}
  void unload() noexcept {asset_.reset();}
  template<class Loader>void reload(Loader&& loader) {
    std::unique_ptr<Asset> candidate=std::forward<Loader>(loader)();
    if(!candidate)throw std::invalid_argument("asset loader returned an empty scope");
    asset_.swap(candidate);
  }
 private:
  std::unique_ptr<Asset> asset_;
};
} // namespace terraflux::graphics
