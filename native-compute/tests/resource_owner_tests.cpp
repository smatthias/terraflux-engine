#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/asset_slot.hpp>
#include <terraflux/graphics/visibility.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/mesh_upload.hpp>

#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>

using terraflux::graphics::BufferUsage;
using terraflux::graphics::RenderResourceOwner;
using terraflux::graphics::ValidatingRenderDevice;

namespace {
void check(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
void creates_and_cleans(){
  ValidatingRenderDevice backend;
  std::array<std::byte,64> data{};
  {
    RenderResourceOwner resources(backend);
    resources.create_buffer({BufferUsage::Vertex,data.size()},data);
    resources.create_buffer({BufferUsage::Uniform,data.size()},data);
    resources.create_texture2d({2,2},std::span(data).first(16));
    resources.create_pipeline({terraflux::graphics::terrain_shader_layout(),true});
    check(resources.tracked_buffer_count()==2 &&
          resources.tracked_texture_count()==1 &&
          resources.tracked_pipeline_count()==1,"tracks each GPU resource kind");
    check(backend.live_buffer_count()==2 && backend.live_texture_count()==1 &&
          backend.live_pipeline_count()==1,"resources live before reset");
    resources.reset();
    check(!backend.live_buffer_count() && !backend.live_texture_count() &&
          !backend.live_pipeline_count(),"explicit reset frees GPU resources");
    resources.reset();
    check(resources.tracked_buffer_count()==0,"reset is idempotent");
  }
  check(backend.live_buffer_count()==0,"destructor after reset harmless");
}
void exception_cleanup(){
  ValidatingRenderDevice backend;
  std::array<std::byte,32> data{};
  bool caught=false;
  try{
    RenderResourceOwner resources(backend);
    resources.create_buffer({BufferUsage::Vertex,data.size()},data);
    resources.create_texture2d({2,2},std::span(data).first(16));
    resources.create_pipeline({terraflux::graphics::terrain_shader_layout(),true});
    // Validation backend fails before creating a new GPU resource.
    resources.create_texture2d({2,2},std::span(data).first(8));
  }catch(const std::invalid_argument&){caught=true;}
  check(caught,"partial construction failure propagated");
  check(backend.live_buffer_count()==0 && backend.live_texture_count()==0 &&
        backend.live_pipeline_count()==0,
        "failure during loading releases earlier resources");
}
void dynamic_asset_lifetime() {
  using namespace terraflux::graphics;
  ValidatingRenderDevice backend;
  std::array<std::byte,64> bytes{};
  RenderResourceOwner terrain(backend);
  const auto terrain_buffer=terrain.create_buffer({BufferUsage::Vertex,bytes.size()},bytes);
  struct Asset {
    RenderResourceOwner resources; // destroyed after ring
    BufferHandle vertex;
    std::unique_ptr<OpaqueRenderQueue::InstanceBufferRing> ring;
    explicit Asset(RenderDevice& device):resources(device){}
  };
  AssetSlot<Asset> slot;
  const auto load=[&](bool fail) {
    auto asset=std::make_unique<Asset>(backend);
    asset->vertex=asset->resources.create_buffer({BufferUsage::Vertex,bytes.size()},bytes);
    asset->resources.create_buffer({BufferUsage::Index,bytes.size()},bytes);
    asset->resources.create_texture2d({2,2},std::span(bytes).first(16));
    asset->resources.create_pipeline({terrain_shader_layout(),true});
    asset->ring=std::make_unique<OpaqueRenderQueue::InstanceBufferRing>(backend);
    asset->ring->begin_frame();
    (void)asset->ring->acquire(2);
    if(fail)asset->resources.create_texture2d({2,2},std::span(bytes).first(8));
    return asset;
  };
  for(int cycle=0;cycle<4;++cycle) {
    slot.reload([&]{return load(false);});
    const auto original=slot->vertex;
    check(backend.live_buffer_count()==4 && backend.live_texture_count()==1 &&
          backend.live_pipeline_count()==1,"asset and ring resources live alongside terrain");
    bool rejected=false;
    try{slot.reload([&]{return load(true);});}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected && slot->vertex==original && backend.live_buffer_count()==4 &&
          backend.live_texture_count()==1 && backend.live_pipeline_count()==1,
          "failed replacement preserves original and cleans partial candidate");
    rejected=false;
    try{slot.reload([]{return std::unique_ptr<Asset>{};});}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected && slot->vertex==original,"empty candidate preserves current asset");
    backend.update_buffer(original,0,bytes);
    slot.reload([&]{return load(false);});
    check(slot->vertex!=original && backend.live_buffer_count()==4,
          "successful replacement retires old scope including streamed buffers");
    rejected=false;
    try{backend.update_buffer(original,0,bytes);}
    catch(const std::exception&){rejected=true;}
    check(rejected,"stale asset handles no longer valid after replacement");
    slot.unload();slot.unload();
    check(!slot && backend.live_buffer_count()==1 && !backend.live_texture_count() &&
          !backend.live_pipeline_count(),"unload returns to terrain-only baseline");
    backend.update_buffer(terrain_buffer,0,bytes);
  }
  // An exception out of the owning scope must release the final loaded asset.
  try{
    AssetSlot<Asset> temporary;
    temporary.reload([&]{return load(false);});
    throw std::runtime_error("leave loaded scope");
  }catch(const std::runtime_error&){}
  check(backend.live_buffer_count()==1 && !backend.live_texture_count() &&
        !backend.live_pipeline_count(),"scope unwinding retires dynamically loaded assets");
}

}
int main(){
  try{
    creates_and_cleans();
    exception_cleanup();
    dynamic_asset_lifetime();
    std::cout<<"GPU resource ownership: 3 test groups passed\n";
  }catch(const std::exception& e){
    std::cerr<<"GPU ownership test failed: "<<e.what()<<'\n';return 1;
  }
}
