#include <terraflux/graphics/opengl_device.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/glcorearb.h>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace terraflux::graphics;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F&& f) {
  bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}
  check(rejected,"invalid target operation was accepted");
}
void verify(OpenGLRenderDevice& device) {
  const auto baseline=device.live_resource_counts();
  const auto bytes=device.live_resource_bytes();
  {
    RenderResourceOwner owner(device);
    const auto a=owner.create_render_target({32,32});
    const auto b=owner.create_render_target({17,19});
    check(device.live_resource_bytes().render_target_bytes==32*32*8+17*19*8,
          "attachment byte accounting mismatch");
    const auto target=RenderTargetDesc{32,32,RenderTargetKind::Offscreen,a};
    RenderPassDesc pass{target};pass.clear_color={0,1,0,1};pass.clear_depth=0.25f;
    device.begin_render_pass(pass);
    const auto green=device.capture_rgba(32,32);
    for(std::size_t i=0;i<green.size();i+=4)
      check(green[i]==0 && green[i+1]==255 && green[i+2]==0 && green[i+3]==255,
            "offscreen color clear mismatch");
    const auto read=reinterpret_cast<PFNGLREADPIXELSPROC>(glfwGetProcAddress("glReadPixels"));
    const auto depth=[&] {float value=0;read(16,16,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&value);return value;};
    check(std::abs(depth()-0.25f)<1e-6f,"offscreen depth clear mismatch");
    rejects([&]{device.destroy_render_target(a);});
    // Creating another framebuffer inside a pass must restore read/draw binding.
    const auto temporary=device.create_render_target({7,9});
    check(device.capture_rgba(32,32)==green,"target creation changed active framebuffer");
    device.destroy_render_target(temporary);
    device.end_render_pass();
    device.begin_render_pass({{17,19,RenderTargetKind::Offscreen,b}});device.end_render_pass();
    pass.color_load=AttachmentLoadOp::Load;pass.depth_load=AttachmentLoadOp::Load;
    device.begin_render_pass(pass);
    check(device.capture_rgba(32,32)==green && std::abs(depth()-0.25f)<1e-6f,
          "load did not preserve separate target attachments");
    device.end_render_pass();

    const std::array<TerrainGpuVertex,3> vertices{{{{-1,-1,0},{0,1,0},1},
        {{1,-1,0},{0,1,0},1},{{0,1,0},{0,1,0},1}}};
    const std::array<std::uint32_t,3> indices{0,1,2};
    const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const auto vb=owner.create_buffer({BufferUsage::Vertex,sizeof(vertices)},std::as_bytes(std::span(vertices)));
    const auto ib=owner.create_buffer({BufferUsage::Index,sizeof(indices)},std::as_bytes(std::span(indices)));
    const auto camera=owner.create_buffer({BufferUsage::Uniform,sizeof(identity)},std::as_bytes(std::span(identity)));
    const auto pipeline=owner.create_pipeline({ShaderProgramId::Terrain,terrain_shader_layout()});
    const DrawIndexed draw{pipeline,vb,ib,0,3,{{0,camera}}, {}};
    // A preserved 0.25 depth must reject the triangle at normalized depth 0.5.
    device.begin_render_pass(pass);device.draw_indexed(draw);
    check(device.capture_rgba(32,32)==green,"load pass lost depth occlusion");
    device.end_render_pass();
    pass.depth_load=AttachmentLoadOp::Clear;pass.clear_depth=1;
    device.begin_render_pass(pass);device.draw_indexed(draw);
    auto pixels=device.capture_rgba(32,32);const auto center=(16*32+16)*4;
    check(pixels[center]==255 && pixels[center+1]<60 && pixels[center+2]<10,
          "geometry failed to render into owned target");
    check(std::abs(depth()-0.5f)<1e-6f,"draw did not write owned depth attachment");
    device.end_render_pass();
    pass.color_load=AttachmentLoadOp::Clear;pass.depth_load=AttachmentLoadOp::Load;
    pass.clear_color={0,0,1,1};
    device.begin_render_pass(pass);
    pixels=device.capture_rgba(32,32);
    check(pixels[center+2]==255 && std::abs(depth()-0.5f)<1e-6f,
          "color-only clear changed depth");
    device.end_render_pass();
    // Return to the borrowed window and verify it has independent color storage.
    device.begin_render_pass({{64,64},AttachmentLoadOp::Clear,AttachmentLoadOp::Clear,{1,0,0,1}});
    const auto red=device.capture_rgba(64,64);
    check(red[0]==255 && red[1]==0 && red[2]==0,"window target did not restore");
    device.end_render_pass();
    rejects([&]{device.begin_render_pass({{33,32,RenderTargetKind::Offscreen,a}});});
    const auto current=device.live_resource_bytes().total();
    device.set_resource_byte_budget(current);
    rejects([&]{(void)device.create_render_target({1,1});});
    check(device.live_resource_bytes().total()==current,"budget rejection leaked attachment storage");
    device.set_resource_byte_budget(std::numeric_limits<std::uint64_t>::max());
  }
  check(device.live_resource_counts()==baseline && device.live_resource_bytes()==bytes,
        "offscreen scope did not return to resource baseline");
  for(int i=0;i<4;++i) {
    const auto handle=device.create_render_target({static_cast<std::uint32_t>(8+i),11});
    device.begin_render_pass({{static_cast<std::uint32_t>(8+i),11,RenderTargetKind::Offscreen,handle}});
    device.end_render_pass();device.destroy_render_target(handle);
    rejects([&]{device.begin_render_pass({{static_cast<std::uint32_t>(8+i),11,RenderTargetKind::Offscreen,handle}});});
  }
  check(device.live_resource_counts()==baseline && device.live_resource_bytes()==bytes,
        "repeated target replacement leaked resources");
  const auto get_error=reinterpret_cast<PFNGLGETERRORPROC>(glfwGetProcAddress("glGetError"));
  check(get_error()==GL_NO_ERROR,"OpenGL error after offscreen tests");
}
}
int main() {
  if(!glfwInit())return 1;
  int result=0;
  try {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    std::unique_ptr<GLFWwindow,decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(64,64,"Offscreen tests",nullptr,nullptr),glfwDestroyWindow);
    check(bool(window),"could not create GL 4.3 test context");glfwMakeContextCurrent(window.get());
    OpenGLRenderDevice device;verify(device);
    std::cout<<"Offscreen GL: clear/load, depth occlusion, drawing, budgets and cleanup passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';result=1;}
  glfwTerminate();return result;
}
