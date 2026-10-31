#include <terraflux/graphics/opengl_device.hpp>
#include <terraflux/graphics/terrain_shading.hpp>
#include <terraflux/graphics/water_shading.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/graphics/fire_effects.hpp>

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
  PFNGLBLENDFUNCPROC BlendFunc = get_proc<PFNGLBLENDFUNCPROC>("glBlendFunc");
  PFNGLDEPTHMASKPROC DepthMask = get_proc<PFNGLDEPTHMASKPROC>("glDepthMask");
  PFNGLCLEARCOLORPROC ClearColor = get_proc<PFNGLCLEARCOLORPROC>("glClearColor");
  PFNGLCLEARDEPTHPROC ClearDepth = get_proc<PFNGLCLEARDEPTHPROC>("glClearDepth");
  PFNGLBINDFRAMEBUFFERPROC BindFramebuffer = get_proc<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
  PFNGLGENFRAMEBUFFERSPROC GenFramebuffers = get_proc<PFNGLGENFRAMEBUFFERSPROC>("glGenFramebuffers");
  PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers = get_proc<PFNGLDELETEFRAMEBUFFERSPROC>("glDeleteFramebuffers");
  PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus = get_proc<PFNGLCHECKFRAMEBUFFERSTATUSPROC>("glCheckFramebufferStatus");
  PFNGLGENRENDERBUFFERSPROC GenRenderbuffers = get_proc<PFNGLGENRENDERBUFFERSPROC>("glGenRenderbuffers");
  PFNGLDELETERENDERBUFFERSPROC DeleteRenderbuffers = get_proc<PFNGLDELETERENDERBUFFERSPROC>("glDeleteRenderbuffers");
  PFNGLBINDRENDERBUFFERPROC BindRenderbuffer = get_proc<PFNGLBINDRENDERBUFFERPROC>("glBindRenderbuffer");
  PFNGLRENDERBUFFERSTORAGEPROC RenderbufferStorage = get_proc<PFNGLRENDERBUFFERSTORAGEPROC>("glRenderbufferStorage");
  PFNGLFRAMEBUFFERRENDERBUFFERPROC FramebufferRenderbuffer = get_proc<PFNGLFRAMEBUFFERRENDERBUFFERPROC>("glFramebufferRenderbuffer");
  PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D = get_proc<PFNGLFRAMEBUFFERTEXTURE2DPROC>("glFramebufferTexture2D");
  PFNGLGETINTEGERVPROC GetIntegerv = get_proc<PFNGLGETINTEGERVPROC>("glGetIntegerv");
  PFNGLGETERRORPROC GetError = get_proc<PFNGLGETERRORPROC>("glGetError");
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

constexpr char kCompositeVertexShader[] = R"GLSL(#version 430 core
layout(location=0) in vec2 POSITION;
layout(location=1) in vec2 TEXCOORD;
out vec2 uv;
void main() {uv=TEXCOORD;gl_Position=vec4(POSITION,0.0,1.0);}
)GLSL";
constexpr char kCompositeFragmentShader[] = R"GLSL(#version 430 core
in vec2 uv;
layout(binding=0) uniform sampler2D SourceColor;
out vec4 fragColor;
void main() {fragColor=texture(SourceColor,uv);}
)GLSL";

// Ground color shared by the terrain and wildfire fragment shaders. The CPU
// reference terrain_material()/terrain_surface_color() in terrain_shading.cpp
// mirrors terrainHeightTint()/terrainMaterial()/terrainBaseColor() exactly,
// and pixel tests compare the two with detail disabled. Procedural detail and
// contour lines need screen-space derivatives, so they have no CPU twin.
// Deterministic value noise in world meters with analytic gradients; shared
// by the terrain and water shaders. No trigonometric hashing.
constexpr char kNoiseGlsl[] = R"GLSL(
float terrainHash(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
vec3 terrainNoise(vec2 p) { // x = value, yz = analytic gradient
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f), du = 6.0 * f * (1.0 - f);
  float a = terrainHash(i), b = terrainHash(i + vec2(1.0, 0.0));
  float c = terrainHash(i + vec2(0.0, 1.0)), d = terrainHash(i + vec2(1.0, 1.0));
  float k = a - b - c + d;
  return vec3(a + (b - a) * u.x + (c - a) * u.y + k * u.x * u.y,
              du.x * ((b - a) + k * u.y), du.y * ((c - a) + k * u.x));
}
// Frequencies whose wavelength approaches the pixel footprint fade out, so
// distant surfaces stay calm instead of shimmering.
float terrainDetailFade(float pixelMeters, float frequency) {
  return 1.0 - smoothstep(0.25, 1.0, pixelMeters * frequency);
}
)GLSL";

constexpr char kTerrainStyleGlsl[] = R"GLSL(
layout(std140, binding = 11) uniform TerrainStyle {
  vec4 elevationRange; // x min, y max, z 1/(max-min) or 0 when flat, w contour interval (0 = off)
  vec4 slopeStyle;     // x rock start degrees, y rock full degrees, z snow line fraction, w ridge curvature
  vec4 sunLight;       // xyz unit direction toward the light, w ambient shade
  vec4 detail;         // x procedural detail strength (0 = base material only), y rock relief meters, z canopy on ground
  vec4 waterFloor;     // x submerged level, y kind (-1 none, 0 sand, 1 vegetation, 2 mud, 3 stones, 4 mixed), z patch size, w seed offset
  vec4 floorWeights;   // normalized sand, vegetation, mud, stones weights for the mixed floor
};
const vec3 kFloorSand = vec3(0.74, 0.68, 0.50), kFloorVegetation = vec3(0.12, 0.32, 0.16);
const vec3 kFloorMud = vec3(0.32, 0.28, 0.22), kFloorStones = vec3(0.46, 0.44, 0.40);
const float kTwoPiFloor = 6.28318530718;
const vec3 kGrassDry = vec3(0.76, 0.66, 0.36), kGrassSemi = vec3(0.55, 0.58, 0.28), kGrassWet = vec3(0.18, 0.47, 0.20);
const vec3 kTreeDry = vec3(0.42, 0.40, 0.19), kTreeSemi = vec3(0.22, 0.38, 0.15), kTreeWet = vec3(0.11, 0.33, 0.13);
const vec3 kRockLow = vec3(0.40, 0.38, 0.36), kRockHigh = vec3(0.60, 0.58, 0.56), kScree = vec3(0.66, 0.63, 0.58);
const vec3 kSnow = vec3(0.93, 0.94, 0.96), kIce = vec3(0.70, 0.85, 0.94);

vec3 terrainHeightTint(float normalizedHeight) {
  const vec3 c0 = vec3(0.16, 0.40, 0.22); // lowland
  const vec3 c1 = vec3(0.42, 0.58, 0.28); // meadow
  const vec3 c2 = vec3(0.72, 0.64, 0.38); // dry grass
  const vec3 c3 = vec3(0.58, 0.44, 0.32); // upland brown
  const vec3 c4 = vec3(0.88, 0.86, 0.82); // pale summit
  float s = clamp(normalizedHeight, 0.0, 1.0) * 4.0;
  if (s < 1.0) return mix(c0, c1, s);
  if (s < 2.0) return mix(c1, c2, s - 1.0);
  if (s < 3.0) return mix(c2, c3, s - 2.0);
  return mix(c3, c4, s - 3.0);
}

struct TerrainMaterial {
  vec3 ground;   // bare/grass ground albedo
  vec3 canopy;   // tree crown albedo
  vec3 stone;    // rock or scree albedo
  float height, slope, vegetation, trees, rock, ridge, snow, ice, moisture;
  float submerged, depth; // lake/sea floor blend and water depth
  vec4 floor;             // zoned sand, vegetation, mud, stones weights (sum 1)
};

// Mirrors floor_weights() in terrain_shading.cpp.
vec4 floorWeightsFor(float depth, float slope) {
  int kind = int(waterFloor.y);
  if (kind == 0) return vec4(1.0, 0.0, 0.0, 0.0);
  if (kind == 1) return vec4(0.0, 1.0, 0.0, 0.0);
  if (kind == 2) return vec4(0.0, 0.0, 1.0, 0.0);
  if (kind == 3) return vec4(0.0, 0.0, 0.0, 1.0);
  float gentle = 1.0 - smoothstep(12.0, 25.0, slope);
  vec4 w;
  w.x = floorWeights.x * ((1.0 - smoothstep(2.0, 6.0, depth)) * gentle + 0.15);
  w.y = floorWeights.y * 1.2 * smoothstep(0.5, 1.5, depth) * (1.0 - smoothstep(6.0, 12.0, depth)) * (1.0 - smoothstep(14.0, 26.0, slope));
  w.z = floorWeights.z * (smoothstep(3.0, 8.0, depth) * (1.0 - smoothstep(6.0, 14.0, slope)) + 0.05);
  w.w = floorWeights.w * (smoothstep(8.0, 20.0, slope) + 0.10 * (1.0 - smoothstep(1.5, 4.0, depth)));
  float sum = w.x + w.y + w.z + w.w;
  return sum > 1e-9 ? w / sum : vec4(1.0, 0.0, 0.0, 0.0);
}

// surface = (fuel load, fuel moisture, curvature) interpolated from the vertices.
TerrainMaterial terrainMaterial(vec3 unitNormal, float elevation, vec3 surface) {
  TerrainMaterial m;
  m.height = clamp((elevation - elevationRange.x) * elevationRange.z, 0.0, 1.0);
  m.slope = degrees(acos(clamp(unitNormal.y, 0.0, 1.0)));
  float fuel = clamp(surface.x, 0.0, 1.0);
  m.moisture = clamp(surface.y, 0.0, 1.0);
  float convexity = clamp(surface.z / slopeStyle.w, -1.0, 1.0);

  float towardSemi = smoothstep(0.10, 0.20, m.moisture);
  float towardWet = smoothstep(0.26, 0.40, m.moisture);
  vec3 grass = mix(mix(kGrassDry, kGrassSemi, towardSemi), kGrassWet, towardWet);
  m.canopy = mix(grass * 0.55, mix(mix(kTreeDry, kTreeSemi, towardSemi), kTreeWet, towardWet), detail.z);
  m.trees = smoothstep(0.45, 0.80, fuel) * (1.0 - smoothstep(0.62, 0.78, m.height)) *
            (1.0 - smoothstep(30.0, 42.0, m.slope));
  m.vegetation = smoothstep(0.04, 0.25, fuel);
  m.ground = mix(terrainHeightTint(m.height), grass, m.vegetation);

  float steep = smoothstep(slopeStyle.x, slopeStyle.y, m.slope);
  m.ridge = smoothstep(0.20, 0.70, convexity) * smoothstep(0.30, 0.60, m.height);
  m.rock = max(steep, 0.85 * m.ridge);
  m.stone = mix(mix(kRockLow, kRockHigh, m.height), kScree, m.ridge * (1.0 - steep));

  float snowHeight = m.height - 0.12 * m.ridge;
  m.snow = smoothstep(slopeStyle.z - 0.05, slopeStyle.z + 0.05, snowHeight) *
           (1.0 - smoothstep(42.0, 60.0, m.slope));
  if (slopeStyle.z >= 1.0) m.snow = 0.0;
  float level = 1.0 - smoothstep(6.0, 14.0, m.slope);
  float basin = 1.0 - smoothstep(-0.35, -0.05, convexity);
  m.ice = m.snow * level * max(basin, 0.6 * smoothstep(0.93, 0.99, m.height));
  m.submerged = 0.0; m.depth = 0.0; m.floor = vec4(1.0, 0.0, 0.0, 0.0);
  if (waterFloor.y >= 0.0) {
    m.depth = max(waterFloor.x - elevation, 0.0);
    m.submerged = 1.0 - smoothstep(waterFloor.x - 0.3, waterFloor.x + 0.3, elevation);
    m.floor = floorWeightsFor(m.depth, m.slope);
  }
  return m;
}

// Composition order shared with the CPU reference.
vec3 terrainCompose(TerrainMaterial m, vec3 ground, vec3 canopy, vec3 stone, vec3 snow, vec3 ice, float trees,
                    vec3 floorColor) {
  vec3 albedo = mix(mix(ground, canopy, trees * m.vegetation), stone, m.rock);
  albedo = mix(albedo, snow, m.snow);
  albedo = mix(albedo, ice, m.ice);
  return mix(albedo, floorColor, m.submerged);
}
vec3 floorBaseColor(vec4 w) {
  return kFloorSand * w.x + kFloorVegetation * w.y + kFloorMud * w.z + kFloorStones * w.w;
}

vec3 terrainBaseColor(vec3 unitNormal, float elevation, vec3 surface) {
  TerrainMaterial m = terrainMaterial(unitNormal, elevation, surface);
  vec3 albedo = terrainCompose(m, m.ground, m.canopy, m.stone, kSnow, kIce, m.trees, floorBaseColor(m.floor));
  float diffuse = max(dot(unitNormal, sunLight.xyz), 0.0);
  return albedo * (sunLight.w + (1.0 - sunLight.w) * diffuse);
}

// Ridged noise: sharp crests at one, creases at zero, with analytic gradient.
vec3 ridgedNoise(vec2 p) {
  vec3 n = terrainNoise(p);
  float s = 2.0 * n.x - 1.0;
  float v = 1.0 - abs(s);
  vec2 g = -sign(s) * 2.0 * n.yz;
  return vec3(v * v, 2.0 * v * g);
}
vec3 ridgedFbm(vec2 p) { // three octaves; x value in 0..1, yz gradient per unit of p
  vec3 a = ridgedNoise(p), b = ridgedNoise(p * 2.13 + vec2(5.2, 1.3)), c = ridgedNoise(p * 4.41 + vec2(9.1, 7.7));
  return vec3(a.x * 0.55 + b.x * 0.30 + c.x * 0.15,
              a.yz * 0.55 + b.yz * 2.13 * 0.30 + c.yz * 4.41 * 0.15);
}
const float kCragFrequency = 0.045; // crags about 22 m across, with two finer octaves
// Visualization-only vertical relief on rock: up to detail.y meters of ridged
// crags. None at detail zero, damped under snow and ice, none below the water
// line or within two relief heights above it (where it could open gaps under
// the water surface). Vertical only, so grid/fire lookups keep their plan position.
float rockRelief(vec3 unitNormal, float elevation, vec3 surface, vec2 p) {
  if (detail.x <= 0.0 || detail.y <= 0.0) return 0.0;
  TerrainMaterial m = terrainMaterial(unitNormal, elevation, surface);
  float amount = m.rock * (1.0 - 0.6 * max(m.snow, m.ice)) * (1.0 - m.submerged);
  if (waterFloor.y >= 0.0) amount *= smoothstep(waterFloor.x, waterFloor.x + 2.0 * detail.y, elevation);
  return detail.x * detail.y * amount * (ridgedFbm(p * kCragFrequency).x - 0.5) * 2.0;
}
)GLSL";

// Fragment-only detail: needs screen-space derivatives.
constexpr char kTerrainDetailGlsl[] = R"GLSL(

// Lake/sea floor detail: seeded patches break the zoned mix apart; sand shows
// grain and ripple marks, plants grow in clumps over sand, mud is smooth with
// faint mottling, stones are pebbles with dark gaps and a rounded bump.
vec3 floorDetailedColor(TerrainMaterial m, vec2 p, float pixelMeters, float strength, out vec3 bump) {
  vec2 q = p + vec2(waterFloor.w);
  vec4 w = m.floor;
  if (int(waterFloor.y) == 4) {
    float patchFrequency = 1.0 / max(waterFloor.z, 5.0);
    vec2 pq = q * patchFrequency;
    vec4 noise = vec4(terrainNoise(pq).x, terrainNoise(pq + vec2(17.0, 5.0)).x,
                      terrainNoise(pq + vec2(3.0, 29.0)).x, terrainNoise(pq + vec2(41.0, 13.0)).x);
    vec4 patchy = w * (0.25 + 1.5 * noise);
    patchy = patchy * patchy * patchy;              // sharpen into distinct patches
    patchy /= max(patchy.x + patchy.y + patchy.z + patchy.w, 1e-6);
    w = mix(w, patchy, strength);
  }
  vec3 grain = terrainNoise(q * 0.9), mottle = terrainNoise(q * 0.06);
  float rippleFade = terrainDetailFade(pixelMeters, 1.0 / 1.2);
  float ripple = sin(dot(q, vec2(0.8, 0.6)) * kTwoPiFloor / 1.2 + mottle.x * 4.0) * 0.5 + 0.5;
  vec3 sand = kFloorSand * (1.0 + strength * ((grain.x - 0.5) * 0.12 * terrainDetailFade(pixelMeters, 0.9) +
                                              (ripple - 0.5) * 0.10 * rippleFade + (mottle.x - 0.5) * 0.10));
  vec3 plantNoise = terrainNoise(q * 0.35);
  float clump = smoothstep(0.42, 0.7, plantNoise.x);
  vec3 plants = mix(sand, kFloorVegetation * (0.85 + 0.3 * terrainNoise(q * 1.4).x), mix(1.0, clump, strength));
  vec3 mud = kFloorMud * (1.0 + strength * (mottle.x - 0.5) * 0.14);
  vec3 pebbleNoise = terrainNoise(q * 0.8);
  float pebble = smoothstep(0.48, 0.6, pebbleNoise.x);
  float pebbleFade = terrainDetailFade(pixelMeters, 0.8);
  vec3 pebbleTint = kFloorStones * (0.8 + 0.45 * terrainHash(floor(q * 0.8)));
  vec3 stones = mix(kFloorStones * 0.55, pebbleTint, mix(1.0, pebble, strength * pebbleFade));
  bump = (vec3(-pebbleNoise.y, 0.0, -pebbleNoise.z) * 0.8 * pebble * pebbleFade * w.w +
          vec3(-grain.y, 0.0, -grain.z) * 0.08 * w.x) * strength;
  return sand * w.x + plants * w.y + mud * w.z + stones * w.w;
}


vec3 terrainDetailedColor(vec3 unitNormal, vec3 worldPosition, float elevation, vec3 surface) {
  float strength = detail.x;
  if (strength <= 0.0) return terrainBaseColor(unitNormal, elevation, surface);
  TerrainMaterial m = terrainMaterial(unitNormal, elevation, surface);
  vec2 p = worldPosition.xz;
  float pixelMeters = max(length(fwidth(p)), 1e-4);

  // Grass and bare ground: broad moisture patches and fine tufts.
  vec3 coarse = terrainNoise(p * 0.035), fine = terrainNoise(p * 0.18);
  float grassVariation = (coarse.x - 0.5) * 0.18 * terrainDetailFade(pixelMeters, 0.035) +
                         (fine.x - 0.5) * 0.12 * terrainDetailFade(pixelMeters, 0.18);
  vec3 ground = m.ground * (1.0 + strength * grassVariation * (0.7 + 0.3 * m.vegetation));
  vec3 groundBump = vec3(-fine.y, 0.0, -fine.z) * 0.06 * terrainDetailFade(pixelMeters, 0.18) * m.vegetation;

  // Trees: crowns about eight meters across grouped into stands and clumps,
  // with gaps that reveal the grass beneath. Close up individual crowns are
  // masked and rounded by their gradient; further away stands and then the
  // clumped coverage take over so forests never flatten into uniform discs.
  vec3 crown = terrainNoise(p * 0.12), stand = terrainNoise(p * 0.05), clump = terrainNoise(p * 0.025);
  float crownFade = terrainDetailFade(pixelMeters, 0.12), standFade = terrainDetailFade(pixelMeters, 0.05);
  float coverage = clamp(m.trees * (0.55 + 0.5 * clump.x + 0.35 * (stand.x - 0.5)), 0.0, 1.0);
  float crownMask = smoothstep(0.62 - 0.45 * coverage, 0.80 - 0.45 * coverage, crown.x);
  float standMask = 0.5 * smoothstep(0.55 - 0.4 * coverage, 0.75 - 0.4 * coverage, stand.x) + 0.5 * coverage;
  float mask = mix(mix(coverage, standMask, standFade), crownMask, crownFade);
  float trees = mix(m.trees, mask * smoothstep(0.05, 0.3, m.trees), strength);
  vec3 canopy = m.canopy * (0.80 + 0.34 * crown.x * crownFade + 0.25 * (stand.x - 0.5) * standFade +
                            0.16 * (clump.x - 0.5));
  vec3 canopyBump = (vec3(-crown.y, 0.0, -crown.z) * 1.2 * crownFade + vec3(-stand.y, 0.0, -stand.z) * 0.5 * standFade) *
                    trees * m.vegetation;

  // Rock: ridged crags with sharp creases (the same field that displaces the
  // vertices, plus a finer octave), hard strata ledges following elevation,
  // grain, dark cracks and crevice shadow; scree on exposed crests is speckled.
  vec3 grain = terrainNoise(p * 0.45), warp = terrainNoise(p * 0.05);
  vec3 crag = ridgedFbm(p * kCragFrequency), fineCrag = ridgedFbm(p * 0.21 + vec2(2.0, 8.0));
  float fineFade = terrainDetailFade(pixelMeters, 0.21);
  float relief = max(detail.y, 0.75);
  vec2 cragSlope = crag.yz * (kCragFrequency * 2.0 * relief) + fineCrag.yz * (0.21 * 2.0 * 0.35 * relief * fineFade);
  float strataNoise = terrainNoise(vec2(elevation * 0.35 + warp.x * 2.5, p.x * 0.02 + p.y * 0.013)).x;
  float strata = smoothstep(0.38, 0.62, strataNoise);
  float crack = 1.0 - smoothstep(0.0, 0.06, abs(terrainNoise(p * 0.09).x - 0.5));
  float crevice = (1.0 - crag.x) * 0.7 + (1.0 - fineCrag.x) * 0.3 * fineFade;
  float rockShade = (strata - 0.5) * 0.26 * terrainDetailFade(pixelMeters, 0.35) +
                    (grain.x - 0.5) * 0.12 * terrainDetailFade(pixelMeters, 0.45) -
                    crack * 0.20 * terrainDetailFade(pixelMeters, 0.09) - crevice * 0.32;
  float speckle = (terrainHash(floor(p * 0.7)) - 0.5) * 0.16 * terrainDetailFade(pixelMeters, 0.7);
  vec3 stone = m.stone * (1.0 + strength * mix(rockShade, speckle, m.ridge));
  vec3 stoneBump = (vec3(-cragSlope.x, 0.0, -cragSlope.y) * (1.0 - m.ridge * 0.7) +
                    vec3(-grain.y, 0.0, -grain.z) * 0.5 * terrainDetailFade(pixelMeters, 0.45)) * m.rock;

  // Snow: soft drifts and a sparse sparkle; ice: smooth with crack lines.
  vec3 drift = terrainNoise(p * 0.04);
  float sparkle = smoothstep(0.90, 0.97, terrainNoise(p * 2.5).x) * 0.12 * terrainDetailFade(pixelMeters, 2.5);
  vec3 snow = kSnow * (1.0 + strength * ((drift.x - 0.5) * 0.08 + sparkle));
  float iceCrack = 1.0 - smoothstep(0.0, 0.035, abs(terrainNoise(p * 0.08).x - 0.5));
  vec3 ice = mix(kIce, kIce * 0.72 + vec3(0.0, 0.02, 0.10), strength * iceCrack * 0.85 * terrainDetailFade(pixelMeters, 0.08));
  ice *= 1.0 + strength * (terrainNoise(p * 0.015).x - 0.5) * 0.06;

  vec3 floorBump = vec3(0.0);
  vec3 floorColor = m.submerged > 0.0 ? floorDetailedColor(m, p, pixelMeters, strength, floorBump) : floorBaseColor(m.floor);
  vec3 albedo = terrainCompose(m, ground, canopy, stone, snow, ice, trees, floorColor);
  float snowy = max(m.snow, m.ice);
  vec3 bump = mix((groundBump * (1.0 - m.rock) + canopyBump * (1.0 - m.rock) + stoneBump) * (1.0 - snowy) * strength,
                  floorBump, m.submerged);
  // Rock is flat-shaded: the geometric facet normal replaces the smooth vertex
  // normal so displaced crags read as hard-edged blocks rather than rolling hills.
  vec3 faceNormal = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));
  if (dot(faceNormal, unitNormal) < 0.0) faceNormal = -faceNormal;
  float facet = m.rock * (1.0 - snowy) * (1.0 - m.submerged) * strength * 0.85;
  vec3 n = normalize(mix(unitNormal, faceNormal, facet) + bump);
  float diffuse = max(dot(n, sunLight.xyz), 0.0);
  // Snow and ice scatter sky light: their shadowed side turns slightly blue.
  vec3 ambientTint = mix(vec3(1.0), vec3(0.74, 0.82, 0.98), snowy * strength);
  return albedo * (sunLight.w * ambientTint + (1.0 - sunLight.w) * diffuse);
}

vec3 applyTerrainContours(vec3 color, float elevation) {
  if (elevationRange.w <= 0.0) return color;
  float level = elevation / elevationRange.w;
  float toLine = abs(fract(level + 0.5) - 0.5); // distance to the nearest contour, in levels
  float width = max(fwidth(level), 1e-6);       // levels per pixel
  float line = 1.0 - smoothstep(0.35 * width, 1.1 * width, toLine);
  line *= 1.0 - smoothstep(0.2, 0.5, width);    // fade out where contours would merge
  // Darken light ground, lift very dark ground: lines stay visible on forest and snow.
  vec3 lineColor = mix(color * 0.45, color * 0.5 + vec3(0.06), step(dot(color, vec3(1.0)), 0.45));
  return mix(color, lineColor, 0.7 * line);
}
)GLSL";

const std::string kVertexShader = std::string(R"GLSL(#version 430 core
layout(location = 0) in vec3 POSITION;
layout(location = 1) in vec3 NORMAL;
layout(location = 2) in float FIRE_STATE;
layout(location = 3) in vec3 SURFACE;
layout(std140, binding = 0) uniform Camera { mat4 viewProjection; };
out vec3 surfaceNormal;
out vec3 worldPosition;   // displaced by rock relief
out float baseElevation;  // measured elevation: materials, contours and water use this
out vec3 surfaceAttributes;
out float fireState;
)GLSL") + kNoiseGlsl + kTerrainStyleGlsl + R"GLSL(
void main() {
  fireState = FIRE_STATE;
  surfaceNormal = NORMAL;
  baseElevation = POSITION.y;
  surfaceAttributes = SURFACE;
  vec3 displaced = POSITION;
  displaced.y += rockRelief(normalize(NORMAL), POSITION.y, SURFACE, POSITION.xz);
  worldPosition = displaced;
  gl_Position = viewProjection * vec4(displaced, 1.0);
}
)GLSL";

const std::string kFragmentShader = std::string(R"GLSL(#version 430 core
in vec3 surfaceNormal;
in vec3 worldPosition;
in float baseElevation;
in vec3 surfaceAttributes;
in float fireState;
out vec4 fragColor;
)GLSL") + kNoiseGlsl + kTerrainStyleGlsl + kTerrainDetailGlsl + R"GLSL(
void main() {
  float elevation = baseElevation;
  vec3 base = terrainDetailedColor(normalize(surfaceNormal), worldPosition, elevation, surfaceAttributes);
  base = applyTerrainContours(base, elevation);
  if (fireState > 0.75) {
    base = vec3(1.0, 0.18, 0.015);  // active fire
  } else if (fireState > 0.25) {
    base = vec3(0.13, 0.115, 0.12); // burned terrain
  }
  fragColor = vec4(base, 1.0);
}
)GLSL";


const std::string kFireVertexShader = std::string(R"GLSL(#version 430 core
layout(location = 0) in vec3 POSITION;
layout(location = 1) in vec3 NORMAL;
layout(location = 3) in vec3 SURFACE;
layout(std140, binding = 0) uniform Camera {
  mat4 viewProjection;
  vec4 fireGrid;
};
out vec3 surfaceNormal;
out vec3 worldPosition;
out float baseElevation;
out vec3 surfaceAttributes;
out vec2 localGridMeters;
)GLSL") + kNoiseGlsl + kTerrainStyleGlsl + R"GLSL(
void main() {
  surfaceNormal = NORMAL;
  baseElevation = POSITION.y;
  surfaceAttributes = SURFACE;
  localGridMeters = vec2(POSITION.x, -POSITION.z);
  vec3 displaced = POSITION;
  displaced.y += rockRelief(normalize(NORMAL), POSITION.y, SURFACE, POSITION.xz);
  worldPosition = displaced;
  gl_Position = viewProjection * vec4(displaced, 1.0);
}
)GLSL";

const std::string kFireFragmentShader = std::string(R"GLSL(#version 430 core
in vec3 surfaceNormal;
in vec3 worldPosition;
in float baseElevation;
in vec3 surfaceAttributes;
in vec2 localGridMeters;
layout(std140, binding = 0) uniform Camera {
  mat4 viewProjection;
  vec4 fireGrid; // x = cell size, y = width, z = height, w = animation seconds
};
layout(std430, binding = 1) readonly buffer FireState { uint fireStates[]; };
layout(std140,binding=10) uniform LayerVisibility {vec4 layerFlags;};
out vec4 fragColor;
)GLSL") + kNoiseGlsl + kTerrainStyleGlsl + kTerrainDetailGlsl + R"GLSL(
void main() {
  float elevation = baseElevation;
  vec3 ground = terrainDetailedColor(normalize(surfaceNormal), worldPosition, elevation, surfaceAttributes);
  ground = applyTerrainContours(ground, elevation);

  if(layerFlags.y<0.5) {
    if(layerFlags.x<0.5)discard;
    fragColor=vec4(ground,1.0);return;
  }
  // Each cell contributes a rounded radial footprint, not a square block.
  // Look up neighboring cell centers so adjacent active fires merge smoothly.
  // Out-of-grid centers and holes never contribute; rasterization is already
  // clipped to the real terrain mesh, so no color can appear beyond its edge.
  vec2 gridPos = localGridMeters / fireGrid.x;
  ivec2 nearest = ivec2(floor(gridPos + vec2(0.5)));
  ivec2 dimensions = ivec2(fireGrid.yz);
  // Continuous front field (CPU twin: fire_front_at): cell states blurred by a
  // radial kernel and summed with saturation, then cut by world-space edge
  // noise (fire_edge_noise) so the outline is ragged instead of a cell square.
  float activeField = 0.0, burnedField = 0.0;
  for (int dy = -2; dy <= 2; ++dy) {
    for (int dx = -2; dx <= 2; ++dx) {
      ivec2 cell = nearest + ivec2(dx, dy);
      if (cell.x < 0 || cell.y < 0 ||
          cell.x >= dimensions.x || cell.y >= dimensions.y) continue;
      uint state = fireStates[cell.y * dimensions.x + cell.x];
      if (state == 0u || state == 3u) continue;
      float weight = 1.0 - smoothstep(0.45, 1.55, length(gridPos - vec2(cell)));
      if (state == 2u) activeField = min(1.0, activeField + weight);
      else burnedField = min(1.0, burnedField + weight);
    }
  }
  float edge = (terrainNoise(localGridMeters * 0.012 + vec2(3.1, 9.7)).x - 0.5) * 0.8 +
               (terrainNoise(localGridMeters * 0.05).x - 0.5) * 0.5 +
               (terrainNoise(localGridMeters * 0.25 + vec2(7.3, 1.9)).x - 0.5) * 0.16;
  // A slow flicker of the flame outline keeps the front alive without moving it far.
  float flickerEdge = 0.05 * sin(2.4 * fireGrid.w + localGridMeters.x * 0.3) * cos(1.5 * fireGrid.w + localGridMeters.y * 0.27);
  // Noise only reshapes outlines that exist: no field, no fire.
  float flame = smoothstep(0.6 - 0.12, 0.6 + 0.12, activeField + edge + flickerEdge) * step(0.02, activeField);
  float burn = smoothstep(0.6 - 0.12, 0.6 + 0.12, burnedField + edge) * step(0.02, burnedField) * (1.0 - flame);
  // Mottled char: some patches burn lighter than others.
  burn *= 0.8 + 0.2 * terrainNoise(localGridMeters * 0.11 + vec2(3.0, 5.0)).x;
  // Only fuel burns: the footprint fades out over rock, water, snow, ice and
  // bare soil inside a burning cell, and is fainter where the fuel is thin.
  TerrainMaterial fuelMaterial = terrainMaterial(normalize(surfaceNormal), elevation, surfaceAttributes);
  float fuelFactor = smoothstep(0.05, 0.15, surfaceAttributes.x) * (1.0 - fuelMaterial.rock) *
                     (1.0 - fuelMaterial.submerged) * (1.0 - max(fuelMaterial.snow, fuelMaterial.ice));
  burn *= fuelFactor;
  flame *= fuelFactor * (0.45 + 0.55 * clamp(surfaceAttributes.x, 0.0, 1.0));
  if(layerFlags.x<0.5) {
    if(max(burn,flame)<=0.001)discard; // Hidden ground writes neither color nor depth.
    ground=vec3(0.13,0.12,0.13);
  } else ground = mix(ground, vec3(0.13, 0.12, 0.13), 0.93 * burn);
  float pulse = 0.86 + 0.14 * sin(5.0 * fireGrid.w + gridPos.x * 1.3);
  vec3 fireColor = mix(vec3(0.79, 0.075, 0.01),
                       vec3(1.0, 0.46, 0.025), clamp(flame, 0.0, 1.0));
  ground = mix(ground, fireColor * pulse, flame);
  fragColor = vec4(ground, 1.0);
}
)GLSL";



// Animated water surface. The CPU twins in water_shading.cpp mirror
// shallowness(), refractedTravel(), the shoaling/breaking limits and the
// flat-water color/opacity (wave height zero); waves, foam and ripples are
// GPU-only because they are animated and need screen-space derivatives.
constexpr char kWaterStyleGlsl[] = R"GLSL(
layout(std140, binding = 12) uniform WaterStyle {
  vec4 surface;  // level, wave height, wave length, wave speed
  vec4 travel;   // unit travel direction (x, z), time, foam depth
  vec4 shallow;  // rgb, absorption depth
  vec4 deep;     // rgb, clarity depth
  vec4 eye;      // camera position, choppiness
  vec4 horizon;  // rgb, specular power
  vec4 zenith;   // rgb, reflectivity
  vec4 sun;      // unit direction toward the light, specular intensity
};
const float kTwoPi = 6.28318530718;
float shallowness(float depth) {
  float k = kTwoPi / surface.z;
  return clamp(sqrt(tanh(k * max(depth, 0.02))), 0.3, 1.0);
}
vec2 refractedTravel(vec2 bedGradient, float depth) {
  float slope = length(bedGradient);
  if (slope < 1e-4) return travel.xy;
  float bend = 0.75 * (1.0 - shallowness(depth)) * clamp(slope * 20.0, 0.0, 1.0);
  vec2 bent = mix(travel.xy, bedGradient / slope, bend);
  return length(bent) > 1e-3 ? normalize(bent) : travel.xy;
}
)GLSL";

constexpr char kWaterVertexShader[] = R"GLSL(#version 430 core
layout(location = 0) in vec3 POSITION;
layout(location = 1) in float BED;
layout(location = 2) in vec2 BED_GRADIENT;
layout(std140, binding = 0) uniform Camera { mat4 viewProjection; };
)GLSL";

const std::string kWaterVertexShaderFull = std::string(kWaterVertexShader) + kWaterStyleGlsl + R"GLSL(
out vec3 surfacePosition;
out float bedHeight;
out vec2 bedGradient;
void main() {
  surfacePosition = POSITION;
  bedHeight = BED;
  bedGradient = BED_GRADIENT;
  // A long swell (six wave lengths) lifts the coarse surface mesh itself; it
  // dies out toward the shore so the water never climbs onto the land.
  float depth = max(surface.x - BED, 0.0);
  float k = kTwoPi / (surface.z * 6.0);
  float swell = 0.5 * surface.y * sin(dot(POSITION.xz, travel.xy) * k - surface.w * k * travel.z);
  vec3 displaced = POSITION;
  displaced.y += swell * (1.0 - exp(-depth / (2.0 * surface.y + 0.05)));
  gl_Position = viewProjection * vec4(displaced, 1.0);
}
)GLSL";

const std::string kWaterFragmentShader = std::string(R"GLSL(#version 430 core
in vec3 surfacePosition;
in float bedHeight;
in vec2 bedGradient;
out vec4 fragColor;
)GLSL") + kNoiseGlsl + kWaterStyleGlsl + R"GLSL(
// One directional wave: shoals (grows) and shortens over shallow ground, is
// capped by the depth it travels in, and accumulates height and slope.
void addWave(inout vec3 acc, vec2 p, vec2 dir, float amplitude, float wavelength, float speed,
             float depth, out float crest, out float breaking) {
  float k = kTwoPi / wavelength;
  float ratio = clamp(sqrt(tanh(k * max(depth, 0.02))), 0.3, 1.0);
  float kLocal = k / ratio;
  float shoal = clamp(1.0 / sqrt(ratio), 1.0, 1.8);
  float limit = 0.6 * depth;
  float a = min(amplitude * shoal, limit);
  float phase = dot(p, dir) * kLocal - speed * k * travel.z;
  float s = sin(phase), c = cos(phase);
  acc += vec3(a * s, a * kLocal * c * dir.x, a * kLocal * c * dir.y);
  crest = smoothstep(0.15, 0.9, s);
  breaking = amplitude > 0.0 ? smoothstep(0.85, 1.0, amplitude * shoal / max(limit, 1e-3)) : 0.0;
}
vec2 rotate(vec2 v, float radians) {
  float c = cos(radians), s = sin(radians);
  return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}
void main() {
  float depth = surface.x - bedHeight;
  if (depth <= 0.001) discard;            // land corners of shoreline triangles
  vec2 p = surfacePosition.xz;
  float time = travel.z, height = surface.y, chop = eye.w;
  float pixelMeters = max(length(fwidth(p)), 1e-4);

  // Wave field: primary wave bent toward the shore, a long irregular swell,
  // two cross waves and ripples. Gusts modulate the amplitude and a slow
  // warp bends the fronts so they never read as ruled lines.
  vec2 dir = refractedTravel(bedGradient, depth);
  float gust = 0.7 + 0.6 * terrainNoise(p * 0.012 - dir * time * 0.12).x;
  vec2 warp = (terrainNoise(p * 0.02 + vec2(3.7, 1.3)).yz) * 0.6 * surface.z * chop;
  vec3 acc = vec3(0.0);
  float crest, breaking, crest2, breaking2;
  addWave(acc, p + warp, dir, height * gust, surface.z, surface.w, depth, crest, breaking);
  addWave(acc, p - warp, rotate(dir, 0.18), 0.45 * height, 1.9 * surface.z, surface.w * sqrt(1.9), depth, crest2, breaking2);
  breaking = max(breaking, 0.6 * breaking2);
  float secondary = 0.35 * height * chop * gust;
  float shortLength = 0.55 * surface.z, shortSpeed = surface.w * sqrt(0.55);
  addWave(acc, p, rotate(dir, 0.56), secondary, shortLength, shortSpeed, depth, crest2, breaking2);
  addWave(acc, p + warp * 0.5, rotate(dir, -0.56), secondary, shortLength, shortSpeed * 1.1, depth, crest2, breaking2);
  vec3 ripple = terrainNoise(p * 0.45 + dir * time * 0.8) + terrainNoise(p * 1.1 - dir.yx * time * 0.5);
  float rippleFade = terrainDetailFade(pixelMeters, 1.1) * smoothstep(0.0, 0.2, height) * chop;
  vec3 n = normalize(vec3(-acc.y - ripple.y * 0.12 * rippleFade, 1.0, -acc.z - ripple.z * 0.12 * rippleFade));

  // Foam: surging shoreline band, splashes on rocky (steep) shores, and the
  // crests of breaking waves. Flat water (zero height) has no foam at all.
  float foamStrength = smoothstep(0.0, 0.3, height);
  float foamNoise = terrainNoise(p * 0.35 + vec2(time * 0.4)).x * 0.6 + terrainNoise(p * 1.3 - vec2(0.0, time * 0.7)).x * 0.4;
  // Swash: the foam line runs up the beach with each arriving crest and drains back.
  float surge = 0.5 + 0.5 * sin(time * surface.w * kTwoPi / surface.z - depth * 2.5);
  float lace = smoothstep(0.3, 0.8, foamNoise * 0.7 + surge * 0.45);
  float shoreFoam = (1.0 - smoothstep(0.0, travel.w, depth)) * max(lace, 0.6 * (1.0 - smoothstep(0.0, 0.25 * travel.w, depth)));
  float rockiness = smoothstep(0.35, 0.9, length(bedGradient));
  float splash = rockiness * (1.0 - smoothstep(0.0, 2.5 * travel.w, depth)) *
                 smoothstep(0.35, 0.8, terrainNoise(p * 0.6 + vec2(time * 1.1, -time * 0.9)).x) *
                 (0.6 + 0.4 * sin(time * 2.3 + p.x * 0.3 + p.y * 0.2));
  float breakFoam = breaking * crest * (0.55 + 0.45 * foamNoise);
  float foam = clamp((shoreFoam * 0.85 + splash + breakFoam) * foamStrength, 0.0, 1.0);

  // Color: depth absorption, Fresnel sky reflection, sun glitter, opacity.
  vec3 view = normalize(eye.xyz - surfacePosition);
  float cosTheta = max(dot(n, view), 0.0);
  float fresnel = 0.02 + 0.98 * pow(1.0 - cosTheta, 5.0);
  vec3 body = mix(shallow.rgb, deep.rgb, 1.0 - exp(-depth / shallow.w));
  vec3 r = reflect(-view, n);
  vec3 skyColor = mix(horizon.rgb, zenith.rgb, clamp(r.y, 0.0, 1.0));
  float specular = pow(max(dot(r, sun.xyz), 0.0), horizon.w) * sun.w;
  float lit = 0.75 + 0.25 * max(dot(n, sun.xyz), 0.0);
  float reflection = fresnel * zenith.w;
  vec3 color = mix(body * lit, skyColor, reflection) + vec3(specular);
  float alpha = max(1.0 - exp(-depth / deep.w), reflection);
  vec3 foamColor = vec3(0.93, 0.95, 0.96) * (0.75 + 0.25 * max(dot(n, sun.xyz), 0.0));
  color = mix(color, foamColor, foam);
  alpha = mix(alpha, 1.0, foam);
  fragColor = vec4(color, alpha);
}
)GLSL";


// Instanced vegetation: unit-height meshes placed, scaled, rotated and tinted
// per instance, swaying in the wind by their flex weight, shrinking away at the
// draw distance so no blending is needed. Two-sided lighting for grass blades.
const std::string kVegetationVertexShader = std::string(R"GLSL(#version 430 core
layout(location = 0) in vec3 POSITION;
layout(location = 1) in vec3 NORMAL;
layout(location = 2) in vec3 COLOR;
layout(location = 3) in float FLEX;
layout(location = 4) in float TINT_WEIGHT;
layout(std140, binding = 0) uniform Camera { mat4 viewProjection; };
layout(std140, binding = 13) uniform VegetationStyle {
  vec4 wind;       // unit direction (x, z), speed, time
  vec4 light;      // unit direction toward the light, ambient
  vec4 params;     // sway amplitude, grass fade start, grass fade end, tree fade end
  vec4 eye;        // camera position
  vec4 fireGrid;   // cell size, width, height, present
  vec4 fireOrigin; // origin offset x, origin offset y
};
struct PlantInstance { vec4 positionScale; vec4 motion; vec4 tint; }; // motion: rotation, phase, bend, kind
layout(std430, binding = 4) readonly buffer InstanceData { PlantInstance plants[]; };
layout(std430, binding = 5) readonly buffer PlantFireState { uint plantFireStates[]; };
out vec3 worldNormal;
out vec3 plantColor;
out float plantFlex;
out float plantBurn;   // 0 unburned, 1 burned (charred), 2 burning (glowing)
)GLSL") + kNoiseGlsl + R"GLSL(
// The plant's burn state follows the same ragged front field as the ground
// and the flames (CPU twins: fire_front_at, fire_edge_noise): 2 burning, 1 charred.
float fireStateAt(vec2 xz) {
  if (fireGrid.w < 0.5) return 0.0;
  vec2 gridPos = vec2(xz.x, -xz.y) / fireGrid.x;
  ivec2 nearest = ivec2(round(gridPos)) + ivec2(fireOrigin.xy);
  float activeField = 0.0, burnedField = 0.0;
  for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
    ivec2 cell = nearest + ivec2(dx, dy);
    if (cell.x < 0 || cell.y < 0 || cell.x >= int(fireGrid.y) || cell.y >= int(fireGrid.z)) continue;
    uint state = plantFireStates[cell.y * int(fireGrid.y) + cell.x];
    if (state != 1u && state != 2u) continue;
    float weight = 1.0 - smoothstep(0.45, 1.55, length(gridPos - vec2(cell - ivec2(fireOrigin.xy))));
    if (state == 2u) activeField = min(1.0, activeField + weight);
    else burnedField = min(1.0, burnedField + weight);
  }
  vec2 gm = vec2(xz.x, -xz.y); // local grid metres, as the ground shader sees them
  float edge = (terrainNoise(gm * 0.012 + vec2(3.1, 9.7)).x - 0.5) * 0.8 + (terrainNoise(gm * 0.05).x - 0.5) * 0.5 +
               (terrainNoise(gm * 0.25 + vec2(7.3, 1.9)).x - 0.5) * 0.16;
  if (activeField > 0.02 && activeField + edge > 0.6) return 2.0;
  if (burnedField > 0.02 && burnedField + edge > 0.6) return 1.0;
  return 0.0;
}
void main() {
  PlantInstance plant = plants[gl_InstanceID];
  float c = cos(plant.motion.x), s = sin(plant.motion.x);
  mat3 rotation = mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c);
  // Grass shrinks to nothing toward the grass distance; trees toward theirs.
  float distanceToEye = length(plant.positionScale.xyz - eye.xyz);
  bool isGrass = plant.positionScale.w < 3.5;
  float fadeStart = isGrass ? params.y : params.w * 0.8, fadeEnd = isGrass ? params.z : params.w;
  float presence = 1.0 - smoothstep(fadeStart, fadeEnd, distanceToEye);
  plantBurn = fireStateAt(plant.positionScale.xz);
  // Burned grass is reduced to stubble; burned trees keep most of their charred size.
  if (plantBurn > 0.5 && plantBurn < 1.5) presence *= isGrass ? 0.25 : 0.9;
  float scale = plant.positionScale.w * presence;
  vec3 local = rotation * (POSITION * scale);
  // Wind: the tip leads, the base stays anchored; gusts vary along the wind direction.
  float gust = sin(wind.w * (0.6 + 0.25 * wind.z) + plant.motion.y + dot(plant.positionScale.xz, wind.xy) * 0.05);
  float sway = params.x * (0.02 + 0.012 * wind.z) * scale * FLEX * FLEX * (gust + 0.35 * sin(wind.w * 2.3 + plant.motion.y * 1.7));
  vec3 offset = vec3(wind.x, 0.0, wind.y) * sway + vec3(-wind.y, 0.0, wind.x) * sway * 0.3;
  offset += vec3(wind.x, 0.0, wind.y) * plant.motion.z * 0.08 * scale * FLEX;  // permanent lean on steep ground
  vec3 world = plant.positionScale.xyz + local + offset;
  worldNormal = rotation * NORMAL;
  vec3 living = COLOR * mix(vec3(1.0), plant.tint.rgb, TINT_WEIGHT);
  vec3 charred = vec3(0.05, 0.045, 0.04) + 0.06 * COLOR;
  if (plantBurn > 1.5) {
    // Burning: foliage glows and flickers orange, wood darkens.
    float flicker = 0.75 + 0.25 * sin(wind.w * 9.0 + plant.motion.y * 5.0 + FLEX * 6.0);
    vec3 ember = mix(vec3(0.6, 0.12, 0.02), vec3(1.0, 0.55, 0.12), FLEX) * flicker;
    plantColor = mix(mix(living, charred, 0.6), ember, TINT_WEIGHT * (0.45 + 0.45 * FLEX));
  } else if (plantBurn > 0.5) {
    plantColor = charred;
  } else {
    plantColor = living;
  }
  plantFlex = FLEX;
  gl_Position = viewProjection * vec4(world, 1.0);
}
)GLSL";

const std::string kVegetationFragmentShader = std::string(R"GLSL(#version 430 core
in vec3 worldNormal;
in vec3 plantColor;
in float plantFlex;
in float plantBurn;
layout(std140, binding = 13) uniform VegetationStyle {
  vec4 wind;
  vec4 light;
  vec4 params;
  vec4 eye;
  vec4 fireGrid;
  vec4 fireOrigin;
};
out vec4 fragColor;
void main() {
  vec3 n = normalize(worldNormal);
  // Two-sided, wrapped lighting: thin blades and cone skins never go black.
  float diffuse = 0.35 + 0.65 * abs(dot(n, light.xyz));
  float occlusion = 0.72 + 0.28 * plantFlex;              // darker toward the base
  vec3 color = plantColor * (light.w + (1.0 - light.w) * diffuse) * occlusion;
  if (plantBurn > 1.5) color = max(color, plantColor * 0.8); // embers are self-lit
  fragColor = vec4(color, 1.0);
}
)GLSL");


// Flames and smoke plumes: camera-facing (cylindrical) billboards with
// procedural noise, flickering in place, smoke drifting with the wind.
const std::string kFireVertexShaderSource = std::string(R"GLSL(#version 430 core
layout(location = 0) in vec2 POSITION;
layout(location = 1) in vec2 TEXCOORD;
layout(std140, binding = 0) uniform Camera { mat4 viewProjection; };
layout(std140, binding = 14) uniform FireStyle {
  vec4 wind;    // unit direction (x, z), speed, time
  vec4 eye;     // camera position, flame scale
  vec4 params;  // smoke scale, smoke height, draw distance
  vec4 light;   // unit direction toward the light
};
struct FireRecord { vec4 positionSize; vec4 motion; vec4 lean; }; // motion: phase, intensity, kind, seed
layout(std430, binding = 4) readonly buffer InstanceData { FireRecord records[]; };
out vec2 uv;
out float intensity;
out float kind;
out float seed;
void main() {
  FireRecord item = records[gl_InstanceID];
  vec3 base = item.positionSize.xyz;
  float height = item.positionSize.w;
  kind = item.motion.z; seed = item.motion.w; intensity = item.motion.y; uv = TEXCOORD;
  float time = wind.w;
  vec3 toEye = eye.xyz - base;
  vec3 right = normalize(vec3(toEye.z, 0.0, -toEye.x) + vec3(1e-4, 0.0, 0.0));
  float width = kind < 0.5 ? height * 0.55 : height * (0.35 + 0.55 * TEXCOORD.y);
  vec3 offset = right * POSITION.x * width;
  if (kind < 0.5) {
    // Flames flicker: the tip sways and the height pulses.
    float flicker = 0.85 + 0.15 * sin(time * 7.0 + item.motion.x * 3.0) * (0.5 + 0.5 * sin(time * 11.0 + seed));
    float sway = sin(time * 9.0 + item.motion.x) * 0.18 * height * TEXCOORD.y * TEXCOORD.y;
    offset += right * sway + vec3(wind.x, 0.0, wind.y) * (0.03 * wind.z * height * TEXCOORD.y);
    height *= flicker;
  } else {
    // Smoke leans downwind as it rises.
    offset += vec3(wind.x, 0.0, wind.y) * (0.02 + 0.06 * wind.z) * height * TEXCOORD.y * TEXCOORD.y;
  }
  vec3 world = base + offset + vec3(0.0, POSITION.y * height, 0.0);
  gl_Position = viewProjection * vec4(world, 1.0);
}
)GLSL");

const std::string kFireFragmentShaderSource = std::string(R"GLSL(#version 430 core
in vec2 uv;
in float intensity;
in float kind;
in float seed;
layout(std140, binding = 14) uniform FireStyle {
  vec4 wind;
  vec4 eye;
  vec4 params;
  vec4 light;
};
out vec4 fragColor;
)GLSL") + kNoiseGlsl + R"GLSL(
float fbm(vec2 p) {
  return 0.55 * terrainNoise(p).x + 0.30 * terrainNoise(p * 2.1 + vec2(3.1, 7.7)).x + 0.15 * terrainNoise(p * 4.3 + vec2(9.2, 1.3)).x;
}
void main() {
  float time = wind.w;
  if (kind < 0.5) {
    // Flame: a tongue narrowing toward the tip, eroded by rising noise.
    float halfWidth = 0.5 * pow(1.0 - uv.y, 0.65) * (0.85 + 0.15 * sin(seed));
    float d = abs(uv.x - 0.5) / max(halfWidth, 1e-3);
    float tongue = 1.0 - smoothstep(0.55, 1.0, d);
    float n = fbm(vec2(uv.x * 3.0 + seed, uv.y * 2.2 - time * 2.4 - seed));
    float body = smoothstep(0.30, 0.62, n + (1.0 - uv.y) * 0.55 - 0.2 * d);
    float alpha = tongue * body * (1.0 - uv.y * uv.y) * (0.55 + 0.45 * intensity);
    if (alpha < 0.02) discard;
    float heat = clamp(uv.y + 0.35 * n - 0.25 * (1.0 - d), 0.0, 1.0);
    vec3 core = vec3(1.0, 0.96, 0.70), mid = vec3(1.0, 0.48, 0.06), edge = vec3(0.78, 0.10, 0.01);
    vec3 color = mix(core, mid, smoothstep(0.0, 0.45, heat));
    color = mix(color, edge, smoothstep(0.45, 1.0, heat));
    fragColor = vec4(color, alpha);
  } else {
    // Smoke plume: widening column of drifting grey, thinning with height.
    float halfWidth = 0.5 * (0.2 + 0.8 * uv.y);
    float d = abs(uv.x - 0.5) / max(halfWidth, 1e-3);
    float column = 1.0 - smoothstep(0.35, 1.0, d);
    float n = fbm(vec2(uv.x * 2.5 + seed, uv.y * 3.0 - time * 0.18 - seed * 0.3));
    float density = smoothstep(0.28, 0.62, n) * column * (1.0 - uv.y) * smoothstep(0.0, 0.08, uv.y);
    float scale = params.x * (kind > 1.5 ? 0.35 : 1.0) * intensity;
    float alpha = density * 0.55 * scale;
    if (alpha < 0.015) discard;
    float lit = 0.55 + 0.45 * max(light.y, 0.0) * (0.5 + 0.5 * n);
    vec3 smoke = vec3(0.36, 0.34, 0.33) * lit;
    if (kind < 1.5) smoke = mix(vec3(0.55, 0.28, 0.12), smoke, smoothstep(0.0, 0.2, uv.y)); // lit from the fire below
    fragColor = vec4(smoke, alpha);
  }
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
out vec3 worldPosition;
void main() {
  worldPosition=(model*vec4(POSITION,1.0)).xyz;
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
out vec3 worldPosition;
void main() {
  InstanceRecord item=instances[gl_InstanceID];
  worldPosition=(item.model*vec4(POSITION,1.0)).xyz;
  surfaceNormal=normalize(mat3(item.normalMatrix)*NORMAL);
  surfaceTint=item.tint.rgb;
  textureUv=TEXCOORD;
  gl_Position=viewProjection*item.model*vec4(POSITION,1.0);
}
)GLSL";

constexpr char kPbrTexturedVertexShader[] = R"GLSL(#version 430 core
layout(location=0) in vec3 POSITION;
layout(location=1) in vec3 NORMAL;
layout(location=3) in vec2 TEXCOORD;
layout(location=4) in vec4 TANGENT;
layout(std140,binding=0) uniform Camera { mat4 viewProjection; };
layout(std140,binding=2) uniform Object {
  mat4 model;
  mat4 normalMatrix;
  vec4 tint;
};
out vec3 surfaceNormal;
out vec3 surfaceTint;
out vec2 textureUv;
out vec3 worldPosition;
out vec4 worldTangent;
void main(){
  worldPosition=(model*vec4(POSITION,1.0)).xyz;
  surfaceNormal=normalize(mat3(normalMatrix)*NORMAL);
  surfaceTint=tint.rgb;
  textureUv=TEXCOORD;
  vec3 T=mat3(model)*TANGENT.xyz;
  float orientation=determinant(mat3(model))<0.0?-1.0:1.0;
  worldTangent=vec4(T,TANGENT.w*orientation);
  gl_Position=viewProjection*model*vec4(POSITION,1.0);
}
)GLSL";

constexpr char kPbrInstancedVertexShader[] = R"GLSL(#version 430 core
layout(location=0) in vec3 POSITION;
layout(location=1) in vec3 NORMAL;
layout(location=3) in vec2 TEXCOORD;
layout(location=4) in vec4 TANGENT;
layout(std140,binding=0) uniform Camera { mat4 viewProjection; };
struct InstanceRecord { mat4 model; mat4 normalMatrix; vec4 tint; };
layout(std430,binding=4) readonly buffer InstanceData {
  InstanceRecord instances[];
};
out vec3 surfaceNormal;
out vec3 surfaceTint;
out vec2 textureUv;
out vec3 worldPosition;
out vec4 worldTangent;
void main(){
  InstanceRecord item=instances[gl_InstanceID];
  worldPosition=(item.model*vec4(POSITION,1.0)).xyz;
  surfaceNormal=normalize(mat3(item.normalMatrix)*NORMAL);
  surfaceTint=item.tint.rgb;
  textureUv=TEXCOORD;
  vec3 T=mat3(item.model)*TANGENT.xyz;
  float orientation=determinant(mat3(item.model))<0.0?-1.0:1.0;
  worldTangent=vec4(T,TANGENT.w*orientation);
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

constexpr char kPbrFragmentShader[] = R"GLSL(#version 430 core
in vec3 surfaceNormal;
in vec3 surfaceTint;
in vec2 textureUv;
in vec3 worldPosition;
in vec4 worldTangent;
layout(binding=3) uniform sampler2D AlbedoTexture;
layout(binding=6) uniform sampler2D MetallicRoughnessTexture;
layout(binding=7) uniform sampler2D NormalTexture;
layout(binding=8) uniform sampler2D OcclusionTexture;
layout(binding=9) uniform sampler2D EmissiveTexture;
layout(std140,binding=5) uniform PbrMaterial {
  vec4 factors; // metallic, roughness, normalScale, occlusionStrength
  vec4 eye;
  vec4 emissive; // emissive RGB factor (linear), alpha cutoff
  vec4 options; // alpha factor, mask enabled
};
out vec4 fragColor;
const float PI=3.14159265358979323846;
// GGX NDF, Smith-Schlick visibility and Schlick Fresnel.
float distributionGGX(vec3 N,vec3 H,float roughness){
  float a=roughness*roughness;
  float a2=a*a;
  float nh=max(dot(N,H),0.0);
  float denom=nh*nh*(a2-1.0)+1.0;
  return a2/max(PI*denom*denom,1e-5);
}
float smithG1(float nd,float roughness){
  float k=pow(roughness+1.0,2.0)/8.0;
  return nd/max(nd*(1.0-k)+k,1e-5);
}
void main(){
  vec4 albedo=texture(AlbedoTexture,textureUv);
  if(options.y>0.5 && options.y<1.5 && albedo.a*options.x<emissive.w)discard;
  vec3 N=normalize(surfaceNormal);
  // Reconstruct a tangent frame from world-space derivatives and UVs.
  // glTF normal texture is linear tangent-space RGB.
  vec3 dpdx=dFdx(worldPosition),dpdy=dFdy(worldPosition);
  vec2 dudx=dFdx(textureUv),dudy=dFdy(textureUv);
  float det=dudx.x*dudy.y-dudx.y*dudy.x;
  vec3 sampled=texture(NormalTexture,textureUv).xyz*2.0-1.0;
  sampled.xy*=factors.z;
  vec3 T=vec3(0.0);
  float handedness=1.0;
  if(abs(worldTangent.w)>0.5 && length(worldTangent.xyz)>1e-8){
    T=worldTangent.xyz;
    handedness=sign(worldTangent.w);
  }else if(abs(det)>1e-8){
    T=(dpdx*dudy.y-dpdy*dudx.y)/det;
    handedness=sign(det);
  }
  if(dot(T,T)>1e-12){
    T=normalize(T-N*dot(N,T));
    vec3 B=normalize(cross(N,T))*handedness;
    N=normalize(mat3(T,B,N)*sampled);
  }
  vec3 V=normalize(eye.xyz-worldPosition);
  vec3 L=normalize(vec3(0.45,0.85,0.25));
  vec3 H=normalize(V+L);
  // glTF metallic-roughness packing: G=roughness, B=metallic.
  vec3 mr=texture(MetallicRoughnessTexture,textureUv).rgb;
  float metallic=clamp(factors.x*mr.b,0.0,1.0);
  float roughness=clamp(factors.y*mr.g,0.045,1.0);
  // Base-color texture and baseColorFactor use sRGB, light in linear RGB.
  vec3 base=pow(max(albedo.rgb,vec3(0.0)),vec3(2.2));
  base*=max(surfaceTint,vec3(0.0));
  vec3 F0=mix(vec3(0.04),base,metallic);
  float nv=max(dot(N,V),0.0),nl=max(dot(N,L),0.0);
  float vh=max(dot(V,H),0.0);
  vec3 F=F0+(1.0-F0)*pow(1.0-vh,5.0);
  float D=distributionGGX(N,H,roughness);
  float G=smithG1(nv,roughness)*smithG1(nl,roughness);
  vec3 spec=D*G*F/max(4.0*nv*nl,0.001);
  vec3 diffuse=(1.0-F)*(1.0-metallic)*base/PI;
  float occlusion=mix(1.0,texture(OcclusionTexture,textureUv).r,
                      clamp(factors.w,0.0,1.0));
  vec3 glow=pow(max(texture(EmissiveTexture,textureUv).rgb,vec3(0.0)),
                vec3(2.2))*emissive.rgb;
  vec3 color=(diffuse+spec)*vec3(3.2)*nl +
             base*vec3(0.055)*(1.0-metallic)*occlusion+glow;
  // ACES approximation plus display conversion.
  color=(color*(2.51*color+0.03))/(color*(2.43*color+0.59)+0.14);
  fragColor=vec4(pow(clamp(color,0.0,1.0),vec3(1.0/2.2)),
                 options.y>1.5?clamp(albedo.a*options.x,0.0,1.0):1.0);
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
    if(name=="LayerVisibility" && values[4]!=16)
      throw std::runtime_error("LayerVisibility UBO must contain one vec4");
    if(name=="TerrainStyle" && values[4]!=static_cast<GLint>(kTerrainStyleBytes))
      throw std::runtime_error("TerrainStyle UBO must contain six vec4s");
    if(name=="WaterStyle" && values[4]!=static_cast<GLint>(kWaterStyleBytes))
      throw std::runtime_error("WaterStyle UBO must contain eight vec4s");
    if(name=="VegetationStyle" && values[4]!=static_cast<GLint>(kVegetationStyleBytes))
      throw std::runtime_error("VegetationStyle UBO must contain six vec4s");
    if(name=="FireStyle" && values[4]!=static_cast<GLint>(kFireStyleBytes))
      throw std::runtime_error("FireStyle UBO must contain four vec4s");
    if (name == "PbrMaterial" && values[4] != 64)
      throw std::runtime_error("PbrMaterial UBO must have four vec4s");
    if (name == "Object" && values[4] != 144)
      throw std::runtime_error("Object UBO must contain model, normalMatrix and tint");
    // Material/layer blocks exist only in the fragment shader. Some Mesa drivers
    // return inconsistent GL_REFERENCED_BY_* flags for active UBOs; the
    // binding index and std140 block size are validated independently.
    std::uint32_t stages=(values[2]?kVertexStage:0U) |
                         (values[3]?kFragmentStage:0U);
    if(name=="PbrMaterial" || name=="LayerVisibility")stages=kFragmentStage;
    if(name=="TerrainStyle" || name=="VegetationStyle" || name=="FireStyle")stages=kVertexStage|kFragmentStage;
    actual.bindings.push_back({name,ShaderResourceKind::UniformBuffer,
                              static_cast<std::uint32_t>(values[1]),stages});
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
    if (stages == 0 && (name == "FireState" || name == "PlantFireState" || name == "InstanceData")) {
      // Certain Mesa software drivers report no stage flag on the block
      // even when it is active. Confirm that the block has active variables;
      // its known GLSL source is compiled only into the fragment stage.
      constexpr GLenum active_property[] = {GL_NUM_ACTIVE_VARIABLES};
      GLint active_variables = 0;
      gl.GetProgramResourceiv(program, GL_SHADER_STORAGE_BLOCK,
                              static_cast<GLuint>(i), 1, active_property,
                              1, nullptr, &active_variables);
      if (active_variables > 0) stages = (name=="InstanceData" || name=="PlantFireState") ? kVertexStage : kFragmentStage;
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
  struct GpuTexture { GLuint name = 0; bool borrowed=false; };
  struct GpuPipeline { GLuint program = 0, vao = 0; PipelineDesc desc; };

  struct GpuTarget {GLuint framebuffer=0,color=0,depth=0;};
  std::unordered_map<std::uint32_t,GpuTarget> render_targets;

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
  std::optional<bool> depth_test_state,depth_write_state,alpha_blend_state;

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
    for(const auto& [id,target]:render_targets) {
      (void)id;
      gl.DeleteFramebuffers(1,&target.framebuffer);
      gl.DeleteTextures(1,&target.color);
      gl.DeleteRenderbuffers(1,&target.depth);
    }
    for (const auto& [id, pipeline] : pipelines) {
      (void)id;
      gl.DeleteVertexArrays(1, &pipeline.vao);
      gl.DeleteProgram(pipeline.program);
    }
    for (const auto& [id, texture] : textures) {
      (void)id;
      if(!texture.borrowed)gl.DeleteTextures(1,&texture.name);
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
  // Element-buffer binding belongs to the currently bound VAO. Loading a
  // replacement asset can mutate the terrain/old asset VAO between frames.
  if(desc.usage==BufferUsage::Index)
    impl_->attribute_pipeline=impl_->attribute_vertex=impl_->attribute_index=0;
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

RenderTargetHandle OpenGLRenderDevice::create_render_target(OffscreenTargetDesc desc) {
  (void)offscreen_storage_bytes(desc);
  GLint maximum=0;
  impl_->gl.GetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&maximum);
  GLint max_texture=0;
  impl_->gl.GetIntegerv(GL_MAX_TEXTURE_SIZE,&max_texture);
  maximum=std::min(maximum,max_texture);
  if(desc.width>static_cast<std::uint32_t>(maximum) ||
     desc.height>static_cast<std::uint32_t>(maximum))
    throw std::invalid_argument("offscreen target exceeds device extent limit");
  const auto handle=impl_->validator.create_render_target(desc);
  Impl::GpuTarget target;
  GLint previous_draw=0,previous_read=0,previous_renderbuffer=0,previous_texture=0;
  impl_->gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previous_draw);
  impl_->gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous_read);
  impl_->gl.GetIntegerv(GL_RENDERBUFFER_BINDING,&previous_renderbuffer);
  impl_->gl.GetIntegerv(GL_TEXTURE_BINDING_2D,&previous_texture);
  const auto restore=[&] {
    impl_->gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(previous_draw));
    impl_->gl.BindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(previous_read));
    impl_->gl.BindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(previous_renderbuffer));
    impl_->gl.BindTexture(GL_TEXTURE_2D,static_cast<GLuint>(previous_texture));
    impl_->texture_units.clear();
  };
  try {
    impl_->gl.GenFramebuffers(1,&target.framebuffer);
    impl_->gl.GenTextures(1,&target.color);
    impl_->gl.GenRenderbuffers(1,&target.depth);
    if(!target.framebuffer || !target.color || !target.depth)
      throw std::runtime_error("offscreen GL object allocation failed");
    impl_->gl.BindFramebuffer(GL_FRAMEBUFFER,target.framebuffer);
    const auto attach=[&](GLuint name,GLenum format,GLenum point) {
      impl_->gl.BindRenderbuffer(GL_RENDERBUFFER,name);
      impl_->gl.RenderbufferStorage(GL_RENDERBUFFER,format,
          static_cast<GLsizei>(desc.width),static_cast<GLsizei>(desc.height));
      impl_->gl.FramebufferRenderbuffer(GL_FRAMEBUFFER,point,GL_RENDERBUFFER,name);
    };
    impl_->gl.BindTexture(GL_TEXTURE_2D,target.color);
    impl_->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    impl_->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    impl_->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    impl_->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    impl_->gl.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,static_cast<GLsizei>(desc.width),
        static_cast<GLsizei>(desc.height),0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    impl_->gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target.color,0);
    attach(target.depth,GL_DEPTH_COMPONENT32F,GL_DEPTH_ATTACHMENT);
    const auto complete=impl_->gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
    const auto error=impl_->gl.GetError();
    if(complete!=GL_FRAMEBUFFER_COMPLETE || error!=GL_NO_ERROR)
      throw std::runtime_error("offscreen framebuffer storage or completeness failed");
    impl_->textures.emplace(impl_->validator.render_target_color(handle).id,
                            Impl::GpuTexture{target.color,true});
    impl_->render_targets.emplace(handle.id,target);
  } catch(...) {
    impl_->textures.erase(impl_->validator.render_target_color(handle).id);
    impl_->render_targets.erase(handle.id);
    restore();
    impl_->gl.DeleteFramebuffers(1,&target.framebuffer);
    impl_->gl.DeleteTextures(1,&target.color);
    impl_->gl.DeleteRenderbuffers(1,&target.depth);
    impl_->validator.destroy_render_target(handle);
    throw;
  }
  restore();
  return handle;
}

void OpenGLRenderDevice::destroy_render_target(RenderTargetHandle handle) {
  const auto color=impl_->validator.render_target_color(handle);
  impl_->validator.destroy_render_target(handle);
  impl_->textures.erase(color.id);impl_->texture_units.clear();
  const auto target=impl_->render_targets.at(handle.id);
  impl_->gl.DeleteFramebuffers(1,&target.framebuffer);
  impl_->gl.DeleteTextures(1,&target.color);
  impl_->gl.DeleteRenderbuffers(1,&target.depth);
  impl_->render_targets.erase(handle.id);
}

TextureHandle OpenGLRenderDevice::render_target_color(RenderTargetHandle handle) const {
  return impl_->validator.render_target_color(handle);
}

PipelineHandle OpenGLRenderDevice::create_pipeline(PipelineDesc desc) {
  validate_pipeline_desc(desc,false);
  const char* vertex_source=nullptr;
  const char* fragment_source=nullptr;
  switch(desc.program) {
    case ShaderProgramId::Composite:vertex_source=kCompositeVertexShader;fragment_source=kCompositeFragmentShader;break;
    case ShaderProgramId::Terrain:vertex_source=kVertexShader.c_str();fragment_source=kFragmentShader.c_str();break;
    case ShaderProgramId::Wildfire:vertex_source=kFireVertexShader.c_str();fragment_source=kFireFragmentShader.c_str();break;
    case ShaderProgramId::Water:vertex_source=kWaterVertexShaderFull.c_str();fragment_source=kWaterFragmentShader.c_str();break;
    case ShaderProgramId::Vegetation:vertex_source=kVegetationVertexShader.c_str();fragment_source=kVegetationFragmentShader.c_str();break;
    case ShaderProgramId::Fire:vertex_source=kFireVertexShaderSource.c_str();fragment_source=kFireFragmentShaderSource.c_str();break;
    case ShaderProgramId::Object:vertex_source=kObjectVertexShader;fragment_source=kObjectFragmentShader;break;
    case ShaderProgramId::TexturedObject:vertex_source=kTexturedVertexShader;fragment_source=kTexturedFragmentShader;break;
    case ShaderProgramId::InstancedTexturedObject:vertex_source=kInstancedTexturedVertexShader;fragment_source=kTexturedFragmentShader;break;
    case ShaderProgramId::PbrObject:vertex_source=kPbrTexturedVertexShader;fragment_source=kPbrFragmentShader;break;
    case ShaderProgramId::PbrInstanced:vertex_source=kPbrInstancedVertexShader;fragment_source=kPbrFragmentShader;break;
    default:throw std::invalid_argument("unsupported OpenGL shader program identity");
  }

  GLuint vertex = 0, fragment = 0, program = 0, vao = 0;
  try {
    vertex = compile_shader(impl_->gl,GL_VERTEX_SHADER,vertex_source);
    fragment = compile_shader(impl_->gl,GL_FRAGMENT_SHADER,fragment_source);
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

ResourceBytes OpenGLRenderDevice::live_resource_bytes() const {
  return impl_->validator.live_resource_bytes();
}
ResourceTelemetry OpenGLRenderDevice::resource_telemetry() const {
  return impl_->validator.resource_telemetry();
}
void OpenGLRenderDevice::reset_resource_telemetry() {
  impl_->validator.reset_resource_telemetry();
}
void OpenGLRenderDevice::set_resource_byte_budget(std::uint64_t bytes) {
  impl_->validator.set_resource_byte_budget(bytes);
}

OpenGLRenderDevice::ResourceCounts OpenGLRenderDevice::live_resource_counts() const {
  return {impl_->buffers.size(),impl_->textures.size(),impl_->pipelines.size(),impl_->render_targets.size()};
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
  if(!impl_->alpha_blend_state || *impl_->alpha_blend_state!=pipeline.desc.alpha_blend) {
    if(pipeline.desc.alpha_blend) {
      impl_->gl.Enable(GL_BLEND);
      impl_->gl.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    }else impl_->gl.Disable(GL_BLEND);
    impl_->alpha_blend_state=pipeline.desc.alpha_blend;
  }
  if(!impl_->depth_write_state || *impl_->depth_write_state!=pipeline.desc.depth_write) {
    impl_->gl.DepthMask(pipeline.desc.depth_write?GL_TRUE:GL_FALSE);
    impl_->depth_write_state=pipeline.desc.depth_write;
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

void OpenGLRenderDevice::begin_render_pass(const RenderPassDesc& desc) {
  // Validate the full descriptor/order before changing any OpenGL state.
  impl_->validator.begin_render_pass(desc);
  const auto framebuffer=desc.target.kind==RenderTargetKind::Window?0U:
      impl_->render_targets.at(desc.target.handle.id).framebuffer;
  impl_->gl.BindFramebuffer(GL_FRAMEBUFFER,framebuffer);
  impl_->gl.Viewport(0,0,static_cast<GLsizei>(desc.target.width),
                          static_cast<GLsizei>(desc.target.height));
  GLbitfield mask=0;
  if(desc.color_load==AttachmentLoadOp::Clear) {
    const auto& c=desc.clear_color;
    impl_->gl.ClearColor(c[0],c[1],c[2],c[3]);
    mask|=GL_COLOR_BUFFER_BIT;
  }
  if(desc.depth_load==AttachmentLoadOp::Clear) {
    // A previous transparent draw may have disabled depth writes. glClear
    // respects DepthMask; restore it and synchronize the draw-state cache.
    if(!impl_->depth_write_state || !*impl_->depth_write_state) {
      impl_->gl.DepthMask(GL_TRUE);
      impl_->depth_write_state=true;
    }
    impl_->gl.ClearDepth(desc.clear_depth);
    mask|=GL_DEPTH_BUFFER_BIT;
  }
  if(mask)impl_->gl.Clear(mask);
}

std::uint64_t OpenGLRenderDevice::active_render_pass_id() const noexcept {
  return impl_->validator.active_render_pass_id();
}

void OpenGLRenderDevice::end_render_pass() {
  impl_->validator.end_render_pass();
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
std::vector<std::uint8_t> OpenGLRenderDevice::capture_rgba(int width,int height) {
  if(width<=0 || height<=0 || static_cast<std::uint64_t>(width)*height>4'000'000)
    throw std::invalid_argument("invalid framebuffer dimensions for capture");
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);
  impl_->gl.ReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  return pixels;
}
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
