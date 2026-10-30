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
#include <optional>
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
  PFNGLGETUNIFORMIVPROC GetUniformiv = get_proc<PFNGLGETUNIFORMIVPROC>("glGetUniformiv");
  PFNGLGENTEXTURESPROC GenTextures = get_proc<PFNGLGENTEXTURESPROC>("glGenTextures");
  PFNGLDELETETEXTURESPROC DeleteTextures = get_proc<PFNGLDELETETEXTURESPROC>("glDeleteTextures");
  PFNGLBINDTEXTUREPROC BindTexture = get_proc<PFNGLBINDTEXTUREPROC>("glBindTexture");
  PFNGLACTIVETEXTUREPROC ActiveTexture = get_proc<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
  PFNGLTEXIMAGE2DPROC TexImage2D = get_proc<PFNGLTEXIMAGE2DPROC>("glTexImage2D");
  PFNGLTEXPARAMETERIPROC TexParameteri = get_proc<PFNGLTEXPARAMETERIPROC>("glTexParameteri");
  PFNGLGENERATEMIPMAPPROC GenerateMipmap = get_proc<PFNGLGENERATEMIPMAPPROC>("glGenerateMipmap");
  PFNGLDRAWELEMENTSPROC DrawElements = get_proc<PFNGLDRAWELEMENTSPROC>("glDrawElements");
  PFNGLDRAWELEMENTSINSTANCEDPROC DrawElementsInstanced =
      get_proc<PFNGLDRAWELEMENTSINSTANCEDPROC>("glDrawElementsInstanced");
  PFNGLENABLEPROC Enable = get_proc<PFNGLENABLEPROC>("glEnable");
  PFNGLDISABLEPROC Disable = get_proc<PFNGLDISABLEPROC>("glDisable");
  PFNGLCLEARCOLORPROC ClearColor = get_proc<PFNGLCLEARCOLORPROC>("glClearColor");
  PFNGLCLEARPROC Clear = get_proc<PFNGLCLEARPROC>("glClear");
  PFNGLVIEWPORTPROC Viewport = get_proc<PFNGLVIEWPORTPROC>("glViewport");
  PFNGLREADPIXELSPROC ReadPixels = get_proc<PFNGLREADPIXELSPROC>("glReadPixels");
  PFNGLGENQUERIESPROC GenQueries = get_proc<PFNGLGENQUERIESPROC>("glGenQueries");
  PFNGLDELETEQUERIESPROC DeleteQueries = get_proc<PFNGLDELETEQUERIESPROC>("glDeleteQueries");
  PFNGLBEGINQUERYPROC BeginQuery = get_proc<PFNGLBEGINQUERYPROC>("glBeginQuery");
  PFNGLENDQUERYPROC EndQuery = get_proc<PFNGLENDQUERYPROC>("glEndQuery");
  PFNGLGETQUERYOBJECTIVPROC GetQueryObjectiv = get_proc<PFNGLGETQUERYOBJECTIVPROC>("glGetQueryObjectiv");
  PFNGLGETQUERYOBJECTUI64VPROC GetQueryObjectui64v = get_proc<PFNGLGETQUERYOBJECTUI64VPROC>("glGetQueryObjectui64v");
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


constexpr char kFireVertexShader[] = R"GLSL(#version 430 core
layout(location = 0) in vec3 POSITION;
layout(location = 1) in vec3 NORMAL;
layout(std140, binding = 0) uniform Camera {
  mat4 viewProjection;
  vec4 fireGrid;
};
out vec3 surfaceNormal;
out float elevation;
out vec2 localGridMeters;
void main() {
  surfaceNormal = NORMAL;
  elevation = POSITION.y;
  localGridMeters = vec2(POSITION.x, -POSITION.z);
  gl_Position = viewProjection * vec4(POSITION, 1.0);
}
)GLSL";

constexpr char kFireFragmentShader[] = R"GLSL(#version 430 core
in vec3 surfaceNormal;
in float elevation;
in vec2 localGridMeters;
layout(std140, binding = 0) uniform Camera {
  mat4 viewProjection;
  vec4 fireGrid; // x = cell size, y = width, z = height, w = animation seconds
};
layout(std430, binding = 1) readonly buffer FireState { uint fireStates[]; };
out vec4 fragColor;

void main() {
  vec3 sun = normalize(vec3(0.45, 0.85, 0.25));
  float light = 0.30 + 0.70 * max(0.0, dot(normalize(surfaceNormal), sun));
  vec3 ground = mix(vec3(0.11, 0.33, 0.20), vec3(0.60, 0.55, 0.42),
                    clamp(elevation / 140.0, 0.0, 1.0)) * light;

  // Each cell contributes a rounded radial footprint, not a square block.
  // Look up neighboring cell centers so adjacent active fires merge smoothly.
  // Out-of-grid centers and holes never contribute; rasterization is already
  // clipped to the real terrain mesh, so no color can appear beyond its edge.
  vec2 gridPos = localGridMeters / fireGrid.x;
  ivec2 nearest = ivec2(floor(gridPos + vec2(0.5)));
  ivec2 dimensions = ivec2(fireGrid.yz);
  float burn = 0.0, flame = 0.0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      ivec2 cell = nearest + ivec2(dx, dy);
      if (cell.x < 0 || cell.y < 0 ||
          cell.x >= dimensions.x || cell.y >= dimensions.y) continue;
      uint state = fireStates[cell.y * dimensions.x + cell.x];
      if (state == 0u || state == 3u) continue;
      vec2 delta = gridPos - vec2(cell);
      // Organic boundary distortion is small enough to keep the fire local.
      float wobble = 0.065 * sin(11.0 * delta.x + 9.0 * delta.y +
                                  2.4 * fireGrid.w) *
                                   cos(8.0 * delta.y - 1.5 * fireGrid.w);
      float radius = length(delta) + wobble;
      if (state == 2u)
        flame = max(flame, 1.0 - smoothstep(0.48, 0.83, radius));
      else if (state == 1u)
        burn = max(burn, 1.0 - smoothstep(0.55, 0.86, radius));
    }
  }
  ground = mix(ground, vec3(0.13, 0.12, 0.13), 0.93 * burn);
  float pulse = 0.86 + 0.14 * sin(5.0 * fireGrid.w + gridPos.x * 1.3);
  vec3 fireColor = mix(vec3(0.79, 0.075, 0.01),
                       vec3(1.0, 0.46, 0.025), clamp(flame, 0.0, 1.0));
  ground = mix(ground, fireColor * pulse, flame);
  fragColor = vec4(ground, 1.0);
}
)GLSL";


constexpr char kObjectVertexShader[] = R"GLSL(#version 430 core
layout(location = 0) in vec3 POSITION;
layout(location = 1) in vec3 NORMAL;
layout(std140, binding = 0) uniform Camera { mat4 viewProjection; };
layout(std140, binding = 2) uniform Object {
  mat4 model;
  mat4 normalMatrix;
  vec4 tint;
};
out vec3 surfaceNormal;
out vec3 surfaceTint;
void main() {
  surfaceNormal = normalize(mat3(normalMatrix) * NORMAL);
  surfaceTint = tint.rgb;
  gl_Position = viewProjection * model * vec4(POSITION, 1.0);
}
)GLSL";

constexpr char kObjectFragmentShader[] = R"GLSL(#version 430 core
in vec3 surfaceNormal;
in vec3 surfaceTint;
out vec4 fragColor;
void main() {
  vec3 sunlight = normalize(vec3(0.45, 0.85, 0.25));
  float shade = 0.28 + 0.72 * max(dot(normalize(surfaceNormal), sunlight), 0.0);
  fragColor = vec4(surfaceTint * shade, 1.0);
}
)GLSL";

constexpr char kTexturedVertexShader[] = R"GLSL(#version 430 core
layout(location=0) in vec3 POSITION;
layout(location=1) in vec3 NORMAL;
layout(location=3) in vec2 TEXCOORD;
layout(std140,binding=0) uniform Camera { mat4 viewProjection; };
layout(std140,binding=2) uniform Object {
  mat4 model;
  mat4 normalMatrix;
  vec4 tint;
};
out vec3 surfaceNormal;
out vec3 surfaceTint;
out vec2 textureUv;
void main() {
  surfaceNormal = normalize(mat3(normalMatrix)*NORMAL);
  surfaceTint = tint.rgb;
  textureUv = TEXCOORD;
  gl_Position = viewProjection*model*vec4(POSITION,1.0);
}
)GLSL";
constexpr char kInstancedTexturedVertexShader[] = R"GLSL(#version 430 core
layout(location=0) in vec3 POSITION;
layout(location=1) in vec3 NORMAL;
layout(location=3) in vec2 TEXCOORD;
layout(std140,binding=0) uniform Camera { mat4 viewProjection; };
struct InstanceRecord {
  mat4 model;
  mat4 normalMatrix;
  vec4 tint;
};
layout(std430,binding=4) readonly buffer InstanceData {
  InstanceRecord instances[];
};
out vec3 surfaceNormal;
out vec3 surfaceTint;
out vec2 textureUv;
void main() {
  InstanceRecord item=instances[gl_InstanceID];
  surfaceNormal=normalize(mat3(item.normalMatrix)*NORMAL);
  surfaceTint=item.tint.rgb;
  textureUv=TEXCOORD;
  gl_Position=viewProjection*item.model*vec4(POSITION,1.0);
}
)GLSL";

constexpr char kTexturedFragmentShader[] = R"GLSL(#version 430 core
in vec3 surfaceNormal;
in vec3 surfaceTint;
in vec2 textureUv;
layout(binding=3) uniform sampler2D AlbedoTexture;
out vec4 fragColor;
void main() {
  vec3 sunlight=normalize(vec3(0.45,0.85,0.25));
  float shade=0.28+0.72*max(dot(normalize(surfaceNormal),sunlight),0.0);
  vec3 albedo=texture(AlbedoTexture,textureUv).rgb;
  fragColor=vec4(albedo*surfaceTint*shade,1.0);
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
    // Some Mesa drivers expose gl_InstanceID in PROGRAM_INPUT reflection.
    // Built-in gl_* variables are not application vertex attributes.
    if (name.starts_with("gl_")) continue;
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
    const auto is_fire = std::any_of(expected.bindings.begin(), expected.bindings.end(),
        [](const ShaderBinding& binding) {
          return binding.kind == ShaderResourceKind::StorageBuffer &&
                 binding.name == "FireState";
        });
    if (name == "Camera" && values[4] != (is_fire ? 80 : 64))
      throw std::runtime_error("Camera UBO does not match expected std140 size");
    if (name == "Object" && values[4] != 144)
      throw std::runtime_error("Object UBO must contain model, normalMatrix and tint");
    actual.bindings.push_back({name, ShaderResourceKind::UniformBuffer,
                              static_cast<std::uint32_t>(values[1]),
                              (values[2] ? kVertexStage : 0U) |
                              (values[3] ? kFragmentStage : 0U)});
  }
  gl.GetProgramInterfaceiv(program, GL_SHADER_STORAGE_BLOCK, GL_ACTIVE_RESOURCES, &count);
  for (GLint i = 0; i < count; ++i) {
    constexpr GLenum props[] = {
      GL_NAME_LENGTH, GL_BUFFER_BINDING,
      GL_REFERENCED_BY_VERTEX_SHADER, GL_REFERENCED_BY_FRAGMENT_SHADER
    };
    GLint values[4] = {};
    gl.GetProgramResourceiv(program, GL_SHADER_STORAGE_BLOCK, static_cast<GLuint>(i),
                            4, props, 4, nullptr, values);
    const auto name = resource_name(gl, program, GL_SHADER_STORAGE_BLOCK,
                                    static_cast<GLuint>(i), values[0]);
    std::uint32_t stages = (values[2] ? kVertexStage : 0U) |
                           (values[3] ? kFragmentStage : 0U);
    if (stages == 0 && (name == "FireState" || name == "InstanceData")) {
      // Certain Mesa software drivers report no stage flag on the block
      // even when it is active. Confirm that the block has active variables;
      // its known GLSL source is compiled only into the fragment stage.
      constexpr GLenum active_property[] = {GL_NUM_ACTIVE_VARIABLES};
      GLint active_variables = 0;
      gl.GetProgramResourceiv(program, GL_SHADER_STORAGE_BLOCK,
                              static_cast<GLuint>(i), 1, active_property,
                              1, nullptr, &active_variables);
      if (active_variables > 0) stages = name=="InstanceData" ? kVertexStage : kFragmentStage;
    }
    actual.bindings.push_back({name, ShaderResourceKind::StorageBuffer,
                              static_cast<std::uint32_t>(values[1]), stages});
  }
  // Active sampler2D uniforms are not UBOs: reflect their texture slots.
  gl.GetProgramInterfaceiv(program, GL_UNIFORM, GL_ACTIVE_RESOURCES, &count);
  for (GLint i=0;i<count;++i) {
    constexpr GLenum props[] = {
      GL_NAME_LENGTH,GL_TYPE,GL_LOCATION,GL_BLOCK_INDEX,
      GL_REFERENCED_BY_FRAGMENT_SHADER
    };
    GLint values[5]{};
    gl.GetProgramResourceiv(program,GL_UNIFORM,static_cast<GLuint>(i),
                            5,props,5,nullptr,values);
    if (values[1]!=GL_SAMPLER_2D || values[3]!=-1) continue;
    const auto name=resource_name(gl,program,GL_UNIFORM,
                                   static_cast<GLuint>(i),values[0]);
    GLint slot=-1;
    gl.GetUniformiv(program,values[2],&slot);
    if (slot<0 || !values[4])
      throw std::invalid_argument("inactive or invalid sampler");
    actual.bindings.push_back({name,ShaderResourceKind::SampledTexture,
                               static_cast<std::uint32_t>(slot),kFragmentStage});
  }
  validate_shader_reflection(expected, actual);
  return actual;
}

GLenum target_for(BufferUsage usage) {
  switch (usage) {
    case BufferUsage::Vertex: return GL_ARRAY_BUFFER;
    case BufferUsage::Index: return GL_ELEMENT_ARRAY_BUFFER;
    case BufferUsage::Uniform: return GL_UNIFORM_BUFFER;
    case BufferUsage::Storage: return GL_SHADER_STORAGE_BUFFER;
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
  struct GpuBuffer { GLuint name=0; BufferUsage usage{}; std::size_t bytes=0; };
  struct GpuTexture { GLuint name = 0; };
  struct GpuPipeline { GLuint program = 0, vao = 0; PipelineDesc desc; };

  GL gl;
  ValidatingRenderDevice validator;
  std::unordered_map<std::uint32_t, GpuBuffer> buffers;
  std::unordered_map<std::uint32_t, GpuTexture> textures;
  std::unordered_map<std::uint32_t, GpuPipeline> pipelines;

  // RenderDevice is the sole owner of GL state within this viewer context.
  // Cached binding state prevents redundant program, VAO/attribute and
  // material texture operations across sorted opaque draw submissions.
  GLuint current_program=0,current_vao=0;
  std::uint32_t attribute_pipeline=0,attribute_vertex=0,attribute_index=0;
  std::unordered_map<std::uint32_t,GLuint> texture_units;
  std::optional<bool> depth_test_state;

  static constexpr std::size_t kGpuQuerySlots=8;
  std::array<GLuint,kGpuQuerySlots> gpu_queries{};
  std::array<bool,kGpuQuerySlots> query_pending{};
  bool timing_enabled=false;
  std::optional<std::size_t> active_query;
  std::size_t gpu_samples=0,gpu_skipped=0;
  double gpu_total_ms=0,gpu_min_ms=0,gpu_max_ms=0;

  void collect_gpu_samples(bool drain) {
    for(std::size_t i=0;i<kGpuQuerySlots;++i) {
      if(!query_pending[i])continue;
      if(!drain) {
        GLint ready=0;
        gl.GetQueryObjectiv(gpu_queries[i],GL_QUERY_RESULT_AVAILABLE,&ready);
        if(!ready)continue;
      }
      GLuint64 nanos=0;
      gl.GetQueryObjectui64v(gpu_queries[i],GL_QUERY_RESULT,&nanos);
      const double ms=static_cast<double>(nanos)*1e-6;
      if(gpu_samples==0 || ms<gpu_min_ms)gpu_min_ms=ms;
      gpu_max_ms=std::max(gpu_max_ms,ms);
      gpu_total_ms+=ms;
      ++gpu_samples;
      query_pending[i]=false;
    }
  }

  ~Impl() {
    // Destruction requires the OpenGL context used at creation to be current.
    if(timing_enabled)gl.DeleteQueries(static_cast<GLsizei>(kGpuQuerySlots),
                                       gpu_queries.data());
    for (const auto& [id, pipeline] : pipelines) {
      (void)id;
      gl.DeleteVertexArrays(1, &pipeline.vao);
      gl.DeleteProgram(pipeline.program);
    }
    for (const auto& [id, texture] : textures) {
      (void)id;
      gl.DeleteTextures(1,&texture.name);
    }
    for (const auto& [id, buffer] : buffers) {
      (void)id;
      gl.DeleteBuffers(1, &buffer.name);
    }
  }
};

OpenGLRenderDevice::OpenGLRenderDevice() : impl_(std::make_unique<Impl>()) {}
OpenGLRenderDevice::~OpenGLRenderDevice() = default;

void OpenGLRenderDevice::enable_gpu_timing() {
  if(impl_->timing_enabled)
    throw std::logic_error("GPU timing already enabled");
  impl_->gl.GenQueries(static_cast<GLsizei>(Impl::kGpuQuerySlots),
                       impl_->gpu_queries.data());
  if(std::any_of(impl_->gpu_queries.begin(),impl_->gpu_queries.end(),
                 [](GLuint q){return q==0;})) {
    impl_->gl.DeleteQueries(static_cast<GLsizei>(Impl::kGpuQuerySlots),
                            impl_->gpu_queries.data());
    throw std::runtime_error("GPU timestamp queries unavailable");
  }
  impl_->timing_enabled=true;
}
void OpenGLRenderDevice::begin_gpu_timing() {
  if(!impl_->timing_enabled)
    throw std::logic_error("GPU timing not enabled");
  if(impl_->active_query)
    throw std::logic_error("GPU query already active");
  impl_->collect_gpu_samples(false);
  for(std::size_t i=0;i<Impl::kGpuQuerySlots;++i) {
    if(impl_->query_pending[i])continue;
    impl_->active_query=i;
    impl_->gl.BeginQuery(GL_TIME_ELAPSED,impl_->gpu_queries[i]);
    return;
  }
  ++impl_->gpu_skipped;
}
void OpenGLRenderDevice::end_gpu_timing() {
  if(!impl_->timing_enabled)
    throw std::logic_error("GPU timing not enabled");
  if(impl_->active_query) {
    impl_->gl.EndQuery(GL_TIME_ELAPSED);
    impl_->query_pending[*impl_->active_query]=true;
    impl_->active_query.reset();
  }
}
OpenGLRenderDevice::GpuTimingSummary OpenGLRenderDevice::gpu_timing_summary(bool drain) {
  if(!impl_->timing_enabled)
    return {};
  if(drain && impl_->active_query)
    throw std::logic_error("cannot drain an active GPU query");
  impl_->collect_gpu_samples(drain);
  return {impl_->gpu_samples,impl_->gpu_skipped,
          impl_->gpu_samples?impl_->gpu_total_ms/impl_->gpu_samples:0,
          impl_->gpu_min_ms,impl_->gpu_max_ms};
}

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
  impl_->buffers.emplace(handle.id, Impl::GpuBuffer{name, desc.usage, desc.size_bytes});
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

void OpenGLRenderDevice::orphan_storage_buffer(BufferHandle handle) {
  // Validate before calling OpenGL; BufferData(nullptr) creates fresh
  // backing storage so draws using the old version can finish asynchronously.
  impl_->validator.orphan_storage_buffer(handle);
  const auto& gpu=impl_->buffers.at(handle.id);
  impl_->gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,gpu.name);
  impl_->gl.BufferData(GL_SHADER_STORAGE_BUFFER,static_cast<GLsizeiptr>(gpu.bytes),
                       nullptr,GL_STREAM_DRAW);
}

void OpenGLRenderDevice::destroy_buffer(BufferHandle handle) {
  impl_->validator.destroy_buffer(handle);
  const GLuint name = impl_->buffers.at(handle.id).name;
  impl_->gl.DeleteBuffers(1, &name);
  impl_->buffers.erase(handle.id);
  if(impl_->attribute_vertex==handle.id || impl_->attribute_index==handle.id) {
    impl_->attribute_pipeline=impl_->attribute_vertex=impl_->attribute_index=0;
  }
}

GLint gl_min_filter(TextureMinFilter filter) {
  switch(filter){
    case TextureMinFilter::Nearest:return GL_NEAREST;
    case TextureMinFilter::Linear:return GL_LINEAR;
    case TextureMinFilter::NearestMipNearest:return GL_NEAREST_MIPMAP_NEAREST;
    case TextureMinFilter::LinearMipNearest:return GL_LINEAR_MIPMAP_NEAREST;
    case TextureMinFilter::NearestMipLinear:return GL_NEAREST_MIPMAP_LINEAR;
    case TextureMinFilter::LinearMipLinear:return GL_LINEAR_MIPMAP_LINEAR;
  }
  throw std::invalid_argument("unsupported texture min filter");
}
GLint gl_mag_filter(TextureMagFilter filter){
  return filter==TextureMagFilter::Linear?GL_LINEAR:GL_NEAREST;
}
GLint gl_wrap(TextureWrap wrap){
  switch(wrap){
    case TextureWrap::Repeat:return GL_REPEAT;
    case TextureWrap::ClampToEdge:return GL_CLAMP_TO_EDGE;
    case TextureWrap::MirroredRepeat:return GL_MIRRORED_REPEAT;
  }
  throw std::invalid_argument("unsupported texture wrap");
}

TextureHandle OpenGLRenderDevice::create_texture2d(
    TextureDesc desc, std::span<const std::byte> rgba8) {
  const auto handle=impl_->validator.create_texture2d(desc,rgba8);
  GLuint name=0;
  impl_->gl.GenTextures(1,&name);
  if (!name) {
    impl_->validator.destroy_texture(handle);
    throw std::runtime_error("glGenTextures failed");
  }
  impl_->gl.BindTexture(GL_TEXTURE_2D,name);
  impl_->texture_units.clear(); // creation modifies the active texture unit
  impl_->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,gl_min_filter(desc.sampling.min));
  impl_->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,gl_mag_filter(desc.sampling.mag));
  impl_->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,gl_wrap(desc.sampling.wrap_s));
  impl_->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,gl_wrap(desc.sampling.wrap_t));
  impl_->gl.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,
                        static_cast<GLsizei>(desc.width),static_cast<GLsizei>(desc.height),
                        0,GL_RGBA,GL_UNSIGNED_BYTE,rgba8.data());
  if(desc.sampling.uses_mipmaps())impl_->gl.GenerateMipmap(GL_TEXTURE_2D);
  impl_->textures.emplace(handle.id,Impl::GpuTexture{name});
  return handle;
}
void OpenGLRenderDevice::destroy_texture(TextureHandle handle) {
  impl_->validator.destroy_texture(handle);
  const GLuint name=impl_->textures.at(handle.id).name;
  impl_->gl.DeleteTextures(1,&name);
  impl_->textures.erase(handle.id);
  impl_->texture_units.clear();
}

PipelineHandle OpenGLRenderDevice::create_pipeline(PipelineDesc desc) {
  validate_shader_layout(desc.layout);
  const bool wildfire = std::any_of(desc.layout.bindings.begin(), desc.layout.bindings.end(),
      [](const ShaderBinding& binding) {
        return binding.kind == ShaderResourceKind::StorageBuffer &&
               binding.name == "FireState";
      });
  const bool object = std::any_of(desc.layout.bindings.begin(), desc.layout.bindings.end(),
      [](const ShaderBinding& binding) {
        return binding.kind == ShaderResourceKind::UniformBuffer &&
               binding.name == "Object" && binding.slot == 2;
      });
  const bool instanced = std::any_of(desc.layout.bindings.begin(),desc.layout.bindings.end(),
      [](const ShaderBinding& binding) {
        return binding.kind==ShaderResourceKind::StorageBuffer &&
               binding.name=="InstanceData" && binding.slot==4;
      });
  const bool textured = std::any_of(desc.layout.bindings.begin(), desc.layout.bindings.end(),
      [](const ShaderBinding& binding) {
        return binding.kind==ShaderResourceKind::SampledTexture &&
               binding.name=="AlbedoTexture" && binding.slot==3;
      });
  if (textured && !object && !instanced)
    throw std::invalid_argument("textured pipeline requires Object or InstanceData");
  if(instanced && (!textured || object || wildfire))
    throw std::invalid_argument("instancing requires textured geometry and no per-object UBO");
  if (object && wildfire)
    throw std::invalid_argument("object and fire shaders are separate pipelines");
  for (const auto& binding : desc.layout.bindings)
    if (binding.kind != ShaderResourceKind::UniformBuffer &&
        !(textured && binding.kind == ShaderResourceKind::SampledTexture &&
          binding.name=="AlbedoTexture") &&
        !(instanced && binding.kind==ShaderResourceKind::StorageBuffer &&
          binding.name=="InstanceData" && binding.slot==4) &&
        !(wildfire && binding.kind == ShaderResourceKind::StorageBuffer &&
          binding.name == "FireState"))
      throw std::invalid_argument("unsupported OpenGL shader resource");

  GLuint vertex = 0, fragment = 0, program = 0, vao = 0;
  try {
    vertex = compile_shader(impl_->gl, GL_VERTEX_SHADER,
                            wildfire ? kFireVertexShader :
                            instanced ? kInstancedTexturedVertexShader :
                            textured ? kTexturedVertexShader :
                            object ? kObjectVertexShader : kVertexShader);
    fragment = compile_shader(impl_->gl, GL_FRAGMENT_SHADER,
                              wildfire ? kFireFragmentShader :
                              textured ? kTexturedFragmentShader :
                              object ? kObjectFragmentShader : kFragmentShader);
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
  if(impl_->current_program==gpu.program) impl_->current_program=0;
  if(impl_->current_vao==gpu.vao) impl_->current_vao=0;
  if(impl_->attribute_pipeline==handle.id)
    impl_->attribute_pipeline=impl_->attribute_vertex=impl_->attribute_index=0;
}

void OpenGLRenderDevice::draw_indexed(const DrawIndexed& command) {
  impl_->validator.draw_indexed(command);
  const auto& pipeline = impl_->pipelines.at(command.pipeline.id);
  const auto vertex = impl_->buffers.at(command.vertex_buffer.id);
  const auto index = impl_->buffers.at(command.index_buffer.id);
  if(impl_->current_program!=pipeline.program) {
    impl_->gl.UseProgram(pipeline.program);
    impl_->current_program=pipeline.program;
  }
  if(impl_->current_vao!=pipeline.vao) {
    impl_->gl.BindVertexArray(pipeline.vao);
    impl_->current_vao=pipeline.vao;
  }
  const auto& layout=pipeline.desc.layout;
  if(layout.vertex_stride_bytes>static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
    throw std::overflow_error("vertex stride exceeds GLsizei");
  if(impl_->attribute_pipeline!=command.pipeline.id ||
     impl_->attribute_vertex!=command.vertex_buffer.id ||
     impl_->attribute_index!=command.index_buffer.id) {
    impl_->gl.BindBuffer(GL_ARRAY_BUFFER,vertex.name);
    impl_->gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,index.name);
    for(const auto& attr:layout.attributes) {
      impl_->gl.EnableVertexAttribArray(attr.location);
      impl_->gl.VertexAttribPointer(attr.location,components_for(attr.format),
          GL_FLOAT,GL_FALSE,static_cast<GLsizei>(layout.vertex_stride_bytes),
          reinterpret_cast<const void*>(static_cast<std::uintptr_t>(attr.offset_bytes)));
    }
    impl_->attribute_pipeline=command.pipeline.id;
    impl_->attribute_vertex=command.vertex_buffer.id;
    impl_->attribute_index=command.index_buffer.id;
  }

  for (const auto& required : layout.bindings) {
    const auto match = std::find_if(command.uniform_buffers.begin(), command.uniform_buffers.end(),
       [&](const UniformBufferBinding& bound) { return bound.slot == required.slot; });
    if (required.kind == ShaderResourceKind::UniformBuffer && match == command.uniform_buffers.end())
      throw std::invalid_argument("missing uniform buffer for shader resource: " + required.name);
  }
  for (const auto& bound : command.uniform_buffers)
    impl_->gl.BindBufferBase(GL_UNIFORM_BUFFER, bound.slot, impl_->buffers.at(bound.buffer.id).name);
  for (const auto& bound : command.storage_buffers)
    impl_->gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, bound.slot,
                             impl_->buffers.at(bound.buffer.id).name);
  for (const auto& bound : command.textures) {
    const GLuint texture=impl_->textures.at(bound.texture.id).name;
    auto it=impl_->texture_units.find(bound.slot);
    if(it==impl_->texture_units.end() || it->second!=texture) {
      impl_->gl.ActiveTexture(GL_TEXTURE0+bound.slot);
      impl_->gl.BindTexture(GL_TEXTURE_2D,texture);
      impl_->texture_units[bound.slot]=texture;
    }
  }

  if(!impl_->depth_test_state || *impl_->depth_test_state!=pipeline.desc.depth_test) {
    if(pipeline.desc.depth_test) impl_->gl.Enable(GL_DEPTH_TEST);
    else impl_->gl.Disable(GL_DEPTH_TEST);
    impl_->depth_test_state=pipeline.desc.depth_test;
  }
  const auto offset=reinterpret_cast<const void*>(
      static_cast<std::uintptr_t>(command.first_index)*sizeof(std::uint32_t));
  if(std::any_of(layout.bindings.begin(),layout.bindings.end(),[](const ShaderBinding& b){
      return b.kind==ShaderResourceKind::StorageBuffer && b.name=="InstanceData";
    })) {
    impl_->gl.DrawElementsInstanced(GL_TRIANGLES,static_cast<GLsizei>(command.index_count),
        GL_UNSIGNED_INT,offset,static_cast<GLsizei>(command.instance_count));
  }else{
    impl_->gl.DrawElements(GL_TRIANGLES,static_cast<GLsizei>(command.index_count),
        GL_UNSIGNED_INT,offset);
  }
}

void OpenGLRenderDevice::clear_frame(int framebuffer_width, int framebuffer_height) {
  if (framebuffer_width <= 0 || framebuffer_height <= 0) return;
  impl_->gl.Viewport(0, 0, framebuffer_width, framebuffer_height);
  impl_->gl.ClearColor(0.065f, 0.095f, 0.13f, 1.0f);
  impl_->gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

std::size_t OpenGLRenderDevice::count_fire_pixels(int width, int height) {
  if (width <= 0 || height <= 0 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > 4'000'000)
    throw std::invalid_argument("invalid framebuffer dimensions for fire validation");
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
  impl_->gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  std::size_t hot_pixels = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    const int red = pixels[i], green = pixels[i + 1], blue = pixels[i + 2];
    if (red >= 175 && red > green * 1.7 && red > blue * 2.0)
      ++hot_pixels;
  }
  return hot_pixels;
}

// Diagnostic only: recognize the saturated blue/purple colors assigned to
// imported objects. The threshold excludes green/brown terrain and the sky.
std::size_t OpenGLRenderDevice::count_asset_pixels(int width, int height) {
  if (width <= 0 || height <= 0 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > 4'000'000)
    throw std::invalid_argument("invalid framebuffer dimensions for asset validation");
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
  impl_->gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  std::size_t asset_pixels = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    const int r = pixels[i], g = pixels[i+1], b = pixels[i+2];
    if (b >= 45 && b * 100 > g * 128 && b * 100 > r * 111)
      ++asset_pixels;
  }
  return asset_pixels;
}

}  // namespace terraflux::graphics
