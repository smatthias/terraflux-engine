#include <terraflux/graphics/city.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRampReach = 110.0;       // the ramps leave the highway this far from the crossing
constexpr double kViaductShoulder = 80.0;  // the viaduct starts rising this far from the crossing
constexpr double kViaductMid = 40.0;       // ... and is at its mid height here
constexpr double kArterialReach = 60.0;    // the ramps join the avenue this far from the crossing
constexpr double kInterchangeRoom = kRampReach + 30.0;  // a straight run needs this much either side

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool finite(math::Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
math::Vec2 rotate(math::Vec2 v, double radians) { const double c = std::cos(radians), s = std::sin(radians); return {v.x * c - v.y * s, v.x * s + v.y * c}; }
math::Vec2 right_of(math::Vec2 dir) { return {-dir.y, dir.x}; }
double hash01(std::uint32_t a, std::uint32_t b, std::uint32_t seed) {
  std::uint32_t h = a * 747796405u + b * 2891336453u + seed * 277803737u + 0x9E3779B9u;
  h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
  return static_cast<double>(h) / 4294967296.0;
}

struct Bounds {
  math::Vec2 lower, upper;
  bool inside(math::Vec2 p) const { return p.x >= lower.x && p.x <= upper.x && p.y >= lower.y && p.y <= upper.y; }
};

// A zone's street lattice: lines i (along the local v axis) and j (along u), warped by a sine field.
struct Lattice {
  std::vector<RoadRoute> routes;
  std::vector<math::Vec2> nodes;  // the intersections
};
Lattice build_lattice(const CityZone& zone, double block, int avenue_every, const CityPlan& plan, const Bounds& bounds, std::uint32_t salt, double warp_scale) {
  Lattice out;
  const int n = std::max(1, static_cast<int>(std::ceil(zone.radius_meters / block)));
  const double amp = plan.warp_amplitude_meters * warp_scale, lambda = plan.warp_wavelength_meters;
  const auto phase = [&](int line, std::uint32_t which) { return 2.0 * kPi * hash01(static_cast<std::uint32_t>(line + 1000), which + salt * 7u, plan.seed); };
  // A point of the lattice at (possibly fractional) indices.
  const auto point = [&](double i, double j) -> std::optional<math::Vec2> {
    const double x = i * block + amp * std::sin(j * block / lambda + phase(static_cast<int>(std::floor(i + 0.5)), 1));
    const double z = j * block + amp * std::sin(i * block / lambda + phase(static_cast<int>(std::floor(j + 0.5)), 2));
    if (std::hypot(i * block, j * block) > zone.radius_meters + 1e-9) return std::nullopt;
    const math::Vec2 p = zone.centre + rotate({x, z}, zone.rotation_radians);
    if (!bounds.inside(p)) return std::nullopt;
    return p;
  };
  const auto kept = [&](int i, int j) { return point(i, j).has_value(); };
  for (int i = -n; i <= n; ++i) for (int j = -n; j <= n; ++j) if (const auto p = point(i, j)) out.nodes.push_back(*p);
  const auto cls_of = [&](int line) { return avenue_every > 0 && ((line % avenue_every) + avenue_every) % avenue_every == 0 ? RoadClass::Avenue : RoadClass::Street; };
  // Lines of constant i run along j, lines of constant j along i; runs of kept nodes become routes with half-block points between for the curve.
  for (int fixed = -n; fixed <= n; ++fixed)
    for (const bool along_j : {true, false}) {
      RoadRoute route;
      route.cls = cls_of(fixed);
      const auto flush = [&] { if (route.points.size() >= 2) out.routes.push_back(route); route.points.clear(); };
      for (int k = -n; k <= n; ++k) {
        const int i = along_j ? fixed : k, j = along_j ? k : fixed;
        if (!kept(i, j)) { flush(); continue; }
        if (!route.points.empty()) {
          const auto mid = along_j ? point(i, j - 0.5) : point(i - 0.5, j);
          if (mid) route.points.push_back(*mid);
        }
        route.points.push_back(*point(i, j));
      }
      flush();
    }
  return out;
}

// The airport: runway, taxiway, apron and where its buildings want to stand.
struct AirportLayout {
  std::vector<RoadRoute> routes;
  std::vector<math::Vec2> connections;  // the apron's ends
  struct Want { BuildingKind kind; math::Vec2 at; math::Vec2 facing; };  // a building's desired centre, facing the apron
  std::vector<Want> wants;
  math::Vec2 apron_a, apron_b;
  CityClearing clearing;  // the airfield, kept clear of plants
};
AirportLayout build_airport(const CityZone& zone, const CityPlan& plan, const RoadStyle& style, const Bounds& bounds) {
  AirportLayout out;
  const math::Vec2 u = rotate({1, 0}, zone.rotation_radians), v = right_of(u);
  const double length = std::min(plan.runway_length_meters, 2.0 * zone.radius_meters * 0.95);
  const double half = 0.5 * length;
  const double taxi_off = 0.5 * style.runway_width_meters + 25.0 + 0.5 * style.taxiway_width_meters;
  const double apron_off = taxi_off + 0.5 * style.taxiway_width_meters + 10.0 + 0.5 * style.apron_width_meters;
  const double link = std::min(60.0, 0.2 * length);
  const math::Vec2 c = zone.centre;
  RoadRoute runway; runway.cls = RoadClass::Runway;
  runway.points = {c - u * half, c - u * (half - link), c + u * (half - link), c + u * half};
  RoadRoute taxiway; taxiway.cls = RoadClass::Taxiway;
  taxiway.points = {c - u * (half - link) + v * taxi_off, c + v * taxi_off, c + u * (half - link) + v * taxi_off};
  RoadRoute link_a; link_a.cls = RoadClass::Taxiway; link_a.points = {c - u * (half - link), c - u * (half - link) + v * taxi_off};
  RoadRoute link_b; link_b.cls = RoadClass::Taxiway; link_b.points = {c + u * (half - link), c + u * (half - link) + v * taxi_off};
  const double apron_half = std::min(150.0, half - link - 20.0);
  RoadRoute apron; apron.cls = RoadClass::Apron;
  out.apron_a = c - u * apron_half + v * apron_off; out.apron_b = c + u * apron_half + v * apron_off;
  apron.points = {out.apron_a, c + v * apron_off, out.apron_b};
  RoadRoute stand; stand.cls = RoadClass::Taxiway; stand.points = {c + v * taxi_off, c + v * apron_off};
  for (auto* r : {&runway, &taxiway, &link_a, &link_b, &apron, &stand})
    for (const auto& p : r->points) if (!bounds.inside(p)) throw std::invalid_argument("the airport does not fit the terrain: " + zone.name);
  out.routes = {runway, taxiway, link_a, link_b, apron, stand};
  out.connections = {out.apron_a, out.apron_b};
  out.clearing = {c + v * (0.5 * apron_off), u, half + 40.0, 0.5 * apron_off + 0.5 * style.apron_width_meters + 30.0};
  // Buildings on the apron's far side (facing the apron, away from the runway): a hangar, the terminal, the tower, a hangar.
  const double far = apron_off + 0.5 * style.apron_width_meters;
  const auto want = [&](BuildingKind kind, double along) {
    const auto spec = building_spec(kind);
    out.wants.push_back({kind, c + u * along + v * (far + spec.setback_meters + 0.5 * spec.depth_meters), v * -1.0});
  };
  want(BuildingKind::Hangar, -apron_half + 30.0);
  want(BuildingKind::Terminal, 0.0);
  want(BuildingKind::ControlTower, 58.0);
  want(BuildingKind::Hangar, apron_half - 30.0);
  return out;
}

// A highway corridor as pieces: straight runs and the arcs that fillet the corners.
struct HighwayPiece { bool straight = true; std::vector<math::Vec2> points; std::vector<double> lifts; };
std::vector<HighwayPiece> fillet(const std::vector<math::Vec2>& given, double radius) {
  std::vector<HighwayPiece> pieces;
  if (given.size() < 2) throw std::invalid_argument("a highway needs at least two corners");
  // A closed loop (first corner repeated last) is filleted at every corner, the closing one included:
  // the run is rotated to start at its second corner so the first corner is interior.
  std::vector<math::Vec2> corners = given;
  const bool closed = given.size() >= 4 && (given.front() - given.back()).length() < 1e-6;
  if (closed) {
    corners.assign(given.begin() + 1, given.end());  // drop the repeat ...
    corners.push_back(given[1]);                      // ... and close again on the second corner, making the old first one interior
  }
  math::Vec2 run_start = corners.front();
  for (std::size_t k = 1; k + 1 < corners.size(); ++k) {
    const math::Vec2 prev = corners[k - 1], corner = corners[k], next = corners[k + 1];
    const math::Vec2 d1 = (corner - prev).normalized(), d2 = (next - corner).normalized();
    const double cos_turn = std::clamp(d1.dot(d2), -1.0, 1.0);
    const double turn = std::acos(cos_turn);
    if (turn < 1e-3) continue;  // straight on
    double r = radius;
    const double limit = 0.45 * std::min((corner - prev).length(), (next - corner).length());
    double tangent = r * std::tan(0.5 * turn);
    if (tangent > limit) { tangent = limit; r = tangent / std::tan(0.5 * turn); }
    const math::Vec2 start = corner - d1 * tangent, end = corner + d2 * tangent;
    pieces.push_back({true, {run_start, start}, {0, 0}});
    // The arc: centre on the inside of the turn.
    const double side = d1.x * d2.y - d1.y * d2.x > 0 ? 1.0 : -1.0;
    const math::Vec2 centre = start + right_of(d1) * (side * r);
    const int steps = std::max(2, static_cast<int>(std::ceil(turn * r / 12.0)));
    HighwayPiece arc; arc.straight = false;
    for (int s = 0; s <= steps; ++s) {
      const double a = turn * s / steps * side;
      const math::Vec2 rel = start - centre;
      arc.points.push_back(centre + rotate(rel, a));
      arc.lifts.push_back(0.0);
    }
    arc.points.front() = start; arc.points.back() = end;
    pieces.push_back(arc);
    run_start = end;
  }
  if (closed && pieces.size() >= 2 && !pieces.back().straight) {
    // Close the loop: the first straight run starts where the last arc ends (the same corner).
    const math::Vec2 last_end = pieces.back().points.back();
    pieces.front().points.front() = last_end;
    return pieces;
  }
  pieces.push_back({true, {run_start, corners.back()}, {0, 0}});
  return pieces;
}

struct Interchange {
  math::Vec2 at, highway_dir, normal;  // the crossing, the highway's direction, the avenue's direction (toward the zone is -normal)
  math::Vec2 h1, h2, a1, a2;
};
// Inserts a diamond interchange into a straight piece at `along` from its start.
Interchange insert_interchange(HighwayPiece& piece, double along, double lift) {
  const math::Vec2 p0 = piece.points.front(), p1 = piece.points.back();
  const math::Vec2 d = (p1 - p0).normalized();
  const math::Vec2 at = p0 + d * along;
  std::vector<std::pair<double, double>> marks = {{-kRampReach, 0.0}, {-kViaductShoulder, 0.0}, {-kViaductMid, 0.62 * lift}, {0.0, lift}, {kViaductMid, 0.62 * lift}, {kViaductShoulder, 0.0}, {kRampReach, 0.0}};
  // The piece may already hold interchanges: merge the new waypoints in order along the run.
  std::vector<std::pair<double, double>> all;
  for (std::size_t k = 0; k < piece.points.size(); ++k) all.push_back({(piece.points[k] - p0).dot(d), piece.lifts[k]});
  for (const auto& m : marks) all.push_back({along + m.first, m.second});
  std::sort(all.begin(), all.end());
  piece.points.clear(); piece.lifts.clear();
  for (const auto& [t, l] : all) {
    if (!piece.points.empty() && std::abs((piece.points.back() - p0).dot(d) - t) < 0.5) { piece.lifts.back() = std::max(piece.lifts.back(), l); continue; }
    piece.points.push_back(p0 + d * t); piece.lifts.push_back(l);
  }
  Interchange ic;
  ic.at = at; ic.highway_dir = d; ic.normal = right_of(d);
  ic.h1 = at - d * kRampReach; ic.h2 = at + d * kRampReach;
  ic.a1 = at - ic.normal * kArterialReach; ic.a2 = at + ic.normal * kArterialReach;
  return ic;
}
// A ramp from a highway point to an avenue point, bending round the quadrant's corner.
RoadRoute ramp(math::Vec2 from, math::Vec2 to, math::Vec2 corner) {
  RoadRoute r; r.cls = RoadClass::Ramp;
  const int steps = 7;
  for (int s = 0; s <= steps; ++s) {
    const double t = static_cast<double>(s) / steps, u = 1.0 - t;
    r.points.push_back(from * (u * u) + corner * (2.0 * u * t) + to * (t * t));
  }
  return r;
}

}  // namespace

const char* zone_kind_name(ZoneKind kind) {
  switch (kind) {
    case ZoneKind::Cbd: return "cbd";
    case ZoneKind::Suburb: return "suburb";
    case ZoneKind::Airport: return "airport";
  }
  throw std::invalid_argument("unknown zone kind");
}

void validate_city_plan(const CityPlan& p) {
  if (!finite(p.lower) || !finite(p.upper) || p.upper.x <= p.lower.x || p.upper.y <= p.lower.y) throw std::invalid_argument("city bounds must be finite and ordered");
  if (!within(p.bounds_margin_meters, 0, 1000) || !within(p.cbd_block_meters, 60, 400) || !within(p.suburb_block_meters, 40, 400))
    throw std::invalid_argument("city margin must be 0..1000 m and blocks 60..400 m (CBD) and 40..400 m (suburb)");
  if (!within(p.warp_amplitude_meters, 0, 60) || !within(p.warp_wavelength_meters, 50, 5000) || !within(p.fillet_radius_meters, 20, 2000))
    throw std::invalid_argument("warp amplitude must be 0..60 m, wavelength 50..5000 m and fillet radius 20..2000 m");
  if (!within(p.interchange_lift_meters, 4, 20) || !within(p.runway_length_meters, 100, 4000)) throw std::invalid_argument("interchange lift must be 4..20 m and runway length 100..4000 m");
  if (p.warp_amplitude_meters * 2.0 >= 0.5 * std::min(p.cbd_block_meters, p.suburb_block_meters)) throw std::invalid_argument("the warp would let lattice lines meet");
  std::size_t airports = 0;
  for (const auto& z : p.zones) {
    (void)zone_kind_name(z.kind);
    if (!finite(z.centre) || !within(z.radius_meters, 50, 5000) || !std::isfinite(z.rotation_radians)) throw std::invalid_argument("zone " + z.name + " must have a finite centre, a radius of 50..5000 m and a rotation");
    if (z.name.empty()) throw std::invalid_argument("every zone needs a name");
    if (z.kind == ZoneKind::Airport) ++airports;
  }
  if (airports > 1) throw std::invalid_argument("one airport at most");
  for (const auto& h : p.highways) {
    if (h.size() < 2) throw std::invalid_argument("a highway needs at least two corners");
    for (const auto& c : h) if (!finite(c)) throw std::invalid_argument("highway corners must be finite");
  }
}

CityLayout plan_city(const CityPlan& plan, const StandingHeightFn& ground, std::optional<double> water_level, const RoadStyle& road_style,
                     const RoadPlanStyle& plan_style) {
  validate_city_plan(plan);
  validate_road_style(road_style);
  if (!ground) throw std::invalid_argument("planning a city needs a ground function");
  const Bounds bounds{plan.lower + math::Vec2{plan.bounds_margin_meters, plan.bounds_margin_meters}, plan.upper - math::Vec2{plan.bounds_margin_meters, plan.bounds_margin_meters}};
  CityLayout out;
  std::vector<RoadRoute> routes;
  // Zones: lattices and the airport.
  struct ZoneBuilt { const CityZone* zone; std::vector<math::Vec2> connections; std::optional<AirportLayout> airport; };
  std::vector<ZoneBuilt> built;
  for (std::size_t z = 0; z < plan.zones.size(); ++z) {
    const auto& zone = plan.zones[z];
    ZoneBuilt zb{&zone, {}, std::nullopt};
    if (zone.kind == ZoneKind::Airport) {
      zb.airport = build_airport(zone, plan, road_style, bounds);
      for (const auto& r : zb.airport->routes) routes.push_back(r);
      zb.connections = zb.airport->connections;
      out.clearings.push_back(zb.airport->clearing);
      ++out.stats.airports;
    } else {
      const bool cbd = zone.kind == ZoneKind::Cbd;
      // Small zones get smaller blocks so that at least two blocks span them.
      const double block = std::min(cbd ? plan.cbd_block_meters : plan.suburb_block_meters, std::max(40.0, 0.55 * zone.radius_meters));
      const auto lattice = build_lattice(zone, block, cbd ? 3 : 0, plan, bounds, static_cast<std::uint32_t>(z), cbd ? 0.4 : 1.0);  // the CBD's blocks stay nearly square for its towers
      if (lattice.routes.empty()) throw std::invalid_argument("zone " + zone.name + " has no room for a street lattice within the bounds");
      for (const auto& r : lattice.routes) routes.push_back(r);
      zb.connections = lattice.nodes;
      if (cbd) ++out.stats.cbds; else ++out.stats.suburbs;
    }
    ++out.stats.zones;
    built.push_back(std::move(zb));
  }
  // Highways: filleted corridors, then an interchange (or an at-grade junction) for every zone.
  std::vector<std::vector<HighwayPiece>> highways;
  for (const auto& h : plan.highways) {
    for (const auto& c : h) if (!bounds.inside(c)) throw std::invalid_argument("a highway corner lies outside the city bounds");
    highways.push_back(fillet(h, plan.fillet_radius_meters));
  }
  std::vector<Interchange> interchanges;
  struct Placed { std::size_t highway, piece; double along; double extent; };  // half the length of run a junction takes up
  std::vector<Placed> placed_interchanges;  // interchanges and T-junctions
  // Where a highway ends on another's straight run it joins it there (a T-junction at grade);
  // an end near another highway's arc is snapped to the arc's nearest waypoint.
  for (std::size_t h = 0; h < highways.size(); ++h)
    for (const bool front : {true, false}) {
      math::Vec2 end = front ? plan.highways[h].front() : plan.highways[h].back();
      bool on_straight = false;
      for (std::size_t o = 0; o < highways.size() && !on_straight; ++o) {
        if (o == h) continue;
        for (const auto& piece : highways[o]) {
          if (!piece.straight) continue;
          const math::Vec2 p0 = piece.points.front(), p1 = piece.points.back();
          const double len = (p1 - p0).length();
          const math::Vec2 d = (p1 - p0) / len;
          const double along = (end - p0).dot(d);
          if (along >= 1.0 && along <= len - 1.0 && std::abs((end - p0).dot(right_of(d))) <= 1.0) on_straight = true;
        }
      }
      if (!on_straight) {
        double best = 60.0;
        std::optional<math::Vec2> snap;
        for (std::size_t o = 0; o < highways.size(); ++o) {
          if (o == h) continue;
          for (const auto& piece : highways[o]) for (const auto& q : piece.points) { const double d = (q - end).length(); if (d < best) { best = d; snap = q; } }
        }
        if (snap) {
          auto& own = front ? highways[h].front() : highways[h].back();
          (front ? own.points.front() : own.points.back()) = *snap;
        }
      }
      for (std::size_t o = 0; o < highways.size(); ++o) {
        if (o == h) continue;
        for (std::size_t pi = 0; pi < highways[o].size(); ++pi) {
          auto& piece = highways[o][pi];
          if (!piece.straight) continue;
          const math::Vec2 p0 = piece.points.front(), p1 = piece.points.back();
          const double len = (p1 - p0).length();
          const math::Vec2 d = (p1 - p0) / len;
          const double along = (end - p0).dot(d);
          if (along < 1.0 || along > len - 1.0 || std::abs((end - p0).dot(right_of(d))) > 1.0) continue;
          placed_interchanges.push_back({o, pi, along, 40.0});
          std::vector<std::pair<double, double>> all;
          for (std::size_t k = 0; k < piece.points.size(); ++k) all.push_back({(piece.points[k] - p0).dot(d), piece.lifts[k]});
          all.push_back({along, 0.0});
          std::sort(all.begin(), all.end());
          piece.points.clear(); piece.lifts.clear();
          for (const auto& [t, l] : all) {
            if (!piece.points.empty() && std::abs((piece.points.back() - p0).dot(d) - t) < 0.5) continue;
            piece.points.push_back(t == along ? end : p0 + d * t); piece.lifts.push_back(l);
          }
        }
      }
    }
  std::vector<const ZoneBuilt*> connected;  // zones joined to the highways (or to a connected zone) so far
  for (const auto& zb : built) {
    if (zb.connections.empty()) continue;
    if (highways.empty() && connected.empty()) { connected.push_back(&zb); continue; }  // the first zone of a city without highways stands alone
    const auto& zone = *zb.zone;
    // The nearest straight run with room, not conflicting with an interchange already there.
    struct Candidate { double distance; std::size_t highway, piece; double along; };
    std::vector<Candidate> candidates;
    for (std::size_t h = 0; h < highways.size(); ++h)
      for (std::size_t k = 0; k < highways[h].size(); ++k) {
        const auto& piece = highways[h][k];
        if (!piece.straight) continue;
        const math::Vec2 p0 = piece.points.front(), p1 = piece.points.back();
        const double len = (p1 - p0).length();
        if (len < 2.0 * kInterchangeRoom + 20.0) continue;
        const math::Vec2 d = (p1 - p0) / len;
        std::vector<double> alongs{std::clamp((zone.centre - p0).dot(d), kInterchangeRoom, len - kInterchangeRoom)};
        for (double a = kInterchangeRoom; a <= len - kInterchangeRoom + 1e-9; a += 50.0) alongs.push_back(a);
        for (const double along : alongs) {
          const math::Vec2 at = p0 + d * along;
          if ((at - zone.centre).length() < zone.radius_meters + 20.0) continue;   // the highway passes through the zone here
          double distance = std::numeric_limits<double>::infinity();  // from the zone's nearest node: the access avenue's length
          for (const auto& c : zb.connections) distance = std::min(distance, (c - at).length());
          if (distance > 450.0) continue;  // too far for an access avenue: the zone joins at grade instead
          // The viaduct's run and the ramps' quadrant on the zone's side stand on dry ground within the
          // bounds (the far side may fall away: then it is a half diamond).
          const double near_side = (zone.centre - at).dot(right_of(d)) > 0 ? kArterialReach : -kArterialReach;
          bool dry = true;
          for (const double t : {-kInterchangeRoom, -kRampReach, -kViaductMid, 0.0, kViaductMid, kRampReach, kInterchangeRoom})
            for (const double side : {0.0, near_side}) {
              const math::Vec2 q = at + d * t + right_of(d) * side;
              const auto h_ground = ground(q.x, q.y);
              if (!h_ground || (water_level && *h_ground <= *water_level + 1.0) || !bounds.inside(q)) dry = false;
            }
          if (!dry) continue;
          candidates.push_back({distance, h, k, along});
        }
      }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    // The access avenue may not cut across another highway or another zone.
    const auto avenue_clear = [&](math::Vec2 from, math::Vec2 to, std::size_t own_highway, std::size_t own_piece) {
      for (int k = 0; k <= 10; ++k) {
        const math::Vec2 q = from + (to - from) * (k / 10.0);
        for (std::size_t h = 0; h < highways.size(); ++h)
          for (std::size_t pi = 0; pi < highways[h].size(); ++pi) {
            if (h == own_highway && pi == own_piece) continue;
            const auto& pts = highways[h][pi].points;
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
              const math::Vec2 d = pts[i + 1] - pts[i];
              const double len = d.length();
              if (len < 1e-9) continue;
              const double along = std::clamp((q - pts[i]).dot(d) / len, 0.0, len);
              if ((pts[i] + d * (along / len) - q).length() < 25.0) return false;
            }
          }
        for (const auto& other : built) if (other.zone != &zone && (q - other.zone->centre).length() < other.zone->radius_meters + 10.0) return false;
      }
      return true;
    };
    std::optional<Candidate> chosen;
    for (const auto& c : candidates) {
      bool conflict = false;
      for (const auto& p : placed_interchanges) if (p.highway == c.highway && p.piece == c.piece && std::abs(p.along - c.along) < p.extent + kRampReach + 15.0) conflict = true;
      // Nor inside another zone, nor with an avenue that cuts across another highway or zone.
      const auto& piece = highways[c.highway][c.piece];
      const math::Vec2 d = (piece.points.back() - piece.points.front()).normalized();
      const auto at = piece.points.front() + d * c.along;
      for (const auto& other : built) if (other.zone != &zone && (at - other.zone->centre).length() < other.zone->radius_meters + 30.0) conflict = true;
      if (!conflict) {
        const math::Vec2 n = right_of(d);
        const math::Vec2 near_a = at + n * ((zone.centre - at).dot(n) > 0 ? kArterialReach : -kArterialReach);
        math::Vec2 b = zb.connections.front();
        for (const auto& cn : zb.connections) if ((cn - near_a).length() < (b - near_a).length()) b = cn;
        if (!avenue_clear(b, near_a, c.highway, c.piece)) conflict = true;
      }
      if (!conflict) { chosen = c; break; }
    }
    if (chosen) {
      auto& piece = highways[chosen->highway][chosen->piece];
      const Interchange ic = insert_interchange(piece, chosen->along, plan.interchange_lift_meters);
      placed_interchanges.push_back({chosen->highway, chosen->piece, chosen->along, kRampReach + 15.0});
      // The avenue from the zone's nearest node to the near ramp junction, on under the viaduct to the far one.
      const bool zone_on_positive = (zone.centre - ic.at).dot(ic.normal) > 0;
      const math::Vec2 near_a = zone_on_positive ? ic.a2 : ic.a1, far_a = zone_on_positive ? ic.a1 : ic.a2;
      math::Vec2 b = zb.connections.front();
      for (const auto& c : zb.connections) if ((c - near_a).length() < (b - near_a).length()) b = c;
      // A full diamond needs room on the far side too; against the bounds it is a half diamond
      // (the avenue passes under the viaduct and stops, two ramps on the near side).
      const auto dry_inside = [&](math::Vec2 q) { const auto g = ground(q.x, q.y); return bounds.inside(q) && g && !(water_level && *g <= *water_level + 1.0); };
      const auto far_corner_ok = [&](math::Vec2 h) { return dry_inside(far_a) && dry_inside(ic.at + (h - ic.at) + (far_a - ic.at)); };
      const bool full = far_corner_ok(ic.h1) && far_corner_ok(ic.h2);
      RoadRoute avenue; avenue.cls = RoadClass::Avenue;
      if ((b - near_a).length() > 5.0) avenue.points.push_back(b);
      avenue.points.push_back(near_a);
      avenue.points.push_back(full ? far_a : ic.at + (far_a - ic.at) * (15.0 / kArterialReach));
      routes.push_back(avenue);
      for (const auto& h : {ic.h1, ic.h2}) {
        routes.push_back(ramp(h, near_a, ic.at + (h - ic.at) + (near_a - ic.at)));
        if (full) routes.push_back(ramp(h, far_a, ic.at + (h - ic.at) + (far_a - ic.at)));
      }
      interchanges.push_back(ic);
      out.interchanges.push_back({zone.name, ic.at, true});
      ++out.stats.interchanges;
    } else {
      // No room for an interchange: the shortest link from one of the zone's nodes to a dry highway
      // waypoint, an interchange's avenue end, or a node of a zone already connected (a link road).
      std::vector<math::Vec2> targets;
      for (const auto& hw : highways) for (const auto& piece : hw) for (std::size_t k = 0; k < piece.points.size(); ++k) {
        if (piece.lifts[k] > 0) continue;
        const auto h_ground = ground(piece.points[k].x, piece.points[k].y);
        if (!h_ground || (water_level && *h_ground <= *water_level + 1.0)) continue;
        targets.push_back(piece.points[k]);
      }
      for (const auto& ic : interchanges) { targets.push_back(ic.a1); targets.push_back(ic.a2); }
      for (const auto* other : connected) for (const auto& oc : other->connections) targets.push_back(oc);
      double best = std::numeric_limits<double>::infinity();
      math::Vec2 from = zb.connections.front(), target = from;
      for (const auto& t : targets)
        for (const auto& c : zb.connections) {
          const double d = (t - c).length();
          if (d < best) { best = d; target = t; from = c; }
        }
      if (!std::isfinite(best)) throw std::invalid_argument("zone " + zone.name + " cannot be joined to the rest of the city");
      RoadRoute link; link.cls = RoadClass::Avenue; link.points = {from, target};
      routes.push_back(link);
      out.interchanges.push_back({zone.name, target, false});
      ++out.stats.at_grade_junctions;
    }
    connected.push_back(&zb);
  }
  for (const auto& hw : highways) {
    RoadRoute route; route.cls = RoadClass::Highway;
    for (const auto& piece : hw)
      for (std::size_t k = 0; k < piece.points.size(); ++k) {
        if (!route.points.empty() && (route.points.back() - piece.points[k]).length() < 0.5) { route.lifts.back() = std::max(route.lifts.back(), piece.lifts[k]); continue; }
        route.points.push_back(piece.points[k]); route.lifts.push_back(piece.lifts[k]);
      }
    routes.push_back(route);
  }
  out.roads = plan_road_network(routes, ground, water_level, road_style, plan_style);
  // Lots along every block edge, by zone.
  const RoadNetwork network(out.roads, road_style, ground);
  const auto zone_of = [&](math::Vec2 p) -> const CityZone* {
    for (const auto& zb : built) if ((p - zb.zone->centre).length() <= zb.zone->radius_meters + 20.0) return zb.zone;
    return nullptr;
  };
  // Civic buildings first (automatic, near the zone centres), then the airport's, then the lots.
  for (const auto& zb : built) {
    const auto& zone = *zb.zone;
    if (zone.kind == ZoneKind::Cbd) {
      out.buildings.push_back({BuildingKind::Hospital, std::nullopt, zone.centre, zone.radius_meters});
      out.buildings.push_back({BuildingKind::PoliceStation, std::nullopt, zone.centre, zone.radius_meters});
    }
  }
  std::size_t suburb_index = 0;
  for (const auto& zb : built)
    if (zb.zone->kind == ZoneKind::Suburb && suburb_index++ % 2 == 0) out.buildings.push_back({BuildingKind::PoliceStation, std::nullopt, zb.zone->centre, zb.zone->radius_meters});
  for (const auto& zb : built) {
    if (!zb.airport) continue;
    for (const auto& w : zb.airport->wants) {
      // The apron segment nearest the wanted centre; the site is its projection and the side away from the runway.
      std::optional<std::uint32_t> seg; double best = std::numeric_limits<double>::infinity(); double along = 0; int side = 1;
      for (std::uint32_t i = 0; i < network.size(); ++i) {
        if (out.roads.segments[i].cls != RoadClass::Apron) continue;
        const auto a = out.roads.nodes[out.roads.segments[i].a].position;
        const auto dir = network.direction(i);
        const double t = std::clamp((w.at - a).dot(dir), 0.0, network.length(i));
        const auto foot = a + dir * t;
        const double d = (foot - w.at).length();
        if (d < best) { best = d; seg = i; along = t; side = (w.at - foot).dot(right_of(dir)) > 0 ? 1 : -1; }
      }
      if (seg) { BuildingRequest request; request.kind = w.kind; request.site = BuildingSite{*seg, along, side}; out.buildings.push_back(request); }
    }
  }
  for (std::uint32_t i = 0; i < network.size(); ++i) {
    const auto& seg = out.roads.segments[i];
    if (seg.kind != RoadKind::Road || (seg.cls != RoadClass::Street && seg.cls != RoadClass::Avenue)) continue;
    const auto a = out.roads.nodes[seg.a].position, b = out.roads.nodes[seg.b].position;
    const auto* zone = zone_of((a + b) * 0.5);
    if (!zone || zone->kind == ZoneKind::Airport) continue;
    const double len = network.length(i);
    const bool cbd = zone->kind == ZoneKind::Cbd;
    for (const int side : {1, -1}) {
      double along = 8.0;
      std::uint32_t k = 0;
      while (true) {
        BuildingKind kind = cbd ? BuildingKind::Skyscraper : (hash01(i, k * 2u + (side > 0 ? 1u : 0u), plan.seed) < 0.6 ? BuildingKind::House : BuildingKind::Bungalow);
        const double pitch = building_spec(kind).width_meters + (cbd ? 8.0 : 6.0);
        if (along + pitch > len - 8.0) break;
        BuildingRequest lot; lot.kind = kind; lot.site = BuildingSite{i, along + 0.5 * pitch, side};
        out.buildings.push_back(lot);
        ++out.stats.lots;
        along += pitch;
        ++k;
      }
    }
  }
  return out;
}

std::size_t clear_vegetation_in(VegetationPlacement& placement, std::span<const CityClearing> clearings) {
  if (clearings.empty()) return 0;
  const auto inside = [&](float px, float pz) {
    const math::Vec2 p{px, pz};
    for (const auto& c : clearings) {
      const math::Vec2 r = p - c.centre;
      if (std::abs(r.dot(c.along)) <= c.half_length && std::abs(r.dot(right_of(c.along))) <= c.half_width) return true;
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
      if (inside(t.position[0], t.position[2])) { ++removed; continue; }
      out.trees.push_back(t); ++kept.tree_count; out.trees_per_kind[t.kind > 0.5f ? 1 : 0]++;
    }
    for (std::uint32_t k = 0; k < c.grass_count; ++k) {
      const auto& gr = placement.grass[c.grass_first + k];
      if (inside(gr.position[0], gr.position[2])) { ++removed; continue; }  // an airfield is mown
      out.grass.push_back(gr); ++kept.grass_count;
    }
    if (kept.tree_count || kept.grass_count) out.chunks.push_back(kept);
  }
  placement = std::move(out);
  return removed;
}

CityPlan demo_city_plan(math::Vec2 centre, math::Vec2 lower, math::Vec2 upper) {
  CityPlan plan;
  plan.lower = lower; plan.upper = upper;
  const auto at = [&](double x, double z) { return centre + math::Vec2{x, z}; };
  plan.zones = {
      {ZoneKind::Cbd, "Central", at(-740, -660), 240.0, 0.12},
      {ZoneKind::Suburb, "Hillcrest", at(-350, -900), 170.0, -0.25},
      {ZoneKind::Suburb, "Westfield", at(-880, -280), 150.0, 0.3},
      {ZoneKind::Suburb, "Lakeside", at(-860, 170), 130.0, -0.15},
      {ZoneKind::Suburb, "Southbank", at(-840, 620), 130.0, 0.2},
      {ZoneKind::Suburb, "Riverside", at(-600, 690), 110.0, -0.3},
      {ZoneKind::Suburb, "Eastgate", at(-90, 720), 95.0, 0.35},
      {ZoneKind::Airport, "Airport", at(-275, 1000), 400.0, kPi},  // the runway east-west, the taxiway, apron and terminal on its northern side
  };
  // The western ring north to south, the lake crossing east, and a southern link that turns toward the airport.
  plan.highways = {
      {at(-1015, -1000), at(-1015, 1000)},
      {at(-1015, -100), at(200, -100)},
      {at(-1015, 470), at(-700, 470), at(-700, 880), at(-560, 880)},
  };
  return plan;
}

CityPlan generic_city_plan(math::Vec2 centre, math::Vec2 lower, math::Vec2 upper) {
  CityPlan plan;
  plan.lower = lower; plan.upper = upper;
  const double half = 0.5 * std::min(upper.x - lower.x, upper.y - lower.y) - plan.bounds_margin_meters;
  // A ring highway at half the extent; the CBD inside it, the suburbs and the airport outside.
  const double ring = 0.5 * half;
  plan.fillet_radius_meters = 120.0;
  plan.zones.push_back({ZoneKind::Cbd, "Central", centre, std::clamp(half * 0.26, 50.0, 300.0), 0.1});
  const double zone_r = std::clamp(half * 0.15, 50.0, 200.0);
  const double orbit = ring + zone_r + 80.0;
  const char* names[5] = {"Northside", "Eastfield", "Southbank", "Westgate", "Hillcrest"};
  for (int k = 0; k < 5; ++k) {
    const double a = -kPi / 2 + k * 2.0 * kPi / 6.0;
    plan.zones.push_back({ZoneKind::Suburb, names[k], centre + math::Vec2{std::cos(a), std::sin(a)} * orbit, zone_r, 0.2 * k});
  }
  const double a = -kPi / 2 + 5 * 2.0 * kPi / 6.0;
  const double airport_r = std::clamp(std::min(400.0, half - orbit), 50.0, 400.0);
  plan.zones.push_back({ZoneKind::Airport, "Airport", centre + math::Vec2{std::cos(a), std::sin(a)} * orbit, airport_r, a + kPi / 2});
  plan.runway_length_meters = std::clamp(airport_r * 1.8, 100.0, 750.0);
  // A hexagonal ring whose sides face the suburbs, so each gets its interchange in front.
  std::vector<math::Vec2> ringway;
  for (int k = 0; k <= 6; ++k) { const double b = k * 2.0 * kPi / 6.0; ringway.push_back(centre + math::Vec2{std::cos(b), std::sin(b)} * (ring / std::cos(kPi / 6.0))); }
  plan.highways = {ringway};
  return plan;
}

}  // namespace terraflux::graphics
