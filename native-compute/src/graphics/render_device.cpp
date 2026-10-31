#include <terraflux/graphics/render_device.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/terrain_shading.hpp>
#include <terraflux/graphics/water_shading.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/graphics/fire_effects.hpp>
#include <terraflux/graphics/mesh_upload.hpp>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace terraflux::graphics {

void validate_render_pass_desc(const RenderPassDesc& desc) {
  if(desc.target.kind!=RenderTargetKind::Window && desc.target.kind!=RenderTargetKind::Offscreen)
    throw std::invalid_argument("unsupported render target kind");
  constexpr auto max_extent=static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
  if(!desc.target.width || !desc.target.height ||
     desc.target.width>max_extent || desc.target.height>max_extent)
    throw std::invalid_argument("render target extents must be 1..INT32_MAX");
  if((desc.target.kind==RenderTargetKind::Window && desc.target.handle) ||
     (desc.target.kind==RenderTargetKind::Offscreen && !desc.target.handle))
    throw std::invalid_argument("render target kind and handle disagree");
  const auto valid_load=[](AttachmentLoadOp op) {
    return op==AttachmentLoadOp::Load || op==AttachmentLoadOp::Clear;
  };
  if(!valid_load(desc.color_load) || !valid_load(desc.depth_load))
    throw std::invalid_argument("unsupported attachment load operation");
  for(const auto channel:desc.clear_color)
    if(!std::isfinite(channel) || channel<0 || channel>1)
      throw std::invalid_argument("clear color must be finite and normalized");
  if(!std::isfinite(desc.clear_depth) || desc.clear_depth<0 || desc.clear_depth>1)
    throw std::invalid_argument("clear depth must be finite and normalized");
}

void ValidatingRenderDevice::begin_render_pass(const RenderPassDesc& desc) {
  if(active_pass_id_)throw std::logic_error("render passes cannot nest");
  validate_render_pass_desc(desc);
  if(desc.target.kind==RenderTargetKind::Offscreen) {
    const auto it=render_targets_.find(desc.target.handle.id);
    if(it==render_targets_.end() || it->second.desc.width!=desc.target.width ||
       it->second.desc.height!=desc.target.height)
      throw std::invalid_argument("unknown render target or mismatched pass extent");
  }
  if(!next_pass_id_)throw std::overflow_error("render pass identity space exhausted");
  active_target_=desc.target.handle;
  active_pass_id_=next_pass_id_++;
}

void ValidatingRenderDevice::end_render_pass() {
  if(!active_pass_id_)throw std::logic_error("no active render pass");
  active_pass_id_=0;
  active_target_={};
}

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

std::uint64_t rgba8_storage_bytes(TextureDesc desc) {
  if(!desc.width || !desc.height || desc.width>4096 || desc.height>4096 ||
     static_cast<std::uint64_t>(desc.width)*desc.height>4'000'000)
    throw std::invalid_argument("invalid texture dimensions for byte accounting");
  std::uint64_t bytes=0;
  auto width=desc.width,height=desc.height;
  while(true) {
    bytes+=static_cast<std::uint64_t>(width)*height*4;
    if(!desc.sampling.uses_mipmaps() || (width==1 && height==1))break;
    width=std::max(1U,width/2);height=std::max(1U,height/2);
  }
  return bytes;
}
std::uint64_t offscreen_storage_bytes(OffscreenTargetDesc desc) {
  if(!desc.width || !desc.height || desc.width>4096 || desc.height>4096 ||
     static_cast<std::uint64_t>(desc.width)*desc.height>4'000'000 ||
     desc.color!=ColorAttachmentFormat::Rgba8 ||
     desc.depth!=DepthAttachmentFormat::Depth32Float)
    throw std::invalid_argument("unsupported offscreen extent or attachment format");
  return static_cast<std::uint64_t>(desc.width)*desc.height*8;
}
RenderTargetHandle ValidatingRenderDevice::create_render_target(OffscreenTargetDesc desc) {
  admit_bytes(offscreen_storage_bytes(desc));
  const auto id=allocate_id();
  const auto color=TextureHandle{allocate_id()};
  try {
    render_targets_.emplace(id,StoredTarget{desc,color});
    TextureDesc texture{desc.width,desc.height};
    texture.sampling.wrap_s=texture.sampling.wrap_t=TextureWrap::ClampToEdge;
    borrowed_colors_.emplace(color.id,RenderTargetHandle{id});
    textures_.emplace(color.id,texture);
  } catch(...) {
    textures_.erase(color.id);borrowed_colors_.erase(color.id);render_targets_.erase(id);
    throw;
  }
  record_allocation();
  return {id};
}
TextureHandle ValidatingRenderDevice::render_target_color(RenderTargetHandle handle) const {
  const auto it=render_targets_.find(handle.id);
  if(it==render_targets_.end())throw std::invalid_argument("unknown render target color view");
  return it->second.color;
}
void ValidatingRenderDevice::destroy_render_target(RenderTargetHandle handle) {
  if(active_pass_id_ && handle==active_target_)
    throw std::logic_error("cannot destroy the active render target");
  const auto color=render_target_color(handle);
  textures_.erase(color.id);borrowed_colors_.erase(color.id);render_targets_.erase(handle.id);
}

void check_resource_byte_budget(std::uint64_t used,std::uint64_t additional,std::uint64_t limit) {
  if(used>limit || additional>limit-used)
    throw std::length_error("resource byte budget exceeded");
}
ResourceBytes ValidatingRenderDevice::live_resource_bytes() const {
  ResourceBytes result;
  for(const auto& [id,buffer]:buffers_)result.buffer_bytes+=buffer.desc.size_bytes;
  for(const auto& [id,texture]:textures_)
    if(!borrowed_colors_.contains(id))result.texture_bytes+=rgba8_storage_bytes(texture);
  for(const auto& [id,target]:render_targets_)result.render_target_bytes+=offscreen_storage_bytes(target.desc);
  return result;
}

ResourceTelemetry ValidatingRenderDevice::resource_telemetry() const {
  auto result=telemetry_;
  const auto used=live_resource_bytes().total();
  result.limit_bytes=byte_budget_;
  result.over_budget=used>byte_budget_;
  result.headroom_bytes=result.over_budget?0:byte_budget_-used;
  return result;
}
void ValidatingRenderDevice::reset_resource_telemetry() {
  telemetry_={};
  telemetry_.peak_bytes=live_resource_bytes();
  telemetry_.peak_total_bytes=telemetry_.peak_bytes.total();
}
void ValidatingRenderDevice::admit_bytes(std::uint64_t bytes) {
  const auto used=live_resource_bytes().total();
  try{check_resource_byte_budget(used,bytes,byte_budget_);}
  catch(const std::length_error&){
    ++telemetry_.budget_rejections;
    telemetry_.last_rejection=BudgetRejection{used,bytes,byte_budget_};
    throw;
  }
}
void ValidatingRenderDevice::record_allocation() {
  const auto used=live_resource_bytes();
  ++telemetry_.allocation_count;
  telemetry_.peak_bytes.buffer_bytes=std::max(telemetry_.peak_bytes.buffer_bytes,used.buffer_bytes);
  telemetry_.peak_bytes.texture_bytes=std::max(telemetry_.peak_bytes.texture_bytes,used.texture_bytes);
  telemetry_.peak_bytes.render_target_bytes=std::max(telemetry_.peak_bytes.render_target_bytes,used.render_target_bytes);
  telemetry_.peak_total_bytes=std::max(telemetry_.peak_total_bytes,used.total());
}

BufferHandle ValidatingRenderDevice::create_buffer(
    BufferDesc desc, std::span<const std::byte> initial_data) {
  if (desc.size_bytes == 0 || initial_data.size() != desc.size_bytes)
    throw std::invalid_argument("buffer must have positive size and complete initialization");
  admit_bytes(desc.size_bytes);
  const auto id = allocate_id();
  buffers_.emplace(id, StoredBuffer{desc, {initial_data.begin(), initial_data.end()}});
  record_allocation();
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

void ValidatingRenderDevice::orphan_storage_buffer(BufferHandle handle) {
  const auto it=buffers_.find(handle.id);
  if(it==buffers_.end() || it->second.desc.usage!=BufferUsage::Storage)
    throw std::invalid_argument("orphan requires a live storage buffer");
  std::fill(it->second.bytes.begin(),it->second.bytes.end(),std::byte{0});
}

void ValidatingRenderDevice::destroy_buffer(BufferHandle handle) {
  if (buffers_.erase(handle.id) != 1)
    throw std::invalid_argument("unknown or already destroyed buffer handle");
}

TextureHandle ValidatingRenderDevice::create_texture2d(
    TextureDesc desc, std::span<const std::byte> rgba8) {
  const auto valid_min=static_cast<unsigned>(desc.sampling.min)<=5;
  const auto valid_mag=static_cast<unsigned>(desc.sampling.mag)<=1;
  const auto valid_wrap=static_cast<unsigned>(desc.sampling.wrap_s)<=2 &&
                        static_cast<unsigned>(desc.sampling.wrap_t)<=2;
  if(!valid_min || !valid_mag || !valid_wrap ||
      !desc.width || !desc.height ||
      desc.width>4096 || desc.height>4096 ||
      static_cast<std::uint64_t>(desc.width)*desc.height>4'000'000 ||
      rgba8.size()!=static_cast<std::size_t>(desc.width)*desc.height*4)
    throw std::invalid_argument("invalid RGBA8 texture dimensions or pixels");
  admit_bytes(rgba8_storage_bytes(desc));
  const auto id=allocate_id();
  textures_.emplace(id,desc);
  record_allocation();
  return {id};
}
void ValidatingRenderDevice::destroy_texture(TextureHandle handle) {
  if(borrowed_colors_.contains(handle.id))
    throw std::invalid_argument("render target color cannot be destroyed separately");
  if (textures_.erase(handle.id)!=1)
    throw std::invalid_argument("unknown or destroyed texture");
}

ShaderLayout shader_program_layout(ShaderProgramId program) {
  switch(program) {
    case ShaderProgramId::Composite:return {16,{{"POSITION",0,VertexFormat::Float2,0},
        {"TEXCOORD",1,VertexFormat::Float2,8}},
        {{"SourceColor",ShaderResourceKind::SampledTexture,0,kFragmentStage}}};
    case ShaderProgramId::Terrain:return terrain_shader_layout();
    case ShaderProgramId::Wildfire:return wildfire_shader_layout();
    case ShaderProgramId::Object:return object_shader_layout();
    case ShaderProgramId::TexturedObject:return textured_object_shader_layout();
    case ShaderProgramId::InstancedTexturedObject:return instanced_textured_object_shader_layout();
    case ShaderProgramId::PbrObject:return pbr_textured_object_shader_layout(false);
    case ShaderProgramId::PbrInstanced:return pbr_textured_object_shader_layout(true);
    case ShaderProgramId::Water:return water_shader_layout();
    case ShaderProgramId::Vegetation:return vegetation_shader_layout();
    case ShaderProgramId::Fire:return fire_shader_layout();
    default:throw std::invalid_argument("shader program has no built-in layout");
  }
}
void validate_pipeline_desc(const PipelineDesc& desc,bool allow_contract_only) {
  validate_shader_layout(desc.layout);
  if(desc.program==ShaderProgramId::Composite &&
     (desc.depth_test || desc.depth_write || desc.alpha_blend))
    throw std::invalid_argument("composite pipeline requires disabled depth and blend state");
  if(desc.alpha_blend && (!desc.depth_test || desc.depth_write ||
     (desc.program!=ShaderProgramId::PbrObject && desc.program!=ShaderProgramId::Water && desc.program!=ShaderProgramId::Fire)))
    throw std::invalid_argument("transparent pipeline requires noninstanced PBR, water or fire, depth testing and no depth writes");
  if((desc.program==ShaderProgramId::Water || desc.program==ShaderProgramId::Fire) &&
     (!desc.alpha_blend || !desc.depth_test || desc.depth_write))
    throw std::invalid_argument("water and fire pipelines require alpha blending, depth testing and no depth writes");
  if(desc.program==ShaderProgramId::ContractOnly) {
    if(!allow_contract_only)throw std::invalid_argument("contract-only shader program cannot render on a GPU");
    return;
  }
  validate_shader_reflection(shader_program_layout(desc.program),desc.layout);
}
PipelineHandle ValidatingRenderDevice::create_pipeline(PipelineDesc desc) {
  validate_pipeline_desc(desc);
  const auto id = allocate_id();
  pipelines_.emplace(id, std::move(desc));
  return {id};
}

void ValidatingRenderDevice::destroy_pipeline(PipelineHandle handle) {
  if (pipelines_.erase(handle.id) != 1)
    throw std::invalid_argument("unknown or already destroyed pipeline handle");
}

void ValidatingRenderDevice::draw_indexed(const DrawIndexed& command) {
  if(!active_pass_id_)throw std::logic_error("drawing requires an active render pass");
  const auto pipeline = pipelines_.find(command.pipeline.id);
  const auto vb = buffers_.find(command.vertex_buffer.id);
  const auto ib = buffers_.find(command.index_buffer.id);
  if (pipeline == pipelines_.end() || vb == buffers_.end() || ib == buffers_.end())
    throw std::invalid_argument("draw references nonexistent graphics resources");
  if(command.instance_count==0 || command.instance_count>65535)
    throw std::invalid_argument("instance count must be 1..65535");
  const auto& layout=pipeline->second.layout;
  const bool instanced=std::any_of(layout.bindings.begin(),layout.bindings.end(),
      [](const ShaderBinding& b){return b.kind==ShaderResourceKind::StorageBuffer &&
                                      b.name=="InstanceData" && b.slot==4;});
  if(!instanced && command.instance_count!=1)
    throw std::invalid_argument("multiple instances require an instanced shader");
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

  for(const auto& required:layout.bindings)if(required.kind==ShaderResourceKind::UniformBuffer && required.name=="LayerVisibility") {
    const auto bound=std::find_if(command.uniform_buffers.begin(),command.uniform_buffers.end(),
        [&](const auto& value){return value.slot==required.slot;});
    if(bound==command.uniform_buffers.end() || buffers_.at(bound->buffer.id).desc.size_bytes<16)
      throw std::invalid_argument("LayerVisibility requires a bound 16-byte uniform buffer");
  }
  // GPU backends require every declared block; here a bound style block must
  // at least hold its std140 vec4s so a short buffer never reaches a driver.
  for(const auto& required:layout.bindings)if(required.kind==ShaderResourceKind::UniformBuffer &&
      (required.name=="TerrainStyle" || required.name=="WaterStyle" || required.name=="VegetationStyle" || required.name=="FireStyle")) {
    const auto bound=std::find_if(command.uniform_buffers.begin(),command.uniform_buffers.end(),
        [&](const auto& value){return value.slot==required.slot;});
    const auto minimum=required.name=="TerrainStyle"?kTerrainStyleBytes:
                       required.name=="WaterStyle"?kWaterStyleBytes:
                       required.name=="VegetationStyle"?kVegetationStyleBytes:kFireStyleBytes;
    if(bound!=command.uniform_buffers.end() && buffers_.at(bound->buffer.id).desc.size_bytes<minimum)
      throw std::invalid_argument(required.name+" requires a "+std::to_string(minimum)+"-byte uniform buffer");
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
  if(instanced) {
    const auto item=std::find_if(command.storage_buffers.begin(),command.storage_buffers.end(),
      [](const StorageBufferBinding& binding){return binding.slot==4;});
    if(item==command.storage_buffers.end())
      throw std::invalid_argument("missing instanced object storage");
    const auto& bytes=buffers_.at(item->buffer.id).bytes;
    // std430 record per instance: mat4+mat4+vec4 for objects, three vec4s for vegetation.
    const std::size_t object_stride=pipeline->second.program==ShaderProgramId::Vegetation?kVegetationInstanceBytes:
                                    pipeline->second.program==ShaderProgramId::Fire?kFireInstanceBytes:144;
    if(command.instance_count>bytes.size()/object_stride)
      throw std::out_of_range("instance count exceeds storage buffer capacity");
  }
  std::unordered_set<std::uint32_t> texture_slots;
  for (const auto& bound : command.textures) {
    if (!texture_slots.insert(bound.slot).second)
      throw std::invalid_argument("duplicate texture slot");
    if (!textures_.contains(bound.texture.id))
      throw std::invalid_argument("unknown or destroyed texture binding");
    const auto owner=borrowed_colors_.find(bound.texture.id);
    if(owner!=borrowed_colors_.end() && owner->second==active_target_)
      throw std::invalid_argument("cannot sample the active render target color");
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
