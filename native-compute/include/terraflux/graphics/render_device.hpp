#pragma once

#include <terraflux/texture_sampling.hpp>
#include <cstddef>
#include <limits>
#include <optional>
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
struct TextureHandle {
  std::uint32_t id=0;
  constexpr explicit operator bool() const {return id!=0;}
  constexpr bool operator==(const TextureHandle&) const = default;
};
struct TextureDesc {
  std::uint32_t width=0,height=0; // 4 channels, RGBA8
  TextureSampling sampling{};
};
struct PipelineHandle {
  std::uint32_t id = 0;
  constexpr explicit operator bool() const { return id != 0; }
  constexpr bool operator==(const PipelineHandle&) const = default;
};

enum class ShaderProgramId : std::uint8_t {
  Unspecified, ContractOnly, Terrain, Wildfire, Object, TexturedObject,
  InstancedTexturedObject, PbrObject, PbrInstanced
};
struct PipelineDesc {
  ShaderProgramId program=ShaderProgramId::Unspecified;
  ShaderLayout layout;
  bool depth_test = true;
  bool alpha_blend = false;
  bool depth_write = true;
};

// Built-in engine identity is explicit; layout validation never selects a shader.
ShaderLayout shader_program_layout(ShaderProgramId program);
void validate_pipeline_desc(const PipelineDesc& desc,bool allow_contract_only=true);

struct UniformBufferBinding {
  std::uint32_t slot = 0;
  BufferHandle buffer;
};

struct StorageBufferBinding {
  std::uint32_t slot = 0;
  BufferHandle buffer;
};

struct TextureBinding {
  std::uint32_t slot=0;
  TextureHandle texture;
};
struct DrawIndexed {
  PipelineHandle pipeline;
  BufferHandle vertex_buffer;
  BufferHandle index_buffer;
  std::uint32_t first_index = 0;
  std::uint32_t index_count = 0;
  std::vector<UniformBufferBinding> uniform_buffers;
  std::vector<StorageBufferBinding> storage_buffers;
  std::vector<TextureBinding> textures{};
  std::uint32_t instance_count=1; // OpenGL glDrawElementsInstanced for instance shader
};

// Logical API storage, excluding driver overhead and deferred orphan copies.
struct ResourceBytes {
  std::uint64_t buffer_bytes=0,texture_bytes=0;
  std::uint64_t total() const {return buffer_bytes+texture_bytes;}
  bool operator==(const ResourceBytes&) const=default;
};
struct BudgetRejection {
  std::uint64_t used_bytes=0,requested_bytes=0,limit_bytes=0;
};
struct ResourceTelemetry {
  ResourceBytes peak_bytes{};
  std::uint64_t peak_total_bytes=0,allocation_count=0,budget_rejections=0,headroom_bytes=0;
  std::uint64_t limit_bytes=std::numeric_limits<std::uint64_t>::max();
  bool over_budget=false;
  std::optional<BudgetRejection> last_rejection;
};
std::uint64_t rgba8_storage_bytes(TextureDesc desc);
void check_resource_byte_budget(std::uint64_t used,std::uint64_t additional,std::uint64_t limit);

// The engine owns this interface; no OpenGL, GLFW, PSSL, or console types
// appear here. Buffers have explicit lifetime and bounded subrange updates.
class RenderDevice {
 public:
  virtual ~RenderDevice() = default;
  virtual ResourceBytes live_resource_bytes() const = 0;
  virtual ResourceTelemetry resource_telemetry() const = 0;
  virtual void reset_resource_telemetry() = 0;
  virtual void set_resource_byte_budget(std::uint64_t bytes) = 0;
  virtual BufferHandle create_buffer(BufferDesc desc, std::span<const std::byte> initial_data) = 0;
  virtual void destroy_buffer(BufferHandle handle) = 0;
  virtual void update_buffer(BufferHandle handle, std::size_t offset_bytes,
                             std::span<const std::byte> data) = 0;
  // Discard storage-buffer contents before writing a new frame. OpenGL
  // orphaning retains the old GPU allocation for in-flight draw commands.
  virtual void orphan_storage_buffer(BufferHandle handle) = 0;
  virtual TextureHandle create_texture2d(TextureDesc desc,
                                      std::span<const std::byte> rgba8) = 0;
  virtual void destroy_texture(TextureHandle handle) = 0;
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
  void orphan_storage_buffer(BufferHandle handle) override;
  TextureHandle create_texture2d(TextureDesc desc, std::span<const std::byte> rgba8) override;
  void destroy_texture(TextureHandle handle) override;
  PipelineHandle create_pipeline(PipelineDesc desc) override;
  void destroy_pipeline(PipelineHandle handle) override;
  void draw_indexed(const DrawIndexed& command) override;
  ResourceBytes live_resource_bytes() const override;
  ResourceTelemetry resource_telemetry() const override;
  void reset_resource_telemetry() override;
  void set_resource_byte_budget(std::uint64_t bytes) override {byte_budget_=bytes;}
  std::size_t draw_count() const { return draw_count_; }
  std::size_t live_buffer_count() const { return buffers_.size(); }
  std::size_t live_pipeline_count() const { return pipelines_.size(); }
  std::size_t live_texture_count() const { return textures_.size(); }

 private:
  struct StoredBuffer {
    BufferDesc desc;
    std::vector<std::byte> bytes;
  };
  ResourceTelemetry telemetry_;
  void admit_bytes(std::uint64_t bytes);
  void record_allocation();
  std::uint64_t byte_budget_=std::numeric_limits<std::uint64_t>::max();
  std::uint32_t next_id_ = 1;
  std::unordered_map<std::uint32_t, StoredBuffer> buffers_;
  std::unordered_map<std::uint32_t, PipelineDesc> pipelines_;
  std::unordered_map<std::uint32_t, TextureDesc> textures_;
  std::size_t draw_count_ = 0;
  std::uint32_t allocate_id();
};

}  // namespace terraflux::graphics
