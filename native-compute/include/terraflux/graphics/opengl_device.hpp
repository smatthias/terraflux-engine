#pragma once

#include <terraflux/graphics/render_device.hpp>

#include <memory>

namespace terraflux::graphics {

// A desktop OpenGL 4.3 backend. Construct only after creating and making a
// GLFW OpenGL 4.3 context current; destroy before destroying that context.
// No GL or GLFW types escape this public interface.
class OpenGLRenderDevice final : public RenderDevice {
 public:
  OpenGLRenderDevice();
  ~OpenGLRenderDevice() override;

  OpenGLRenderDevice(const OpenGLRenderDevice&) = delete;
  OpenGLRenderDevice& operator=(const OpenGLRenderDevice&) = delete;

  BufferHandle create_buffer(BufferDesc desc, std::span<const std::byte> initial_data) override;
  void update_buffer(BufferHandle handle, std::size_t offset_bytes,
                     std::span<const std::byte> data) override;
  void destroy_buffer(BufferHandle handle) override;
  PipelineHandle create_pipeline(PipelineDesc desc) override;
  void destroy_pipeline(PipelineHandle handle) override;
  void draw_indexed(const DrawIndexed& command) override;

  void clear_frame(int framebuffer_width, int framebuffer_height);
  // Smoke-test diagnostic; inspect the back buffer before swapping it.
  // Counts bright red/orange pixels in the current render target.
  std::size_t count_fire_pixels(int framebuffer_width, int framebuffer_height);
  std::size_t count_asset_pixels(int framebuffer_width, int framebuffer_height);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace terraflux::graphics
