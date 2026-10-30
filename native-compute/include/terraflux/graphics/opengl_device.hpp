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
  void orphan_storage_buffer(BufferHandle handle) override;
  TextureHandle create_texture2d(TextureDesc desc, std::span<const std::byte> rgba8) override;
  void destroy_texture(TextureHandle handle) override;
  PipelineHandle create_pipeline(PipelineDesc desc) override;
  void destroy_pipeline(PipelineHandle handle) override;
  void draw_indexed(const DrawIndexed& command) override;

  struct GpuTimingSummary {
    std::size_t samples=0;
    std::size_t skipped=0; // timed frames skipped if all query slots were busy
    double mean_ms=0, min_ms=0, max_ms=0;
  };
  // GPU elapsed time for clear/draw commands only. Asynchronous ring queries
  // do not block normal frames; drain=true waits for the remaining samples.
  void enable_gpu_timing();
  void begin_gpu_timing();
  void end_gpu_timing();
  GpuTimingSummary gpu_timing_summary(bool drain=false);

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
