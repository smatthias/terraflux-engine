#include <terraflux/graphics/roads.hpp>

#include <terraflux/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <stdexcept>
#include <utility>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRailingRadius = 0.15;   // the railing as a chain of posts this thick, touching
constexpr double kMinPierHeight = 0.5;    // shorter piers are abutment fill, not drawn
constexpr double kDeckLevelTolerance = 1.5;  // a trunk lies on a deck only where the deck is this close to the ground
constexpr double kAbutmentTolerance = 0.3;    // the ground may rise this far into a planned deck (its abutments)

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool finite(math::Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
float f(double v) { return static_cast<float>(v); }

double hash01(std::uint32_t a, std::uint32_t b, std::uint32_t seed) {
  std::uint32_t h = a * 747796405u + b * 2891336453u + seed * 277803737u + 0x9E3779B9u;
  h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
  return static_cast<double>(h) / 4294967296.0;
}

math::Vec2 right_of(math::Vec2 dir) { return {-dir.y, dir.x}; }  // forward x up in plan (x, z)

// Closest points of two plan segments p0-p1 and q0-q1: the distance and the
// parameters (0 .. 1) along each.
struct SegmentApproach { double distance = 0.0, s = 0.0, t = 0.0; };
SegmentApproach segment_approach(math::Vec2 p0, math::Vec2 p1, math::Vec2 q0, math::Vec2 q1) {
  const math::Vec2 u = p1 - p0, v = q1 - q0, w = p0 - q0;
  const double a = u.dot(u), b = u.dot(v), c = v.dot(v), d = u.dot(w), e = v.dot(w);
  const double denom = a * c - b * b;
  double s = 0.0, t = 0.0;
  // A crossing inside both segments; otherwise the nearest end points.
  if (denom > 1e-12) {
    s = (b * e - c * d) / denom;
    t = (a * e - b * d) / denom;
  }
  const auto clamp01 = [](double x) { return std::clamp(x, 0.0, 1.0); };
  if (denom <= 1e-12 || s < 0 || s > 1 || t < 0 || t > 1) {
    // Check the four end-point projections and keep the closest pair.
    double best = std::numeric_limits<double>::infinity();
    const auto consider = [&](double ss, double tt) {
      ss = clamp01(ss); tt = clamp01(tt);
      const math::Vec2 pp = p0 + u * ss, qq = q0 + v * tt;
      const double dist = (pp - qq).length();
      if (dist < best) { best = dist; s = ss; t = tt; }
    };
    consider(0.0, c > 1e-12 ? -e / c : 0.0);
    consider(1.0, c > 1e-12 ? (b - e) / c : 0.0);
    consider(a > 1e-12 ? -d / a : 0.0, 0.0);
    consider(a > 1e-12 ? (b - d) / a : 0.0, 1.0);
  }
  const math::Vec2 pp = p0 + u * s, qq = q0 + v * t;
  return {(pp - qq).length(), s, t};
}

math::Vec3 lift(math::Vec2 p, double y) { return {p.x, y, p.y}; }

}  // namespace

const char* road_kind_name(RoadKind kind) {
  switch (kind) {
    case RoadKind::Road: return "road";
    case RoadKind::Bridge: return "bridge";
  }
  throw std::invalid_argument("unknown road kind");
}

const char* road_class_name(RoadClass cls) {
  switch (cls) {
    case RoadClass::Street: return "street";
    case RoadClass::Avenue: return "avenue";
    case RoadClass::Highway: return "highway";
    case RoadClass::Ramp: return "ramp";
    case RoadClass::Runway: return "runway";
    case RoadClass::Taxiway: return "taxiway";
    case RoadClass::Apron: return "apron";
  }
  throw std::invalid_argument("unknown road class");
}

std::optional<RoadClass> road_class_from_name(const std::string& name) {
  for (const auto cls : {RoadClass::Street, RoadClass::Avenue, RoadClass::Highway, RoadClass::Ramp, RoadClass::Runway, RoadClass::Taxiway, RoadClass::Apron})
    if (name == road_class_name(cls)) return cls;
  return std::nullopt;
}

int road_class_lanes(RoadClass cls) {
  switch (cls) {
    case RoadClass::Street: return 2;
    case RoadClass::Avenue: return 4;
    case RoadClass::Highway: return 6;
    case RoadClass::Ramp: return 2;
    case RoadClass::Runway: case RoadClass::Taxiway: case RoadClass::Apron: return 0;
  }
  throw std::invalid_argument("unknown road class");
}

const char* road_event_name(RoadEvent::Kind kind) {
  switch (kind) {
    case RoadEvent::Kind::Blocked: return "blocked";
    case RoadEvent::Kind::Damaged: return "damaged";
    case RoadEvent::Kind::Alight: return "alight";
    case RoadEvent::Kind::Destroyed: return "destroyed";
    case RoadEvent::Kind::Collapsed: return "collapsed";
    case RoadEvent::Kind::WashedOut: return "washed out";
  }
  throw std::invalid_argument("unknown road event");
}

void validate_road_style(const RoadStyle& s) {
  if (!within(s.lane_width_meters, 1.5, 10) || !within(s.shoulder_meters, 0, 5)) throw std::invalid_argument("lane width must be 1.5..10 m and shoulder 0..5 m");
  if (!within(s.highway_median_meters, 0, 10) || !within(s.runway_width_meters, 10, 100) || !within(s.taxiway_width_meters, 5, 60) || !within(s.apron_width_meters, 10, 200))
    throw std::invalid_argument("median must be 0..10 m, runway 10..100 m, taxiway 5..60 m and apron 10..200 m wide");
  if (!within(s.sample_spacing_meters, 0.5, 50) || !within(s.surface_lift_meters, 0, 1)) throw std::invalid_argument("road sample spacing must be 0.5..50 m and surface lift 0..1 m");
  if (!within(s.marking_width_meters, 0.02, 1) || !within(s.marking_dash_meters, 0.5, 50) || !within(s.marking_gap_meters, 0, 100))
    throw std::invalid_argument("marking width must be 0.02..1 m, dash 0.5..50 m and gap 0..100 m");
  if (!within(s.deck_thickness_meters, 0.1, 5) || !within(s.railing_height_meters, 0, 3) || !within(s.railing_post_spacing_meters, 0.5, 50))
    throw std::invalid_argument("deck thickness must be 0.1..5 m, railing height 0..3 m and post spacing 0.5..50 m");
  if (!within(s.pier_spacing_meters, 2, 200) || !within(s.pier_width_meters, 0.2, 10) || !within(s.stub_meters, 0, 100))
    throw std::invalid_argument("pier spacing must be 2..200 m, pier width 0.2..10 m and stubs 0..100 m");
  if (!within(s.road_speed_factor, 1, 3) || !within(s.bridge_speed_factor, 1, 3) || !within(s.rubble_speed_factor, 0.1, 1))
    throw std::invalid_argument("road and bridge speed factors must be 1..3 and the rubble factor 0.1..1");
  if (!within(s.blockage_reach_meters, 0, 100)) throw std::invalid_argument("blockage reach must be 0..100 m");
  if (!within(s.road_fire_damage_per_second, 0, 1) || !within(s.bridge_fire_damage_per_second, 0, 1))
    throw std::invalid_argument("fire damage rates must be 0..1 per second");
  if (!within(s.washout_current_mps, 0.1, 30) || !within(s.washout_depth_meters, 0, 20) || !within(s.washout_seconds, 1, 86400))
    throw std::invalid_argument("washout current must be 0.1..30 m/s, depth 0..20 m and time 1..86400 s");
  if (!within(s.collapse_damage, 0, 1)) throw std::invalid_argument("collapse damage must be 0..1");
}

double road_width(const RoadStyle& style) {
  validate_road_style(style);
  return 2.0 * (style.lane_width_meters + style.shoulder_meters);
}

double road_class_width(const RoadStyle& style, RoadClass cls) {
  validate_road_style(style);
  switch (cls) {
    case RoadClass::Street: case RoadClass::Avenue: case RoadClass::Ramp: return road_class_lanes(cls) * style.lane_width_meters + 2.0 * style.shoulder_meters;
    case RoadClass::Highway: return road_class_lanes(cls) * style.lane_width_meters + 2.0 * style.shoulder_meters + style.highway_median_meters;
    case RoadClass::Runway: return style.runway_width_meters;
    case RoadClass::Taxiway: return style.taxiway_width_meters;
    case RoadClass::Apron: return style.apron_width_meters;
  }
  throw std::invalid_argument("unknown road class");
}

void validate_road_network(const RoadNetworkDef& def) {
  const auto n = def.nodes.size();
  for (const auto& node : def.nodes) {
    if (!finite(node.position)) throw std::invalid_argument("road node position must be finite");
    if (!within(node.lift_meters, 0, 60)) throw std::invalid_argument("road node lift must be 0..60 m");
  }
  std::vector<std::vector<std::uint32_t>> adjacency(n);
  std::vector<bool> has_road(n, false);
  std::map<std::pair<std::uint32_t, std::uint32_t>, bool> seen;
  // The bridges as chains: union-find over the nodes they join.
  std::vector<std::uint32_t> chain(n);
  for (std::uint32_t i = 0; i < n; ++i) chain[i] = i;
  const auto find = [&](std::uint32_t i) { while (chain[i] != i) { chain[i] = chain[chain[i]]; i = chain[i]; } return i; };
  bool any_bridge = false;
  for (const auto& s : def.segments) {
    if (s.a >= n || s.b >= n) throw std::invalid_argument("road segment refers to a node that does not exist");
    if (s.a == s.b) throw std::invalid_argument("road segment joins a node to itself");
    if ((def.nodes[s.b].position - def.nodes[s.a].position).length() < 1e-6) throw std::invalid_argument("road segment has no length");
    (void)road_class_name(s.cls);
    const auto key = std::make_pair(std::min(s.a, s.b), std::max(s.a, s.b));
    if (!seen.emplace(key, true).second) throw std::invalid_argument("road segment is duplicated");
    adjacency[s.a].push_back(s.b); adjacency[s.b].push_back(s.a);
    if (s.kind == RoadKind::Road) {
      has_road[s.a] = true; has_road[s.b] = true;
      if (def.nodes[s.a].lift_meters > 0 || def.nodes[s.b].lift_meters > 0) throw std::invalid_argument("a road cannot reach a lifted node: only bridges run at height (a viaduct)");
    } else { chain[find(s.a)] = find(s.b); any_bridge = true; }
  }
  if (any_bridge) {
    // Every chain of bridges must come down to roads at two of its nodes.
    std::map<std::uint32_t, std::size_t> landings;
    for (std::uint32_t i = 0; i < n; ++i) if (has_road[i]) ++landings[find(i)];
    for (const auto& s : def.segments)
      if (s.kind == RoadKind::Bridge && landings[find(s.a)] < 2)
        throw std::invalid_argument("a bridge must join roads at both of its ends (no stand-alone bridge)");
  }
  if (n == 0) return;
  for (std::size_t i = 0; i < n; ++i)
    if (adjacency[i].empty()) throw std::invalid_argument("road node is joined to no segment");
  std::vector<bool> reached(n, false);
  std::queue<std::uint32_t> queue;
  queue.push(0); reached[0] = true;
  std::size_t count = 0;
  while (!queue.empty()) {
    const auto i = queue.front(); queue.pop(); ++count;
    for (const auto j : adjacency[i]) if (!reached[j]) { reached[j] = true; queue.push(j); }
  }
  if (count != n) throw std::invalid_argument("road network is not connected: every road and bridge must join the rest");
}

void validate_road_plan_style(const RoadPlanStyle& p) {
  if (!within(p.max_road_grade, 0.01, 2) || !within(p.max_bridge_grade, 0, 1)) throw std::invalid_argument("road grade must be 0.01..2 and bridge grade 0..1");
  if (!within(p.max_span_meters, 5, 5000) || !within(p.min_dip_meters, 0, 100) || !within(p.freeboard_meters, 0, 20))
    throw std::invalid_argument("span must be 5..5000 m, dip 0..100 m and freeboard 0..20 m");
}

RoadNetworkDef plan_road_network(std::span<const std::vector<math::Vec2>> routes, const StandingHeightFn& ground,
                                 std::optional<double> water_level, const RoadStyle& style, const RoadPlanStyle& plan) {
  std::vector<RoadRoute> streets;
  streets.reserve(routes.size());
  for (const auto& r : routes) streets.push_back({r, RoadClass::Street, {}});
  return plan_road_network(streets, ground, water_level, style, plan);
}

RoadNetworkDef plan_road_network(std::span<const RoadRoute> routes, const StandingHeightFn& ground,
                                 std::optional<double> water_level, const RoadStyle& style, const RoadPlanStyle& plan) {
  validate_road_style(style);
  validate_road_plan_style(plan);
  if (!ground) throw std::invalid_argument("planning roads needs a ground function");
  if (water_level && !std::isfinite(*water_level)) throw std::invalid_argument("water level must be finite");
  RoadNetworkDef def;
  std::map<std::pair<std::int64_t, std::int64_t>, std::uint32_t> nodes;  // quantised to a centimetre
  const auto node_at = [&](math::Vec2 p, double lift = 0.0) {
    const auto key = std::make_pair(static_cast<std::int64_t>(std::llround(p.x * 100.0)), static_cast<std::int64_t>(std::llround(p.y * 100.0)));
    const auto found = nodes.find(key);
    if (found != nodes.end()) { def.nodes[found->second].lift_meters = std::max(def.nodes[found->second].lift_meters, lift); return found->second; }
    const auto index = static_cast<std::uint32_t>(def.nodes.size());
    def.nodes.push_back({p, lift});
    nodes.emplace(key, index);
    return index;
  };
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> joined;
  const auto join = [&](std::uint32_t a, std::uint32_t b, RoadKind kind, RoadClass cls) {
    if (a == b) return;
    const auto key = std::make_pair(std::min(a, b), std::max(a, b));
    if (joined.count(key)) return;  // routes may share a leg
    joined.emplace(key, def.segments.size());
    def.segments.push_back({a, b, kind, cls});
  };
  constexpr int kMinRoadSamples = 4;  // a bridge needs this much road before and after it
  for (const auto& route_def : routes) {
    const auto& route = route_def.points;
    (void)road_class_name(route_def.cls);
    if (route.size() < 2) throw std::invalid_argument("a route needs at least two waypoints");
    if (!route_def.lifts.empty() && route_def.lifts.size() != route.size()) throw std::invalid_argument("a route's lifts must match its waypoints");
    for (const auto& w : route) {
      if (!finite(w)) throw std::invalid_argument("route waypoint must be finite");
      if (!ground(w.x, w.y)) throw std::invalid_argument("route waypoint lies off the terrain");
    }
    for (const auto l : route_def.lifts) if (!within(l, 0, 60)) throw std::invalid_argument("route lifts must be 0..60 m");
    const auto lift_at = [&](std::size_t i) { return route_def.lifts.empty() ? 0.0 : route_def.lifts[i]; };
    for (std::size_t leg = 0; leg + 1 < route.size(); ++leg) {
      const math::Vec2 a = route[leg], b = route[leg + 1];
      const double len = (b - a).length();
      if (len < 1e-6) continue;
      if (lift_at(leg) > 0 || lift_at(leg + 1) > 0) {
        // A viaduct leg: the deck runs from lift to lift, a bridge whatever the ground does.
        join(node_at(a, lift_at(leg)), node_at(b, lift_at(leg + 1)), RoadKind::Bridge, route_def.cls);
        continue;
      }
      const math::Vec2 dir = (b - a) / len;
      const int n = std::max(1, static_cast<int>(std::ceil(len / style.sample_spacing_meters)));
      std::vector<double> t(n + 1), g(n + 1);
      std::vector<bool> dry(n + 1), good(n + 1);  // good: dry and no steep interval on either side
      for (int k = 0; k <= n; ++k) {
        t[k] = std::min(len, k * style.sample_spacing_meters);
        if (k == n) t[k] = len;
        const math::Vec2 p = a + dir * t[k];
        const auto h = ground(p.x, p.y);
        g[k] = h ? *h : (k > 0 ? g[k - 1] : 0.0);  // a hole inside a leg: carry the last height (the road goes on)
        dry[k] = !water_level || g[k] > *water_level;
      }
      std::vector<bool> steep(n, false);
      for (int k = 0; k < n; ++k) steep[k] = std::abs(g[k + 1] - g[k]) > plan.max_road_grade * std::max(t[k + 1] - t[k], 1e-9);
      for (int k = 0; k <= n; ++k) good[k] = dry[k] && !(k > 0 && steep[k - 1]) && !(k < n && steep[k]);
      // A deck end over water must stand the freeboard above it.
      const auto high_enough = [&](int k) { return !water_level || g[k] >= *water_level + plan.freeboard_meters; };
      std::uint32_t previous = node_at(a);
      int previous_sample = 0;
      int k = 0;
      while (k < n) {
        const bool trouble = !good[k + 1];
        if (!trouble || !good[k] || k < kMinRoadSamples - 1) { ++k; continue; }
        // The road cannot go on from sample k: the deck starts at the last
        // sample high enough over the water, and looks for a landing.
        int start = k;
        bool wet_ahead = false;
        for (int m = k + 1; m <= n && !good[m]; ++m) if (!dry[m]) wet_ahead = true;
        if (wet_ahead) while (start > previous_sample + kMinRoadSamples - 1 && !high_enough(start)) --start;
        if (start < previous_sample + kMinRoadSamples - 1 || !good[start] || (wet_ahead && !high_enough(start))) { ++k; continue; }
        int landing = -1;
        for (int j = k + 2; j <= n - kMinRoadSamples && t[j] - t[start] <= plan.max_span_meters; ++j) {
          bool clear_run = true;
          for (int m = j; m <= std::min(n, j + kMinRoadSamples) && clear_run; ++m) clear_run = good[m];
          if (!clear_run) continue;
          const double span = t[j] - t[start];
          if (std::abs(g[j] - g[start]) > plan.max_bridge_grade * span) continue;
          bool clears = true, worth = false, wet = false;
          for (int m = start + 1; m < j && clears; ++m) {
            const double chord = g[start] + (g[j] - g[start]) * (t[m] - t[start]) / span;
            if (chord - g[m] < -kAbutmentTolerance) clears = false;  // ground above the deck: a cutting, not a bridge
            if (chord - g[m] >= plan.min_dip_meters || !dry[m]) worth = true;
            if (!dry[m]) wet = true;
          }
          if (!clears || !worth || (wet && !high_enough(j))) continue;
          landing = j;
          break;
        }
        if (landing < 0) { ++k; continue; }
        const auto from = node_at(a + dir * t[start]), to = node_at(a + dir * t[landing]);
        join(previous, from, RoadKind::Road, route_def.cls);
        join(from, to, RoadKind::Bridge, route_def.cls);
        previous = to;
        previous_sample = landing;
        k = landing;
      }
      join(previous, node_at(b), RoadKind::Road, route_def.cls);
    }
  }
  validate_road_network(def);
  return def;
}

RoadNetworkDef parse_road_network(const std::string& json_text, const StandingHeightFn& ground, std::optional<double> water_level,
                                  const RoadStyle& style, const RoadPlanStyle& plan, math::Vec2 origin) {
  using terraflux::json::Json;
  json::Parser parser(json_text);
  const Json root = parser.parse_document();
  const auto* object = std::get_if<Json::Object>(&root.value);
  if (!object) throw std::invalid_argument("road network JSON must be an object");
  const auto number = [](const Json& j, const char* what) {
    const auto* v = std::get_if<double>(&j.value);
    if (!v || !std::isfinite(*v)) throw std::invalid_argument(std::string("road network JSON: ") + what + " must be a finite number");
    return *v;
  };
  const auto field = [&](const Json::Object& o, const char* name) -> const Json* {
    const auto it = o.find(name);
    return it == o.end() ? nullptr : &it->second;
  };
  if (const auto* version = field(*object, "version")) {
    if (number(*version, "version") != 1.0) throw std::invalid_argument("road network JSON version must be 1");
  } else throw std::invalid_argument("road network JSON needs a version");
  const auto point = [&](const Json& j) -> math::Vec2 {
    if (const auto* o = std::get_if<Json::Object>(&j.value)) {
      const auto* x = field(*o, "x"); const auto* z = field(*o, "z");
      if (!x || !z) throw std::invalid_argument("road network JSON: a position needs x and z");
      return {number(*x, "x") + origin.x, number(*z, "z") + origin.y};
    }
    if (const auto* a = std::get_if<Json::Array>(&j.value)) {
      if (a->size() != 2) throw std::invalid_argument("road network JSON: a position array needs two numbers");
      return {number((*a)[0], "x") + origin.x, number((*a)[1], "z") + origin.y};
    }
    throw std::invalid_argument("road network JSON: a position must be {x, z} or [x, z]");
  };
  const auto class_of = [&](const Json* j) {
    if (!j) return RoadClass::Street;
    const auto* name = std::get_if<std::string>(&j->value);
    if (!name) throw std::invalid_argument("road network JSON: class must be a string");
    const auto cls = road_class_from_name(*name);
    if (!cls) throw std::invalid_argument("road network JSON: unknown road class " + *name);
    return *cls;
  };
  if (const auto* routes = field(*object, "routes")) {
    const auto* list = std::get_if<Json::Array>(&routes->value);
    if (!list) throw std::invalid_argument("road network JSON: routes must be an array");
    std::vector<RoadRoute> planned;
    for (const auto& r : *list) {
      RoadRoute route;
      const Json::Array* points = std::get_if<Json::Array>(&r.value);
      if (const auto* o = std::get_if<Json::Object>(&r.value)) {
        const auto* pts = field(*o, "points");
        if (!pts || !(points = std::get_if<Json::Array>(&pts->value))) throw std::invalid_argument("road network JSON: a route object needs points");
        route.cls = class_of(field(*o, "class"));
        if (const auto* lifts = field(*o, "lifts")) {
          const auto* arr = std::get_if<Json::Array>(&lifts->value);
          if (!arr) throw std::invalid_argument("road network JSON: lifts must be an array");
          for (const auto& l : *arr) route.lifts.push_back(number(l, "lift"));
        }
      }
      if (!points) throw std::invalid_argument("road network JSON: a route must be an array of positions or an object with points");
      for (const auto& p : *points) route.points.push_back(point(p));
      planned.push_back(std::move(route));
    }
    return plan_road_network(planned, ground, water_level, style, plan);
  }
  const auto* nodes = field(*object, "nodes");
  const auto* segments = field(*object, "segments");
  if (!nodes || !segments) throw std::invalid_argument("road network JSON needs nodes and segments (or routes)");
  const auto* node_list = std::get_if<Json::Array>(&nodes->value);
  const auto* segment_list = std::get_if<Json::Array>(&segments->value);
  if (!node_list || !segment_list) throw std::invalid_argument("road network JSON: nodes and segments must be arrays");
  RoadNetworkDef def;
  for (const auto& n : *node_list) {
    RoadNode node{point(n), 0.0};
    if (const auto* o = std::get_if<Json::Object>(&n.value)) if (const auto* lift = field(*o, "lift")) node.lift_meters = number(*lift, "lift");
    def.nodes.push_back(node);
  }
  for (const auto& s : *segment_list) {
    const auto* o = std::get_if<Json::Object>(&s.value);
    if (!o) throw std::invalid_argument("road network JSON: a segment must be an object");
    const auto* from = field(*o, "from"); const auto* to = field(*o, "to"); const auto* kind = field(*o, "kind");
    if (!from || !to) throw std::invalid_argument("road network JSON: a segment needs from and to");
    const double a = number(*from, "from"), b = number(*to, "to");
    if (a < 0 || b < 0 || a != std::floor(a) || b != std::floor(b) || a > 4e9 || b > 4e9) throw std::invalid_argument("road network JSON: from and to must be node indices");
    RoadSegment seg{static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), RoadKind::Road, class_of(field(*o, "class"))};
    if (kind) {
      const auto* name = std::get_if<std::string>(&kind->value);
      if (!name) throw std::invalid_argument("road network JSON: kind must be a string");
      if (*name == "bridge") seg.kind = RoadKind::Bridge;
      else if (*name != "road") throw std::invalid_argument("road network JSON: kind must be road or bridge");
    }
    def.segments.push_back(seg);
  }
  validate_road_network(def);
  for (const auto& n : def.nodes)
    if (!ground(n.position.x, n.position.y)) throw std::invalid_argument("road network JSON: a node lies off the terrain");
  return def;
}

// ---------------------------------------------------------------------------
RoadNetwork::RoadNetwork(RoadNetworkDef def, const RoadStyle& style, StandingHeightFn ground)
    : def_(std::move(def)), style_(style), ground_(std::move(ground)) {
  validate_road_style(style_);
  validate_road_network(def_);
  if (!ground_) throw std::invalid_argument("a road network needs a ground function");
  node_surface_.reserve(def_.nodes.size());
  for (const auto& n : def_.nodes) {
    const auto h = ground_(n.position.x, n.position.y);
    if (!h || !std::isfinite(*h)) throw std::invalid_argument("road node lies off the terrain");
    node_surface_.push_back(*h + style_.surface_lift_meters + n.lift_meters);
  }
  geometry_.reserve(def_.segments.size());
  std::vector<std::vector<std::uint32_t>> at_node(def_.nodes.size());
  for (std::size_t i = 0; i < def_.segments.size(); ++i) {
    const auto& s = def_.segments[i];
    Geometry g;
    g.a = def_.nodes[s.a].position; g.b = def_.nodes[s.b].position;
    g.length = (g.b - g.a).length();
    g.dir = (g.b - g.a) / g.length;
    g.right = right_of(g.dir);
    g.mitre_a = g.mitre_b = g.right;
    g.ya = node_surface_[s.a]; g.yb = node_surface_[s.b];
    g.half = 0.5 * road_class_width(style_, s.cls);
    max_half_ = std::max(max_half_, g.half);
    geometry_.push_back(g);
    at_node[s.a].push_back(static_cast<std::uint32_t>(i));
    at_node[s.b].push_back(static_cast<std::uint32_t>(i));
  }
  // Where exactly two segments of one width meet, the ribbon is mitred so a
  // polyline reads as one curving road.
  for (std::size_t n = 0; n < at_node.size(); ++n) {
    if (at_node[n].size() != 2) continue;
    const auto i = at_node[n][0], j = at_node[n][1];
    if (std::abs(geometry_[i].half - geometry_[j].half) > 1e-9) continue;
    const auto through = [&](std::uint32_t seg) { return def_.segments[seg].b == n ? geometry_[seg].dir : geometry_[seg].dir * -1.0; };  // arriving at the node
    const math::Vec2 t = through(i) - through(j);  // arriving along i, leaving along j
    const double tl = t.length();
    if (tl < 1e-9) continue;
    const math::Vec2 mitre = right_of(t / tl);
    for (const auto seg : {i, j}) {
      auto& g = geometry_[seg];
      const double cosine = std::abs(mitre.dot(g.right));
      if (cosine < 0.7) continue;  // sharper than 90 degrees: a corner, not a curve
      const math::Vec2 m = mitre.dot(g.right) < 0 ? mitre * -1.0 : mitre;
      if (def_.segments[seg].a == n) { g.mitre_a = m; g.mitre_scale_a = 1.0 / cosine; } else { g.mitre_b = m; g.mitre_scale_b = 1.0 / cosine; }
    }
  }
  states_.assign(def_.segments.size(), RoadSegmentState{});
  // A plan index of the segments by their corridors' bounding boxes.
  for (std::size_t i = 0; i < geometry_.size(); ++i) {
    const auto& g = geometry_[i];
    const double pad = g.half * 1.5 + 1.0;
    const double lo_x = std::min(g.a.x, g.b.x) - pad, hi_x = std::max(g.a.x, g.b.x) + pad, lo_z = std::min(g.a.y, g.b.y) - pad, hi_z = std::max(g.a.y, g.b.y) + pad;
    for (auto cz = index_cell(lo_z); cz <= index_cell(hi_z); ++cz)
      for (auto cx = index_cell(lo_x); cx <= index_cell(hi_x); ++cx) index_[index_key(cx, cz)].push_back(static_cast<std::uint32_t>(i));
  }
}

double RoadNetwork::width(std::uint32_t segment) const {
  if (segment >= geometry_.size()) throw std::invalid_argument("road segment index out of range");
  return 2.0 * geometry_[segment].half;
}

double RoadNetwork::distance_to_centreline(double x, double z, double search_radius) const {
  if (!std::isfinite(x) || !std::isfinite(z) || !within(search_radius, 0, 1e6)) throw std::invalid_argument("centre line query out of range");
  double best = std::numeric_limits<double>::infinity();
  const math::Vec2 p{x, z};
  for_each_segment_near(x, z, search_radius, [&](std::uint32_t i) {
    const auto& g = geometry_[i];
    const double along = std::clamp((p - g.a).dot(g.dir), 0.0, g.length);
    best = std::min(best, (g.a + g.dir * along - p).length());
  });
  return best;
}

double RoadNetwork::ground_at(double x, double z, double fallback) const {
  const auto h = ground_(x, z);
  return h && std::isfinite(*h) ? *h : fallback;
}

double RoadNetwork::length(std::uint32_t segment) const {
  if (segment >= geometry_.size()) throw std::invalid_argument("road segment index out of range");
  return geometry_[segment].length;
}
math::Vec2 RoadNetwork::direction(std::uint32_t segment) const {
  if (segment >= geometry_.size()) throw std::invalid_argument("road segment index out of range");
  return geometry_[segment].dir;
}
double RoadNetwork::surface_at_node(std::uint32_t node) const {
  if (node >= node_surface_.size()) throw std::invalid_argument("road node index out of range");
  return node_surface_[node];
}

double RoadNetwork::deck_height(std::uint32_t segment, double along) const {
  const auto& g = geometry_[segment];
  const double t = std::clamp(along / std::max(g.length, 1e-9), 0.0, 1.0);
  if (def_.segments[segment].kind == RoadKind::Bridge) return g.ya + (g.yb - g.ya) * t;
  const math::Vec2 p = g.a + g.dir * (t * g.length);
  return ground_at(p.x, p.y, g.ya + (g.yb - g.ya) * t) + style_.surface_lift_meters;
}

bool RoadNetwork::deck_stands(std::uint32_t segment, double along) const {
  if (def_.segments[segment].kind != RoadKind::Bridge || !states_[segment].destroyed) return true;
  const auto& g = geometry_[segment];
  return along <= style_.stub_meters || along >= g.length - style_.stub_meters;
}

double RoadNetwork::clearance(std::uint32_t segment) const {
  if (segment >= geometry_.size()) throw std::invalid_argument("road segment index out of range");
  if (def_.segments[segment].kind != RoadKind::Bridge) return 0.0;
  const auto& g = geometry_[segment];
  double best = 0.0;
  const int n = std::max(1, static_cast<int>(std::ceil(g.length / style_.sample_spacing_meters)));
  for (int k = 0; k <= n; ++k) {
    const double along = std::min(g.length, k * style_.sample_spacing_meters);
    const math::Vec2 p = g.a + g.dir * along;
    best = std::max(best, deck_height(segment, along) - ground_at(p.x, p.y, deck_height(segment, along)));
  }
  return best;
}

std::vector<math::Vec2> RoadNetwork::pier_positions(std::uint32_t segment) const {
  std::vector<math::Vec2> out;
  const auto& g = geometry_[segment];
  const int count = std::max(0, static_cast<int>(std::lround(g.length / style_.pier_spacing_meters)) - 1);
  for (int k = 1; k <= count; ++k) {
    const auto p = g.a + g.dir * (g.length * k / (count + 1));
    // No pier stands on another carriageway (the road a viaduct crosses).
    bool on_other = false;
    for_each_segment_near(p.x, p.y, max_half_ + 1.0, [&](std::uint32_t i) {
      if (i == segment || on_other) return;
      const auto& o = geometry_[i];
      const math::Vec2 rel = p - o.a;
      const double along = rel.dot(o.dir);
      if (along >= -0.5 && along <= o.length + 0.5 && std::abs(rel.dot(o.right)) <= o.half + 0.5 * style_.pier_width_meters) on_other = true;
    });
    if (!on_other) out.push_back(p);
  }
  return out;
}

RoadSample RoadNetwork::sample(double x, double z) const { return sample(x, z, std::numeric_limits<double>::quiet_NaN()); }

RoadSample RoadNetwork::sample(double x, double z, double feet_y) const {
  if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("road sample position must be finite");
  RoadSample best;
  double best_offset = std::numeric_limits<double>::infinity(), best_surface = -std::numeric_limits<double>::infinity();
  const bool by_height = std::isfinite(feet_y);
  const math::Vec2 p{x, z};
  for_each_segment_near(x, z, max_half_ + 1.0, [&](std::uint32_t i) {
    const auto& g = geometry_[i];
    const math::Vec2 rel = p - g.a;
    const double along = rel.dot(g.dir), offset = rel.dot(g.right);
    // Within the ribbon: between the (mitred) end sections and the half width.
    const bool inside_ends = (p - g.a).dot(right_of(g.mitre_a) * -1.0) >= -1e-9 && (p - g.b).dot(right_of(g.mitre_b) * -1.0) <= 1e-9;
    if (!inside_ends || std::abs(offset) > g.half + 1e-9) return;
    const double relative = std::abs(offset) / g.half;
    if (by_height) {
      // The highest surface within a metre above the feet wins; among surfaces out of reach, the one nearest its centre line.
      const double surface = deck_height(i, std::clamp(along, 0.0, g.length));
      const bool reachable = surface <= feet_y + 1.0, best_reachable = best.on && best_surface <= feet_y + 1.0;
      if (best.on) {
        if (best_reachable && !reachable) return;
        if (best_reachable && reachable && surface <= best_surface) return;
        if (!best_reachable && !reachable && relative >= best_offset) return;
      }
      best_surface = surface;
    } else if (relative >= best_offset) return;
    best_offset = relative;
    best.on = true;
    best.segment = i;
    best.kind = def_.segments[i].kind;
    best.cls = def_.segments[i].cls;
    best.along_meters = std::clamp(along, 0.0, g.length);
    best.offset_meters = offset;
  });
  if (!best.on) return best;
  const auto& st = states_[best.segment];
  best.surface_meters = deck_height(best.segment, best.along_meters);
  best.damage = st.damage;
  best.destroyed = st.destroyed;
  best.deck_missing = !deck_stands(best.segment, best.along_meters);
  for (const auto& b : blockages_)
    if (b.segment == best.segment && std::abs(b.along_meters - best.along_meters) <= style_.blockage_reach_meters) best.blocked = true;
  if (st.destroyed) best.speed_factor = best.kind == RoadKind::Road ? style_.rubble_speed_factor : 1.0;
  else if (best.blocked) best.speed_factor = 1.0;
  else {
    const double base = best.kind == RoadKind::Road ? style_.road_speed_factor : style_.bridge_speed_factor;
    best.speed_factor = base - (base - 1.0) * std::clamp(st.damage, 0.0, 1.0);
  }
  return best;
}

std::optional<double> RoadNetwork::platform_height(double x, double z) const { return platform_height(x, z, std::numeric_limits<double>::quiet_NaN()); }
std::optional<double> RoadNetwork::platform_height(double x, double z, double feet_y) const {
  const auto s = sample(x, z, feet_y);
  if (!s.on || s.deck_missing) return std::nullopt;
  return s.surface_meters;
}

double RoadNetwork::speed_factor(double x, double z) const { return sample(x, z).speed_factor; }
double RoadNetwork::speed_factor(double x, double z, double feet_y) const { return sample(x, z, feet_y).speed_factor; }

std::vector<Obstacle> RoadNetwork::obstacles() const {
  std::vector<Obstacle> out;
  for (std::size_t i = 0; i < geometry_.size(); ++i) {
    if (def_.segments[i].kind != RoadKind::Bridge) continue;
    const auto& g = geometry_[i];
    const double half = g.half;
    const auto seg = static_cast<std::uint32_t>(i);
    // Piers stand whatever happened to the deck.
    for (const auto& p : pier_positions(seg)) {
      const double along = (p - g.a).dot(g.dir);
      const double top = deck_height(seg, along) - style_.deck_thickness_meters;
      const double bottom = ground_at(p.x, p.y, top);
      if (top - bottom < kMinPierHeight) continue;
      Obstacle o; o.x = p.x; o.z = p.y; o.radius = 0.5 * style_.pier_width_meters; o.bottom = bottom; o.top = top;
      out.push_back(o);
    }
    if (style_.railing_height_meters <= 0) continue;
    const double spacing = 2.0 * kRailingRadius;
    const int count = std::max(1, static_cast<int>(std::ceil(g.length / spacing)));
    for (int k = 0; k <= count; ++k) {
      const double along = std::min(g.length, k * spacing);
      if (!deck_stands(seg, along)) continue;
      const double deck = deck_height(seg, along);
      for (const double side : {-1.0, 1.0}) {
        const math::Vec2 p = g.a + g.dir * along + g.right * (side * (half - kRailingRadius));
        Obstacle o; o.x = p.x; o.z = p.y; o.radius = kRailingRadius; o.bottom = deck; o.top = deck + style_.railing_height_meters;
        out.push_back(o);
      }
    }
  }
  return out;
}

void RoadNetwork::raise(RoadEvent::Kind kind, std::uint32_t segment, int tree) { events_.push_back({kind, segment, tree}); }

std::size_t RoadNetwork::log_landed(std::uint32_t tree, math::Vec2 base, math::Vec2 direction, double length, bool burning) {
  if (!finite(base) || !finite(direction) || !within(length, 0, 1000)) throw std::invalid_argument("a landed trunk needs a finite base, direction and length");
  const double dl = direction.length();
  if (dl < 1e-9 || length <= 0) return 0;
  const math::Vec2 dir = direction / dl, tip = base + dir * length;
  std::size_t added = 0;
  std::vector<std::uint32_t> candidates;
  for_each_segment_near(0.5 * (base.x + tip.x), 0.5 * (base.y + tip.y), 0.5 * length + max_half_ + 1.0, [&](std::uint32_t i) { candidates.push_back(i); });
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
  for (const auto i : candidates) {
    const auto& g = geometry_[i];
    const auto seg = i;
    const auto approach = segment_approach(g.a, g.b, base, tip);
    if (approach.distance > g.half) continue;
    const double along = approach.s * g.length;
    if (def_.segments[i].kind == RoadKind::Bridge) {
      // A trunk lies on the ground; it lies on a deck only where the deck is at ground level.
      const math::Vec2 at = g.a + g.dir * along;
      if (deck_height(seg, along) - ground_at(at.x, at.y, deck_height(seg, along)) > kDeckLevelTolerance) continue;
    }
    bool known = false;
    for (const auto& b : blockages_) if (b.segment == seg && b.tree == tree) known = true;
    if (known) continue;
    blockages_.push_back({seg, tree, along, burning});
    ++states_[i].blockages;
    raise(RoadEvent::Kind::Blocked, seg, static_cast<int>(tree));
    ++added;
  }
  if (added) ++version_;
  return added;
}

void RoadNetwork::advance(double dt, const std::function<double(std::uint32_t tree)>& log_intensity, const WaterFn& water) {
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("road time step must be finite and non-negative");
  events_.clear();
  if (dt <= 0) return;
  for (auto& b : blockages_) {
    const double intensity = log_intensity ? std::clamp(log_intensity(b.tree), 0.0, 1.0) : 0.0;
    b.burning = intensity > 0;
    if (intensity <= 0) continue;
    auto& st = states_[b.segment];
    if (st.destroyed) continue;
    const bool bridge = def_.segments[b.segment].kind == RoadKind::Bridge;
    const double rate = bridge ? style_.bridge_fire_damage_per_second : style_.road_fire_damage_per_second;
    const double before = st.damage;
    st.damage = std::min(1.0, st.damage + rate * intensity * dt);
    if (before <= 0 && st.damage > 0) raise(bridge ? RoadEvent::Kind::Alight : RoadEvent::Kind::Damaged, b.segment, static_cast<int>(b.tree));
    if (st.damage >= 1.0) destroy(b.segment, "fire");
  }
  if (water) {
    for (std::size_t i = 0; i < geometry_.size(); ++i) {
      if (def_.segments[i].kind != RoadKind::Bridge || states_[i].destroyed) continue;
      const auto seg = static_cast<std::uint32_t>(i);
      bool scoured = false;
      for (const auto& p : pier_positions(seg)) {
        const double ground = ground_at(p.x, p.y, deck_height(seg, (p - geometry_[i].a).dot(geometry_[i].dir)));
        const auto w = water(p.x, p.y, ground);
        if (w.current_mps >= style_.washout_current_mps && w.depth_meters >= style_.washout_depth_meters) { scoured = true; break; }
      }
      auto& st = states_[i];
      st.washout_seconds = scoured ? st.washout_seconds + dt : std::max(0.0, st.washout_seconds - dt);
      if (st.washout_seconds >= style_.washout_seconds) destroy(seg, "flood");
    }
  }
}

void RoadNetwork::destroy(std::uint32_t segment, const char* cause) {
  if (segment >= states_.size()) throw std::invalid_argument("road segment index out of range");
  if (!cause) cause = "";
  auto& st = states_[segment];
  if (st.destroyed) return;
  st.destroyed = true;
  st.destroyed_by = cause;
  st.damage = 1.0;
  ++version_;
  raise(RoadEvent::Kind::Destroyed, segment);
  if (def_.segments[segment].kind == RoadKind::Bridge)
    raise(st.destroyed_by == "flood" ? RoadEvent::Kind::WashedOut : RoadEvent::Kind::Collapsed, segment);
}

void RoadNetwork::reset() {
  states_.assign(def_.segments.size(), RoadSegmentState{});
  blockages_.clear();
  events_.clear();
  ++version_;
}

RoadStats RoadNetwork::stats() const {
  RoadStats s;
  s.nodes = def_.nodes.size();
  s.blockages = blockages_.size();
  std::vector<std::vector<std::uint32_t>> at_node(def_.nodes.size());
  for (std::size_t i = 0; i < geometry_.size(); ++i) {
    const auto& st = states_[i];
    const auto& seg = def_.segments[i];
    const bool bridge = seg.kind == RoadKind::Bridge;
    if (bridge) { ++s.bridges; s.bridge_meters += geometry_[i].length; s.longest_bridge_meters = std::max(s.longest_bridge_meters, geometry_[i].length);
      s.highest_deck_meters = std::max(s.highest_deck_meters, clearance(static_cast<std::uint32_t>(i))); }
    else { ++s.roads; s.road_meters += geometry_[i].length; }
    switch (seg.cls) {
      case RoadClass::Street: ++s.streets; break;
      case RoadClass::Avenue: ++s.avenues; break;
      case RoadClass::Highway: ++s.highways; s.highway_meters += geometry_[i].length; break;
      case RoadClass::Ramp: ++s.ramps; break;
      case RoadClass::Runway: ++s.runways; s.runway_meters += geometry_[i].length; break;
      case RoadClass::Taxiway: ++s.taxiways; break;
      case RoadClass::Apron: ++s.aprons; break;
    }
    if (st.blockages) ++s.blocked;
    if (st.damage > 0 && !st.destroyed) ++s.damaged;
    if (st.destroyed) ++s.destroyed;
    at_node[seg.a].push_back(static_cast<std::uint32_t>(i));
    at_node[seg.b].push_back(static_cast<std::uint32_t>(i));
  }
  for (std::size_t n = 0; n < at_node.size(); ++n) {
    if (at_node[n].size() != 2) continue;
    const auto i = at_node[n][0], j = at_node[n][1];
    const math::Vec2 di = def_.segments[i].b == n ? geometry_[i].dir : geometry_[i].dir * -1.0;
    const math::Vec2 dj = def_.segments[j].a == n ? geometry_[j].dir : geometry_[j].dir * -1.0;
    if (di.dot(dj) < std::cos(2.0 * kPi / 180.0)) ++s.curve_joints;
  }
  return s;
}

namespace {
// Surface colours by class and state.
math::Vec3 surface_colour(RoadClass cls) {
  switch (cls) {
    case RoadClass::Street: case RoadClass::Avenue: case RoadClass::Ramp: return {0.30, 0.30, 0.31};
    case RoadClass::Highway: return {0.27, 0.27, 0.29};
    case RoadClass::Runway: return {0.24, 0.24, 0.25};
    case RoadClass::Taxiway: case RoadClass::Apron: return {0.58, 0.57, 0.55};
  }
  return {0.30, 0.30, 0.31};
}
}  // namespace

std::vector<RoadMeshPart> RoadNetwork::build_meshes() const {
  std::vector<RoadMeshPart> parts;
  const math::Vec3 charred{0.10, 0.09, 0.08}, rubble{0.36, 0.33, 0.29};
  const math::Vec3 concrete{0.62, 0.60, 0.57}, steel{0.70, 0.71, 0.70}, paint{0.88, 0.87, 0.80}, yellow{0.85, 0.72, 0.25};
  MeshPart markings; markings.color = paint; markings.what = "marking"; markings.material = SurfaceMaterial::Paint;
  MeshPart yellow_markings; yellow_markings.color = yellow; yellow_markings.what = "marking"; yellow_markings.material = SurfaceMaterial::Paint;
  MeshPart railings; railings.color = steel; railings.what = "railing"; railings.material = SurfaceMaterial::Metal;
  MeshPart piers; piers.color = concrete; piers.what = "pier"; piers.material = SurfaceMaterial::Concrete;
  MeshPart medians; medians.color = concrete * 0.9; medians.what = "median"; medians.material = SurfaceMaterial::Concrete;
  MeshPart rubble_part; rubble_part.color = concrete * 0.8; rubble_part.what = "rubble"; rubble_part.material = SurfaceMaterial::Concrete;
  const auto surface_material = [](RoadClass c) { return c == RoadClass::Taxiway || c == RoadClass::Apron ? SurfaceMaterial::Pavement : SurfaceMaterial::Asphalt; };
  // Sound surfaces are merged by class colour; damaged ones keep their own part.
  std::map<int, MeshPart> merged;
  const auto mix = [](math::Vec3 a, math::Vec3 b, double t) { return a * (1.0 - t) + b * t; };
  std::vector<MeshPart> decks, damaged_surfaces;
  for (std::size_t i = 0; i < geometry_.size(); ++i) {
    const auto& g = geometry_[i];
    const auto seg = static_cast<std::uint32_t>(i);
    const auto& st = states_[i];
    const auto cls = def_.segments[i].cls;
    const bool bridge = def_.segments[i].kind == RoadKind::Bridge;
    const double half = g.half, shoulder = std::min(style_.shoulder_meters, half * 0.25);
    const double lane_edge = half - shoulder;
    const bool marked = cls != RoadClass::Apron;
    MeshPart own;
    own.what = "surface";
    own.material = st.destroyed && !bridge ? SurfaceMaterial::Concrete : surface_material(cls);
    const math::Vec3 base = surface_colour(cls);
    own.color = st.destroyed && !bridge ? rubble : mix(base, charred, std::clamp(st.damage, 0.0, 1.0) * 0.8);
    const bool sound = st.damage <= 0 && !st.destroyed;
    MeshPart* surface = &own;
    if (sound) {
      const int key = static_cast<int>(cls == RoadClass::Taxiway ? RoadClass::Apron : cls == RoadClass::Avenue || cls == RoadClass::Ramp ? RoadClass::Street : cls);
      auto& m = merged[key];
      if (m.what[0] == '\0') { m.what = "surface"; m.color = base; m.material = surface_material(cls); }
      surface = &m;
    }
    const int n = std::max(1, static_cast<int>(std::ceil(g.length / style_.sample_spacing_meters)));
    // The surface: a ribbon following the ground (a road) or the deck (a bridge), mitred at a curve's joints.
    std::vector<math::Vec3> left(n + 1), centre(n + 1), right(n + 1);
    std::vector<bool> stands(n + 1);
    for (int k = 0; k <= n; ++k) {
      const double along = std::min(g.length, k * style_.sample_spacing_meters);
      const math::Vec2 cross = k == 0 ? g.mitre_a * (half * g.mitre_scale_a) : k == n ? g.mitre_b * (half * g.mitre_scale_b) : g.right * half;
      const math::Vec2 c = g.a + g.dir * along, l = c - cross, r = c + cross;
      double yc = deck_height(seg, along), yl = yc, yr = yc;
      if (!bridge) { yl = ground_at(l.x, l.y, yc - style_.surface_lift_meters) + style_.surface_lift_meters; yr = ground_at(r.x, r.y, yc - style_.surface_lift_meters) + style_.surface_lift_meters; }
      else { yc += 0.01; yl += 0.01; yr += 0.01; }  // the wearing course on the slab
      left[k] = lift(l, yl); centre[k] = lift(c, yc); right[k] = lift(r, yr);
      stands[k] = deck_stands(seg, along);
    }
    // A point on the ribbon at a lateral offset (plan along the section, height by the two halves it is drawn in).
    const auto ribbon_point = [&](int kk, double off, double raise) {
      const double s_across = std::clamp((off + half) / (2.0 * half), 0.0, 1.0);
      const math::Vec3 plan = left[kk] * (1.0 - s_across) + right[kk] * s_across;
      const double y = s_across <= 0.5 ? left[kk].y + (centre[kk].y - left[kk].y) * (2.0 * s_across) : centre[kk].y + (right[kk].y - centre[kk].y) * (2.0 * s_across - 1.0);
      return math::Vec3{plan.x, y + raise, plan.z};
    };
    // A line (solid strip) along the ribbon at a lateral offset, where the deck stands.
    const auto solid_line = [&](MeshPart& part, double off, double width, double raise) {
      for (int k = 0; k < n; ++k) {
        if (!stands[k] || !stands[k + 1]) continue;
        add_quad(part, ribbon_point(k, off - 0.5 * width, raise), ribbon_point(k, off + 0.5 * width, raise), ribbon_point(k + 1, off + 0.5 * width, raise), ribbon_point(k + 1, off - 0.5 * width, raise));
      }
    };
    // Dashes along the ribbon at a lateral offset.
    const auto dashes = [&](MeshPart& part, double off, double width, double dash, double gap, double raise) {
      const double period = dash + gap;
      for (double start = 0.5 * gap; start < g.length; start += period) {
        const double end = std::min(g.length, start + dash);
        if (!deck_stands(seg, start) || !deck_stands(seg, end)) continue;
        const auto at = [&](double along, double o) { return lift(g.a + g.dir * along + g.right * o, deck_height(seg, along) + raise); };
        add_quad(part, at(start, off - 0.5 * width), at(start, off + 0.5 * width), at(end, off + 0.5 * width), at(end, off - 0.5 * width));
      }
    };
    for (int k = 0; k < n; ++k) {
      if (!stands[k] || !stands[k + 1]) continue;
      add_quad(*surface, left[k], centre[k], centre[k + 1], left[k + 1]);
      add_quad(*surface, centre[k], right[k], right[k + 1], centre[k + 1]);
    }
    const double raise = bridge ? 0.03 : 0.02, mw = style_.marking_width_meters;
    if (marked && !(st.destroyed && !bridge)) {
      const double lane = style_.lane_width_meters;
      switch (cls) {
        case RoadClass::Street:
          for (const double side : {-1.0, 1.0}) solid_line(markings, side * lane_edge, mw, raise);
          dashes(markings, 0.0, mw, style_.marking_dash_meters, style_.marking_gap_meters, raise);
          break;
        case RoadClass::Avenue:
          for (const double side : {-1.0, 1.0}) { solid_line(markings, side * lane_edge, mw, raise); dashes(markings, side * lane, mw, style_.marking_dash_meters, style_.marking_gap_meters, raise); }
          solid_line(yellow_markings, -0.5 * mw * 1.2, mw, raise); solid_line(yellow_markings, 0.5 * mw * 1.2, mw, raise);  // a double centre line
          break;
        case RoadClass::Highway: {
          const double median = 0.5 * style_.highway_median_meters;
          for (const double side : {-1.0, 1.0}) {
            solid_line(markings, side * lane_edge, mw, raise);
            solid_line(yellow_markings, side * (median + 0.5 * mw), mw, raise);
            for (int l = 1; l < 3; ++l) dashes(markings, side * (median + l * lane), mw, style_.marking_dash_meters, style_.marking_gap_meters, raise);
          }
          // The median: a raised kerbed strip between the carriageways.
          for (int k = 0; k < n; ++k) {
            if (!stands[k] || !stands[k + 1]) continue;
            const auto p0 = ribbon_point(k, -median, 0), p1 = ribbon_point(k, median, 0), p2 = ribbon_point(k + 1, median, 0), p3 = ribbon_point(k + 1, -median, 0);
            const math::Vec3 up{0, 0.22, 0};
            add_quad(medians, p0 + up, p1 + up, p2 + up, p3 + up);
            add_quad(medians, p0, p0 + up, p3 + up, p3, {-g.right.x, 0, -g.right.y});
            add_quad(medians, p1, p1 + up, p2 + up, p2, {g.right.x, 0, g.right.y});
          }
          break;
        }
        case RoadClass::Ramp:
          for (const double side : {-1.0, 1.0}) solid_line(markings, side * lane_edge, mw, raise);
          break;
        case RoadClass::Runway: {
          // Centreline dashes, edge lines and the threshold stripes at both ends.
          for (const double side : {-1.0, 1.0}) solid_line(markings, side * (half - 1.0), 0.45, raise);
          dashes(markings, 0.0, 0.9, 30.0, 20.0, raise);
          const int stripes = 4;
          for (const double end : {0.0, 1.0}) {
            const double from = end == 0.0 ? 6.0 : g.length - 36.0, to = from + 30.0;
            if (from < 0 || to > g.length) continue;
            for (int k = 0; k < stripes; ++k)
              for (const double side : {-1.0, 1.0}) {
                const double off = side * (half * 0.18 + k * half * 0.19);
                const auto at = [&](double along, double o) { return lift(g.a + g.dir * along + g.right * o, deck_height(seg, along) + raise); };
                add_quad(markings, at(from, off - 0.9), at(from, off + 0.9), at(to, off + 0.9), at(to, off - 0.9));
              }
          }
          break;
        }
        case RoadClass::Taxiway:
          solid_line(yellow_markings, 0.0, 0.3, raise);
          break;
        case RoadClass::Apron: break;
      }
    }
    if (!sound) damaged_surfaces.push_back(std::move(own));
    if (!bridge) continue;
    // The deck slab, the railings, the piers — and what is left of a destroyed span.
    const math::Vec3 along3{g.dir.x, 0.0, g.dir.y}, across3{g.right.x, 0.0, g.right.y};
    const double rise = (g.yb - g.ya) / std::max(g.length, 1e-9);
    const math::Vec3 deck_along = math::Vec3{g.dir.x, rise, g.dir.y}.normalized();
    const math::Vec3 deck_up = across3.cross(deck_along).normalized() * (across3.cross(deck_along).y < 0 ? -1.0 : 1.0);
    MeshPart deck; deck.what = "deck"; deck.color = mix(concrete, charred, std::clamp(st.damage, 0.0, 1.0) * 0.7); deck.material = SurfaceMaterial::Concrete;
    const auto slab = [&](double from, double to) {
      const double mid = 0.5 * (from + to), len = to - from;
      const math::Vec2 c = g.a + g.dir * mid;
      const double top = deck_height(seg, mid);
      const math::Vec3 centre3 = lift(c, top) - deck_up * (0.5 * style_.deck_thickness_meters);
      add_box(deck, centre3, deck_along, deck_up, across3, {0.5 * len / deck_along.dot(along3), 0.5 * style_.deck_thickness_meters, half + 0.15});
    };
    const auto rails = [&](double from, double to) {
      if (style_.railing_height_meters <= 0 || to - from < 0.2) return;
      const double mid = 0.5 * (from + to), len = to - from;
      for (const double side : {-1.0, 1.0}) {
        const math::Vec2 c = g.a + g.dir * mid + g.right * (side * (half - kRailingRadius));
        const double top = deck_height(seg, mid);
        for (const double h : {style_.railing_height_meters, 0.5 * style_.railing_height_meters})
          add_box(railings, lift(c, top) + deck_up * h, deck_along, deck_up, across3, {0.5 * len / deck_along.dot(along3), 0.05, 0.05});
        for (double a = from + 0.5 * style_.railing_post_spacing_meters; a < to; a += style_.railing_post_spacing_meters) {
          const math::Vec2 p = g.a + g.dir * a + g.right * (side * (half - kRailingRadius));
          add_box(railings, lift(p, deck_height(seg, a)) + deck_up * (0.5 * style_.railing_height_meters), deck_along, deck_up, across3,
                  {0.06, 0.5 * style_.railing_height_meters, 0.06});
        }
      }
    };
    if (!st.destroyed) { slab(0.0, g.length); rails(0.0, g.length); }
    else {
      const double stub = std::min(style_.stub_meters, 0.5 * g.length);
      if (stub > 0.05) { slab(0.0, stub); slab(g.length - stub, g.length); rails(0.0, stub); rails(g.length - stub, g.length); }
      // The fallen spans lie tilted on whatever is below.
      const double piece = std::min(style_.pier_spacing_meters, g.length - 2.0 * stub) * 0.8;
      if (piece > 1.0)
        for (double a = stub + 0.5 * piece / 0.8; a + 0.5 * piece <= g.length - stub + 1e-9; a += piece / 0.8) {
          const auto k = static_cast<std::uint32_t>(std::lround(a));
          const double tilt = (hash01(seg, k, style_.seed) - 0.5) * 0.9, pitch = (hash01(seg, k + 7u, style_.seed) - 0.5) * 0.35;
          const math::Vec2 c = g.a + g.dir * a + g.right * ((hash01(seg, k + 3u, style_.seed) - 0.5) * half);
          const double ground = ground_at(c.x, c.y, deck_height(seg, a));
          // Rolled about its length by the tilt, then pitched about its width.
          math::Vec3 up = math::Vec3{0, 1, 0} * std::cos(tilt) + across3 * std::sin(tilt);
          const math::Vec3 al = along3 * std::cos(pitch) + up * std::sin(pitch);
          up = up * std::cos(pitch) - along3 * std::sin(pitch);
          const math::Vec3 ac = al.cross(up).normalized();
          add_box(rubble_part, lift(c, ground) + up * (0.5 * style_.deck_thickness_meters) + math::Vec3{0, half * std::abs(std::sin(tilt)), 0}, al, up, ac,
                  {0.5 * piece, 0.5 * style_.deck_thickness_meters, half});
        }
    }
    for (const auto& p : pier_positions(seg)) {
      const double along = (p - g.a).dot(g.dir);
      const double top = deck_height(seg, along) - style_.deck_thickness_meters;
      const double bottom = ground_at(p.x, p.y, top) - 0.5;  // founded a little into the ground
      if (top - bottom < kMinPierHeight) continue;
      add_box(piers, lift(p, 0.5 * (top + bottom)), along3, {0, 1, 0}, across3, {0.5 * style_.pier_width_meters, 0.5 * (top - bottom), half * 0.7});
    }
    decks.push_back(std::move(deck));
  }
  for (auto& [key, part] : merged) if (!part.indices.empty()) parts.push_back(std::move(part));
  for (auto& part : damaged_surfaces) if (!part.indices.empty()) parts.push_back(std::move(part));
  for (auto& part : decks) parts.push_back(std::move(part));
  for (auto* p : {&markings, &yellow_markings, &medians, &railings, &piers, &rubble_part}) if (!p->indices.empty()) parts.push_back(std::move(*p));
  return parts;
}

void RoadNetwork::emit(std::vector<FireInstance>& flames, std::vector<FireInstance>& smoke, const FireStyle& style) const {
  for (std::size_t i = 0; i < geometry_.size(); ++i) {
    const auto& st = states_[i];
    const auto& g = geometry_[i];
    const auto seg = static_cast<std::uint32_t>(i);
    const bool bridge = def_.segments[i].kind == RoadKind::Bridge;
    if (st.damage <= 0) continue;
    if (st.destroyed) {
      // Burned through, a span smoulders at its stubs and a road where the trunks lie.
      if (st.destroyed_by != "fire") continue;
      std::vector<double> spots;
      if (bridge) spots = {std::min(style_.stub_meters, g.length * 0.25), g.length - std::min(style_.stub_meters, g.length * 0.25)};
      else for (const auto& b : blockages_) if (b.segment == seg) spots.push_back(b.along_meters);
      for (const double along : spots) {
        const math::Vec2 p = g.a + g.dir * along;
        FireInstance pl{};
        pl.position[0] = f(p.x); pl.position[1] = f(deck_height(seg, along)); pl.position[2] = f(p.y);
        pl.size = f(style.smoke_height_meters * 0.25); pl.phase = f(2.0 * kPi * hash01(seg, 3, style.seed));
        pl.intensity = 0.25f; pl.kind = 2.0f; pl.seed = f(hash01(seg, 4, style.seed) * 100.0);
        smoke.push_back(pl);
      }
      continue;
    }
    // Alight where the burning trunks lie: flames along the deck, a plume above.
    for (const auto& b : blockages_) {
      if (b.segment != seg || !b.burning) continue;
      const int count = bridge ? 5 : 2;
      for (int k = 0; k < count; ++k) {
        const double along = std::clamp(b.along_meters + (k - (count - 1) * 0.5) * 3.0, 0.0, g.length);
        const math::Vec2 p = g.a + g.dir * along + g.right * ((hash01(seg, 10u + k, style.seed) - 0.5) * road_width(style_) * 0.6);
        FireInstance fl{};
        fl.position[0] = f(p.x); fl.position[1] = f(deck_height(seg, along)); fl.position[2] = f(p.y);
        fl.size = f((bridge ? 2.5 : 1.2) * style.flame_scale * (0.5 + 0.5 * st.damage));
        fl.phase = f(2.0 * kPi * hash01(seg, 20u + k, style.seed));
        fl.intensity = f(0.4 + 0.6 * st.damage); fl.kind = 0.0f; fl.seed = f(hash01(seg, 30u + k, style.seed) * 100.0);
        flames.push_back(fl);
      }
      const math::Vec2 p = g.a + g.dir * b.along_meters;
      FireInstance pl{};
      pl.position[0] = f(p.x); pl.position[1] = f(deck_height(seg, b.along_meters) + 1.0); pl.position[2] = f(p.y);
      pl.size = f(style.smoke_height_meters * (bridge ? 0.5 : 0.3) * (0.4 + 0.6 * st.damage)); pl.phase = f(2.0 * kPi * hash01(seg, 40, style.seed));
      pl.intensity = f(0.4 + 0.6 * st.damage); pl.kind = 1.0f; pl.seed = f(hash01(seg, 41, style.seed) * 100.0);
      smoke.push_back(pl);
    }
  }
}

// ---------------------------------------------------------------------------
std::size_t clear_vegetation_along(VegetationPlacement& placement, const RoadNetwork& network, double tree_margin_meters, double grass_margin_meters) {
  if (!within(tree_margin_meters, 0, 100) || !within(grass_margin_meters, 0, 100)) throw std::invalid_argument("vegetation margins must be 0..100 m");
  if (network.empty()) return 0;
  const auto& def = network.def();
  const auto near_road = [&](float px, float pz, double margin) {
    const math::Vec2 p{px, pz};
    bool hit = false;
    network.for_each_segment_near(px, pz, 0.5 * network.max_width() + margin + 1.0, [&](std::uint32_t i) {
      if (hit) return;
      const auto& a = def.nodes[def.segments[i].a].position, b = def.nodes[def.segments[i].b].position;
      const math::Vec2 d = b - a;
      const double len = d.length();
      if (len < 1e-9) return;
      const double along = std::clamp((p - a).dot(d) / len, 0.0, len);
      if ((p - (a + d * (along / len))).length() <= 0.5 * network.width(i) + margin) hit = true;
    });
    return hit;
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
      if (near_road(t.position[0], t.position[2], tree_margin_meters)) { ++removed; continue; }
      out.trees.push_back(t); ++kept.tree_count; out.trees_per_kind[t.kind > 0.5f ? 1 : 0]++;
    }
    for (std::uint32_t k = 0; k < c.grass_count; ++k) {
      const auto& gr = placement.grass[c.grass_first + k];
      if (near_road(gr.position[0], gr.position[2], grass_margin_meters)) { ++removed; continue; }
      out.grass.push_back(gr); ++kept.grass_count;
    }
    if (kept.tree_count || kept.grass_count) out.chunks.push_back(kept);
  }
  placement = std::move(out);
  return removed;
}

}  // namespace terraflux::graphics
