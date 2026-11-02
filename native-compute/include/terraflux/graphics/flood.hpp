#pragma once

#include <terraflux/graphics/weather.hpp>
#include <terraflux/math.hpp>
#include <terraflux/terrain_mesh.hpp>
#include <terraflux/terrain_water.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace terraflux::graphics {

// Flash floods. Rain that lands on the slopes runs down the drainage the
// runoff field already knows (flow accumulation = the catchment above each
// texel, the drainage direction, the slope); the heavier and the shorter the
// rain, the more of it arrives in the channels at once. This model turns that
// into water with a depth and a current at every point: a channel's depth is
// `channel_depth x flow^exponent x flash`, where `flash` is the flood wave
// (0 .. 1) that rises toward rain / storm_rain with `rise_seconds` and drains
// with `fall_seconds` on the weather's accelerated clock — a thunderstorm's
// 25 mm/h fills the channels within a quarter of an hour, a drizzle never
// does. The current follows the drainage at Manning's speed
// `depth^(2/3) sqrt(slope) / n`, capped. Standing water (the lake with its
// flood rise) is the other source; the deeper of the two counts.
// The walker samples this to wade, swim or be swept; the viewer draws it as a
// water surface lying on the channels.
struct FloodStyle {
  double storm_rain_mm_h = 25.0;      // 1 .. 500: rain that fills the channels completely
  double rise_seconds = 900.0;        // 1 .. 86400 (accelerated): the flood wave's rise
  double fall_seconds = 3600.0;       // 1 .. 86400: its draining
  double channel_depth_meters = 1.5;  // 0 .. 20: a full channel at the largest catchment
  double flow_exponent = 2.0;         // 0.5 .. 6: depth against the (log-scaled) catchment
  double flow_threshold = 0.25;       // 0 .. 1: below this catchment there is no channel
  double manning_n = 0.04;            // 0.01 .. 0.2 roughness
  double max_current_mps = 8.0;       // 0 .. 30
  double min_depth_meters = 0.03;     // 0 .. 1: shallower water is ignored (and not drawn)
};
void validate_flood_style(const FloodStyle& style);  // throws std::invalid_argument

struct FloodSample {
  double depth_meters = 0.0;          // above the ground at the point (0: dry)
  double surface_meters = 0.0;        // absolute height of the water surface (ground when dry)
  double current_mps = 0.0;           // speed of the running water
  math::Vec2 current;                 // unit direction of the current (zero when still)
  bool channel = false;               // the depth comes from running water (not the lake)
  bool waterfall = false;             // the water here goes over a drop
};

class FloodModel {
 public:
  FloodModel(const RunoffField& runoff, const FloodStyle& style);
  const FloodStyle& style() const { return style_; }
  double flash() const { return flash_; }
  void set_flash(double flash);  // 0 .. 1, for tests and seeks
  // Advances the flood wave by accelerated seconds under a rain rate.
  void advance(double accelerated_seconds, double rain_mm_h);
  // The running water's depth at a plan position (bilinear over the field).
  double channel_depth(double x, double z) const;
  // How much of a waterfall the drainage makes here (0 .. 1, bilinear), regardless of the wave.
  double waterfall_at(double x, double z) const;
  // Water at a point whose ground lies at ground_y: the channel water, or the
  // lake when that is deeper. Dry ground gives depth 0 with the ground as surface.
  FloodSample sample(double x, double z, double ground_y, std::optional<double> lake_level) const;
  // A water surface lying on the channels: a vertex per terrain vertex at
  // ground + depth (ground where dry: such corners are discarded by the water
  // shader), triangles where any corner is wet; vertices under the lake are
  // left to the lake. Empty when the flood wave is at zero.
  terrain::WaterMesh build_mesh(const terrain::TerrainMesh& terrain, std::optional<double> lake_level) const;
  struct Stats { std::size_t wet_vertices = 0; double max_depth_meters = 0.0, max_current_mps = 0.0; };
  Stats stats(const terrain::TerrainMesh& terrain, std::optional<double> lake_level) const;

 private:
  RunoffTexel interpolate(double x, double z) const;
  RunoffField field_;
  FloodStyle style_;
  double flash_ = 0.0;
};

}  // namespace terraflux::graphics
