#pragma once

#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/collision.hpp>
#include <terraflux/graphics/mesh_parts.hpp>
#include <terraflux/graphics/roads.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/math.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace terraflux::graphics {

// Buildings: the settled part of the infrastructure. Every building stands
// beside a road, its front wall parallel to it and its door facing it, and a
// paved footpath runs from the door to the edge of the carriageway — there
// is no building without a road to reach it. Four kinds are built:
//
//  Bungalow    one storey, wooden plank walls, a thatched gable roof, open
//              window holes and a plank door that opens; built hollow and
//              furnished, the one kind the walker enters.
//  House       two storeys, stone walls, a red tiled gable roof, glass windows.
//  Hospital    five storeys, white concrete walls, a flat dark grey roof,
//              glass windows, a canopy over a double glass door, and a red
//              cross on the front and on the roof.
//  Skyscraper  twenty storeys of glass facade with floor bands and mullions,
//              a white flat roof with a plant room, a glass lobby entrance.
//  Police      two storeys, light walls with a blue band and a blue lamp at
//              the door, a flat dark roof with a mast and beacon.
//  Terminal    an airport terminal: two tall storeys of white walls under
//              continuous glass bands, a long canopy over the doors.
//  Tower       an airport control tower: a concrete shaft with a wider
//              glazed cab on top.
//  Hangar      one tall storey of grey metal under a shallow gable, a wide
//              sliding door on the front.
//
// Sites are chosen automatically (the closest suitable spot to a preferred
// point along the roads: flat enough, dry, off the carriageways, clear of the
// other buildings) or given as a road segment, a distance along it and a
// side. The buildings are solid to the walker (their walls are obstacles;
// the doors are closed) — except the bungalow, whose door opens onto a
// furnished room (see Interiors below) — the footpaths are surfaces it
// walks on at the road's pace (ramping up to a raised door), and the plants
// on the plots are cleared.
enum class BuildingKind : std::uint8_t { Bungalow, House, Hospital, Skyscraper, PoliceStation, Terminal, ControlTower, Hangar };
const char* building_kind_name(BuildingKind kind);
std::optional<BuildingKind> building_kind_from_name(const std::string& name);

// What a kind is built like. The defaults are the four kinds above; the
// dimensions are validated (storeys 1 .. 200, storey height 2 .. 6 m,
// footprint 3 .. 200 m).
struct BuildingSpec {
  BuildingKind kind = BuildingKind::House;
  double width_meters = 11.0;         // along the road
  double depth_meters = 9.0;          // away from the road
  std::uint32_t storeys = 2;
  double storey_height_meters = 3.0;
  double setback_meters = 6.0;        // from the edge of the carriageway to the front wall (the footpath's length)
  double path_width_meters = 2.0;
  double max_site_step_meters = 2.0;  // the most the ground may vary under the footprint
  double max_buried_meters = 1.8;     // the most the ground may rise over the floor along the uphill walls — or, for an
                                      // enterable kind (its floor is raised over the highest ground under it), the most
                                      // the door may stand above the ground at it (the footpath ramps up to it)
};
BuildingSpec building_spec(BuildingKind kind);
void validate_building_spec(const BuildingSpec& spec);

struct BuildingSite {
  std::uint32_t segment = 0;  // a road segment (never a bridge)
  double along_meters = 0.0;  // of the front door, from the segment's node a
  int side = 1;               // +1 to the right looking from a to b, -1 to the left
};
struct BuildingRequest {
  BuildingKind kind = BuildingKind::House;
  std::optional<BuildingSite> site;      // nullopt: placed automatically ...
  std::optional<math::Vec2> preferred;   // ... nearest to this point (else the call's preferred point) ...
  double search_radius_meters = 1e9;     // ... among the candidates this close to it
};

struct Building {
  BuildingKind kind = BuildingKind::House;
  BuildingSpec spec;
  BuildingSite site;
  math::Vec2 centre;          // plan centre of the footprint
  math::Vec2 along;           // unit, the front wall's direction (parallel to the road)
  math::Vec2 facing;          // unit, from the building toward the road
  math::Vec2 door;            // plan, the middle of the front wall
  math::Vec2 path_end;        // plan, where the footpath meets the carriageway edge
  double ground_min_meters = 0.0, ground_max_meters = 0.0;  // under the footprint's corners
  double floor_meters = 0.0;  // the ground floor level
  double height_meters = 0.0; // floor to the top of the roof
  std::uint32_t storeys = 0;
};

struct BuildingPlacementStyle {
  double candidate_spacing_meters = 5.0;   // 1 .. 100 along the roads
  double end_margin_meters = 20.0;         // 0 .. 500 kept free at a segment's ends (junctions)
  double gap_meters = 6.0;                 // 0 .. 100 between buildings
  double water_margin_meters = 0.5;        // 0 .. 20 above the water level for every corner
  double surface_lift_meters = 0.1;        // 0 .. 1: the footpath over the ground
  double path_speed_factor = 1.3;          // 1 .. 3 walking the footpath (a paved surface)
  std::uint32_t seed = 0;
};
void validate_building_placement_style(const BuildingPlacementStyle& style);

// Places the requested buildings along the network's roads, in request order:
// an automatic request takes the suitable candidate (every candidate_spacing
// along every road segment, both sides, clear of the end margins) nearest to
// `preferred`; a given site is checked for suitability and rejected
// (std::invalid_argument) when it is not — or, with `skip_unsuitable_sites`,
// left out (a city's lots are offered this way). A request that finds no
// site is left out (the result may be shorter than the requests). Deterministic.
struct PlacementReport { std::size_t placed = 0, no_site = 0, uneven = 0, buried = 0, wet = 0, on_road = 0, no_ground = 0, taken = 0; };
std::vector<Building> place_buildings(const RoadNetwork& roads, std::span<const BuildingRequest> requests, const StandingHeightFn& ground,
                                      std::optional<double> water_level, math::Vec2 preferred, const BuildingPlacementStyle& style,
                                      bool skip_unsuitable_sites = false, PlacementReport* report = nullptr);
// Requests from JSON: "buildings": ["bungalow", {"kind": "hospital", "segment": 2, "along": 120, "side": "left"}, ...]
// in a road network file (see roads.hpp). An absent key gives an empty list.
std::vector<BuildingRequest> parse_building_requests(const std::string& json_text);

// Interiors. The bungalow is built hollow — four plank walls with a doorway,
// a timber floor, a ceiling — with a door that swings open on its hinge and
// a few pieces of furniture: a kitchen (cupboards, worktop, sink, stove and
// a wall cupboard) along the back wall, a table with four chairs, a
// wardrobe and a bed. The walker opens the door (E), steps in onto the floor
// and bumps into the furniture; the other kinds stay solid.
bool building_enterable(BuildingKind kind);
struct DoorState {
  double open = 0.0;      // 0 closed .. 1 swung 90 degrees inward
  bool opening = false;   // the way it is moving (or resting)
};
// A piece of furniture (or a part of one) in the building's local frame:
// a box at (along, across) of the given half sizes standing `bottom` over
// the floor, whether the walker collides with it (a solid piece stands as
// posts no lower than the step height, so it is never climbed) and whether
// it is drawn (a table is drawn as its top and legs and blocks as one box).
struct Furnishing {
  const char* what = "";
  math::Vec2 centre;      // local: along the front wall, toward the road
  double half_along = 0.0, half_across = 0.0, bottom = 0.0, height = 0.0;
  math::Vec3 color;
  SurfaceMaterial material = SurfaceMaterial::Timber;
  bool solid = true;
  bool drawn = true;
};
// The furniture of an enterable building of this spec (local frame): the
// pieces that fit between its walls.
std::vector<Furnishing> furnish(const BuildingSpec& spec);
// The wall thickness of an enterable building and the room's extent (local, plan).
constexpr double kInteriorWallThickness = 0.25;

struct BuildingStats {
  std::size_t buildings = 0, bungalows = 0, houses = 0, hospitals = 0, skyscrapers = 0, police_stations = 0, terminals = 0, towers = 0, hangars = 0, interiors = 0;
  double tallest_meters = 0.0, floor_area_m2 = 0.0;
  std::uint32_t most_storeys = 0;
};

// What the walker meets: the built surfaces and the walls.
class BuildingSet {
 public:
  BuildingSet(std::vector<Building> buildings, StandingHeightFn ground, const BuildingPlacementStyle& style);
  const std::vector<Building>& buildings() const { return buildings_; }
  bool empty() const { return buildings_.empty(); }
  std::size_t size() const { return buildings_.size(); }
  BuildingStats stats() const;
  // The built surface at a plan position — the footpath (the lifted ground,
  // ramping up to the door where the floor stands higher) or the floor of
  // an enterable building's rooms — or nullopt.
  std::optional<double> platform_height(double x, double z) const;
  // The path's speed factor on a footpath or a floor, 1 elsewhere.
  double speed_factor(double x, double z) const;
  // The building whose footprint (plus margin) covers the point, if any.
  std::optional<std::size_t> building_at(double x, double z, double margin_meters = 0.0) const;
  // The building whose door is within `reach` of the point, nearest first.
  std::optional<std::size_t> entrance_near(double x, double z, double reach_meters) const;
  // Whether the point lies inside an enterable building's rooms (within its walls).
  bool inside(std::size_t building, double x, double z) const;
  std::optional<std::size_t> inside_of(double x, double z) const;
  // The walls as chains of posts from the ground to the roof (with the doorway
  // left open in an enterable building) and the furniture.
  std::vector<Obstacle> obstacles() const;
  // Doors. toggle_door sets an enterable building's door swinging the other way
  // (returns false for a solid building); advance moves the swinging doors
  // (0.6 s for a full swing); door_version changes while any door moves.
  const std::vector<DoorState>& doors() const { return doors_; }
  bool toggle_door(std::size_t building);
  void advance(double dt);
  std::uint64_t door_version() const { return door_version_; }
  // The door leaves as they stand this moment: posts along each leaf, and their meshes.
  std::vector<Obstacle> door_obstacles() const;
  std::vector<MeshPart> build_door_meshes() const;
  // Geometry: foundations, walls, roofs, windows, painted doors, crosses, canopies,
  // footpaths — and the rooms and furniture of the enterable buildings.
  std::vector<MeshPart> build_meshes() const;

 private:
  struct Leaf { math::Vec2 hinge, along; double width, height, bottom; };  // a door leaf's plan as it stands now
  Leaf door_leaf(std::size_t building) const;
  std::vector<Building> buildings_;
  std::vector<DoorState> doors_;
  StandingHeightFn ground_;
  BuildingPlacementStyle style_;
  std::uint64_t door_version_ = 0;
};

// Removes the plants standing on the plots (the footprint plus the margin) and
// the footpaths, keeping the chunks consistent. Returns how many were removed.
std::size_t clear_vegetation_for_buildings(VegetationPlacement& placement, const BuildingSet& buildings, double margin_meters = 3.0);

}  // namespace terraflux::graphics
