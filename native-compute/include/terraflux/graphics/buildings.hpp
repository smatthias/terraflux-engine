#pragma once

#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/collision.hpp>
#include <terraflux/graphics/fire_effects.hpp>
#include <terraflux/graphics/mesh_parts.hpp>
#include <terraflux/graphics/roads.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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
//              furnished as one room the walker enters.
//  House       two storeys, stone walls, a red tiled gable roof, glass
//              windows; entered too: a living room, a kitchen and a
//              bathroom downstairs, a stair up to the bedroom.
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
// the doors are closed) — except the bungalow and the house, whose doors
// open onto furnished rooms (see Interiors below) — the footpaths are surfaces it
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
  double rubble_speed_factor = 0.7;        // 0.1 .. 1 over the rubble of a collapsed building
  // Hazards (see BuildingHazards below).
  double heat_reach_meters = 12.0;         // 1 .. 200: a fully burning building's heat fades out by this distance from its walls
  double spread_heat = 0.85;               // 0 .. 1: the heat a burning element puts on the others of its building
  double wind_burn_factor = 0.06;          // 0 .. 1 per m/s: burns faster in wind
  double rain_slowdown_mm_h = 25.0;        // 1 .. 500: this much rain halves the burning
  double flood_current_mps = 3.0;          // 0.1 .. 30 at the walls ...
  double flood_depth_meters = 1.0;         // 0 .. 20 ... this deep ...
  double flood_scour_seconds = 60.0;       // 1 .. 86400 ... for this long scours the foundation: the building goes
  double strike_radius_meters = 15.0;      // 0 .. 200: a strike this close to the walls hits the building (the tallest of several)
  double strike_scorch = 0.05;             // 0 .. 1 damage to a roof that does not burn
  double collapse_damage = 0.6;            // 0 .. 1 of health taken by the building coming down on the walker inside
  double roof_fall_damage = 0.35;          // 0 .. 1 of health taken by the roof falling in on the walker inside
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

// Interiors. The bungalow and the house are built hollow — their walls with
// a doorway and real window holes, a timber floor, a ceiling — with a door
// that swings open on its hinge and furnished rooms. The bungalow is one
// room: a kitchen (cupboards, worktop, sink, stove and a wall cupboard)
// along the back wall, a table with four chairs, a wardrobe and a bed. The
// house has three rooms downstairs behind plaster partition walls with open
// doorways — a living room inside the front door (a television on its
// stand, a sofa, a low table), a kitchen at the back left (a cooking area
// of cupboards, worktop, sink and stove, a tall kitchen cabinet, a table
// with four chairs) and a bathroom at the back right (a toilet, a
// washbasin, a cabinet) — and a straight wooden staircase up the left wall
// of the living room to the bedroom that is the whole upper floor (a bed, a
// wardrobe), with a handrail along the stair and a banister round the
// stairwell. The walker opens the door (E), steps in onto the floor, walks
// from room to room, climbs the stairs and bumps into the furniture; the
// other kinds stay solid.
bool building_enterable(BuildingKind kind);
// The rooms, partition walls, stair and rails of an enterable kind, in the
// building's local frame (a along the front wall, b toward the road;
// heights over the ground floor).
struct InteriorRoom { const char* name = ""; double a0 = 0, a1 = 0, b0 = 0, b1 = 0; int level = 0; };
struct InteriorOpening { double along = 0, width = 0, height = 0; };          // a doorway: its centre along the wall from `from`
struct InteriorWall { math::Vec2 from, to; double bottom = 0, top = 0; std::vector<InteriorOpening> openings; };  // a partition, kInteriorWallThickness thick
struct InteriorStair { math::Vec2 foot, head; double width = 1.0, rise = 0; };  // a straight flight: the floor at `foot`, `rise` over it at `head`
struct InteriorRail { math::Vec2 from, to; double bottom = 0, top = 0; };      // a handrail or banister: posts from bottom to top
struct InteriorPlan {
  std::vector<InteriorRoom> rooms;
  std::vector<InteriorWall> walls;
  std::optional<InteriorStair> stair;
  std::vector<InteriorRail> rails;
  double upper_floor = 0.0;   // the upper floor's height over the ground floor (0: one storey)
  double slab = 0.25;         // the upper floor slab's thickness
};
InteriorPlan interior_plan(const BuildingSpec& spec);
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

// Hazards. A building is made of elements — the walls, the roof, the
// windows, the door and (in an enterable one) the interior — each of a
// material (mesh_parts.hpp: material_hazard). What happens to it:
//
//  fire      A flammable element (thatch, planks, timber, fabrics) under
//            enough heat for long enough catches: from burning trees and
//            logs beside it (the hazards' heat function), from a burning
//            trunk lying on it, from lightning, from another burning
//            building, from a torch (ignite). Alight, it burns through in
//            its material's time (faster in wind, slower in rain) and heats
//            the building's other elements (spread_heat), so a thatch roof
//            lights the plank walls and the room; a building's heat reaches
//            the walker and its neighbours (heat_at). A non-flammable
//            element is damaged by heat above its threshold: glass bursts,
//            paint blisters, metal buckles, masonry spalls only in extreme
//            heat. Burned-through walls bring the building down (collapsed);
//            a burned-through roof falls in.
//  impact    A trunk landing across the footprint smashes the roof by its
//            mass (tiles and thatch go under a few hundred kilos, concrete
//            takes tonnes), and the walls by half of it; a burning trunk on
//            the roof keeps heating it.
//  flood     Water over the floor (flood_depth) soaks the interior, the
//            door and the walls: organic materials rot in their soak time;
//            a torrent at the walls for flood_scour_seconds scours the
//            foundation and the building goes, whatever it is made of; water
//            over a burning element puts it out.
//  wind      A gale tears at the roof and the windows: thatch from 28 m/s,
//            tiles from 38, glass from 40, stripped in their wind times.
//  lightning A strike within strike_radius of the walls (the tallest of the
//            buildings it reaches) lights a flammable roof or scorches one
//            that does not burn. Inside, the walker is sheltered.
//
// The events tell the viewer what happened; a collapse or a roof falling in
// hurts a walker inside (collapse_damage, roof_fall_damage). A destroyed
// building is a heap of rubble: a surface walked slowly, no walls, no door,
// no room — the chimney stands in it.
enum class BuildingElement : std::uint8_t { Walls, Roof, Windows, Door, Interior, Count };
const char* building_element_name(BuildingElement element);
// The element's material for a kind (nullopt: the kind has no such element).
std::optional<SurfaceMaterial> building_element_material(BuildingKind kind, BuildingElement element);
struct ElementState {
  SurfaceMaterial material = SurfaceMaterial::Plain;
  bool present = false;
  double damage = 0.0;          // 0 sound .. 1 gone (burned through, burst, smashed, rotted, stripped)
  bool alight = false;
  double intensity = 0.0;       // 0 .. 1 flames
  double heat_seconds = 0.0;    // of heat past the ignition or damage threshold, toward catching or cracking
  double soaked_seconds = 0.0;  // under water
  double wind_seconds = 0.0;    // in a tearing wind
  std::string cause;            // what damaged it last: "fire", "heat", "impact", "flood", "wind", "lightning"
};
struct BuildingState {
  std::array<ElementState, static_cast<std::size_t>(BuildingElement::Count)> elements;
  double flood_depth_meters = 0.0;  // water over the floor
  double scour_seconds = 0.0;       // a torrent at the walls for this long
  bool roof_fallen = false;         // the roof has come down (burned through, smashed, or with the walls)
  bool destroyed = false;           // collapsed: rubble
  std::string destroyed_by;         // "fire", "impact", "flood", or what destroy() was told
  std::uint32_t strikes = 0;        // lightning
  double burn_seconds = 0.0;        // any element alight for this long
  const ElementState& element(BuildingElement e) const { return elements[static_cast<std::size_t>(e)]; }
  ElementState& element(BuildingElement e) { return elements[static_cast<std::size_t>(e)]; }
  bool alight() const;              // any element burning
  double intensity() const;         // the strongest flames
  double integrity() const;         // 1 sound .. 0: the walls' and the roof's soundness
};
// A burning trunk lying on a building.
struct BuildingLog { std::size_t building = 0; std::uint32_t tree = 0; bool burning = false; };
struct BuildingEvent {
  enum class Kind : std::uint8_t { Alight, BurnedOut, Damaged, RoofFell, Collapsed, WashedOut, Extinguished, Struck, Hit, Flooded } kind;
  std::size_t building = 0;
  BuildingElement element = BuildingElement::Walls;  // for Alight, BurnedOut, Damaged, Extinguished
  int tree = -1;                                     // for Hit
  std::string cause;                                 // for Damaged, Collapsed
};
const char* building_event_name(BuildingEvent::Kind kind);
// What the world does to the buildings this step (all optional).
struct BuildingHazards {
  std::function<double(double x, double z)> heat;          // external fire heat 0 .. 1 at a plan position (burning trees, logs, embers)
  std::function<double(std::uint32_t tree)> log_intensity; // of a trunk lying on a building (0 once it has burned out)
  WaterFn water;                                            // flood and lake water at a plan position over its ground
  double wind_speed_mps = 0.0;
  double rain_mm_h = 0.0;
};

struct BuildingStats {
  std::size_t buildings = 0, bungalows = 0, houses = 0, hospitals = 0, skyscrapers = 0, police_stations = 0, terminals = 0, towers = 0, hangars = 0, interiors = 0;
  std::size_t alight = 0, damaged = 0, roofless = 0, destroyed = 0, flooded = 0, struck = 0, logs = 0;
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
  // ramping up to the door where the floor stands higher), the ground floor
  // of an enterable building's rooms, or the rubble of a destroyed one — or
  // nullopt. With the feet's height: of the building's levels (the ground
  // floor, the stair as a ramp, the upper floor outside the stairwell) the
  // highest no more than the step height (0.5 m) above the feet.
  std::optional<double> platform_height(double x, double z) const;
  std::optional<double> platform_height(double x, double z, double feet_y) const;
  // The room the point lies in at the feet's height ("kitchen", "stairs", ...), or nullptr.
  const char* room_at(std::size_t building, double x, double z, double feet_y) const;
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
  // (returns false for a solid building, or one whose door is gone); advance
  // moves the swinging doors (0.6 s for a full swing) and runs the hazards;
  // door_version changes while any door moves.
  const std::vector<DoorState>& doors() const { return doors_; }
  bool toggle_door(std::size_t building);
  void advance(double dt, const BuildingHazards& hazards = {});
  std::uint64_t door_version() const { return door_version_; }
  // Hazards (see above). The states as they stand; the events of the last
  // advance / ignite / strike / log_landed / destroy (cleared by advance);
  // version changes whenever the drawn geometry, the surfaces or the
  // obstacles change (damage is drawn in steps of a tenth).
  const std::vector<BuildingState>& states() const { return states_; }
  const std::vector<BuildingLog>& logs() const { return logs_; }
  const std::vector<BuildingEvent>& events() const { return events_; }
  std::uint64_t version() const { return version_; }
  // Sets an element alight (a torch, an ember): false when it is absent, not
  // flammable, already alight, burned through, or under water.
  bool ignite(std::size_t building, BuildingElement element, const char* cause = "fire");
  // A lightning strike at a plan position: the tallest building within the
  // strike radius of its walls is struck (event Struck; a flammable roof
  // alight, another scorched). Returns the building or nullopt.
  std::optional<std::size_t> strike(double x, double z);
  // A trunk came down: lying from `base` along `direction` (unit, plan) for
  // `length` metres, weighing `mass_kg`. Every building whose footprint it
  // crosses is hit (event Hit): the roof smashed by the mass, the walls by
  // half of it; a burning trunk stays on the roof as a log that heats it
  // while it burns (log_intensity). Returns how many buildings were hit.
  std::size_t log_landed(std::uint32_t tree, math::Vec2 base, math::Vec2 direction, double length, bool burning, double mass_kg);
  // Brings a building down outright (rubble).
  void destroy(std::size_t building, const char* cause);
  // Everything sound, dry, unlit and standing again; the doors closed.
  void reset();
  // What a walker feels from the burning buildings at a plan position: heat
  // (1 inside or at the walls of a fully burning building, fading to 0 by
  // heat_reach) and smoke (their intensity summed, fading by 60 m), 0 .. 1.
  struct Exposure { double heat = 0.0, smoke = 0.0; };
  Exposure exposure_at(double x, double z) const;
  // Whether the point is under a sound roof (inside a standing enterable building whose roof has not gone).
  bool sheltered(double x, double z) const;
  // Flames on the burning elements and plumes over them (a burned-out
  // building smoulders), appended.
  void emit(std::vector<FireInstance>& flames, std::vector<FireInstance>& smoke, const FireStyle& style) const;
  // The door leaves as they stand this moment: posts along each leaf, and their meshes.
  std::vector<Obstacle> door_obstacles() const;
  std::vector<MeshPart> build_door_meshes() const;
  // Geometry: foundations, walls, roofs, windows, painted doors, crosses, canopies,
  // footpaths — and the rooms and furniture of the enterable buildings.
  std::vector<MeshPart> build_meshes() const;

 private:
  struct Leaf { math::Vec2 hinge, along; double width, height, bottom; };  // a door leaf's plan as it stands now
  Leaf door_leaf(std::size_t building) const;
  bool door_present(std::size_t building) const;
  void raise(BuildingEvent::Kind kind, std::size_t building, BuildingElement element = BuildingElement::Walls, int tree = -1, const char* cause = "");
  void damage_element(std::size_t building, BuildingElement element, double amount, const char* cause);
  void roof_falls(std::size_t building);
  double external_heat_at(std::size_t building, const BuildingHazards& hazards) const;
  std::vector<Building> buildings_;
  std::vector<DoorState> doors_;
  std::vector<BuildingState> states_;
  std::vector<BuildingLog> logs_;
  std::vector<BuildingEvent> events_;
  std::vector<std::array<int, static_cast<std::size_t>(BuildingElement::Count)>> drawn_levels_;  // damage tenths as last drawn
  StandingHeightFn ground_;
  BuildingPlacementStyle style_;
  std::uint64_t door_version_ = 0, version_ = 0;
};

// Removes the plants standing on the plots (the footprint plus the margin) and
// the footpaths, keeping the chunks consistent. Returns how many were removed.
std::size_t clear_vegetation_for_buildings(VegetationPlacement& placement, const BuildingSet& buildings, double margin_meters = 3.0);

}  // namespace terraflux::graphics
