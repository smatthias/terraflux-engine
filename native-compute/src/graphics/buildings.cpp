#include <terraflux/graphics/buildings.hpp>

#include <terraflux/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kWallPostRadius = 0.35;   // the walls as chains of posts this thick, touching
constexpr double kFoundationDepth = 0.5;   // below the lowest ground corner
constexpr double kFloorAboveGround = 0.15; // the slab top over the ground at the door: the entrance is at grade
constexpr double kWall = kInteriorWallThickness;  // an enterable building's walls
constexpr double kDoorwayClearance = 0.15; // the doorway is this much wider than the leaf on either side (the frame)
constexpr double kDoorPostRadius = 0.08;   // a door leaf as a chain of posts this thick
constexpr double kDoorSwingSeconds = 0.6;  // a full swing
constexpr double kRampGrade = 0.3;         // the footpath climbs to a raised door at most this steeply (1:3.3)

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool finite(math::Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
math::Vec3 lift(math::Vec2 p, double y) { return {p.x, y, p.y}; }
math::Vec3 plan3(math::Vec2 v) { return {v.x, 0.0, v.y}; }
const math::Vec3 kUp{0, 1, 0};

// The building's local frame: a along the front wall, b toward the road.
struct Frame {
  math::Vec2 c, u, v;
  math::Vec2 plan(double a, double b) const { return c + u * a + v * b; }
  math::Vec3 at(double a, double b, double y) const { return lift(plan(a, b), y); }
  // Local coordinates of a plan point.
  std::pair<double, double> local(math::Vec2 p) const { const auto r = p - c; return {r.dot(u), r.dot(v)}; }
};
Frame frame_of(const Building& b) { return {b.centre, b.along, b.facing}; }

// Wall height (eaves) and roof pitch by kind.
double wall_height(const BuildingSpec& s) { return s.storeys * s.storey_height_meters; }
double roof_pitch(BuildingKind kind) {
  switch (kind) {
    case BuildingKind::Bungalow: return 38.0 * kPi / 180.0;
    case BuildingKind::House: return 40.0 * kPi / 180.0;
    case BuildingKind::Hangar: return 12.0 * kPi / 180.0;
    default: return 0.0;
  }
}
double roof_overhang(BuildingKind kind) { return kind == BuildingKind::Bungalow ? 0.7 : kind == BuildingKind::House ? 0.5 : kind == BuildingKind::Hangar ? 0.4 : 0.3; }
bool gabled(BuildingKind kind) { return kind == BuildingKind::Bungalow || kind == BuildingKind::House || kind == BuildingKind::Hangar; }
double roof_height(const BuildingSpec& s) {
  if (gabled(s.kind)) return (0.5 * s.depth_meters + roof_overhang(s.kind)) * std::tan(roof_pitch(s.kind)) + 0.25;
  switch (s.kind) {
    case BuildingKind::Skyscraper: return 4.4;   // the plant room
    case BuildingKind::PoliceStation: return 3.6; // the mast and beacon
    case BuildingKind::ControlTower: return 4.8;  // the cab
    default: return 0.6;                          // the slab and parapet
  }
}
// The door leaf's plan width and height.
struct DoorSize { double width, height; };
DoorSize door_size(BuildingKind kind) {
  switch (kind) {
    case BuildingKind::Bungalow: return {0.9, 2.0};
    case BuildingKind::House: return {1.0, 2.1};
    case BuildingKind::Hospital: return {2.4, 2.6};
    case BuildingKind::Skyscraper: return {3.0, 3.0};
    case BuildingKind::PoliceStation: return {1.8, 2.4};
    case BuildingKind::Terminal: return {6.0, 3.6};
    case BuildingKind::ControlTower: return {1.2, 2.2};
    case BuildingKind::Hangar: return {20.0, 9.0};
  }
  return {1.0, 2.1};
}
// The floor of an enterable building is raised over the highest ground under it.
bool raised_floor(BuildingKind kind) { return building_enterable(kind); }
// Oriented rectangles (plan) separated by at least `gap`: a separating-axis test over both frames' axes.
bool footprints_clear(math::Vec2 ca, math::Vec2 ua, math::Vec2 va, double ha, double da, math::Vec2 cb, math::Vec2 ub, math::Vec2 vb, double hb, double db, double gap) {
  const math::Vec2 axes[4] = {ua, va, ub, vb};
  const math::Vec2 d = cb - ca;
  for (const auto& axis : axes) {
    const double ra = ha * std::abs(axis.dot(ua)) + da * std::abs(axis.dot(va));
    const double rb = hb * std::abs(axis.dot(ub)) + db * std::abs(axis.dot(vb));
    if (std::abs(d.dot(axis)) > ra + rb + gap) return true;
  }
  return false;
}

}  // namespace

const char* building_kind_name(BuildingKind kind) {
  switch (kind) {
    case BuildingKind::Bungalow: return "bungalow";
    case BuildingKind::House: return "house";
    case BuildingKind::Hospital: return "hospital";
    case BuildingKind::Skyscraper: return "skyscraper";
    case BuildingKind::PoliceStation: return "police";
    case BuildingKind::Terminal: return "terminal";
    case BuildingKind::ControlTower: return "tower";
    case BuildingKind::Hangar: return "hangar";
  }
  throw std::invalid_argument("unknown building kind");
}

std::optional<BuildingKind> building_kind_from_name(const std::string& name) {
  for (const auto kind : {BuildingKind::Bungalow, BuildingKind::House, BuildingKind::Hospital, BuildingKind::Skyscraper, BuildingKind::PoliceStation,
                          BuildingKind::Terminal, BuildingKind::ControlTower, BuildingKind::Hangar})
    if (name == building_kind_name(kind)) return kind;
  if (name == "police station") return BuildingKind::PoliceStation;
  if (name == "control tower") return BuildingKind::ControlTower;
  return std::nullopt;
}

bool building_enterable(BuildingKind kind) { return kind == BuildingKind::Bungalow; }

std::vector<Furnishing> furnish(const BuildingSpec& spec) {
  validate_building_spec(spec);
  std::vector<Furnishing> out;
  if (!building_enterable(spec.kind)) return out;
  const double iw = 0.5 * spec.width_meters - kWall, id = 0.5 * spec.depth_meters - kWall;  // the room's half extents
  const double ceiling = spec.storeys * spec.storey_height_meters - 0.1;
  const double door_half = 0.5 * door_size(spec.kind).width + kDoorwayClearance;
  const math::Vec3 timber{0.50, 0.34, 0.20}, dark_timber{0.42, 0.28, 0.16}, pale_timber{0.56, 0.40, 0.24}, steel{0.75, 0.76, 0.78}, iron{0.25, 0.25, 0.27};
  const math::Vec3 stone{0.55, 0.55, 0.52}, linen{0.92, 0.92, 0.88}, wool{0.30, 0.40, 0.65};
  // A piece is kept when it stands clear of the walls, the doorway and the ceiling.
  const auto add = [&](const char* what, double a, double b, double half_a, double half_b, double bottom, double height, math::Vec3 color,
                       SurfaceMaterial material, bool solid, bool drawn = true) {
    if (std::abs(a) + half_a > iw + 1e-9 || std::abs(b) + half_b > id + 1e-9 || bottom + height > ceiling) return false;
    if (b + half_b > id - 1.2 && std::abs(a) - half_a < door_half + 0.4) return false;  // keep the entrance clear
    out.push_back(Furnishing{what, {a, b}, half_a, half_b, bottom, height, color, material, solid, drawn});
    return true;
  };
  // The kitchen along the back wall: a run of cupboards under a worktop with
  // a sink, a stove beside it and a cupboard on the wall over the worktop.
  {
    const double b = -id + 0.32, a0 = -iw + 0.25;  // 2 cm off the back wall
    if (add("cupboard", a0 + 1.5, b, 1.5, 0.3, 0.0, 0.85, timber, SurfaceMaterial::Timber, true)) {
      add("worktop", a0 + 1.5, b, 1.52, 0.32, 0.85, 0.05, stone, SurfaceMaterial::Stone, false);
      add("sink", a0 + 0.7, b, 0.25, 0.2, 0.9, 0.1, steel, SurfaceMaterial::Metal, false);
      add("cupboard", a0 + 1.1, b - 0.12, 0.9, 0.18, 1.5, 0.7, timber, SurfaceMaterial::Timber, false);  // on the wall, clear of the window
    }
    add("stove", a0 + 3.4, b, 0.3, 0.3, 0.0, 0.9, iron, SurfaceMaterial::Metal, true);
  }
  // A table with four chairs, right of the door.
  {
    const double ta = std::min(1.6, iw - 1.6), tb = 0.2;
    if (add("table", ta, tb, 0.7, 0.45, 0.0, 0.76, pale_timber, SurfaceMaterial::Timber, true, false)) {
      add("table", ta, tb, 0.7, 0.45, 0.72, 0.04, pale_timber, SurfaceMaterial::Timber, false);
      for (const double sa : {-1.0, 1.0})
        for (const double sb : {-1.0, 1.0}) add("table", ta + sa * 0.6, tb + sb * 0.35, 0.04, 0.04, 0.0, 0.72, pale_timber, SurfaceMaterial::Timber, false);
      for (const double sb : {-1.0, 1.0})
        for (const double sa : {-1.0, 1.0}) {
          const double ca = ta + sa * 0.4, cb = tb + sb * 0.75;
          if (!add("chair", ca, cb, 0.21, 0.21, 0.0, 0.9, timber, SurfaceMaterial::Timber, true, false)) continue;
          add("chair", ca, cb, 0.21, 0.21, 0.42, 0.04, timber, SurfaceMaterial::Timber, false);
          for (const double la : {-1.0, 1.0})
            for (const double lb : {-1.0, 1.0}) add("chair", ca + la * 0.18, cb + lb * 0.18, 0.02, 0.02, 0.0, 0.42, timber, SurfaceMaterial::Timber, false);
          add("chair", ca, cb + sb * 0.19, 0.21, 0.02, 0.46, 0.44, timber, SurfaceMaterial::Timber, false);  // the backrest, away from the table
        }
    }
  }
  // A wardrobe against the left wall, a bed against the right one.
  add("wardrobe", -iw + 0.32, 1.5, 0.3, 0.6, 0.0, 2.0, dark_timber, SurfaceMaterial::Timber, true);
  {
    const double ba = iw - 0.72, bb = -id + 1.65;
    if (add("bed", ba, bb, 0.7, 1.0, 0.0, 0.35, timber, SurfaceMaterial::Timber, false)) {
      add("mattress", ba, bb, 0.66, 0.96, 0.35, 0.22, linen, SurfaceMaterial::Plain, true);
      add("blanket", ba, bb - 0.36, 0.66, 0.6, 0.57, 0.05, wool, SurfaceMaterial::Plain, false);
      add("pillow", ba, bb + 0.7, 0.4, 0.22, 0.57, 0.12, linen, SurfaceMaterial::Plain, false);
    }
  }
  return out;
}

BuildingSpec building_spec(BuildingKind kind) {
  BuildingSpec s;
  s.kind = kind;
  switch (kind) {
    case BuildingKind::Bungalow: s.width_meters = 9.0; s.depth_meters = 7.0; s.storeys = 1; s.storey_height_meters = 2.8; s.setback_meters = 6.0; s.path_width_meters = 1.6; s.max_site_step_meters = 2.5; break;
    case BuildingKind::House: s.width_meters = 11.0; s.depth_meters = 9.0; s.storeys = 2; s.storey_height_meters = 3.0; s.setback_meters = 7.0; s.path_width_meters = 2.0; s.max_site_step_meters = 3.0; break;
    case BuildingKind::Hospital: s.width_meters = 40.0; s.depth_meters = 20.0; s.storeys = 5; s.storey_height_meters = 3.6; s.setback_meters = 12.0; s.path_width_meters = 4.0; s.max_site_step_meters = 5.0; break;
    case BuildingKind::Skyscraper: s.width_meters = 30.0; s.depth_meters = 30.0; s.storeys = 20; s.storey_height_meters = 3.8; s.setback_meters = 14.0; s.path_width_meters = 5.0; s.max_site_step_meters = 6.0; break;
    case BuildingKind::PoliceStation: s.width_meters = 24.0; s.depth_meters = 16.0; s.storeys = 2; s.storey_height_meters = 3.4; s.setback_meters = 9.0; s.path_width_meters = 3.0; s.max_site_step_meters = 3.5; break;
    case BuildingKind::Terminal: s.width_meters = 90.0; s.depth_meters = 30.0; s.storeys = 2; s.storey_height_meters = 4.5; s.setback_meters = 16.0; s.path_width_meters = 8.0; s.max_site_step_meters = 5.0; break;
    case BuildingKind::ControlTower: s.width_meters = 8.0; s.depth_meters = 8.0; s.storeys = 7; s.storey_height_meters = 3.5; s.setback_meters = 8.0; s.path_width_meters = 2.0; s.max_site_step_meters = 3.0; break;
    case BuildingKind::Hangar: s.width_meters = 45.0; s.depth_meters = 40.0; s.storeys = 1; s.storey_height_meters = 5.0; s.setback_meters = 10.0; s.path_width_meters = 20.0; s.max_site_step_meters = 6.0; break;
  }
  // The big footprints sit on plinths and cut into the slope behind; the
  // bungalow's floor stands over the highest ground, its door at most this
  // far above the path (a ramp up to it).
  switch (kind) {
    case BuildingKind::Bungalow: s.max_buried_meters = 1.5; break;
    case BuildingKind::Skyscraper: case BuildingKind::Hospital: s.max_buried_meters = 2.5; break;
    case BuildingKind::Terminal: s.max_buried_meters = 3.5; s.max_site_step_meters = 7.0; break;
    case BuildingKind::Hangar: s.max_buried_meters = 3.0; break;
    default: break;
  }
  return s;
}

void validate_building_spec(const BuildingSpec& s) {
  (void)building_kind_name(s.kind);
  if (!within(s.width_meters, 3, 200) || !within(s.depth_meters, 3, 200)) throw std::invalid_argument("building footprint must be 3..200 m");
  if (s.storeys < 1 || s.storeys > 200 || !within(s.storey_height_meters, 2, 6)) throw std::invalid_argument("building needs 1..200 storeys of 2..6 m");
  if (!within(s.setback_meters, 1, 200) || !within(s.path_width_meters, 0.5, 20)) throw std::invalid_argument("setback must be 1..200 m and the path 0.5..20 m wide");
  if (!within(s.max_site_step_meters, 0, 50) || !within(s.max_buried_meters, 0, 10)) throw std::invalid_argument("site step must be 0..50 m and burial 0..10 m");
  if (s.kind == BuildingKind::Hospital && s.storeys < 5) throw std::invalid_argument("a hospital has at least five storeys");
  if (s.kind == BuildingKind::Skyscraper && s.storeys < 20) throw std::invalid_argument("a skyscraper has at least twenty storeys");
  if (s.kind == BuildingKind::Bungalow && s.storeys != 1) throw std::invalid_argument("a bungalow has one storey");
  if (s.kind == BuildingKind::Hangar && s.storeys != 1) throw std::invalid_argument("a hangar is one tall storey");
  if (s.kind == BuildingKind::ControlTower && s.storeys < 5) throw std::invalid_argument("a control tower has at least five storeys");
}

void validate_building_placement_style(const BuildingPlacementStyle& s) {
  if (!within(s.candidate_spacing_meters, 1, 100) || !within(s.end_margin_meters, 0, 500) || !within(s.gap_meters, 0, 100))
    throw std::invalid_argument("candidate spacing must be 1..100 m, end margin 0..500 m and gap 0..100 m");
  if (!within(s.water_margin_meters, 0, 20) || !within(s.surface_lift_meters, 0, 1) || !within(s.path_speed_factor, 1, 3))
    throw std::invalid_argument("water margin must be 0..20 m, lift 0..1 m and path speed factor 1..3");
}

namespace {

// Lays a building of `spec` out at a site: the front wall parallel to the
// road at the setback from the carriageway edge, the door facing it.
std::optional<Building> lay_out(const RoadNetwork& roads, const BuildingSpec& spec, const BuildingSite& site, const StandingHeightFn& ground) {
  if (site.segment >= roads.size()) throw std::invalid_argument("building site refers to a segment that does not exist");
  if (roads.def().segments[site.segment].kind != RoadKind::Road) throw std::invalid_argument("a building stands beside a road, not a bridge");
  if (site.side != 1 && site.side != -1) throw std::invalid_argument("building side must be +1 (right) or -1 (left)");
  if (!within(site.along_meters, 0, roads.length(site.segment))) throw std::invalid_argument("building site lies beyond the segment");
  const math::Vec2 dir = roads.direction(site.segment);
  const math::Vec2 right{-dir.y, dir.x};
  const math::Vec2 outward = right * static_cast<double>(site.side);
  const math::Vec2 a = roads.def().nodes[roads.def().segments[site.segment].a].position;
  const math::Vec2 on_road = a + dir * site.along_meters;
  const double half_road = 0.5 * roads.width(site.segment);
  Building b;
  b.kind = spec.kind; b.spec = spec; b.site = site; b.storeys = spec.storeys;
  b.path_end = on_road + outward * half_road;
  b.door = on_road + outward * (half_road + spec.setback_meters);
  b.centre = on_road + outward * (half_road + spec.setback_meters + 0.5 * spec.depth_meters);
  b.facing = outward * -1.0;
  b.along = dir;
  const Frame frame{b.centre, b.along, b.facing};
  double lo = std::numeric_limits<double>::infinity(), hi = -lo;
  for (const double sa : {-0.5, 0.0, 0.5})
    for (const double sb : {-0.5, 0.0, 0.5}) {
      const auto p = frame.plan(sa * spec.width_meters, sb * spec.depth_meters);
      const auto h = ground(p.x, p.y);
      if (!h || !std::isfinite(*h)) return std::nullopt;
      lo = std::min(lo, *h); hi = std::max(hi, *h);
    }
  for (const double t : {0.0, 0.5, 1.0}) {
    const auto p = b.door + (b.path_end - b.door) * t;
    if (!ground(p.x, p.y)) return std::nullopt;
  }
  b.ground_min_meters = lo; b.ground_max_meters = hi;
  // The entrance is at grade: the floor sits on the ground at the door; where
  // the ground falls away behind, the foundation takes up the difference. An
  // enterable building's floor stands over the highest ground under it (so
  // the ground never rises through the rooms) and the footpath ramps up.
  b.floor_meters = (raised_floor(spec.kind) ? hi : *ground(b.door.x, b.door.y)) + kFloorAboveGround;
  b.height_meters = wall_height(spec) + roof_height(spec);
  return b;
}

double footprint_radius(const Building& b) { return 0.5 * std::hypot(b.spec.width_meters, b.spec.depth_meters); }

// Whether a laid-out building may stand where it is: flat and dry enough,
// off every carriageway, clear of the others.
bool suitable(const Building& b, const RoadNetwork& roads, std::span<const Building> placed, const StandingHeightFn& ground,
              std::optional<double> water_level, const BuildingPlacementStyle& style, const char** why = nullptr) {
  const auto fail = [&](const char* reason) { if (why) *why = reason; return false; };
  if (b.ground_max_meters - b.ground_min_meters > b.spec.max_site_step_meters) return fail("the ground is too uneven");
  if (b.ground_max_meters > b.floor_meters + b.spec.max_buried_meters) return fail("the ground rises too far over the floor behind the entrance");
  if (raised_floor(b.kind)) {
    const auto at_door = ground(b.door.x, b.door.y);
    if (!at_door || b.floor_meters - *at_door > b.spec.max_buried_meters + kFloorAboveGround) return fail("the ground rises too far over the floor behind the entrance");
  }
  if (water_level && b.ground_min_meters <= *water_level + style.water_margin_meters) return fail("the site is under water or on the shore");
  const Frame frame = frame_of(b);
  const double search = 0.5 * roads.max_width() + 1.0;
  for (const double sa : {-0.5, -0.25, 0.0, 0.25, 0.5})
    for (const double sb : {-0.5, -0.25, 0.0, 0.25, 0.5}) {
      const auto p = frame.plan(sa * b.spec.width_meters, sb * b.spec.depth_meters);
      if (roads.sample(p.x, p.y).on) return fail("the footprint lies on a carriageway");
      bool on_corridor = false;
      roads.for_each_segment_near(p.x, p.y, search, [&](std::uint32_t i) {
        if (on_corridor) return;
        const auto& seg = roads.def().segments[i];
        const auto na = roads.def().nodes[seg.a].position, nb = roads.def().nodes[seg.b].position;
        const auto d = nb - na;
        const double len = d.length();
        if (len < 1e-9) return;
        const double along = std::clamp((p - na).dot(d) / len, 0.0, len);
        if ((na + d * (along / len) - p).length() < 0.5 * roads.width(i) + 0.5) on_corridor = true;
      });
      if (on_corridor) return fail("the footprint lies on a carriageway");
      const auto h = ground(p.x, p.y);
      if (!h) return fail("the footprint has no ground");
    }
  const double reach = footprint_radius(b) + style.gap_meters;
  for (const auto& other : placed) {
    if ((other.centre - b.centre).length() > reach + footprint_radius(other) + other.spec.setback_meters) continue;
    if (!footprints_clear(b.centre, b.along, b.facing, 0.5 * b.spec.width_meters, 0.5 * b.spec.depth_meters, other.centre, other.along, other.facing,
                          0.5 * other.spec.width_meters, 0.5 * other.spec.depth_meters, style.gap_meters))
      return fail("the site is taken by another building");
    // Footpaths stay clear of footprints both ways.
    for (const double t : {0.0, 0.5, 1.0}) {
      const auto [a, bb] = frame.local(other.door + (other.path_end - other.door) * t);
      if (std::abs(a) <= 0.5 * b.spec.width_meters + 0.5 * other.spec.path_width_meters && std::abs(bb) <= 0.5 * b.spec.depth_meters + 0.5 * other.spec.path_width_meters)
        return fail("the site is taken by another building");
      const auto [oa, ob] = frame_of(other).local(b.door + (b.path_end - b.door) * t);
      if (std::abs(oa) <= 0.5 * other.spec.width_meters + 0.5 * b.spec.path_width_meters && std::abs(ob) <= 0.5 * other.spec.depth_meters + 0.5 * b.spec.path_width_meters)
        return fail("the site's footpath would cross another building");
    }
  }
  return true;
}

}  // namespace

std::vector<Building> place_buildings(const RoadNetwork& roads, std::span<const BuildingRequest> requests, const StandingHeightFn& ground,
                                      std::optional<double> water_level, math::Vec2 preferred, const BuildingPlacementStyle& style, bool skip_unsuitable_sites,
                                      PlacementReport* report) {
  validate_building_placement_style(style);
  if (!ground) throw std::invalid_argument("placing buildings needs a ground function");
  if (!finite(preferred)) throw std::invalid_argument("the preferred point must be finite");
  if (water_level && !std::isfinite(*water_level)) throw std::invalid_argument("water level must be finite");
  std::vector<Building> placed;
  PlacementReport local;
  PlacementReport& rep = report ? *report : local;
  const auto count_reason = [&](const std::string& why) {
    if (why.find("uneven") != std::string::npos) ++rep.uneven;
    else if (why.find("rises") != std::string::npos) ++rep.buried;
    else if (why.find("water") != std::string::npos) ++rep.wet;
    else if (why.find("carriageway") != std::string::npos) ++rep.on_road;
    else if (why.find("no ground") != std::string::npos) ++rep.no_ground;
    else ++rep.taken;
  };
  for (const auto& request : requests) {
    const auto spec = building_spec(request.kind);
    validate_building_spec(spec);
    if (request.site) {
      auto laid = lay_out(roads, spec, *request.site, ground);
      if (!laid) { if (skip_unsuitable_sites) { ++rep.no_ground; continue; } throw std::invalid_argument("building site has no ground"); }
      const char* why = "";
      if (!suitable(*laid, roads, placed, ground, water_level, style, &why)) {
        if (skip_unsuitable_sites) { count_reason(why); continue; }
        throw std::invalid_argument(std::string("building site is unsuitable: ") + why);
      }
      placed.push_back(*laid);
      ++rep.placed;
      continue;
    }
    std::optional<Building> best;
    double best_distance = std::numeric_limits<double>::infinity();
    const math::Vec2 near_point = request.preferred.value_or(preferred);
    if (!finite(near_point) || !within(request.search_radius_meters, 0, 1e12)) throw std::invalid_argument("a request's preferred point must be finite and its radius non-negative");
    for (std::uint32_t seg = 0; seg < roads.size(); ++seg) {
      const auto cls = roads.def().segments[seg].cls;
      if (roads.def().segments[seg].kind != RoadKind::Road || cls == RoadClass::Highway || cls == RoadClass::Ramp || cls == RoadClass::Runway || cls == RoadClass::Taxiway) continue;
      const double len = roads.length(seg);
      // Only segments that can come within the search radius are walked.
      {
        const auto& d = roads.def();
        const auto a = d.nodes[d.segments[seg].a].position, b = d.nodes[d.segments[seg].b].position;
        const auto dd = b - a;
        const double along = std::clamp((near_point - a).dot(dd) / std::max(len, 1e-9), 0.0, len);
        if ((a + dd * (along / std::max(len, 1e-9)) - near_point).length() > request.search_radius_meters + 200.0) continue;
      }
      for (double along = style.end_margin_meters; along <= len - style.end_margin_meters + 1e-9; along += style.candidate_spacing_meters)
        for (const int side : {1, -1}) {
          const auto laid = lay_out(roads, spec, {seg, along, side}, ground);
          if (!laid) continue;
          const double distance = (laid->centre - near_point).length();
          if (distance >= best_distance || distance > request.search_radius_meters) continue;
          if (!suitable(*laid, roads, placed, ground, water_level, style)) continue;
          best = laid; best_distance = distance;
        }
    }
    if (best) { placed.push_back(*best); ++rep.placed; } else ++rep.no_site;
  }
  return placed;
}

std::vector<BuildingRequest> parse_building_requests(const std::string& json_text) {
  using terraflux::json::Json;
  json::Parser parser(json_text);
  const Json root = parser.parse_document();
  const auto* object = std::get_if<Json::Object>(&root.value);
  if (!object) throw std::invalid_argument("road network JSON must be an object");
  std::vector<BuildingRequest> out;
  const auto it = object->find("buildings");
  if (it == object->end()) return out;
  const auto* list = std::get_if<Json::Array>(&it->second.value);
  if (!list) throw std::invalid_argument("road network JSON: buildings must be an array");
  const auto kind_of = [](const std::string& name) {
    const auto kind = building_kind_from_name(name);
    if (!kind) throw std::invalid_argument("road network JSON: unknown building kind " + name);
    return *kind;
  };
  for (const auto& item : *list) {
    BuildingRequest request;
    if (const auto* name = std::get_if<std::string>(&item.value)) { request.kind = kind_of(*name); out.push_back(request); continue; }
    const auto* o = std::get_if<Json::Object>(&item.value);
    if (!o) throw std::invalid_argument("road network JSON: a building is a kind name or an object");
    const auto kind = o->find("kind");
    if (kind == o->end()) throw std::invalid_argument("road network JSON: a building needs a kind");
    const auto* kind_name = std::get_if<std::string>(&kind->second.value);
    if (!kind_name) throw std::invalid_argument("road network JSON: a building kind must be a string");
    request.kind = kind_of(*kind_name);
    const auto segment = o->find("segment"), along = o->find("along"), side = o->find("side");
    if (segment != o->end() || along != o->end() || side != o->end()) {
      if (segment == o->end() || along == o->end()) throw std::invalid_argument("road network JSON: a building site needs segment and along");
      const auto* seg = std::get_if<double>(&segment->second.value);
      const auto* al = std::get_if<double>(&along->second.value);
      if (!seg || !al || *seg < 0 || *seg != std::floor(*seg) || !std::isfinite(*al) || *al < 0) throw std::invalid_argument("road network JSON: building segment must be an index and along a distance");
      BuildingSite site{static_cast<std::uint32_t>(*seg), *al, 1};
      if (side != o->end()) {
        if (const auto* s = std::get_if<std::string>(&side->second.value)) {
          if (*s == "left") site.side = -1; else if (*s == "right") site.side = 1; else throw std::invalid_argument("road network JSON: building side must be left or right");
        } else if (const auto* n = std::get_if<double>(&side->second.value)) {
          if (*n != 1 && *n != -1) throw std::invalid_argument("road network JSON: building side must be 1 or -1");
          site.side = static_cast<int>(*n);
        } else throw std::invalid_argument("road network JSON: building side must be left, right, 1 or -1");
      }
      request.site = site;
    }
    out.push_back(request);
  }
  return out;
}

// ---------------------------------------------------------------------------
namespace {

// The footpath's height at a plan point near the door: the lifted ground,
// ramping up to the floor where the door stands above it.
double path_height_at(const Building& b, math::Vec2 p, double ground_y, double lift) {
  const math::Vec2 d = b.path_end - b.door;
  const double len = d.length();
  const double along = len > 1e-9 ? std::clamp((p - b.door).dot(d) / len, 0.0, len) : 0.0;
  return std::max(ground_y + lift, b.floor_meters - kRampGrade * along);
}
// Whether a local point lies in the doorway's opening through the front wall.
bool in_doorway(const Building& b, double a, double bb) {
  const double hd = 0.5 * b.spec.depth_meters;
  return std::abs(a) <= 0.5 * door_size(b.kind).width + kDoorwayClearance && bb >= hd - kWall - 1e-9 && bb <= hd + 1e-9;
}
// Posts along a line, the last one centred on `to`.
void post_chain(std::vector<Obstacle>& out, math::Vec2 from, math::Vec2 to, double radius, double bottom, double top) {
  const double len = (to - from).length();
  const int count = std::max(1, static_cast<int>(std::ceil(len / (2.0 * radius))));
  for (int k = 0; k <= count; ++k) {
    const auto p = from + (to - from) * (static_cast<double>(k) / count);
    Obstacle o; o.x = p.x; o.z = p.y; o.radius = radius; o.bottom = bottom; o.top = top;
    out.push_back(o);
  }
}

}  // namespace

BuildingSet::BuildingSet(std::vector<Building> buildings, StandingHeightFn ground, const BuildingPlacementStyle& style)
    : buildings_(std::move(buildings)), ground_(std::move(ground)), style_(style) {
  validate_building_placement_style(style_);
  if (!ground_) throw std::invalid_argument("a building set needs a ground function");
  for (const auto& b : buildings_) {
    validate_building_spec(b.spec);
    if (!finite(b.centre) || !finite(b.along) || !finite(b.facing) || std::abs(b.along.length() - 1) > 1e-6 || std::abs(b.facing.length() - 1) > 1e-6)
      throw std::invalid_argument("building frame must be finite with unit axes");
  }
  doors_.resize(buildings_.size());
}

BuildingStats BuildingSet::stats() const {
  BuildingStats s;
  s.buildings = buildings_.size();
  for (const auto& b : buildings_) {
    switch (b.kind) {
      case BuildingKind::Bungalow: ++s.bungalows; break;
      case BuildingKind::House: ++s.houses; break;
      case BuildingKind::Hospital: ++s.hospitals; break;
      case BuildingKind::Skyscraper: ++s.skyscrapers; break;
      case BuildingKind::PoliceStation: ++s.police_stations; break;
      case BuildingKind::Terminal: ++s.terminals; break;
      case BuildingKind::ControlTower: ++s.towers; break;
      case BuildingKind::Hangar: ++s.hangars; break;
    }
    if (building_enterable(b.kind)) ++s.interiors;
    s.tallest_meters = std::max(s.tallest_meters, b.height_meters);
    s.most_storeys = std::max(s.most_storeys, b.storeys);
    s.floor_area_m2 += b.spec.width_meters * b.spec.depth_meters * b.storeys;
  }
  return s;
}

std::optional<double> BuildingSet::platform_height(double x, double z) const {
  if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("building sample position must be finite");
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    const auto& b = buildings_[i];
    const auto [a, bb] = frame_of(b).local({x, z});
    if (building_enterable(b.kind) && (inside(i, x, z) || in_doorway(b, a, bb))) return b.floor_meters;
    if (std::abs(a) > 0.5 * b.spec.path_width_meters || bb < 0.5 * b.spec.depth_meters - 1e-9 || bb > 0.5 * b.spec.depth_meters + b.spec.setback_meters + 1e-9) continue;
    const auto h = ground_(x, z);
    if (!h) continue;
    return path_height_at(b, {x, z}, *h, style_.surface_lift_meters);
  }
  return std::nullopt;
}

double BuildingSet::speed_factor(double x, double z) const { return platform_height(x, z) ? style_.path_speed_factor : 1.0; }

std::optional<std::size_t> BuildingSet::building_at(double x, double z, double margin) const {
  if (!std::isfinite(x) || !std::isfinite(z) || !within(margin, 0, 1000)) throw std::invalid_argument("building query out of range");
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    const auto& b = buildings_[i];
    const auto [a, bb] = frame_of(b).local({x, z});
    if (std::abs(a) <= 0.5 * b.spec.width_meters + margin && std::abs(bb) <= 0.5 * b.spec.depth_meters + margin) return i;
  }
  return std::nullopt;
}

std::optional<std::size_t> BuildingSet::entrance_near(double x, double z, double reach) const {
  if (!std::isfinite(x) || !std::isfinite(z) || !within(reach, 0, 1000)) throw std::invalid_argument("entrance query out of range");
  std::optional<std::size_t> best;
  double best_d = reach;
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    const double d = (buildings_[i].door - math::Vec2{x, z}).length();
    if (d <= best_d) { best_d = d; best = i; }
  }
  return best;
}

bool BuildingSet::inside(std::size_t building, double x, double z) const {
  if (building >= buildings_.size()) throw std::out_of_range("building index out of range");
  if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("building query out of range");
  const auto& b = buildings_[building];
  if (!building_enterable(b.kind)) return false;
  const auto [a, bb] = frame_of(b).local({x, z});
  return std::abs(a) < 0.5 * b.spec.width_meters - kWall && std::abs(bb) < 0.5 * b.spec.depth_meters - kWall;
}

std::optional<std::size_t> BuildingSet::inside_of(double x, double z) const {
  for (std::size_t i = 0; i < buildings_.size(); ++i)
    if (inside(i, x, z)) return i;
  return std::nullopt;
}

std::vector<Obstacle> BuildingSet::obstacles() const {
  std::vector<Obstacle> out;
  for (const auto& b : buildings_) {
    const Frame frame = frame_of(b);
    const double hw = 0.5 * b.spec.width_meters, hd = 0.5 * b.spec.depth_meters;
    const double bottom = b.ground_min_meters - kFoundationDepth, top = b.floor_meters + b.height_meters;
    const auto chain = [&](math::Vec2 from, math::Vec2 to) { post_chain(out, from, to, kWallPostRadius, bottom, top); };
    if (!building_enterable(b.kind)) {
      chain(frame.plan(-hw, hd), frame.plan(hw, hd));
      chain(frame.plan(hw, hd), frame.plan(hw, -hd));
      chain(frame.plan(hw, -hd), frame.plan(-hw, -hd));
      chain(frame.plan(-hw, -hd), frame.plan(-hw, hd));
      continue;
    }
    // Hollow: the posts stand on the walls' centre lines, the front wall's
    // chains end at the doorway's jambs (the gap is the doorway's width).
    const double mw = hw - 0.5 * kWall, md = hd - 0.5 * kWall;
    const double jamb = 0.5 * door_size(b.kind).width + kDoorwayClearance + kWallPostRadius;
    chain(frame.plan(-mw, md), frame.plan(-jamb, md));
    chain(frame.plan(mw, md), frame.plan(jamb, md));
    chain(frame.plan(mw, md), frame.plan(mw, -md));
    chain(frame.plan(mw, -md), frame.plan(-mw, -md));
    chain(frame.plan(-mw, -md), frame.plan(-mw, md));
    // The furniture: a solid piece as posts along its longer axis, as thick as its shorter one.
    for (const auto& f : furnish(b.spec)) {
      if (!f.solid) continue;
      const double r = std::min(f.half_along, f.half_across), reach = std::max(f.half_along, f.half_across) - r;
      const math::Vec2 axis = f.half_along >= f.half_across ? math::Vec2{1, 0} : math::Vec2{0, 1};
      const int count = reach > 1e-9 ? std::max(1, static_cast<int>(std::ceil(reach / r))) : 0;
      for (int k = -count; k <= count; ++k) {
        const double t = count > 0 ? reach * k / count : 0.0;
        const auto p = frame.plan(f.centre.x + axis.x * t, f.centre.y + axis.y * t);
        Obstacle o; o.x = p.x; o.z = p.y; o.radius = r; o.bottom = b.floor_meters + f.bottom; o.top = b.floor_meters + f.bottom + f.height;
        out.push_back(o);
      }
    }
  }
  return out;
}

BuildingSet::Leaf BuildingSet::door_leaf(std::size_t i) const {
  const auto& b = buildings_[i];
  const Frame frame = frame_of(b);
  const DoorSize door = door_size(b.kind);
  const double hd = 0.5 * b.spec.depth_meters;
  // Hinged on the right jamb (looking in from the road), swinging inward.
  const double angle = std::clamp(doors_[i].open, 0.0, 1.0) * 0.5 * kPi;
  const math::Vec2 along = (b.along * -std::cos(angle)) + (b.facing * -std::sin(angle));
  return Leaf{frame.plan(0.5 * door.width, hd - 0.5 * kWall), along, door.width - 0.02, door.height, b.floor_meters};
}

bool BuildingSet::toggle_door(std::size_t building) {
  if (building >= buildings_.size()) throw std::out_of_range("building index out of range");
  if (!building_enterable(buildings_[building].kind)) return false;
  auto& d = doors_[building];
  d.opening = !(d.opening || d.open >= 1.0);
  ++door_version_;
  return true;
}

void BuildingSet::advance(double dt) {
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("door time step must be finite and non-negative");
  for (auto& d : doors_) {
    const double target = d.opening ? 1.0 : 0.0;
    if (d.open == target) continue;
    const double step = dt / kDoorSwingSeconds;
    d.open = d.opening ? std::min(1.0, d.open + step) : std::max(0.0, d.open - step);
    ++door_version_;
  }
}

std::vector<Obstacle> BuildingSet::door_obstacles() const {
  std::vector<Obstacle> out;
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    if (!building_enterable(buildings_[i].kind)) continue;
    const Leaf leaf = door_leaf(i);
    post_chain(out, leaf.hinge, leaf.hinge + leaf.along * leaf.width, kDoorPostRadius, leaf.bottom - 0.1, leaf.bottom + leaf.height);
  }
  return out;
}

std::vector<MeshPart> BuildingSet::build_door_meshes() const {
  std::vector<MeshPart> parts;
  MeshPart leaves; leaves.color = {0.36, 0.24, 0.13}; leaves.what = "door"; leaves.material = SurfaceMaterial::Timber;
  MeshPart handles; handles.color = {0.75, 0.72, 0.55}; handles.what = "handle"; handles.material = SurfaceMaterial::Metal;
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    if (!building_enterable(buildings_[i].kind)) continue;
    const Leaf leaf = door_leaf(i);
    const math::Vec3 along = plan3(leaf.along), across = kUp.cross(along).normalized();
    const math::Vec3 centre = lift(leaf.hinge + leaf.along * (0.5 * leaf.width), leaf.bottom + 0.01 + 0.5 * (leaf.height - 0.02));
    add_box(leaves, centre, along, kUp, across, {0.5 * leaf.width, 0.5 * (leaf.height - 0.02), 0.02});
    const math::Vec3 handle = lift(leaf.hinge + leaf.along * (leaf.width - 0.1), leaf.bottom + 1.0);
    for (const double side : {-1.0, 1.0}) add_box(handles, handle + across * (side * 0.05), along, kUp, across, {0.07, 0.015, 0.03});
  }
  if (!leaves.indices.empty()) { parts.push_back(std::move(leaves)); parts.push_back(std::move(handles)); }
  return parts;
}

std::vector<MeshPart> BuildingSet::build_meshes() const {
  std::vector<MeshPart> parts;
  const math::Vec3 concrete{0.62, 0.60, 0.57}, path_color{0.70, 0.68, 0.65};
  MeshPart paths; paths.color = path_color; paths.what = "path"; paths.material = SurfaceMaterial::Pavement;
  MeshPart foundations; foundations.color = concrete * 0.85; foundations.what = "foundation"; foundations.material = SurfaceMaterial::Concrete;
  std::vector<MeshPart> furniture;  // shared by every hollow building, a part per kind of piece
  for (const auto& b : buildings_) {
    const Frame frame = frame_of(b);
    const math::Vec3 u3 = plan3(b.along), v3 = plan3(b.facing);
    const double hw = 0.5 * b.spec.width_meters, hd = 0.5 * b.spec.depth_meters;
    const double walls_top = b.floor_meters + wall_height(b.spec);
    const auto kind = b.kind;
    // Materials by kind: the colour and the procedural surface (mesh_parts.hpp).
    math::Vec3 wall{0.55, 0.53, 0.50}, roof{0.62, 0.22, 0.15}, window{0.55, 0.70, 0.85}, door_color{0.30, 0.20, 0.12}, trim{0.80, 0.82, 0.85};
    SurfaceMaterial wall_material = SurfaceMaterial::Stone, roof_material = SurfaceMaterial::Tiles, door_material = SurfaceMaterial::Timber;
    const char* wall_what = "wall";
    switch (kind) {
      case BuildingKind::Bungalow: wall = {0.58, 0.42, 0.26}; roof = {0.74, 0.62, 0.34}; window = {0.10, 0.08, 0.06}; door_color = {0.36, 0.24, 0.13};
        wall_material = SurfaceMaterial::Planks; roof_material = SurfaceMaterial::Thatch; break;  // planks, thatch, open holes
      case BuildingKind::House: wall = {0.58, 0.56, 0.52}; roof = {0.64, 0.24, 0.16}; window = {0.55, 0.70, 0.85}; door_color = {0.30, 0.20, 0.12};
        wall_material = SurfaceMaterial::Stone; roof_material = SurfaceMaterial::Tiles; break;   // stone, red tiles, glass
      case BuildingKind::Hospital: wall = {0.92, 0.92, 0.90}; roof = {0.25, 0.26, 0.27}; window = {0.55, 0.70, 0.85}; door_color = {0.30, 0.42, 0.55};
        wall_material = SurfaceMaterial::Concrete; roof_material = SurfaceMaterial::Concrete; door_material = SurfaceMaterial::Window; break; // white concrete, dark grey flat roof
      case BuildingKind::Skyscraper: wall = {0.45, 0.62, 0.80}; roof = {0.95, 0.95, 0.95}; window = {0.45, 0.62, 0.80}; door_color = {0.25, 0.35, 0.48}; trim = {0.86, 0.88, 0.90}; wall_what = "glass";
        wall_material = SurfaceMaterial::Glass; roof_material = SurfaceMaterial::Concrete; door_material = SurfaceMaterial::Window; break;
      case BuildingKind::PoliceStation: wall = {0.84, 0.85, 0.88}; roof = {0.22, 0.23, 0.25}; window = {0.55, 0.70, 0.85}; door_color = {0.15, 0.22, 0.45}; trim = {0.62, 0.64, 0.66};
        wall_material = SurfaceMaterial::Concrete; roof_material = SurfaceMaterial::Concrete; door_material = SurfaceMaterial::Metal; break;  // light walls, a blue band
      case BuildingKind::Terminal: wall = {0.93, 0.93, 0.92}; roof = {0.86, 0.86, 0.87}; window = {0.50, 0.66, 0.82}; door_color = {0.30, 0.42, 0.55}; trim = {0.80, 0.82, 0.85};
        wall_material = SurfaceMaterial::Concrete; roof_material = SurfaceMaterial::Concrete; door_material = SurfaceMaterial::Window; break;       // white with glass bands
      case BuildingKind::ControlTower: wall = {0.70, 0.70, 0.68}; roof = {0.30, 0.31, 0.33}; window = {0.22, 0.32, 0.42}; door_color = {0.30, 0.32, 0.35}; trim = {0.62, 0.64, 0.66};
        wall_material = SurfaceMaterial::Concrete; roof_material = SurfaceMaterial::Concrete; door_material = SurfaceMaterial::Metal; break;   // a concrete shaft, a dark glazed cab
      case BuildingKind::Hangar: wall = {0.60, 0.62, 0.64}; roof = {0.52, 0.54, 0.56}; window = {0.45, 0.55, 0.65}; door_color = {0.40, 0.42, 0.45}; trim = {0.55, 0.57, 0.59}; wall_what = "metal";
        wall_material = SurfaceMaterial::Metal; roof_material = SurfaceMaterial::Metal; door_material = SurfaceMaterial::Metal; break;  // grey metal
    }
    const bool hollow = building_enterable(kind);
    const DoorSize door = door_size(kind);
    // Foundation: from below the lowest corner to the floor slab (a hollow
    // building's timber floor lies on it), a little wider than the walls.
    const double foundation_top = hollow ? b.floor_meters - 0.1 : b.floor_meters;
    add_box(foundations, frame.at(0, 0, 0.5 * (b.ground_min_meters - kFoundationDepth + foundation_top)), u3, kUp, v3,
            {hw + 0.1, 0.5 * (foundation_top - (b.ground_min_meters - kFoundationDepth)), hd + 0.1});
    // The windows: a row per storey on every face (computed here, drawn below
    // as panes proud of a solid wall or left as holes through a hollow one).
    struct Face { math::Vec3 normal; double length; bool front; };
    const Face faces[4] = {{v3, b.spec.width_meters, true}, {v3 * -1.0, b.spec.width_meters, false}, {u3, b.spec.depth_meters, false}, {u3 * -1.0, b.spec.depth_meters, false}};
    const auto face_centre = [&](const Face& face) {
      return face.normal.dot(v3) != 0 ? frame.at(0, face.normal.dot(v3) > 0 ? hd : -hd, 0) : frame.at(face.normal.dot(u3) > 0 ? hw : -hw, 0, 0);
    };
    struct Pane { int face; double along, y, half_w, half_h; };  // along the face from its centre (looking at it), at height y
    std::vector<Pane> panes;
    if (kind != BuildingKind::Skyscraper && kind != BuildingKind::Terminal && kind != BuildingKind::Hangar) {
      const double ww = kind == BuildingKind::Hospital ? 1.6 : kind == BuildingKind::Bungalow ? 1.0 : kind == BuildingKind::ControlTower ? 0.8 : kind == BuildingKind::PoliceStation ? 1.3 : 1.1;
      const double wh = kind == BuildingKind::Hospital ? 1.8 : kind == BuildingKind::Bungalow ? 1.0 : kind == BuildingKind::ControlTower ? 1.0 : kind == BuildingKind::PoliceStation ? 1.4 : 1.3;
      const double spacing = kind == BuildingKind::Hospital ? 3.2 : kind == BuildingKind::PoliceStation ? 3.0 : 2.8;
      for (int f = 0; f < 4; ++f) {
        const Face& face = faces[f];
        const int columns = std::max(1, static_cast<int>(std::floor((face.length - 1.6) / spacing)));
        const double span = (columns - 1) * spacing;
        for (std::uint32_t storey = 0; storey < b.spec.storeys; ++storey) {
          const double y = b.floor_meters + (storey + 0.5) * b.spec.storey_height_meters + 0.15;
          for (int c = 0; c < columns; ++c) {
            const double a = -0.5 * span + c * spacing;
            if (face.front && storey == 0 && std::abs(a) < 0.5 * door.width + 0.5 * ww + 0.4) continue;  // the door is here
            panes.push_back({f, a, y, 0.5 * ww, 0.5 * wh});
          }
        }
      }
    }
    // The body: one block, or four walls pierced by the doorway and the windows.
    MeshPart body; body.color = wall; body.what = wall_what; body.material = wall_material;
    if (!hollow) add_box(body, frame.at(0, 0, 0.5 * (b.floor_meters + walls_top)), u3, kUp, v3, {hw, 0.5 * (walls_top - b.floor_meters), hd});
    else {
      // A wall runs along its face's `across` direction (looking at it from
      // outside, left to right) between s0 and s1, kWall thick inside the
      // face; the pieces between the openings are full height, an opening
      // has a piece below and one above it.
      struct Hole { double s, half_w, y0, y1; };
      for (int f = 0; f < 4; ++f) {
        const Face& face = faces[f];
        const math::Vec3 across = kUp.cross(face.normal).normalized();
        const math::Vec3 fc = face_centre(face) - face.normal * (0.5 * kWall);  // the wall's centre plane
        const double half_len = face.front || f == 1 ? hw : hd - kWall;   // the side walls fit between the front and back ones
        std::vector<Hole> holes;
        if (face.front) holes.push_back({0.0, 0.5 * door.width + kDoorwayClearance, b.floor_meters, b.floor_meters + door.height + 0.05});
        for (const auto& pane : panes)
          if (pane.face == f) holes.push_back({pane.along, pane.half_w, pane.y - pane.half_h, pane.y + pane.half_h});
        std::sort(holes.begin(), holes.end(), [](const Hole& x, const Hole& y) { return x.s < y.s; });
        const auto piece = [&](double s0, double s1, double y0, double y1) {
          if (s1 - s0 < 1e-6 || y1 - y0 < 1e-6) return;
          add_box(body, fc + across * (0.5 * (s0 + s1)) + kUp * (0.5 * (y0 + y1) - fc.y), across, kUp, face.normal, {0.5 * (s1 - s0), 0.5 * (y1 - y0), 0.5 * kWall});
        };
        double s = -half_len;
        for (const auto& hole : holes) {
          const double h0 = std::max(s, hole.s - hole.half_w), h1 = std::min(half_len, hole.s + hole.half_w);
          piece(s, h0, b.floor_meters, walls_top);
          piece(h0, h1, b.floor_meters, hole.y0);
          piece(h0, h1, hole.y1, walls_top);
          s = h1;
        }
        piece(s, half_len, b.floor_meters, walls_top);
      }
      // The floor and the ceiling.
      MeshPart floor_part; floor_part.color = {0.62, 0.46, 0.28}; floor_part.what = "floor"; floor_part.material = SurfaceMaterial::Planks;
      add_box(floor_part, frame.at(0, 0, b.floor_meters - 0.05), u3, kUp, v3, {hw - kWall + 0.02, 0.05, hd - kWall + 0.02});
      parts.push_back(std::move(floor_part));
      MeshPart ceiling; ceiling.color = {0.88, 0.86, 0.80}; ceiling.what = "ceiling"; ceiling.material = SurfaceMaterial::Plain;
      add_box(ceiling, frame.at(0, 0, walls_top - 0.05), u3, kUp, v3, {hw - kWall + 0.02, 0.05, hd - kWall + 0.02});
      parts.push_back(std::move(ceiling));
      // The furniture, a part per kind of piece.
      for (const auto& f : furnish(b.spec)) {
        if (!f.drawn) continue;
        auto it = std::find_if(furniture.begin(), furniture.end(), [&](const MeshPart& part) { return std::string_view(part.what) == f.what; });
        if (it == furniture.end()) {
          MeshPart part; part.color = f.color; part.what = f.what; part.material = f.material;
          furniture.push_back(std::move(part));
          it = furniture.end() - 1;
        }
        add_box(*it, frame.at(f.centre.x, f.centre.y, b.floor_meters + f.bottom + 0.5 * f.height), u3, kUp, v3, {f.half_along, 0.5 * f.height, f.half_across});
      }
    }
    if (kind == BuildingKind::Bungalow) {
      // Masonry: a course of fieldstone the timber walls stand on, and a brick chimney.
      MeshPart plinth; plinth.color = {0.62, 0.60, 0.56}; plinth.what = "masonry"; plinth.material = SurfaceMaterial::Stone;
      if (!hollow) add_box(plinth, frame.at(0, 0, b.floor_meters + 0.35), u3, kUp, v3, {hw + 0.06, 0.35, hd + 0.06});
      else  // a skirt round the walls (down to the foundation), not a block through the rooms
        for (const double side : {-1.0, 1.0}) {
          add_box(plinth, frame.at(0, side * (hd + 0.01), b.floor_meters + 0.3), u3, kUp, v3, {hw + 0.06, 0.4, 0.05});
          add_box(plinth, frame.at(side * (hw + 0.01), 0, b.floor_meters + 0.3), u3, kUp, v3, {0.05, 0.4, hd + 0.06});
        }
      parts.push_back(std::move(plinth));
      MeshPart chimney; chimney.color = {0.70, 0.42, 0.32}; chimney.what = "chimney"; chimney.material = SurfaceMaterial::Brick;
      const double ridge = walls_top + (hd + roof_overhang(kind)) * std::tan(roof_pitch(kind));
      add_box(chimney, frame.at(-hw * 0.55, -hd * 0.3, ridge - 0.4), u3, kUp, v3, {0.3, 1.0, 0.3});
      parts.push_back(std::move(chimney));
    }
    // Roof.
    MeshPart roof_part; roof_part.color = roof; roof_part.what = "roof"; roof_part.material = roof_material;
    MeshPart trim_part; trim_part.color = trim; trim_part.what = "trim"; trim_part.material = SurfaceMaterial::Concrete;
    if (gabled(kind)) {
      const double pitch = roof_pitch(kind), over = roof_overhang(kind), thick = 0.25;
      const double run = hd + over, rise = run * std::tan(pitch), slope_len = run / std::cos(pitch);
      const double eave_y = walls_top - over * std::tan(pitch) + 0.05;
      for (const double side : {-1.0, 1.0}) {
        const math::Vec3 toward_ridge = (v3 * (-side) * std::cos(pitch) + kUp * std::sin(pitch)).normalized();  // up the slope
        const math::Vec3 normal = (v3 * side * std::sin(pitch) + kUp * std::cos(pitch)).normalized();
        const math::Vec3 centre = frame.at(0, side * 0.5 * run, eave_y + 0.5 * rise) + normal * (0.5 * thick);
        add_box(roof_part, centre, u3, normal, toward_ridge, {hw + over, 0.5 * thick, 0.5 * slope_len});
      }
      // Gable ends in the wall's material.
      const double ridge_y = eave_y + rise;
      for (const double side : {-1.0, 1.0}) {
        const math::Vec3 n = u3 * side;
        const auto p0 = frame.at(side * hw, -hd, walls_top), p1 = frame.at(side * hw, hd, walls_top), p2 = frame.at(side * hw, 0, ridge_y);
        add_triangle(body, p0, side > 0 ? p1 : p2, side > 0 ? p2 : p1, n);
      }
      if (kind == BuildingKind::House) {
        // A chimney through the back slope.
        MeshPart chimney; chimney.color = {0.62, 0.34, 0.26}; chimney.what = "chimney"; chimney.material = SurfaceMaterial::Brick;
        add_box(chimney, frame.at(hw * 0.5, -hd * 0.45, ridge_y - 0.3), u3, kUp, v3, {0.35, 0.9, 0.35});
        parts.push_back(std::move(chimney));
      }
    } else {
      // A flat slab with a parapet; the hospital's is dark, the skyscraper's white with a plant room.
      const double slab = 0.3, over = roof_overhang(kind);
      add_box(roof_part, frame.at(0, 0, walls_top + 0.5 * slab), u3, kUp, v3, {hw + over, 0.5 * slab, hd + over});
      const double parapet_h = 0.6, parapet_t = 0.2;
      for (const double side : {-1.0, 1.0}) {
        add_box(roof_part, frame.at(side * (hw + over - 0.5 * parapet_t), 0, walls_top + slab + 0.5 * parapet_h), u3, kUp, v3, {0.5 * parapet_t, 0.5 * parapet_h, hd + over});
        add_box(roof_part, frame.at(0, side * (hd + over - 0.5 * parapet_t), walls_top + slab + 0.5 * parapet_h), u3, kUp, v3, {hw + over, 0.5 * parapet_h, 0.5 * parapet_t});
      }
      if (kind == BuildingKind::Skyscraper) add_box(roof_part, frame.at(0, -hd * 0.2, walls_top + slab + 2.0), u3, kUp, v3, {4.0, 2.0, 3.0});  // the plant room
      if (kind == BuildingKind::PoliceStation) {
        // A mast with a blue beacon.
        add_box(trim_part, frame.at(hw * 0.6, -hd * 0.5, walls_top + slab + 1.4), u3, kUp, v3, {0.08, 1.4, 0.08});
        MeshPart beacon; beacon.color = {0.25, 0.45, 1.0}; beacon.what = "beacon"; beacon.material = SurfaceMaterial::Glass;
        add_box(beacon, frame.at(hw * 0.6, -hd * 0.5, walls_top + slab + 3.0), u3, kUp, v3, {0.25, 0.25, 0.25});
        parts.push_back(std::move(beacon));
      }
      if (kind == BuildingKind::ControlTower) {
        // The cab: a wider glazed box on the shaft under its own roof, and an antenna.
        MeshPart cab; cab.color = window; cab.what = "cab"; cab.material = SurfaceMaterial::Window;
        add_box(cab, frame.at(0, 0, walls_top + slab + 1.7), u3, kUp, v3, {hw + 2.0, 1.5, hd + 2.0});
        parts.push_back(std::move(cab));
        add_box(roof_part, frame.at(0, 0, walls_top + slab + 3.4), u3, kUp, v3, {hw + 2.3, 0.2, hd + 2.3});
        add_box(trim_part, frame.at(0, 0, walls_top + slab + 4.3), u3, kUp, v3, {0.06, 0.7, 0.06});
      }
    }
    // Windows: glass (or open holes) a little proud of the wall; a hollow wall has real holes.
    MeshPart windows; windows.color = window; windows.what = kind == BuildingKind::Bungalow ? "opening" : "window";
    windows.material = kind == BuildingKind::Bungalow ? SurfaceMaterial::Plain : SurfaceMaterial::Window;
    if (kind == BuildingKind::Terminal) {
      // Continuous glass bands along every face, one per storey, parted at the doors.
      for (const auto& face : faces) {
        const math::Vec3 across = kUp.cross(face.normal).normalized();
        const math::Vec3 fc = face_centre(face);
        for (std::uint32_t storey = 0; storey < b.spec.storeys; ++storey) {
          const double y = b.floor_meters + (storey + 0.55) * b.spec.storey_height_meters;
          if (face.front && storey == 0) {
            const double side_len = 0.5 * (face.length - 2.0) - 0.5 * door.width - 1.0;
            for (const double side : {-1.0, 1.0})
              add_box(windows, math::Vec3{fc.x, y, fc.z} + across * (side * (0.5 * door.width + 1.0 + 0.5 * side_len)) + face.normal * 0.03, across, kUp, face.normal, {0.5 * side_len, 1.1, 0.02});
          } else add_box(windows, math::Vec3{fc.x, y, fc.z} + face.normal * 0.03, across, kUp, face.normal, {0.5 * (face.length - 2.0), 1.1, 0.02});
        }
      }
    } else if (kind == BuildingKind::PoliceStation) {
      // A blue band at the first-floor line, all round.
      MeshPart band; band.color = {0.10, 0.25, 0.75}; band.what = "band"; band.material = SurfaceMaterial::Paint;
      for (const auto& face : faces) {
        const math::Vec3 across = kUp.cross(face.normal).normalized();
        const math::Vec3 fc = face_centre(face);
        add_box(band, math::Vec3{fc.x, b.floor_meters + b.spec.storey_height_meters, fc.z} + face.normal * 0.04, across, kUp, face.normal, {0.5 * face.length + 0.04, 0.3, 0.03});
      }
      // The blue lamp over the door.
      add_box(band, frame.at(0, hd + 0.25, b.floor_meters + door.height + 0.5), u3, kUp, v3, {0.25, 0.25, 0.25});
      parts.push_back(std::move(band));
    }
    if (!hollow)
      for (const auto& pane : panes) {
        const Face& face = faces[pane.face];
        const math::Vec3 across = kUp.cross(face.normal).normalized();
        const math::Vec3 fc = face_centre(face);
        add_box(windows, math::Vec3{fc.x, pane.y, fc.z} + across * pane.along + face.normal * 0.03, across, kUp, face.normal, {pane.half_w, pane.half_h, 0.02});
      }
    if (kind == BuildingKind::Skyscraper) {
      // A glass facade: floor bands and mullions stand proud of the glass body.
      for (std::uint32_t storey = 0; storey <= b.spec.storeys; ++storey) {
        const double y = b.floor_meters + storey * b.spec.storey_height_meters;
        for (const double side : {-1.0, 1.0}) {
          add_box(trim_part, frame.at(0, side * (hd + 0.04), y), u3, kUp, v3, {hw + 0.08, 0.25, 0.04});
          add_box(trim_part, frame.at(side * (hw + 0.04), 0, y), u3, kUp, v3, {0.04, 0.25, hd + 0.08});
        }
      }
      const double mullion = 3.0;
      for (double a = -hw + mullion; a < hw - 0.5; a += mullion)
        for (const double side : {-1.0, 1.0}) {
          add_box(trim_part, frame.at(a, side * (hd + 0.04), 0.5 * (b.floor_meters + walls_top)), u3, kUp, v3, {0.06, 0.5 * (walls_top - b.floor_meters), 0.04});
          add_box(trim_part, frame.at(side * (hw + 0.04), a, 0.5 * (b.floor_meters + walls_top)), u3, kUp, v3, {0.04, 0.5 * (walls_top - b.floor_meters), 0.06});
        }
    }
    // The door, on the front wall facing the road, and what marks the entrance.
    MeshPart door_part; door_part.color = door_color; door_part.what = hollow ? "doorframe" : "door"; door_part.material = door_material;
    if (!hollow) add_box(door_part, frame.at(0, hd + 0.04, b.floor_meters + 0.5 * door.height), u3, kUp, v3, {0.5 * door.width, 0.5 * door.height, 0.04});
    else {
      // The door frame round the doorway (the leaf itself is build_door_meshes').
      const double jamb = 0.5 * door.width + kDoorwayClearance, jamb_w = 0.5 * (kDoorwayClearance + 0.01);  // the jambs fill the clearance to the leaf
      for (const double side : {-1.0, 1.0})
        add_box(door_part, frame.at(side * (jamb - jamb_w), hd - 0.5 * kWall, b.floor_meters + 0.5 * door.height), u3, kUp, v3, {jamb_w, 0.5 * door.height, 0.5 * kWall + 0.01});
      add_box(door_part, frame.at(0, hd - 0.5 * kWall, b.floor_meters + door.height + 0.02), u3, kUp, v3, {jamb, 0.03, 0.5 * kWall + 0.01});
    }
    if (kind == BuildingKind::Hospital || kind == BuildingKind::Skyscraper || kind == BuildingKind::PoliceStation || kind == BuildingKind::Terminal) {
      // A canopy over the entrance (the terminal's runs along most of its front).
      const double canopy_y = b.floor_meters + door.height + 0.6, reach = kind == BuildingKind::Hospital ? 3.0 : kind == BuildingKind::Terminal ? 5.0 : kind == BuildingKind::PoliceStation ? 2.0 : 3.5;
      const double canopy_half = kind == BuildingKind::Terminal ? hw - 4.0 : 0.5 * door.width + 1.5;
      add_box(trim_part, frame.at(0, hd + 0.5 * reach, canopy_y), u3, kUp, v3, {canopy_half, 0.12, 0.5 * reach});
      const double post_spacing = kind == BuildingKind::Terminal ? 8.0 : 2.0 * canopy_half - 0.6;
      for (double a = -canopy_half + 0.3; a <= canopy_half - 0.3 + 1e-9; a += post_spacing)
        add_box(trim_part, frame.at(a, hd + reach - 0.2, 0.5 * (b.floor_meters + canopy_y)), u3, kUp, v3, {0.1, 0.5 * (canopy_y - b.floor_meters), 0.1});
    }
    if (kind == BuildingKind::Hospital) {
      // The red cross: on the front above the entrance and lying on the roof.
      MeshPart cross; cross.color = {0.85, 0.08, 0.08}; cross.what = "cross"; cross.material = SurfaceMaterial::Paint;
      const double cy = walls_top - 0.5 * b.spec.storey_height_meters;
      add_box(cross, frame.at(0, hd + 0.08, cy), u3, kUp, v3, {1.8, 0.35, 0.06});
      add_box(cross, frame.at(0, hd + 0.08, cy), u3, kUp, v3, {0.35, 1.8, 0.06});
      const double roof_y = walls_top + 0.3 + 0.04;
      add_box(cross, frame.at(0, 0, roof_y), u3, kUp, v3, {5.0, 0.04, 1.0});
      add_box(cross, frame.at(0, 0, roof_y), u3, kUp, v3, {1.0, 0.04, 5.0});
      parts.push_back(std::move(cross));
    }
    // The footpath from the door to the carriageway, following the ground.
    {
      const math::Vec2 from = b.door, to = b.path_end;
      const double len = (to - from).length();
      const math::Vec2 dir = (to - from) / std::max(len, 1e-9), right{-dir.y, dir.x};
      const int n = std::max(1, static_cast<int>(std::ceil(len / 1.0)));
      const double half = 0.5 * b.spec.path_width_meters;
      std::vector<math::Vec3> left(n + 1), right_side(n + 1);
      for (int k = 0; k <= n; ++k) {
        const auto c = from + dir * (len * k / n);
        for (const double s : {-1.0, 1.0}) {
          const auto p = c + right * (s * half);
          const auto h = ground_(p.x, p.y);
          const double y = path_height_at(b, p, h ? *h : ground_(c.x, c.y).value_or(b.ground_max_meters), style_.surface_lift_meters);
          (s < 0 ? left : right_side)[k] = lift(p, y);
        }
      }
      for (int k = 0; k < n; ++k) add_quad(paths, left[k], right_side[k], right_side[k + 1], left[k + 1]);
      // The ramp's sides and end, where it stands over the ground.
      for (int k = 0; k < n; ++k)
        for (const auto* side : {&left, &right_side}) {
          const auto& p0 = (*side)[k]; const auto& p1 = (*side)[k + 1];
          const double g0 = ground_(p0.x, p0.z).value_or(p0.y) + style_.surface_lift_meters - 0.3, g1 = ground_(p1.x, p1.z).value_or(p1.y) + style_.surface_lift_meters - 0.3;
          if (p0.y - g0 < 0.06 && p1.y - g1 < 0.06) continue;
          const math::Vec3 normal = plan3(right * (side == &left ? -1.0 : 1.0));
          add_quad(foundations, p0, p1, {p1.x, g1, p1.z}, {p0.x, g0, p0.z}, normal);
        }
    }
    parts.push_back(std::move(body));
    parts.push_back(std::move(roof_part));
    if (!windows.indices.empty()) parts.push_back(std::move(windows));
    if (!trim_part.indices.empty()) parts.push_back(std::move(trim_part));
    parts.push_back(std::move(door_part));
  }
  if (!paths.indices.empty()) parts.push_back(std::move(paths));
  if (!foundations.indices.empty()) parts.push_back(std::move(foundations));
  for (auto& part : furniture) parts.push_back(std::move(part));
  return parts;
}

std::size_t clear_vegetation_for_buildings(VegetationPlacement& placement, const BuildingSet& buildings, double margin) {
  if (!within(margin, 0, 100)) throw std::invalid_argument("vegetation margin must be 0..100 m");
  if (buildings.empty()) return 0;
  const auto on_plot = [&](float px, float pz) {
    const math::Vec2 p{px, pz};
    for (const auto& b : buildings.buildings()) {
      const auto [a, bb] = frame_of(b).local(p);
      const double hw = 0.5 * b.spec.width_meters, hd = 0.5 * b.spec.depth_meters;
      if (std::abs(a) <= hw + margin && std::abs(bb) <= hd + margin) return true;
      if (std::abs(a) <= 0.5 * b.spec.path_width_meters + margin && bb > hd && bb <= hd + b.spec.setback_meters + margin) return true;
    }
    return false;
  };
  VegetationPlacement out;
  out.chunk_size_meters = placement.chunk_size_meters;
  std::size_t removed = 0;
  for (const auto& c : placement.chunks) {
    VegetationChunk kept = c;
    kept.tree_first = static_cast<std::uint32_t>(out.trees.size());
    kept.grass_first = static_cast<std::uint32_t>(out.grass.size());
    kept.tree_count = kept.grass_count = 0;
    for (std::uint32_t k = 0; k < c.tree_count; ++k) {
      const auto& t = placement.trees[c.tree_first + k];
      if (on_plot(t.position[0], t.position[2])) { ++removed; continue; }
      out.trees.push_back(t); ++kept.tree_count; out.trees_per_kind[t.kind > 0.5f ? 1 : 0]++;
    }
    for (std::uint32_t k = 0; k < c.grass_count; ++k) {
      const auto& gr = placement.grass[c.grass_first + k];
      if (on_plot(gr.position[0], gr.position[2])) { ++removed; continue; }
      out.grass.push_back(gr); ++kept.grass_count;
    }
    if (kept.tree_count || kept.grass_count) out.chunks.push_back(kept);
  }
  placement = std::move(out);
  return removed;
}

}  // namespace terraflux::graphics
