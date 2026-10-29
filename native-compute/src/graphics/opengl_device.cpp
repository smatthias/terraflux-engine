#include <terraflux/graphics/opengl_device.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/glcorearb.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terraflux::graphics {
namespace {

template <class Procedure> Procedure get_proc(const char* name) {
  const auto proc = glfwGetProcAddress(name);
  if (!proc) throw std::runtime_error(std::string("missing OpenGL procedure: ") + name);
  return reinterpret_cast<Procedure>(proc);
}

struct GL {
  PFNGLGENBUFFERSPROC GenBuffers = get_proc<PFNGLGENBUFFERSPROC>("glGenBuffers");
  PFNGLDELETEBUFFERSPROC DeleteBuffers = get_proc<PFNGLDELETEBUFFERSPROC>("glDeleteBuffers");
  PFNGLBINDBUFFERPROC BindBuffer = get_proc<PFNGLBINDBUFFERPROC>("glBindBuffer");
  PFNGLBUFFERDATAPROC BufferData = get_proc<PFNGLBUFFERDATAPROC>("glBufferData");
  PFNGLBUFFERSUBDATAPROC BufferSubData = get_proc<PFNGLBUFFERSUBDATAPROC>("glBufferSubData");
  PFNGLBINDBUFFERBASEPROC BindBufferBase = get_proc<PFNGLBINDBUFFERBASEPROC>("glBindBufferBase");
  PFNGLGENVERTEXARRAYSPROC GenVertexArrays = get_proc<PFNGLGENVERTEXARRAYSPROC>("glGenVertexArrays");
  PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays = get_proc<PFNGLDELETEVERTEXARRAYSPROC>("glDeleteVertexArrays");
  PFNGLBINDVERTEXARRAYPROC BindVertexArray = get_proc<PFNGLBINDVERTEXARRAYPROC>("glBindVertexArray");
  PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = get_proc<PFNGLENABLEVERTEXATTRIBARRAYPROC>("glEnableVertexAttribArray");
  PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = get_proc<PFNGLVERTEXATTRIBPOINTERPROC>("glVertexAttribPointer");
  PFNGLCREATESHADERPROC CreateShader = get_proc<PFNGLCREATESHADERPROC>("glCreateShader");
  PFNGLSHADERSOURCEPROC ShaderSource = get_proc<PFNGLSHADERSOURCEPROC>("glShaderSource");
  PFNGLCOMPILESHADERPROC CompileShader = get_proc<PFNGLCOMPILESHADERPROC>("glCompileShader");
  PFNGLGETSHADERIVPROC GetShaderiv = get_proc<PFNGLGETSHADERIVPROC>("glGetShaderiv");
  PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = get_proc<PFNGLGETSHADERINFOLOGPROC>("glGetShaderInfoLog");
  PFNGLDELETESHADERPROC DeleteShader = get_proc<PFNGLDELETESHADERPROC>("glDeleteShader");
  PFNGLCREATEPROGRAMPROC CreateProgram = get_proc<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
  PFNGLATTACHSHADERPROC AttachShader = get_proc<PFNGLATTACHSHADERPROC>("glAttachShader");
  PFNGLLINKPROGRAMPROC LinkProgram = get_proc<PFNGLLINKPROGRAMPROC>("glLinkProgram");
  PFNGLGETPROGRAMIVPROC GetProgramiv = get_proc<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
  PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = get_proc<PFNGLGETPROGRAMINFOLOGPROC>("glGetProgramInfoLog");
  PFNGLDELETEPROGRAMPROC DeleteProgram = get_proc<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
  PFNGLUSEPROGRAMPROC UseProgram = get_proc<PFNGLUSEPROGRAMPROC>("glUseProgram");
  PFNGLGETPROGRAMINTERFACEIVPROC GetProgramInterfaceiv = get_proc<PFNGLGETPROGRAMINTERFACEIVPROC>("glGetProgramInterfaceiv");
  PFNGLGETPROGRAMRESOURCEIVPROC GetProgramResourceiv = get_proc<PFNGLGETPROGRAMRESOURCEIVPROC>("glGetProgramResourceiv");
  PFNGLGETPROGRAMRESOURCENAMEPROC GetProgramResourceName = get_proc<PFNGLGETPROGRAMRESOURCENAMEPROC>("glGetProgramResourceName");
  PFNGLDRAWELEMENTSPROC DrawElements = get_proc<PFNGLDRAWELEMENTSPROC>("glDrawElements");
  PFNGLENABLEPROC Enable = get_proc<PFNGLENABLEPROC>("glEnable");
  PFNGLDISABLEPROC Disable = get_proc<PFNGLDISABLEPROC>("glDisable");
  PFNGLCLEARCOLORPROC ClearColor = get_proc<PFNGLCLEARCOLORPROC>("glClearColor");
  PFNGLCLEARPROC Clear = get_proc<PFNGLCLEARPROC>("glClear");
  PFNGLVIEWPORTPROC Viewport = get_proc<PFNGLVIEWPORTPROC>("glViewport");
};

constexpr char kVertexShader[] = R"GLSL(#version 430 core
layout(location = 0) in vec3 POSITION;
layout(location = 1) in vec3 NORMAL;
layout(location = 2) in float FIRE_STATE;
layout(std140, binding = 0) uniform Camera { mat4 viewProjection; };
out vec3 surfaceNormal;
out float elevation;
out float fireState;
void main() {
  fireState = FIRE_STATE;
  surfaceNormal = NORMAL;
  elevation = POSITION.y;
  gl_Position = viewProjection * vec4(POSITION, 1.0);
}
)GLSL";

constexpr char kFragmentShader[] = R"GLSL(#version 430 core
in vec3 surfaceNormal;
in float elevation;
in float fireState;
out vec4 fragColor;
void main() {
  vec3 light = normalize(vec3(0.45, 0.85, 0.25));
  float intensity = 0.30 + 0.70 * max(0.0, dot(normalize(surfaceNormal), light));
  vec3 lowland = vec3(0.11, 0.33, 0.20);
  vec3 highland = vec3(0.60, 0.55, 0.42);
  vec3 terrainColor = mix(lowland, highland, clamp(elevation / 140.0, 0.0, 1.0));
  vec3 base = terrainColor * intensity;
  if (fireState > 0.75) {
    base = vec3(1.0, 0.18, 0.015);  // active fire
  } else if (fireState > 0.25) {
    base = vec3(0.13, 0.115, 0.12); // burned terrain
  }
  fragColor = vec4(base, 1.0);
}
)GLSL";

GLuint compile_shader(const GL& gl, GLenum stage, const char* source) {
  const GLuint shader = gl.CreateShader(stage);
  if (!shader) throw std::runtime_error("glCreateShader failed");
  gl.ShaderSource(shader, 1, &source, nullptr);
  gl.CompileShader(shader);
  GLint ok = 0;
  gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint length = 0;
    gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(1, length)), '\0');
    gl.GetShaderInfoLog(shader, length, nullptr, log.data());
    gl.DeleteShader(shader);
    throw std::runtime_error("GLSL compilation failed: " + log);
  }
  return shader;
}

std::string resource_name(const GL& gl, GLuint program, GLenum interface, GLuint index, GLint length) {
  std::string name(static_cast<std::size_t>(std::max(1, length)), '\0');
  GLsizei written = 0;
  gl.GetProgramResourceName(program, interface, index,
                            static_cast<GLsizei>(name.size()), &written, name.data());
  name.resize(static_cast<std::size_t>(written));
  return name;
}

ShaderLayout reflect_program(const GL& gl, GLuint program, const ShaderLayout& expected) {
  // OpenGL reflects active shader attributes and uniform blocks. Vertex memory
  // stride/offset are VAO properties, not shader reflection: they are checked by
  // validate_shader_layout() and configured explicitly in draw_indexed().
  ShaderLayout actual;
  actual.vertex_stride_bytes = expected.vertex_stride_bytes;

  GLint count = 0;
  gl.GetProgramInterfaceiv(program, GL_PROGRAM_INPUT, GL_ACTIVE_RESOURCES, &count);
  for (GLint i = 0; i < count; ++i) {
    constexpr GLenum props[] = {GL_NAME_LENGTH, GL_LOCATION, GL_TYPE};
    GLint values[3] = {};
    gl.GetProgramResourceiv(program, GL_PROGRAM_INPUT, static_cast<GLuint>(i),
                            3, props, 3, nullptr, values);
    const auto name = resource_name(gl, program, GL_PROGRAM_INPUT,
                                    static_cast<GLuint>(i), values[0]);
    const auto match = std::find_if(expected.attributes.begin(), expected.attributes.end(),
        [&](const VertexAttribute& attr) { return attr.semantic == name; });
    if (match == expected.attributes.end())
      throw std::invalid_argument("unexpected GLSL vertex input: " + name);
    VertexFormat format{};
    switch (values[2]) {
      case GL_FLOAT: format = VertexFormat::Float1; break;
      case GL_FLOAT_VEC2: format = VertexFormat::Float2; break;
      case GL_FLOAT_VEC3: format = VertexFormat::Float3; break;
      case GL_FLOAT_VEC4: format = VertexFormat::Float4; break;
      default: throw std::invalid_argument("unsupported GLSL vertex input type: " + name);
    }
    actual.attributes.push_back({name, static_cast<std::uint32_t>(values[1]),
                                  format, match->offset_bytes});
  }

  gl.GetProgramInterfaceiv(program, GL_UNIFORM_BLOCK, GL_ACTIVE_RESOURCES, &count);
  for (GLint i = 0; i < count; ++i) {
    constexpr GLenum props[] = {
      GL_NAME_LENGTH, GL_BUFFER_BINDING, GL_REFERENCED_BY_VERTEX_SHADER,
      GL_REFERENCED_BY_FRAGMENT_SHADER, GL_BUFFER_DATA_SIZE
    };
    GLint values[5] = {};
    gl.GetProgramResourceiv(program, GL_UNIFORM_BLOCK, static_cast<GLuint>(i),
                            5, props, 5, nullptr, values);
    const auto name = resource_name(gl, program, GL_UNIFORM_BLOCK,
                                    static_cast<GLuint>(i), values[0]);
    if (name == "Camera" && values[4] != 64)
      throw std::runtime_error("Camera UBO does not have one std140 mat4");
    actual.bindings.push_back({name, ShaderResourceKind::UniformBuffer,
                              static_cast<std::uint32_t>(values[1]),
                              (values[2] ? kVertexStage : 0U) |
                              (values[3] ? kFragmentStage : 0U)});
  }
  validate_shader_reflection(expected, actual);
  return actual;
}

GLenum target_for(BufferUsage usage) {
  switch (usage) {
    case BufferUsage::Vertex: return GL_ARRAY_BUFFER;
    case BufferUsage::Index: return GL_ELEMENT_ARRAY_BUFFER;
    case BufferUsage::Uniform: return GL_UNIFORM_BUFFER;
  }
  throw std::invalid_argument("unknown buffer usage");
}

GLint components_for(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float1: return 1;
    case VertexFormat::Float2: return 2;
    case VertexFormat::Float3: return 3;
    case VertexFormat::Float4: return 4;
  }
  throw std::invalid_argument("unsupported vertex format");
}

}  // namespace

struct OpenGLRenderDevice::Impl {
  struct GpuBuffer { GLuint name = 0; BufferUsage usage{}; };
  struct GpuPipeline { GLuint program = 0, vao = 0; PipelineDesc desc; };

  GL gl;
  ValidatingRenderDevice validator;
  std::unordered_map<std::uint32_t, GpuBuffer> buffers;
  std::unordered_map<std::uint32_t, GpuPipeline> pipelines;

  ~Impl() {
    // Destruction requires the OpenGL context used at creation to be current.
    for (const auto& [id, pipeline] : pipelines) {
      (void)id;
      gl.DeleteVertexArrays(1, &pipeline.vao);
      gl.DeleteProgram(pipeline.program);
    }
    for (const auto& [id, buffer] : buffers) {
      (void)id;
      gl.DeleteBuffers(1, &buffer.name);
    }
  }
};

OpenGLRenderDevice::OpenGLRenderDevice() : impl_(std::make_unique<Impl>()) {}
OpenGLRenderDevice::~OpenGLRenderDevice() = default;

BufferHandle OpenGLRenderDevice::create_buffer(BufferDesc desc,
                                                std::span<const std::byte> initial_data) {
  if (desc.size_bytes > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()))
    throw std::overflow_error("GPU buffer size exceeds OpenGL address range");
  const auto handle = impl_->validator.create_buffer(desc, initial_data);
  const GLenum target = target_for(desc.usage);
  GLuint name = 0;
  impl_->gl.GenBuffers(1, &name);
  if (!name) {
    impl_->validator.destroy_buffer(handle);
    throw std::runtime_error("glGenBuffers failed");
  }
  impl_->gl.BindBuffer(target, name);
  impl_->gl.BufferData(target, static_cast<GLsizeiptr>(desc.size_bytes),
                       initial_data.data(), GL_DYNAMIC_DRAW);
  impl_->buffers.emplace(handle.id, Impl::GpuBuffer{name, desc.usage});
  return handle;
}

void OpenGLRenderDevice::update_buffer(BufferHandle handle, std::size_t offset_bytes,
                                       std::span<const std::byte> data) {
  impl_->validator.update_buffer(handle, offset_bytes, data);
  const auto& gpu = impl_->buffers.at(handle.id);
  const GLenum target = target_for(gpu.usage);
  impl_->gl.BindBuffer(target, gpu.name);
  impl_->gl.BufferSubData(target, static_cast<GLintptr>(offset_bytes),
                          static_cast<GLsizeiptr>(data.size()), data.data());
}

void OpenGLRenderDevice::destroy_buffer(BufferHandle handle) {
  impl_->validator.destroy_buffer(handle);
  const GLuint name = impl_->buffers.at(handle.id).name;
  impl_->gl.DeleteBuffers(1, &name);
  impl_->buffers.erase(handle.id);
}

PipelineHandle OpenGLRenderDevice::create_pipeline(PipelineDesc desc) {
  validate_shader_layout(desc.layout);
  for (const auto& binding : desc.layout.bindings)
    if (binding.kind != ShaderResourceKind::UniformBuffer)
      throw std::invalid_argument("terrain OpenGL backend only supports uniform buffers currently");

  GLuint vertex = 0, fragment = 0, program = 0, vao = 0;
  try {
    vertex = compile_shader(impl_->gl, GL_VERTEX_SHADER, kVertexShader);
    fragment = compile_shader(impl_->gl, GL_FRAGMENT_SHADER, kFragmentShader);
    program = impl_->gl.CreateProgram();
    if (!program) throw std::runtime_error("glCreateProgram failed");
    impl_->gl.AttachShader(program, vertex);
    impl_->gl.AttachShader(program, fragment);
    impl_->gl.LinkProgram(program);
    GLint linked = 0;
    impl_->gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
      GLint length = 0;
      impl_->gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
      std::string log(static_cast<std::size_t>(std::max(1, length)), '\0');
      impl_->gl.GetProgramInfoLog(program, length, nullptr, log.data());
      throw std::runtime_error("GLSL linking failed: " + log);
    }
    impl_->gl.DeleteShader(vertex); vertex = 0;
    impl_->gl.DeleteShader(fragment); fragment = 0;
    (void)reflect_program(impl_->gl, program, desc.layout);
    impl_->gl.GenVertexArrays(1, &vao);
    if (!vao) throw std::runtime_error("glGenVertexArrays failed");
    const auto handle = impl_->validator.create_pipeline(desc);
    impl_->pipelines.emplace(handle.id, Impl::GpuPipeline{program, vao, std::move(desc)});
    return handle;
  } catch (...) {
    if (vao) impl_->gl.DeleteVertexArrays(1, &vao);
    if (program) impl_->gl.DeleteProgram(program);
    if (vertex) impl_->gl.DeleteShader(vertex);
    if (fragment) impl_->gl.DeleteShader(fragment);
    throw;
  }
}

void OpenGLRenderDevice::destroy_pipeline(PipelineHandle handle) {
  impl_->validator.destroy_pipeline(handle);
  const auto gpu = impl_->pipelines.at(handle.id);
  impl_->gl.DeleteVertexArrays(1, &gpu.vao);
  impl_->gl.DeleteProgram(gpu.program);
  impl_->pipelines.erase(handle.id);
}

void OpenGLRenderDevice::draw_indexed(const DrawIndexed& command) {
  impl_->validator.draw_indexed(command);
  const auto& pipeline = impl_->pipelines.at(command.pipeline.id);
  const auto vertex = impl_->buffers.at(command.vertex_buffer.id);
  const auto index = impl_->buffers.at(command.index_buffer.id);
  impl_->gl.UseProgram(pipeline.program);
  impl_->gl.BindVertexArray(pipeline.vao);
  impl_->gl.BindBuffer(GL_ARRAY_BUFFER, vertex.name);
  impl_->gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index.name);

  const auto& layout = pipeline.desc.layout;
  if (layout.vertex_stride_bytes > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
    throw std::overflow_error("vertex stride exceeds GLsizei");
  for (const auto& attr : layout.attributes) {
    impl_->gl.EnableVertexAttribArray(attr.location);
    impl_->gl.VertexAttribPointer(attr.location, components_for(attr.format),
        GL_FLOAT, GL_FALSE, static_cast<GLsizei>(layout.vertex_stride_bytes),
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(attr.offset_bytes)));
  }

  for (const auto& required : layout.bindings) {
    const auto match = std::find_if(command.uniform_buffers.begin(), command.uniform_buffers.end(),
       [&](const UniformBufferBinding& bound) { return bound.slot == required.slot; });
    if (required.kind == ShaderResourceKind::UniformBuffer && match == command.uniform_buffers.end())
      throw std::invalid_argument("missing uniform buffer for shader resource: " + required.name);
  }
  for (const auto& bound : command.uniform_buffers)
    impl_->gl.BindBufferBase(GL_UNIFORM_BUFFER, bound.slot, impl_->buffers.at(bound.buffer.id).name);

  if (pipeline.desc.depth_test) impl_->gl.Enable(GL_DEPTH_TEST);
  else impl_->gl.Disable(GL_DEPTH_TEST);
  impl_->gl.DrawElements(GL_TRIANGLES, static_cast<GLsizei>(command.index_count),
      GL_UNSIGNED_INT, reinterpret_cast<const void*>(
          static_cast<std::uintptr_t>(command.first_index) * sizeof(std::uint32_t)));
}

void OpenGLRenderDevice::clear_frame(int framebuffer_width, int framebuffer_height) {
  if (framebuffer_width <= 0 || framebuffer_height <= 0) return;
  impl_->gl.Viewport(0, 0, framebuffer_width, framebuffer_height);
  impl_->gl.ClearColor(0.065f, 0.095f, 0.13f, 1.0f);
  impl_->gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

}  // namespace terraflux::graphics
