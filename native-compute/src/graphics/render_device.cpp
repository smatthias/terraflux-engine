#include <terraflux/graphics/render_device.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace terraflux::graphics {

std::uint32_t vertex_format_bytes(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float1: return 4;
    case VertexFormat::Float2: return 8;
    case VertexFormat::Float3: return 12;
    case VertexFormat::Float4: return 16;
  }
  throw std::invalid_argument("invalid vertex attribute format");
}

void validate_shader_layout(const ShaderLayout& layout) {
  if (layout.vertex_stride_bytes == 0 || layout.attributes.empty())
    throw std::invalid_argument("shader layout requires nonempty vertex attributes and stride");

  std::unordered_set<std::string> semantics;
  std::unordered_set<std::uint32_t> locations;
  for (const auto& attr : layout.attributes) {
    if (attr.semantic.empty() || !semantics.insert(attr.semantic).second ||
        !locations.insert(attr.location).second)
      throw std::invalid_argument("duplicate or empty vertex attribute");
    const auto width = vertex_format_bytes(attr.format);
    if (attr.offset_bytes > layout.vertex_stride_bytes ||
        width > layout.vertex_stride_bytes - attr.offset_bytes)
      throw std::invalid_argument("vertex attribute outside vertex stride");
  }

  std::unordered_set<std::string> binding_names;
  for (const auto& binding : layout.bindings) {
    if (binding.name.empty() || !binding_names.insert(binding.name).second ||
        binding.stages == 0 || (binding.stages & ~(kVertexStage | kFragmentStage)) != 0)
      throw std::invalid_argument("invalid shader binding declaration: " + binding.name + " kind=" + std::to_string(static_cast<int>(binding.kind)) + " slot=" + std::to_string(binding.slot) + " stages=" + std::to_string(binding.stages));
  }
  for (std::size_t a = 0; a < layout.bindings.size(); ++a) {
    for (std::size_t b = a + 1; b < layout.bindings.size(); ++b) {
      const auto& lhs = layout.bindings[a];
      const auto& rhs = layout.bindings[b];
      if (lhs.kind == rhs.kind && lhs.slot == rhs.slot && (lhs.stages & rhs.stages) != 0)
        throw std::invalid_argument("shader binding slot collision within a stage");
    }
  }
}

void validate_shader_reflection(const ShaderLayout& expected, const ShaderLayout& reflected) {
  validate_shader_layout(expected);
  validate_shader_layout(reflected);
  if (expected.vertex_stride_bytes != reflected.vertex_stride_bytes ||
      expected.attributes.size() != reflected.attributes.size() ||
      expected.bindings.size() != reflected.bindings.size())
    throw std::invalid_argument("shader reflection differs from declared layout");

  // Reflection order is not significant; semantic/binding names are stable IDs.
  for (const auto& attr : expected.attributes) {
    const auto it = std::find_if(reflected.attributes.begin(), reflected.attributes.end(),
        [&](const VertexAttribute& candidate) { return candidate.semantic == attr.semantic; });
    if (it == reflected.attributes.end() || it->location != attr.location ||
        it->offset_bytes != attr.offset_bytes || it->format != attr.format)
      throw std::invalid_argument("vertex reflection mismatch: " + attr.semantic);
  }
  for (const auto& binding : expected.bindings) {
    const auto it = std::find_if(reflected.bindings.begin(), reflected.bindings.end(),
        [&](const ShaderBinding& candidate) { return candidate.name == binding.name; });
    if (it == reflected.bindings.end() || it->kind != binding.kind ||
        it->slot != binding.slot || it->stages != binding.stages)
      throw std::invalid_argument("resource reflection mismatch: " + binding.name);
  }
}

std::uint32_t ValidatingRenderDevice::allocate_id() {
  if (next_id_ == 0 || next_id_ == std::numeric_limits<std::uint32_t>::max())
    throw std::overflow_error("graphics handle space exhausted");
  return next_id_++;
}

BufferHandle ValidatingRenderDevice::create_buffer(
    BufferDesc desc, std::span<const std::byte> initial_data) {
  if (desc.size_bytes == 0 || initial_data.size() != desc.size_bytes)
    throw std::invalid_argument("buffer must have positive size and complete initialization");
  const auto id = allocate_id();
  buffers_.emplace(id, StoredBuffer{desc, {initial_data.begin(), initial_data.end()}});
  return {id};
}

void ValidatingRenderDevice::update_buffer(BufferHandle handle, std::size_t offset_bytes,
                                          std::span<const std::byte> data) {
  const auto it = buffers_.find(handle.id);
  if (it == buffers_.end()) throw std::invalid_argument("unknown buffer handle for update");
  auto& bytes = it->second.bytes;
  if (offset_bytes > bytes.size() || data.size() > bytes.size() - offset_bytes)
    throw std::out_of_range("buffer update exceeds allocated storage");
  std::copy(data.begin(), data.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset_bytes));
}

void ValidatingRenderDevice::destroy_buffer(BufferHandle handle) {
  if (buffers_.erase(handle.id) != 1)
    throw std::invalid_argument("unknown or already destroyed buffer handle");
}

TextureHandle ValidatingRenderDevice::create_texture2d(
    TextureDesc desc, std::span<const std::byte> rgba8) {
  if (!desc.width || !desc.height ||
      desc.width>4096 || desc.height>4096 ||
      static_cast<std::uint64_t>(desc.width)*desc.height>4'000'000 ||
      rgba8.size()!=static_cast<std::size_t>(desc.width)*desc.height*4)
    throw std::invalid_argument("invalid RGBA8 texture dimensions or pixels");
  const auto id=allocate_id();
  textures_.emplace(id,desc);
  return {id};
}
void ValidatingRenderDevice::destroy_texture(TextureHandle handle) {
  if (textures_.erase(handle.id)!=1)
    throw std::invalid_argument("unknown or destroyed texture");
}

PipelineHandle ValidatingRenderDevice::create_pipeline(PipelineDesc desc) {
  validate_shader_layout(desc.layout);
  const auto id = allocate_id();
  pipelines_.emplace(id, std::move(desc));
  return {id};
}

void ValidatingRenderDevice::destroy_pipeline(PipelineHandle handle) {
  if (pipelines_.erase(handle.id) != 1)
    throw std::invalid_argument("unknown or already destroyed pipeline handle");
}

void ValidatingRenderDevice::draw_indexed(const DrawIndexed& command) {
  const auto pipeline = pipelines_.find(command.pipeline.id);
  const auto vb = buffers_.find(command.vertex_buffer.id);
  const auto ib = buffers_.find(command.index_buffer.id);
  if (pipeline == pipelines_.end() || vb == buffers_.end() || ib == buffers_.end())
    throw std::invalid_argument("draw references nonexistent graphics resources");
  if (vb->second.desc.usage != BufferUsage::Vertex || ib->second.desc.usage != BufferUsage::Index)
    throw std::invalid_argument("draw buffers have incorrect usages");
  const auto stride = pipeline->second.layout.vertex_stride_bytes;
  if (vb->second.bytes.size() % stride != 0 ||
      ib->second.bytes.size() % sizeof(std::uint32_t) != 0)
    throw std::invalid_argument("draw buffer size is not element-aligned");
  const auto available_indices = ib->second.bytes.size() / sizeof(std::uint32_t);
  if (command.index_count == 0 || command.index_count % 3 != 0 ||
      command.first_index > available_indices ||
      command.index_count > available_indices - command.first_index)
    throw std::out_of_range("indexed triangle draw exceeds index buffer");
  const auto vertex_count = vb->second.bytes.size() / stride;
  for (std::size_t i = command.first_index;
       i < static_cast<std::size_t>(command.first_index) + command.index_count; ++i) {
    std::uint32_t vertex_index = 0;
    std::memcpy(&vertex_index, ib->second.bytes.data() + i * sizeof(vertex_index),
                sizeof(vertex_index));
    if (vertex_index >= vertex_count)
      throw std::out_of_range("draw references nonexistent vertex");
  }
  std::unordered_set<std::uint32_t> bound_slots;
  for (const auto& bound : command.uniform_buffers) {
    if (!bound_slots.insert(bound.slot).second)
      throw std::invalid_argument("duplicate uniform buffer binding");
    const auto buffer = buffers_.find(bound.buffer.id);
    if (buffer == buffers_.end() || buffer->second.desc.usage != BufferUsage::Uniform)
      throw std::invalid_argument("invalid uniform buffer resource");
    const auto& bindings = pipeline->second.layout.bindings;
    const auto matching = std::find_if(bindings.begin(), bindings.end(), [&](const ShaderBinding& value) {
      return value.kind == ShaderResourceKind::UniformBuffer && value.slot == bound.slot;
    });
    if (matching == bindings.end())
      throw std::invalid_argument("uniform buffer bound to undeclared slot");
  }

  std::unordered_set<std::uint32_t> storage_slots;
  for (const auto& bound : command.storage_buffers) {
    if (!storage_slots.insert(bound.slot).second)
      throw std::invalid_argument("duplicate shader storage binding");
    const auto it = buffers_.find(bound.buffer.id);
    if (it == buffers_.end() || it->second.desc.usage != BufferUsage::Storage)
      throw std::invalid_argument("invalid shader storage buffer");
    const auto& declared = pipeline->second.layout.bindings;
    const auto match = std::find_if(declared.begin(), declared.end(),
        [&](const ShaderBinding& binding) {
          return binding.kind == ShaderResourceKind::StorageBuffer && binding.slot == bound.slot;
        });
    if (match == declared.end()) throw std::invalid_argument("undeclared shader storage slot");
  }
  for (const auto& required : pipeline->second.layout.bindings)
    if (required.kind == ShaderResourceKind::StorageBuffer &&
        !storage_slots.contains(required.slot))
      throw std::invalid_argument("required shader storage buffer not bound");
  std::unordered_set<std::uint32_t> texture_slots;
  for (const auto& bound : command.textures) {
    if (!texture_slots.insert(bound.slot).second)
      throw std::invalid_argument("duplicate texture slot");
    if (!textures_.contains(bound.texture.id))
      throw std::invalid_argument("unknown or destroyed texture binding");
    const auto& declared=pipeline->second.layout.bindings;
    const auto found=std::find_if(declared.begin(),declared.end(),
      [&](const ShaderBinding& binding) {
        return binding.kind==ShaderResourceKind::SampledTexture &&
               binding.slot==bound.slot;
      });
    if (found==declared.end()) throw std::invalid_argument("undeclared texture slot");
  }
  for (const auto& binding : pipeline->second.layout.bindings)
    if (binding.kind==ShaderResourceKind::SampledTexture &&
        !texture_slots.contains(binding.slot))
      throw std::invalid_argument("required texture missing");
  ++draw_count_;
}

}  // namespace terraflux::graphics
