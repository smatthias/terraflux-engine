#pragma once

#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/collision.hpp>
#include <terraflux/graphics/fire_effects.hpp>
#include <terraflux/graphics/flood.hpp>
#include <terraflux/graphics/mesh_parts.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/math.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace terraflux::graphics {

// Roads and bridges: fixed infrastructure laid over the terrain that a walker
// can use. A network is a graph of plan nodes joined by segments; a segment
// is a *road* (a carriageway draped over the ground: two lanes, one each
// way, with a dashed centre line for a street; four for an avenue; six with
// a median for a highway; ramps, and an airport's runway, taxiway and apron)
// or a *bridge* (a straight deck between its two ends at the surface height
// of each — the ground, or the ground plus the node's lift for a viaduct —
// spanning whatever lies between on railings and piers). Curves are
// polylines of short segments; where exactly two segments meet the ribbon
// is mitred so a curve reads as one road. Every network is validated the
// way real road nets are built: every bridge lies on a chain of bridges
// that reaches roads at both of its ends (there is no stand-alone bridge)
// and everything is connected.
//
// The walker meets the network through the walk environment: a road surface
// or a sound deck is a platform the feet rest on (a deck is reached however
// steep the ground under it and fallen from at its edge), the railings and
// piers are obstacles, and the surface sets a factor on the walking speed —
// faster on a sound road or deck than on open ground, no bonus near a fallen
// tree that blocks the way or on a damaged surface, slower over the rubble
// of a destroyed one.
//
// The infrastructure is not indestructible. A trunk that the fire (or the
// wind, or an axe) brings down across a road blocks it where it lies and
// cracks the surface by its mass as it lands (asphalt and concrete do not
// burn, but they break); while it burns it damages the surface, and a deck
// it burns on is alight and collapses when the damage is complete. A flash
// flood running at a pier washes a bridge out; running over a road's
// surface for long enough it washes the road away. A walker on a deck that
// gives way is hurt by the collapse and then falls with it. Nothing here
// feeds back into the fire simulation, the contracts or the fixtures.
enum class RoadKind : std::uint8_t { Road, Bridge };
const char* road_kind_name(RoadKind kind);
// What a segment carries: lane counts and widths come from the class.
enum class RoadClass : std::uint8_t { Street, Avenue, Highway, Ramp, Runway, Taxiway, Apron };
const char* road_class_name(RoadClass cls);
std::optional<RoadClass> road_class_from_name(const std::string& name);
// Lanes of a class (0 for the airport pavements, which have no lanes).
int road_class_lanes(RoadClass cls);

struct RoadStyle {
  // Carriageway: lanes of this width with shoulders; a highway adds a median.
  double lane_width_meters = 3.0;        // 1.5 .. 10
  double shoulder_meters = 0.5;          // 0 .. 5 either side
  double highway_median_meters = 2.0;    // 0 .. 10 between the carriageways of a highway
  double runway_width_meters = 45.0;     // 10 .. 100
  double taxiway_width_meters = 20.0;    // 5 .. 60
  double apron_width_meters = 60.0;      // 10 .. 200
  double sample_spacing_meters = 2.5;    // 0.5 .. 50: the draped surface follows the ground at this spacing
  double surface_lift_meters = 0.12;     // 0 .. 1: the surface lies this far above the ground
  double marking_width_meters = 0.15;    // 0.02 .. 1
  double marking_dash_meters = 3.0;      // 0.5 .. 50 of dash
  double marking_gap_meters = 6.0;       // 0 .. 100 between dashes
  // Bridges: a deck on railings and piers.
  double deck_thickness_meters = 0.6;    // 0.1 .. 5
  double railing_height_meters = 1.1;    // 0 .. 3 (0: none)
  double railing_post_spacing_meters = 4.0; // 0.5 .. 50
  double pier_spacing_meters = 20.0;     // 2 .. 200
  double pier_width_meters = 1.2;        // 0.2 .. 10
  double stub_meters = 6.0;              // 0 .. 100 of deck left standing at each end of a destroyed bridge
  // Walking.
  double road_speed_factor = 1.3;        // 1 .. 3 on a sound road
  double bridge_speed_factor = 1.25;     // 1 .. 3 on a sound deck
  double rubble_speed_factor = 0.7;      // 0.1 .. 1 on a destroyed road
  double blockage_reach_meters = 6.0;    // 0 .. 100: this close (along the road) to a trunk lying across it the bonus is gone
  // Damage and destruction.
  double road_fire_damage_per_second = 0.02;    // 0 .. 1 per burning trunk at full intensity lying on a road
  double bridge_fire_damage_per_second = 0.05;  // 0 .. 1 per burning trunk on a deck (alight; at 1 it collapses)
  double washout_current_mps = 3.0;      // 0.1 .. 30 at a pier, or over a road's surface
  double washout_depth_meters = 1.0;     // 0 .. 20 at a pier, or over a road's surface
  double washout_seconds = 45.0;         // 1 .. 86400 of such water before the bridge goes (a road takes twice as long)
  double impact_tonnes = 12.0;           // 0.1 .. 1000: a trunk of this mass landing on a road surface ruins it outright (a deck takes 2/3 of it)
  double collapse_damage = 0.35;         // 0 .. 1 of health taken by a deck giving way under the walker (the fall is extra)
  std::uint32_t seed = 0;
};
void validate_road_style(const RoadStyle& style);  // throws std::invalid_argument
// The full width of a street: two lanes and two shoulders.
double road_width(const RoadStyle& style);
// The full width of a class: its lanes and shoulders (and median), or the airport pavement width.
double road_class_width(const RoadStyle& style, RoadClass cls);

// The graph. Nodes are plan positions (metres, x east, z south as the
// terrain's z) with a lift over the ground for viaducts (a lifted node is
// reached by bridges only); a segment joins two of them.
struct RoadNode {
  math::Vec2 position;
  double lift_meters = 0.0;   // 0 .. 60 over the ground: the deck of a viaduct passes this high here
};
struct RoadSegment {
  std::uint32_t a = 0, b = 0;
  RoadKind kind = RoadKind::Road;
  RoadClass cls = RoadClass::Street;
};
struct RoadNetworkDef {
  std::vector<RoadNode> nodes;
  std::vector<RoadSegment> segments;
};
// Rejects node indices out of range, a segment joining a node to itself, a
// duplicate segment, a zero-length segment, a lifted node that a road
// reaches (only bridges run at height), a bridge (or a chain of bridges)
// that does not reach roads at two of its nodes (a stand-alone bridge), and
// a network that is not connected (every node reachable from every other).
// An empty network is valid.
void validate_road_network(const RoadNetworkDef& def);

// Where a route calls for a bridge. A route (a plan polyline) is walked at
// the sample spacing: where the ground along it rises or falls faster than
// max_road_grade, or lies under the water level, the road cannot go, and a
// bridge is planned from the last good point to the first point beyond the
// trouble at which a deck of at most max_bridge_grade lands on good ground
// (a stretch of road's worth of it) with the ground nowhere above the deck —
// as long as that span is no longer than max_span and the deck is worth it
// (ground at least min_dip under it somewhere, or water). Over water both
// ends stand at least `freeboard` above the water level, so the deck clears
// it. Where no such span exists the road goes on over the ground.
struct RoadPlanStyle {
  double max_road_grade = 0.18;          // 0.01 .. 2 (tangent)
  double max_bridge_grade = 0.06;        // 0 .. 1
  double max_span_meters = 400.0;        // 5 .. 5000
  double min_dip_meters = 2.0;           // 0 .. 100
  double freeboard_meters = 1.5;         // 0 .. 20
};
void validate_road_plan_style(const RoadPlanStyle& style);
// A route to plan: a plan polyline of a class; a waypoint with a lift is a
// viaduct point and the legs touching it are bridges (the deck passes the
// lift over the ground there — an overpass), planned without sampling.
struct RoadRoute {
  std::vector<math::Vec2> points;
  RoadClass cls = RoadClass::Street;
  std::vector<double> lifts;  // empty, or one per point
};
// Plans a network from routes. Waypoints at the same position (within a
// centimetre) are one node, so routes that share a waypoint join there. A
// route whose ground cannot be sampled at a waypoint (a hole, outside the
// terrain) is rejected. The result is validated.
RoadNetworkDef plan_road_network(std::span<const RoadRoute> routes, const StandingHeightFn& ground,
                                 std::optional<double> water_level, const RoadStyle& style, const RoadPlanStyle& plan);
RoadNetworkDef plan_road_network(std::span<const std::vector<math::Vec2>> routes, const StandingHeightFn& ground,
                                 std::optional<double> water_level, const RoadStyle& style, const RoadPlanStyle& plan);
// A network from JSON: {"version": 1, "nodes": [{"x": .., "z": .., "lift": 0}, ..],
// "segments": [{"from": i, "to": j, "kind": "road" | "bridge", "class": "street" | "avenue" |
// "highway" | "ramp" | "runway" | "taxiway" | "apron"}, ..]} gives the graph as
// authored; {"version": 1, "routes": [[[x, z], [x, z], ..], {"points": [..], "class": "highway",
// "lifts": [..]}, ..]} gives routes to plan; positions may also be {x, z} objects.
// `origin` is added to every position (the viewer passes the terrain centre, so
// positions are offsets from it, as --view-x/--view-z are). Validated.
RoadNetworkDef parse_road_network(const std::string& json_text, const StandingHeightFn& ground, std::optional<double> water_level,
                                  const RoadStyle& style, const RoadPlanStyle& plan, math::Vec2 origin = {});

// A trunk lying across a segment.
struct RoadBlockage {
  std::uint32_t segment = 0;
  std::uint32_t tree = 0;
  double along_meters = 0.0;    // from node a along the segment
  bool burning = false;         // still burning the last time the network advanced
};
struct RoadSegmentState {
  double damage = 0.0;          // 0 sound .. 1 destroyed (by fire on the surface)
  bool destroyed = false;
  std::string destroyed_by;       // "fire", "flood", or what destroy() was told
  double washout_seconds = 0.0;   // running water at a pier (bridges) or over the surface (roads) for this long
  std::size_t blockages = 0;      // trunks lying across it
};
struct RoadEvent {
  enum class Kind : std::uint8_t { Blocked, Damaged, Alight, Destroyed, Collapsed, WashedOut } kind;
  std::uint32_t segment = 0;
  int tree = -1;
};
const char* road_event_name(RoadEvent::Kind kind);

// Where a plan position lies on the network.
struct RoadSample {
  bool on = false;              // within the carriageway of a segment
  std::uint32_t segment = 0;
  RoadKind kind = RoadKind::Road;
  RoadClass cls = RoadClass::Street;
  double along_meters = 0.0;    // from node a
  double offset_meters = 0.0;   // lateral, signed (+ to the right looking from a to b)
  double surface_meters = 0.0;  // the surface height there (a deck's even where it is gone)
  double speed_factor = 1.0;    // what the surface does to the walking speed
  bool blocked = false;         // a trunk lies across the road within the blockage reach
  double damage = 0.0;
  bool destroyed = false;
  bool deck_missing = false;    // on a destroyed bridge, between the stubs
};

// Road geometry is built from the shared mesh parts (mesh_parts.hpp).
using RoadMeshPart = MeshPart;

struct RoadStats {
  std::size_t nodes = 0, roads = 0, bridges = 0, blocked = 0, damaged = 0, destroyed = 0, blockages = 0;
  std::size_t streets = 0, avenues = 0, highways = 0, ramps = 0, runways = 0, taxiways = 0, aprons = 0;
  std::size_t curve_joints = 0;  // nodes where exactly two segments meet at an angle: a curve's joints
  double road_meters = 0.0, bridge_meters = 0.0, longest_bridge_meters = 0.0, highest_deck_meters = 0.0;
  double highway_meters = 0.0, runway_meters = 0.0;
};

class RoadNetwork {
 public:
  // The ground the roads are draped over and the decks are founded on: the
  // walker's standing height without the water (so piers reach the bed).
  RoadNetwork(RoadNetworkDef def, const RoadStyle& style, StandingHeightFn ground);
  const RoadNetworkDef& def() const { return def_; }
  const RoadStyle& style() const { return style_; }
  const std::vector<RoadSegmentState>& states() const { return states_; }
  const std::vector<RoadBlockage>& blockages() const { return blockages_; }
  const std::vector<RoadEvent>& events() const { return events_; }  // of the last advance / landing / destroy; cleared by advance()
  RoadStats stats() const;
  std::size_t size() const { return def_.segments.size(); }
  bool empty() const { return def_.segments.empty(); }
  // The segment's length, direction, width (by its class) and surface heights (the ground plus
  // the lift at each node, plus the node's own lift on a viaduct).
  double length(std::uint32_t segment) const;
  math::Vec2 direction(std::uint32_t segment) const;
  double width(std::uint32_t segment) const;
  double max_width() const { return 2.0 * max_half_; }
  double surface_at_node(std::uint32_t node) const;
  // The segments whose carriageway may reach within `radius` of a plan position (a superset).
  template <class Visit> void for_each_segment_near(double x, double z, double radius, Visit&& visit) const {
    if (geometry_.empty()) return;
    const auto lo_x = index_cell(x - radius), hi_x = index_cell(x + radius), lo_z = index_cell(z - radius), hi_z = index_cell(z + radius);
    for (auto cz = lo_z; cz <= hi_z; ++cz)
      for (auto cx = lo_x; cx <= hi_x; ++cx) {
        const auto found = index_.find(index_key(cx, cz));
        if (found == index_.end()) continue;
        for (const auto i : found->second) visit(i);
      }
  }
  // The smallest distance from a plan position to a segment's centre line (within its length), or +inf.
  double distance_to_centreline(double x, double z, double search_radius = 200.0) const;
  // How high the deck of a bridge stands over the ground at its lowest point (0 for a road).
  double clearance(std::uint32_t segment) const;
  // Where a plan position lies: the segment whose carriageway covers it
  // nearest its centre line — or, given the feet's height, the highest of
  // the covering surfaces within a metre above the feet (a viaduct over a
  // road: the deck for whoever stands on it, the road for whoever walks
  // under it).
  RoadSample sample(double x, double z) const;
  RoadSample sample(double x, double z, double feet_y) const;
  // The walker's hooks: the surface the feet can rest on at a plan position
  // (a road surface; a deck where it still stands; of overlapping surfaces
  // the highest within a metre above the feet), the speed factor, and the
  // railings and piers of the standing bridges as obstacles.
  std::optional<double> platform_height(double x, double z) const;
  std::optional<double> platform_height(double x, double z, double feet_y) const;
  double speed_factor(double x, double z) const;
  double speed_factor(double x, double z, double feet_y) const;
  std::vector<Obstacle> obstacles() const;
  // A trunk came down: lying from `base` along `direction` (unit, plan) for
  // `length` metres, weighing `mass_kg`. Every segment it crosses is blocked
  // where it crosses (event Blocked, one per segment) and takes impact damage
  // of mass / impact_tonnes (event Damaged; a deck smashed outright collapses);
  // a burning trunk will damage them further as the network advances.
  // Returns the number of segments newly blocked.
  std::size_t log_landed(std::uint32_t tree, math::Vec2 base, math::Vec2 direction, double length, bool burning, double mass_kg = 0.0);
  // Advances by dt seconds: burning trunks damage the surfaces they lie on
  // (`log_intensity(tree)` is 0 .. 1, 0 once it has burned out), a deck at
  // full damage collapses (Destroyed + Collapsed), running water at a pier
  // (from `water` over the pier's ground) for washout_seconds washes a bridge
  // out (Destroyed + WashedOut), running water over a road's surface for
  // twice that washes the road away (the same events). Events of the call
  // replace the previous ones.
  void advance(double dt, const std::function<double(std::uint32_t tree)>& log_intensity, const WaterFn& water);
  // Destroys a segment outright (a bridge collapses, a road is rubble).
  void destroy(std::uint32_t segment, const char* cause);
  // Everything sound, unblocked and intact again.
  void reset();
  // Changes whenever the drawn geometry or the obstacles change.
  std::uint64_t version() const { return version_; }
  // Rendering: the parts of the network as they stand (the sound surfaces of
  // a class merged into one part, damaged ones their own).
  std::vector<RoadMeshPart> build_meshes() const;
  // Flames and smoke on the burning surfaces, appended.
  void emit(std::vector<FireInstance>& flames, std::vector<FireInstance>& smoke, const FireStyle& style) const;

 private:
  struct Geometry {
    math::Vec2 a, b, dir, right; double length = 0.0, ya = 0.0, yb = 0.0, half = 0.0;
    math::Vec2 mitre_a, mitre_b;  // the cross-section direction at each end (the right vector, or the mitre at a curve's joint)
    double mitre_scale_a = 1.0, mitre_scale_b = 1.0;
  };
  double deck_height(std::uint32_t segment, double along) const;
  bool deck_stands(std::uint32_t segment, double along) const;
  std::vector<math::Vec2> pier_positions(std::uint32_t segment) const;
  double ground_at(double x, double z, double fallback) const;
  void raise(RoadEvent::Kind kind, std::uint32_t segment, int tree = -1);
  std::int64_t index_cell(double v) const { return static_cast<std::int64_t>(std::floor(v / kIndexCell)); }
  static std::uint64_t index_key(std::int64_t cx, std::int64_t cz) { return (static_cast<std::uint64_t>(cx) << 32) ^ static_cast<std::uint32_t>(cz); }
  static constexpr double kIndexCell = 64.0;
  RoadNetworkDef def_;
  RoadStyle style_;
  StandingHeightFn ground_;
  std::vector<Geometry> geometry_;
  std::vector<double> node_surface_;
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> index_;
  double max_half_ = 0.0;
  std::vector<RoadSegmentState> states_;
  std::vector<RoadBlockage> blockages_;
  std::vector<RoadEvent> events_;
  std::uint64_t version_ = 0;
};

// Removes the plants standing on the carriageways (trees within the half
// width plus tree_margin of a centre line, grass within the half width plus
// grass_margin), keeping the chunks consistent. Returns how many were removed.
std::size_t clear_vegetation_along(VegetationPlacement& placement, const RoadNetwork& network, double tree_margin_meters = 2.0,
                                   double grass_margin_meters = 0.3);

// The network's parts are drawn by the shared MeshPartRenderer (one draw per part).
using RoadRenderer = MeshPartRenderer;

}  // namespace terraflux::graphics
