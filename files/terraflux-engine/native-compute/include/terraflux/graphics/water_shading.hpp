#pragma once

#include <terraflux/graphics/render_device.hpp>
#include <terraflux/math.hpp>
#include <terraflux/terrain_water.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terraflux::graphics {

// Backend-neutral description of the animated water surface. Depth (level
// minus bed) drives color absorption, transparency and foam; waves are a
// primary directional wave plus secondary cross waves and ripples, shoaling,
// shortening and bending toward the shore as the water gets shallow, and
// breaking into foam where the depth can no longer carry their height. The
// surface reflects a procedural sky and the sun by Fresnel weight; there is no
// planar reflection of the scene. Units: meters, seconds, compass degrees.
struct WaterStyle {
  double level_meters = 0.0;
  double wave_height_meters = 0.6;      // amplitude of the primary wave, 0 (flat) .. 5
  double wave_length_meters = 24.0;     // 1 .. 1000
  double wave_speed_mps = 3.0;          // deep-water phase speed, 0 (frozen) .. 50
  double wave_direction_degrees = 90.0; // compass direction the waves travel toward (0 north, 90 east)
  double choppiness = 0.5;              // 0 .. 1: secondary waves and ripples
  double foam_depth_meters = 2.0;       // shoreline foam band, 0 .. 50
  math::Vec3 shallow_color{0.22, 0.52, 0.50}, deep_color{0.02, 0.11, 0.26};
  double absorption_meters = 8.0;       // depth at which 63 % of the deep color is reached
  double clarity_meters = 4.0;          // depth at which the surface is 63 % opaque (0.5 .. 50)
  math::Vec3 horizon_color{0.70, 0.80, 0.92}, zenith_color{0.30, 0.48, 0.80};
  double reflectivity = 1.0;            // scales the Fresnel sky reflection, 0 .. 1
  math::Vec3 light_direction{-0.4545, 0.7660, 0.4545};
  double specular_power = 160.0, specular_intensity = 0.9;
};

// Uniform block "WaterStyle" at this slot, std140, exactly eight vec4s:
//   surface: level, wave height, wave length, wave speed
//   travel:  unit travel direction (world x, world z), time seconds, foam depth
//   shallow: rgb, absorption depth
//   deep:    rgb, clarity depth
//   eye:     camera position xyz, choppiness
//   horizon: rgb, specular power
//   zenith:  rgb, reflectivity
//   sun:     unit direction toward the light, specular intensity
inline constexpr std::uint32_t kWaterStyleSlot = 12;
inline constexpr std::size_t kWaterStyleBytes = 128;
using WaterStyleUniform = std::array<float, 32>;
static_assert(sizeof(WaterStyleUniform) == kWaterStyleBytes);

// Rejects non-finite values and values outside the documented ranges, a
// non-positive absorption/clarity depth, colors outside [0, 1], a zero light
// vector and a non-finite eye or negative time.
WaterStyleUniform pack_water_style(const WaterStyle& style, math::Vec3 eye, double time_seconds);

// Compass degrees (clockwise from north) to a unit direction in the world xz
// plane: 0 -> (0, -1) north, 90 -> (1, 0) east.
math::Vec2 wave_travel_vector(double compass_degrees);

// Packed vertex: position, bed height, bed gradient (24 bytes).
struct WaterGpuVertex {
  float position[3];
  float bed_height;
  float bed_gradient[2];
};
struct WaterUpload {
  std::vector<WaterGpuVertex> vertices;
  std::vector<std::uint32_t> indices;
};
// POSITION (0), BED (1), BED_GRADIENT (2); Camera at slot 0 (vertex) and
// WaterStyle at slot 12 (vertex and fragment stages).
ShaderLayout water_shader_layout();
WaterUpload prepare_water_upload(const terrain::WaterMesh& mesh);

// CPU references mirroring the GLSL for the parts that need no screen-space
// derivatives or animation:
//  - shallow-water phase-speed ratio c / c_deep = sqrt(tanh(k d)), clamped to [0.3, 1]
double wave_shallowness(double depth_meters, double wave_length_meters);
//  - travel direction refracted toward rising ground as the water shallows
math::Vec2 wave_travel_direction(math::Vec2 travel, math::Vec2 bed_gradient, double depth_meters,
                                 double wave_length_meters);
//  - depth-limited local amplitude and whether the wave is breaking (0 .. 1)
struct WaveShoaling { double amplitude_meters = 0.0, breaking = 0.0, wave_length_meters = 0.0; };
WaveShoaling wave_shoaling(double depth_meters, const WaterStyle& style);
//  - flat-water surface color and opacity (wave height zero, no foam) for a
//    point on the surface seen from `eye`: what the GPU blends over the bed.
struct WaterSample { math::Vec3 color; double alpha = 0.0; };
WaterSample water_surface_sample(math::Vec3 surface_position, double depth_meters, math::Vec3 eye,
                                 const WaterStyle& style);

}  // namespace terraflux::graphics
