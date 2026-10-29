#include <terraflux/graphics/resource_owner.hpp>
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
}
int main(){
  try{
    creates_and_cleans();
    exception_cleanup();
    std::cout<<"GPU resource ownership: 2 test groups passed\n";
  }catch(const std::exception& e){
    std::cerr<<"GPU ownership test failed: "<<e.what()<<'\n';return 1;
  }
}
