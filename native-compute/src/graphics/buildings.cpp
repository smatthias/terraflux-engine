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
constexpr double kRubbleHeight = 0.45;     // a collapsed building's heap over its floor (stepped onto)
constexpr double kFlameRamp = 2.0;         // seconds for a lit element's flames to reach full strength

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

bool building_enterable(BuildingKind kind) { return kind == BuildingKind::Bungalow || kind == BuildingKind::House || kind == BuildingKind::Skyscraper; }

namespace {
// The house's plan: the stair up the left wall of the living room, the
// partition behind the living room, the one between the kitchen and the
// bathroom (local, from the spec).
struct HouseLayout {
  double iw, id;              // the rooms' half extents
  double stair_a0, stair_a1;  // the stair's strip along the left wall
  double stair_foot_b, stair_head_b;
  double back_b;              // the front face of the partition behind the living room
  double split_a;             // the centre line of the partition between the kitchen and the bathroom
  double upper;               // the upper floor's height
};
HouseLayout house_layout(const BuildingSpec& spec) {
  HouseLayout h;
  h.iw = 0.5 * spec.width_meters - kWall; h.id = 0.5 * spec.depth_meters - kWall;
  h.upper = spec.storey_height_meters;
  h.stair_a0 = -h.iw; h.stair_a1 = -h.iw + 1.0;
  h.stair_foot_b = h.id - 0.55;
  h.stair_head_b = h.stair_foot_b - 1.4 * h.upper;  // a 35-degree flight
  h.back_b = h.stair_head_b;
  h.split_a = std::min(1.0, h.iw - 3.0);
  return h;
}
// The skyscraper's plan: a concrete core in the middle — the stairwell (two
// switchback flights a storey, a half landing between them) on its left,
// the lift shaft on its right — halls in front of and behind the core,
// corridors beside it, and an apartment in each corner of every floor above
// the ground floor's gallery.
struct TowerLayout {
  double iw, id, storey;
  double core_a0 = -3.0, core_a1 = 3.0, core_b0 = -4.0, core_b1 = 4.0;   // the core's outer faces
  double stair_a0 = -3.0, stair_a1 = 0.0;                                  // the stairwell: two flights side by side ...
  double flight_w = 1.4, spine_a = -1.5;                                   // ... each this wide, a thin wall between them
  double flight_foot_b = 3.0, flight_head_b = -1.6, landing_b0 = -3.6;     // a flight runs from the door side to the back, the landing behind; half a metre of floor at the door side joins the strips
  double shaft_a0 = 0.5, shaft_a1 = 3.0, shaft_b0 = 1.0, shaft_b1 = 4.0;   // the lift shaft, its door on the front face
  double band_a = 4.5, band_b = 5.5;                                       // the halls and corridors round the core reach this far
  double apt_w, apt_d;                                                     // an apartment's size (a, b)
};
TowerLayout tower_layout(const BuildingSpec& spec) {
  TowerLayout t;
  t.iw = 0.5 * spec.width_meters - kWall; t.id = 0.5 * spec.depth_meters - kWall; t.storey = spec.storey_height_meters;
  t.apt_w = t.iw - t.band_a; t.apt_d = t.id - t.band_b;
  return t;
}
// An apartment's corner: the signs of a and b (which quadrant it is in).
struct Quadrant { double sa, sb; };
constexpr Quadrant kQuadrants[4] = {{-1, 1}, {1, 1}, {-1, -1}, {1, -1}};  // front left, front right, back left, back right
// A frame for laying an apartment out: u runs from the corridor-side wall
// toward the exterior wall, v from the hall-side wall (the door's) toward
// the back, both from the apartment's inner corner; mirrored per quadrant.
struct AptFrame {
  double a_in, b_in;  // the inner corner (the corridor side)
  double sa, sb;      // u grows toward sa * a, v toward sb * b
  math::Vec2 plan(double u, double v) const { return {a_in + sa * u, b_in + sb * v}; }
};
AptFrame apt_frame(const TowerLayout& t, const Quadrant& q) { return {q.sa * (t.band_a + kWall), q.sb * (t.band_b + kWall), q.sa, q.sb}; }
// A wall segment of an apartment in its frame.
InteriorWall apt_wall(const AptFrame& f, double u0, double v0, double u1, double v1, double bottom, double top, std::vector<InteriorOpening> openings = {}) {
  InteriorWall w; w.from = f.plan(u0, v0); w.to = f.plan(u1, v1); w.bottom = bottom; w.top = top; w.openings = std::move(openings);
  return w;
}
}  // namespace

InteriorPlan interior_plan(const BuildingSpec& spec) {
  validate_building_spec(spec);
  InteriorPlan plan;
  if (!building_enterable(spec.kind)) return plan;
  const double iw = 0.5 * spec.width_meters - kWall, id = 0.5 * spec.depth_meters - kWall;
  if (spec.kind == BuildingKind::Bungalow || (spec.kind == BuildingKind::House && spec.storeys < 2)) {
    plan.rooms.push_back({"room", -iw, iw, -id, id, 0});
    return plan;
  }
  if (spec.kind == BuildingKind::House) {
    const HouseLayout h = house_layout(spec);
    const DoorSize doorway{1.0, 2.1};
    plan.floors = 2; plan.floor_height = h.upper;
    plan.rooms.push_back({"living room", h.stair_a1, iw, h.back_b, id, 0});
    plan.rooms.push_back({"stairs", h.stair_a0, h.stair_a1, h.stair_head_b, id, 0});
    plan.rooms.push_back({"kitchen", -iw, h.split_a - 0.5 * kWall, -id, h.back_b - kWall, 0});
    plan.rooms.push_back({"bathroom", h.split_a + 0.5 * kWall, iw, -id, h.back_b - kWall, 0});
    plan.rooms.push_back({"bedroom", -iw, iw, -id, id, 1});
    // The partition behind the living room, with the kitchen's and the bathroom's doorways; its centre line is half a wall behind the face.
    InteriorWall back;
    back.from = {-iw, h.back_b - 0.5 * kWall}; back.to = {iw, h.back_b - 0.5 * kWall}; back.bottom = 0.0; back.top = h.upper - plan.slab;
    back.openings.push_back({(h.stair_a1 + 1.5) - (-iw), doorway.width, doorway.height, 0.0});                 // into the kitchen, a metre past the stair
    back.openings.push_back({(0.5 * (h.split_a + 0.5 * kWall + iw)) - (-iw), doorway.width, doorway.height, 0.0});  // into the bathroom, mid-wall
    plan.walls.push_back(std::move(back));
    InteriorWall split;
    split.from = {h.split_a, h.back_b - kWall}; split.to = {h.split_a, -id}; split.bottom = 0.0; split.top = h.upper - plan.slab;
    plan.walls.push_back(std::move(split));
    plan.flights.push_back(InteriorStair{{0.5 * (h.stair_a0 + h.stair_a1), h.stair_foot_b}, {0.5 * (h.stair_a0 + h.stair_a1), h.stair_head_b}, h.stair_a1 - h.stair_a0, h.upper, 0.0});
    plan.holes.push_back({h.stair_a0, h.stair_a1, h.stair_head_b, id, 1});  // the stairwell: the stair's strip from its head to the front wall
    // The handrail along the stair's open side runs on up as the banister round the stairwell.
    plan.rails.push_back({{h.stair_a1, id}, {h.stair_a1, h.stair_head_b}, -0.5, h.upper + 1.0});
    return plan;
  }
  // The skyscraper.
  const TowerLayout t = tower_layout(spec);
  plan.floors = static_cast<int>(spec.storeys); plan.floor_height = t.storey;
  const double top = plan.floors * t.storey;
  const DoorSize doorway{1.0, 2.1};
  // The core: concrete walls the full height. Its front face carries the
  // stairwell's and the lift's doors on every floor; a wall parts the
  // stairwell from the shaft; the thin spine wall stands between the flights.
  const auto concrete_wall = [&](math::Vec2 from, math::Vec2 to, std::vector<InteriorOpening> openings = {}, double thickness = kWall) {
    InteriorWall w; w.from = from; w.to = to; w.bottom = 0.0; w.top = top; w.openings = std::move(openings); w.thickness = thickness; w.material = SurfaceMaterial::Concrete;
    return w;
  };
  {
    std::vector<InteriorOpening> front_doors;
    const double stair_door_a = 0.5 * (t.spine_a + 0.5 * 0.1 + t.stair_a1), shaft_door_a = 0.5 * (t.shaft_a0 + t.shaft_a1);
    for (int k = 0; k < plan.floors; ++k) {
      front_doors.push_back({stair_door_a - t.core_a0, doorway.width, doorway.height, plan.level(k)});
      front_doors.push_back({shaft_door_a - t.core_a0, doorway.width, doorway.height, plan.level(k)});
    }
    plan.walls.push_back(concrete_wall({t.core_a0, t.core_b1 - 0.5 * kWall}, {t.core_a1, t.core_b1 - 0.5 * kWall}, std::move(front_doors)));  // the front face
    plan.walls.push_back(concrete_wall({t.core_a0, t.core_b0 + 0.5 * kWall}, {t.core_a1, t.core_b0 + 0.5 * kWall}));  // the back face
    plan.walls.push_back(concrete_wall({t.core_a0 + 0.5 * kWall, t.core_b0 + kWall}, {t.core_a0 + 0.5 * kWall, t.core_b1 - kWall}));  // the left face
    plan.walls.push_back(concrete_wall({t.core_a1 - 0.5 * kWall, t.core_b0 + kWall}, {t.core_a1 - 0.5 * kWall, t.core_b1 - kWall}));  // the right face
    plan.walls.push_back(concrete_wall({0.5 * (t.stair_a1 + t.shaft_a0), t.core_b0 + kWall}, {0.5 * (t.stair_a1 + t.shaft_a0), t.core_b1 - kWall}, {}, t.shaft_a0 - t.stair_a1));  // between the stairwell and the shaft
    plan.walls.push_back(concrete_wall({t.shaft_a0, t.shaft_b0 - 0.5 * kWall}, {t.shaft_a1 - kWall, t.shaft_b0 - 0.5 * kWall}));  // the shaft's back
    // The spine between the flights, from the landing to 0.6 m short of the
    // door side: there the two flights are within a step of each other and
    // the walker crosses between them to the door.
    plan.walls.push_back(concrete_wall({t.spine_a, t.flight_head_b}, {t.spine_a, t.flight_foot_b - 0.6}, {}, 0.1));
    plan.lift = InteriorLift{t.shaft_a0 + kWall, t.shaft_a1 - kWall, t.shaft_b0, t.shaft_b1 - kWall, shaft_door_a, doorway.width, doorway.height, 1.5};
  }
  // The flights: up the left strip from the door side to the back, the landing, back up the right strip to the next floor.
  const double flight_a_left = 0.5 * (t.stair_a0 + t.spine_a - 0.05), flight_a_right = 0.5 * (t.spine_a + 0.05 + t.stair_a1);
  for (int k = 0; k + 1 < plan.floors; ++k) {
    const double base = plan.level(k), half = 0.5 * t.storey;
    plan.flights.push_back(InteriorStair{{flight_a_left, t.flight_foot_b}, {flight_a_left, t.flight_head_b}, t.flight_w, half, base});
    plan.landings.push_back({t.stair_a0, t.stair_a1, t.landing_b0, t.flight_head_b, base + half});
    plan.flights.push_back(InteriorStair{{flight_a_right, t.flight_head_b}, {flight_a_right, t.flight_foot_b}, t.flight_w, half, base + half});
    plan.holes.push_back({t.stair_a0, t.stair_a1, t.landing_b0, t.flight_foot_b, k + 1});  // the stairwell through the floor above
    plan.holes.push_back({t.shaft_a0, t.shaft_a1, t.shaft_b0, t.shaft_b1, k + 1});         // the shaft
  }
  // The ground floor: the gallery round the core; every floor above: the
  // halls, the corridors, and the four apartments in the corners.
  plan.rooms.push_back({"stairwell", t.stair_a0, t.stair_a1, t.core_b0, t.core_b1, 0});
  plan.rooms.push_back({"lift", t.shaft_a0, t.shaft_a1, t.shaft_b0, t.shaft_b1, 0});
  plan.rooms.push_back({"gallery", -iw, iw, -id, id, 0});
  for (int k = 1; k < plan.floors; ++k) {
    plan.rooms.push_back({"stairwell", t.stair_a0, t.stair_a1, t.core_b0, t.core_b1, k});
    plan.rooms.push_back({"lift", t.shaft_a0, t.shaft_a1, t.shaft_b0, t.shaft_b1, k});
    plan.rooms.push_back({"hall", -t.band_a, t.band_a, t.core_b1, id, k});   // before the core (the apartments' doors open onto the side halls)
    plan.rooms.push_back({"hall", -t.band_a, t.band_a, -id, t.core_b0, k});  // behind it
    plan.rooms.push_back({"hall", -iw, t.core_a0, -t.band_b, t.band_b, k});  // beside it, left
    plan.rooms.push_back({"hall", t.core_a1, iw, -t.band_b, t.band_b, k});   // and right
    for (const auto& q : kQuadrants) {
      const AptFrame f = apt_frame(t, q);
      const double W = t.apt_w - kWall, D = t.apt_d - kWall;  // the rooms' extents (the apartment's own walls take kWall on its two inner sides)
      const auto room = [&](const char* name, double u0, double v0, double u1, double v1) {
        const math::Vec2 p0 = f.plan(u0, v0), p1 = f.plan(u1, v1);
        plan.rooms.push_back({name, std::min(p0.x, p1.x), std::max(p0.x, p1.x), std::min(p0.y, p1.y), std::max(p0.y, p1.y), k});
      };
      room("living room", 0, 0, 0.5 * W, 0.5 * D);
      room("kitchen", 0.5 * W, 0, W, 0.5 * D);
      room("bedroom", 0, 0.5 * D, 0.5 * W, D);
      room("bathroom", 0.5 * W, 0.5 * D, W, D);
      const double bottom = plan.level(k), ceiling = plan.level(k + 1) - plan.slab;
      // The apartment's walls to the hall (with its door) and to the corridor, a storey high; the partitions inside.
      plan.walls.push_back(apt_wall(f, -kWall, -0.5 * kWall, W, -0.5 * kWall, bottom, ceiling, {{kWall + 1.5, doorway.width, doorway.height, bottom}}));
      plan.walls.push_back(apt_wall(f, -0.5 * kWall, 0, -0.5 * kWall, D, bottom, ceiling));
      plan.walls.push_back(apt_wall(f, 0.5 * W, 0, 0.5 * W, D, bottom, ceiling, {{0.15 * D, doorway.width, doorway.height, bottom}, {0.75 * D, doorway.width, doorway.height, bottom}}));  // living room to kitchen, bedroom to bathroom
      plan.walls.push_back(apt_wall(f, 0, 0.5 * D, 0.5 * W, 0.5 * D, bottom, ceiling, {{0.4 * W, doorway.width, doorway.height, bottom}}));  // living room to bedroom
    }
  }
  return plan;
}

namespace {
// Lays furniture out in a room's own frame: u and v from an origin along
// two plan axes (each +a, -a, +b or -b), half sizes swapped when the axes
// are turned; the pieces are kept when they fit between the building's walls.
struct Placer {
  std::vector<Furnishing>& out;
  double iw, id, ceiling;
  math::Vec2 origin;          // local (a, b)
  math::Vec2 ua, va;          // unit axes in (a, b): u and v
  double lift = 0.0;          // the floor's height over the ground floor
  bool add(const char* what, double u, double v, double half_u, double half_v, double bottom, double height, math::Vec3 color,
           SurfaceMaterial material, bool solid, bool drawn = true) const {
    const math::Vec2 c = origin + ua * u + va * v;
    const double half_a = std::abs(ua.x) > 0.5 ? half_u : half_v, half_b = std::abs(ua.x) > 0.5 ? half_v : half_u;
    if (std::abs(c.x) + half_a > iw + 1e-9 || std::abs(c.y) + half_b > id + 1e-9 || lift + bottom + height > ceiling) return false;
    out.push_back(Furnishing{what, c, half_a, half_b, lift + bottom, height, color, material, solid, drawn});
    return true;
  }
};
const math::Vec3 kTimber{0.50, 0.34, 0.20}, kDarkTimber{0.42, 0.28, 0.16}, kPaleTimber{0.56, 0.40, 0.24}, kSteel{0.75, 0.76, 0.78}, kIron{0.25, 0.25, 0.27};
const math::Vec3 kStone{0.55, 0.55, 0.52}, kLinen{0.92, 0.92, 0.88}, kWool{0.30, 0.40, 0.65}, kPorcelain{0.94, 0.95, 0.95}, kFabric{0.30, 0.36, 0.48}, kBlack{0.06, 0.06, 0.07};
// A table with chairs round it (a kitchen or dining table), its long side along u; `chairs` 2 (one each long side) or 4.
void table_and_chairs(const Placer& p, double tu, double tv, int chairs = 4) {
  if (!p.add("table", tu, tv, 0.7, 0.45, 0.0, 0.76, kPaleTimber, SurfaceMaterial::Timber, true, false)) return;
  p.add("table", tu, tv, 0.7, 0.45, 0.72, 0.04, kPaleTimber, SurfaceMaterial::Timber, false);
  for (const double su : {-1.0, 1.0})
    for (const double sv : {-1.0, 1.0}) p.add("table", tu + su * 0.6, tv + sv * 0.35, 0.04, 0.04, 0.0, 0.72, kPaleTimber, SurfaceMaterial::Timber, false);
  for (const double sv : {-1.0, 1.0})
    for (const double su : (chairs >= 4 ? std::vector<double>{-1.0, 1.0} : std::vector<double>{0.0})) {
      const double cu = tu + su * 0.4, cv = tv + sv * 0.75;
      if (!p.add("chair", cu, cv, 0.21, 0.21, 0.0, 0.9, kTimber, SurfaceMaterial::Timber, true, false)) continue;
      p.add("chair", cu, cv, 0.21, 0.21, 0.42, 0.04, kTimber, SurfaceMaterial::Timber, false);
      for (const double lu : {-1.0, 1.0})
        for (const double lv : {-1.0, 1.0}) p.add("chair", cu + lu * 0.18, cv + lv * 0.18, 0.02, 0.02, 0.0, 0.42, kTimber, SurfaceMaterial::Timber, false);
      p.add("chair", cu, cv + sv * 0.19, 0.21, 0.02, 0.46, 0.44, kTimber, SurfaceMaterial::Timber, false);  // the backrest, away from the table
    }
}
// A cooking area along a wall at v (the wall behind it at smaller v) from u0: a run of
// cupboards under a worktop with a sink, a cupboard on the wall over it, a stove beside it.
void cooking_area(const Placer& p, double u0, double v) {
  if (p.add("cupboard", u0 + 1.5, v, 1.5, 0.3, 0.0, 0.85, kTimber, SurfaceMaterial::Timber, true)) {
    p.add("worktop", u0 + 1.5, v, 1.52, 0.32, 0.85, 0.05, kStone, SurfaceMaterial::Stone, false);
    p.add("sink", u0 + 0.7, v, 0.25, 0.2, 0.9, 0.1, kSteel, SurfaceMaterial::Metal, false);
    p.add("cupboard", u0 + 1.1, v - 0.12, 0.9, 0.18, 1.5, 0.7, kTimber, SurfaceMaterial::Timber, false);  // on the wall
  }
  p.add("stove", u0 + 3.4, v, 0.3, 0.3, 0.0, 0.9, kIron, SurfaceMaterial::Metal, true);
}
// A bed, its head toward smaller v (the wall behind it).
void bed(const Placer& p, double bu, double bv) {
  if (p.add("bed", bu, bv, 0.7, 1.0, 0.0, 0.35, kTimber, SurfaceMaterial::Timber, false)) {
    p.add("mattress", bu, bv, 0.66, 0.96, 0.35, 0.22, kLinen, SurfaceMaterial::Plain, true);
    p.add("blanket", bu, bv + 0.36, 0.66, 0.6, 0.57, 0.05, kWool, SurfaceMaterial::Plain, false);
    p.add("pillow", bu, bv - 0.7, 0.4, 0.22, 0.57, 0.12, kLinen, SurfaceMaterial::Plain, false);
  }
}
// A toilet (its cistern toward smaller v) and a washbasin against the wall at smaller v.
void bathroom_fixtures(const Placer& p, double tu, double tv, double su, double sv) {
  if (p.add("toilet", tu, tv, 0.2, 0.35, 0.0, 0.85, kPorcelain, SurfaceMaterial::Plain, true, false)) {
    p.add("toilet", tu, tv + 0.05, 0.2, 0.3, 0.0, 0.42, kPorcelain, SurfaceMaterial::Plain, false);   // the bowl
    p.add("toilet", tu, tv - 0.25, 0.2, 0.1, 0.42, 0.43, kPorcelain, SurfaceMaterial::Plain, false);  // the cistern
  }
  if (p.add("washbasin", su, sv, 0.28, 0.22, 0.0, 0.95, kPorcelain, SurfaceMaterial::Plain, true, false)) {
    p.add("washbasin", su, sv, 0.1, 0.1, 0.0, 0.8, kPorcelain, SurfaceMaterial::Plain, false);    // the pedestal
    p.add("washbasin", su, sv, 0.28, 0.22, 0.8, 0.15, kPorcelain, SurfaceMaterial::Plain, false);  // the basin
    p.add("tap", su, sv - 0.17, 0.02, 0.02, 0.95, 0.12, kSteel, SurfaceMaterial::Metal, false);
  }
}
// A television on its stand against the wall at larger u, the sofa facing it over a low table (all at v).
void living_room_set(const Placer& p, double tv_u, double v) {
  if (p.add("television", tv_u, v, 0.25, 0.6, 0.0, 1.15, kBlack, SurfaceMaterial::Plain, true, false)) {
    p.add("stand", tv_u, v, 0.25, 0.6, 0.0, 0.5, kTimber, SurfaceMaterial::Timber, false);
    p.add("television", tv_u + 0.02, v, 0.04, 0.55, 0.5, 0.65, kBlack, SurfaceMaterial::Plain, false);
  }
  const double cu = tv_u - 1.18;
  if (p.add("table", cu, v, 0.45, 0.3, 0.0, 0.55, kPaleTimber, SurfaceMaterial::Timber, true, false)) {
    p.add("table", cu, v, 0.45, 0.3, 0.5, 0.05, kPaleTimber, SurfaceMaterial::Timber, false);
    for (const double su : {-1.0, 1.0})
      for (const double sv : {-1.0, 1.0}) p.add("table", cu + su * 0.38, v + sv * 0.23, 0.03, 0.03, 0.0, 0.5, kPaleTimber, SurfaceMaterial::Timber, false);
  }
  const double fu = tv_u - 2.58;
  if (p.add("sofa", fu, v, 0.45, 1.1, 0.0, 0.9, kFabric, SurfaceMaterial::Plain, true, false)) {
    p.add("sofa", fu, v, 0.45, 1.0, 0.1, 0.35, kFabric, SurfaceMaterial::Plain, false);           // the seat
    p.add("sofa", fu - 0.35, v, 0.1, 1.0, 0.45, 0.45, kFabric, SurfaceMaterial::Plain, false);    // the backrest, away from the television
    for (const double sv : {-1.0, 1.0}) p.add("sofa", fu, v + sv * 1.0, 0.45, 0.1, 0.0, 0.6, kFabric, SurfaceMaterial::Plain, false);  // the armrests
  }
}
}  // namespace

std::vector<Furnishing> furnish(const BuildingSpec& spec) {
  validate_building_spec(spec);
  std::vector<Furnishing> out;
  if (!building_enterable(spec.kind)) return out;
  const double iw = 0.5 * spec.width_meters - kWall, id = 0.5 * spec.depth_meters - kWall;  // the rooms' half extents
  const double ceiling = spec.storeys * spec.storey_height_meters - 0.1;
  const Placer local{out, iw, id, ceiling, {0, 0}, {1, 0}, {0, 1}, 0.0};  // the building's own frame
  if (spec.kind == BuildingKind::House && spec.storeys >= 2) {
    const HouseLayout h = house_layout(spec);
    // The kitchen (back left): the cooking area along the back wall, a tall cabinet against the left wall, the table with four chairs.
    cooking_area(local, -iw + 0.25, -id + 0.32);
    local.add("cabinet", -iw + 0.32, -id + 1.3, 0.3, 0.45, 0.0, 2.0, kDarkTimber, SurfaceMaterial::Timber, true);  // between the cooking area and the window
    table_and_chairs(local, h.split_a - 1.9, 0.5 * (h.back_b - kWall - id) + 0.2);
    // The bathroom (back right): a toilet and a washbasin against the back wall, a cabinet against the right wall.
    bathroom_fixtures(local, iw - 0.75, -id + 0.4, h.split_a + 1.4, -id + 0.3);
    local.add("cabinet", iw - 0.22, 0.5 * (h.back_b - kWall - id), 0.2, 0.45, 0.0, 1.8, kTimber, SurfaceMaterial::Timber, true);
    // The living room (front): the television against the right wall, the sofa facing it over a low table.
    living_room_set(local, iw - 0.27, 0.5 * (h.back_b + id) + 0.7);
    // The bedroom upstairs: the bed against the back wall, the wardrobe in the front corner by the right wall.
    Placer upstairs = local; upstairs.lift = h.upper;
    bed(upstairs, iw - 2.75, -id + 1.05);
    upstairs.add("wardrobe", iw - 0.32, id - 0.95, 0.3, 0.9, 0.0, 2.1, kDarkTimber, SurfaceMaterial::Timber, true);
    return out;
  }
  if (spec.kind == BuildingKind::Skyscraper) {
    const TowerLayout t = tower_layout(spec);
    // The gallery: the reception desk inside the entrance, facing the door, and two sofas over low tables either side.
    local.add("desk", 0.0, id - 4.0, 1.8, 0.4, 0.0, 1.1, kDarkTimber, SurfaceMaterial::Timber, true);
    local.add("desk", 0.0, id - 4.0, 1.85, 0.45, 1.1, 0.05, kStone, SurfaceMaterial::Stone, false);  // its top
    local.add("chair", 0.0, id - 4.9, 0.25, 0.25, 0.0, 0.9, kBlack, SurfaceMaterial::Plain, true, false);
    local.add("chair", 0.0, id - 4.9, 0.25, 0.25, 0.45, 0.05, kBlack, SurfaceMaterial::Plain, false);
    local.add("chair", 0.0, id - 5.1, 0.25, 0.04, 0.5, 0.45, kBlack, SurfaceMaterial::Plain, false);
    for (const double side : {-1.0, 1.0}) {
      const Placer side_frame{out, iw, id, ceiling, {side * 7.0, id - 3.0}, {0, -1}, {side, 0}, 0.0};  // u toward the back, v outward
      living_room_set(side_frame, 3.0, 0.0);
    }
    // The apartments: the living room inside the door, the kitchen beside it, the bedroom behind, the bathroom in the far corner.
    const double W = t.apt_w - kWall, D = t.apt_d - kWall;
    for (int k = 1; k < static_cast<int>(spec.storeys); ++k)
      for (const auto& q : kQuadrants) {
        const AptFrame f = apt_frame(t, q);
        Placer p{out, iw, id, ceiling, {f.a_in, f.b_in}, {f.sa, 0}, {0, f.sb}, k * t.storey};
        // The living room: the television against the wall to the kitchen, the sofa before it, the door and the doorways clear.
        living_room_set(p, 0.5 * W - 0.27, 0.5 * D - 1.6);
        // The kitchen: the cooking area along the exterior wall, a table with two chairs.
        {
          const Placer kitchen{out, iw, id, ceiling, f.plan(W, 0.0), {0, f.sb}, {-f.sa, 0}, k * t.storey};  // u along the exterior wall from the hall side, v into the room
          cooking_area(kitchen, 0.3, 0.32);
          table_and_chairs(kitchen, 0.25 * D + 0.3, 2.6, 2);
        }
        // The bedroom: the bed against the corridor-side wall, the wardrobe against the back wall.
        {
          const Placer bedroom{out, iw, id, ceiling, f.plan(0.0, D), {0, -f.sb}, {f.sa, 0}, k * t.storey};  // u along the corridor wall from the back, v into the room
          bed(bedroom, 1.0, 0.35 + 1.0);
          bedroom.add("wardrobe", 0.25 * D + 1.2, 0.32, 0.9, 0.3, 0.0, 2.1, kDarkTimber, SurfaceMaterial::Timber, true);
        }
        // The bathroom: the toilet and the washbasin against the back wall, a cabinet beside them.
        {
          const Placer bathroom{out, iw, id, ceiling, f.plan(W, D), {-f.sa, 0}, {0, -f.sb}, k * t.storey};  // u along the back wall from the corner, v into the room
          bathroom_fixtures(bathroom, 0.6, 0.4, 1.6, 0.3);
          bathroom.add("cabinet", 2.6, 0.25, 0.4, 0.2, 0.0, 1.8, kTimber, SurfaceMaterial::Timber, true);
        }
      }
    return out;
  }
  // The bungalow: one room.
  cooking_area(local, -iw + 0.25, -id + 0.32);
  table_and_chairs(local, std::min(1.6, iw - 1.6), 0.2);
  // A wardrobe against the left wall, a bed against the right one.
  local.add("wardrobe", -iw + 0.32, 1.5, 0.3, 0.6, 0.0, 2.0, kDarkTimber, SurfaceMaterial::Timber, true);
  bed(local, iw - 0.72, -id + 1.65);
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
  if (!within(s.rubble_speed_factor, 0.1, 1) || !within(s.heat_reach_meters, 1, 200) || !within(s.spread_heat, 0, 1) || !within(s.wind_burn_factor, 0, 1) ||
      !within(s.rain_slowdown_mm_h, 1, 500))
    throw std::invalid_argument("rubble speed factor must be 0.1..1, heat reach 1..200 m, spread heat 0..1, wind burn factor 0..1 and rain slowdown 1..500 mm/h");
  if (!within(s.flood_current_mps, 0.1, 30) || !within(s.flood_depth_meters, 0, 20) || !within(s.flood_scour_seconds, 1, 86400))
    throw std::invalid_argument("flood current must be 0.1..30 m/s, depth 0..20 m and scour time 1..86400 s");
  if (!within(s.strike_radius_meters, 0, 200) || !within(s.strike_scorch, 0, 1) || !within(s.collapse_damage, 0, 1) || !within(s.roof_fall_damage, 0, 1))
    throw std::invalid_argument("strike radius must be 0..200 m, scorch 0..1, collapse and roof fall damage 0..1");
}

// ---------------------------------------------------------------------------
const char* building_element_name(BuildingElement element) {
  switch (element) {
    case BuildingElement::Walls: return "walls";
    case BuildingElement::Roof: return "roof";
    case BuildingElement::Windows: return "windows";
    case BuildingElement::Door: return "door";
    case BuildingElement::Interior: return "interior";
    case BuildingElement::Count: break;
  }
  throw std::invalid_argument("unknown building element");
}

std::optional<SurfaceMaterial> building_element_material(BuildingKind kind, BuildingElement element) {
  using M = SurfaceMaterial;
  (void)building_kind_name(kind);
  switch (element) {
    case BuildingElement::Walls:
      switch (kind) {
        case BuildingKind::Bungalow: return M::Planks;
        case BuildingKind::House: return M::Stone;
        case BuildingKind::Hangar: return M::Metal;
        default: return M::Concrete;  // the skyscraper stands on its concrete frame; its glass facade is its windows
      }
    case BuildingElement::Roof:
      switch (kind) {
        case BuildingKind::Bungalow: return M::Thatch;
        case BuildingKind::House: return M::Tiles;
        case BuildingKind::Hangar: return M::Metal;
        default: return M::Concrete;
      }
    case BuildingElement::Windows:
      switch (kind) {
        case BuildingKind::Bungalow: case BuildingKind::Hangar: return std::nullopt;  // open holes; none
        case BuildingKind::Skyscraper: return M::Glass;                              // the whole facade
        default: return M::Window;
      }
    case BuildingElement::Door:
      switch (kind) {
        case BuildingKind::Bungalow: case BuildingKind::House: return M::Timber;
        case BuildingKind::PoliceStation: case BuildingKind::ControlTower: case BuildingKind::Hangar: return M::Metal;
        default: return M::Window;
      }
    case BuildingElement::Interior: return building_enterable(kind) ? std::optional<M>(M::Timber) : std::nullopt;
    case BuildingElement::Count: break;
  }
  throw std::invalid_argument("unknown building element");
}

const char* building_event_name(BuildingEvent::Kind kind) {
  switch (kind) {
    case BuildingEvent::Kind::Alight: return "alight";
    case BuildingEvent::Kind::BurnedOut: return "burned out";
    case BuildingEvent::Kind::Damaged: return "damaged";
    case BuildingEvent::Kind::RoofFell: return "roof fell";
    case BuildingEvent::Kind::Collapsed: return "collapsed";
    case BuildingEvent::Kind::WashedOut: return "washed out";
    case BuildingEvent::Kind::Extinguished: return "extinguished";
    case BuildingEvent::Kind::Struck: return "struck";
    case BuildingEvent::Kind::Hit: return "hit";
    case BuildingEvent::Kind::Flooded: return "flooded";
  }
  throw std::invalid_argument("unknown building event");
}

bool BuildingState::alight() const {
  for (const auto& e : elements) if (e.alight) return true;
  return false;
}
double BuildingState::intensity() const {
  double best = 0.0;
  for (const auto& e : elements) best = std::max(best, e.intensity);
  return best;
}
double BuildingState::integrity() const {
  if (destroyed) return 0.0;
  return std::clamp(1.0 - 0.6 * element(BuildingElement::Walls).damage - 0.4 * element(BuildingElement::Roof).damage, 0.0, 1.0);
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
// Where a kind's chimney stands (plan), if it has one.
std::optional<math::Vec2> chimney_plan(const Building& b) {
  const Frame frame = frame_of(b);
  const double hw = 0.5 * b.spec.width_meters, hd = 0.5 * b.spec.depth_meters;
  if (b.kind == BuildingKind::Bungalow) return frame.plan(-hw * 0.55, -hd * 0.3);
  if (b.kind == BuildingKind::House) return frame.plan(hw * 0.5, -hd * 0.45);
  return std::nullopt;
}
// The distance from a plan point to a building's footprint (0 inside it).
double footprint_distance(const Building& b, math::Vec2 p) {
  const auto [a, bb] = frame_of(b).local(p);
  const double dx = std::max(0.0, std::abs(a) - 0.5 * b.spec.width_meters), dz = std::max(0.0, std::abs(bb) - 0.5 * b.spec.depth_meters);
  return std::hypot(dx, dz);
}
// Whether a plan segment crosses a building's footprint (Liang-Barsky in the local frame).
bool crosses_footprint(const Building& b, math::Vec2 from, math::Vec2 to) {
  const Frame frame = frame_of(b);
  const auto [a0, b0] = frame.local(from);
  const auto [a1, b1] = frame.local(to);
  const double hw = 0.5 * b.spec.width_meters, hd = 0.5 * b.spec.depth_meters;
  double t0 = 0.0, t1 = 1.0;
  const double d[2] = {a1 - a0, b1 - b0}, s0[2] = {a0, b0}, h[2] = {hw, hd};
  for (int axis = 0; axis < 2; ++axis) {
    for (const double sign : {-1.0, 1.0}) {
      const double p = -sign * d[axis], q = sign * s0[axis] + h[axis];  // sign * s <= h  ->  p t <= q
      if (std::abs(p) < 1e-12) { if (q < 0) return false; continue; }
      const double t = q / p;
      if (p < 0) t0 = std::max(t0, t); else t1 = std::min(t1, t);
      if (t0 > t1) return false;
    }
  }
  return true;
}
// A colour darkened by damage (char, soot, rot).
math::Vec3 charred(math::Vec3 color, double damage) { return color * (1.0 - 0.65 * std::clamp(damage, 0.0, 1.0)); }
// A flight's height over the ground floor at a local point within its strip, if the point is on it.
std::optional<double> flight_height_at(const InteriorStair& flight, double a, double b) {
  const math::Vec2 run = flight.head - flight.foot;
  const double len = run.length();
  if (len < 1e-9) return std::nullopt;
  const math::Vec2 dir = run / len, side{-dir.y, dir.x};
  const math::Vec2 d = math::Vec2{a, b} - flight.foot;
  const double along = d.dot(dir), across = std::abs(d.dot(side));
  if (across > 0.5 * flight.width || along < -1e-9 || along > len + 1e-9) return std::nullopt;
  return flight.base + flight.rise * (along / len);
}
bool in_rect(double a, double b, double a0, double a1, double b0, double b1) { return a >= a0 - 1e-9 && a <= a1 + 1e-9 && b >= b0 - 1e-9 && b <= b1 + 1e-9; }
// Whether a local point lies over a hole in a level's slab.
bool in_hole(const InteriorPlan& plan, int level, double a, double b) {
  for (const auto& h : plan.holes) if (h.level == level && in_rect(a, b, h.a0, h.a1, h.b0, h.b1)) return true;
  return false;
}
// The surfaces of a plan at a local point (heights over the ground floor): the
// floors outside their holes, the flights, the landings.
std::vector<double> surfaces_at(const InteriorPlan& plan, double a, double b) {
  std::vector<double> out;
  for (int k = 0; k < std::max(1, plan.floors); ++k) if (!in_hole(plan, k, a, b)) out.push_back(plan.level(k));
  for (const auto& f : plan.flights) if (const auto h = flight_height_at(f, a, b)) out.push_back(*h);
  for (const auto& l : plan.landings) if (in_rect(a, b, l.a0, l.a1, l.b0, l.b1)) out.push_back(l.height);
  return out;
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
  lifts_.resize(buildings_.size());
  plans_.reserve(buildings_.size());
  for (const auto& b : buildings_) plans_.push_back(interior_plan(b.spec));
  states_.resize(buildings_.size());
  drawn_levels_.resize(buildings_.size());
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    for (std::size_t e = 0; e < static_cast<std::size_t>(BuildingElement::Count); ++e) {
      auto& el = states_[i].elements[e];
      if (const auto m = building_element_material(buildings_[i].kind, static_cast<BuildingElement>(e))) { el.material = *m; el.present = true; }
      drawn_levels_[i][e] = 0;
    }
  }
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
    const auto& st = states_[&b - buildings_.data()];
    if (st.alight()) ++s.alight;
    if (st.destroyed) ++s.destroyed;
    else {
      bool any = false;
      for (const auto& e : st.elements) if (e.present && e.damage > 0) any = true;
      if (any) ++s.damaged;
      if (st.roof_fallen) ++s.roofless;
    }
    if (st.flood_depth_meters > 0) ++s.flooded;
    if (st.strikes) ++s.struck;
    s.tallest_meters = std::max(s.tallest_meters, b.height_meters);
    s.most_storeys = std::max(s.most_storeys, b.storeys);
    s.floor_area_m2 += b.spec.width_meters * b.spec.depth_meters * b.storeys;
  }
  s.logs = logs_.size();
  return s;
}

std::optional<double> BuildingSet::platform_height(double x, double z) const {
  if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("building sample position must be finite");
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    const auto& b = buildings_[i];
    const auto [a, bb] = frame_of(b).local({x, z});
    if (states_[i].destroyed) {
      // A heap of rubble over the footprint, stepped onto.
      if (std::abs(a) <= 0.5 * b.spec.width_meters && std::abs(bb) <= 0.5 * b.spec.depth_meters) return b.floor_meters + kRubbleHeight;
    } else if (building_enterable(b.kind) && (inside(i, x, z) || in_doorway(b, a, bb))) return b.floor_meters;
    if (std::abs(a) > 0.5 * b.spec.path_width_meters || bb < 0.5 * b.spec.depth_meters - 1e-9 || bb > 0.5 * b.spec.depth_meters + b.spec.setback_meters + 1e-9) continue;
    const auto h = ground_(x, z);
    if (!h) continue;
    return path_height_at(b, {x, z}, *h, style_.surface_lift_meters);
  }
  return std::nullopt;
}

std::optional<double> BuildingSet::platform_height(double x, double z, double feet_y) const {
  if (!std::isfinite(feet_y)) throw std::invalid_argument("building sample position must be finite");
  const auto i = inside_of(x, z);
  if (!i) return platform_height(x, z);
  const auto& b = buildings_[*i];
  const InteriorPlan& plan = plans_[*i];
  if (plan.floors <= 1 && plan.flights.empty()) return b.floor_meters;
  const auto [a, bb] = frame_of(b).local({x, z});
  // The surfaces here — the floors outside their holes, the flights, the
  // landings, the lift's car in its shaft — and of them the highest no more
  // than the step height above the feet.
  std::vector<double> levels;
  if (plan.lift && in_rect(a, bb, plan.lift->a0, plan.lift->a1, plan.lift->b0, plan.lift->b1)) levels.push_back(lifts_[*i].height);
  else levels = surfaces_at(plan, a, bb);
  if (levels.empty()) levels.push_back(0.0);
  double best = *std::min_element(levels.begin(), levels.end());
  for (const double level : levels) if (b.floor_meters + level <= feet_y + 0.5 + 1e-6 && level > best) best = level;
  return b.floor_meters + best;
}

int BuildingSet::level_at(std::size_t building, double feet_y) const {
  if (building >= buildings_.size()) throw std::out_of_range("building index out of range");
  const InteriorPlan& plan = plans_[building];
  if (plan.floors <= 1 || plan.floor_height <= 0) return 0;
  return std::clamp(static_cast<int>(std::floor((feet_y - buildings_[building].floor_meters + 0.25) / plan.floor_height)), 0, plan.floors - 1);
}

const char* BuildingSet::room_at(std::size_t building, double x, double z, double feet_y) const {
  if (building >= buildings_.size()) throw std::out_of_range("building index out of range");
  if (!inside(building, x, z)) return nullptr;
  const auto& b = buildings_[building];
  const InteriorPlan& plan = plans_[building];
  const auto [a, bb] = frame_of(b).local({x, z});
  const int level = level_at(building, feet_y);
  const double over = feet_y - b.floor_meters;
  if (plan.lift && in_rect(a, bb, plan.lift->a0, plan.lift->a1, plan.lift->b0, plan.lift->b1)) return "lift";
  for (const auto& f : plan.flights)
    if (const auto h = flight_height_at(f, a, bb); h && std::abs(*h - over) < 0.6 && *h > f.base + 0.05 && *h < f.base + f.rise - 0.05) return "stairs";
  for (const auto& l : plan.landings) if (in_rect(a, bb, l.a0, l.a1, l.b0, l.b1) && std::abs(l.height - over) < 0.6) return "stairs";
  for (const auto& room : plan.rooms)
    if (room.level == level && in_rect(a, bb, room.a0, room.a1, room.b0, room.b1)) return room.name;
  return level ? "upstairs" : "inside";
}

std::optional<LiftState> BuildingSet::lift_state(std::size_t building) const {
  if (building >= buildings_.size()) throw std::out_of_range("building index out of range");
  if (!plans_[building].lift || states_[building].destroyed) return std::nullopt;
  return lifts_[building];
}

bool BuildingSet::in_lift(std::size_t building, double x, double z) const {
  if (building >= buildings_.size()) throw std::out_of_range("building index out of range");
  const auto& l = plans_[building].lift;
  if (!l || !inside(building, x, z)) return false;
  const auto [a, bb] = frame_of(buildings_[building]).local({x, z});
  return in_rect(a, bb, l->a0, l->a1, l->b0, l->b1);
}

std::optional<std::size_t> BuildingSet::lift_door_near(double x, double z, double feet_y, double reach) const {
  if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(feet_y) || !within(reach, 0, 100)) throw std::invalid_argument("lift query out of range");
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    const auto& l = plans_[i].lift;
    if (!l || states_[i].destroyed || !inside(i, x, z)) continue;
    const auto& b = buildings_[i];
    const math::Vec2 door = frame_of(b).plan(l->door_along, l->b1 + kWall);  // just outside the shaft's wall
    if ((door - math::Vec2{x, z}).length() > reach) continue;
    const int level = level_at(i, feet_y);
    if (std::abs(feet_y - b.floor_meters - plans_[i].level(level)) > 1.0) continue;  // on a floor, not on the stairs
    return i;
  }
  return std::nullopt;
}

bool BuildingSet::call_lift(std::size_t building, int floor) {
  if (building >= buildings_.size()) throw std::out_of_range("building index out of range");
  const auto& plan = plans_[building];
  if (!plan.lift || states_[building].destroyed || floor < 0 || floor >= plan.floors) return false;
  auto& l = lifts_[building];
  if (floor == l.target && (l.moving || l.open)) return true;  // already coming, or here with its doors open
  l.target = floor;
  l.open = false;
  l.moving = true;
  ++door_version_;
  return true;
}

bool BuildingSet::ride_lift(std::size_t building, int floors_up) {
  if (building >= buildings_.size()) throw std::out_of_range("building index out of range");
  const auto& l = lifts_[building];
  return call_lift(building, (l.moving ? l.target : l.floor) + floors_up);
}

double BuildingSet::speed_factor(double x, double z) const {
  if (!platform_height(x, z)) return 1.0;
  if (const auto i = building_at(x, z)) if (states_[*i].destroyed) return style_.rubble_speed_factor;
  return style_.path_speed_factor;
}

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
  if (!building_enterable(b.kind) || states_[building].destroyed) return false;
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
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    const auto& b = buildings_[i];
    const auto& st = states_[i];
    const Frame frame = frame_of(b);
    const double hw = 0.5 * b.spec.width_meters, hd = 0.5 * b.spec.depth_meters;
    const double bottom = b.ground_min_meters - kFoundationDepth, top = b.floor_meters + b.height_meters;
    if (st.destroyed) {
      // Rubble is walked over; the chimney stands in it.
      if (const auto c = chimney_plan(b)) { Obstacle o; o.x = c->x; o.z = c->y; o.radius = 0.45; o.bottom = bottom; o.top = top; out.push_back(o); }
      continue;
    }
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
    // The partitions (with their doorways open) and the rails, heights over the floor.
    const InteriorPlan& plan = plans_[i];
    for (const auto& w : plan.walls) {
      const math::Vec2 dir = (w.to - w.from) / std::max((w.to - w.from).length(), 1e-9);
      const double radius = w.thickness >= kWall - 1e-9 ? kWallPostRadius : 0.5 * w.thickness + 0.1;  // a thin spine wall stands as thin posts
      std::vector<std::pair<double, double>> gaps;
      for (const auto& o : w.openings) gaps.push_back({o.along - 0.5 * o.width - radius, o.along + 0.5 * o.width + radius});
      std::sort(gaps.begin(), gaps.end());
      double at = 0.0;
      const double len = (w.to - w.from).length();
      const auto local_chain = [&](double s0, double s1) {
        const math::Vec2 p0 = w.from + dir * s0, p1 = w.from + dir * s1;
        post_chain(out, frame.plan(p0.x, p0.y), frame.plan(p1.x, p1.y), radius, b.floor_meters + w.bottom - 0.5, b.floor_meters + w.top);
      };
      for (const auto& gap : gaps) {
        if (gap.first > at) local_chain(at, gap.first);
        at = std::max(at, gap.second);
      }
      if (len > at) local_chain(at, len);
    }
    for (const auto& r : plan.rails) post_chain(out, frame.plan(r.from.x, r.from.y), frame.plan(r.to.x, r.to.y), kDoorPostRadius, b.floor_meters + r.bottom, b.floor_meters + r.top);
    // The furniture: a solid piece as posts along its longer axis, as thick as its shorter one (gone once it has burned).
    if (st.element(BuildingElement::Interior).damage >= 1.0) continue;
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

bool BuildingSet::door_present(std::size_t i) const {
  return building_enterable(buildings_[i].kind) && !states_[i].destroyed && states_[i].element(BuildingElement::Door).damage < 1.0;
}

bool BuildingSet::toggle_door(std::size_t building) {
  if (building >= buildings_.size()) throw std::out_of_range("building index out of range");
  if (!door_present(building)) return false;
  auto& d = doors_[building];
  d.opening = !(d.opening || d.open >= 1.0);
  ++door_version_;
  return true;
}

void BuildingSet::raise(BuildingEvent::Kind kind, std::size_t building, BuildingElement element, int tree, const char* cause) {
  BuildingEvent e; e.kind = kind; e.building = building; e.element = element; e.tree = tree; e.cause = cause ? cause : "";
  events_.push_back(std::move(e));
}

void BuildingSet::damage_element(std::size_t i, BuildingElement element, double amount, const char* cause) {
  auto& el = states_[i].element(element);
  if (!el.present || amount <= 0 || el.damage >= 1.0) return;
  const double before = el.damage;
  el.damage = std::min(1.0, el.damage + amount);
  el.cause = cause ? cause : "";
  if (before <= 0) raise(BuildingEvent::Kind::Damaged, i, element, -1, cause);
}

void BuildingSet::roof_falls(std::size_t i) {
  auto& st = states_[i];
  if (st.roof_fallen) return;
  st.roof_fallen = true;
  auto& roof = st.element(BuildingElement::Roof);
  const bool burning = roof.alight;
  roof.alight = false; roof.intensity = 0.0; roof.damage = 1.0;
  ++version_;
  raise(BuildingEvent::Kind::RoofFell, i, BuildingElement::Roof, -1, roof.cause.c_str());
  // The debris lands in the room: the interior is smashed, and lit if the roof was burning.
  damage_element(i, BuildingElement::Interior, 0.5, "impact");
  if (burning) ignite(i, BuildingElement::Interior, "fire");
}

void BuildingSet::destroy(std::size_t i, const char* cause) {
  if (i >= buildings_.size()) throw std::out_of_range("building index out of range");
  auto& st = states_[i];
  if (st.destroyed) return;
  st.destroyed = true;
  st.destroyed_by = cause ? cause : "";
  st.roof_fallen = true;
  for (auto& el : st.elements) { if (!el.present) continue; el.alight = false; el.intensity = 0.0; el.damage = 1.0; if (el.cause.empty()) el.cause = st.destroyed_by; }
  doors_[i] = DoorState{};
  lifts_[i] = LiftState{};
  logs_.erase(std::remove_if(logs_.begin(), logs_.end(), [&](const BuildingLog& l) { return l.building == i; }), logs_.end());
  ++version_; ++door_version_;
  raise(st.destroyed_by == "flood" ? BuildingEvent::Kind::WashedOut : BuildingEvent::Kind::Collapsed, i, BuildingElement::Walls, -1, st.destroyed_by.c_str());
}

bool BuildingSet::ignite(std::size_t i, BuildingElement element, const char* cause) {
  if (i >= buildings_.size()) throw std::out_of_range("building index out of range");
  if (element >= BuildingElement::Count) throw std::invalid_argument("unknown building element");
  auto& st = states_[i];
  auto& el = st.element(element);
  if (st.destroyed || !el.present || el.alight || el.damage >= 1.0 || !material_hazard(el.material).flammable) return false;
  const double submerged_at = element == BuildingElement::Interior ? 0.05 : element == BuildingElement::Door ? 0.1 : element == BuildingElement::Walls ? 0.3 : element == BuildingElement::Windows ? 1.0 : 1e9;
  if (st.flood_depth_meters > submerged_at) return false;
  el.alight = true; el.heat_seconds = 0.0; el.cause = cause ? cause : "fire";
  raise(BuildingEvent::Kind::Alight, i, element, -1, el.cause.c_str());
  return true;
}

std::optional<std::size_t> BuildingSet::strike(double x, double z) {
  if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("strike position must be finite");
  std::optional<std::size_t> best;
  double best_score = std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    if (states_[i].destroyed) continue;
    const double d = footprint_distance(buildings_[i], {x, z});
    if (d > style_.strike_radius_meters) continue;
    const double score = d - buildings_[i].height_meters;  // the tallest within reach draws the bolt
    if (score < best_score) { best_score = score; best = i; }
  }
  if (!best) return std::nullopt;
  auto& st = states_[*best];
  ++st.strikes;
  raise(BuildingEvent::Kind::Struck, *best, BuildingElement::Roof);
  if (!ignite(*best, BuildingElement::Roof, "lightning")) damage_element(*best, BuildingElement::Roof, style_.strike_scorch, "lightning");
  return best;
}

std::size_t BuildingSet::log_landed(std::uint32_t tree, math::Vec2 base, math::Vec2 direction, double length, bool burning, double mass_kg) {
  if (!finite(base) || !finite(direction) || !within(length, 0, 1000) || !within(mass_kg, 0, 1e7))
    throw std::invalid_argument("a landed trunk needs a finite base, direction, length and mass");
  const double dl = direction.length();
  if (dl < 1e-9 || length <= 0) return 0;
  const math::Vec2 tip = base + direction * (length / dl);
  std::size_t hit = 0;
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    if (states_[i].destroyed || !crosses_footprint(buildings_[i], base, tip)) continue;
    bool known = false;
    for (const auto& l : logs_) if (l.building == i && l.tree == tree) known = true;
    if (known) continue;
    logs_.push_back({i, tree, burning});
    ++hit;
    raise(BuildingEvent::Kind::Hit, i, BuildingElement::Roof, static_cast<int>(tree));
    auto& st = states_[i];
    const double tonnes = mass_kg / 1000.0;
    const auto smash = [&](BuildingElement element, double share) {
      const auto& el = st.element(element);
      if (!el.present) return;
      const double strength = material_hazard(el.material).impact_tonnes;
      if (strength <= 0) return;
      damage_element(i, element, share * tonnes / strength, "impact");
    };
    smash(BuildingElement::Roof, 1.0);
    smash(BuildingElement::Walls, 0.5);
    smash(BuildingElement::Windows, 0.5);
    if (st.element(BuildingElement::Roof).damage >= 1.0) roof_falls(i);
    if (st.element(BuildingElement::Walls).damage >= 1.0) destroy(i, "impact");
    ++version_;
  }
  return hit;
}

void BuildingSet::reset() {
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    BuildingState fresh;
    for (std::size_t e = 0; e < static_cast<std::size_t>(BuildingElement::Count); ++e) {
      if (const auto m = building_element_material(buildings_[i].kind, static_cast<BuildingElement>(e))) { fresh.elements[e].material = *m; fresh.elements[e].present = true; }
      drawn_levels_[i][e] = 0;
    }
    states_[i] = std::move(fresh);
    doors_[i] = DoorState{};
    lifts_[i] = LiftState{};
  }
  logs_.clear();
  events_.clear();
  ++version_; ++door_version_;
}

double BuildingSet::external_heat_at(std::size_t i, const BuildingHazards& hazards) const {
  const auto& b = buildings_[i];
  const Frame frame = frame_of(b);
  const double hw = 0.5 * b.spec.width_meters, hd = 0.5 * b.spec.depth_meters;
  double heat = 0.0;
  const math::Vec2 samples[6] = {b.centre, b.door, frame.plan(-hw, -hd), frame.plan(hw, -hd), frame.plan(hw, hd), frame.plan(-hw, hd)};
  for (const auto& p : samples) {
    if (hazards.heat) heat = std::max(heat, std::clamp(hazards.heat(p.x, p.y), 0.0, 1.0));
    // The other burning buildings.
    for (std::size_t j = 0; j < buildings_.size(); ++j) {
      if (j == i) continue;
      const double intensity = states_[j].intensity();
      if (intensity <= 0) continue;
      const double d = footprint_distance(buildings_[j], p);
      heat = std::max(heat, intensity * (1.0 - std::clamp((d - 3.0) / std::max(style_.heat_reach_meters - 3.0, 0.1), 0.0, 1.0)));
    }
  }
  return heat;
}

void BuildingSet::advance(double dt, const BuildingHazards& hazards) {
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("building time step must be finite and non-negative");
  events_.clear();
  if (dt <= 0) return;
  // The doors.
  for (auto& d : doors_) {
    const double target = d.opening ? 1.0 : 0.0;
    if (d.open == target) continue;
    const double step = dt / kDoorSwingSeconds;
    d.open = d.opening ? std::min(1.0, d.open + step) : std::max(0.0, d.open - step);
    ++door_version_;
  }
  // The lifts: travelling to their floor, opening on arrival.
  for (std::size_t i = 0; i < lifts_.size(); ++i) {
    auto& l = lifts_[i];
    if (!l.moving) continue;
    const auto& plan = plans_[i];
    const double goal = plan.level(l.target), step = (plan.lift ? plan.lift->speed_mps : 1.5) * dt;
    if (std::abs(goal - l.height) <= step) { l.height = goal; l.floor = l.target; l.moving = false; l.open = true; }
    else l.height += goal > l.height ? step : -step;
    ++door_version_;
  }
  // The logs lying on the buildings: their fire goes on or out.
  for (auto& l : logs_) l.burning = hazards.log_intensity && l.building < states_.size() && std::clamp(hazards.log_intensity(l.tree), 0.0, 1.0) > 0;
  const double wind = std::clamp(hazards.wind_speed_mps, 0.0, 150.0), rain = std::max(0.0, hazards.rain_mm_h);
  const double burn_rate = (1.0 + style_.wind_burn_factor * wind) / (1.0 + rain / style_.rain_slowdown_mm_h);
  constexpr std::size_t kCount = static_cast<std::size_t>(BuildingElement::Count);
  constexpr double kSubmergedAt[kCount] = {0.3, 1e9, 1.0, 0.1, 0.05};  // walls, roof, windows, door, interior: water over the floor that covers the element
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    auto& st = states_[i];
    const auto& b = buildings_[i];
    if (st.destroyed) continue;
    // The hazards at this building, sampled once for the step.
    const double external = external_heat_at(i, hazards);
    double log_heat = 0.0;
    for (const auto& l : logs_) if (l.building == i && l.burning) log_heat = std::max(log_heat, std::clamp(hazards.log_intensity(l.tree), 0.0, 1.0));
    double surface = -std::numeric_limits<double>::infinity(), depth_here = 0.0;
    bool torrent = false;
    if (hazards.water) {
      const Frame frame = frame_of(b);
      const double hw = 0.5 * b.spec.width_meters, hd = 0.5 * b.spec.depth_meters;
      const math::Vec2 samples[5] = {b.door, frame.plan(-hw, -hd), frame.plan(hw, -hd), frame.plan(hw, hd), frame.plan(-hw, hd)};
      for (const auto& p : samples) {
        const auto g = ground_(p.x, p.y);
        if (!g) continue;
        const auto w = hazards.water(p.x, p.y, *g);
        if (w.depth_meters > 0) surface = std::max(surface, w.surface_meters);
        if (w.current_mps >= style_.flood_current_mps && w.depth_meters >= style_.flood_depth_meters) torrent = true;
      }
      depth_here = std::isfinite(surface) ? std::max(0.0, surface - b.floor_meters) : 0.0;
    }
    const bool was_flooded = st.flood_depth_meters > 0.05;
    st.flood_depth_meters = depth_here;
    if (!was_flooded && depth_here > 0.05) raise(BuildingEvent::Kind::Flooded, i);
    st.scour_seconds = torrent ? st.scour_seconds + dt : std::max(0.0, st.scour_seconds - dt);
    if (st.scour_seconds >= style_.flood_scour_seconds) { destroy(i, "flood"); continue; }
    // The elements, in sub-steps so a cascade within one call follows its delays.
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / 0.25)));
    const double h = dt / steps;
    for (int step = 0; step < steps; ++step) {
      const double internal = st.intensity() * style_.spread_heat;
      for (std::size_t e = 0; e < kCount; ++e) {
        auto& el = st.elements[e];
        if (!el.present) continue;
        const auto element = static_cast<BuildingElement>(e);
        const auto hz = material_hazard(el.material);
        double heat = internal;
        if (element != BuildingElement::Interior) heat = std::max(heat, external);
        if (element == BuildingElement::Roof) heat = std::max(heat, log_heat);
        const bool under_water = st.flood_depth_meters > kSubmergedAt[e];
        if (under_water) {
          if (el.alight) { el.alight = false; el.intensity = 0.0; raise(BuildingEvent::Kind::Extinguished, i, element, -1, "flood"); }
          el.heat_seconds = 0.0;
          if (hz.soak_seconds > 0) { el.soaked_seconds += h; damage_element(i, element, h / hz.soak_seconds, "flood"); }
        } else if (hz.flammable) {
          if (el.alight) {
            el.intensity = std::min(1.0, el.intensity + h / kFlameRamp);
            st.burn_seconds += h;
            damage_element(i, element, h * el.intensity * burn_rate / hz.burn_seconds, "fire");
            if (el.damage >= 1.0) { el.alight = false; el.intensity = 0.0; raise(BuildingEvent::Kind::BurnedOut, i, element); }
          } else if (el.damage < 1.0 && heat >= hz.ignition_heat) {
            el.heat_seconds += h;
            if (el.heat_seconds >= hz.ignition_seconds) ignite(i, element, "fire");
          } else el.heat_seconds = std::max(0.0, el.heat_seconds - h);
        } else if (heat >= hz.heat_threshold && hz.heat_damage_seconds > 0) {
          damage_element(i, element, h / hz.heat_damage_seconds, "heat");
        }
        if (!el.alight && el.intensity > 0) el.intensity = std::max(0.0, el.intensity - h / kFlameRamp);
        // The wind tears at what it can.
        if (!under_water && hz.wind_mps > 0 && wind >= hz.wind_mps && hz.wind_seconds > 0) {
          el.wind_seconds += h;
          const double ratio = wind / hz.wind_mps;
          damage_element(i, element, h * ratio * ratio / hz.wind_seconds, "wind");
        }
      }
      // Consequences.
      if (st.element(BuildingElement::Roof).present && st.element(BuildingElement::Roof).damage >= 1.0 && !st.roof_fallen) roof_falls(i);
      if (st.element(BuildingElement::Walls).damage >= 1.0) { destroy(i, st.element(BuildingElement::Walls).cause.c_str()); break; }
    }
    if (st.destroyed) continue;
    // What is drawn changes in tenths of damage.
    for (std::size_t e = 0; e < kCount; ++e) {
      const int level = static_cast<int>(std::floor(std::clamp(st.elements[e].damage, 0.0, 1.0) * 10.0 + 1e-9));
      if (level != drawn_levels_[i][e]) { drawn_levels_[i][e] = level; ++version_; if (e == static_cast<std::size_t>(BuildingElement::Door) && level >= 10) ++door_version_; }
    }
  }
}

BuildingSet::Exposure BuildingSet::exposure_at(double x, double z) const {
  if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("exposure needs a finite position");
  Exposure e;
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    const double intensity = states_[i].intensity();
    if (intensity <= 0) continue;
    const double d = footprint_distance(buildings_[i], {x, z});
    e.heat = std::max(e.heat, intensity * (1.0 - std::clamp((d - 3.0) / std::max(style_.heat_reach_meters - 3.0, 0.1), 0.0, 1.0)));
    if (d < 60.0) e.smoke += intensity * (1.0 - d / 60.0) * 0.5;
  }
  e.heat = std::clamp(e.heat, 0.0, 1.0);
  e.smoke = std::clamp(e.smoke, 0.0, 1.0);
  return e;
}

bool BuildingSet::sheltered(double x, double z) const {
  const auto i = inside_of(x, z);
  return i && !states_[*i].roof_fallen;
}

void BuildingSet::emit(std::vector<FireInstance>& flames, std::vector<FireInstance>& smoke, const FireStyle& style) const {
  const auto f = [](double v) { return static_cast<float>(v); };
  const auto hash01 = [](std::size_t i, std::uint32_t salt, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(i) * 2654435761u ^ (salt * 40503u) ^ (seed * 97u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (h & 0xffffffu) / static_cast<double>(0x1000000u);
  };
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    const auto& st = states_[i];
    const auto& b = buildings_[i];
    const Frame frame = frame_of(b);
    const double hw = 0.5 * b.spec.width_meters, hd = 0.5 * b.spec.depth_meters;
    const double walls_top = b.floor_meters + wall_height(b.spec);
    if (st.destroyed) {
      if (st.destroyed_by != "fire") continue;
      FireInstance pl{};
      pl.position[0] = f(b.centre.x); pl.position[1] = f(b.floor_meters + kRubbleHeight); pl.position[2] = f(b.centre.y);
      pl.size = f(style.smoke_height_meters * 0.3); pl.phase = f(2.0 * kPi * hash01(i, 3, style.seed));
      pl.intensity = 0.25f; pl.kind = 2.0f; pl.seed = f(hash01(i, 4, style.seed) * 100.0);
      smoke.push_back(pl);
      continue;
    }
    const auto flame = [&](math::Vec3 at, double size, double intensity, std::uint32_t salt) {
      FireInstance fl{};
      fl.position[0] = f(at.x); fl.position[1] = f(at.y); fl.position[2] = f(at.z);
      fl.size = f(size * style.flame_scale); fl.phase = f(2.0 * kPi * hash01(i, salt, style.seed));
      fl.intensity = f(intensity); fl.kind = 0.0f; fl.seed = f(hash01(i, salt + 50, style.seed) * 100.0);
      flames.push_back(fl);
    };
    double plume = 0.0;
    for (std::size_t e = 0; e < static_cast<std::size_t>(BuildingElement::Count); ++e) {
      const auto& el = st.elements[e];
      if (el.intensity <= 0) continue;
      plume = std::max(plume, el.intensity);
      const double size = 1.5 + 2.0 * el.intensity;
      switch (static_cast<BuildingElement>(e)) {
        case BuildingElement::Roof: {
          // Over the roof's surface: up the slopes of a gable, on the slab of a flat roof.
          const int count = std::max(3, static_cast<int>(b.spec.width_meters / 2.5));
          for (int k = 0; k < count; ++k) {
            const double bb = (hash01(i, 100 + k, style.seed) - 0.5) * 1.6 * hd;
            const double surface = gabled(b.kind) ? walls_top + (hd - std::abs(bb) + roof_overhang(b.kind)) * std::tan(roof_pitch(b.kind)) : walls_top + 0.3;
            flame(frame.at(-hw + (k + 0.5) * b.spec.width_meters / count, bb, surface + 0.3), size, el.intensity, 100 + k);
          }
          break;
        }
        case BuildingElement::Walls:
          // Up the outside of the walls, a little proud of them.
          for (int k = 0; k < 8; ++k) {
            const double t = (k % 4 + 0.5) / 4.0;
            const double out = 0.35;
            const math::Vec2 p = k < 4 ? frame.plan(-hw + t * b.spec.width_meters, (k % 2 ? 1.0 : -1.0) * (hd + out)) : frame.plan((k % 2 ? 1.0 : -1.0) * (hw + out), -hd + t * b.spec.depth_meters);
            flame(lift(p, walls_top - 1.2), size * 0.8, el.intensity, 200 + k);
          }
          break;
        case BuildingElement::Windows:
          for (const double side : {-1.0, 1.0}) flame(frame.at(side * hw * 0.5, hd + 0.3, b.floor_meters + 1.6), size * 0.5, el.intensity, side > 0 ? 301 : 300);
          break;
        case BuildingElement::Door: flame(lift(b.door + b.facing * 0.35, b.floor_meters + 1.0), size * 0.6, el.intensity, 400); break;
        case BuildingElement::Interior:
          flame(frame.at(0, 0, b.floor_meters + 0.8), size * 0.8, el.intensity, 500);
          flame(frame.at(hw * 0.4, 0, b.floor_meters + 0.8), size * 0.6, el.intensity, 501);
          flame(frame.at(-hw * 0.4, -hd * 0.5, b.floor_meters + 0.8), size * 0.6, el.intensity, 502);
          break;
        case BuildingElement::Count: break;
      }
    }
    if (plume > 0) {
      FireInstance pl{};
      pl.position[0] = f(b.centre.x); pl.position[1] = f(b.floor_meters + b.height_meters * (st.roof_fallen ? 0.6 : 1.0)); pl.position[2] = f(b.centre.y);
      pl.size = f(style.smoke_height_meters * 0.6 * (0.4 + 0.6 * plume)); pl.phase = f(2.0 * kPi * hash01(i, 40, style.seed));
      pl.intensity = f(plume); pl.kind = 1.0f; pl.seed = f(hash01(i, 41, style.seed) * 100.0);
      smoke.push_back(pl);
    }
  }
}

std::vector<Obstacle> BuildingSet::door_obstacles() const {
  std::vector<Obstacle> out;
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    if (door_present(i)) {
      const Leaf leaf = door_leaf(i);
      post_chain(out, leaf.hinge, leaf.hinge + leaf.along * leaf.width, kDoorPostRadius, leaf.bottom - 0.1, leaf.bottom + leaf.height);
    }
    // The lift's shaft doors: closed on every floor but the one the car stands open at.
    const auto& plan = plans_[i];
    if (!plan.lift || states_[i].destroyed) continue;
    const auto& l = lifts_[i];
    const Frame frame = frame_of(buildings_[i]);
    for (int k = 0; k < plan.floors; ++k) {
      if (!l.moving && l.open && l.floor == k) continue;
      const double y = buildings_[i].floor_meters + plan.level(k);
      post_chain(out, frame.plan(plan.lift->door_along - 0.5 * plan.lift->door_width, plan.lift->b1 + 0.5 * kWall),
                 frame.plan(plan.lift->door_along + 0.5 * plan.lift->door_width, plan.lift->b1 + 0.5 * kWall), kDoorPostRadius, y - 0.5, y + plan.lift->door_height);
    }
  }
  return out;
}

std::vector<MeshPart> BuildingSet::build_door_meshes() const {
  std::vector<MeshPart> parts;
  MeshPart leaves; leaves.color = {0.36, 0.24, 0.13}; leaves.what = "door"; leaves.material = SurfaceMaterial::Timber;
  MeshPart glass_leaves; glass_leaves.color = {0.55, 0.70, 0.85}; glass_leaves.what = "door"; glass_leaves.material = SurfaceMaterial::Window;  // the tower's lobby door
  MeshPart handles; handles.color = {0.75, 0.72, 0.55}; handles.what = "handle"; handles.material = SurfaceMaterial::Metal;
  double worst = 0.0;
  for (std::size_t i = 0; i < buildings_.size(); ++i) if (door_present(i)) worst = std::max(worst, states_[i].element(BuildingElement::Door).damage);
  leaves.color = charred(leaves.color, worst);
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    if (!door_present(i)) continue;
    const Leaf leaf = door_leaf(i);
    const bool glass = material_hazard(states_[i].element(BuildingElement::Door).material).flammable == false;
    const math::Vec3 along = plan3(leaf.along), across = kUp.cross(along).normalized();
    const math::Vec3 centre = lift(leaf.hinge + leaf.along * (0.5 * leaf.width), leaf.bottom + 0.01 + 0.5 * (leaf.height - 0.02));
    add_box(glass ? glass_leaves : leaves, centre, along, kUp, across, {0.5 * leaf.width, 0.5 * (leaf.height - 0.02), 0.02});
    const math::Vec3 handle = lift(leaf.hinge + leaf.along * (leaf.width - 0.1), leaf.bottom + 1.0);
    for (const double side : {-1.0, 1.0}) add_box(handles, handle + across * (side * 0.05), along, kUp, across, {0.07, 0.015, 0.03});
  }
  if (!leaves.indices.empty()) parts.push_back(std::move(leaves));
  if (!glass_leaves.indices.empty()) parts.push_back(std::move(glass_leaves));
  if (!handles.indices.empty()) parts.push_back(std::move(handles));
  // The lifts: the car where it stands (a floor, three walls and a ceiling of brushed metal) and the closed shaft doors.
  MeshPart cars; cars.color = {0.72, 0.73, 0.75}; cars.what = "lift"; cars.material = SurfaceMaterial::Metal;
  MeshPart shaft_doors; shaft_doors.color = {0.55, 0.57, 0.60}; shaft_doors.what = "lift door"; shaft_doors.material = SurfaceMaterial::Metal;
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    const auto& plan = plans_[i];
    if (!plan.lift || states_[i].destroyed) continue;
    const auto& b = buildings_[i];
    const auto& lf = *plan.lift;
    const auto& l = lifts_[i];
    const Frame frame = frame_of(b);
    const math::Vec3 u3 = plan3(b.along), v3 = plan3(b.facing);
    const double ca = 0.5 * (lf.a0 + lf.a1), cb = 0.5 * (lf.b0 + lf.b1), ha = 0.5 * (lf.a1 - lf.a0), hb = 0.5 * (lf.b1 - lf.b0), y = b.floor_meters + l.height;
    const double car_h = 2.4;
    add_box(cars, frame.at(ca, cb, y - 0.05), u3, kUp, v3, {ha, 0.05, hb});                       // the floor
    add_box(cars, frame.at(ca, cb, y + car_h + 0.03), u3, kUp, v3, {ha, 0.03, hb});               // the ceiling
    add_box(cars, frame.at(ca, lf.b0 + 0.03, y + 0.5 * car_h), u3, kUp, v3, {ha, 0.5 * car_h, 0.03});  // the back wall
    for (const double side : {-1.0, 1.0}) add_box(cars, frame.at(ca + side * (ha - 0.03), cb, y + 0.5 * car_h), u3, kUp, v3, {0.03, 0.5 * car_h, hb});  // the sides
    for (int k = 0; k < plan.floors; ++k) {
      if (!l.moving && l.open && l.floor == k) continue;
      add_box(shaft_doors, frame.at(lf.door_along, lf.b1 + 0.5 * kWall, b.floor_meters + plan.level(k) + 0.5 * lf.door_height), u3, kUp, v3, {0.5 * lf.door_width, 0.5 * lf.door_height, 0.03});
    }
  }
  if (!cars.indices.empty()) { parts.push_back(std::move(cars)); if (!shaft_doors.indices.empty()) parts.push_back(std::move(shaft_doors)); }
  return parts;
}

std::vector<std::size_t> BuildingSet::interiors_near(math::Vec2 focus, double radius) const {
  if (!finite(focus) || !within(radius, 0, 1e7)) throw std::invalid_argument("interior focus must be finite and the radius non-negative");
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < buildings_.size(); ++i)
    if (building_enterable(buildings_[i].kind) && !states_[i].destroyed && footprint_distance(buildings_[i], focus) <= radius) out.push_back(i);
  return out;
}

std::vector<MeshPart> BuildingSet::build_meshes(std::optional<math::Vec2> focus, double interior_radius) const {
  if (focus && (!finite(*focus) || !within(interior_radius, 0, 1e7))) throw std::invalid_argument("interior focus must be finite and the radius non-negative");
  std::vector<MeshPart> parts;
  const math::Vec3 concrete{0.62, 0.60, 0.57}, path_color{0.70, 0.68, 0.65};
  MeshPart paths; paths.color = path_color; paths.what = "path"; paths.material = SurfaceMaterial::Pavement;
  MeshPart foundations; foundations.color = concrete * 0.85; foundations.what = "foundation"; foundations.material = SurfaceMaterial::Concrete;
  std::vector<MeshPart> furniture;  // shared by every hollow building, a part per kind of piece (and per tenth of char)
  std::vector<std::string> furniture_keys;
  MeshPart rubble; rubble.color = {0.46, 0.44, 0.41}; rubble.what = "rubble"; rubble.material = SurfaceMaterial::Stone;
  MeshPart ashes; ashes.color = {0.16, 0.14, 0.12}; ashes.what = "rubble"; ashes.material = SurfaceMaterial::Stone;  // burned down: black
  for (std::size_t index = 0; index < buildings_.size(); ++index) {
    const auto& b = buildings_[index];
    const auto& st = states_[index];
    const Frame frame = frame_of(b);
    const math::Vec3 u3 = plan3(b.along), v3 = plan3(b.facing);
    const double hw = 0.5 * b.spec.width_meters, hd = 0.5 * b.spec.depth_meters;
    const double walls_top = b.floor_meters + wall_height(b.spec);
    const auto kind = b.kind;
    const double walls_damage = st.element(BuildingElement::Walls).damage, roof_damage = st.element(BuildingElement::Roof).damage;
    const double windows_damage = st.element(BuildingElement::Windows).present ? st.element(BuildingElement::Windows).damage : 0.0;
    const double door_damage = st.element(BuildingElement::Door).damage, interior_damage = st.element(BuildingElement::Interior).damage;
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
    const bool detailed = !focus || footprint_distance(b, *focus) <= interior_radius;  // the rooms and furniture are built near the focus only
    const DoorSize door = door_size(kind);
    // Damage shows: char, soot, rot and dust darken what is damaged.
    wall = charred(wall, kind == BuildingKind::Skyscraper ? std::max(walls_damage, windows_damage) : walls_damage); roof = charred(roof, roof_damage); window = charred(window, windows_damage); door_color = charred(door_color, door_damage);
    if (door_damage >= 1.0) { door_color = {0.05, 0.04, 0.03}; door_material = SurfaceMaterial::Plain; }  // burned away: the dark doorway
    const bool windows_burst = windows_damage >= 1.0;
    if (windows_burst) window = {0.05, 0.05, 0.06};  // burst: dark holes
    // Foundation: from below the lowest corner to the floor slab (a hollow
    // building's timber floor lies on it), a little wider than the walls.
    const double foundation_top = hollow && !st.destroyed ? b.floor_meters - 0.1 : b.floor_meters;
    add_box(foundations, frame.at(0, 0, 0.5 * (b.ground_min_meters - kFoundationDepth + foundation_top)), u3, kUp, v3,
            {hw + 0.1, 0.5 * (foundation_top - (b.ground_min_meters - kFoundationDepth)), hd + 0.1});
    if (st.destroyed) {
      // A heap of rubble over the footprint, a few leaning slabs of wall in it, and the chimney standing.
      MeshPart& heap = st.destroyed_by == "fire" ? ashes : rubble;
      add_box(heap, frame.at(0, 0, b.floor_meters + 0.5 * kRubbleHeight), u3, kUp, v3, {hw * 0.95, 0.5 * kRubbleHeight, hd * 0.95});
      add_box(heap, frame.at(-hw * 0.3, hd * 0.2, b.floor_meters + kRubbleHeight + 0.25), u3, kUp, v3, {std::min(hw * 0.4, 3.0), 0.25, std::min(hd * 0.3, 2.0)});
      add_box(heap, frame.at(hw * 0.35, -hd * 0.3, b.floor_meters + kRubbleHeight + 0.15), u3, kUp, v3, {std::min(hw * 0.25, 2.0), 0.15, std::min(hd * 0.45, 3.0)});
      {
        const math::Vec3 tilted = (u3 * 0.94 + kUp * 0.34).normalized();
        math::Vec3 tilted_up = v3.cross(tilted).normalized();
        if (tilted_up.y < 0) tilted_up = tilted_up * -1.0;
        MeshPart slab; slab.color = charred(wall, st.destroyed_by == "fire" ? 1.0 : 0.3); slab.what = wall_what; slab.material = wall_material;
        add_box(slab, frame.at(hw * 0.5, hd * 0.6, b.floor_meters + kRubbleHeight + 0.6), tilted, tilted_up, v3, {std::min(hw * 0.45, 3.0), 0.12, std::min(hd * 0.2, 1.2)});
        parts.push_back(std::move(slab));
      }
      if (const auto c = chimney_plan(b)) {
        MeshPart chimney; chimney.color = charred(kind == BuildingKind::Bungalow ? math::Vec3{0.70, 0.42, 0.32} : math::Vec3{0.62, 0.34, 0.26}, st.destroyed_by == "fire" ? 0.5 : 0.0);
        chimney.what = "chimney"; chimney.material = SurfaceMaterial::Brick;
        const double chimney_top = b.floor_meters + b.height_meters - 0.2;
        add_box(chimney, lift(*c, 0.5 * (b.floor_meters + chimney_top)), u3, kUp, v3, {0.3, 0.5 * (chimney_top - b.floor_meters), 0.3});
        parts.push_back(std::move(chimney));
      }
      // The footpath stays.
      {
        const math::Vec2 from = b.door, to = b.path_end;
        const double len = (to - from).length();
        const math::Vec2 dir = (to - from) / std::max(len, 1e-9), right{-dir.y, dir.x};
        const int n = std::max(1, static_cast<int>(std::ceil(len / 1.0)));
        const double half = 0.5 * b.spec.path_width_meters;
        std::vector<math::Vec3> left(n + 1), right_side(n + 1);
        for (int k = 0; k <= n; ++k) {
          const auto c = from + dir * (len * k / n);
          for (const double sgn : {-1.0, 1.0}) {
            const auto p = c + right * (sgn * half);
            const auto h = ground_(p.x, p.y);
            const double y = path_height_at(b, p, h ? *h : ground_(c.x, c.y).value_or(b.ground_max_meters), style_.surface_lift_meters);
            (sgn < 0 ? left : right_side)[k] = lift(p, y);
          }
        }
        for (int k = 0; k < n; ++k) add_quad(paths, left[k], right_side[k], right_side[k + 1], left[k + 1]);
      }
      continue;
    }
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
        // Between every pair of hole edges along the wall, the strip is solid
        // except where holes (stacked storey over storey, the door under a
        // window) cut it: pieces below, between and above them.
        std::vector<double> edges{-half_len, half_len};
        for (const auto& hole : holes) { edges.push_back(std::clamp(hole.s - hole.half_w, -half_len, half_len)); edges.push_back(std::clamp(hole.s + hole.half_w, -half_len, half_len)); }
        std::sort(edges.begin(), edges.end());
        for (std::size_t k = 0; k + 1 < edges.size(); ++k) {
          const double s0 = edges[k], s1 = edges[k + 1];
          if (s1 - s0 < 1e-6) continue;
          std::vector<std::pair<double, double>> cuts;
          for (const auto& hole : holes)
            if (hole.s - hole.half_w <= s0 + 1e-9 && hole.s + hole.half_w >= s1 - 1e-9) cuts.push_back({hole.y0, hole.y1});
          std::sort(cuts.begin(), cuts.end());
          double y = b.floor_meters;
          for (const auto& cut : cuts) { piece(s0, s1, y, cut.first); y = std::max(y, cut.second); }
          piece(s0, s1, y, walls_top);
        }
      }
      // The floor and the ceiling (the ceiling goes with a burned-out room or a fallen roof).
      MeshPart floor_part; floor_part.color = charred({0.62, 0.46, 0.28}, interior_damage); floor_part.what = "floor"; floor_part.material = SurfaceMaterial::Planks;
      add_box(floor_part, frame.at(0, 0, b.floor_meters - 0.05), u3, kUp, v3, {hw - kWall + 0.02, 0.05, hd - kWall + 0.02});
      parts.push_back(std::move(floor_part));
      if (interior_damage < 1.0 && !st.roof_fallen) {
        MeshPart ceiling; ceiling.color = charred({0.88, 0.86, 0.80}, interior_damage); ceiling.what = "ceiling"; ceiling.material = SurfaceMaterial::Plain;
        add_box(ceiling, frame.at(0, 0, walls_top - 0.05), u3, kUp, v3, {hw - kWall + 0.02, 0.05, hd - kWall + 0.02});
        parts.push_back(std::move(ceiling));
      }
      // The rooms: partitions with their doorways, the flights and landings
      // with their rails, the upper floors round their holes, the furniture
      // — built for the buildings near the focus.
      const InteriorPlan& plan = plans_[index];
      if (detailed && !plan.walls.empty()) {
        MeshPart partitions; partitions.color = charred({0.90, 0.88, 0.82}, walls_damage); partitions.what = "partition"; partitions.material = SurfaceMaterial::Plain;
        MeshPart core; core.color = charred({0.66, 0.65, 0.62}, walls_damage); core.what = "core"; core.material = SurfaceMaterial::Concrete;
        for (const auto& w : plan.walls) {
          MeshPart& part = w.material == SurfaceMaterial::Concrete ? core : partitions;
          const math::Vec2 run = w.to - w.from;
          const double len = run.length();
          if (len < 1e-9) continue;
          const math::Vec2 dir = run / len;
          const math::Vec3 along = plan3(b.along * dir.x + b.facing * dir.y), normal = kUp.cross(along).normalized();
          const auto piece = [&](double s0, double s1, double y0, double y1) {
            if (s1 - s0 < 1e-6 || y1 - y0 < 1e-6) return;
            const math::Vec2 c = w.from + dir * (0.5 * (s0 + s1));
            add_box(part, frame.at(c.x, c.y, b.floor_meters + 0.5 * (y0 + y1)), along, kUp, normal, {0.5 * (s1 - s0), 0.5 * (y1 - y0), 0.5 * w.thickness});
          };
          // Strips between the doorways' edges; in a strip the doorways (one per floor, maybe) cut it in height order.
          std::vector<double> edges{0.0, len};
          for (const auto& o : w.openings) { edges.push_back(std::clamp(o.along - 0.5 * o.width, 0.0, len)); edges.push_back(std::clamp(o.along + 0.5 * o.width, 0.0, len)); }
          std::sort(edges.begin(), edges.end());
          for (std::size_t k = 0; k + 1 < edges.size(); ++k) {
            const double s0 = edges[k], s1 = edges[k + 1];
            if (s1 - s0 < 1e-6) continue;
            std::vector<std::pair<double, double>> cuts;
            for (const auto& o : w.openings)
              if (o.along - 0.5 * o.width <= s0 + 1e-9 && o.along + 0.5 * o.width >= s1 - 1e-9) cuts.push_back({o.bottom, o.bottom + o.height});
            std::sort(cuts.begin(), cuts.end());
            double y = w.bottom;
            for (const auto& cut : cuts) { piece(s0, s1, y, cut.first); y = std::max(y, cut.second); }
            piece(s0, s1, y, w.top);
          }
        }
        if (!partitions.indices.empty()) parts.push_back(std::move(partitions));
        if (!core.indices.empty()) parts.push_back(std::move(core));
      }
      if (detailed && !plan.flights.empty()) {
        const bool timber_stair = kind != BuildingKind::Skyscraper;
        MeshPart steps; steps.color = timber_stair ? charred({0.55, 0.38, 0.22}, interior_damage) : charred({0.62, 0.61, 0.58}, walls_damage);
        steps.what = "stair"; steps.material = timber_stair ? SurfaceMaterial::Timber : SurfaceMaterial::Concrete;
        for (const auto& flight : plan.flights) {
          const math::Vec2 run = flight.head - flight.foot;
          const double len = run.length();
          const math::Vec2 dir = run / std::max(len, 1e-9);
          const math::Vec3 along = plan3(b.along * dir.x + b.facing * dir.y), across = kUp.cross(along).normalized();
          const int count = std::max(1, static_cast<int>(std::ceil(flight.rise / 0.19)));
          const double riser = flight.rise / count, tread = len / count;
          // A solid step from the flight's base up to its tread (a closed string stair); a flight above another stands on a slab under its steps.
          const double under = flight.base - (flight.base > 0 ? 0.3 : 0.0);
          for (int k = 0; k < count; ++k) {
            const math::Vec2 c = flight.foot + dir * ((k + 0.5) * tread);
            const double top = flight.base + (k + 1) * riser;
            add_box(steps, frame.at(c.x, c.y, b.floor_meters + 0.5 * (under + top)), along, kUp, across, {0.5 * tread, 0.5 * (top - under), 0.5 * flight.width});
          }
        }
        for (const auto& l : plan.landings)
          add_box(steps, frame.at(0.5 * (l.a0 + l.a1), 0.5 * (l.b0 + l.b1), b.floor_meters + l.height - 0.15), u3, kUp, v3, {0.5 * (l.a1 - l.a0), 0.15, 0.5 * (l.b1 - l.b0)});
        // The rails: posts every metre standing on whatever surface is under them, a rail following it.
        const auto height_at = [&](math::Vec2 p) {
          const auto here = surfaces_at(plan, p.x, p.y);
          double best = 0.0;
          for (const auto& f : plan.flights) if (const auto h = flight_height_at(f, p.x, p.y)) best = std::max(best, *h);
          if (best > 0) return best;
          for (const double h : here) best = std::max(best, h);
          return best;
        };
        for (const auto& r : plan.rails) {
          const math::Vec2 rd = r.to - r.from;
          const double rl = rd.length();
          if (rl < 1e-9) continue;
          const math::Vec2 rdir = rd / rl;
          const math::Vec3 rail_along = plan3(b.along * rdir.x + b.facing * rdir.y), rail_across = kUp.cross(rail_along).normalized();
          const int posts = std::max(2, static_cast<int>(std::ceil(rl / 1.0)));
          for (int k = 0; k <= posts; ++k) {
            const math::Vec2 p = r.from + rdir * (rl * k / posts);
            add_box(steps, frame.at(p.x, p.y, b.floor_meters + height_at(p) + 0.45), rail_along, kUp, rail_across, {0.03, 0.45, 0.03});
          }
          for (int k = 0; k < posts; ++k) {
            const math::Vec2 p0 = r.from + rdir * (rl * k / posts), p1 = r.from + rdir * (rl * (k + 1) / posts);
            const math::Vec3 e0 = frame.at(p0.x, p0.y, b.floor_meters + height_at(p0) + 0.9), e1 = frame.at(p1.x, p1.y, b.floor_meters + height_at(p1) + 0.9);
            const math::Vec3 seg = e1 - e0;
            const double sl = seg.length();
            if (sl < 1e-6) continue;
            const math::Vec3 sa = seg / sl, sup = rail_across.cross(sa).normalized();
            add_box(steps, (e0 + e1) * 0.5, sa, sup, rail_across, {0.5 * sl, 0.035, 0.035});
          }
        }
        parts.push_back(std::move(steps));
      }
      if (detailed && plan.floors > 1) {
        // The upper floors: slabs round their holes (their undersides the ceilings of the floors below).
        MeshPart upper; upper.color = charred({0.62, 0.46, 0.28}, interior_damage); upper.what = "floor"; upper.material = SurfaceMaterial::Planks;
        const double iw = hw - kWall, id = hd - kWall;
        struct Rect { double a0, a1, b0, b1; };
        for (int k = 1; k < plan.floors; ++k) {
          std::vector<Rect> slabs{{-iw, iw, -id, id}};
          for (const auto& h : plan.holes) {
            if (h.level != k) continue;
            std::vector<Rect> next;
            for (const auto& r : slabs) {
              if (h.a1 <= r.a0 || h.a0 >= r.a1 || h.b1 <= r.b0 || h.b0 >= r.b1) { next.push_back(r); continue; }
              const double ha0 = std::max(h.a0, r.a0), ha1 = std::min(h.a1, r.a1), hb0 = std::max(h.b0, r.b0), hb1 = std::min(h.b1, r.b1);
              if (ha0 > r.a0) next.push_back({r.a0, ha0, r.b0, r.b1});
              if (ha1 < r.a1) next.push_back({ha1, r.a1, r.b0, r.b1});
              if (hb0 > r.b0) next.push_back({ha0, ha1, r.b0, hb0});
              if (hb1 < r.b1) next.push_back({ha0, ha1, hb1, r.b1});
            }
            slabs = std::move(next);
          }
          const double slab_y = b.floor_meters + plan.level(k) - 0.5 * plan.slab;
          for (const auto& r : slabs)
            if (r.a1 - r.a0 > 1e-6 && r.b1 - r.b0 > 1e-6)
              add_box(upper, frame.at(0.5 * (r.a0 + r.a1), 0.5 * (r.b0 + r.b1), slab_y), u3, kUp, v3, {0.5 * (r.a1 - r.a0), 0.5 * plan.slab, 0.5 * (r.b1 - r.b0)});
        }
        parts.push_back(std::move(upper));
      }
      // The furniture, a part per kind of piece (gone once the room has burned out).
      const int char_level = static_cast<int>(std::floor(std::clamp(interior_damage, 0.0, 1.0) * 10.0 + 1e-9));
      if (detailed && interior_damage < 1.0)
        for (const auto& f : furnish(b.spec)) {
          if (!f.drawn) continue;
          const std::string key = std::string(f.what) + "#" + std::to_string(char_level) + "#" + surface_material_name(f.material) + "#" +
                                  std::to_string(static_cast<int>(f.color.x * 255)) + "," + std::to_string(static_cast<int>(f.color.y * 255)) + "," + std::to_string(static_cast<int>(f.color.z * 255));
          auto it = std::find(furniture_keys.begin(), furniture_keys.end(), key);
          if (it == furniture_keys.end()) {
            MeshPart part; part.color = charred(f.color, interior_damage); part.what = f.what; part.material = f.material;
            furniture.push_back(std::move(part));
            furniture_keys.push_back(key);
            it = furniture_keys.end() - 1;
          }
          add_box(furniture[static_cast<std::size_t>(it - furniture_keys.begin())], frame.at(f.centre.x, f.centre.y, b.floor_meters + f.bottom + 0.5 * f.height), u3, kUp, v3,
                  {f.half_along, 0.5 * f.height, f.half_across});
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
    MeshPart windows; windows.color = window; windows.what = kind == BuildingKind::Bungalow ? "opening" : windows_burst ? "burst" : "window";
    windows.material = kind == BuildingKind::Bungalow || windows_burst ? SurfaceMaterial::Plain : SurfaceMaterial::Window;
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
    else if (st.element(BuildingElement::Windows).present)
      // Glazed: a pane in each hole, in the wall's centre plane (the bungalow's holes stay open).
      for (const auto& pane : panes) {
        const Face& face = faces[pane.face];
        const math::Vec3 across = kUp.cross(face.normal).normalized();
        const math::Vec3 fc = face_centre(face) - face.normal * (0.5 * kWall);
        add_box(windows, math::Vec3{fc.x, pane.y, fc.z} + across * pane.along, across, kUp, face.normal, {pane.half_w, pane.half_h, 0.01});
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
    if (!st.roof_fallen) parts.push_back(std::move(roof_part));  // burned through, smashed or stripped: open to the sky
    if (!windows.indices.empty()) parts.push_back(std::move(windows));  // burst: dark holes
    if (!trim_part.indices.empty()) parts.push_back(std::move(trim_part));
    parts.push_back(std::move(door_part));
  }
  if (!paths.indices.empty()) parts.push_back(std::move(paths));
  if (!foundations.indices.empty()) parts.push_back(std::move(foundations));
  if (!rubble.indices.empty()) parts.push_back(std::move(rubble));
  if (!ashes.indices.empty()) parts.push_back(std::move(ashes));
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
