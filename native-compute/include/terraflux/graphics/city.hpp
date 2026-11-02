#pragma once

#include <terraflux/graphics/buildings.hpp>
#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/roads.hpp>
#include <terraflux/math.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace terraflux::graphics {

// A city over the terrain, planned from zones and highway corridors:
//
//  - Every zone but the airport is a street lattice whose lines are warped
//    by a smooth sine field, so the streets curve and the blocks are not
//    quite square, clipped to the zone's disc. The central business district
//    has avenues (four lanes) every third line and large blocks filled with
//    skyscrapers, a hospital and a police station; a suburb has streets of
//    houses and bungalows on smaller blocks, and every other suburb a police
//    station.
//  - The airport is a runway with a parallel taxiway joined at both ends, an
//    apron off the taxiway, and the terminal, control tower and hangars on
//    the apron's far side; the apron's end is the airport's road connection.
//  - Highways (six lanes and a median) follow the given corridors with their
//    corners filleted into arcs. Every zone is joined to the nearest highway
//    by an avenue; where that meets a straight run of highway a diamond
//    interchange is built — the highway rises on a viaduct over the avenue
//    and four curved ramps join the two — and where there is no room (near
//    a highway's end or another interchange) the avenue joins at grade.
//  - Lots: every block edge in a zone is parcelled along both sides into
//    building sites of the zone's mix; every building therefore stands
//    beside a road with its door to it. Unsuitable lots (uneven, wet, on
//    another carriageway, on another building) are left empty.
//
// Everything is deterministic for a plan and a seed. The result is a road
// network definition (validated: connected, no stand-alone bridge) and the
// building requests to place on it.
enum class ZoneKind : std::uint8_t { Cbd, Suburb, Airport };
const char* zone_kind_name(ZoneKind kind);

struct CityZone {
  ZoneKind kind = ZoneKind::Suburb;
  std::string name;
  math::Vec2 centre;
  double radius_meters = 200.0;     // 50 .. 5000
  double rotation_radians = 0.0;    // of the lattice (or the runway)
};

struct CityPlan {
  std::vector<CityZone> zones;
  std::vector<std::vector<math::Vec2>> highways;  // corridors as corner polylines
  math::Vec2 lower, upper;          // plan bounds the city must stay within (with the margin)
  double bounds_margin_meters = 40.0;
  double cbd_block_meters = 120.0;  // 60 .. 400
  double suburb_block_meters = 100.0;  // 40 .. 400
  double warp_amplitude_meters = 14.0;  // 0 .. 60: how far the lattice lines wander
  double warp_wavelength_meters = 320.0;  // 50 .. 5000
  double fillet_radius_meters = 180.0;    // 20 .. 2000 at highway corners
  double interchange_lift_meters = 7.0;   // 4 .. 20 of the viaduct over the avenue
  double runway_length_meters = 750.0;    // 100 .. 4000
  std::uint32_t seed = 0;
};
void validate_city_plan(const CityPlan& plan);

struct CityInterchange { std::string zone; math::Vec2 at; bool diamond = true; };  // diamond, or an at-grade junction

struct CityStats {
  std::size_t zones = 0, cbds = 0, suburbs = 0, airports = 0, interchanges = 0, at_grade_junctions = 0, lots = 0;
};

// A plan rectangle kept clear of plants (an airfield).
struct CityClearing { math::Vec2 centre, along; double half_length = 0.0, half_width = 0.0; };

struct CityLayout {
  RoadNetworkDef roads;
  std::vector<BuildingRequest> buildings;  // every one with a site
  std::vector<CityInterchange> interchanges;
  std::vector<CityClearing> clearings;
  CityStats stats;
};
// Removes the plants inside the clearings, keeping the chunks consistent. Returns how many were removed.
std::size_t clear_vegetation_in(VegetationPlacement& placement, std::span<const CityClearing> clearings);

// Plans the city: the routes of every zone and highway (with interchanges)
// are planned into a network over the ground (bridges where the terrain
// asks for them, viaducts at the interchanges), and the lots are parcelled
// along the network's block edges. The building requests are to be placed
// with place_buildings(..., skip_unsuitable_sites = true).
CityLayout plan_city(const CityPlan& plan, const StandingHeightFn& ground, std::optional<double> water_level, const RoadStyle& road_style,
                     const RoadPlanStyle& plan_style);

// The demo plan for a terrain: the zones around a centre within the bounds
// (a CBD north-west, five suburbs around the lake, an airport in the south,
// a western ring highway, the lake crossing and a southern link). Positions
// are offsets from `centre`; `extent` is the terrain's half size.
CityPlan demo_city_plan(math::Vec2 centre, math::Vec2 lower, math::Vec2 upper);
// A generic plan for any terrain: a CBD at the centre, five suburbs and an
// airport around it on a ring, a highway ring between them.
CityPlan generic_city_plan(math::Vec2 centre, math::Vec2 lower, math::Vec2 upper);

}  // namespace terraflux::graphics
