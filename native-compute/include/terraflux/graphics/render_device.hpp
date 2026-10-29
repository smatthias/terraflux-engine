#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace terraflux::graphics {

// Graphics-API-neutral shader contract. GLSL and PSSL reflection must each be
// compared with this declaration by their respective (future) backends.
enum class VertexFormat : std::uint8_t { Float1, Float2, Float3, Float4 };
enum class ShaderResourceKind : std::uint8_t { UniformBuffer, SampledTexture, Sampler, StorageBuffer };
inline constexpr std::uint32_t kVertexStage = 1U;
inline constexpr std::uint32_t kFragmentStage = 2U;

struct VertexAttribute {
  std::string semantic;
  std::uint32_t location = 0;
  VertexFormat format = VertexFormat::Float3;
  std::uint32_t offset_bytes = 0;
};

struct ShaderBinding {
  std::string name;
  ShaderResourceKind kind = ShaderResourceKind::UniformBuffer;
  std::uint32_t slot = 0;
  std::uint32_t stages = 0;  // bitmask of kVertexStage / kFragmentStage
};

struct ShaderLayout {
  std::uint32_t vertex_stride_bytes = 0;
  std::vector<VertexAttribute> attributes;
  std::vector<ShaderBinding> bindings;
};

std::uint32_t vertex_format_bytes(VertexFormat format);
void validate_shader_layout(const ShaderLayout& layout);
void validate_shader_reflection(const ShaderLayout& expected, const ShaderLayout& reflected);

enum class BufferUsage : std::uint8_t { Vertex, Index, Uniform, Storage };
struct BufferDesc {
  BufferUsage usage = BufferUsage::Vertex;
  std::size_t size_bytes = 0;
};

struct BufferHandle {
  std::uint32_t id = 0;
  constexpr explicit operator bool() const { return id != 0; }
  constexpr bool operator==(const BufferHandle&) const = default;
};
struct PipelineHandle {
  std::uint32_t id = 0;
  constexpr explicit operator bool() const { return id != 0; }
  constexpr bool operator==(const PipelineHandle&) const = default;
};

struct PipelineDesc {
  ShaderLayout layout;
  bool depth_test = true;
};

struct UniformBufferBinding {
  std::uint32_t slot = 0;
  BufferHandle buffer;
};

struct StorageBufferBinding {
  std::uint32_t slot = 0;
  BufferHandle buffer;
};

struct DrawIndexed {
  PipelineHandle pipeline;
  BufferHandle vertex_buffer;
  BufferHandle index_buffer;
  std::uint32_t first_index = 0;
  std::uint32_t index_count = 0;
  std::vector<UniformBufferBinding> uniform_buffers;
  std::vector<StorageBufferBinding> storage_buffers;
};

// The engine owns this interface; no OpenGL, GLFW, PSSL, or console types
// appear here. Buffers have explicit lifetime and bounded subrange updates.
class RenderDevice {
 public:
  virtual ~RenderDevice() = default;
  virtual BufferHandle create_buffer(BufferDesc desc, std::span<const std::byte> initial_data) = 0;
  virtual void destroy_buffer(BufferHandle handle) = 0;
  virtual void update_buffer(BufferHandle handle, std::size_t offset_bytes,
                             std::span<const std::byte> data) = 0;
  virtual PipelineHandle create_pipeline(PipelineDesc desc) = 0;
  virtual void destroy_pipeline(PipelineHandle handle) = 0;
  virtual void draw_indexed(const DrawIndexed& command) = 0;
};

// CPU-only validation backend: tests resource lifetimes, shader declarations
// and draw bounds without requiring a GPU or display server in CI.
// Deliberately does not pretend to compile GLSL/PSSL or draw pixels.
class ValidatingRenderDevice final : public RenderDevice {
 public:
  BufferHandle create_buffer(BufferDesc desc, std::span<const std::byte> initial_data) override;
  void destroy_buffer(BufferHandle handle) override;
  void update_buffer(BufferHandle handle, std::size_t offset_bytes,
                     std::span<const std::byte> data) override;
  PipelineHandle create_pipeline(PipelineDesc desc) override;
  void destroy_pipeline(PipelineHandle handle) override;
  void draw_indexed(const DrawIndexed& command) override;
  std::size_t draw_count() const { return draw_count_; }
  std::size_t live_buffer_count() const { return buffers_.size(); }
  std::size_t live_pipeline_count() const { return pipelines_.size(); }

 private:
  struct StoredBuffer {
    BufferDesc desc;
    std::vector<std::byte> bytes;
  };
  std::uint32_t next_id_ = 1;
  std::unordered_map<std::uint32_t, StoredBuffer> buffers_;
  std::unordered_map<std::uint32_t, PipelineDesc> pipelines_;
  std::size_t draw_count_ = 0;
  std::uint32_t allocate_id();
};

}  // namespace terraflux::graphics
