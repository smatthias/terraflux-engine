#include <terraflux/graphics/opengl_device.hpp>
#include <terraflux/graphics/terrain_shading.hpp>
#include <terraflux/graphics/water_shading.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/graphics/fire_effects.hpp>
#include <terraflux/graphics/weather.hpp>

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
// Where the ropes of a waterfall run across its face (0 between them, 1 in a rope): the
// ground shader paints them and the spray leaves from them. across/along are metres in the
// flow frame, height 0..1 grows with the fall (the ropes divide lower down), time in seconds.
float waterfallRope(float across, float along, float elevation, float height, float time) {
  float wander = 1.5 * sin(along * 0.11 + elevation * 0.05) + 0.6 * sin(elevation * 0.23 - time * 1.7);
  float ropeAcross = across + wander;
  return mix(terrainNoise(vec2(ropeAcross * 0.09, 3.7 + along * 0.04)).x,
             terrainNoise(vec2(ropeAcross * 0.19, 5.1 + along * 0.06)).x, height);
}
)GLSL";

// Daylight, fog, clouds and precipitation shared by every world shader. The
// CPU twins in weather.cpp (atmosphere_shade, atmosphere_haze,
// atmosphere_sky_color) mirror the cloud-free parts; the cloud ray march and
// the orographic modulation are GPU-only. With atmoEye.w == 0 (neutral block)
// every helper returns its input or the legacy formula unchanged.
constexpr char kAtmosphereBlockGlsl[] = R"GLSL(
layout(std140, binding = 15) uniform Atmosphere {
  vec4 camForward;     // xyz basis, w tan half fov x
  vec4 camRight;       // xyz basis, w tan half fov y
  vec4 camUp;          // xyz basis, w moon altitude degrees
  vec4 atmoEye;        // camera position, w enabled
  vec4 atmoSun;        // unit direction toward the light (sun or moon), w direct intensity
  vec4 atmoSunColor;   // rgb, w ambient intensity
  vec4 atmoAmbient;    // rgb, w daylight
  vec4 atmoZenith;     // rgb, w sun altitude degrees
  vec4 atmoHorizon;    // rgb, w lightning flash
  vec4 atmoFog;        // rgb, w visibility meters
  vec4 atmoCloud;      // base, top, coverage, density
  vec4 atmoCloudShape; // detail, drift x, drift z, type
  vec4 atmoFieldOrigin;// x0, z0, 1/width, 1/height
  vec4 atmoFieldScale; // base shift scale, thickening strength, top lift, pattern scale
  vec4 atmoWind;       // direction x, z, speed, time
  vec4 atmoWeather;    // precipitation, snow, wetness, stars
  vec4 atmoSunDisc;    // true sun direction, w valley floor altitude
  vec4 atmoStrike;     // lightning channel midpoint, w flash intensity
  vec4 atmoRain;       // drop diameter mm, ponding, rainbow, convection
  vec4 atmoLife;       // cloud lifecycle time, flood rise, moon diameter degrees, rock relief m
  vec4 atmoMoon;       // unit direction toward the moon, w illuminated fraction
  vec4 atmoCloudSun;   // cloud light color, w intensity
  vec4 atmoCloudLight; // unit direction of the cloud light, w moonlight intensity
  vec4 atmoHorizonAway;// horizon color opposite the sun
  vec4 atmoSnow;       // freezing level m, melting layer m, runoff, snow cover
};
)GLSL";
const std::string kAtmosphereGlsl = std::string(kAtmosphereBlockGlsl) + R"GLSL(
layout(binding = 1) uniform sampler2D OrographicField;
bool atmosphereEnabled() { return atmoEye.w > 0.5; }
// Per-pixel start offset of the cloud march (fragment stages set it from the
// fragment coordinate) turns slicing bands into fine grain.
float atmosphereJitter = 0.5;
float atmosphereDither(vec2 fragCoord) { return terrainHash(fragCoord * 0.731 + vec2(0.17, 0.29)); }

// Orographic texel: thickening, dissipation, base shift (0.5 neutral), precipitation
// (0.5 neutral). Neutral outside the terrain footprint.
vec4 orographicAt(vec2 xz) {
  vec2 uv = (xz - atmoFieldOrigin.xy) * atmoFieldOrigin.zw;
  vec4 t = texture(OrographicField, clamp(uv, 0.0, 1.0));
  vec2 outside = max(max(-uv, uv - 1.0), 0.0);
  float inside = 1.0 - smoothstep(0.0, 0.08, max(outside.x, outside.y));
  return mix(vec4(0.0, 0.0, 0.5, 0.5), t, inside);
}
// Four octaves, each rotated so no lattice direction survives, with a fine erosion octave
// that frays the edges instead of leaving the blocky corners of plain value noise.
vec2 cloudRotate(vec2 p, float c, float s) { return vec2(c * p.x - s * p.y, s * p.x + c * p.y); }
float cloudNoise(vec2 p) {
  float n = 0.50 * terrainNoise(p).x +
            0.26 * terrainNoise(cloudRotate(p, 0.80, 0.60) * 2.03 + vec2(5.2, 1.3)).x +
            0.15 * terrainNoise(cloudRotate(p, -0.28, 0.96) * 4.11 + vec2(9.1, 7.7)).x +
            0.09 * terrainNoise(cloudRotate(p, 0.53, -0.85) * 8.7 + vec2(2.9, 4.4)).x;
  return clamp((n - 0.5) * 1.9 + 0.5, 0.0, 1.0);
}
// 3D value noise for the volume: erosion of the cell edges and the lifecycle field.
float cloudHash3(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}
float cloudNoise3(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  vec3 u = f * f * (3.0 - 2.0 * f);
  float c000 = cloudHash3(i), c100 = cloudHash3(i + vec3(1, 0, 0)), c010 = cloudHash3(i + vec3(0, 1, 0)), c110 = cloudHash3(i + vec3(1, 1, 0));
  float c001 = cloudHash3(i + vec3(0, 0, 1)), c101 = cloudHash3(i + vec3(1, 0, 1)), c011 = cloudHash3(i + vec3(0, 1, 1)), c111 = cloudHash3(i + vec3(1, 1, 1));
  return mix(mix(mix(c000, c100, u.x), mix(c010, c110, u.x), u.y), mix(mix(c001, c101, u.x), mix(c011, c111, u.x), u.y), u.z);
}
// Cell lifecycle (0 fed .. 1 decayed): convective cells are crisp and dense while an
// updraft feeds them; once it dies (night, cold ground, the lee of a ridge) dry air mixes
// in, the edges fray, the cloud thins and the cell vanishes. The field drifts with the
// lifecycle clock so cells form and dissolve over about half an hour of weather time.
float cloudDecayAt(vec2 q, vec4 oro) {
  float feed = clamp(atmoRain.w + 0.6 * oro.x - 0.5 * oro.y, 0.0, 1.0);
  float life = cloudNoise3(vec3(q * 2.1 + vec2(11.0, 3.0), atmoLife.x / 1800.0));
  return smoothstep(feed, feed + 0.35, life) * atmoCloudShape.x;
}
// Cloud density 0..1 at a world point. The layer sits between the base and
// top altitudes, shifted and deepened by the orographic texel: the base rises
// in the lee, the top lifts and the coverage grows on windward slopes, and
// the lee dissipation thins it. Cumuliform detail raises the threshold with
// height so towers form; stratiform layers stay flat and soft.
float cloudDensityAt(vec3 p, vec4 oro, bool detailed) {
  float base = atmoCloud.x + (oro.z * 2.0 - 1.0) * atmoFieldScale.x;
  float top = atmoCloud.y + oro.x * atmoFieldScale.z;
  float depth = max(top - base, 1.0);
  float h = (p.y - base) / depth;
  if (h < 0.0 || h > 1.0) return 0.0;
  float detail = atmoCloudShape.x;
  float coverage = clamp(atmoCloud.z * (1.0 + atmoFieldScale.y * oro.x) * (1.0 - oro.y), 0.0, 1.0);
  vec2 q = (p.xz - atmoCloudShape.yz) / atmoFieldScale.w;
  if (atmoCloudShape.w > 0.5 && atmoCloudShape.w < 1.5) {
    // Cirrus: ice streaks drawn out along the wind.
    vec2 along = normalize(atmoWind.xy + vec2(1e-4, 0.0)), across = vec2(-along.y, along.x);
    q = vec2(dot(q, along) * 0.22, dot(q, across) * 1.6);
  }
  q += vec2(0.37, -0.23) * h * detail;
  float n = cloudNoise(q);
  float decay = cloudDecayAt(q, oro);
  // Towers: the threshold rises with height so only the strongest cells reach the top. Fed
  // cells keep a crisp, cauliflower edge carved by 3D erosion; decaying cells get a wide,
  // fuzzy transition, thin out and finally drop below the threshold.
  float threshold = (1.0 - coverage) + detail * 0.55 * h + 0.18 * decay;
  float edge = 0.16 + 0.25 * (1.0 - detail) + 0.5 * decay;
  float body = smoothstep(threshold, threshold + edge, n);
  body *= body * (3.0 - 2.0 * body);
  float profile = smoothstep(0.0, 0.12, h) * (1.0 - smoothstep(0.75 + 0.2 * (1.0 - detail), 1.0, h));
  float density = body * profile;
  if (detailed && density > 0.001 && detail > 0.05) {
    // Volume: two octaves of 3D noise erode the shell (strongly at the edges, little in the
    // core) so towers bulge and hollow from every angle; fuzzy remnants erode softly.
    vec3 e = (p - vec3(atmoCloudShape.y, 0.0, atmoCloudShape.z)) / 260.0;
    float erosion = 0.65 * cloudNoise3(e) + 0.35 * cloudNoise3(e * 2.3 + vec3(4.1, 7.3, 1.9));
    float carve = (0.55 - 0.25 * decay) * detail;
    density = clamp((density - carve * erosion * (1.0 - 0.6 * density)) / max(1.0 - carve * 0.5, 0.3), 0.0, 1.0);
  }
  return density * (1.0 - 0.7 * decay) * atmoCloud.w;
}
// Premultiplied in-scattered light (rgb) and transmittance (a) of the cloud
// layer along a ray segment. Geometry passes its own distance; the sky passes
// a large one. Nothing beyond a few visibility lengths is marched.
vec4 cloudAlong(vec3 origin, vec3 dir, float maxDist) {
  if (!atmosphereEnabled() || atmoCloud.z <= 0.001 || atmoCloud.w <= 0.001) return vec4(0.0, 0.0, 0.0, 1.0);
  float lo = atmoCloud.x - atmoFieldScale.x, hi = atmoCloud.y + atmoFieldScale.z;
  float t0, t1;
  if (abs(dir.y) < 1e-5) {
    if (origin.y < lo || origin.y > hi) return vec4(0.0, 0.0, 0.0, 1.0);
    t0 = 0.0; t1 = maxDist;
  } else {
    float ta = (lo - origin.y) / dir.y, tb = (hi - origin.y) / dir.y;
    t0 = max(min(ta, tb), 0.0); t1 = min(max(ta, tb), maxDist);
  }
  // Beyond a few visibility lengths the haze hides the layer anyway.
  t1 = min(t1, t0 + min(3.0 * atmoFog.w, 16000.0));
  if (t1 <= t0) return vec4(0.0, 0.0, 0.0, 1.0);
  // A fixed step of a twenty-fourth of the layer depth (so neighbouring pixels
  // sample the same pattern and no seams appear), stretched only when a long
  // path would exceed the budget of 96 samples.
  float depthStep = max(atmoCloud.y - atmoCloud.x, 50.0) / 24.0;
  float dt = max(depthStep, (t1 - t0) / 96.0);
  int steps = max(int(ceil((t1 - t0) / dt)), 1);
  const float kSigma = 0.0055; // extinction per meter at density one
  // The cloud light is the true sun while it still reaches the layer (from the side and, at
  // sunset, from below the base in deep reds), the moon afterwards; a Henyey-Greenstein
  // forward lobe brightens the edges seen against the light (silver linings, moonlit rims).
  vec3 lightDir = atmoCloudLight.xyz;
  // Moonlit clouds read brighter than their photometry (the eye adapts): the moon's share
  // of the cloud light is lifted, the rest is the sun as it is.
  float moonShare = atmoCloudSun.w > 0.0 ? clamp(atmoCloudLight.w / atmoCloudSun.w, 0.0, 1.0) : 0.0;
  vec3 light = atmoCloudSun.rgb * atmoCloudSun.w * (1.0 + 1.6 * moonShare), ambient = atmoAmbient.rgb * atmoSunColor.w;
  float cosView = dot(dir, lightDir);
  float hg = (1.0 - 0.36) / (4.0 * 3.14159265 * pow(1.0 + 0.36 - 1.2 * cosView, 1.5));
  float forward = clamp(hg * 2.5, 0.0, 3.0);
  float transmittance = 1.0;
  vec3 scattered = vec3(0.0);
  for (int i = 0; i < steps; ++i) {
    float t = t0 + (float(i) + atmosphereJitter) * dt;
    vec3 p = origin + dir * t;
    vec4 oro = orographicAt(p.xz);
    float d = cloudDensityAt(p, oro, true);
    if (d <= 0.002) continue;
    // Self-shadowing: optical depth toward the light along a widening cone (three cheap
    // samples); Beer's extinction times a "powder" term that darkens thin edges so the
    // bulges read as volume, and a darker base from the ambient gradient.
    float toward = 0.5 * cloudDensityAt(p + lightDir * 60.0, oro, false) +
                   0.35 * cloudDensityAt(p + lightDir * 170.0, oro, false) +
                   0.15 * cloudDensityAt(p + lightDir * 360.0, oro, false);
    float lit = exp(-toward * 2.6) * (0.55 + 0.45 * (1.0 - exp(-d * 4.0)));
    float h = clamp((p.y - atmoCloud.x) / max(atmoCloud.y - atmoCloud.x, 1.0), 0.0, 1.0);
    // Sunset: a low sun reddens the lit faces and the undersides it reaches; the shaded faces
    // keep the cool ambient of the sky opposite.
    vec3 color = ambient * (1.1 + 1.1 * h) * (1.0 - 0.3 * d) + light * (0.78 * lit + 0.55 * forward * exp(-d * 2.5)) +
                 vec3(0.55, 0.62, 0.80) * atmoHorizon.w;
    // A lightning channel lights the cloud from inside: a point light at its midpoint.
    float strikeDistance = length(p - atmoStrike.xyz);
    color += vec3(0.70, 0.78, 1.0) * (atmoStrike.w * 1.5 / (1.0 + strikeDistance * strikeDistance / 250000.0));
    color = mix(color, atmoFog.rgb, 1.0 - exp(-t / (2.5 * atmoFog.w)));
    float alpha = 1.0 - exp(-d * kSigma * dt);
    scattered += transmittance * alpha * color;
    transmittance *= 1.0 - alpha;
    if (transmittance < 0.01) break;
  }
  return vec4(scattered, transmittance);
}
// Transmittance toward the light through the cloud layer above a ground point.
float cloudShadow(vec3 p) {
  if (!atmosphereEnabled() || atmoCloud.z <= 0.001 || atmoSun.y <= 0.05) return 1.0;
  float t = (atmoCloud.x - p.y) / atmoSun.y;
  if (t < 0.0) return 1.0;
  vec3 q = p + atmoSun.xyz * t;
  vec4 oro = orographicAt(q.xz);
  float depth = max(atmoCloud.y - atmoCloud.x, 1.0);
  float d = cloudDensityAt(vec3(q.x, atmoCloud.x + 0.2 * depth, q.z), oro, false) + cloudDensityAt(vec3(q.x, atmoCloud.x + 0.55 * depth, q.z), oro, false);
  return mix(1.0, exp(-d * 1.6), 0.85);
}
// Surface lighting: ambient plus direct light with cloud shadow, wet darkening
// and lightning when the atmosphere is active; the caller's legacy light and
// ambient shade otherwise (CPU twin: atmosphere_shade / terrain_surface_color).
vec3 atmosphereLit(vec3 albedo, vec3 n, float diffuse, vec3 legacyLight, float legacyAmbient, vec3 ambientTint, vec3 worldPos) {
  if (!atmosphereEnabled()) return albedo * (legacyAmbient * ambientTint + (1.0 - legacyAmbient) * diffuse);
  float shadow = cloudShadow(worldPos);
  vec3 wet = mix(vec3(1.0), vec3(0.72, 0.74, 0.78), atmoWeather.z);
  // Sheet lightning brightens everything; a channel adds a point light falling off over ~1 km.
  float strikeDistance = length(worldPos - atmoStrike.xyz);
  vec3 flash = vec3(0.55, 0.62, 0.80) * (0.35 * atmoHorizon.w + 0.9 * atmoStrike.w / (1.0 + strikeDistance * strikeDistance / 1000000.0));
  return albedo * wet * (atmoAmbient.rgb * atmoSunColor.w * ambientTint + atmoSunColor.rgb * atmoSun.w * diffuse * shadow + flash);
}
vec3 atmosphereLightDirection(vec3 legacyLight) { return atmosphereEnabled() ? atmoSun.xyz : legacyLight; }
// Clouds between the eye and the surface, then distance haze (CPU twin of the haze: atmosphere_haze).
vec3 applyAtmosphere(vec3 color, vec3 worldPos) {
  if (!atmosphereEnabled()) return color;
  vec3 toFrag = worldPos - atmoEye.xyz;
  float dist = length(toFrag);
  vec3 dir = toFrag / max(dist, 1e-3);
  vec4 clouds = cloudAlong(atmoEye.xyz, dir, dist);
  color = color * clouds.a + clouds.rgb;
  float haze = 1.0 - exp(-dist / atmoFog.w);
  return mix(color, atmoFog.rgb, haze);
}
// Clear-sky color for a view direction (CPU twin: atmosphere_sky_color).
vec3 atmosphereSkyColor(vec3 dir) {
  float up = clamp(dir.y, -1.0, 1.0);
  // Below the horizon: distant ground under haze (the terrain is an island).
  vec3 ground = mix(vec3(0.14, 0.15, 0.13), atmoFog.rgb, 0.25) * (0.15 + 0.7 * atmoAmbient.w);
  // The horizon is warm toward the sun and cool opposite it (strongest around sunrise and sunset).
  vec2 sunH = atmoSunDisc.xz, dirH = dir.xz;
  float toward = length(sunH) > 1e-6 && length(dirH) > 1e-6 ? 0.5 + 0.5 * dot(normalize(sunH), normalize(dirH)) : 0.5;
  vec3 horizonColor = mix(atmoHorizonAway.rgb, atmoHorizon.rgb, pow(toward, 1.5));
  vec3 sky = up >= 0.0 ? mix(horizonColor, atmoZenith.rgb, pow(up, 0.55))
                       : mix(horizonColor, ground, clamp(-up * 6.0, 0.0, 1.0));
  // Aerial perspective: the path through the hazy boundary layer (1.5 km
  // above the eye, or down to the valley floor) whitens the horizon and greys
  // an overcast sky toward the fog color.
  float path = up >= 0.0 ? 1500.0 / max(up, 0.02) : max(atmoEye.y - atmoSunDisc.w, 50.0) / max(-up, 0.02);
  sky = mix(sky, atmoFog.rgb, 1.0 - exp(-min(path, 200000.0) / atmoFog.w));
  vec3 sunDir = atmoSunDisc.xyz;
  float cosSun = clamp(dot(dir, sunDir), -1.0, 1.0);
  float altitude = atmoZenith.w;
  float visible = smoothstep(-1.5, 0.5, altitude);
  float glow = (0.22 * pow(max(cosSun, 0.0), 24.0) + 0.5 * pow(max(cosSun, 0.0), 400.0)) * visible * smoothstep(-6.0, 2.0, altitude);
  float disc = smoothstep(0.99955, 0.99975, cosSun) * visible;
  vec3 sunLightColor = dot(atmoSun.xyz, sunDir) > 0.999 ? atmoSunColor.rgb : vec3(1.0, 0.62, 0.30);
  sky += sunLightColor * glow + mix(sunLightColor, vec3(1.0), 0.5) * (disc * 3.0);
  sky += vec3(0.55, 0.62, 0.80) * (0.45 * atmoHorizon.w);
  return clamp(sky, 0.0, 1.0);
}
// Puddles: after rain, flat ground holds a film of water that reflects the sky (Fresnel)
// and darkens the soil; it drains away as the ponding falls.
vec3 applyPuddles(vec3 color, vec3 worldPos, vec3 unitNormal, float submerged) {
  if (!atmosphereEnabled() || atmoRain.y <= 0.001) return color;
  float flat_ = 1.0 - smoothstep(8.0, 16.0, degrees(acos(clamp(unitNormal.y, 0.0, 1.0)))); // gentle ground holds water in its hollows
  // Puddles collect in the hollows of a two-scale noise; they grow and merge as the ponding rises.
  float hollows = 0.6 * terrainNoise(worldPos.xz * 0.18).x + 0.4 * terrainNoise(worldPos.xz * 0.55 + vec2(7.0, 3.0)).x;
  float mask = smoothstep(0.62 - 0.3 * atmoRain.y, 0.66 - 0.3 * atmoRain.y, hollows);
  float puddle = flat_ * mask * (1.0 - submerged) * smoothstep(0.0, 0.3, atmoRain.y);
  if (puddle <= 0.001) return color;
  vec3 view = normalize(atmoEye.xyz - worldPos);
  float fresnel = 0.06 + 0.94 * pow(1.0 - max(view.y, 0.0), 4.0);
  vec3 reflection = atmosphereSkyColor(reflect(-view, vec3(0.0, 1.0, 0.0)));
  vec3 wet = color * 0.45;
  return mix(color, mix(wet, reflection, fresnel), puddle);
}
// Rainbow: the primary bow 40.6..42.4 degrees from the anti-solar point (red outside), the
// fainter secondary 50.1..53.5 (reversed) and Alexander's dark band between; only with sunshine
// behind the observer and rain in front (atmoRain.z).
vec3 spectrum(float t) {
  return clamp(vec3(1.2 * t * t + 0.15 * (1.0 - t), 1.0 - 2.6 * abs(t - 0.45), 1.3 * (1.0 - t) - 0.3), 0.0, 1.0);
}
// Spectral weight of the primary and secondary bows for a view direction and a light
// direction (the bow is centred on the anti-light point); no gating.
vec3 bowWeight(vec3 dir, vec3 lightDir) {
  float a = degrees(acos(clamp(dot(dir, -lightDir), -1.0, 1.0)));
  float t1 = (a - 40.6) / 1.8, t2 = (53.5 - a) / 3.4;
  vec3 bow = vec3(0.0);
  if (t1 > 0.0 && t1 < 1.0) bow += spectrum(t1) * smoothstep(0.0, 0.2, t1) * (1.0 - smoothstep(0.8, 1.0, t1)) * 0.45;
  if (t2 > 0.0 && t2 < 1.0) bow += spectrum(t2) * smoothstep(0.0, 0.25, t2) * (1.0 - smoothstep(0.75, 1.0, t2)) * 0.14;
  return bow;
}
vec3 rainbowAt(vec3 dir) {
  if (!atmosphereEnabled() || atmoRain.z <= 0.001) return vec3(0.0);
  return bowWeight(dir, atmoSunDisc.xyz) * atmoRain.z * smoothstep(-0.05, 0.08, dir.y);
}
float alexanderBand(vec3 dir) {
  if (!atmosphereEnabled() || atmoRain.z <= 0.001) return 1.0;
  float a = degrees(acos(clamp(dot(dir, -atmoSunDisc.xyz), -1.0, 1.0)));
  return 1.0 - 0.08 * atmoRain.z * smoothstep(42.0, 43.5, a) * (1.0 - smoothstep(49.0, 50.5, a));
}
)GLSL";

// Runoff and snow cover on the ground (terrain and wildfire programs): rain below the
// freezing level wets and glosses the slopes, gathers into streams along the drainage and
// pours over cliffs as white water that the crags split into ropes, that speeds up and
// aerates as it falls, bursts white where it strikes a protruding rock and boils in the
// plunge pool; above the freezing level the fresh snow whitens the ground.
constexpr char kRunoffGlsl[] = R"GLSL(
layout(binding = 2) uniform sampler2D RunoffField;
const float kRunoffDropScale = 250.0;
// x flow, yz drainage direction scaled by the steepness (length 1 at 60 degrees, zero in a
// pit), w drop (0.85 sqrt(drop / 250 m)) or a plunge pool (0.9 .. 1). The lookup is warped
// by a few metres of noise so the lattice's straight texel edges never show.
vec4 runoffAt(vec2 xz) {
  vec3 warp = terrainNoise(xz * 0.07 + vec2(3.1, 9.4));
  vec2 uv = (xz + (warp.yz * 0.07 + vec2(warp.x - 0.5)) * 3.0 - atmoFieldOrigin.xy) * atmoFieldOrigin.zw;
  // Texel i sits at x0 + i * spacing: map onto texel centres for the bilinear fetch.
  vec2 size = vec2(textureSize(RunoffField, 0));
  vec4 t = texture(RunoffField, (clamp(uv, 0.0, 1.0) * (size - 1.0) + 0.5) / size);
  vec2 outside = max(max(-uv, uv - 1.0), 0.0);
  float inside = 1.0 - smoothstep(0.0, 0.05, max(outside.x, outside.y));
  return vec4(t.x * inside, (t.yz * 2.0 - 1.0) * inside, t.w * inside);
}
vec3 applySnowCover(vec3 color, vec3 worldPos, vec3 n, float elevation) {
  if (!atmosphereEnabled() || atmoSnow.w <= 0.001) return color;
  float cover = atmoSnow.w * smoothstep(atmoSnow.x - 120.0, atmoSnow.x + 80.0, elevation);
  if (cover <= 0.001) return color;
  float diffuse = max(dot(n, atmoSun.xyz), 0.0);
  vec3 snow = vec3(0.93, 0.94, 0.96) * (atmoAmbient.rgb * atmoSunColor.w * vec3(0.74, 0.82, 0.98) + atmoSunColor.rgb * atmoSun.w * diffuse);
  return mix(color, snow, cover * 0.9);
}
vec3 applyRunoff(vec3 color, vec3 worldPos, vec3 n, float elevation, float submerged) {
  if (!atmosphereEnabled() || atmoSnow.z <= 0.001) return color;
  // Snow, not water, falls above the freezing level.
  float liquid = 1.0 - smoothstep(atmoSnow.x - 150.0, atmoSnow.x + 50.0, elevation);
  float running = atmoSnow.z * liquid * (1.0 - submerged);
  if (running <= 0.001) return color;
  vec4 r = runoffAt(worldPos.xz);
  float flow = r.x, drop = kRunoffDropScale * (r.w / 0.85) * (r.w / 0.85);
  float slope = degrees(acos(clamp(n.y, 0.0, 1.0)));          // of the rendered surface
  float latticeSlope = 60.0 * length(r.yz);                   // of the drainage lattice
  // A cliff (by the lattice; the rendered crest above it, still level, stays dry) with a
  // stream pouring over it.
  // A cliff (by the lattice; the rendered crest above it, still level, stays dry) with a
  // stream pouring over it.
  float fall = smoothstep(40.0, 60.0, latticeSlope) * smoothstep(12.0, 32.0, slope) * smoothstep(0.3, 0.6, flow);
  float time = atmoWind.w;
  // The drainage direction of the lattice (smooth across the face, unlike the normal of
  // the refined mesh) gives the along/across frame of the flow; the normal is a fallback.
  vec2 down = length(r.yz) > 0.03 ? normalize(r.yz) : normalize(n.xz + vec2(1e-5, 0.0));
  vec2 perp = vec2(-down.y, down.x);
  float steep = smoothstep(8.0, 35.0, slope);
  float along = dot(worldPos.xz, down), across = dot(worldPos.xz, perp);
  // Sheet flow: water seeps slowly over gentle ground and streams down steep rock, in
  // streaks that scroll downhill at a speed set by the slope; wet ground darkens and glosses.
  float speed = 0.3 + 2.5 * steep;
  float streak = terrainNoise(vec2(across * 0.8, along * 0.25 - time * speed)).x;
  float film = running * (0.25 + 0.75 * flow);
  color *= 1.0 - film * (0.2 + 0.25 * steep);
  vec3 view = normalize(atmoEye.xyz - worldPos);
  vec3 hv = normalize(view + atmoSun.xyz);
  float gloss = pow(max(dot(n, hv), 0.0), 60.0) * film * atmoSun.w * (0.5 + 0.5 * streak) * (1.0 - 0.7 * smoothstep(35.0, 50.0, slope));
  color += atmoSunColor.rgb * gloss * 0.6;
  vec3 waterLight = atmoAmbient.rgb * atmoSunColor.w * 2.5 + atmoSunColor.rgb * atmoSun.w * 0.9;
  vec3 white = vec3(0.92, 0.95, 1.0) * waterLight;
  // Streams: bright threads of water where the drainage gathers on the slopes (the cliffs
  // themselves carry the ropes of the waterfall below instead).
  float steepRock = smoothstep(35.0, 50.0, latticeSlope);
  float thread = smoothstep(0.45, 0.7, flow) * steep * (1.0 - steepRock) * running * smoothstep(0.5, 0.8, streak);
  color = mix(color, vec3(0.75, 0.82, 0.9) * waterLight, thread * 0.7);
  // Plunge pool: the cascade lands on the gentle ground below (a drop recorded on a texel
  // that is not itself falling) and boils there: churning white foam spreading outward.
  float pool = running * (1.0 - steepRock) * smoothstep(0.6, 0.9, r.w) * smoothstep(0.3, 0.6, flow);
  if (pool > 0.001) {
    float boil = terrainNoise(worldPos.xz * 0.7 + vec2(time * 1.1, -time * 0.8)).x * 0.6 +
                 terrainNoise(worldPos.xz * 1.8 + vec2(-time * 1.7, time * 2.3)).x * 0.4;
    float foamHere = smoothstep(0.3, 0.6, boil + 0.25 * smoothstep(0.85, 1.0, r.w));
    color = mix(color, white, pool * foamHere * 0.85);
  }
  // Waterfalls: a stream meeting a cliff shoots over the edge. The water is not one sheet:
  // the crags of the cliff (the same ridged field that displaces the rock) split it into
  // ropes that run down the creases and swing round the crests, and the ropes keep dividing
  // as the fall gets longer. The water accelerates as it falls (sqrt(2 g h), braked by the
  // rock), so its texture stretches into long streaks and it turns whiter with entrained
  // air; where a crest of rock protrudes into the fall the water strikes it and bursts.
  float pour = smoothstep(0.1, 0.5, fall) * running;
  if (pour > 0.001) {
    float height = smoothstep(0.0, 120.0, drop);
    float fallSpeed = clamp(sqrt(19.62 * drop), 2.5, 18.0);
    vec3 crag = ridgedFbm(worldPos.xz * kCragFrequency);
    vec3 fineCrag = ridgedFbm(worldPos.xz * 0.21 + vec2(2.0, 8.0));
    float crest = crag.x * 0.7 + fineCrag.x * 0.3;       // 1 on a protruding rock, 0 in a crease
    // The crests push the ropes sideways: the across coordinate is warped by the lateral
    // crag gradient, and the ropes split finer lower down.
    float deflect = (crag.x - 0.5) * 3.0 + (fineCrag.x - 0.5) * 1.2;
    // (Blending two fixed frequencies rather than scaling one: a frequency that varied with
    // the interpolated drop would sweep the noise phase into rings round every texel.)
    // The ropes wander down the face (a slow meander plus the surge of the flow) and fray
    // into strands at their edges.
    float rope = waterfallRope(across + deflect, along, elevation, height, time);
    float strands = terrainNoise(vec2((across + deflect) * 0.9, elevation * 0.08 + time * 2.0)).x;
    float core = smoothstep(0.47, 0.58, rope + (strands - 0.5) * 0.26);   // a rope has a crisp, frayed edge: bare rock between
    float inCrease = 0.55 + 0.45 * smoothstep(0.7, 0.3, crest);
    float ribbon = max(core * inCrease, smoothstep(0.74, 0.9, rope)); // the strongest ropes cross the crests anyway
    // The cliff's own edge softens the lattice: a texel's pour fades in over noise, not a line.
    float ropes = pour * ribbon * smoothstep(0.03, 0.35, fall + (strands - 0.5) * 0.25);
    // Texture: streaks scroll down the fall at its speed and stretch with it; lower down
    // the water is whiter (aerated) and more fragmented.
    float speedMix = smoothstep(2.5, 18.0, fallSpeed);
    float slowStreak = terrainNoise(vec2((across + deflect) * 0.6, (elevation + time * 4.0) * 0.2)).x * 0.6 +
                       terrainNoise(vec2((across + deflect) * 1.4, (elevation + time * 4.0) * 0.5 + 7.0)).x * 0.4;
    float fastStreak = terrainNoise(vec2((across + deflect) * 0.6, (elevation + time * 16.0) * 0.035)).x * 0.6 +
                       terrainNoise(vec2((across + deflect) * 1.4, (elevation + time * 16.0) * 0.09 + 7.0)).x * 0.4;
    float foam = mix(slowStreak, fastStreak, speedMix);
    float aerated = 0.35 - 0.2 * height;
    float streaks = smoothstep(aerated, aerated + 0.4, foam);
    // The rope covers the rock (its core is opaque water, not a tint over the facets); the
    // streaks and strands shade the water itself: bright foam over darker glassy water.
    float cover = ropes * (0.85 + 0.15 * streaks);
    vec3 waterColor = white * (0.72 + 0.28 * streaks) * (0.85 + 0.15 * strands);
    // Impact on a protruding crest: the rope bursts into a churning white splash.
    float hit = smoothstep(0.62, 0.85, crest) * pour * smoothstep(0.25, 0.6, rope) * smoothstep(4.0, 20.0, drop);
    float burst = terrainNoise(vec2(across * 2.5 + time * 3.0, elevation * 0.5 - time * 6.0)).x;
    hit *= 0.55 + 0.45 * burst;
    color = mix(color, waterColor, clamp(cover, 0.0, 1.0));
    color = mix(color, white, clamp(hit * 0.9, 0.0, 1.0));
    // Between the ropes the rock is wet and glossy, with a thin film of mist.
    color *= 1.0 - pour * 0.12 * (1.0 - ribbon);
    vec2 driftXZ = atmoWind.xy * (time * 0.3);
    float mist = pour * (0.2 + 0.6 * height) * (0.35 + 0.65 * terrainNoise(worldPos.xz * 0.5 + driftXZ + vec2(0.0, time * 0.5)).x);
    vec3 viewDir = -view;
    float glint = pow(max(dot(viewDir, atmoSun.xyz), 0.0), 6.0) * atmoSun.w;
    vec3 mistColor = vec3(0.85, 0.9, 1.0) * (atmoAmbient.rgb * atmoSunColor.w * 2.0 + atmoSunColor.rgb * atmoSun.w * (0.5 + 1.5 * glint));
    mistColor += bowWeight(viewDir, atmoSun.xyz) * atmoSun.w * 0.6;
    color = mix(color, mistColor, mist * 0.25);
  }
  return color;
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
vec3 terrainBaseColor(vec3 unitNormal, float elevation, vec3 surface, vec3 worldPosition) {
  TerrainMaterial m = terrainMaterial(unitNormal, elevation, surface);
  vec3 albedo = terrainCompose(m, m.ground, m.canopy, m.stone, kSnow, kIce, m.trees, floorBaseColor(m.floor));
  float diffuse = max(dot(unitNormal, atmosphereLightDirection(sunLight.xyz)), 0.0);
  return atmosphereLit(albedo, unitNormal, diffuse, sunLight.xyz, sunLight.w, vec3(1.0), worldPosition);
}

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
  if (strength <= 0.0) return terrainBaseColor(unitNormal, elevation, surface, worldPosition);
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
  float diffuse = max(dot(n, atmosphereLightDirection(sunLight.xyz)), 0.0);
  // Snow and ice scatter sky light: their shadowed side turns slightly blue.
  vec3 ambientTint = mix(vec3(1.0), vec3(0.74, 0.82, 0.98), snowy * strength);
  return atmosphereLit(albedo, n, diffuse, sunLight.xyz, sunLight.w, ambientTint, worldPosition);
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
)GLSL") + kNoiseGlsl + kAtmosphereGlsl + kRunoffGlsl + kTerrainStyleGlsl + kTerrainDetailGlsl + R"GLSL(
void main() {
  atmosphereJitter = atmosphereDither(gl_FragCoord.xy);
  float elevation = baseElevation;
  vec3 n = normalize(surfaceNormal);
  vec3 base = terrainDetailedColor(n, worldPosition, elevation, surfaceAttributes);
  base = applyTerrainContours(base, elevation);
  float submerged = terrainMaterial(n, elevation, surfaceAttributes).submerged;
  base = applySnowCover(base, worldPosition, n, elevation);
  base = applyRunoff(base, worldPosition, n, elevation, submerged);
  base = applyPuddles(base, worldPosition, n, submerged);
  if (fireState > 0.75) {
    base = vec3(1.0, 0.18, 0.015);  // active fire
  } else if (fireState > 0.25) {
    base = vec3(0.13, 0.115, 0.12); // burned terrain
  }
  fragColor = vec4(applyAtmosphere(base, worldPosition), 1.0);
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
)GLSL") + kNoiseGlsl + kAtmosphereGlsl + kRunoffGlsl + kTerrainStyleGlsl + kTerrainDetailGlsl + R"GLSL(
void main() {
  atmosphereJitter = atmosphereDither(gl_FragCoord.xy);
  float elevation = baseElevation;
  vec3 n = normalize(surfaceNormal);
  vec3 ground = terrainDetailedColor(n, worldPosition, elevation, surfaceAttributes);
  ground = applyTerrainContours(ground, elevation);
  float submerged = terrainMaterial(n, elevation, surfaceAttributes).submerged;
  ground = applySnowCover(ground, worldPosition, n, elevation);
  ground = applyRunoff(ground, worldPosition, n, elevation, submerged);
  ground = applyPuddles(ground, worldPosition, n, submerged);

  if(layerFlags.y<0.5) {
    if(layerFlags.x<0.5)discard;
    fragColor=vec4(applyAtmosphere(ground, worldPosition),1.0);return;
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
  fragColor = vec4(applyAtmosphere(ground, worldPosition), 1.0);
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
)GLSL") + kNoiseGlsl + kAtmosphereGlsl + kWaterStyleGlsl + R"GLSL(
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
// Normal perturbation of expanding impact rings (see main): x and z slopes.
vec3 rainRings(vec2 p, float t, float rate, float dropMm) {
  vec3 bump = vec3(0.0);
  for (int layer = 0; layer < 2; ++layer) {
    float cell = layer == 0 ? 0.35 : 0.8;
    vec2 g = floor(p / cell);
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
      vec2 c = g + vec2(dx, dy);
      float h1 = terrainHash(c + vec2(3.7 * float(layer), 1.1)), h2 = terrainHash(c + vec2(9.2, 5.3 + float(layer)));
      if (h1 > rate * 0.9 + 0.1) continue;                   // fewer active impact points in light rain
      vec2 center = (c + vec2(h2, terrainHash(c + vec2(2.4, 8.8)))) * cell;
      float period = 0.4 + 0.6 * h1;
      float age = fract(t / period + h2) * period;
      float radius = age * 0.35;
      float dist = length(p - center);
      float wavelength = 0.04 + 0.012 * dropMm;
      float envelope = exp(-age * 3.5) * (1.0 - smoothstep(0.0, 0.04 + 0.025 * dropMm, abs(dist - radius)));
      float slope = cos((dist - radius) * 6.2831853 / wavelength) * envelope * (0.08 + 0.04 * dropMm);
      vec2 radial = dist > 1e-4 ? (p - center) / dist : vec2(0.0);
      bump += vec3(-radial.x, 0.0, -radial.y) * slope;
    }
  }
  return bump;
}
void main() {
  atmosphereJitter = atmosphereDither(gl_FragCoord.xy);
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
  float rain = atmosphereEnabled() && atmoWeather.y < 0.5 ? atmoWeather.x : 0.0;
  if (rain > 0.0) {
    // Raindrops hit the surface and send out rings: a jittered grid of impact points (two
    // scales), each repeating with its own period; a ring spreads at 0.5 m/s and its
    // capillary crests decay within a second. Bigger drops make bigger rings.
    vec3 rings = rainRings(p, atmoWind.w, rain, atmoRain.x) * terrainDetailFade(pixelMeters, 6.0);
    n = normalize(n + rings);
  }

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
  // With the atmosphere active the reflected sky, the light and the haze come
  // from the weather; otherwise the style's fixed sky and sun.
  vec3 view = normalize(eye.xyz - surfacePosition);
  float cosTheta = max(dot(n, view), 0.0);
  float fresnel = 0.02 + 0.98 * pow(1.0 - cosTheta, 5.0);
  vec3 body = mix(shallow.rgb, deep.rgb, 1.0 - exp(-depth / shallow.w));
  vec3 r = reflect(-view, n);
  vec3 lightDir = atmosphereLightDirection(sun.xyz);
  float diffuse = max(dot(n, lightDir), 0.0);
  vec3 skyColor, lit, specularColor, foamLight;
  if (atmosphereEnabled()) {
    vec3 reflected = atmosphereSkyColor(r);
    vec4 clouds = cloudAlong(surfacePosition, r, 1e9);
    skyColor = reflected * clouds.a + clouds.rgb;
    float shadow = cloudShadow(surfacePosition);
    vec3 ambient = atmoAmbient.rgb * atmoSunColor.w, direct = atmoSunColor.rgb * atmoSun.w * shadow;
    lit = ambient * 2.4 + direct * (0.3 + 0.7 * diffuse) + vec3(0.55, 0.62, 0.80) * (0.6 * atmoHorizon.w);
    specularColor = direct * (1.0 / 0.72);
    foamLight = ambient * 2.4 + direct * (0.4 + 0.6 * diffuse);
  } else {
    skyColor = mix(horizon.rgb, zenith.rgb, clamp(r.y, 0.0, 1.0));
    lit = vec3(0.75 + 0.25 * diffuse);
    specularColor = vec3(1.0);
    foamLight = vec3(0.75 + 0.25 * diffuse);
  }
  // Sun glitter: a Beckmann microfacet lobe whose roughness grows with the waves, the chop
  // and the rain, so a calm lake shows a tight sun image and a rain-dimpled or rough one a
  // broad, softer glare (the energy spreads: peak height falls as the lobe widens).
  float specular;
  if (atmosphereEnabled()) {
    float m = 0.03 + 0.07 * clamp(height / 5.0, 0.0, 1.0) + 0.05 * chop + 0.12 * rain;
    vec3 hv = normalize(view + lightDir);
    float cosA = max(dot(n, hv), 1e-3);
    float tan2 = (1.0 - cosA * cosA) / (cosA * cosA);
    specular = exp(-tan2 / (m * m)) / max(cosA * cosA * cosA * cosA, 1e-3) * (0.0025 / (m * m)) * sun.w;
    // Rain scatters the reflected sky into a dull sheen.
    skyColor = mix(skyColor, mix(atmoHorizon.rgb, atmoZenith.rgb, 0.5), 0.4 * rain);
  } else {
    specular = pow(max(dot(r, lightDir), 0.0), horizon.w) * sun.w;
  }
  float reflection = fresnel * zenith.w;
  vec3 color = mix(body * lit, skyColor, reflection) + specularColor * specular;
  float alpha = max(1.0 - exp(-depth / deep.w), reflection);
  vec3 foamColor = vec3(0.93, 0.95, 0.96) * foamLight;
  color = mix(color, foamColor, foam);
  alpha = mix(alpha, 1.0, foam);
  fragColor = vec4(applyAtmosphere(color, surfacePosition), alpha);
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
out vec3 worldPosition;
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
  // Sway grows faster than the wind (drag goes with its square) and a gale adds a fast flutter.
  float strength = 0.02 + 0.012 * wind.z + 0.0007 * wind.z * wind.z;
  float flutter = smoothstep(8.0, 20.0, wind.z) * 0.35 * sin(wind.w * 6.5 + plant.motion.y * 3.1 + FLEX * 4.0);
  float sway = params.x * strength * scale * FLEX * FLEX * (gust + 0.35 * sin(wind.w * 2.3 + plant.motion.y * 1.7) + flutter);
  vec3 offset = vec3(wind.x, 0.0, wind.y) * sway + vec3(-wind.y, 0.0, wind.x) * sway * 0.3;
  offset += vec3(wind.x, 0.0, wind.y) * plant.motion.z * 0.08 * scale * FLEX;  // permanent lean on steep ground
  // Storm bending: the whole crown leans downwind, up to a quarter of the height in a gale.
  offset += vec3(wind.x, 0.0, wind.y) * (0.012 * max(wind.z - 6.0, 0.0)) * scale * FLEX * FLEX;
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
  worldPosition = world;
  gl_Position = viewProjection * vec4(world, 1.0);
}
)GLSL";

const std::string kVegetationFragmentShader = std::string(R"GLSL(#version 430 core
in vec3 worldNormal;
in vec3 worldPosition;
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
)GLSL") + kNoiseGlsl + kAtmosphereGlsl + R"GLSL(
void main() {
  atmosphereJitter = atmosphereDither(gl_FragCoord.xy);
  vec3 n = normalize(worldNormal);
  // Two-sided, wrapped lighting: thin blades and cone skins never go black.
  float diffuse = 0.35 + 0.65 * abs(dot(n, atmosphereLightDirection(light.xyz)));
  float occlusion = 0.72 + 0.28 * plantFlex;              // darker toward the base
  vec3 color = atmosphereLit(plantColor, n, diffuse, light.xyz, light.w, vec3(1.0), worldPosition) * occlusion;
  if (plantBurn > 1.5) color = max(color, plantColor * 0.8); // embers are self-lit
  fragColor = vec4(applyAtmosphere(color, worldPosition), 1.0);
}
)GLSL";


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
out vec3 worldPosition;
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
    // Flames flicker: the tip sways and the height pulses; the fire weather (params.w)
    // lifts them in hot dry air and beats them down in rain.
    float flicker = 0.85 + 0.15 * sin(time * 7.0 + item.motion.x * 3.0) * (0.5 + 0.5 * sin(time * 11.0 + seed));
    float sway = sin(time * 9.0 + item.motion.x) * 0.18 * height * TEXCOORD.y * TEXCOORD.y;
    offset += right * sway + vec3(wind.x, 0.0, wind.y) * (0.03 * wind.z * height * TEXCOORD.y);
    height *= flicker * params.w;
  } else {
    // Smoke: a subdued fire smoulders lower; wet fuel steams more (see the fragment stage).
    height *= 0.6 + 0.4 * params.w;
    // Smoke leans downwind as it rises.
    offset += vec3(wind.x, 0.0, wind.y) * (0.02 + 0.06 * wind.z) * height * TEXCOORD.y * TEXCOORD.y;
  }
  vec3 world = base + offset + vec3(0.0, POSITION.y * height, 0.0);
  worldPosition = world;
  gl_Position = viewProjection * vec4(world, 1.0);
}
)GLSL");

const std::string kFireFragmentShaderSource = std::string(R"GLSL(#version 430 core
in vec2 uv;
in float intensity;
in float kind;
in float seed;
in vec3 worldPosition;
layout(std140, binding = 14) uniform FireStyle {
  vec4 wind;
  vec4 eye;
  vec4 params;
  vec4 light;
};
out vec4 fragColor;
)GLSL") + kNoiseGlsl + kAtmosphereGlsl + R"GLSL(
float fbm(vec2 p) {
  return 0.55 * terrainNoise(p).x + 0.30 * terrainNoise(p * 2.1 + vec2(3.1, 7.7)).x + 0.15 * terrainNoise(p * 4.3 + vec2(9.2, 1.3)).x;
}
void main() {
  atmosphereJitter = atmosphereDither(gl_FragCoord.xy);
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
    fragColor = vec4(applyAtmosphere(color, worldPosition), alpha);
  } else {
    // Smoke plume: widening column of drifting grey, thinning with height.
    float halfWidth = 0.5 * (0.2 + 0.8 * uv.y);
    float d = abs(uv.x - 0.5) / max(halfWidth, 1e-3);
    float column = 1.0 - smoothstep(0.35, 1.0, d);
    float n = fbm(vec2(uv.x * 2.5 + seed, uv.y * 3.0 - time * 0.18 - seed * 0.3));
    float density = smoothstep(0.28, 0.62, n) * column * (1.0 - uv.y) * smoothstep(0.0, 0.08, uv.y);
    float scale = params.x * (kind > 1.5 ? 0.35 : 1.0) * intensity;
    // Rain on a fire makes steam: denser, whiter plumes.
    float steam = atmosphereEnabled() ? atmoWeather.x : 0.0;
    float alpha = density * 0.55 * scale * (1.0 + 0.7 * steam);
    if (alpha < 0.015) discard;
    vec3 lit = vec3(0.55 + 0.45 * max(light.y, 0.0) * (0.5 + 0.5 * n));
    if (atmosphereEnabled())
      lit = atmoAmbient.rgb * atmoSunColor.w * 2.0 + atmoSunColor.rgb * atmoSun.w * (0.8 * (0.5 + 0.5 * n)) +
            vec3(0.55, 0.62, 0.80) * (0.6 * atmoHorizon.w);
    vec3 smoke = mix(vec3(0.36, 0.34, 0.33), vec3(0.72, 0.72, 0.74), steam) * lit;
    if (kind < 1.5) smoke = mix(vec3(0.55, 0.28, 0.12), smoke, smoothstep(0.0, 0.2, uv.y)); // lit from the fire below
    fragColor = vec4(applyAtmosphere(smoke, worldPosition), alpha);
  }
}
)GLSL";


// Sky dome: one fullscreen triangle pinned to the far depth, so it only fills
// pixels no geometry covered. The fragment stage reconstructs the view ray
// from the camera basis, shades the clear sky (gradient, sun glow and disc,
// stars, moon, lightning) and ray-marches the cloud layer over it.
constexpr char kSkyVertexShader[] = R"GLSL(#version 430 core
layout(location = 0) in vec2 POSITION;
out vec2 ndc;
void main() { ndc = POSITION; gl_Position = vec4(POSITION, 0.9999, 1.0); }
)GLSL";
const std::string kSkyFragmentShader = std::string(R"GLSL(#version 430 core
in vec2 ndc;
out vec4 fragColor;
)GLSL") + kNoiseGlsl + kAtmosphereGlsl + R"GLSL(
// Sparse procedural stars on an azimuth/altitude lattice, dimmed toward the horizon.
vec3 starField(vec3 dir) {
  if (dir.y <= 0.0) return vec3(0.0);
  vec2 uv = vec2(atan(dir.x, dir.z), asin(clamp(dir.y, -1.0, 1.0))) * 70.0;
  vec2 cell = floor(uv);
  float h = terrainHash(cell + vec2(31.0, 17.0));
  if (h < 0.965) return vec3(0.0);
  vec2 center = cell + 0.5 + (vec2(terrainHash(cell + vec2(3.0, 9.0)), terrainHash(cell + vec2(7.0, 1.0))) - 0.5) * 0.7;
  float d = length(uv - center);
  float brightness = (h - 0.965) / 0.035;
  float star = (1.0 - smoothstep(0.0, 0.2 + 0.22 * brightness, d)) * (0.55 + 0.6 * brightness);
  vec3 tint = mix(vec3(0.85, 0.9, 1.0), vec3(1.0, 0.95, 0.85), terrainHash(cell + vec2(5.0, 5.0)));
  return tint * star * smoothstep(0.0, 0.15, dir.y);
}
// Procedural lunar surface: maria as dark noise patches, craters on two scales as bowls with
// bright sun-facing rims. `uv` is the position on the apparent disc (the same face always
// faces the Earth), `sunOnDisc` the projected sun direction.
float moonSurface(vec2 uv, vec2 sunOnDisc, float lit, out float albedo) {
  float maria = 0.55 * terrainNoise(uv * 2.6 + vec2(3.1, 7.7)).x + 0.45 * terrainNoise(uv * 5.2 + vec2(1.3, 9.1)).x;
  albedo = mix(0.92, 0.52, smoothstep(0.48, 0.62, maria));
  float shade = lit;
  for (int layer = 0; layer < 2; ++layer) {
    float cells = layer == 0 ? 7.0 : 16.0;
    vec2 g = floor(uv * cells);
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
      vec2 c = g + vec2(dx, dy);
      float h = terrainHash(c + vec2(11.0 * float(layer), 5.0));
      if (h < 0.55) continue;
      vec2 center = (c + vec2(terrainHash(c + vec2(2.0, 3.0)), terrainHash(c + vec2(8.0, 6.0)))) / cells;
      float radius = (0.18 + 0.28 * terrainHash(c + vec2(4.0, 1.0))) / cells;
      vec2 toPoint = uv - center;
      float r = length(toPoint) / radius;
      if (r > 1.25) continue;
      vec2 radial = toPoint / max(length(toPoint), 1e-5);
      float facing = dot(radial, sunOnDisc);
      float bowl = 1.0 - smoothstep(0.55, 0.95, r);              // floor in shadow on the sun-facing wall
      float rim = smoothstep(0.8, 1.0, r) * (1.0 - smoothstep(1.0, 1.25, r));
      shade *= 1.0 - 0.35 * bowl * max(facing, 0.0) - 0.12 * bowl;
      shade += lit * rim * (0.25 * max(-facing, 0.0) - 0.12 * max(facing, 0.0));
      albedo *= 1.0 - 0.1 * bowl;
    }
  }
  return max(shade, 0.0);
}
vec3 moonDisc(vec3 dir, vec3 sky) {
  vec3 moonDir = atmoMoon.xyz;
  float cosDisc = dot(dir, moonDir);
  float radius = radians(atmoLife.z) * 0.5;
  float angle = acos(clamp(cosDisc, -1.0, 1.0));
  float visible = smoothstep(-0.5, 2.0, degrees(asin(clamp(moonDir.y, -1.0, 1.0))));
  if (visible <= 0.0) return vec3(0.0);
  // Halo around the lit moon at night.
  float halo = exp(-angle / (radius * 4.0)) * 0.12 * atmoMoon.w * (1.0 - atmoAmbient.w);
  if (angle > radius * 1.02) return vec3(0.8, 0.85, 1.0) * halo * visible;
  vec3 right = normalize(cross(moonDir, vec3(0.0, 1.0, 0.0)));
  vec3 up = cross(right, moonDir);
  vec3 onDisc = normalize(dir - moonDir * cosDisc);
  float rr = min(angle / radius, 1.0);
  vec2 uv = vec2(dot(onDisc, right), dot(onDisc, up)) * rr;
  vec3 normal = normalize(right * uv.x + up * uv.y - moonDir * sqrt(max(1.0 - dot(uv, uv), 0.0)));
  float lit = max(dot(normal, atmoSunDisc.xyz), 0.0);
  vec2 sunOnDisc = normalize(vec2(dot(atmoSunDisc.xyz, right), dot(atmoSunDisc.xyz, up)) + vec2(1e-5, 0.0));
  float albedo;
  float shade = moonSurface(uv, sunOnDisc, lit, albedo);
  float earthshine = 0.025 * (1.0 - atmoAmbient.w);
  vec3 lunar = vec3(0.96, 0.96, 0.92) * albedo * (shade * 1.05 + earthshine);
  float edge = 1.0 - smoothstep(0.97, 1.0, rr);
  // By day the pale disc blends with the bright sky; at night the dark side is black.
  return mix(vec3(0.0), max(lunar - sky, vec3(0.0)) + (sky * 0.9 - sky), edge) * visible;
}
void main() {
  atmosphereJitter = atmosphereDither(gl_FragCoord.xy);
  vec3 dir = normalize(camForward.xyz + camRight.xyz * (ndc.x * camForward.w) + camUp.xyz * (ndc.y * camRight.w));
  vec3 sky = atmosphereSkyColor(dir);
  // Stars, then the moon: a sphere shaded by the sun (so the phase and terminator follow the
  // ephemeris), with maria and craters on the face that always turns toward us, a faint
  // earthshine on its night side and a soft halo; the dark part occludes the stars.
  sky += starField(dir) * atmoWeather.w;
  sky += moonDisc(dir, sky);
  // A strike lights the sky around its channel; the flash of the return strokes is brief.
  if (atmoStrike.w > 0.0) {
    vec3 toStrike = normalize(atmoStrike.xyz - atmoEye.xyz);
    float angle = acos(clamp(dot(dir, toStrike), -1.0, 1.0));
    sky += vec3(0.75, 0.8, 1.0) * (atmoStrike.w * 0.7 * exp(-angle * angle / 0.035));
  }
  sky *= alexanderBand(dir);
  sky += rainbowAt(dir);
  vec4 clouds = cloudAlong(atmoEye.xyz, dir, 1.0e9);
  fragColor = vec4(sky * clouds.a + clouds.rgb, 1.0);
}
)GLSL";

// Precipitation: the vertex stage derives every streak from its particle
// index, the time and the wind, wrapped in a world-anchored box around the
// camera; rain falls fast and leans with the wind, snow drifts and sways.
// Streaks keep a minimum screen size so distant rain remains visible. Rain
// exists only below the local cloud base and thins in the rain shadow.
const std::string kPrecipitationVertexShader = std::string(R"GLSL(#version 430 core
layout(location = 0) in vec2 CORNER;
layout(location = 1) in float PARTICLE;
layout(std140, binding = 0) uniform Camera { mat4 viewProjection; };
out vec2 uv;
out float fade;
out float flakiness;
out vec3 worldPosition;
)GLSL") + kNoiseGlsl + kAtmosphereGlsl + R"GLSL(
void main() {
  // Phase from the altitude: snow above the freezing level, melting to rain through the
  // layer below it (sleet in between), judged at the camera's altitude since the box of
  // drops around it is far smaller than the melting layer.
  float snowiness = smoothstep(atmoSnow.x - atmoSnow.y, atmoSnow.x, atmoEye.y);
  vec3 box = mix(vec3(40.0, 26.0, 40.0), vec3(34.0, 22.0, 34.0), snowiness);
  vec3 h = vec3(terrainHash(vec2(PARTICLE, 1.3)), terrainHash(vec2(PARTICLE * 1.7, 7.1)), terrainHash(vec2(PARTICLE * 0.37, 3.9)));
  // Three forces: gravity against drag sets the terminal velocity of each drop from its
  // diameter (Atlas et al.: v = 9.65 - 10.3 exp(-0.6 D), D in mm; drizzle 0.4 mm falls at
  // 2 m/s, thunderstorm drops of 3 mm at 8 m/s); diameters scatter around the form's mean
  // (exponential, Marshall-Palmer-like); the wind carries small drops fully and big drops
  // with a lag. Snowflakes fall slowly, ride the wind and sway.
  float diameter = clamp(atmoRain.x * (0.45 + 1.1 * pow(-log(max(1.0 - h.x, 1e-3)), 0.7)), 0.15, 6.0);
  // Flakes fall at about a metre a second and ride the wind fully; a strong wind lofts them
  // and carries them off sideways. A melting flake speeds up toward the drop's velocity.
  float rainFall = max(9.65 - 10.3 * exp(-0.6 * diameter), 0.3);
  float loft = smoothstep(6.0, 15.0, atmoWind.z);
  float snowFall = (0.8 + 0.5 * h.x) * (1.0 - 0.5 * loft);
  float fall = mix(rainFall, snowFall, snowiness);
  float windFollow = mix(1.0 - 0.25 * smoothstep(1.0, 4.0, diameter), 1.0 + 0.3 * loft, snowiness);
  vec3 velocity = vec3(atmoWind.x, 0.0, atmoWind.y) * atmoWind.z * windFollow + vec3(0.0, -fall, 0.0);
  velocity.xz += vec2(sin(atmoWind.w * 1.3 + h.y * 6.28), cos(atmoWind.w * 1.1 + h.z * 6.28)) * (0.6 + 0.8 * loft) * snowiness;
  velocity.y += sin(atmoWind.w * 2.1 + h.z * 6.28) * 0.35 * loft * snowiness;
  vec3 path = h * box + velocity * atmoWind.w;
  vec3 rel = path - atmoEye.xyz + box * 0.5;
  rel -= box * floor(rel / box);
  vec3 p = atmoEye.xyz - box * 0.5 + rel;
  // Drops exist inside and below the (locally lifted) cloud, never above its top.
  vec4 oro = orographicAt(p.xz);
  float localTop = atmoCloud.y + oro.x * atmoFieldScale.z;
  float rainHere = clamp(oro.w * 2.0, 0.0, 1.5) * atmoWeather.x;
  float presence = (p.y < localTop && h.y < rainHere) ? 1.0 : 0.0;
  vec3 toEye = atmoEye.xyz - p;
  float dist = max(length(toEye), 0.1);
  vec3 along = normalize(velocity);
  vec3 right = normalize(cross(along, toEye / dist) + vec3(1e-4, 0.0, 0.0));
  // A streak is the distance a drop falls during an exposure of about 1/30 s; its width is
  // the drop (min one-and-a-half pixels so distant rain stays visible).
  float len = max(mix(0.033 * length(velocity), 0.10 + 0.08 * h.z, snowiness), dist * mix(0.009, 0.004, snowiness));
  float width = max(mix(0.0012 * diameter, 0.05 + 0.05 * h.z, snowiness), dist * mix(0.0016, 0.0009, snowiness));
  vec3 world = p + right * (CORNER.x * width) - along * ((CORNER.y - 0.5) * len);
  uv = vec2(CORNER.x + 0.5, CORNER.y);  // 0..1 across and along the quad
  flakiness = snowiness;
  fade = presence * (1.0 - smoothstep(box.x * 0.32, box.x * 0.5, length(toEye.xz))) * mix(0.55 + 0.18 * diameter, 1.0 - 0.6 * smoothstep(6.0, 20.0, dist), snowiness);
  worldPosition = world;
  gl_Position = presence > 0.0 ? viewProjection * vec4(world, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}
)GLSL";
const std::string kPrecipitationFragmentShader = std::string(R"GLSL(#version 430 core
in vec2 uv;
in float fade;
in float flakiness;
in vec3 worldPosition;
out vec4 fragColor;
)GLSL") + kNoiseGlsl + kAtmosphereGlsl + R"GLSL(
void main() {
  atmosphereJitter = atmosphereDither(gl_FragCoord.xy);
  float flake = 1.0 - smoothstep(0.3, 0.5, length(uv - 0.5));
  float streak = (1.0 - smoothstep(0.2, 0.5, abs(uv.x - 0.5))) * (1.0 - 0.6 * abs(uv.y - 0.5) * 2.0);
  float shape = mix(streak, flake, flakiness);
  float alpha = shape * fade * mix(0.42, 0.85, flakiness);
  if (alpha < 0.01) discard;
  vec3 light = atmoAmbient.rgb * atmoSunColor.w * 2.5 + atmoSunColor.rgb * atmoSun.w * 0.6 + vec3(0.55, 0.62, 0.80) * atmoHorizon.w;
  vec3 color = mix(vec3(0.75, 0.80, 0.90), vec3(0.95), flakiness) * light;
  fragColor = vec4(applyAtmosphere(color, worldPosition), alpha);
}
)GLSL";


// Lightning: emissive camera-facing strips with a white-blue core fading into a soft halo.
constexpr char kLightningVertexShader[] = R"GLSL(#version 430 core
layout(location = 0) in vec3 POSITION;
layout(location = 1) in float GLOW;
layout(std140, binding = 0) uniform Camera { mat4 viewProjection; };
out float glow;
void main() { glow = GLOW; gl_Position = viewProjection * vec4(POSITION, 1.0); }
)GLSL";
const std::string kLightningFragmentShader = std::string(R"GLSL(#version 430 core
in float glow;
out vec4 fragColor;
)GLSL") + kAtmosphereBlockGlsl + R"GLSL(
void main() {
  // glow: signed position across the strip in halo units (+-1 at the halo edge). A white-hot
  // core a few metres wide inside a blue-white halo falling off like a gaussian.
  float intensity = atmoStrike.w;
  float x = abs(glow);
  float core = exp(-x * x * 60.0);
  float halo = exp(-x * x * 2.2) * 0.45;
  vec3 color = mix(vec3(0.55, 0.65, 1.0), vec3(1.0), core) * intensity * (1.2 + 1.5 * core);
  float alpha = (core + halo) * smoothstep(0.0, 0.15, intensity);
  if (alpha < 0.01) discard;
  fragColor = vec4(clamp(color, 0.0, 1.0), clamp(alpha, 0.0, 1.0));
}
)GLSL";

// Waterfall spray: every drop is derived from its emitter (a lip, a ledge the fall strikes
// or a plunge pool), its index and the clock; it launches with the water and decays to its
// own terminal velocity in the wind. Lit drop by drop: the sun (or moon) scatters forward
// through it, the drops around it and the cliff shadow it, and those at the bow angle show
// a speck of the spectrum, so the plume carries a fragmented bow down the cascade.
const std::string kSprayVertexShader = std::string(R"GLSL(#version 430 core
layout(location = 0) in vec2 CORNER;
layout(location = 1) in float PARTICLE;
layout(location = 2) in vec3 ORIGIN;
layout(location = 3) in vec3 FLOW;
layout(location = 4) in vec3 FACE;
layout(location = 5) in vec4 SHAPE;
layout(std140, binding = 0) uniform Camera { mat4 viewProjection; };
out vec2 uv;
out vec3 worldPosition;
out vec4 dropColor;
out float softness;
)GLSL") + kNoiseGlsl + kAtmosphereGlsl + R"GLSL(
const float kPuffs = 14.0;  // the first particles of every emitter are mist puffs, the rest drops
void main() {
  float kind = SHAPE.x, strength = SHAPE.y, drop = SHAPE.z, width = SHAPE.w;
  float time = atmoWind.w;
  float seed = PARTICLE + fract(ORIGIN.x * 0.137 + ORIGIN.z * 0.071) * 977.0;
  vec4 h = vec4(terrainHash(vec2(seed, 1.3)), terrainHash(vec2(seed * 1.7, 7.1)),
                terrainHash(vec2(seed * 0.37, 3.9)), terrainHash(vec2(seed * 2.3, 5.7)));
  vec3 g = vec3(terrainHash(vec2(seed * 0.71, 9.2)), terrainHash(vec2(seed * 1.13, 2.6)), terrainHash(vec2(seed * 3.1, 4.4)));
  bool puff = PARTICLE < kPuffs;
  vec2 downXZ = FLOW.xy;
  float launch = FLOW.z;
  vec3 down = vec3(downXZ.x, 0.0, downXZ.y), side = vec3(-downXZ.y, 0.0, downXZ.x), up = vec3(0.0, 1.0, 0.0);
  // Water runs only below the freezing level, and only while the rain's runoff lasts.
  float liquid = 1.0 - smoothstep(atmoSnow.x - 150.0, atmoSnow.x + 50.0, ORIGIN.y);
  float running = atmoSnow.z * liquid;
  // The drops leave where the ropes of water pour (the same rope field the ground paints):
  // of three places across the texel, each drop takes the one most inside a rope.
  vec2 perpXZ = vec2(-downXZ.y, downXZ.x);
  float alongE = dot(ORIGIN.xz, downXZ), acrossE = dot(ORIGIN.xz, perpXZ);
  float heightE = smoothstep(0.0, 120.0, drop);
  // (The stream is strongest at the texel's centre, so a rope near the middle wins.)
  float across = 0.0, ribbon = -1.0;
  for (int c = 0; c < 3; ++c) {
    float u = fract(h.x + 0.37 * float(c));
    float candidate = (u - 0.5) * width;
    vec2 at = ORIGIN.xz + perpXZ * candidate;
    float deflect = (ridgedFbm(at * kCragFrequency).x - 0.5) * 3.0 + (ridgedFbm(at * 0.21 + vec2(2.0, 8.0)).x - 0.5) * 1.2;
    float r = waterfallRope(acrossE + candidate + deflect, alongE, ORIGIN.y, heightE, time) * (1.0 - 0.4 * abs(u - 0.5) * 2.0);
    if (r > ribbon) { ribbon = r; across = candidate; }
  }
  ribbon = smoothstep(0.35, 0.6, ribbon);
  float weight = mix(0.45, 1.0, ribbon) * sqrt(strength) * running;
  // Drop size: the lip sheds a fine drizzle, the ledges and the pool throw coarser splash.
  float fineShare = kind == 0.0 ? 0.75 : 0.45;
  float diameter = h.w < fineShare ? mix(0.06, 0.35, h.w / fineShare) : mix(0.35, 2.0, pow((h.w - fineShare) / (1.0 - fineShare), 1.5));
  if (puff) diameter = 0.16;  // a puff is the haze of the finest drops, seen together (a pale fog bow)
  // Terminal velocity from the diameter (Atlas et al.). A big drop flies ballistically and
  // relaxes to it in terminal / g; a fine one is carried by the air it left with, and that
  // parcel of air keeps its momentum for about a second before the fall's own wind
  // (dragged down with the water, thrown up and out at the pool) and the weather's take over.
  float terminal = max(9.65 - 10.3 * exp(-0.6 * diameter), 0.25);
  float fineness = 1.0 - smoothstep(0.3, 1.5, diameter);
  float tau = max(terminal / 9.81, (0.7 + 0.5 * h.y) * fineness);
  float life = mix(1.0, 2.8, h.y) * (kind == 2.0 ? 1.3 : 1.0) * mix(1.0, 0.6, smoothstep(0.8, 2.0, diameter)) * (puff ? 1.6 : 1.0);
  float age = fract(time / life + h.z) * life;
  // The rock is displaced by its crags on the GPU only (on rock, by slope): lift the emitter
  // with the same field, never sinking it more than a metre.
  float rockiness = smoothstep(28.0, 48.0, degrees(acos(clamp(FACE.y, 0.0, 1.0))));
  vec3 relief = up * (max(atmoLife.w * rockiness * (ridgedFbm(ORIGIN.xz * kCragFrequency).x - 0.5) * 2.0, -1.0) + 0.4);
  vec3 p0, v0;
  if (kind == 0.0) {
    // Lip: the water shoots over the edge; the drizzle lifts off the top of the sheet.
    p0 = ORIGIN + relief + side * across + FACE * (0.8 + 1.2 * g.x) + up * (0.3 + 0.6 * g.y);
    v0 = down * launch * (0.6 + 0.7 * g.x) + side * (g.z - 0.5) * launch * 0.5 + up * (0.3 + 2.0 * g.y);
  } else if (kind == 1.0) {
    // Contact: the fall strikes a ledge and splashes off the face, still heading down.
    p0 = ORIGIN + relief + side * across + FACE * (0.8 + 1.2 * g.x) + down * (g.y - 0.5) * width * 0.3;
    v0 = FACE * launch * (0.4 + 0.6 * g.x) + side * (g.z - 0.5) * launch * 0.7 + down * launch * 0.25 - up * launch * (0.1 + 0.3 * g.y);
  } else {
    // Plunge: the pool throws a plume up and outward, the biggest drops highest.
    p0 = ORIGIN + relief + side * across * 0.8 + down * (g.x - 0.5) * width * 0.5 + up * 0.2;
    v0 = up * launch * (0.45 + 0.75 * g.y) + down * launch * (0.15 + 0.45 * g.x) + side * (g.z - 0.5) * launch * 0.9;
  }
  float loft = smoothstep(6.0, 15.0, atmoWind.z);
  float windFollow = 1.0 - 0.6 * smoothstep(0.3, 2.0, diameter);
  vec3 carried = vec3(atmoWind.x, 0.0, atmoWind.y) * atmoWind.z * windFollow + vec3(0.0, -terminal, 0.0);
  // The finest drops ride the air the fall drags down, the plume the pool throws up and
  // the eddies around both.
  carried.y += (kind == 0.0 ? -0.6 : kind == 1.0 ? -0.8 : 0.4) * fineness * (0.5 + 0.5 * strength);
  carried.xz += vec2(sin(time * 1.7 + h.y * 6.28), cos(time * 1.3 + h.z * 6.28)) * 0.5 * fineness * (1.0 + loft);
  float relax = tau * (1.0 - exp(-age / tau));
  vec3 p = p0 + carried * age + (v0 - carried) * relax;
  // The falling sheet drags the air (and the drizzle in it) down with it, faster and faster
  // until the air's own drag caps it: fine drops off a lip or a ledge sink with the fall.
  float dragged = (kind == 0.0 ? 1.0 : kind == 1.0 ? 0.6 : 0.0) * fineness * (0.4 + 0.6 * strength);
  p.y -= dragged * (age < 1.333 ? 1.5 * age * age : 4.0 * age - 2.667);
  vec3 velocity = carried + (v0 - carried) * exp(-age / tau);
  // Drops that fall back into the pool or against the rock are gone.
  float sunk = kind == 2.0 ? smoothstep(0.0, 0.8, ORIGIN.y + relief.y - p.y) : kind == 1.0 ? smoothstep(0.0, 1.0, -dot(p - ORIGIN - relief, FACE) - 0.2) : 0.0;
  float fadeIn = smoothstep(0.0, 0.06 * life, age), fadeOut = 1.0 - smoothstep(0.6 * life, life, age);
  vec3 toEye = atmoEye.xyz - p;
  float dist = max(length(toEye), 0.1);
  vec3 viewDir = -toEye / dist;
  // Lighting. The plume around the emitter shadows the drops inside and behind it: the
  // deeper into the plume the light has to reach, the darker the drop (and the drops
  // cluster, so whole wisps go dark). The cliff shadows drops close to a face turned away
  // from the light; the farther out and the higher the light, the more escape it.
  vec3 sun = atmoSun.xyz;
  vec3 centre = ORIGIN + relief + (kind == 0.0 ? down * 1.5 + up * 0.8 : kind == 1.0 ? FACE * 1.5 : up * 2.0);
  vec3 o = p - centre;
  float radius = 2.5 + 2.5 * strength + (kind == 0.0 ? 0.0 : 0.04 * drop);
  float depth = clamp((radius - dot(o, sun)) / (2.0 * radius), 0.0, 1.0);
  float density = strength * running * (0.5 + 0.5 * ribbon);
  float amongDrops = exp(-2.4 * density * depth);
  float clump = terrainNoise(vec2(dot(p, vec3(0.7, 0.5, 0.5)), dot(p, vec3(-0.4, 0.8, 0.45))) * 0.9 + vec2(time * 0.3, -time * 0.2)).x;
  amongDrops *= mix(0.3, 1.0, smoothstep(0.3, 0.65, clump));
  float faceLit = smoothstep(-0.35, 0.15, dot(FACE, sun));
  float escape = smoothstep(0.5, 6.0, dot(o, FACE)) * smoothstep(0.05, 0.45, sun.y);
  float rock = mix(faceLit, 1.0, kind == 0.0 ? max(escape, 0.6) : escape);
  float lit = amongDrops * rock * atmoSun.w * cloudShadow(p);  // the clouds shade the spray like the ground
  // Forward scattering makes a drop between the eye and the light sparkle; at the bow
  // angle (40.6..42.4 degrees from the anti-light point, the secondary at 50..53.5) it
  // shows a speck of the spectrum: tiny drops give a broad pale fog bow, bigger ones a
  // saturated one, and each drop is offset a little, so the bow is fragmented.
  float cosSun = dot(viewDir, sun);
  float glint = 0.9 + 2.6 * pow(max(cosSun, 0.0), 5.0);  // white in any light, a sparkle toward the light
  float pale = smoothstep(0.08, 0.35, diameter);
  float widen = mix(2.2, 1.0, smoothstep(0.1, 0.5, diameter));
  float a = degrees(acos(clamp(-cosSun, -1.0, 1.0))) + (g.x - 0.5) * 0.9;
  float t1 = (a - 40.6) / (1.8 * widen), t2 = (53.5 - a) / (3.4 * widen);
  vec3 bow = vec3(0.0);
  if (t1 > 0.0 && t1 < 1.0) bow += mix(vec3(0.8), spectrum(t1), pale) * smoothstep(0.0, 0.2, t1) * (1.0 - smoothstep(0.8, 1.0, t1)) * 0.8;
  if (t2 > 0.0 && t2 < 1.0) bow += mix(vec3(0.6), spectrum(t2), pale) * smoothstep(0.0, 0.25, t2) * (1.0 - smoothstep(0.75, 1.0, t2)) * 0.25;
  bow *= smoothstep(0.3, 0.8, g.z);
  vec3 skyLight = atmoAmbient.rgb * atmoSunColor.w * 2.2 + vec3(0.55, 0.62, 0.80) * atmoHorizon.w;
  // In the bow the drop's own white gives way to its colour (added on top it would only clip to white).
  float bowAmount = min(max(max(bow.r, bow.g), bow.b) * 1.25, 1.0) * lit;
  vec3 color = vec3(0.86, 0.91, 1.0) * (skyLight + atmoSunColor.rgb * lit * glint) * (1.0 - 0.65 * bowAmount) + bow * atmoSunColor.rgb * lit * 2.4;
  // A streak as long as the drop moves in about 1/40 s, never thinner than a pixel or two;
  // a puff is a soft camera-facing disc a few metres wide that swells as it thins out.
  float speed = max(length(velocity), 0.1);
  vec3 along = velocity / speed;
  vec3 right = normalize(cross(along, toEye / dist) + vec3(1e-4, 0.0, 0.0));
  float thickness = max(0.01 + 0.025 * diameter, dist * 0.0028);
  float len = max(thickness, 0.025 * speed);
  float alpha = weight * fadeIn * fadeOut * (1.0 - sunk) * mix(0.6, 0.95, smoothstep(0.1, 1.0, diameter)) *
                (1.0 - smoothstep(900.0, 2500.0, dist)) * (0.5 + 0.5 * lit);
  if (puff) {
    float swell = (2.5 + 4.0 * g.y + (kind == 0.0 ? 0.0 : 0.05 * drop)) * (0.4 + 0.6 * age / life);
    p += FACE * (1.5 + swell * 0.5);  // the haze hangs off the rock, not half inside it
    thickness = swell; len = swell;
    along = normalize(cross(toEye / dist, vec3(1.0, 0.0, 0.0)) + vec3(0.0, 1e-3, 0.0));
    right = normalize(cross(along, toEye / dist));
    alpha = weight * fadeIn * pow(1.0 - age / life, 0.7) * (1.0 - sunk) * 0.5 * (1.0 - smoothstep(900.0, 2500.0, dist)) * (0.6 + 0.4 * lit);
    color = vec3(0.9, 0.94, 1.0) * (skyLight * 1.6 + atmoSunColor.rgb * lit * (0.9 + 1.6 * pow(max(cosSun, 0.0), 4.0))) * (1.0 - 0.4 * bowAmount) +
            bow * atmoSunColor.rgb * lit * 1.4;
  }
  vec3 world = p + right * (CORNER.x * thickness) + along * ((CORNER.y - 0.5) * len);
  uv = vec2(CORNER.x + 0.5, CORNER.y);  // 0..1 across and along the quad
  softness = puff ? 1.0 : 0.0;
  worldPosition = world;
  dropColor = vec4(color, alpha);
  gl_Position = alpha > 0.003 ? viewProjection * vec4(world, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}
)GLSL";
const std::string kSprayFragmentShader = std::string(R"GLSL(#version 430 core
in vec2 uv;
in vec3 worldPosition;
in vec4 dropColor;
in float softness;
out vec4 fragColor;
)GLSL") + kNoiseGlsl + kAtmosphereGlsl + R"GLSL(
void main() {
  atmosphereJitter = atmosphereDither(gl_FragCoord.xy);
  float r = length(uv - 0.5);
  float shape = mix(1.0 - smoothstep(0.28, 0.5, r), exp(-r * r * 14.0) * (1.0 - smoothstep(0.35, 0.5, r)), softness);
  float alpha = shape * dropColor.a;
  if (alpha < 0.01) discard;
  fragColor = vec4(applyAtmosphere(dropColor.rgb, worldPosition), alpha);
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
    if(name=="Atmosphere" && values[4]!=static_cast<GLint>(kAtmosphereBytes))
      throw std::runtime_error("Atmosphere UBO must contain twenty-five vec4s");
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
    // The shared Atmosphere block is compiled into whichever stages the
    // program's contract declares; Mesa's reference flags are unreliable for it.
    if(name=="Atmosphere") {
      const auto declared=std::find_if(expected.bindings.begin(),expected.bindings.end(),
          [](const ShaderBinding& b){return b.name=="Atmosphere";});
      if(declared!=expected.bindings.end())stages=declared->stages;
    }
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
  const bool wide=desc.format==TextureFormat::Rgba16;
  impl_->gl.TexImage2D(GL_TEXTURE_2D,0,wide?GL_RGBA16:GL_RGBA8,
                        static_cast<GLsizei>(desc.width),static_cast<GLsizei>(desc.height),
                        0,GL_RGBA,wide?GL_UNSIGNED_SHORT:GL_UNSIGNED_BYTE,rgba8.data());
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
    case ShaderProgramId::Sky:vertex_source=kSkyVertexShader;fragment_source=kSkyFragmentShader.c_str();break;
    case ShaderProgramId::Precipitation:vertex_source=kPrecipitationVertexShader.c_str();fragment_source=kPrecipitationFragmentShader.c_str();break;
    case ShaderProgramId::Lightning:vertex_source=kLightningVertexShader;fragment_source=kLightningFragmentShader.c_str();break;
    case ShaderProgramId::Spray:vertex_source=kSprayVertexShader.c_str();fragment_source=kSprayFragmentShader.c_str();break;
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
