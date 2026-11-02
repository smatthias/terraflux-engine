#include <terraflux/graphics/opengl_device.hpp>
#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/composite.hpp>
#include <terraflux/graphics/render_pass.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/terrain_shading.hpp>
#include <terraflux/graphics/water_shading.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/graphics/fire_effects.hpp>
#include <terraflux/graphics/tree_fire.hpp>
#include <terraflux/graphics/flood.hpp>
#include <terraflux/graphics/vitals.hpp>
#include <terraflux/graphics/hud.hpp>
#include <terraflux/graphics/body_view.hpp>
#include <terraflux/graphics/items.hpp>
#include <terraflux/graphics/tools.hpp>
#include <terraflux/graphics/roads.hpp>
#include <terraflux/graphics/buildings.hpp>
#include <terraflux/graphics/city.hpp>
#include <terraflux/graphics/weather.hpp>
#include <terraflux/terrain_water.hpp>
#include <terraflux/graphics/terrain_tiles.hpp>
#include <terraflux/graphics/layer_visibility.hpp>
#include <terraflux/graphics/mesh_upload.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/asset_slot.hpp>
#include <terraflux/graphics/shared_texture_cache.hpp>
#include <terraflux/graphics/visibility.hpp>
#include <terraflux/assets/obj.hpp>
#include <terraflux/assets/scene_assets.hpp>
#include <terraflux/assets/gltf.hpp>
#include <terraflux/assets/material.hpp>
#include <terraflux/assets/resource_resolution.hpp>
#include <terraflux/scene.hpp>
#include <terraflux/scene_io.hpp>
#include <terraflux/runtime.hpp>
#include <terraflux/scene_residency.hpp>
#include <terraflux/residency_state.hpp>
#include <terraflux/grid.hpp>
#include <terraflux/math.hpp>
#include <terraflux/replay.hpp>
#include <terraflux/replay_clock.hpp>
#include <terraflux/terrain_mesh.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <filesystem>
#include <map>
#include <utility>
#include <tuple>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct GlfwSession {
  GlfwSession() {
    if (glfwInit() != GLFW_TRUE)
      throw std::runtime_error("GLFW initialization failed (is a display server available?)");
  }
  ~GlfwSession() { glfwTerminate(); }
};
struct WindowDeleter {
  void operator()(GLFWwindow* window) const { if (window) glfwDestroyWindow(window); }
};
using Window = std::unique_ptr<GLFWwindow, WindowDeleter>;

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot open " + path);
  std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (!file.good() && !file.eof()) throw std::runtime_error("error reading " + path);
  return content;
}

std::filesystem::path asset_path(const std::filesystem::path& directory,
                                 const std::filesystem::path& reference) {
  if(reference.empty() || reference.is_absolute() || reference.has_root_path())
    throw std::invalid_argument("material reference must be a relative path");
  for(const auto& part:reference)
    if(part=="..") throw std::invalid_argument("material parent path traversal forbidden");
  return (directory/reference).lexically_normal();
}

struct Options {
  bool smoke_test=false, profile=false, no_culling=false, no_instancing=false,asset_preview=false;
  bool asset_lifecycle_test=false,scene_controls_test=false,scene_detach_test=false,scene_residency_test=false;
  bool residency_report=false,start_inactive=false,resource_recovery_test=false;
  bool offscreen=false,offscreen_resize_test=false;
  bool hide_terrain=false,hide_fire=false,layers_test=false,replay_controls_test=false;
  double replay_speed=1.0;
  bool replay_speed_requested=false;
  std::optional<std::size_t> replay_frame; // start paused on this zero-based frame
  bool terrain_tiles=false,terrain_tile_test=false,terrain_residency=false,terrain_residency_test=false,terrain_retention_test=false;
  bool terrain_shading_test=false;
  std::optional<double> terrain_contour_interval; // unset: automatic spacing
  double terrain_detail=1.0,rock_relief=5.0;
  double render_cell=5.0;                    // render lattice spacing in meters; 0 keeps the simulation lattice
  std::size_t max_render_vertices=4'000'000;
  std::optional<double> snow_line; // unset: automatic (relief-dependent)
  std::optional<double> water_level; // unset: the demo's authored lake; none for replay data
  double wave_height=0.6,wave_length=24.0,wave_speed=3.0,wave_direction=90.0,water_clarity=4.0;
  bool water_test=false;
  std::string water_floor="mixed"; // sand|vegetation|mud|stones|mixed|random
  std::string vegetation="medium";  // off|low|medium|high
  std::string tree_style="mixed";   // conifer|broadleaf|mixed|custom
  std::string tree_obj;             // custom tree model (OBJ) for the "custom" style
  std::optional<double> tree_spacing,grass_spacing,grass_distance,tree_height;
  std::uint32_t vegetation_seed=0;
  bool vegetation_test=false;
  double flame_scale=1.0,smoke_scale=1.0;
  bool fire_test=false;
  // Weather: unset keeps the interactive default (cumulus) and turns weather
  // off in smoke runs, so every reference comparison keeps the neutral sky.
  std::optional<std::string> weather; // off|clear|cirrus|cumulus|stratocumulus|stratus|nimbostratus|cumulonimbus
  std::optional<double> cloud_cover,cloud_base,dew_point;
  double temperature=18.0,stability=0.01;
  std::optional<double> wind_direction; // unset: follows --wave-direction (or the replay request's wind)
  std::optional<double> wind_min,wind_max; // unset: 2..6 m/s (or 0.6..1.3 x the replay request's wind)
  double wind_swing=20.0,wind_period=12.0;
  double hour=10.0,latitude=47.0,day_of_year=172.0,time_scale=10.0,weather_time=0.0,moon_size=2.0;
  std::optional<double> moon_phase;
  double rainfall_mm=0.0; // rain already on the ground at the start (wet, running, flooded) // unset: from the day of the year
  bool weather_test=false;
  std::optional<std::uint32_t> floor_seed;
  std::optional<double> floor_patch_size;
  std::uint32_t max_resident_terrain_tiles=4096,terrain_retention_frames=0;
  std::uint32_t terrain_tile_quads=32;
  std::optional<terraflux::scene::NodeId> selected_node,activate_node;
  std::uint32_t max_resident_meshes=512,max_scene_instances=1024;
  std::optional<std::uint64_t> resource_budget_bytes,cpu_recipe_budget_bytes;
  std::uint32_t benchmark_frames=0;
  std::optional<double> view_yaw,view_pitch,view_zoom; // initial orbit camera: degrees, degrees, fraction of the framing distance
  double view_x=0.0,view_z=0.0; // initial target offset from the terrain centre, meters (follows the ground)
  std::string view_mode="world"; // world (orbit) or first-person: where the camera starts; Tab switches at run time
  std::optional<double> walk_speed; // first-person walking speed, m/s (unset: 2.5, a brisk walk)
  bool first_person_test=false;
  double max_slope=45.0;  // steepest ascent the first-person walker can climb, degrees
  bool no_collision=false; // walk through trees, assets and cliffs
  bool collision_test=false;
  double jump_height=1.0,gravity=9.81; // first-person jump apex (0 disables) and the gravity everything falls with
  bool jump_test=false;
  bool no_gait=false; // glide at constant speed instead of walking in steps
  bool gait_test=false;
  std::string tree_fire="model"; // model: trees burn, drop branches, fall and spread the fire among themselves; grid: no tree-to-tree spread; off: cell look
  bool tree_fire_test=false;
  bool storm_test=false;
  std::optional<double> flash_flood; // start with the channels this full (0..1)
  bool flood_test=false;
  bool climb_test=false;
  bool vitals_test=false;
  bool no_arms=false; // hide the walker's own arms
  bool body_test=false;
  std::string body_pose_override; // idle|walk|run|crouch|climb|swim|crawl|dive|down: draw the arms in this pose (screenshots)
  bool no_stones=false;
  bool items_test=false;
  std::string hold; // stone|branch|torch: start holding one
  std::string tool; // rope|axe|chainsaw: start with this tool in hand
  double rope_length=20.0, chainsaw_fuel=300.0;
  bool tools_test=false;
  std::string roads="auto"; // auto (the city interactively, none in smoke runs) | city | demo (two routes and four buildings) | off | a network JSON file
  bool roads_test=false;
  bool city_test=false;
  std::string buildings="auto"; // auto (the four kinds beside the demo/replay roads, a file's own list) | off
  bool buildings_test=false;
  bool interior_test=false;   // scripted: walk into the bungalow through its door and out again (needs --smoke-test)
  bool hazards_test=false;    // scripted: fire, a falling trunk, a flood and lightning on the buildings (needs --smoke-test)
  std::string load_state,save_state,screenshot_file;
  std::string request_file, response_file, obj_file, gltf_file, texture_file, scene_file;
};

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--smoke-test") options.smoke_test = true;
    else if(arg=="--replay-controls-test")options.replay_controls_test=true;
    else if(arg=="--replay-speed" && i+1<argc) {
      const std::string text=argv[++i];double speed=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),speed);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
        throw std::invalid_argument("invalid --replay-speed");
      (void)terraflux::replay::ReplayClock::speed_units(speed);
      options.replay_speed=speed;options.replay_speed_requested=true;
    }
    else if(arg=="--replay-frame" && i+1<argc) {
      const std::string text=argv[++i];std::size_t frame=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),frame);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || frame>719)
        throw std::invalid_argument("--replay-frame must be 0..719");
      options.replay_frame=frame;
    }
    else if(arg=="--hide-terrain")options.hide_terrain=true;
    else if(arg=="--hide-fire")options.hide_fire=true;
    else if(arg=="--layers-test")options.layers_test=true;
    else if(arg=="--terrain-residency"){options.terrain_residency=true;options.terrain_tiles=true;}
    else if(arg=="--terrain-retention-test")options.terrain_retention_test=true;
    else if(arg=="--terrain-retain-frames" && i+1<argc) {
      const std::string text=argv[++i];std::uint32_t n=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || n>terraflux::graphics::kMaxTerrainRetentionFrames)
        throw std::invalid_argument("--terrain-retain-frames must be 0..600");
      options.terrain_residency=true;options.terrain_tiles=true;options.terrain_retention_frames=n;
    }
    else if(arg=="--terrain-residency-test")options.terrain_residency_test=true;
    else if(arg=="--max-resident-terrain-tiles" && i+1<argc) {
      const std::string text=argv[++i];std::uint32_t n=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || n>4096)
        throw std::invalid_argument("--max-resident-terrain-tiles must be 0..4096");
      options.terrain_residency=true;options.terrain_tiles=true;options.max_resident_terrain_tiles=n;
    }
    else if(arg=="--terrain-contours" && i+1<argc) {
      const std::string text=argv[++i];double meters=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),meters);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(meters) ||
         meters<0 || meters>10000)
        throw std::invalid_argument("--terrain-contours must be 0..10000 meters");
      options.terrain_contour_interval=meters;
    }
    else if((arg=="--terrain-detail" || arg=="--snow-line") && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value) || value<0 || value>1)
        throw std::invalid_argument(arg+" must be 0..1");
      if(arg=="--terrain-detail")options.terrain_detail=value;else options.snow_line=value;
    }
    else if(arg=="--render-cell" && i+1<argc) {
      const std::string text=argv[++i];double meters=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),meters);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(meters) || meters<0 || meters>10000)
        throw std::invalid_argument("--render-cell must be 0..10000 meters (0 keeps the simulation lattice)");
      options.render_cell=meters;
    }
    else if(arg=="--max-render-vertices" && i+1<argc) {
      const std::string text=argv[++i];std::size_t count=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),count);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || count<1000 || count>20'000'000)
        throw std::invalid_argument("--max-render-vertices must be 1000..20000000");
      options.max_render_vertices=count;
    }
    else if(arg=="--rock-relief" && i+1<argc) {
      const std::string text=argv[++i];double meters=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),meters);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(meters) || meters<0 || meters>20)
        throw std::invalid_argument("--rock-relief must be 0..20 meters");
      options.rock_relief=meters;
    }
    else if((arg=="--water-level" || arg=="--wave-height" || arg=="--wave-length" || arg=="--wave-speed" ||
             arg=="--wave-direction" || arg=="--water-clarity") && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value))
        throw std::invalid_argument(arg+" must be a finite number");
      const auto bounded=[&](double lo,double hi,const char* what) {
        if(value<lo || value>hi)throw std::invalid_argument(arg+" must be "+what);
      };
      if(arg=="--water-level"){bounded(-12000,12000,"-12000..12000 meters");options.water_level=value;}
      else if(arg=="--wave-height"){bounded(0,5,"0..5 meters");options.wave_height=value;}
      else if(arg=="--wave-length"){bounded(1,1000,"1..1000 meters");options.wave_length=value;}
      else if(arg=="--wave-speed"){bounded(0,50,"0..50 m/s");options.wave_speed=value;}
      else if(arg=="--water-clarity"){bounded(0.5,50,"0.5..50 meters");options.water_clarity=value;}
      else {bounded(0,360,"0..360 compass degrees");options.wave_direction=value;}
    }
    else if(arg=="--water-test")options.water_test=true;
    else if(arg=="--vegetation" && i+1<argc) {
      options.vegetation=argv[++i];
      if(options.vegetation!="off" && options.vegetation!="low" && options.vegetation!="medium" && options.vegetation!="high")
        throw std::invalid_argument("--vegetation must be off, low, medium or high");
    }
    else if(arg=="--tree-style" && i+1<argc) {
      options.tree_style=argv[++i];
      if(options.tree_style!="conifer" && options.tree_style!="broadleaf" && options.tree_style!="mixed" && options.tree_style!="custom")
        throw std::invalid_argument("--tree-style must be conifer, broadleaf, mixed or custom");
    }
    else if(arg=="--tree-obj" && i+1<argc)options.tree_obj=argv[++i];
    else if((arg=="--tree-spacing" || arg=="--grass-spacing" || arg=="--grass-distance" || arg=="--tree-height") && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value))
        throw std::invalid_argument(arg+" must be a finite number");
      const auto bounded=[&](double lo,double hi,const char* what){if(value<lo || value>hi)throw std::invalid_argument(arg+" must be "+what);};
      if(arg=="--tree-spacing"){bounded(4,200,"4..200 meters");options.tree_spacing=value;}
      else if(arg=="--grass-spacing"){bounded(0.5,20,"0.5..20 meters");options.grass_spacing=value;}
      else if(arg=="--grass-distance"){bounded(0,5000,"0..5000 meters");options.grass_distance=value;}
      else {bounded(1,80,"1..80 meters");options.tree_height=value;}
    }
    else if(arg=="--vegetation-seed" && i+1<argc) {
      const std::string text=argv[++i];std::uint32_t seed=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),seed);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())throw std::invalid_argument("--vegetation-seed must be an unsigned 32-bit integer");
      options.vegetation_seed=seed;
    }
    else if(arg=="--vegetation-test")options.vegetation_test=true;
    else if((arg=="--flame-scale" || arg=="--smoke-scale") && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value) || value<0 || value>3)
        throw std::invalid_argument(arg+" must be 0..3");
      if(arg=="--flame-scale")options.flame_scale=value;else options.smoke_scale=value;
    }
    else if(arg=="--fire-test")options.fire_test=true;
    else if(arg=="--weather" && i+1<argc) {
      options.weather=argv[++i];
      if(*options.weather!="off" && *options.weather!="auto")(void)terraflux::graphics::parse_cloud_type(options.weather->c_str());
    }
    else if((arg=="--cloud-cover" || arg=="--cloud-base" || arg=="--temperature" || arg=="--dew-point" || arg=="--stability" ||
             arg=="--wind-direction" || arg=="--wind-min" || arg=="--wind-max" || arg=="--wind-swing" || arg=="--wind-period" ||
             arg=="--hour" || arg=="--latitude" || arg=="--day-of-year" || arg=="--time-scale" || arg=="--weather-time" ||
             arg=="--moon-phase" || arg=="--moon-size" || arg=="--rainfall") && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value))
        throw std::invalid_argument(arg+" must be a finite number");
      const auto bounded=[&](double lo,double hi,const char* what){if(value<lo || value>hi)throw std::invalid_argument(arg+" must be "+what);};
      if(arg=="--cloud-cover"){bounded(0,1,"0..1");options.cloud_cover=value;}
      else if(arg=="--cloud-base"){bounded(-12000,20000,"-12000..20000 meters");options.cloud_base=value;}
      else if(arg=="--temperature"){bounded(-40,50,"-40..50 degrees Celsius");options.temperature=value;}
      else if(arg=="--dew-point"){bounded(-60,50,"-60..50 degrees Celsius");options.dew_point=value;}
      else if(arg=="--stability"){bounded(0.002,0.05,"0.002..0.05 per second");options.stability=value;}
      else if(arg=="--wind-direction"){bounded(0,360,"0..360 compass degrees");options.wind_direction=value;}
      else if(arg=="--wind-min"){bounded(0,60,"0..60 m/s");options.wind_min=value;}
      else if(arg=="--wind-max"){bounded(0,60,"0..60 m/s");options.wind_max=value;}
      else if(arg=="--wind-swing"){bounded(0,90,"0..90 degrees");options.wind_swing=value;}
      else if(arg=="--wind-period"){bounded(1,3600,"1..3600 seconds");options.wind_period=value;}
      else if(arg=="--hour"){bounded(0,24,"0..24");options.hour=value;}
      else if(arg=="--latitude"){bounded(-90,90,"-90..90 degrees");options.latitude=value;}
      else if(arg=="--day-of-year"){bounded(1,366,"1..366");options.day_of_year=value;}
      else if(arg=="--weather-time"){bounded(0,86400,"0..86400 seconds");options.weather_time=value;}
      else if(arg=="--moon-phase"){bounded(0,1,"0 (new) .. 0.5 (full) .. 1");options.moon_phase=value;}
      else if(arg=="--moon-size"){bounded(0.25,6,"0.25..6 degrees of apparent diameter");options.moon_size=value;}
      else if(arg=="--rainfall"){bounded(0,500,"0..500 mm already on the ground");options.rainfall_mm=value;}
      else {bounded(0,3600,"0..3600 seconds per simulated hour");options.time_scale=value;}
    }
    else if(arg=="--weather-test")options.weather_test=true;
    else if((arg=="--view-x" || arg=="--view-z") && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value) || value<-100000 || value>100000)
        throw std::invalid_argument(arg+" must be -100000..100000 meters");
      if(arg=="--view-x")options.view_x=value;else options.view_z=value;
    }
    else if(arg=="--view" && i+1<argc) {
      options.view_mode=argv[++i];
      if(options.view_mode!="world" && options.view_mode!="first-person")
        throw std::invalid_argument("--view must be world or first-person");
    }
    else if(arg=="--walk-speed" && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value) ||
         value<terraflux::graphics::kMinWalkSpeed || value>terraflux::graphics::kMaxWalkSpeed)
        throw std::invalid_argument("--walk-speed must be 1..60 m/s");
      options.walk_speed=value;
    }
    else if(arg=="--first-person-test")options.first_person_test=true;
    else if(arg=="--max-slope" && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value) || value<1 || value>89)
        throw std::invalid_argument("--max-slope must be 1..89 degrees");
      options.max_slope=value;
    }
    else if(arg=="--no-collision")options.no_collision=true;
    else if(arg=="--collision-test")options.collision_test=true;
    else if((arg=="--jump-height" || arg=="--gravity") && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value))
        throw std::invalid_argument(arg+" must be a finite number");
      if(arg=="--jump-height"){if(value<0 || value>10)throw std::invalid_argument("--jump-height must be 0..10 meters");options.jump_height=value;}
      else {if(value<0.1 || value>100)throw std::invalid_argument("--gravity must be 0.1..100 m/s^2");options.gravity=value;}
    }
    else if(arg=="--jump-test")options.jump_test=true;
    else if(arg=="--no-gait")options.no_gait=true;
    else if(arg=="--gait-test")options.gait_test=true;
    else if(arg=="--tree-fire" && i+1<argc) {
      options.tree_fire=argv[++i];
      if(options.tree_fire!="model" && options.tree_fire!="grid" && options.tree_fire!="off")
        throw std::invalid_argument("--tree-fire must be model, grid or off");
    }
    else if(arg=="--tree-fire-test")options.tree_fire_test=true;
    else if(arg=="--storm-test")options.storm_test=true;
    else if(arg=="--flash-flood" && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value) || value<0 || value>1)
        throw std::invalid_argument("--flash-flood must be 0..1");
      options.flash_flood=value;
    }
    else if(arg=="--flood-test")options.flood_test=true;
    else if(arg=="--climb-test")options.climb_test=true;
    else if(arg=="--vitals-test")options.vitals_test=true;
    else if(arg=="--no-arms")options.no_arms=true;
    else if(arg=="--no-stones")options.no_stones=true;
    else if(arg=="--hold" && i+1<argc) {
      options.hold=argv[++i];
      if(options.hold!="stone" && options.hold!="branch" && options.hold!="torch")throw std::invalid_argument("--hold must be stone, branch or torch");
    }
    else if(arg=="--items-test")options.items_test=true;
    else if(arg=="--tool" && i+1<argc) {
      options.tool=argv[++i];
      if(options.tool!="rope" && options.tool!="axe" && options.tool!="chainsaw")throw std::invalid_argument("--tool must be rope, axe or chainsaw");
    }
    else if(arg=="--tools-test")options.tools_test=true;
    else if(arg=="--roads" && i+1<argc)options.roads=argv[++i];
    else if(arg=="--roads-test")options.roads_test=true;
    else if(arg=="--city-test")options.city_test=true;
    else if(arg=="--buildings" && i+1<argc) {
      options.buildings=argv[++i];
      if(options.buildings!="auto" && options.buildings!="off")throw std::invalid_argument("--buildings must be auto or off");
    }
    else if(arg=="--buildings-test")options.buildings_test=true;
    else if(arg=="--interior-test")options.interior_test=true;
    else if(arg=="--hazards-test")options.hazards_test=true;
    else if((arg=="--rope-length" || arg=="--chainsaw-fuel") && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value))throw std::invalid_argument(arg+" must be a number");
      if(arg=="--rope-length"){if(value<2 || value>200)throw std::invalid_argument("--rope-length must be 2..200 m");options.rope_length=value;}
      else {if(value<1 || value>36000)throw std::invalid_argument("--chainsaw-fuel must be 1..36000 s");options.chainsaw_fuel=value;}
    }
    else if(arg=="--body-pose" && i+1<argc) {
      options.body_pose_override=argv[++i];
      static const char* const poses[]={"idle","walk","run","crouch","climb","swim","crawl","dive","down"};
      bool known=false;for(const char* p:poses)if(options.body_pose_override==p)known=true;
      if(!known)throw std::invalid_argument("--body-pose must be idle, walk, run, crouch, climb, swim, crawl, dive or down");
    }
    else if(arg=="--body-test")options.body_test=true;
    else if((arg=="--view-yaw" || arg=="--view-pitch" || arg=="--view-zoom") && i+1<argc) {
      const std::string text=argv[++i];double value=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value))
        throw std::invalid_argument(arg+" must be a finite number");
      if(arg=="--view-yaw"){if(value<-360 || value>360)throw std::invalid_argument("--view-yaw must be -360..360 degrees");options.view_yaw=value;}
      else if(arg=="--view-pitch"){if(value<-85 || value>85)throw std::invalid_argument("--view-pitch must be -85..85 degrees (the world view uses 6..84 above the ground)");options.view_pitch=value;}
      else {if(value<0.03 || value>4)throw std::invalid_argument("--view-zoom must be 0.03..4 times the framing distance");options.view_zoom=value;}
    }
    else if(arg=="--water-floor" && i+1<argc) {
      options.water_floor=argv[++i];
      if(options.water_floor!="random")(void)terraflux::graphics::parse_water_floor(options.water_floor.c_str());
    }
    else if(arg=="--floor-seed" && i+1<argc) {
      const std::string text=argv[++i];std::uint32_t seed=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),seed);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
        throw std::invalid_argument("--floor-seed must be an unsigned 32-bit integer");
      options.floor_seed=seed;
    }
    else if(arg=="--floor-patch-size" && i+1<argc) {
      const std::string text=argv[++i];double meters=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),meters);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(meters) || meters<5 || meters>500)
        throw std::invalid_argument("--floor-patch-size must be 5..500 meters");
      options.floor_patch_size=meters;
    }
    else if(arg=="--terrain-shading-test")options.terrain_shading_test=true;
    else if(arg=="--terrain-tiles")options.terrain_tiles=true;
    else if(arg=="--terrain-tile-test")options.terrain_tile_test=true;
    else if(arg=="--terrain-tile-quads" && i+1<argc) {
      const std::string text=argv[++i];std::uint32_t n=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !n || n>256)
        throw std::invalid_argument("--terrain-tile-quads must be 1..256");
      options.terrain_tiles=true;options.terrain_tile_quads=n;
    }
    else if(arg=="--offscreen")options.offscreen=true;
    else if(arg=="--offscreen-resize-test")options.offscreen_resize_test=true;
    else if(arg=="--resource-recovery-test"){options.resource_recovery_test=true;options.scene_residency_test=true;}
    else if (arg == "--scene-residency-test") options.scene_residency_test=true;
    else if (arg == "--scene-detach-test") options.scene_detach_test=true;
    else if (arg == "--scene-controls-test") options.scene_controls_test=true;
    else if (arg == "--asset-lifecycle-test") options.asset_lifecycle_test=true;
    else if((arg=="--resource-budget-mib" || arg=="--cpu-recipe-budget-mib") && i+1<argc) {
      const std::string value=argv[++i];std::uint64_t mib=0;
      const auto parsed=std::from_chars(value.data(),value.data()+value.size(),mib);
      if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || mib>16384)
        throw std::invalid_argument("resource byte budget must be 0..16384 MiB");
      if(arg=="--cpu-recipe-budget-mib")options.cpu_recipe_budget_bytes=mib*1048576;
      else options.resource_budget_bytes=mib*1048576;
    }
    else if(arg=="--start-inactive")options.start_inactive=true;
    else if((arg=="--select-node" || arg=="--activate-node") && i+1<argc) {
      const std::string value=argv[++i];std::uint32_t node=0;
      const auto parsed=std::from_chars(value.data(),value.data()+value.size(),node);
      if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || !node)
        throw std::invalid_argument("node ID must be a positive integer");
      options.selected_node=node;
      if(arg=="--activate-node")options.activate_node=node;
    }
    else if(arg=="--residency-report")options.residency_report=true;
    else if((arg=="--max-resident-meshes" || arg=="--max-scene-instances") && i+1<argc) {
      const std::string value=argv[++i];std::uint32_t limit=0;
      const auto parsed=std::from_chars(value.data(),value.data()+value.size(),limit);
      const auto maximum=arg=="--max-resident-meshes"?512U:1024U;
      if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || limit>maximum)
        throw std::invalid_argument("invalid scene residency budget limit");
      if(arg=="--max-resident-meshes")options.max_resident_meshes=limit;
      else options.max_scene_instances=limit;
    }
    else if (arg == "--profile") options.profile = true;
    else if (arg == "--no-culling") options.no_culling = true;
    else if (arg == "--no-instancing") options.no_instancing = true;
    else if (arg == "--asset-preview") options.asset_preview = true;
    else if (arg == "--benchmark-frames" && i+1 < argc) {
      const std::string text=argv[++i];
      std::uint32_t n=0;
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() ||
         n<8 || n>2000)
        throw std::invalid_argument("--benchmark-frames must be 8..2000");
      options.benchmark_frames=n;
    }
    else if (arg == "--request" && i + 1 < argc) options.request_file = argv[++i];
    else if (arg == "--response" && i + 1 < argc) options.response_file = argv[++i];
    else if (arg == "--obj" && i + 1 < argc) options.obj_file = argv[++i];
    else if (arg == "--gltf" && i + 1 < argc) options.gltf_file = argv[++i];
    else if (arg == "--texture" && i + 1 < argc) options.texture_file = argv[++i];
    else if (arg == "--load-state" && i+1<argc)options.load_state=argv[++i];
    else if (arg == "--save-state" && i+1<argc)options.save_state=argv[++i];
    else if (arg == "--screenshot" && i+1<argc)options.screenshot_file=argv[++i];
    else if (arg == "--scene" && i + 1 < argc) options.scene_file = argv[++i];
    else throw std::invalid_argument(
        "usage: terraflux-terrain-viewer [--request request.json --response response.json] [--obj model.obj | --gltf model.gltf] [--texture texture.ppm] [--scene scene.json (legacy scenes require --obj)] [--replay-speed 1] [--replay-frame 0] [--replay-controls-test with replay --smoke-test] [--hide-terrain] [--hide-fire] [--layers-test with replay --smoke-test] [--terrain-contours 50] [--render-cell 5] [--max-render-vertices 4000000] [--terrain-detail 1] [--rock-relief 5] [--snow-line 0.72] [--terrain-shading-test with --smoke-test] [--water-level 35] [--wave-height 0.6] [--wave-length 24] [--wave-speed 3] [--wave-direction 90] [--water-clarity 4] [--water-floor sand|vegetation|mud|stones|mixed|random] [--floor-seed 7] [--floor-patch-size 45] [--water-test with --smoke-test] [--vegetation off|low|medium|high] [--tree-style conifer|broadleaf|mixed|custom] [--tree-obj model.obj] [--tree-spacing 12] [--grass-spacing 2.5] [--grass-distance 300] [--tree-height 16] [--vegetation-seed 0] [--vegetation-test with --smoke-test] [--flame-scale 1] [--smoke-scale 1] [--fire-test with replay --smoke-test] [--weather off|auto|clear|cirrus|cumulus|stratocumulus|stratus|nimbostratus|cumulonimbus] [--cloud-cover 0.35] [--cloud-base 1200] [--temperature 18] [--dew-point 8] [--stability 0.01] [--wind-direction 90] [--wind-min 2] [--wind-max 6] [--wind-swing 20] [--wind-period 12] [--hour 10] [--latitude 47] [--day-of-year 172] [--time-scale 10] [--weather-time 0] [--moon-phase 0.5] [--moon-size 2] [--rainfall 0] [--weather-test with --smoke-test] [--view-yaw 26] [--view-pitch 43] [--view-zoom 1] [--view-x 0] [--view-z 0] [--view world|first-person] [--walk-speed 2.5] [--max-slope 45] [--no-collision] [--jump-height 1] [--gravity 9.81] [--no-gait] [--tree-fire model|grid|off] [--tree-fire-test with replay --smoke-test] [--storm-test with --smoke-test] [--flash-flood 0] [--flood-test with --smoke-test] [--climb-test with --smoke-test] [--vitals-test with --smoke-test] [--no-arms] [--body-pose run] [--body-test with --smoke-test] [--no-stones] [--hold torch] [--items-test with --smoke-test] [--tool rope|axe|chainsaw] [--rope-length 20] [--chainsaw-fuel 300] [--tools-test with --smoke-test] [--roads auto|city|demo|off|roads.json] [--roads-test with --smoke-test] [--city-test with --smoke-test] [--buildings auto|off] [--buildings-test with --smoke-test] [--interior-test with --smoke-test] [--hazards-test with --smoke-test] [--first-person-test with --smoke-test] [--collision-test with --smoke-test] [--jump-test with --smoke-test] [--gait-test with --smoke-test] [--terrain-tiles] [--terrain-residency] [--terrain-retain-frames 30] [--terrain-retention-test with --smoke-test --terrain-retain-frames 2] [--max-resident-terrain-tiles 4096] [--terrain-residency-test with --smoke-test --terrain-residency] [--terrain-tile-quads 32] [--terrain-tile-test with --smoke-test --terrain-tiles] [--offscreen] [--offscreen-resize-test with --offscreen --smoke-test] [--profile] [--no-culling] [--no-instancing] [--asset-preview with --gltf] [--benchmark-frames 60] [--screenshot frame.ppm] [--smoke-test] [--asset-lifecycle-test with --smoke-test]");
  }
  if(options.replay_speed_requested && options.request_file.empty())
    throw std::invalid_argument("--replay-speed requires replay input");
  if(options.replay_frame && options.request_file.empty())
    throw std::invalid_argument("--replay-frame requires replay input");
  if(options.replay_controls_test && (!options.smoke_test || options.request_file.empty() ||
      !options.scene_file.empty() || !options.obj_file.empty() || !options.gltf_file.empty() ||
      options.layers_test || options.hide_terrain || options.hide_fire || options.replay_speed!=1 || options.replay_frame ||
      options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test ||
      options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--replay-controls-test requires replay smoke mode, default speed and no assets or other scripted test");
  if(options.layers_test && (!options.smoke_test || options.request_file.empty() ||
      !options.scene_file.empty() || !options.obj_file.empty() || !options.gltf_file.empty() ||
      options.hide_terrain || options.hide_fire || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test ||
      options.terrain_tile_test || options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--layers-test requires replay smoke mode without assets, initial hidden layers or another scripted test");
  if(options.terrain_shading_test && (!options.smoke_test || !options.scene_file.empty() ||
      !options.obj_file.empty() || !options.gltf_file.empty() || options.hide_terrain || options.hide_fire ||
      options.terrain_contour_interval || options.terrain_detail!=1.0 || options.snow_line ||
      options.layers_test || options.replay_controls_test ||
      options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test ||
      options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--terrain-shading-test requires smoke mode with visible layers, default style options and no assets or other scripted test");
  if(options.tree_style=="custom" && options.tree_obj.empty())
    throw std::invalid_argument("--tree-style custom requires --tree-obj");
  if(options.fire_test && (!options.smoke_test || options.request_file.empty() || !options.scene_file.empty() ||
      !options.obj_file.empty() || !options.gltf_file.empty() || options.hide_terrain || options.hide_fire ||
      options.vegetation=="off" || options.layers_test || options.replay_controls_test || options.terrain_shading_test ||
      options.water_test || options.vegetation_test || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test ||
      options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--fire-test requires replay smoke mode with both layers and vegetation enabled and no assets or other scripted test");
  if(options.wind_max.value_or(6.0)<options.wind_min.value_or(2.0))
    throw std::invalid_argument("--wind-max must not be below --wind-min");
  if(options.first_person_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test ||
      options.fire_test || options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test ||
      options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--first-person-test requires smoke mode starting in the world view with visible terrain and no assets or other scripted test");
  if(options.collision_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.no_collision ||
      options.vegetation=="off" || options.first_person_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test ||
      options.fire_test || options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test ||
      options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--collision-test requires smoke mode starting in the world view with visible terrain, vegetation and collision enabled and no assets or other scripted test");
  if(options.jump_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.jump_height<=0 ||
      options.first_person_test || options.collision_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test ||
      options.fire_test || options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test ||
      options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--jump-test requires smoke mode starting in the world view with visible terrain, a positive jump height and no assets or other scripted test");
  if(options.gait_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.no_gait ||
      options.jump_test || options.first_person_test || options.collision_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test ||
      options.fire_test || options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test ||
      options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--gait-test requires smoke mode starting in the world view with visible terrain, the gait enabled and no assets or other scripted test");
  if(options.tree_fire_test && (!options.smoke_test || options.request_file.empty() || !options.scene_file.empty() ||
      !options.obj_file.empty() || !options.gltf_file.empty() || options.hide_terrain || options.hide_fire ||
      options.vegetation=="off" || options.tree_fire=="off" || options.view_mode!="world" || options.gait_test ||
      options.jump_test || options.first_person_test || options.collision_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test ||
      options.fire_test || options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test ||
      options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--tree-fire-test requires replay smoke mode with both layers, vegetation and the tree fire model and no assets or other scripted test");
  if(options.storm_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.hide_fire || options.vegetation=="off" ||
      options.tree_fire=="off" || options.view_mode!="world" || options.weather || options.wind_direction || options.no_collision ||
      options.tree_fire_test || options.gait_test || options.jump_test || options.first_person_test || options.collision_test ||
      options.layers_test || options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test ||
      options.fire_test || options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test ||
      options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--storm-test requires demo or replay smoke mode with visible layers, vegetation, the tree fire model, default weather options and no assets or other scripted test");
  if(options.flood_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.flash_flood || options.weather ||
      options.no_collision || options.storm_test || options.tree_fire_test || options.gait_test || options.jump_test ||
      options.first_person_test || options.collision_test || options.layers_test || options.replay_controls_test ||
      options.terrain_shading_test || options.water_test || options.vegetation_test || options.fire_test || options.weather_test ||
      options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test || options.scene_detach_test ||
      options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--flood-test requires demo or replay smoke mode with visible terrain, default weather and flood options and no assets or other scripted test");
  if(options.climb_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.flash_flood || options.weather ||
      options.no_collision || options.flood_test || options.storm_test || options.tree_fire_test || options.gait_test || options.jump_test ||
      options.first_person_test || options.collision_test || options.layers_test || options.replay_controls_test ||
      options.terrain_shading_test || options.water_test || options.vegetation_test || options.fire_test || options.weather_test ||
      options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test || options.scene_detach_test ||
      options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--climb-test requires demo or replay smoke mode with visible terrain, default weather and flood options and no assets or other scripted test");
  if(options.vitals_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.flash_flood || options.weather ||
      options.no_collision || options.vegetation=="off" || options.tree_fire=="off" || options.climb_test || options.flood_test ||
      options.storm_test || options.tree_fire_test || options.gait_test || options.jump_test || options.first_person_test ||
      options.collision_test || options.layers_test || options.replay_controls_test || options.terrain_shading_test || options.water_test ||
      options.vegetation_test || options.fire_test || options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test ||
      options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--vitals-test requires demo or replay smoke mode with visible terrain, vegetation and the tree fire model, default weather options and no assets or other scripted test");
  if(options.body_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.no_arms || options.flash_flood ||
      options.weather || options.no_collision || options.vitals_test || options.climb_test || options.flood_test || options.storm_test ||
      options.tree_fire_test || options.gait_test || options.jump_test || options.first_person_test || options.collision_test ||
      options.layers_test || options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test ||
      options.fire_test || options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.terrain_retention_test))
    throw std::invalid_argument("--body-test requires demo or replay smoke mode with visible terrain, the arms shown, default weather and flood options and no assets or other scripted test");
  if(options.items_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.no_stones || options.no_arms ||
      options.vegetation=="off" || options.tree_fire=="off" || options.flash_flood || options.weather || options.no_collision ||
      options.body_test || options.vitals_test || options.climb_test || options.flood_test || options.storm_test || options.tree_fire_test ||
      options.gait_test || options.jump_test || options.first_person_test || options.collision_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test || options.fire_test ||
      options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.terrain_retention_test))
    throw std::invalid_argument("--items-test requires demo or replay smoke mode with visible terrain, stones, trees, the tree fire model and the arms, and no assets or other scripted test");
  if(options.tools_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.no_arms || !options.tool.empty() ||
      options.vegetation=="off" || options.tree_fire=="off" || options.flash_flood || options.weather || options.no_collision ||
      options.items_test || options.body_test || options.vitals_test || options.climb_test || options.flood_test || options.storm_test || options.tree_fire_test ||
      options.gait_test || options.jump_test || options.first_person_test || options.collision_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test || options.fire_test ||
      options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.terrain_retention_test))
    throw std::invalid_argument("--tools-test requires demo or replay smoke mode with visible terrain, trees, the tree fire model and the arms, no tool preselected, and no assets or other scripted test");
  if(options.roads.empty())throw std::invalid_argument("--roads needs auto, demo, off or a network JSON file");
  if(options.roads_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.roads=="off" ||
      options.vegetation=="off" || options.tree_fire=="off" || options.flash_flood || options.weather || options.no_collision ||
      options.tools_test || options.items_test || options.body_test || options.vitals_test || options.climb_test || options.flood_test || options.storm_test ||
      options.tree_fire_test || options.gait_test || options.jump_test || options.first_person_test || options.collision_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test || options.fire_test ||
      options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.terrain_retention_test))
    throw std::invalid_argument("--roads-test requires demo or replay smoke mode with visible terrain, roads, trees and the tree fire model, default weather and flood options, and no assets or other scripted test");
  if(options.city_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || (options.roads!="auto" && options.roads!="city") || options.buildings=="off" ||
      options.flash_flood || options.weather || options.no_collision || options.roads_test || options.buildings_test ||
      options.tools_test || options.items_test || options.body_test || options.vitals_test || options.climb_test || options.flood_test || options.storm_test ||
      options.tree_fire_test || options.gait_test || options.jump_test || options.first_person_test || options.collision_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test || options.fire_test ||
      options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.terrain_retention_test))
    throw std::invalid_argument("--city-test requires demo or replay smoke mode with visible terrain, the city (--roads auto or city) and buildings, default weather and flood options, and no assets or other scripted test");
  if(options.buildings_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.roads=="off" || options.buildings=="off" ||
      options.flash_flood || options.weather || options.no_collision || options.roads_test || options.interior_test ||
      options.tools_test || options.items_test || options.body_test || options.vitals_test || options.climb_test || options.flood_test || options.storm_test ||
      options.tree_fire_test || options.gait_test || options.jump_test || options.first_person_test || options.collision_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test || options.fire_test ||
      options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.terrain_retention_test))
    throw std::invalid_argument("--buildings-test requires demo or replay smoke mode with visible terrain, roads and buildings, default weather and flood options, and no assets or other scripted test");
  if(options.interior_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.roads=="off" || options.buildings=="off" ||
      options.flash_flood || options.weather || options.no_collision || options.roads_test || options.buildings_test || options.city_test || options.hazards_test ||
      options.tools_test || options.items_test || options.body_test || options.vitals_test || options.climb_test || options.flood_test || options.storm_test ||
      options.tree_fire_test || options.gait_test || options.jump_test || options.first_person_test || options.collision_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test || options.fire_test ||
      options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.terrain_retention_test))
    throw std::invalid_argument("--interior-test requires demo or replay smoke mode with visible terrain, roads and buildings, default weather and flood options, and no assets or other scripted test");
  if(options.hazards_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.view_mode!="world" || options.roads=="off" || options.buildings=="off" ||
      options.flash_flood || options.weather || options.no_collision || options.roads_test || options.buildings_test || options.city_test || options.interior_test ||
      options.tools_test || options.items_test || options.body_test || options.vitals_test || options.climb_test || options.flood_test || options.storm_test ||
      options.tree_fire_test || options.gait_test || options.jump_test || options.first_person_test || options.collision_test || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test || options.fire_test ||
      options.weather_test || options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.terrain_retention_test))
    throw std::invalid_argument("--hazards-test requires demo or replay smoke mode with visible terrain, roads, buildings and the tree fire model, default weather and flood options, and no assets or other scripted test");
  if(options.weather_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.hide_fire || options.weather || options.cloud_cover ||
      options.cloud_base || options.wind_direction || options.wave_direction!=90.0 || options.weather_time!=0.0 || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.vegetation_test ||
      options.fire_test || options.offscreen_resize_test || options.asset_lifecycle_test || options.scene_controls_test ||
      options.scene_detach_test || options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.terrain_retention_test))
    throw std::invalid_argument("--weather-test requires smoke mode with visible layers, default weather/wind options and no assets or other scripted test");
  if(options.vegetation_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.vegetation=="off" || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.water_test || options.offscreen_resize_test ||
      options.asset_lifecycle_test || options.scene_controls_test || options.scene_detach_test ||
      options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test || options.terrain_retention_test))
    throw std::invalid_argument("--vegetation-test requires smoke mode with vegetation enabled, visible terrain and no assets or other scripted test");
  if(options.water_test && (!options.smoke_test || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.hide_terrain || options.hide_fire || options.layers_test ||
      options.replay_controls_test || options.terrain_shading_test || options.offscreen_resize_test ||
      options.asset_lifecycle_test || options.scene_controls_test || options.scene_detach_test ||
      options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.terrain_retention_test || (options.water_level.has_value()==false && !options.request_file.empty()) ||
      options.wave_height<=0 || options.wave_speed<=0))
    throw std::invalid_argument("--water-test requires smoke mode with visible layers, a water level (demo or --water-level), moving waves and no assets or other scripted test");
  if(options.terrain_retention_test && (!options.terrain_residency || !options.smoke_test ||
      options.terrain_retention_frames!=2 || !options.request_file.empty() || !options.scene_file.empty() ||
      !options.obj_file.empty() || !options.gltf_file.empty() || options.offscreen_resize_test ||
      options.asset_lifecycle_test || options.scene_controls_test || options.scene_detach_test ||
      options.scene_residency_test || options.terrain_tile_test || options.terrain_residency_test ||
      options.no_culling || options.resource_budget_bytes || options.max_resident_terrain_tiles!=4096))
    throw std::invalid_argument("--terrain-retention-test requires demo smoke mode, two retention frames, culling and default budgets");
  if(options.terrain_residency_test && (!options.terrain_residency || !options.smoke_test ||
      !options.request_file.empty() || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test ||
      options.terrain_tile_test || options.no_culling || options.resource_budget_bytes ||
      options.max_resident_terrain_tiles!=4096 || options.terrain_retention_frames!=0))
    throw std::invalid_argument("--terrain-residency-test requires demo residency smoke mode, culling, default budgets and zero retention");
  if(options.terrain_tile_test && (!options.terrain_tiles || !options.smoke_test ||
      !options.request_file.empty() || !options.scene_file.empty() || !options.obj_file.empty() ||
      !options.gltf_file.empty() || options.offscreen_resize_test || options.asset_lifecycle_test ||
      options.scene_controls_test || options.scene_detach_test || options.scene_residency_test || options.no_culling))
    throw std::invalid_argument("--terrain-tile-test requires tiled smoke mode with the demo terrain and culling enabled");
  if(options.offscreen_resize_test)options.no_instancing=true; // Isolate target admission from ring growth.
  if(options.offscreen_resize_test && (!options.offscreen || !options.smoke_test ||
      options.asset_lifecycle_test || options.scene_controls_test || options.scene_detach_test ||
      options.scene_residency_test || options.resource_budget_bytes))
    throw std::invalid_argument("--offscreen-resize-test requires --offscreen --smoke-test, no other scripted test or byte cap");
  if (options.request_file.empty() != options.response_file.empty())
    throw std::invalid_argument("--request and --response must be provided together");
  if (!options.obj_file.empty() && !options.gltf_file.empty())
    throw std::invalid_argument("--obj and --gltf are mutually exclusive");
  if (!options.texture_file.empty() && options.obj_file.empty() && options.gltf_file.empty() && options.scene_file.empty())
    throw std::invalid_argument("--texture requires --obj, --gltf or --scene");
  if (!options.scene_file.empty() && !options.gltf_file.empty())
    throw std::invalid_argument("--scene cannot be combined with --gltf");
  if(options.benchmark_frames && options.smoke_test)
    throw std::invalid_argument("benchmark and smoke-test cannot run together");
  if(options.no_culling && !options.benchmark_frames && !options.profile && !options.smoke_test)
    throw std::invalid_argument("--no-culling requires --profile, --benchmark-frames or --smoke-test");
  if(options.asset_preview && (options.gltf_file.empty() || options.benchmark_frames))
    throw std::invalid_argument("--asset-preview requires --gltf and cannot benchmark");
  if(options.no_instancing && !options.benchmark_frames && !options.profile &&
     !options.smoke_test)
    throw std::invalid_argument("--no-instancing requires --profile, --benchmark-frames or --smoke-test");
  if(options.asset_lifecycle_test && (!options.smoke_test ||
      (options.obj_file.empty() && options.gltf_file.empty() && options.scene_file.empty())))
    throw std::invalid_argument("--asset-lifecycle-test requires --smoke-test and an asset");
  if(options.scene_controls_test && (!options.smoke_test || options.scene_file.empty() ||
                                      options.asset_lifecycle_test))
    throw std::invalid_argument("--scene-controls-test requires --smoke-test and --scene; cannot test lifecycle together");
  if(options.scene_detach_test && (!options.smoke_test || options.scene_file.empty() ||
      options.scene_controls_test || options.asset_lifecycle_test))
    throw std::invalid_argument("--scene-detach-test requires --smoke-test and --scene, exclusively");
  if(options.scene_residency_test && (!options.smoke_test || options.scene_file.empty() ||
      options.scene_detach_test || options.scene_controls_test || options.asset_lifecycle_test))
    throw std::invalid_argument("--scene-residency-test requires --smoke-test and --scene, exclusively");
  if((options.start_inactive || options.selected_node || options.activate_node) && options.scene_file.empty())
    throw std::invalid_argument("node selection and inactive loading require --scene");
  if(options.start_inactive && (options.scene_controls_test || options.scene_detach_test ||
                               options.scene_residency_test || options.asset_lifecycle_test))
    throw std::invalid_argument("inactive loading cannot be combined with scripted lifecycle tests");
  if((!options.load_state.empty() || !options.save_state.empty()) && options.scene_file.empty())
    throw std::invalid_argument("residency state requires --scene");
  if(!options.save_state.empty() && std::filesystem::absolute(options.save_state).lexically_normal()==
      std::filesystem::absolute(options.scene_file).lexically_normal())
    throw std::invalid_argument("residency state must not overwrite the scene file");
  return options;
}

// Deterministic value noise for authoring the demo's vegetation pattern.
double demo_noise(double x, double y) {
  const auto hash = [](int i, int j) {
    std::uint32_t h = static_cast<std::uint32_t>(i) * 374761393u + static_cast<std::uint32_t>(j) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<double>(h ^ (h >> 16)) / 4294967296.0;
  };
  const double fx = std::floor(x), fy = std::floor(y);
  const int i = static_cast<int>(fx), j = static_cast<int>(fy);
  const double u = x - fx, v = y - fy;
  const double su = u * u * (3 - 2 * u), sv = v * v * (3 - 2 * v);
  const double a = hash(i, j), b = hash(i + 1, j), c = hash(i, j + 1), d = hash(i + 1, j + 1);
  return (a + (b - a) * su) * (1 - sv) + (c + (d - c) * su) * sv;
}
double demo_fbm(double x, double y) {
  return 0.5 * demo_noise(x, y) + 0.3 * demo_noise(x * 2.1 + 7.3, y * 2.1 + 3.1) +
         0.2 * demo_noise(x * 4.3 + 1.7, y * 4.3 + 9.2);
}
// Ridged multifractal: sharp crests and creases for crags and rock faces.
double demo_ridged(double x, double y) {
  const auto ridge = [](double n) { const double v = 1.0 - std::abs(2.0 * n - 1.0); return v * v; };
  return 0.55 * ridge(demo_noise(x, y)) + 0.30 * ridge(demo_noise(x * 2.3 + 4.1, y * 2.3 + 2.7)) +
         0.15 * ridge(demo_noise(x * 4.9 + 8.3, y * 4.9 + 5.9));
}

constexpr double kDemoWaterLevel = 34.0; // meters; the demo lake's still-water level

terraflux::compute::Grid make_demo_terrain() {
  constexpr int kSide = 72;
  std::vector<terraflux::compute::GridCell> cells;
  cells.reserve(kSide * kSide);
  for (int y = 0; y < kSide; ++y) {
    for (int x = 0; x < kSide; ++x) {
      terraflux::compute::GridCell cell{};
      cell.x = x; cell.y = y;
      cell.burn_susceptibility = 0.8;
      // Rolling hills, an eastern plateau behind a winding two-band escarpment
      // and a craggy ridge on top of it. The cliff bands rise about 45 degrees
      // at the 30 m grid; ridged noise breaks faces and crest into crags and
      // ledges instead of smooth slopes.
      const double hill = 55.0 * std::sin(x * 0.065) * std::cos(y * 0.075);
      const double u = x - 44.0 - 5.0 * std::sin(y * 0.18);
      const double scarp = 42.0 * (1.0 + std::tanh(u / 1.1)) + 24.0 * (1.0 + std::tanh((u - 4.5) / 0.9));
      const double cliff_mask = std::exp(-std::pow(u / 2.4, 2)) + std::exp(-std::pow((u - 4.5) / 2.2, 2));
      const double ridge_dome = 110.0 * std::exp(-std::pow((x-56.0)/12,2) - std::pow((y-30.0)/10,2));
      const double crag = (demo_ridged(x * 0.19, y * 0.19) - 0.45) * (16.0 * std::min(cliff_mask, 1.0) +
                          14.0 * std::clamp((ridge_dome - 25.0) / 60.0, 0.0, 1.0));
      const double ridge = ridge_dome + crag;
      // A lake basin at the foot of the escarpment: its eastern shore is the
      // rocky scarp, the other shores are gentle beaches (see kDemoWaterLevel).
      const double basin = -18.0 * std::exp(-(std::pow(x-34.0,2) + std::pow(y-48.0,2))/(2*11.0*11.0));
      const double elevation = 70.0 + hill + scarp + ridge + basin;
      cell.elevation_meters = elevation;
      // Vegetation: a wet riparian strip winds through the western lowland,
      // moisture otherwise falls with height (dry plateau, semi-dry lowland)
      // with noisy local variation; irregular forest stands cover the mid
      // slopes and thin out toward the summit, where the ground is bare.
      const double relative = std::clamp((elevation - 15.0) / 305.0, 0.0, 1.0);
      const double riparian = std::exp(-std::pow((x - 14.0 - 6.0 * std::sin(y * 0.15)) / 4.0, 2));
      const double moisture = 0.08 + 0.17 * (1.0 - relative) + 0.45 * riparian +
                              0.12 * (demo_fbm(x * 0.07 + 5.0, y * 0.07) - 0.5);
      const double patches = demo_fbm(x * 0.09, y * 0.09) + 0.15 * (1.0 - relative) + 0.12 * riparian;
      const double forest = std::clamp((patches - 0.50) / 0.16, 0.0, 1.0);
      const double alpine = std::clamp((relative - 0.78) / 0.10, 0.0, 1.0);
      cell.fuel_moisture = std::clamp(moisture, 0.0, 1.0);
      cell.fuel_load = std::clamp((0.30 + 0.65 * forest * forest * (3 - 2 * forest)) * (1.0 - alpine), 0.0, 1.0);
      // Vegetation thins smoothly across the first meters of the lake bed so the
      // shoreline fringe interpolates cleanly instead of stepping per triangle.
      cell.fuel_load *= std::clamp((elevation - (kDemoWaterLevel - 6.0)) / 6.0, 0.0, 1.0);
      cells.push_back(cell);
    }
  }
  return terraflux::compute::Grid(std::move(cells));
}

// Binary PPM of the last presented frame. OpenGL rows start at the bottom;
// the file is written top-down. A failed write leaves no partial destination.
void write_ppm(const std::string& path,int width,int height,const std::vector<std::uint8_t>& rgba) {
  if(width<=0 || height<=0 || rgba.size()!=static_cast<std::size_t>(width)*height*4)
    throw std::invalid_argument("screenshot dimensions do not match captured pixels");
  const std::string temporary=path+".tmp";
  {
    std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
    if(!file)throw std::runtime_error("cannot create screenshot "+path);
    file<<"P6\n"<<width<<' '<<height<<"\n255\n";
    std::vector<char> row(static_cast<std::size_t>(width)*3);
    for(int y=height-1;y>=0;--y) {
      const auto* source=rgba.data()+static_cast<std::size_t>(y)*width*4;
      for(int x=0;x<width;++x)for(int c=0;c<3;++c)row[static_cast<std::size_t>(x)*3+c]=static_cast<char>(source[x*4+c]);
      file.write(row.data(),static_cast<std::streamsize>(row.size()));
    }
    file.close();
    if(!file)throw std::runtime_error("error writing screenshot "+path);
  }
  std::error_code error;
  std::filesystem::rename(temporary,path,error);
  if(error){std::filesystem::remove(temporary);throw std::runtime_error("cannot replace screenshot "+path);}
}

template <class T>
std::span<const std::byte> bytes_of(const std::vector<T>& items) {
  return std::as_bytes(std::span(items));
}

struct CameraGeometry {
  terraflux::math::Vec3 target;
  double starting_distance;
};

CameraGeometry camera_geometry(const terraflux::terrain::TerrainMesh& mesh) {
  using terraflux::math::Vec3;
  if (mesh.vertices.empty()) throw std::runtime_error("terrain mesh contains no vertices");
  auto lower = mesh.vertices.front().position;
  auto upper = lower;
  for (const auto& vertex : mesh.vertices) {
    const auto& p = vertex.position;
    lower.x = std::min(lower.x, p.x); lower.y = std::min(lower.y, p.y); lower.z = std::min(lower.z, p.z);
    upper.x = std::max(upper.x, p.x); upper.y = std::max(upper.y, p.y); upper.z = std::max(upper.z, p.z);
  }
  // Start where the terrain's bounding sphere exactly fills the 60-degree
  // vertical field of view. Terrain is far flatter than its sphere, which
  // leaves a margin around the oblique default view; interactive zoom is
  // later clamped to 0.2x..4x of this distance.
  constexpr double kHalfFovSine = 0.5;  // sin(60 deg / 2)
  const Vec3 extent = upper - lower;
  const double radius = 0.5 * extent.length();
  return {{(lower.x + upper.x) * 0.5, (lower.y + upper.y) * 0.5,
           (lower.z + upper.z) * 0.5},
          std::max(200.0, radius / kHalfFovSine)};
}

struct CameraFrame {
  terraflux::math::Mat4 view_projection;
  terraflux::math::Vec3 eye;
};

// The viewpoint of one frame, independent of whether it came from the orbit
// (world view) or the first-person camera: where the eye is, what it looks
// at and the depth range the projection has to cover.
struct CameraPose {
  terraflux::math::Vec3 eye, target;
  double near_plane = 1.0, far_plane = 12000.0;
  terraflux::math::Vec3 up{0, 1, 0};  // the gait rolls the first-person head slightly
};

CameraPose orbit_pose(const terraflux::graphics::OrbitCamera& orbit) {
  return {orbit.eye(), orbit.target, 1.0, std::max(12000.0, orbit.distance * 8.0), {0, 1, 0}};
}
// Standing on the ground the near plane moves in to 0.3 m so grass and rock at
// the feet are not cut away; the far plane still covers the whole terrain.
CameraPose first_person_pose(const terraflux::graphics::FirstPersonCamera& person, double framing_distance) {
  return {person.eye(), person.target(), 0.3, std::max(12000.0, framing_distance * 8.0), person.up()};
}

CameraFrame update_camera(terraflux::graphics::RenderDevice& device,
                   terraflux::graphics::BufferHandle uniform, int width, int height,
                   const CameraPose& pose,
                   double cell_size, std::uint32_t fire_width,
                   std::uint32_t fire_height, double now) {
  using terraflux::math::Vec3;
  using terraflux::math::Mat4;
  const Vec3 eye = pose.eye;
  constexpr double kPi = 3.14159265358979323846;
  const Mat4 view = Mat4::lookAtRH(eye, pose.target, pose.up);
  const Mat4 proj = Mat4::perspectiveRH_NO(kPi / 3.0,
      static_cast<double>(width) / static_cast<double>(height), pose.near_plane, pose.far_plane);
  const Mat4 vp = proj * view;
  std::array<float, 20> camera{};
  for (std::size_t i = 0; i < 16; ++i) camera[i] = static_cast<float>(vp.values[i]);
  camera[16] = static_cast<float>(cell_size);
  camera[17] = static_cast<float>(fire_width);
  camera[18] = static_cast<float>(fire_height);
  camera[19] = static_cast<float>(now);
  device.update_buffer(uniform, 0, std::as_bytes(std::span(camera)));
  return {vp, eye};
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_args(argc, argv);


    std::optional<terraflux::replay::Playback> playback;
    std::optional<terraflux::compute::Grid> demo;
    if (!options.request_file.empty())
      playback.emplace(terraflux::replay::load(read_file(options.request_file),
                                              read_file(options.response_file)));
    else
      demo.emplace(make_demo_terrain());
    if(options.replay_controls_test && playback->frames.size()<3)
      throw std::invalid_argument("replay control test requires at least three frames");
    auto& grid = playback ? playback->grid : *demo;
    const double cell_size = playback ? playback->cell_size_meters : 30.0;

    // This is visualization-only preprocessing. Native compute outputs and
    // regression semantics are unchanged. Unmeasured rivers remain holes.
    terraflux::compute::fill_elevation_voids(grid);
    for (auto& cell : grid.cells)
      if (!cell.elevation_meters && cell.fuel_load > 0) cell.elevation_meters = 0.0;
    // The simulation lattice stays untouched; rendering uses a finer C1 surface
    // through the measured vertices (default 5 m), bounded by a vertex budget.
    const auto simulation_mesh = terraflux::terrain::build_terrain_mesh(grid, cell_size);
    const std::uint32_t refinement = options.render_cell > 0.0
        ? terraflux::terrain::refinement_for_cell_size(simulation_mesh, options.render_cell, options.max_render_vertices) : 1U;
    const auto mesh = terraflux::terrain::refine_terrain_mesh(simulation_mesh, refinement, options.max_render_vertices);
    auto upload = (!options.terrain_tiles || options.smoke_test)?
        terraflux::graphics::prepare_terrain_upload(mesh):terraflux::graphics::TerrainUpload{};
    std::optional<terraflux::replay::FireField> fire_field;
    if (playback) fire_field.emplace(terraflux::replay::make_fire_field(grid));
    if (mesh.indices.empty()) throw std::runtime_error("scenario terrain has no drawable triangles");
    const auto framing_camera = camera_geometry(mesh);
    auto scene_camera = framing_camera;  // target pans with W/A/S/D and follows the ground
    const terraflux::graphics::TerrainSampler ground_sampler(mesh);
    const auto mesh_summary=terraflux::terrain::summarize_terrain_mesh(mesh);
    // Height colors span this surface's own range; contour spacing defaults to
    // an automatic 1-2-2.5-5 step, detail and snow line are style options.
    auto terrain_style=terraflux::graphics::terrain_style_for(mesh,options.terrain_contour_interval.value_or(-1.0));
    terrain_style.detail_strength=options.terrain_detail;
    terrain_style.rock_relief_meters=options.rock_relief;
    if(options.snow_line)terrain_style.snow_line_fraction=*options.snow_line;
    (void)terraflux::graphics::pack_terrain_style(terrain_style); // Validate the combined style early.
    if(options.smoke_test || options.profile || options.residency_report || options.benchmark_frames)
      std::cout<<"TERRAIN_MESH_JSON={\"vertices\":"<<mesh_summary.vertex_count
               <<",\"triangles\":"<<mesh_summary.triangle_count
               <<",\"simulation_vertices\":"<<simulation_mesh.vertices.size()
               <<",\"simulation_triangles\":"<<simulation_mesh.indices.size()/3
               <<",\"simulation_cell_m\":"<<simulation_mesh.cell_size_meters
               <<",\"render_cell_m\":"<<mesh.cell_size_meters<<",\"refinement\":"<<mesh.refinement
               <<",\"min_elevation_m\":"<<mesh_summary.min_elevation_meters
               <<",\"max_elevation_m\":"<<mesh_summary.max_elevation_meters
               <<",\"max_slope_deg\":"<<mesh_summary.max_slope_degrees
               <<",\"mean_slope_deg\":"<<mesh_summary.mean_slope_degrees
               <<",\"curvature\":["<<mesh_summary.min_curvature<<","<<mesh_summary.max_curvature<<"]"
               <<",\"vegetated_vertices\":"<<mesh_summary.vegetated_vertices
               <<",\"contour_interval_m\":"<<terrain_style.contour_interval_meters
               <<",\"rock_slope_deg\":["<<terrain_style.rock_slope_start_degrees<<","<<terrain_style.rock_slope_full_degrees<<"]"
               <<",\"snow_line\":"<<terrain_style.snow_line_fraction
               <<",\"detail\":"<<terrain_style.detail_strength
               <<",\"rock_relief_m\":"<<terrain_style.rock_relief_meters<<"}\n";

    // Weather: wind wandering between two thresholds, a solar clock with
    // twilight, a cloud form with the conditions it announces and the
    // orographic response of this terrain. Smoke runs keep it off unless asked
    // (--weather), so every reference comparison keeps the neutral sky; the
    // scripted --weather-test drives it explicitly.
    const bool weather_requested=options.weather?*options.weather!="off":!options.smoke_test;
    terraflux::graphics::WeatherConfig weather_config;
    weather_config.enabled=weather_requested && !options.weather_test && !options.storm_test && !options.vitals_test;
    // "auto" (or no option) leaves the form to the replay's weather, cumulus otherwise.
    const bool explicit_form=options.weather && *options.weather!="off" && *options.weather!="auto";
    weather_config.clouds=explicit_form?terraflux::graphics::parse_cloud_type(options.weather->c_str()):terraflux::graphics::CloudType::Cumulus;
    weather_config.coverage=options.cloud_cover;
    weather_config.cloud_base_meters=options.cloud_base;
    weather_config.temperature_celsius=options.temperature;
    weather_config.dew_point_celsius=options.dew_point;
    weather_config.reference_elevation_meters=mesh.min_elevation_meters;
    weather_config.stability_hz=options.stability;
    // A replay brings its own drivers: the request's wind (v2/v3 blow FROM the given compass
    // angle) seeds the viewer's wind unless overridden, and a v3 weather series steers the
    // temperature, humidity, rain and cloud form frame by frame (replay_weather below).
    const bool replay_wind=playback && playback->contract_version!=terraflux::compute::kContractVersionV1 && playback->wind_speed_kmh>0;
    const double replay_wind_mps=replay_wind?playback->wind_speed_kmh/3.6:0.0;
    weather_config.wind.direction_degrees=options.wind_direction.value_or(
        replay_wind?std::fmod(playback->wind_direction_degrees+180.0,360.0):options.wave_direction);
    weather_config.wind.speed_min_mps=options.wind_min.value_or(replay_wind?std::clamp(0.6*replay_wind_mps,0.0,60.0):2.0);
    weather_config.wind.speed_max_mps=options.wind_max.value_or(replay_wind?std::clamp(1.3*replay_wind_mps,0.0,60.0):6.0);
    if(weather_config.wind.speed_max_mps<weather_config.wind.speed_min_mps)weather_config.wind.speed_max_mps=weather_config.wind.speed_min_mps;
    weather_config.wind.swing_degrees=options.wind_swing;weather_config.wind.period_seconds=options.wind_period;
    weather_config.wind.seed=options.vegetation_seed;
    weather_config.daylight.latitude_degrees=options.latitude;weather_config.daylight.day_of_year=options.day_of_year;
    weather_config.daylight.start_hour=options.hour;weather_config.daylight.seconds_per_hour=options.time_scale;
    weather_config.daylight.moon_phase=options.moon_phase;weather_config.daylight.moon_diameter_degrees=options.moon_size;
    weather_config.seed=options.vegetation_seed;
    terraflux::graphics::validate_weather(weather_config);
    terraflux::graphics::WeatherSimulation weather(weather_config);
    // Reproduce a moment of a gust, a shower band or a strike, with the rain that fell until then.
    for(double elapsed=0;elapsed<options.weather_time;elapsed+=1.0)weather.advance(std::min(1.0,options.weather_time-elapsed));
    if(options.rainfall_mm>0.0)weather.set_rainfall_mm(options.rainfall_mm+weather.rainfall_mm());
    const auto height_field=terraflux::graphics::sample_height_field(mesh,128);
    // Where rain runs: drainage, slopes, waterfalls and their spray (static), on a finer
    // lattice than the orographic field so the cascades resolve their lips and ledges.
    const auto runoff_terrain=terraflux::graphics::sample_height_field(mesh,256);
    const auto runoff_field=terraflux::graphics::compute_runoff_field(runoff_terrain);
    auto spray_emitters=terraflux::graphics::waterfall_spray_emitters(runoff_field,runoff_terrain);

    // The lattice averages the rendered surface: seat every emitter on the mesh itself so
    // its drops start on the rock, not inside it.
    for(auto& e:spray_emitters)
      if(const auto ground=ground_sampler.sample(e.position.x,e.position.z))e.position.y=std::max(e.position.y,ground->position.y)+0.3;
    // v3 replay weather: the sample of the shown frame sets the surface conditions the fire
    // was simulated under; the cloud form follows the rain unless --weather chose one.
    const bool replay_weather=playback && !playback->weather.empty();
    std::optional<std::size_t> replay_weather_frame;
    const auto apply_replay_weather=[&](std::size_t frame) {
      if(!replay_weather || replay_weather_frame==frame)return;
      const auto sample=*playback->weather_for_frame(frame);
      auto config=weather.config();
      config.temperature_celsius=std::clamp(sample.temperature_celsius,-40.0,50.0);
      config.dew_point_celsius=std::clamp(terraflux::graphics::dew_point_from_humidity(config.temperature_celsius,sample.relative_humidity),-60.0,50.0);
      if(!explicit_form) {
        // The form follows the sample; its base is the form's typical one (the saturated
        // surface air of a rainy step would otherwise put the base on the ground as fog).
        config.clouds=terraflux::graphics::cloud_type_for_sample(sample,terraflux::graphics::CloudType::Cumulus);
        if(!options.cloud_base)config.cloud_base_meters=config.reference_elevation_meters+terraflux::graphics::cloud_profile(config.clouds).base_meters_agl;
        if(!options.cloud_cover)config.coverage.reset();
      }
      config.precipitation_mm_h=std::clamp(sample.precipitation_mm_h,0.0,200.0);
      weather.set_config(config);
      replay_weather_frame=frame;
    };
    if(replay_weather)apply_replay_weather(0);
    const auto report_weather=[&](const char* event) {
      if(!(options.smoke_test || options.profile || options.residency_report || options.benchmark_frames))return;
      const auto& c=weather.config();
      const auto layer=weather.layer();
      const auto sun=weather.sun();
      const auto wind=weather.wind();
      const auto* field=weather.field();
      terraflux::graphics::CameraBasis probe;probe.eye={0,c.reference_elevation_meters,0};
      const auto now=weather.state(probe,c.reference_elevation_meters);
      std::cout<<"WEATHER_JSON={\"event\":\""<<event<<"\",\"enabled\":"<<(c.enabled?"true":"false")
               <<",\"surface_temperature_c\":"<<now.temperature_celsius<<",\"relative_humidity\":"<<now.relative_humidity
               <<",\"rain_mm_h\":"<<now.precipitation_mm_h<<",\"fire_weather\":"<<now.fire_weather<<",\"snow\":"<<(now.snow?"true":"false")
               <<",\"fire_vigour\":"<<(c.enabled?std::clamp(1.0+2.5*now.fire_weather,0.25,1.6):1.0)
               <<",\"rainfall_mm\":"<<now.rainfall_mm<<",\"ponding\":"<<now.ponding<<",\"flood_rise_m\":"<<now.flood_rise_meters
               <<",\"convection\":"<<now.convection<<",\"rainbow\":"<<now.rainbow<<",\"drop_mm\":"<<now.drop_diameter_mm
               <<",\"freezing_level_m\":"<<now.freezing_level_meters<<",\"runoff\":"<<now.runoff<<",\"snow_cover\":"<<now.snow_cover
               <<",\"channel_texels\":"<<runoff_field.channel_texels<<",\"waterfall_texels\":"<<runoff_field.waterfall_texels
               <<",\"lip_texels\":"<<runoff_field.lip_texels<<",\"plunge_texels\":"<<runoff_field.plunge_texels
               <<",\"max_drop_m\":"<<runoff_field.max_drop_meters<<",\"spray_emitters\":"<<spray_emitters.size()
               <<",\"replay_weather\":"<<(replay_weather?"true":"false")
               <<",\"clouds\":\""<<terraflux::graphics::cloud_type_name(layer.type)<<"\",\"expected\":\""
               <<terraflux::graphics::cloud_profile(layer.type).expected_weather<<"\""
               <<",\"cloud_base_m\":"<<layer.base_meters<<",\"cloud_top_m\":"<<layer.top_meters<<",\"coverage\":"<<layer.coverage
               <<",\"precipitation\":"<<layer.precipitation<<",\"storm\":"<<layer.storm<<",\"visibility_m\":"<<layer.visibility_meters
               <<",\"temperature_c\":"<<c.temperature_celsius<<",\"dew_point_c\":"<<layer.dew_point_celsius
               <<",\"hour\":"<<sun.hour<<",\"phase\":\""<<terraflux::graphics::daylight_phase_name(sun.phase)<<"\""
               <<",\"sun_altitude_deg\":"<<sun.altitude_degrees<<",\"sun_azimuth_deg\":"<<sun.azimuth_degrees<<",\"daylight\":"<<sun.daylight
               <<",\"moon_phase\":"<<sun.moon_phase<<",\"moon_illumination\":"<<sun.moon_illumination<<",\"moon_altitude_deg\":"<<sun.moon_altitude_degrees
               <<",\"moon_azimuth_deg\":"<<sun.moon_azimuth_degrees<<",\"moonlight\":"<<sun.moon_intensity
               <<",\"wind_toward_deg\":"<<wind.direction_degrees<<",\"wind_mps\":"<<wind.speed_mps
               <<",\"wind_range_mps\":["<<weather.effective_wind().speed_min_mps<<","<<weather.effective_wind().speed_max_mps<<"]"
               <<",\"field\":"<<(field?"{\"width\":"+std::to_string(field->width)+",\"height\":"+std::to_string(field->height)+
                   ",\"coupling\":"+std::to_string(field->coupling)+",\"froude\":"+std::to_string(field->froude)+
                   ",\"max_thickening\":"+std::to_string(field->max_thickening)+",\"max_dissipation\":"+std::to_string(field->max_dissipation)+
                   ",\"windward_texels\":"+std::to_string(field->windward_texels)+",\"lee_texels\":"+std::to_string(field->lee_texels)+
                   ",\"blocked_texels\":"+std::to_string(field->blocked_texels)+",\"in_cloud_texels\":"+std::to_string(field->in_cloud_texels)+"}":"null")
               <<"}\n";
    };

    // Still-water surface: the demo lake by default, otherwise only on request.
    std::optional<terraflux::graphics::WaterStyle> water_style;
    terraflux::graphics::WaterUpload water_upload;
    if(options.water_level || !playback) {
      terraflux::graphics::WaterStyle style;
      style.level_meters=options.water_level.value_or(kDemoWaterLevel);
      style.wave_height_meters=options.wave_height;style.wave_length_meters=options.wave_length;
      style.wave_speed_mps=options.wave_speed;style.wave_direction_degrees=options.wave_direction;
      style.clarity_meters=options.water_clarity;
      style.light_direction=terrain_style.light_direction;
      (void)terraflux::graphics::pack_water_style(style,{},0); // Validate before building geometry.
      const auto water_mesh=terraflux::terrain::build_water_mesh(mesh,style.level_meters);
      if(options.smoke_test || options.profile || options.residency_report || options.benchmark_frames)
        std::cout<<"WATER_JSON={\"level_m\":"<<style.level_meters<<",\"triangles\":"<<water_mesh.indices.size()/3
                 <<",\"submerged_vertices\":"<<water_mesh.submerged_vertices<<",\"max_depth_m\":"<<water_mesh.max_depth_meters
                 <<",\"wave_height_m\":"<<style.wave_height_meters<<",\"wave_length_m\":"<<style.wave_length_meters
                 <<",\"wave_speed_mps\":"<<style.wave_speed_mps<<",\"wave_direction_deg\":"<<style.wave_direction_degrees
                 <<",\"clarity_m\":"<<style.clarity_meters<<"}\n";
      if(options.water_test && water_mesh.submerged_vertices<50)
        throw std::invalid_argument("--water-test needs a water level that submerges at least fifty vertices");
      if(!water_mesh.indices.empty()) {
        water_upload=terraflux::graphics::prepare_water_upload(water_mesh);
        water_style=style;
        // Submerged ground renders as lake/sea floor. "random" derives weights
        // and patch layout from a seed (random unless --floor-seed) and prints it.
        terraflux::graphics::WaterFloorStyle floor;
        if(options.water_floor=="random") {
          const std::uint32_t seed=options.floor_seed.value_or(static_cast<std::uint32_t>(std::random_device{}()));
          floor=terraflux::graphics::random_water_floor(seed);
        } else {
          floor.kind=terraflux::graphics::parse_water_floor(options.water_floor.c_str());
          if(options.floor_seed)floor.seed=*options.floor_seed;
        }
        if(options.floor_patch_size)floor.patch_size_meters=*options.floor_patch_size;
        terrain_style.submerged_level_meters=style.level_meters;
        terrain_style.floor=floor;
        (void)terraflux::graphics::pack_terrain_style(terrain_style);
        if(options.smoke_test || options.profile || options.residency_report || options.benchmark_frames)
          std::cout<<"WATER_FLOOR_JSON={\"kind\":\""<<terraflux::graphics::water_floor_name(floor.kind)
                   <<"\",\"requested\":\""<<options.water_floor<<"\",\"seed\":"<<floor.seed
                   <<",\"weights\":["<<floor.sand<<","<<floor.vegetation<<","<<floor.mud<<","<<floor.stones<<"]"
                   <<",\"patch_size_m\":"<<floor.patch_size_meters<<"}\n";
      }
    }

    // Where a viewer can stand at a plan position: the measured ground or the
    // water surface over it, lifted by the rock relief the crags may be
    // displaced by (visualization only, so the eye never sinks into a crag).
    // Holes and the outside of the terrain have no standing height. Shared by
    // the first-person camera, the orbit target snapping of W/A/S/D and the
    // --view-x/--view-z start offset so the two views agree exactly.
    double rendered_rock_relief=terrain_style.detail_strength*terrain_style.rock_relief_meters; // the crag displacement the spray must clear
    // Ground with the crag allowance but without the water: the walker wades,
    // swims or is swept in the water rather than walking on it (see the flood
    // model below); the orbit target keeps hovering above the water.
    const terraflux::graphics::StandingHeightFn ground_height=[&](double x,double z)->std::optional<double> {
      const auto ground=ground_sampler.sample(x,z);
      if(!ground)return std::nullopt;
      terraflux::graphics::TerrainSurfacePoint point;
      point.elevation_meters=ground->position.y;point.normal=ground->normal;
      point.fuel_load=ground->fuel_load;point.fuel_moisture=ground->fuel_moisture;point.curvature=ground->curvature;
      const auto material=terraflux::graphics::terrain_material(point,terrain_style);
      double amount=material.rock*(1.0-0.6*std::max(material.snow,material.ice))*(1.0-material.submerged);
      if(terrain_style.submerged_level_meters) {
        const double relief=std::max(terrain_style.rock_relief_meters,0.0);
        const double t=relief>0?std::clamp((ground->position.y-*terrain_style.submerged_level_meters)/(2.0*relief),0.0,1.0):1.0;
        amount*=t*t*(3.0-2.0*t);
      }
      return ground->position.y+amount*std::clamp(rendered_rock_relief,0.0,50.0);
    };
    const terraflux::graphics::StandingHeightFn standing_height=[&](double x,double z)->std::optional<double> {
      const auto ground=ground_sampler.sample(x,z);
      if(!ground)return std::nullopt;
      terraflux::graphics::TerrainSurfacePoint point;
      point.elevation_meters=ground->position.y;point.normal=ground->normal;
      point.fuel_load=ground->fuel_load;point.fuel_moisture=ground->fuel_moisture;point.curvature=ground->curvature;
      // The shader's rockRelief() amount without its +-1 noise term: the most a
      // crag can rise here (damped under snow and ice, none at or just above the water line).
      const auto material=terraflux::graphics::terrain_material(point,terrain_style);
      double amount=material.rock*(1.0-0.6*std::max(material.snow,material.ice))*(1.0-material.submerged);
      if(terrain_style.submerged_level_meters) {
        const double relief=std::max(terrain_style.rock_relief_meters,0.0);
        const double t=relief>0?std::clamp((ground->position.y-*terrain_style.submerged_level_meters)/(2.0*relief),0.0,1.0):1.0;
        amount*=t*t*(3.0-2.0*t); // smoothstep(level, level + 2 relief, elevation)
      }
      const double crags=amount*std::clamp(rendered_rock_relief,0.0,50.0);
      const double surface=water_style?std::max(ground->position.y,water_style->level_meters):ground->position.y;
      return surface+crags;
    };
    // Moves the orbit target over the ground (or the water) and keeps it
    // hovering kOrbitTargetHeight above it; rejected over holes.
    const auto snap_orbit_target=[&](terraflux::math::Vec3 target) {
      target.x=std::clamp(target.x,ground_sampler.lower_bound().x,ground_sampler.upper_bound().x);
      target.z=std::clamp(target.z,ground_sampler.lower_bound().z,ground_sampler.upper_bound().z);
      if(const auto standing=standing_height(target.x,target.z))target.y=*standing+terraflux::graphics::kOrbitTargetHeight;
      return target;
    };
    // An initial target offset walks the camera over the ground (or the water) like W/A/S/D.
    if(options.view_x!=0.0 || options.view_z!=0.0)
      scene_camera.target=snap_orbit_target({scene_camera.target.x+options.view_x,scene_camera.target.y,scene_camera.target.z+options.view_z});
    // The first-person camera stands on the ground; Tab (or --view first-person)
    // puts it under the orbit target looking the orbit's way.
    terraflux::graphics::CameraMode view_mode=terraflux::graphics::CameraMode::World;
    terraflux::graphics::FirstPersonCamera person;
    if(options.walk_speed)person.walk_speed_mps=*options.walk_speed;
    const double standing_search_step=std::max(1.0,mesh.cell_size_meters);
    const double standing_search_radius=std::max(ground_sampler.upper_bound().x-ground_sampler.lower_bound().x,
                                                 ground_sampler.upper_bound().z-ground_sampler.lower_bound().z);
    // What the walker collides with: tree trunks (rebuilt with the vegetation),
    // imported assets (their bounding spheres, refreshed every frame because
    // scenes animate) and ground steeper than the slope limit. Grass and
    // anything lower than the step height are walked over. --no-collision
    // empties the fields and lifts the slope limit.
    terraflux::graphics::ObstacleField tree_field,asset_field;
    std::vector<terraflux::graphics::Obstacle> asset_obstacles;
    terraflux::graphics::WalkEnvironment walk_env;
    walk_env.standing_height=ground_height;
    walk_env.lower=ground_sampler.lower_bound();walk_env.upper=ground_sampler.upper_bound();
    walk_env.body.height=person.eye_height_meters;
    walk_env.body.max_slope_degrees=options.no_collision?89.0:options.max_slope;
    terraflux::graphics::ObstacleField stone_field;  // the stones lying about (filled below)
    terraflux::graphics::ObstacleField road_field;   // bridge railings and piers (filled below)
    terraflux::graphics::ObstacleField building_field;  // the buildings' walls and furniture (filled below)
    terraflux::graphics::ObstacleField door_field;      // the door leaves as they stand this frame (rebuilt while they swing)
    if(!options.no_collision)walk_env.obstacles={&tree_field,&asset_field,&stone_field,&road_field,&building_field,&door_field};
    terraflux::graphics::validate_walker_body(walk_env.body);
    // Earth-like gravity: Space jumps, ledges are fallen from, landings dip the eye.
    walk_env.physics.gravity_mps2=options.gravity;
    walk_env.physics.jump_height_meters=options.jump_height;
    terraflux::graphics::validate_walker_physics(walk_env.physics);
    // Walking in steps: acceleration from rest, a speed pulse per step, head
    // bob, sway and roll over the stance foot, a jolt per footfall, stops on a footfall.
    walk_env.gait.enabled=!options.no_gait;
    terraflux::graphics::validate_gait_style(walk_env.gait);
    // Flash floods: the rain that lands on the slopes runs down this drainage and
    // fills the channels; the walker wades, swims or is swept in it and it is
    // drawn as water lying on the channels. The lake (with its flood rise) is
    // the other water the walker meets.
    terraflux::graphics::FloodStyle flood_style;
    terraflux::graphics::FloodModel flood(runoff_field,flood_style);
    if(options.flash_flood)flood.set_flash(*options.flash_flood);
    double built_flash=-1.0;
    walk_env.water=[&](double x,double z,double ground_y) {
      return flood.sample(x,z,ground_y,water_style?std::optional<double>(water_style->level_meters):std::nullopt);
    };
    double last_rain_mm_h=0.0;
    // Climbing: what the face is made of decides how steep a climb it allows.
    // Dry rock 75 degrees, wet rock 62, soil 55 (wet 48), snow 40; ice,
    // running water and a waterfall that is running cannot be climbed.
    double last_runoff=0.0;
    walk_env.climb=[&](double x,double z)->terraflux::graphics::ClimbSurface {
      terraflux::graphics::ClimbSurface c;
      const auto ground=ground_sampler.sample(x,z);
      if(!ground)return c;
      const bool water_running=last_runoff>0.05 || flood.flash()>0.05;
      if(water_running && flood.waterfall_at(x,z)>0.3){c.name="waterfall";return c;}
      const auto w=flood.sample(x,z,ground->position.y,water_style?std::optional<double>(water_style->level_meters):std::nullopt);
      if(w.channel && w.waterfall){c.name="waterfall";return c;}
      if(w.channel && w.depth_meters>0.1){c.name="running water";return c;}
      terraflux::graphics::TerrainSurfacePoint point;
      point.elevation_meters=ground->position.y;point.normal=ground->normal;
      point.fuel_load=ground->fuel_load;point.fuel_moisture=ground->fuel_moisture;point.curvature=ground->curvature;
      const auto m=terraflux::graphics::terrain_material(point,terrain_style);
      const bool wet=last_rain_mm_h>0.5 || last_runoff>0.2;
      if(m.ice>=0.5){c.name="ice";return c;}
      c.climbable=true;
      if(m.snow>=0.5){c.name="snow";c.max_slope_degrees=40.0;}
      else if(m.rock>=0.5){c.name=wet?"wet rock":"rock";c.max_slope_degrees=wet?62.0:75.0;}
      else {c.name=wet?"wet soil":"soil";c.max_slope_degrees=wet?48.0:55.0;}
      return c;
    };
    // The ground's grip: ice 0.1, snow (the material or a frozen fresh cover) 0.5,
    // wet rock 0.7, wet soil 0.85, dry ground 1.
    double last_snow_cover=0.0,last_temperature=18.0;
    walk_env.grip=[&](double x,double z)->double {
      const auto ground=ground_sampler.sample(x,z);
      if(!ground)return 1.0;
      terraflux::graphics::TerrainSurfacePoint point;
      point.elevation_meters=ground->position.y;point.normal=ground->normal;
      point.fuel_load=ground->fuel_load;point.fuel_moisture=ground->fuel_moisture;point.curvature=ground->curvature;
      const auto m=terraflux::graphics::terrain_material(point,terrain_style);
      const bool wet=last_rain_mm_h>0.5 || last_runoff>0.2;
      if(m.ice>=0.5)return 0.1;
      if(m.snow>=0.5 || (last_snow_cover>0.5 && last_temperature<0.0))return 0.5;
      if(m.rock>=0.5)return wet?0.7:1.0;
      return wet?0.85:1.0;
    };
    // Roads and bridges (see roads.hpp): two-way carriageways draped over the
    // ground and decks spanning valleys and water on railings and piers, all
    // one connected network (a bridge always joins roads). The walker rests
    // on their surfaces (a deck carries it over whatever lies below), goes
    // faster on a sound road or deck, loses the bonus beside a fallen trunk
    // and over a damaged surface, and is slowed by rubble. Burning trunks
    // that fall across them damage them, a deck alight collapses, a flash
    // flood at the piers washes a bridge out — under the walker, too.
    // Interactive runs get the demo network (two routes that cross west of
    // the lake, one of them over the lake on a long bridge); smoke runs leave
    // it out unless --roads asks, so reference images stay as they were.
    std::unique_ptr<terraflux::graphics::RoadNetwork> roads;
    terraflux::graphics::RoadStyle road_style;
    road_style.seed=options.vegetation_seed;
    terraflux::graphics::RoadPlanStyle road_plan;
    road_plan.max_span_meters=1000.0;  // the demo lake is 870 m across where the road crosses it
    std::size_t vegetation_cleared_for_roads=0;
    // "auto" is the city in interactive runs, the simple demo network for the roads and
    // buildings tests, and nothing in other smoke runs.
    const std::string roads_choice=options.roads!="auto"?options.roads:options.city_test?"city":(options.roads_test || options.buildings_test || options.interior_test || options.hazards_test)?"demo":options.smoke_test?"off":"city";
    std::optional<terraflux::graphics::CityPlan> city_plan;
    std::optional<terraflux::graphics::CityLayout> city;
    std::vector<terraflux::graphics::BuildingRequest> city_requests;
    {
      const bool wanted=roads_choice!="off";
      if(wanted) {
        const terraflux::math::Vec2 centre{framing_camera.target.x,framing_camera.target.z};  // positions are offsets from the terrain centre, as --view-x/--view-z
        const std::optional<double> lake=water_style?std::optional<double>(water_style->level_meters):std::nullopt;
        terraflux::graphics::RoadNetworkDef def;
        if(roads_choice=="city") {
          // The city: zones of curved streets, a CBD of towers, suburbs, an airport, and highways with interchanges.
          const auto lo=ground_sampler.lower_bound(),hi=ground_sampler.upper_bound();
          city_plan=playback?terraflux::graphics::generic_city_plan(centre,{lo.x,lo.z},{hi.x,hi.z}):terraflux::graphics::demo_city_plan(centre,{lo.x,lo.z},{hi.x,hi.z});
          try { city=terraflux::graphics::plan_city(*city_plan,ground_height,lake,road_style,road_plan); def=city->roads; city_requests=city->buildings; }
          catch(const std::exception& error) {
            if(options.roads!="auto" || options.city_test)throw;
            std::cerr<<"City left out: "<<error.what()<<" (give --roads demo or a network file for this terrain)\n";
            city.reset();
          }
        } else if(roads_choice=="demo") {
          std::vector<std::vector<terraflux::math::Vec2>> routes;
          if(!playback) {
            // The demo's two routes: west to east across the lowland and over the lake
            // to the foot of the escarpment, and north to south along the western
            // lowland; they cross at (-825, -100).
            routes={{{centre.x-1000,centre.y-100},{centre.x-825,centre.y-100},{centre.x+200,centre.y-100}},
                    {{centre.x-825,centre.y-1005},{centre.x-825,centre.y-100},{centre.x-825,centre.y+1035}}};
          } else {
            // Replay terrain: two routes through the centre over the middle three fifths of the extent.
            const auto lo=ground_sampler.lower_bound(),hi=ground_sampler.upper_bound();
            routes={{{lo.x+(hi.x-lo.x)*0.2,centre.y},{centre.x,centre.y},{lo.x+(hi.x-lo.x)*0.8,centre.y}},
                    {{centre.x,lo.z+(hi.z-lo.z)*0.2},{centre.x,centre.y},{centre.x,lo.z+(hi.z-lo.z)*0.8}}};
          }
          try { def=terraflux::graphics::plan_road_network(routes,ground_height,lake,road_style,road_plan); }
          catch(const std::exception& error) {
            if(options.roads!="auto" || options.roads_test)throw;
            std::cerr<<"Roads left out: "<<error.what()<<" (give --roads with a network file for this terrain)\n";
          }
        } else def=terraflux::graphics::parse_road_network(read_file(roads_choice),ground_height,lake,road_style,road_plan,centre);
        if(!def.segments.empty())roads=std::make_unique<terraflux::graphics::RoadNetwork>(std::move(def),road_style,ground_height);
      }
    }
    std::uint64_t road_obstacles_version=0;
    const auto rebuild_road_obstacles=[&] {
      road_field=(roads && !options.no_collision)?terraflux::graphics::ObstacleField(roads->obstacles()):terraflux::graphics::ObstacleField{};
      if(roads)road_obstacles_version=roads->version();
    };
    rebuild_road_obstacles();
    // Buildings (see buildings.hpp): a bungalow, a two-storey house, a
    // hospital and a skyscraper, each beside a road with its door facing it
    // and a footpath to the carriageway. The demo and replay networks get the
    // four automatically (nearest to where the routes cross); a network file
    // lists its own under "buildings"; --buildings off leaves them out.
    std::unique_ptr<terraflux::graphics::BuildingSet> buildings;
    terraflux::graphics::BuildingPlacementStyle building_style;
    building_style.seed=options.vegetation_seed;
    building_style.path_speed_factor=road_style.road_speed_factor;
    if(roads && options.buildings!="off") {
      const terraflux::math::Vec2 centre{framing_camera.target.x,framing_camera.target.z};
      const std::optional<double> lake=water_style?std::optional<double>(water_style->level_meters):std::nullopt;
      std::vector<terraflux::graphics::BuildingRequest> requests;
      terraflux::math::Vec2 preferred=centre;
      terraflux::graphics::PlacementReport placement_report;
      if(city)requests=city_requests;
      else if(roads_choice=="demo") {
        for(const auto kind:{terraflux::graphics::BuildingKind::Skyscraper,terraflux::graphics::BuildingKind::Hospital,terraflux::graphics::BuildingKind::House,terraflux::graphics::BuildingKind::Bungalow})
          requests.push_back({kind,std::nullopt});
        preferred=playback?centre:terraflux::math::Vec2{centre.x-825,centre.y-100};  // the demo routes cross here
      } else requests=terraflux::graphics::parse_building_requests(read_file(roads_choice));
      // A city's lots are offered; the unsuitable ones stay empty. Authored sites must stand.
      auto placed=terraflux::graphics::place_buildings(*roads,requests,ground_height,lake,preferred,building_style,city.has_value(),&placement_report);
      if(!city && placed.size()<requests.size())std::cerr<<"Buildings: "<<(requests.size()-placed.size())<<" of "<<requests.size()<<" found no suitable site beside the roads\n";
      if(city && (options.smoke_test || options.profile || options.residency_report)) {
        const auto& st=city->stats;
        std::cout<<"CITY_JSON={\"zones\":"<<st.zones<<",\"cbds\":"<<st.cbds<<",\"suburbs\":"<<st.suburbs<<",\"airports\":"<<st.airports<<",\"interchanges\":"<<st.interchanges
                 <<",\"at_grade_junctions\":"<<st.at_grade_junctions<<",\"lots\":"<<st.lots<<",\"requests\":"<<requests.size()<<",\"placed\":"<<placement_report.placed
                 <<",\"empty_lots\":{\"uneven\":"<<placement_report.uneven<<",\"buried\":"<<placement_report.buried<<",\"wet\":"<<placement_report.wet<<",\"on_road\":"<<placement_report.on_road
                 <<",\"no_ground\":"<<placement_report.no_ground<<",\"taken\":"<<placement_report.taken<<"},\"zone_names\":[";
        for(std::size_t i=0;i<city_plan->zones.size();++i)std::cout<<(i?",":"")<<"{\"name\":\""<<city_plan->zones[i].name<<"\",\"kind\":\""<<terraflux::graphics::zone_kind_name(city_plan->zones[i].kind)<<"\"}";
        std::cout<<"],\"junctions\":[";
        for(std::size_t i=0;i<city->interchanges.size();++i){const auto& ic=city->interchanges[i];std::cout<<(i?",":"")<<"{\"zone\":\""<<ic.zone<<"\",\"diamond\":"<<(ic.diamond?"true":"false")<<",\"at\":["<<ic.at.x<<","<<ic.at.y<<"]}";}
        std::cout<<"]}\n";
      }
      if(!placed.empty())buildings=std::make_unique<terraflux::graphics::BuildingSet>(std::move(placed),ground_height,building_style);
    }
    // The zone a plan position lies in (for the title).
    const auto zone_at=[&](double x,double z)->const terraflux::graphics::CityZone* {
      if(!city_plan)return nullptr;
      for(const auto& zone:city_plan->zones)if(std::hypot(x-zone.centre.x,z-zone.centre.y)<=zone.radius_meters+20.0)return &zone;
      return nullptr;
    };
    std::uint64_t building_obstacles_version=~std::uint64_t{0};
    const auto rebuild_building_obstacles=[&] {
      building_field=(buildings && !options.no_collision)?terraflux::graphics::ObstacleField(buildings->obstacles()):terraflux::graphics::ObstacleField{};
      building_obstacles_version=buildings?buildings->version():0;
    };
    rebuild_building_obstacles();
    std::uint64_t door_obstacles_version=~std::uint64_t{0};
    const auto rebuild_door_obstacles=[&] {
      door_field=(buildings && !options.no_collision)?terraflux::graphics::ObstacleField(buildings->door_obstacles()):terraflux::graphics::ObstacleField{};
      door_obstacles_version=buildings?buildings->door_version():0;
    };
    rebuild_door_obstacles();
    // The enterable building's door within arm's reach of the walker (from either side).
    const auto door_in_reach=[&]()->std::optional<std::size_t> {
      if(!buildings)return std::nullopt;
      const auto i=buildings->entrance_near(person.feet.x,person.feet.z,2.0);
      if(!i || !terraflux::graphics::building_enterable(buildings->buildings()[*i].kind))return std::nullopt;
      return i;
    };
    // What is built over the ground at a plan position: a road surface, a deck or a footpath.
    const auto built_surface=[&](double x,double z,double feet_y)->std::optional<double> {
      std::optional<double> best;
      if(roads)best=roads->platform_height(x,z,feet_y);
      if(buildings){const auto path=buildings->platform_height(x,z);if(path && (!best || *path>*best))best=path;}
      return best;
    };
    walk_env.platforms=built_surface;
    walk_env.surface_speed=[&](double x,double z) {
      if(roads){const auto at=roads->sample(x,z,person.feet.y);if(at.on)return at.speed_factor;}
      return buildings?buildings->speed_factor(x,z):1.0;
    };
    double pending_collapse=0.0;          // a deck gave way under the walker this frame (fraction of health)
    // The walker's body: health, body heat, wetness; what the world does to it
    // every frame in first person (see vitals.hpp).
    terraflux::graphics::Vitals vitals;
    terraflux::graphics::VitalsStyle vitals_style;
    terraflux::graphics::HeldItem held;   // what the hand carries (see the items below)
    std::string interact_hint;            // what E would do right now
    terraflux::graphics::LightningHazard pending_lightning=terraflux::graphics::LightningHazard::None;
    double pending_crush=0.0;             // a trunk that landed on the walker this frame (fraction of health)
    // The tools on the belt (see tools.hpp): 1 rope, 2 fire axe, 3 chainsaw.
    terraflux::graphics::ToolBelt belt;
    terraflux::graphics::Rope rope;
    terraflux::graphics::RopeRules rope_rules;
    terraflux::graphics::AxeRules axe_rules;
    terraflux::graphics::ChainsawRules saw_rules;
    rope_rules.length_meters=options.rope_length;
    saw_rules.fuel_seconds=options.chainsaw_fuel;
    belt.chainsaw_fuel_seconds=options.chainsaw_fuel;
    if(const auto preset=terraflux::graphics::tool_kind_from_name(options.tool.empty()?"none":options.tool))belt.selected=*preset;
    int chop_target=-1;                   // the tree (or -2 a dropped branch, -3 a burning branch) the swing in progress is aimed at
    std::size_t chop_branch=0;            // the dropped branch index for -2
    double saw_branch_seconds=0.0;        // sawing into a branch so far
    std::string tool_message;             // the last thing a tool did (for the title)
    std::string rope_message;
    std::string collapse_message;
    double vitals_heat_here=0.0,vitals_smoke_here=0.0;

    GlfwSession glfw;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, (options.smoke_test || options.benchmark_frames) ?
                     GLFW_FALSE : GLFW_TRUE);
    Window window(glfwCreateWindow(1280, 800, "TerraFlux 3D Wildfire Viewer", nullptr, nullptr));
    if (!window) throw std::runtime_error("OpenGL 4.3 context could not be created");
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval((options.smoke_test || options.benchmark_frames) ? 0 : 1);

    using namespace terraflux::graphics;
    OpenGLRenderDevice device;
    if(options.resource_budget_bytes)device.set_resource_byte_budget(*options.resource_budget_bytes);
    std::unique_ptr<FullscreenComposite> compositor;
    if(options.offscreen)compositor=std::make_unique<FullscreenComposite>(device);
    ResizableRenderTarget offscreen(device);
    std::uint64_t resize_failures=0;
    using ResizeAttempt=std::tuple<int,int,std::uint64_t,std::uint64_t>;
    std::optional<ResizeAttempt> failed_resize;
    std::optional<RenderTargetDesc> resize_test_original;
    RenderResourceOwner owned(device);
    // Daylight/cloud block and orographic field bound by every world draw;
    // the sky dome and precipitation only exist when weather can be shown.
    AtmosphereGpu atmosphere(device);
    if(weather_requested || options.weather_test || options.storm_test || options.vitals_test)atmosphere.update_runoff(runoff_field);
    std::unique_ptr<SkyRenderer> sky;
    std::unique_ptr<PrecipitationRenderer> precipitation;
    std::unique_ptr<LightningRenderer> lightning;
    std::unique_ptr<SprayRenderer> spray;
    // The walker's bars: health, body heat, strength, wetness (first person only).
    terraflux::graphics::HudRenderer hud(device);
    std::size_t hud_bars_drawn=0;
    // The walker's own arms and hands, posed for what it is doing.
    terraflux::graphics::BodyRenderer body(device);
    terraflux::graphics::BodyStyle body_style;
    bool show_arms=!options.no_arms;
    std::size_t body_draws=0;
    terraflux::graphics::BodyActivity body_activity=terraflux::graphics::BodyActivity::Idle;
    if(weather_requested || options.weather_test || options.storm_test || options.vitals_test) {
      sky=std::make_unique<SkyRenderer>(device);
      precipitation=std::make_unique<PrecipitationRenderer>(device);
      lightning=std::make_unique<LightningRenderer>(device);
      spray=std::make_unique<SprayRenderer>(device);
      spray->set_emitters(spray_emitters);
    }
    const auto vertices = bytes_of(upload.vertices);
    const auto indices = bytes_of(upload.indices);
    BufferHandle vb{},ib{};
    if(!options.terrain_tiles || options.smoke_test) {
      vb=owned.create_buffer({BufferUsage::Vertex,vertices.size()},vertices);
      ib=owned.create_buffer({BufferUsage::Index,indices.size()},indices);
    }
    LayerVisibility layer_visibility{!options.hide_terrain,!options.hide_fire};
    std::optional<BufferHandle> layer_buffer;
    if(playback) {
      const auto flags=layer_visibility.uniform();
      layer_buffer=owned.create_buffer({BufferUsage::Uniform,sizeof(flags)},std::as_bytes(std::span(&flags,1)));
    }
    double engine_time=0.0;
    std::string lightning_message;double lightning_message_until=0.0;
    // The walker's state in the title: climbing (on what, how steep), strength,
    // water, and why a steep face was refused.
    const auto walker_title=[&]()->std::string {
      if(view_mode!=terraflux::graphics::CameraMode::FirstPerson)return "";
      std::ostringstream text;
      if(person.climbing)text<<" | CLIMBING "<<person.climb_surface<<' '<<static_cast<int>(std::lround(person.climb_slope_degrees))<<"\u00b0";
      else if(person.climb_refusal[0])text<<" | cannot climb: "<<person.climb_refusal;
      if(person.stamina<0.999 || person.exhausted)text<<" | strength "<<static_cast<int>(std::lround(person.stamina*100))<<"%"<<(person.exhausted?" EXHAUSTED":"");
      if(person.diving)text<<" | DIVING breath "<<static_cast<int>(std::lround(person.breath*100))<<"%";
      else if(person.swimming)text<<(person.crawling?" | SWIMMING (crawl)":" | SWIMMING")<<(person.breath<0.99?" breath ":"")<<(person.breath<0.99?std::to_string(static_cast<int>(std::lround(person.breath*100)))+"%":"");
      else if(person.water_depth_meters>0.05)text<<" | wading "<<static_cast<int>(std::lround(person.water_depth_meters*100))<<" cm";
      if(person.on_rope)text<<" | ON THE ROPE "<<static_cast<int>(std::lround(person.rope_t_meters))<<" m down"<<(rope.branch_tree>=0?" (from a branch)":"");
      if(belt.selected!=terraflux::graphics::ToolKind::None) {
        text<<" | "<<terraflux::graphics::tool_kind_name(belt.selected);
        if(belt.selected==terraflux::graphics::ToolKind::Chainsaw)text<<" fuel "<<static_cast<int>(belt.chainsaw_fuel_seconds)/60<<':'<<std::setw(2)<<std::setfill('0')<<static_cast<int>(belt.chainsaw_fuel_seconds)%60<<std::setfill(' ')<<(belt.sawing?" SAWING":"");
        if(belt.selected==terraflux::graphics::ToolKind::Rope && rope.state!=terraflux::graphics::RopeState::Coiled)text<<' '<<terraflux::graphics::rope_state_name(rope.state);
      }
      if(held.kind!=terraflux::graphics::HeldKind::Nothing)text<<" | holding "<<terraflux::graphics::held_kind_name(held.kind)
          <<(held.kind==terraflux::graphics::HeldKind::Torch?" ("+std::to_string(static_cast<int>(std::lround(held.torch_seconds)))+" s)":"")
          <<(belt.selected!=terraflux::graphics::ToolKind::None?" (left hand)":"");
      if(!interact_hint.empty())text<<" | "<<interact_hint;
      if(person.crouched)text<<" | crouched";
      if(person.grip<0.6)text<<(person.grip<0.3?" | ICE":" | snow");
      if(buildings && person.grounded) {
        const auto building_state=[&](std::size_t i) {
          const auto& st=buildings->states()[i];
          if(st.destroyed)text<<(st.destroyed_by=="flood"?" (washed out)":st.destroyed_by=="fire"?" (burned down)":" (collapsed)");
          else {
            if(st.alight())text<<" ALIGHT";
            if(st.roof_fallen)text<<" (no roof)";
            if(st.flood_depth_meters>0.05)text<<" flooded "<<std::fixed<<std::setprecision(1)<<st.flood_depth_meters<<" m"<<std::defaultfloat;
            if(!st.alight() && !st.roof_fallen && st.integrity()<0.999)text<<" (damaged)";
          }
        };
        if(const auto room=buildings->inside_of(person.feet.x,person.feet.z)) {
          text<<" | inside the "<<terraflux::graphics::building_kind_name(buildings->buildings()[*room].kind)<<(buildings->doors()[*room].open>0.5?" (door open)":" (door closed)");
          building_state(*room);
        } else if(const auto door=buildings->entrance_near(person.feet.x,person.feet.z,4.0)) {
          text<<" | at the "<<terraflux::graphics::building_kind_name(buildings->buildings()[*door].kind)<<"'s door";
          building_state(*door);
        } else if(const auto on=buildings->building_at(person.feet.x,person.feet.z)) {
          if(buildings->states()[*on].destroyed){text<<" | on the "<<terraflux::graphics::building_kind_name(buildings->buildings()[*on].kind)<<"'s rubble";building_state(*on);}
        }
        else if(buildings->platform_height(person.feet.x,person.feet.z) && !(roads && roads->sample(person.feet.x,person.feet.z).on))text<<" | on a footpath";
      }
      if(const auto* zone=zone_at(person.feet.x,person.feet.z))text<<" | "<<zone->name<<" ("<<terraflux::graphics::zone_kind_name(zone->kind)<<")";
      if(roads && person.grounded) {
        const auto at=roads->sample(person.feet.x,person.feet.z,person.feet.y);
        if(at.on) {
          const auto& st=roads->states()[at.segment];
          text<<" | on the "<<(at.kind==terraflux::graphics::RoadKind::Bridge?"bridge (":"")<<terraflux::graphics::road_class_name(at.cls)<<(at.kind==terraflux::graphics::RoadKind::Bridge?")":"");
          if(terraflux::graphics::road_class_lanes(at.cls)>0)text<<", "<<terraflux::graphics::road_class_lanes(at.cls)<<" lanes";
          if(at.destroyed)text<<(at.kind==terraflux::graphics::RoadKind::Bridge?" (COLLAPSED)":" (destroyed: rubble)");
          else if(at.blocked)text<<" (blocked by a fallen tree)";
          else if(st.damage>0)text<<(at.kind==terraflux::graphics::RoadKind::Bridge?" (ALIGHT ":" (damaged ")<<static_cast<int>(std::lround(st.damage*100))<<"%)";
          if(std::abs(at.speed_factor-1.0)>0.005)text<<" x"<<std::fixed<<std::setprecision(2)<<at.speed_factor<<std::defaultfloat;
        }
      }
      text<<" | health "<<static_cast<int>(std::lround(vitals.health*100))<<"% heat "<<static_cast<int>(std::lround(vitals.heat*100))<<"%";
      if(vitals.wetness>0.3)text<<" wet";
      if(vitals.last_loss>0 && vitals.last_cause!=terraflux::graphics::DamageCause::None)text<<" ("<<terraflux::graphics::damage_cause_name(vitals.last_cause)<<")";
      return text.str();
    };
    const auto weather_title=[&]()->std::string {
      if(!sky)return walker_title();
      const auto& c=weather.config();
      if(!c.enabled)return " | O weather off";
      const auto sun=weather.sun();const auto wind=weather.wind();
      terraflux::graphics::CameraBasis probe;probe.eye={0,c.reference_elevation_meters,0};
      const auto now=weather.state(probe,view_mode==terraflux::graphics::CameraMode::FirstPerson?person.feet.y:scene_camera.target.y);
      char hour[16];std::snprintf(hour,sizeof hour,"%02d:%02d",static_cast<int>(sun.hour),static_cast<int>(std::fmod(sun.hour,1.0)*60));
      std::ostringstream text;
      text<<" | "<<cloud_type_name(c.clouds)<<" (C) | "<<hour<<' '<<daylight_phase_name(sun.phase)<<" (J/K) | wind "
          <<static_cast<int>(std::lround(wind.speed_mps))<<" m/s from "<<static_cast<int>(std::lround(std::fmod(wind.direction_degrees+180.0,360.0)))
          <<"\u00b0 (X) | "<<static_cast<int>(std::lround(now.temperature_celsius))<<"\u00b0C";
      if(now.precipitation_mm_h>0)text<<", "<<(now.snow?"snow ":"rain ")<<static_cast<int>(std::lround(now.precipitation_mm_h))<<" mm/h";
      if(now.strike.active)text<<", lightning";
      if(!lightning_message.empty() && engine_time<lightning_message_until)text<<" | "<<lightning_message;
      text<<" | O weather";
      return text.str()+walker_title();
    };
    const auto layer_title=[&] {
      return std::string(" | T terrain ")+(layer_visibility.terrain?"on":"off")+
             " | F fire "+(playback?(layer_visibility.fire?"on":"off"):"unavailable")+
             " | Tab "+(view_mode==terraflux::graphics::CameraMode::FirstPerson?"first person":"world view")+weather_title();
    };
    const auto style_packed=pack_terrain_style(terrain_style);
    const auto style_buffer=owned.create_buffer({BufferUsage::Uniform,sizeof(style_packed)},
                                                std::as_bytes(std::span(style_packed)));
    struct WaterGpu { BufferHandle vb,ib,style; PipelineHandle pipeline; std::uint32_t indices=0; };
    std::optional<WaterGpu> water;
    if(water_style) {
      const auto water_vertices=bytes_of(water_upload.vertices);
      const auto water_indices=bytes_of(water_upload.indices);
      const auto water_packed=pack_water_style(*water_style,{},0);
      water=WaterGpu{owned.create_buffer({BufferUsage::Vertex,water_vertices.size()},water_vertices),
                     owned.create_buffer({BufferUsage::Index,water_indices.size()},water_indices),
                     owned.create_buffer({BufferUsage::Uniform,sizeof(water_packed)},std::as_bytes(std::span(water_packed))),
                     {},static_cast<std::uint32_t>(water_upload.indices.size())};
      // Transparent surface over the opaque terrain: depth-tested, no depth writes.
      water->pipeline=owned.create_pipeline({ShaderProgramId::Water,water_shader_layout(),true,true,false});
    }

    // The flash-flood surface: muddy, opaque water lying on the channels, drawn
    // with the water shader after the lake; rebuilt when the wave changes.
    struct FloodGpu { BufferHandle vb,ib,style; PipelineHandle pipeline; std::uint32_t indices=0; };
    std::optional<FloodGpu> flood_gpu;
    terraflux::graphics::WaterStyle flood_water_style;
    flood_water_style.shallow_color={0.30,0.25,0.16};flood_water_style.deep_color={0.14,0.11,0.07};
    flood_water_style.clarity_meters=0.5;flood_water_style.absorption_meters=0.6;flood_water_style.wave_height_meters=0.05;
    flood_water_style.wave_length_meters=7.0;flood_water_style.wave_speed_mps=3.5;flood_water_style.reflectivity=0.6;
    flood_water_style.choppiness=1.0;flood_water_style.foam_depth_meters=0.25;flood_water_style.light_direction=terrain_style.light_direction;
    std::size_t flood_triangles=0;
    const auto rebuild_flood=[&] {
      const auto flood_mesh=flood.build_mesh(mesh,water_style?std::optional<double>(water_style->level_meters):std::nullopt);
      built_flash=flood.flash();
      flood_triangles=flood_mesh.indices.size()/3;
      if(flood_mesh.indices.empty()) {
        if(flood_gpu){owned.release_buffer(flood_gpu->vb);owned.release_buffer(flood_gpu->ib);flood_gpu->indices=0;}
        return;
      }
      auto upload=terraflux::graphics::prepare_water_upload(flood_mesh);
      const auto vertices=bytes_of(upload.vertices);
      const auto indices=bytes_of(upload.indices);
      const auto vb=owned.create_buffer({BufferUsage::Vertex,vertices.size()},vertices);
      const auto ib=owned.create_buffer({BufferUsage::Index,indices.size()},indices);
      if(flood_gpu) {
        if(flood_gpu->indices){owned.release_buffer(flood_gpu->vb);owned.release_buffer(flood_gpu->ib);}
        flood_gpu->vb=vb;flood_gpu->ib=ib;flood_gpu->indices=static_cast<std::uint32_t>(upload.indices.size());
      } else {
        const auto packed=pack_water_style(flood_water_style,{},0);
        flood_gpu=FloodGpu{vb,ib,owned.create_buffer({BufferUsage::Uniform,sizeof(packed)},std::as_bytes(std::span(packed))),
                           owned.create_pipeline({ShaderProgramId::Water,water_shader_layout(),true,true,false}),
                           static_cast<std::uint32_t>(upload.indices.size())};
      }
      if(options.smoke_test || options.profile) {
        const auto st=flood.stats(mesh,water_style?std::optional<double>(water_style->level_meters):std::nullopt);
        std::cout<<"FLASH_FLOOD_JSON={\"flash\":"<<flood.flash()<<",\"triangles\":"<<flood_triangles<<",\"wet_vertices\":"<<st.wet_vertices
                 <<",\"max_depth_m\":"<<st.max_depth_meters<<",\"max_current_mps\":"<<st.max_current_mps<<"}\n";
      }
    };
    std::optional<BufferHandle> fire_buffer;
    if (fire_field) {
      const auto fire_bytes = std::as_bytes(std::span(fire_field->states));
      fire_buffer = owned.create_buffer({BufferUsage::Storage, fire_bytes.size()}, fire_bytes);
    }
    // 3D vegetation: procedural or custom tree meshes and grass tufts, placed
    // from the material model, instanced, wind-swayed and swappable at run time.
    VegetationStyle vegetation_style;
    vegetation_style.seed=options.vegetation_seed;
    vegetation_style.light_direction=terrain_style.light_direction;
    if(water_style){vegetation_style.wind_direction_degrees=water_style->wave_direction_degrees;vegetation_style.wind_speed_mps=std::min(30.0,water_style->wave_speed_mps);}
    std::string vegetation_level=options.vegetation,tree_style=options.tree_style;
    const auto apply_vegetation_level=[&](const std::string& level) {
      vegetation_style.trees=level!="off";vegetation_style.grass=level=="medium" || level=="high";
      vegetation_style.tree_spacing_meters=level=="low"?18.0:level=="high"?8.0:12.0;
      vegetation_style.grass_spacing_meters=level=="high"?2.0:2.5;
      vegetation_style.grass_distance_meters=level=="high"?450.0:300.0;
      if(options.tree_spacing)vegetation_style.tree_spacing_meters=*options.tree_spacing;
      if(options.grass_spacing)vegetation_style.grass_spacing_meters=*options.grass_spacing;
      if(options.grass_distance)vegetation_style.grass_distance_meters=*options.grass_distance;
      if(options.tree_height)vegetation_style.tree_height_meters=*options.tree_height;
    };
    apply_vegetation_level(vegetation_level);
    std::optional<VegetationMesh> custom_tree;
    if(!options.tree_obj.empty()) {
      std::ifstream model_file(options.tree_obj);
      if(!model_file)throw std::runtime_error("cannot open tree model "+options.tree_obj);
      auto model=terraflux::assets::import_obj(model_file);
      terraflux::assets::MaterialLibrary materials;
      for(const auto& [name,located]:terraflux::assets::load_obj_materials(model,std::filesystem::path(options.tree_obj).parent_path()))
        materials.emplace(name,located.material);
      custom_tree=vegetation_mesh_from_model(model,materials,{0.2,0.4,0.17},std::filesystem::path(options.tree_obj).filename().string());
    }
    const auto tree_meshes_for=[&](const std::string& style)->std::vector<VegetationMesh> {
      TreeMeshOptions conifer;conifer.shape=TreeShape::Conifer;conifer.segments=6;conifer.layers=3;
      TreeMeshOptions broadleaf;broadleaf.shape=TreeShape::Broadleaf;broadleaf.segments=7;broadleaf.layers=3;
      broadleaf.crown_width=0.8;broadleaf.foliage_color={0.16,0.40,0.17};
      if(style=="conifer")return {make_tree_mesh(conifer)};
      if(style=="broadleaf")return {make_tree_mesh(broadleaf)};
      if(style=="custom"){if(!custom_tree)throw std::invalid_argument("custom tree style needs --tree-obj");return {*custom_tree};}
      return {make_tree_mesh(conifer),make_tree_mesh(broadleaf)};
    };
    // The ground under 3D trees shows litter rather than a second painted canopy.
    const auto canopy_for=[&](bool trees){return trees?0.35:1.0;};
    const auto refresh_terrain_style=[&] {
      const auto packed=pack_terrain_style(terrain_style);
      device.update_buffer(style_buffer,0,std::as_bytes(std::span(packed)));
    };
    terrain_style.canopy_on_ground=canopy_for(vegetation_style.trees);
    refresh_terrain_style();
    // Flooding: heavy rain raises the standing water. The surface is rebuilt at the
    // raised level (new shoreline triangles) between passes; the floor follows.
    const double base_water_level=water_style?water_style->level_meters:0.0;
    double built_flood_rise=0.0;
    const auto rebuild_water=[&](double rise) {
      if(!water || !water_style)return;
      const double level=base_water_level+rise;
      const auto water_mesh=terraflux::terrain::build_water_mesh(mesh,level);
      if(water_mesh.indices.empty())return;
      auto upload=terraflux::graphics::prepare_water_upload(water_mesh);
      const auto vertices=bytes_of(upload.vertices);
      const auto indices=bytes_of(upload.indices);
      const auto vb=owned.create_buffer({BufferUsage::Vertex,vertices.size()},vertices);   // admit before retiring
      const auto ib=owned.create_buffer({BufferUsage::Index,indices.size()},indices);
      owned.release_buffer(water->vb);owned.release_buffer(water->ib);
      water->vb=vb;water->ib=ib;water->indices=static_cast<std::uint32_t>(upload.indices.size());
      water_upload=std::move(upload);
      water_style->level_meters=level;
      terrain_style.submerged_level_meters=level;
      refresh_terrain_style();
      built_flood_rise=rise;
      if(options.smoke_test || options.profile)
        std::cout<<"FLOOD_JSON={\"rise_m\":"<<rise<<",\"level_m\":"<<level<<",\"triangles\":"<<water_mesh.indices.size()/3<<"}\n";
    };
    bool fire_emitters_dirty=true;
    std::size_t playback_frame = 0;
    std::size_t uploaded_frame = static_cast<std::size_t>(-1);
    std::unique_ptr<VegetationRenderer> vegetation;
    std::optional<BufferHandle> vegetation_style_buffer;
    std::optional<PipelineHandle> vegetation_pipeline;
    const auto report_vegetation=[&](const char* event) {
      if(!(options.smoke_test || options.profile || options.residency_report || options.benchmark_frames))return;
      const auto& placement=vegetation?vegetation->placement():VegetationPlacement{};
      std::cout<<"VEGETATION_JSON={\"event\":\""<<event<<"\",\"level\":\""<<vegetation_level<<"\",\"tree_style\":\""<<tree_style
               <<"\",\"trees\":"<<placement.trees.size()<<",\"conifer_like\":"<<placement.trees_per_kind[0]
               <<",\"broadleaf_like\":"<<placement.trees_per_kind[1]<<",\"grass\":"<<placement.grass.size()
               <<",\"chunks\":"<<placement.chunks.size()<<",\"tree_spacing_m\":"<<vegetation_style.tree_spacing_meters
               <<",\"grass_distance_m\":"<<vegetation_style.grass_distance_meters
               <<",\"grass_capacity\":"<<(vegetation?vegetation->grass_capacity():0)
               <<",\"cleared_for_roads\":"<<vegetation_cleared_for_roads
               <<",\"geometry_bytes\":"<<(vegetation?vegetation->geometry_bytes():0)<<"}\n";
    };
    std::unique_ptr<TreeFireModel> tree_fire;  // created after the fire effects below
    std::uint64_t tree_fire_obstacles_seen=0;
    const auto rebuild_tree_obstacles=[&] {
      if(vegetation && !options.no_collision) {
        auto standing=terraflux::graphics::tree_obstacles(vegetation->placement(),vegetation->tree_meshes());
        // Fallen trunks lie across the ground as chains of low obstacles.
        if(tree_fire && tree_fire->size()==standing.size()){standing=tree_fire->obstacles(standing);tree_fire_obstacles_seen=tree_fire->obstacle_version();}
        tree_field=terraflux::graphics::ObstacleField(std::move(standing));
      } else tree_field=terraflux::graphics::ObstacleField{};
      if(options.smoke_test || options.profile || options.residency_report) {
        double largest=0;
        for(const auto& o:tree_field.obstacles())largest=std::max(largest,o.radius);
        std::cout<<"COLLISION_JSON={\"trees\":"<<tree_field.size()<<",\"largest_trunk_radius_m\":"<<largest
                 <<",\"body_radius_m\":"<<walk_env.body.radius<<",\"step_height_m\":"<<walk_env.body.step_height
                 <<",\"max_slope_deg\":"<<walk_env.body.max_slope_degrees<<",\"enabled\":"<<(options.no_collision?"false":"true")<<"}\n";
      }
    };
    const auto rebuild_vegetation=[&] {
      terrain_style.canopy_on_ground=canopy_for(vegetation_style.trees);
      refresh_terrain_style();
      fire_emitters_dirty=true;
      if(!vegetation_style.trees && !vegetation_style.grass){vegetation.reset();rebuild_tree_obstacles();return;}
      auto placement=place_vegetation(mesh,terrain_style,vegetation_style);
      // Nothing grows on the carriageways: the plants standing on them are cleared.
      vegetation_cleared_for_roads=roads?terraflux::graphics::clear_vegetation_along(placement,*roads):0;
      if(buildings)vegetation_cleared_for_roads+=terraflux::graphics::clear_vegetation_for_buildings(placement,*buildings);
      if(city)vegetation_cleared_for_roads+=terraflux::graphics::clear_vegetation_in(placement,city->clearings);
      if(vegetation)vegetation->set_placement(std::move(placement),vegetation_style.grass_distance_meters,vegetation_style.grass_spacing_meters);
      else vegetation=std::make_unique<VegetationRenderer>(device,std::move(placement),tree_meshes_for(tree_style),make_grass_mesh(3),
                                                           vegetation_style.grass_distance_meters,vegetation_style.grass_spacing_meters);
      rebuild_tree_obstacles();
    };
    {
      const auto packed=pack_vegetation_style(vegetation_style,{},0);
      vegetation_style_buffer=owned.create_buffer({BufferUsage::Uniform,sizeof(packed)},std::as_bytes(std::span(packed)));
      vegetation_pipeline=owned.create_pipeline({ShaderProgramId::Vegetation,vegetation_shader_layout(),true});
      rebuild_vegetation();
      report_vegetation("initial");
    }
    report_weather("initial");
    // Burn states for plants come from the same fire field the ground uses; a
    // one-record buffer stands in when there is no replay.
    VegetationFireGrid vegetation_fire;
    if(fire_field) {
      vegetation_fire.present=true;vegetation_fire.cell_size_meters=cell_size;
      vegetation_fire.width=fire_field->width;vegetation_fire.height=fire_field->height;
      vegetation_fire.origin_offset_x=static_cast<std::int64_t>(mesh.origin_grid_x)-fire_field->min_x;
      vegetation_fire.origin_offset_y=static_cast<std::int64_t>(mesh.origin_grid_y)-fire_field->min_y;
    }
    const std::uint32_t no_fire_state=0;
    const auto no_fire_buffer=owned.create_buffer({BufferUsage::Storage,sizeof(no_fire_state)},std::as_bytes(std::span(&no_fire_state,1)));
    const BufferHandle vegetation_fire_buffer=fire_buffer?*fire_buffer:no_fire_buffer;

    // Animated flames and smoke for the replay's burning and burned cells.
    FireStyle fire_style;
    fire_style.flame_scale=options.flame_scale;fire_style.smoke_scale=options.smoke_scale;
    fire_style.wind_direction_degrees=vegetation_style.wind_direction_degrees;fire_style.wind_speed_mps=vegetation_style.wind_speed_mps;
    fire_style.light_direction=terrain_style.light_direction;fire_style.seed=options.vegetation_seed;
    std::unique_ptr<FireEffectsRenderer> fire_effects;
    std::optional<BufferHandle> fire_style_buffer;
    std::optional<PipelineHandle> fire_pipeline;
    // Flames are needed without a replay too: lightning lights trees in a storm.
    if(fire_field || (vegetation && options.tree_fire!="off" && !vegetation->placement().trees.empty())) {
      const auto packed=pack_fire_style(fire_style,{},0);
      fire_style_buffer=owned.create_buffer({BufferUsage::Uniform,sizeof(packed)},std::as_bytes(std::span(packed)));
      fire_pipeline=owned.create_pipeline({ShaderProgramId::Fire,fire_shader_layout(),true,true,false});
      const std::size_t capacity=std::clamp<std::size_t>(grid.cells.size()*static_cast<std::size_t>(fire_style.flames_per_cell+3),4096,200'000);
      fire_effects=std::make_unique<FireEffectsRenderer>(device,capacity);
    }
    // Tree-level fire: every tree burns, drops branches, falls and (in model
    // mode) lights its neighbours, driven by the replay's cells and clock.
    TreeFireStyle tree_fire_style;
    tree_fire_style.spread=options.tree_fire=="model";
    tree_fire_style.gravity_mps2=walk_env.physics.gravity_mps2;
    tree_fire_style.seed=options.vegetation_seed;
    double replay_seconds_seen=-1.0;
    bool tree_fire_synced=false; // the first sync after (re)creation settles cells burned before it
    std::int64_t last_strike_slot=-1;std::size_t lightning_strikes=0;
    const auto tree_fire_sync=[&](bool settle_burned) {
      if(!tree_fire || !fire_field)return;
      tree_fire->ignite_from_field(*fire_field,mesh.origin_grid_x,mesh.origin_grid_y,cell_size,settle_burned || !tree_fire_synced);
      tree_fire_synced=true;
    };
    const auto report_tree_fire=[&](const char* event) {
      if(!tree_fire || !(options.smoke_test || options.profile || options.residency_report))return;
      const auto st=tree_fire->stats();
      std::cout<<"TREE_FIRE_JSON={\"event\":\""<<event<<"\",\"mode\":\""<<options.tree_fire<<"\",\"seconds\":"<<st.seconds
               <<",\"trees\":"<<st.trees<<",\"unburned\":"<<st.unburned<<",\"ignited\":"<<st.ignited<<",\"burning\":"<<st.burning
               <<",\"falling\":"<<st.falling<<",\"fallen\":"<<st.fallen<<",\"burned_out\":"<<st.burned_out
               <<",\"leaning\":"<<st.leaning<<",\"grounded\":"<<st.grounded<<",\"ignited_by_cell\":"<<st.ignited_by_cell
               <<",\"ignited_by_contact\":"<<st.ignited_by_contact<<",\"ignited_by_radiation\":"<<st.ignited_by_radiation
               <<",\"ignited_by_ember\":"<<st.ignited_by_ember<<",\"ignited_by_collapse\":"<<st.ignited_by_collapse
               <<",\"branches_dropped\":"<<st.branches_dropped<<",\"branches_burning\":"<<st.branches_burning
               <<",\"mass_kg\":["<<st.lightest_kg<<","<<st.heaviest_kg<<"],\"breaking_moment_nm\":["<<st.weakest_nm<<","<<st.strongest_nm<<"]}\n";
    };
    const auto rebuild_tree_fire=[&] {
      tree_fire.reset();
      if(!vegetation || options.tree_fire=="off" || vegetation->placement().trees.empty())return;
      const auto moisture=[&](double x,double z){const auto g=ground_sampler.sample(x,z);return g?std::clamp(g->fuel_moisture,0.0,1.0):0.0;};
      tree_fire=std::make_unique<TreeFireModel>(vegetation->placement(),vegetation->tree_meshes(),tree_fire_style,standing_height,moisture);
      tree_fire_synced=false;
      if(uploaded_frame!=static_cast<std::size_t>(-1))tree_fire_sync(true); // otherwise the first frame upload syncs
      tree_fire->apply_to_placement(vegetation->placement_mutable());
      rebuild_tree_obstacles();
      fire_emitters_dirty=true;
      report_tree_fire("initial");
    };
    const auto rebuild_fire_emitters=[&] {
      if(!fire_effects || !fire_field)return;
      // With the tree model the crown flames come from the model, not the cells.
      fire_effects->set_emitters(build_fire_emitters(*fire_field,mesh.origin_grid_x,mesh.origin_grid_y,cell_size,ground_sampler,
                                                     terrain_style,(vegetation && !tree_fire)?&vegetation->placement():nullptr,fire_style));
      fire_emitters_dirty=false;
      if(options.smoke_test || options.profile || options.residency_report) {
        const auto& e=fire_effects->emitters();
        std::cout<<"FIRE_EFFECTS_JSON={\"active_cells\":"<<e.active_cells<<",\"burned_cells\":"<<e.burned_cells
                 <<",\"flames\":"<<e.flames.size()<<",\"crown_flames\":"<<e.crown_flames<<",\"smoke\":"<<e.smoke.size()
                 <<",\"fuelless_samples\":"<<e.fuelless_samples<<",\"capacity\":"<<fire_effects->capacity()<<"}\n";
      }
    };
    rebuild_tree_fire();
    // Things to pick up: stones on the rock and scree, branches broken off
    // trees or dropped, and the fire's own burning branches. Carried in the
    // right hand, dropped at the feet or thrown under gravity.
    terraflux::graphics::StoneStyle stone_style;
    stone_style.seed=options.vegetation_seed;
    std::vector<terraflux::graphics::Stone> stones=options.no_stones?std::vector<terraflux::graphics::Stone>{}:
        terraflux::graphics::place_stones(mesh,terrain_style,ground_sampler,stone_style,water_style?std::optional<double>(water_style->level_meters):std::nullopt);
    std::vector<terraflux::graphics::DroppedBranch> dropped_branches;
    std::vector<terraflux::graphics::Projectile> projectiles;
    terraflux::graphics::PropRenderer props(device,192);
    std::size_t prop_draws=0;
    // The roads drawn: rebuilt whenever the network's geometry changes (a
    // trunk across a road, a damaged surface, a collapsed span).
    std::unique_ptr<terraflux::graphics::RoadRenderer> road_renderer;
    std::uint64_t roads_uploaded_version=0;
    std::size_t road_draws=0;
    bool roads_visible=true;
    const auto report_roads=[&](const char* event) {
      if(!roads || !(options.smoke_test || options.profile || options.residency_report))return;
      const auto st=roads->stats();
      std::cout<<"ROADS_JSON={\"event\":\""<<event<<"\",\"source\":\""<<roads_choice<<"\",\"nodes\":"<<st.nodes<<",\"roads\":"<<st.roads<<",\"bridges\":"<<st.bridges
               <<",\"road_m\":"<<st.road_meters<<",\"bridge_m\":"<<st.bridge_meters<<",\"longest_bridge_m\":"<<st.longest_bridge_meters
               <<",\"highest_deck_m\":"<<st.highest_deck_meters<<",\"streets\":"<<st.streets<<",\"avenues\":"<<st.avenues<<",\"highways\":"<<st.highways<<",\"ramps\":"<<st.ramps
               <<",\"runways\":"<<st.runways<<",\"taxiways\":"<<st.taxiways<<",\"aprons\":"<<st.aprons<<",\"highway_m\":"<<st.highway_meters<<",\"runway_m\":"<<st.runway_meters
               <<",\"curve_joints\":"<<st.curve_joints<<",\"blocked\":"<<st.blocked<<",\"blockages\":"<<st.blockages<<",\"damaged\":"<<st.damaged
               <<",\"destroyed\":"<<st.destroyed<<",\"parts\":"<<(road_renderer?road_renderer->parts():0)<<",\"triangles\":"<<(road_renderer?road_renderer->triangles():0)
               <<",\"obstacles\":"<<road_field.size()<<",\"road_speed_factor\":"<<road_style.road_speed_factor<<",\"bridge_speed_factor\":"<<road_style.bridge_speed_factor
               <<",\"cleared_plants\":"<<vegetation_cleared_for_roads<<"}\n";
    };
    const auto upload_roads=[&] {
      if(!roads || !road_renderer)return;
      road_renderer->set_parts(roads->build_meshes());
      roads_uploaded_version=roads->version();
    };
    if(roads){road_renderer=std::make_unique<terraflux::graphics::RoadRenderer>(device);upload_roads();report_roads("initial");}
    std::unique_ptr<terraflux::graphics::MeshPartRenderer> building_renderer,door_renderer;  // the doors move: their own parts
    std::size_t building_draws=0;
    bool buildings_visible=true;
    std::uint64_t doors_uploaded_version=~std::uint64_t{0},buildings_uploaded_version=~std::uint64_t{0};
    const auto upload_doors=[&] {
      if(!buildings || !door_renderer)return;
      door_renderer->set_parts(buildings->build_door_meshes());
      doors_uploaded_version=buildings->door_version();
    };
    const auto upload_buildings=[&] {
      if(!buildings || !building_renderer)return;
      building_renderer->set_parts(buildings->build_meshes());
      buildings_uploaded_version=buildings->version();
    };
    if(buildings) {
      building_renderer=std::make_unique<terraflux::graphics::MeshPartRenderer>(device);
      upload_buildings();
      door_renderer=std::make_unique<terraflux::graphics::MeshPartRenderer>(device);
      upload_doors();
      if(options.smoke_test || options.profile || options.residency_report) {
        const auto st=buildings->stats();
        std::cout<<"BUILDINGS_JSON={\"buildings\":"<<st.buildings<<",\"bungalows\":"<<st.bungalows<<",\"houses\":"<<st.houses<<",\"hospitals\":"<<st.hospitals
                 <<",\"skyscrapers\":"<<st.skyscrapers<<",\"tallest_m\":"<<st.tallest_meters<<",\"most_storeys\":"<<st.most_storeys<<",\"floor_area_m2\":"<<st.floor_area_m2
                 <<",\"interiors\":"<<st.interiors<<",\"parts\":"<<building_renderer->parts()<<",\"triangles\":"<<building_renderer->triangles()<<",\"door_parts\":"<<door_renderer->parts()
                 <<",\"obstacles\":"<<building_field.size()<<",\"door_obstacles\":"<<door_field.size()<<",\"sites\":[";
        for(std::size_t i=0;i<buildings->size();++i) {
          const auto& b=buildings->buildings()[i];
          std::cout<<(i?",":"")<<"{\"kind\":\""<<terraflux::graphics::building_kind_name(b.kind)<<"\",\"segment\":"<<b.site.segment<<",\"along_m\":"<<b.site.along_meters<<",\"side\":"<<b.site.side
                   <<",\"storeys\":"<<b.storeys<<",\"height_m\":"<<b.height_meters<<",\"floor_m\":"<<b.floor_meters<<",\"door\":["<<b.door.x<<","<<b.door.y<<"]}";
        }
        std::cout<<"]}\n";
      }
    }
    // What the network reports: a trunk across a road, a surface damaged, a deck
    // alight, a span collapsed or washed out — and a deck that gives way under the
    // walker hurts it (the fall that follows is the walker's own).
    std::size_t road_events_handled=0;
    const auto handle_road_events=[&] {
      if(!roads)return;
      const auto& events=roads->events();
      for(;road_events_handled<events.size();++road_events_handled) {
        const auto& e=events[road_events_handled];
        const bool bridge=roads->def().segments[e.segment].kind==terraflux::graphics::RoadKind::Bridge;
        std::string what;
        using Kind=terraflux::graphics::RoadEvent::Kind;
        switch(e.kind) {
          case Kind::Blocked:what=std::string("A tree fell across the ")+(bridge?"bridge":"road");break;
          case Kind::Damaged:what="A burning tree is damaging the road";break;
          case Kind::Alight:what="THE BRIDGE IS ALIGHT";break;
          case Kind::Destroyed:if(bridge)continue;what="The road has burned through: rubble";break;
          case Kind::Collapsed:what="THE BRIDGE COLLAPSED";break;
          case Kind::WashedOut:what="THE BRIDGE WAS WASHED OUT by the flood";break;
        }
        if((e.kind==Kind::Collapsed || e.kind==Kind::WashedOut) && view_mode==terraflux::graphics::CameraMode::FirstPerson) {
          const auto at=roads->sample(person.feet.x,person.feet.z,person.feet.y);
          const auto ground=ground_height(person.feet.x,person.feet.z);
          if(at.on && at.segment==e.segment && at.deck_missing && person.grounded && ground && person.feet.y-*ground>walk_env.body.step_height) {
            pending_collapse=std::max(pending_collapse,road_style.collapse_damage);
            terraflux::graphics::knock_down(person,2.0,0.9);
            terraflux::graphics::shake(person,0.12);
            what+=" UNDER YOU";
          }
        }
        lightning_message=what;lightning_message_until=engine_time+6.0;
        std::cout<<what<<'\n';
      }
    };
    // The network's own time: burning trunks damage what they lie on, a
    // flooded channel scours the piers.
    const auto advance_roads=[&](double dt) {
      if(!roads || dt<=0)return;
      roads->advance(dt,[&](std::uint32_t tree)->double{ return tree_fire && tree<tree_fire->size()?tree_fire->states()[tree].intensity:0.0; },walk_env.water);
      road_events_handled=0;  // advance() starts a new list
      handle_road_events();
    };
    // What the buildings report: alight, burned out, damaged (and by what), a
    // roof fallen in, a collapse, a washout, a strike, a trunk landed on one —
    // and a roof or a building coming down on a walker inside hurts it.
    std::size_t building_events_handled=0;
    const auto handle_building_events=[&] {
      if(!buildings)return;
      const auto& events=buildings->events();
      for(;building_events_handled<events.size();++building_events_handled) {
        const auto& e=events[building_events_handled];
        const std::string kind=terraflux::graphics::building_kind_name(buildings->buildings()[e.building].kind);
        std::string KIND=kind;for(auto& c:KIND)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const std::string element=terraflux::graphics::building_element_name(e.element);
        const bool inside_it=view_mode==terraflux::graphics::CameraMode::FirstPerson && buildings->building_at(person.feet.x,person.feet.z)==e.building && person.grounded;
        std::string what;
        using Kind=terraflux::graphics::BuildingEvent::Kind;
        switch(e.kind) {
          case Kind::Alight:what="THE "+KIND+"'S "+element+(e.element==terraflux::graphics::BuildingElement::Walls || e.element==terraflux::graphics::BuildingElement::Windows?" ARE":" IS")+" ALIGHT ("+e.cause+")";break;
          case Kind::BurnedOut:what="The "+kind+"'s "+element+" burned through";break;
          case Kind::Damaged:what="The "+kind+"'s "+element+(e.cause=="heat"?(e.element==terraflux::graphics::BuildingElement::Windows?" burst in the heat":" cracked in the heat"):
                                   e.cause=="impact"?" smashed":e.cause=="flood"?" soaked through":e.cause=="wind"?" torn by the wind":e.cause=="lightning"?" scorched by the strike":" damaged ("+e.cause+")");break;
          case Kind::RoofFell:what="THE "+KIND+"'S ROOF FELL IN";break;
          case Kind::Collapsed:what="THE "+KIND+" COLLAPSED ("+e.cause+")";break;
          case Kind::WashedOut:what="THE "+KIND+" WAS WASHED OUT by the flood";break;
          case Kind::Extinguished:what="The flood put out the "+kind+"'s "+element;break;
          case Kind::Struck:what="LIGHTNING STRUCK THE "+KIND;break;
          case Kind::Hit:what="A tree fell on the "+kind;break;
          case Kind::Flooded:what="The "+kind+" is flooded";break;
        }
        if(inside_it && (e.kind==Kind::RoofFell || e.kind==Kind::Collapsed || e.kind==Kind::WashedOut)) {
          pending_collapse=std::max(pending_collapse,e.kind==Kind::RoofFell?building_style.roof_fall_damage:building_style.collapse_damage);
          terraflux::graphics::knock_down(person,e.kind==Kind::RoofFell?2.5:4.0,1.0);
          terraflux::graphics::shake(person,e.kind==Kind::RoofFell?0.08:0.14);
          what+=" ON YOU";
        }
        lightning_message=what;lightning_message_until=engine_time+6.0;
        std::cout<<what<<'\n';
      }
    };
    // The buildings' own time: the doors swing, the fire beside them (and on
    // them) heats their elements, the flood soaks and scours them, the wind
    // tears at them — and a blaze lights the trees beside it.
    std::vector<double> building_radiant_seconds(buildings?buildings->size():0,0.0);
    const auto advance_buildings=[&](double dt) {
      if(!buildings || dt<=0)return;
      terraflux::graphics::BuildingHazards hazards;
      if(tree_fire) {
        hazards.heat=[&](double x,double z){return tree_fire->exposure_at(x,z,fire_style.wind_direction_degrees,12.0,0.1).heat;};  // heat only: the smoke is not needed here
        hazards.log_intensity=[&](std::uint32_t tree)->double{return tree<tree_fire->size()?tree_fire->states()[tree].intensity:0.0;};
      }
      hazards.water=walk_env.water;
      hazards.wind_speed_mps=weather.config().enabled?std::clamp(weather.wind().speed_mps,0.0,150.0):0.0;
      hazards.rain_mm_h=last_rain_mm_h;
      buildings->advance(dt,hazards);
      building_events_handled=0;  // advance() starts a new list
      handle_building_events();
      if(tree_fire && vegetation) {
        const auto& trees=vegetation->placement().trees;
        for(std::size_t i=0;i<buildings->size();++i) {
          const double intensity=buildings->states()[i].intensity();
          if(intensity<=0){building_radiant_seconds[i]=0;continue;}
          building_radiant_seconds[i]+=intensity*dt;
          if(building_radiant_seconds[i]<3.0)continue;  // every three seconds of a full blaze, the nearest tree in reach
          building_radiant_seconds[i]=0;
          const auto& b=buildings->buildings()[i];
          std::optional<std::size_t> pick;double best=8.0;
          for(std::size_t t=0;t<trees.size() && t<tree_fire->size();++t) {
            if(tree_fire->states()[t].stage!=TreeFireStage::Unburned || !tree_fire->standing(static_cast<std::uint32_t>(t)))continue;
            const terraflux::math::Vec2 p{trees[t].position[0],trees[t].position[2]};
            const auto [a,bb]=std::pair{(p-b.centre).dot(b.along),(p-b.centre).dot(b.facing)};
            const double d=std::hypot(std::max(0.0,std::abs(a)-0.5*b.spec.width_meters),std::max(0.0,std::abs(bb)-0.5*b.spec.depth_meters));
            if(d<best){best=d;pick=t;}
          }
          if(pick)tree_fire->ignite(static_cast<std::uint32_t>(*pick),IgnitionSource::Radiation);
        }
      }
    };
    // Trunks the tree model has just grounded lie across whatever road they
    // reach, and land on whatever building they reach.
    const auto logs_on_roads=[&] {
      if(!tree_fire || !vegetation)return;
      for(const auto& e:tree_fire->events()) {
        if(e.kind!=TreeFireEvent::Kind::Grounded)continue;
        const auto& st=tree_fire->states()[e.tree];
        const auto& b=tree_fire->bodies()[e.tree];
        const auto& t=vegetation->placement().trees[e.tree];
        if(roads)roads->log_landed(e.tree,{t.position[0],t.position[2]},st.fall_direction,b.height_meters,st.intensity>0,b.mass_kg);
        if(buildings)buildings->log_landed(e.tree,{t.position[0],t.position[2]},st.fall_direction,b.height_meters,st.intensity>0,b.mass_kg);
      }
      handle_road_events();
      handle_building_events();
    };
    terraflux::math::Vec3 held_world_position;  // where the held thing is (the torch flame)
    const auto rebuild_stone_obstacles=[&]{ stone_field=options.no_collision?terraflux::graphics::ObstacleField{}:terraflux::graphics::ObstacleField(terraflux::graphics::stone_obstacles(stones)); };
    rebuild_stone_obstacles();
    if(options.hold=="stone"){held.kind=terraflux::graphics::HeldKind::Stone;held.size=0.12;held.stone_size={0.24,0.18,0.22};held.source=0;if(!stones.empty()){stones[0].present=false;rebuild_stone_obstacles();}}
    else if(options.hold=="branch"){held.kind=terraflux::graphics::HeldKind::Branch;held.size=0.8;}
    else if(options.hold=="torch"){held.kind=terraflux::graphics::HeldKind::Torch;held.size=0.8;held.torch_seconds=terraflux::graphics::kTorchSeconds;}
    if(options.smoke_test || options.profile || options.residency_report)
      std::cout<<"STONES_JSON={\"stones\":"<<stones.size()<<",\"spacing_m\":"<<stone_style.spacing_meters<<"}\n";
    // What E would do right now, and doing it.
    const auto look_forward=[&](){return terraflux::graphics::look_direction(person.yaw,person.pitch);};
    // The nearest standing tree within arm's reach of the feet (by the placement, not the
    // obstacle field, whose fallen logs shift the indices).
    const auto tree_in_reach=[&](double reach)->std::optional<std::size_t> {
      if(!vegetation)return std::nullopt;
      const auto& trees=vegetation->placement().trees;
      std::optional<std::size_t> best;double best_d=reach;
      for(std::size_t i=0;i<trees.size();++i) {
        if(tree_fire && i<tree_fire->size() && (tree_fire->states()[i].fall_angle>0.3 || tree_fire->states()[i].stage==TreeFireStage::Falling))continue;
        const double r=tree_fire && i<tree_fire->size()?tree_fire->bodies()[i].trunk_radius_meters:0.4;
        const double d=std::hypot(trees[i].position[0]-person.feet.x,trees[i].position[2]-person.feet.z)-r;
        if(d<best_d){best_d=d;best=i;}
      }
      return best;
    };
    const auto fire_heat_at_hand=[&]()->double {
      double heat=0.0;
      if(tree_fire)heat=tree_fire->exposure_at(person.feet.x,person.feet.z,fire_style.wind_direction_degrees).heat;
      if(buildings)heat=std::max(heat,buildings->exposure_at(person.feet.x,person.feet.z).heat);
      return heat;
    };
    // The building element a torch in the hand would light: the room the
    // walker stands in, or the walls beside it (at the door, E opens the door
    // instead: a fire is set from the side of the house) — when it burns.
    const auto torch_target=[&]()->std::optional<std::pair<std::size_t,terraflux::graphics::BuildingElement>> {
      if(!buildings)return std::nullopt;
      using E=terraflux::graphics::BuildingElement;
      std::optional<std::pair<std::size_t,E>> target;
      if(const auto room=buildings->inside_of(person.feet.x,person.feet.z))target={*room,E::Interior};
      else if(buildings->entrance_near(person.feet.x,person.feet.z,2.0))return std::nullopt;
      else if(const auto beside=buildings->building_at(person.feet.x,person.feet.z,1.5))target={*beside,E::Walls};
      if(!target)return std::nullopt;
      const auto& st=buildings->states()[target->first];
      const auto& el=st.element(target->second);
      if(st.destroyed || !el.present || el.alight || el.damage>=1.0 || !terraflux::graphics::material_hazard(el.material).flammable)return std::nullopt;
      return target;
    };
    const auto interact_options=[&]()->std::string {
      const auto eye=person.eye(),fwd=look_forward();
      if(const auto door=door_in_reach()) {
        const auto& state=buildings->doors()[*door];
        const bool open=state.opening || state.open>=1.0;
        return std::string(open?"E: close the door":"E: open the door")+(held.kind!=terraflux::graphics::HeldKind::Nothing?" | Q: throw":"");
      }
      if(held.kind==terraflux::graphics::HeldKind::Nothing) {
        if(terraflux::graphics::stone_in_reach(stones,eye,fwd,2.3,60.0))return "E: pick up the stone";
        if(tree_fire && tree_fire->take_branch(person.feet.x,person.feet.z,0.0))return ""; // (never taken with radius 0)
        bool burning_near=false;
        if(tree_fire)for(const auto& b:tree_fire->branches())if(b.landed && std::hypot(b.position.x-person.feet.x,b.position.z-person.feet.z)<1.8)burning_near=true;
        if(burning_near)return "E: take the burning branch";
        if(terraflux::graphics::branch_in_reach(dropped_branches,eye,fwd,2.3,60.0))return "E: pick up the branch";
        if(tree_in_reach(1.4))return "E: break off a branch";
        return "";
      }
      if(held.kind==terraflux::graphics::HeldKind::Branch) {
        if(fire_heat_at_hand()>=0.3)return "E: light the branch | Q: throw";
        return "E: drop the branch | Q: throw";
      }
      if(held.kind==terraflux::graphics::HeldKind::Torch) {
        const auto trunk=tree_in_reach(1.4);
        if(trunk && tree_fire && *trunk<tree_fire->size() && tree_fire->states()[*trunk].stage==TreeFireStage::Unburned)return "E: set the tree alight | Q: throw";
        if(const auto target=torch_target())return std::string("E: set the ")+terraflux::graphics::building_kind_name(buildings->buildings()[target->first].kind)+"'s "+terraflux::graphics::building_element_name(target->second)+" alight | Q: throw";
        return "E: put the torch down | Q: throw";
      }
      return "E: drop the stone | Q: throw";
    };
    const auto interact=[&]()->std::string {
      const auto eye=person.eye(),fwd=look_forward();
      using terraflux::graphics::HeldKind;
      if(const auto door=door_in_reach()) {
        const bool was_open=buildings->doors()[*door].opening || buildings->doors()[*door].open>=1.0;
        buildings->toggle_door(*door);
        return was_open?"closed the door":"opened the door";
      }
      if(held.kind==HeldKind::Nothing) {
        if(const auto i=terraflux::graphics::stone_in_reach(stones,eye,fwd,2.3,60.0)) {
          auto& st=stones[*i];st.present=false;
          held.kind=HeldKind::Stone;held.size=st.radius;held.stone_size=st.size;held.source=static_cast<std::uint32_t>(*i);
          rebuild_stone_obstacles();
          return "picked up a stone";
        }
        if(tree_fire)if(const auto left=tree_fire->take_branch(person.feet.x,person.feet.z,1.8)){held.kind=HeldKind::Torch;held.size=0.8;held.torch_seconds=std::max(10.0,*left);return "took a burning branch";}
        if(const auto i=terraflux::graphics::branch_in_reach(dropped_branches,eye,fwd,2.3,60.0)) {
          held.kind=HeldKind::Branch;held.size=dropped_branches[*i].length;
          dropped_branches.erase(dropped_branches.begin()+static_cast<std::ptrdiff_t>(*i));
          return "picked up a branch";
        }
        if(tree_in_reach(1.4)){held.kind=HeldKind::Branch;held.size=0.8;return "broke off a branch";}
        return "";
      }
      if(held.kind==HeldKind::Branch && fire_heat_at_hand()>=0.3){held.kind=HeldKind::Torch;held.torch_seconds=terraflux::graphics::kTorchSeconds;return "lit the branch";}
      if(held.kind==HeldKind::Torch) {
        const auto trunk=tree_in_reach(1.4);
        if(trunk && tree_fire && *trunk<tree_fire->size() && tree_fire->states()[*trunk].stage==TreeFireStage::Unburned) {
          tree_fire->ignite(static_cast<std::uint32_t>(*trunk),IgnitionSource::Ember);
          return "set the tree alight";
        }
        if(const auto target=torch_target()) {
          if(buildings->ignite(target->first,target->second,"torch")){handle_building_events();return std::string("set the ")+terraflux::graphics::building_kind_name(buildings->buildings()[target->first].kind)+"'s "+terraflux::graphics::building_element_name(target->second)+" alight";}
        }
      }
      // Drop at the feet.
      const auto g=ground_height(person.feet.x,person.feet.z);
      const terraflux::math::Vec3 at{person.feet.x+fwd.x*0.6,g?*g:person.feet.y,person.feet.z+fwd.z*0.6};
      if(held.kind==HeldKind::Stone) {
        auto& st=stones[held.source];st.present=true;st.position={at.x,at.y+st.size.y*0.4,at.z};
        rebuild_stone_obstacles();
      } else if(held.kind==HeldKind::Torch && tree_fire) tree_fire->add_burning_branch(at,held.torch_seconds);
      else dropped_branches.push_back({at,held.size,person.yaw,true});
      const std::string what=std::string("dropped the ")+terraflux::graphics::held_kind_name(held.kind);
      held=terraflux::graphics::HeldItem{};
      return what;
    };
    const auto throw_held=[&]()->std::string {
      if(held.kind==terraflux::graphics::HeldKind::Nothing)return "";
      terraflux::graphics::Projectile p;
      p.item=held;p.position=person.eye()+look_forward()*0.5;p.velocity=terraflux::graphics::throw_velocity(look_forward(),12.0,12.0);
      projectiles.push_back(p);
      const std::string what=std::string("threw the ")+terraflux::graphics::held_kind_name(held.kind);
      held=terraflux::graphics::HeldItem{};
      return what;
    };
    // Flights end on the ground as what they are.
    const auto land_projectiles=[&](double dt) {
      for(auto& p:projectiles) {
        terraflux::graphics::advance_projectile(p,dt,walk_env.physics.gravity_mps2,ground_height,ground_sampler.lower_bound(),ground_sampler.upper_bound());
        if(!p.landed)continue;
        using terraflux::graphics::HeldKind;
        if(p.item.kind==HeldKind::Stone){auto& st=stones[p.item.source];st.present=true;st.position={p.position.x,p.position.y+st.size.y*0.4,p.position.z};rebuild_stone_obstacles();}
        else if(p.item.kind==HeldKind::Torch && tree_fire)tree_fire->add_burning_branch(p.position,p.item.torch_seconds);
        else dropped_branches.push_back({p.position,p.item.size,person.yaw,true});
      }
      projectiles.erase(std::remove_if(projectiles.begin(),projectiles.end(),[](const terraflux::graphics::Projectile& p){return p.landed;}),projectiles.end());
    };
    // The tools. A flying rope coil is caught by the crown it passes through
    // (within a quarter of the tree's height of a standing trunk, between 30 %
    // and 95 % of its height): it wraps round a branch there.
    const terraflux::graphics::BranchCatchFn branch_catch=[&](terraflux::math::Vec3 from,terraflux::math::Vec3 to)->std::optional<terraflux::graphics::RopeCatch> {
      if(!vegetation || !tree_fire)return std::nullopt;
      const auto& trees=vegetation->placement().trees;
      const terraflux::math::Vec3 mid=(from+to)*0.5;
      std::optional<terraflux::graphics::RopeCatch> best;double best_d=1e300;
      for(std::size_t i=0;i<trees.size() && i<tree_fire->size();++i) {
        if(!tree_fire->standing(static_cast<std::uint32_t>(i)))continue;
        const double h=tree_fire->bodies()[i].height_meters;
        const double crown=std::max(1.2,0.25*h);
        const double dx=trees[i].position[0]-mid.x,dz=trees[i].position[2]-mid.z;
        const double d=std::hypot(dx,dz);
        if(d>crown+0.3)continue;
        const double above=mid.y-trees[i].position[1];
        if(above<0.3*h || above>0.95*h)continue;
        if(d<best_d) {
          best_d=d;
          // The branch: out from the trunk toward the coil, at the coil's height.
          const double out=std::min(d,crown*0.6);
          const terraflux::math::Vec3 point=d>1e-6?terraflux::math::Vec3{trees[i].position[0]-dx/d*out,mid.y,trees[i].position[2]-dz/d*out}:mid;
          best=terraflux::graphics::RopeCatch{point,static_cast<int>(i)};
        }
      }
      return best;
    };
    const auto rope_in_hand_reach=[&]()->bool {
      using terraflux::graphics::RopeState;
      if(rope.state!=RopeState::Hung && rope.state!=RopeState::Laid)return false;
      const double reach=rope_rules.grab_reach_meters+1.5;
      for(const auto& p:{rope.anchor,rope.end,rope.thrower_feet,rope.landing})if((p-person.feet).length()<=reach)return true;
      return false;
    };
    // A standing trunk or a branch within the tool's reach: the tree index, or -2 for
    // the dropped branch `chop_branch`, -3 for a burning branch at the feet, -1 for nothing.
    const auto tool_target=[&](double reach)->int {
      if(const auto t=tree_in_reach(reach))return static_cast<int>(*t);
      if(const auto b=terraflux::graphics::branch_in_reach(dropped_branches,person.eye(),look_forward(),reach+0.7,70.0)){chop_branch=*b;return -2;}
      if(tree_fire)for(const auto& b:tree_fire->branches())if(b.landed && std::hypot(b.position.x-person.feet.x,b.position.z-person.feet.z)<reach+0.3)return -3;
      return -1;
    };
    const auto toward_target=[&](int tree)->terraflux::math::Vec2 {
      const auto& t=vegetation->placement().trees[static_cast<std::size_t>(tree)];
      const double dx=t.position[0]-person.feet.x,dz=t.position[2]-person.feet.z,l=std::hypot(dx,dz);
      if(l<1e-6){const auto f=look_forward();return {f.x,f.z};}
      return {dx/l,dz/l};
    };
    // A cut through a branch: a dropped branch becomes two halves (or is too short); a burning one too.
    const auto cut_branch=[&](int target)->std::string {
      if(target==-2 && chop_branch<dropped_branches.size()) {
        auto& b=dropped_branches[chop_branch];
        if(!terraflux::graphics::branch_can_be_cut(b.length))return "the branch is too short to cut";
        b.length*=0.5;
        terraflux::graphics::DroppedBranch half=b;
        half.position.x+=std::cos(b.yaw)*b.length*0.7;half.position.z+=std::sin(b.yaw)*b.length*0.7;
        if(const auto g=ground_height(half.position.x,half.position.z))half.position.y=*g;
        dropped_branches.push_back(half);
        return "cut the branch in two";
      }
      if(target==-3 && tree_fire)return tree_fire->split_branch(person.feet.x,person.feet.z,axe_rules.reach_meters+0.3,terraflux::graphics::kMinBranchPiece)?
          "cut the burning branch in two":"the burning branch is too short to cut";
      return "";
    };
    // A chop lands: the trunk is cut deeper (and falls away from you at 70 %), a branch in two.
    const auto land_chop=[&]()->std::string {
      if(chop_target>=0 && tree_fire && static_cast<std::size_t>(chop_target)<tree_fire->size()) {
        const auto i=static_cast<std::uint32_t>(chop_target);
        if(!tree_fire->standing(i))return "the tree is already down";
        const double before=tree_fire->states()[i].cut;
        const double after=tree_fire->cut(i,axe_rules.chop_depth_meters,toward_target(chop_target));
        if(after>=1.0)return "felled the tree";
        const int left=static_cast<int>(std::ceil((terraflux::graphics::kFellCutFraction-after)/(after-before+1e-12)-1e-9));
        return "chopped: "+std::to_string(std::max(1,left))+" more";
      }
      return cut_branch(chop_target);
    };
    // What E would do with the tool in hand, and doing it (on the press).
    const auto tool_hint=[&]()->std::string {
      using terraflux::graphics::ToolKind;using terraflux::graphics::RopeState;
      switch(belt.selected) {
        case ToolKind::None:return "";
        case ToolKind::Rope: {
          if(person.on_rope)return rope.branch_tree<0 && person.rope_t_meters<=1e-6?"E: step off onto the edge | Space: let go":"W/S: climb | Space: let go";
          if(rope.state==RopeState::Coiled)return "E: throw the rope";
          if(rope.state==RopeState::Flying)return "";
          const bool can_pull=rope_in_hand_reach();
          if(rope.state==RopeState::Hung && terraflux::graphics::rope_grab_point(rope,person.feet,1.4,rope_rules))return can_pull?"E: take hold of the rope | 1: pull it in":"E: take hold of the rope";
          if(can_pull)return "1: pull the rope in";
          return rope.state==RopeState::Hung?"the rope hangs there":"the rope lies there";
        }
        case ToolKind::Axe: {
          if(belt.swing>0)return "";
          const int target=tool_target(axe_rules.reach_meters);
          if(target>=0){const int n=terraflux::graphics::chops_to_fell(tree_fire->bodies()[static_cast<std::size_t>(target)].trunk_radius_meters*(1.0-tree_fire->states()[static_cast<std::size_t>(target)].cut/terraflux::graphics::kFellCutFraction),axe_rules);return "E: chop ("+std::to_string(std::max(1,n))+" to fell)";}
          if(target==-2)return terraflux::graphics::branch_can_be_cut(dropped_branches[chop_branch].length)?"E: cut the branch in two":"the branch is too short to cut";
          if(target==-3)return "E: cut the burning branch in two";
          return "";
        }
        case ToolKind::Chainsaw: {
          if(belt.chainsaw_fuel_seconds<=0)return "the chainsaw is out of fuel";
          const int target=tool_target(saw_rules.reach_meters);
          if(target>=0)return "hold E: saw ("+std::to_string(static_cast<int>(std::ceil(terraflux::graphics::seconds_to_saw(tree_fire->bodies()[static_cast<std::size_t>(target)].trunk_radius_meters*(1.0-tree_fire->states()[static_cast<std::size_t>(target)].cut/terraflux::graphics::kFellCutFraction),saw_rules))))+" s to fell)";
          if(target==-2)return terraflux::graphics::branch_can_be_cut(dropped_branches[chop_branch].length)?"hold E: saw the branch in two":"the branch is too short to cut";
          if(target==-3)return "hold E: saw the burning branch in two";
          return "";
        }
      }
      return "";
    };
    const auto use_tool=[&]()->std::string {
      using terraflux::graphics::ToolKind;using terraflux::graphics::RopeState;
      switch(belt.selected) {
        case ToolKind::None:return "";
        case ToolKind::Rope: {
          if(person.on_rope)return "";
          if(rope.state==RopeState::Coiled) {
            terraflux::graphics::throw_rope(rope,person.feet,person.eye(),look_forward(),rope_rules);
            return "threw the rope";
          }
          if(rope.state==RopeState::Hung)if(const auto at=terraflux::graphics::rope_grab_point(rope,person.feet,1.4,rope_rules)){terraflux::graphics::grab_rope(person,rope,*at);return "took hold of the rope";}
          return "";
        }
        case ToolKind::Axe: {
          if(belt.swing>0)return "";
          const int target=tool_target(axe_rules.reach_meters);
          if(target==-1)return "";
          chop_target=target;belt.swing=1e-6;  // the swing starts; the chop lands halfway through it
          return "swung the axe";
        }
        case ToolKind::Chainsaw:  // held, not pressed: see saw_step
          return belt.chainsaw_fuel_seconds>0 && tool_target(saw_rules.reach_meters)!=-1?"started the chainsaw":"";
      }
      return "";
    };
    // The rope key with the rope out in the world and within reach: pulled in (coiled).
    const auto pull_rope_in=[&]()->bool {
      if(rope.state==terraflux::graphics::RopeState::Coiled || rope.state==terraflux::graphics::RopeState::Flying || person.on_rope || !rope_in_hand_reach())return false;
      terraflux::graphics::coil_rope(rope);
      belt.selected=terraflux::graphics::ToolKind::Rope;
      return true;
    };
    // The chainsaw while E is held: saws the trunk or branch in reach, burning fuel.
    const auto saw_step=[&](bool held_key,double dt)->std::string {
      belt.sawing=false;
      if(belt.selected!=terraflux::graphics::ToolKind::Chainsaw || !held_key || belt.chainsaw_fuel_seconds<=0 || dt<=0)return "";
      const int target=tool_target(saw_rules.reach_meters);
      if(target==-1){saw_branch_seconds=0;return "";}
      belt.sawing=true;belt.saw_phase+=dt;
      belt.chainsaw_fuel_seconds=std::max(0.0,belt.chainsaw_fuel_seconds-dt);
      terraflux::graphics::shake(person,0.004);
      if(target>=0 && tree_fire) {
        const auto i=static_cast<std::uint32_t>(target);
        const double after=tree_fire->cut(i,saw_rules.cut_rate_mps*dt,toward_target(target));
        if(after>=1.0 && tree_fire->states()[i].felled && tree_fire->states()[i].stage==TreeFireStage::Falling && tree_fire->states()[i].fall_angle<0.05)return "sawed the tree through";
        return "";
      }
      saw_branch_seconds+=dt;
      if(saw_branch_seconds>=saw_rules.branch_seconds){saw_branch_seconds=0;return cut_branch(target).size()?cut_branch(target):"";}
      return "";
    };
    // The swing and the flying rope go on by themselves.
    const auto advance_tools=[&](double dt)->std::string {
      std::string what;
      if(belt.swing>0) {
        const double before=belt.swing;
        belt.swing+=dt/axe_rules.swing_seconds;
        if(before<0.5 && belt.swing>=0.5)what=land_chop();
        if(belt.swing>=1.0){belt.swing=0;chop_target=-1;}
      }
      if(rope.state==terraflux::graphics::RopeState::Flying) {
        terraflux::graphics::advance_rope(rope,dt,walk_env.physics.gravity_mps2,ground_height,ground_sampler.lower_bound(),ground_sampler.upper_bound(),branch_catch,rope_rules);
        using terraflux::graphics::RopeState;
        if(rope.state==RopeState::Hung)what=rope.branch_tree>=0?"The rope wrapped round a branch "+std::to_string(static_cast<int>(std::lround(rope.anchor_height_meters)))+" m up":
            "The rope hangs "+std::to_string(static_cast<int>(std::lround(rope.anchor_height_meters)))+" m down the face, pegged at the lip";
        else if(rope.state==RopeState::Laid)what="The rope lies on the ground (nothing to climb down here)";
        else if(rope.state==RopeState::Coiled)what="The rope was lost and pulled back in";
      }
      return what;
    };
    // A trunk coming down on the walker: the events of the last tree step.
    const auto trunks_landing=[&]() {
      if(!tree_fire || !vegetation)return;
      for(const auto& e:tree_fire->events()) {
        if(e.kind!=TreeFireEvent::Kind::Grounded)continue;
        const auto& st=tree_fire->states()[e.tree];
        const auto& b=tree_fire->bodies()[e.tree];
        const auto& t=vegetation->placement().trees[e.tree];
        const double dx=person.feet.x-t.position[0],dz=person.feet.z-t.position[2];
        const double along=dx*st.fall_direction.x+dz*st.fall_direction.y;
        const double lateral=std::abs(dx*st.fall_direction.y-dz*st.fall_direction.x);
        if(along<0.5 || along>b.height_meters || lateral>b.trunk_radius_meters+walk_env.body.radius+0.6)continue;
        if(person.on_rope && person.feet.y>t.position[1]+2.0)continue;  // up a rope, above the log
        pending_crush=std::max(pending_crush,terraflux::graphics::crush_damage(b.mass_kg));
        terraflux::graphics::knock_down(person,3.0,1.0);
        terraflux::graphics::shake(person,0.06);
        lightning_message="A TREE FELL ON YOU ("+std::to_string(static_cast<int>(std::lround(b.mass_kg)))+" kg)";
        lightning_message_until=engine_time+6.0;
        std::cout<<lightning_message<<'\n';
      }
    };
    std::array<float, 20> initial_camera{};
    const auto camera_bytes = std::as_bytes(std::span(initial_camera));
    const auto camera = owned.create_buffer({BufferUsage::Uniform, camera_bytes.size()}, camera_bytes);
    const auto pipeline = owned.create_pipeline({
        playback?ShaderProgramId::Wildfire:ShaderProgramId::Terrain,
        playback ? wildfire_shader_layout() : terrain_shader_layout(), true});

    std::unique_ptr<TerrainTileRenderer> tiled_terrain;
    // Tile sizes are given in simulation cells; scale them to the render lattice.
    const std::uint32_t tile_quads=static_cast<std::uint32_t>(std::min<std::uint64_t>(256,
        static_cast<std::uint64_t>(options.terrain_tile_quads)*mesh.refinement));
    if(options.terrain_tiles)tiled_terrain=std::make_unique<TerrainTileRenderer>(
        device,mesh,terraflux::terrain::TerrainTileOptions{tile_quads},!options.terrain_residency);
    if(tiled_terrain) {
      tiled_terrain->set_residency_limit(options.max_resident_terrain_tiles);
      tiled_terrain->set_retention_frames(options.terrain_retention_frames);
      // Rock relief displaces vertices vertically; keep culling conservative.
      tiled_terrain->set_bounds_padding(terrain_style.rock_relief_meters*terrain_style.detail_strength);
    }
    std::vector<std::size_t> terrain_initial_residents;
    std::vector<BufferHandle> terrain_initial_handles;
    bool terrain_saw_eviction=false,terrain_saw_readmission=false;
    std::uint64_t benchmark_terrain_uploads=0,benchmark_terrain_evictions=0;
    std::uint64_t benchmark_terrain_retained=0,benchmark_terrain_trimmed=0;
    std::uint64_t benchmark_terrain_draws=0,benchmark_terrain_culled=0,tile_test_culled=0;

    // Two independent world transforms share the original asset VBO/IBO.
    // Their own std140 object UBOs are refreshed every frame (not mesh data).
    struct ImportedGpu {
      // Declared first, destroyed last: instance-ring storage is retired before
      // geometry/material/pipeline resources; the GL context outlives both.
      std::unique_ptr<RenderResourceOwner> resources;
      BufferHandle vb, ib, object_uniform_left, object_uniform_right;
      struct GltfInstance {
        BufferHandle uniform;
        std::uint32_t mesh_index=0;
        terraflux::math::Mat4 model;
      };
      std::vector<GltfInstance> gltf_instances;
      struct SceneInstance {
        BufferHandle uniform;
        terraflux::scene::NodeId node=0;
        std::uint32_t mesh_index=0;
        terraflux::math::Mat4 asset_local=terraflux::math::Mat4::identity();
        std::shared_ptr<RenderResourceOwner> uniform_owner;
      };
      std::vector<SceneInstance> scene_instances;
      terraflux::math::Mat4 scene_anchor=terraflux::math::Mat4::identity();
      bool is_gltf=false, uses_pbr=false, uses_scene_file=false;
      BoundingSphere primary_bounds{};
      std::vector<BoundingSphere> additional_bounds;
      struct Section {
        std::uint32_t first=0,count=0;
        TextureHandle albedo{},metallic_roughness{},normal{},occlusion{},emissive{};
        terraflux::math::Vec3 base_color{1,1,1};
        BufferHandle pbr_uniform{};
        float metallic=1,roughness=1,normal_scale=1,occlusion_strength=1;
        float alpha_factor=1,alpha_cutoff=.5f;
        bool alpha_mask=false,alpha_blend=false;
        terraflux::math::Vec3 emissive_factor{};
        std::vector<TextureLease> texture_leases;
      };
      SharedTextureCache textures;
      explicit ImportedGpu(RenderDevice& device):textures(device){}
      std::vector<Section> sections;
      std::unique_ptr<RenderResourceOwner> primary_resources;
      struct AdditionalMesh {
        std::unique_ptr<RenderResourceOwner> resources;
        BufferHandle vb, ib;
        std::vector<Section> sections;
        std::uint32_t indices=0;
      };
      std::vector<AdditionalMesh> additional_meshes;
      std::vector<terraflux::assets::Mesh> cpu_meshes;
      std::function<void(ImportedGpu&,std::uint32_t)> ensure_mesh;
      terraflux::runtime::SceneResidency<SceneInstance> residency;
      PipelineHandle pipeline,instanced_pipeline,transparent_pipeline;
      std::unique_ptr<OpaqueRenderQueue::InstanceBufferRing> instance_ring;
      std::uint32_t indices = 0;
      terraflux::scene::Scene scene;
      std::string authored_signature;
      terraflux::assets::CpuRecipeBytes cpu_payload;
      terraflux::scene::NodeId left_pivot = 0, right_pivot = 0;
      terraflux::scene::NodeId left_mesh = 0, right_mesh = 0;
      double separation = 1.0;
      terraflux::math::Vec3 local_center{};
    };
    auto terrain_resource_base=device.live_resource_counts();
    auto terrain_byte_base=device.live_resource_bytes();
    if(tiled_terrain) {
      terrain_resource_base.buffers-=2*tiled_terrain->resident_tile_count();
      terrain_byte_base.buffer_bytes-=tiled_terrain->resident_bytes();
    }
    const auto terrain_resources=[&] {
      auto counts=terrain_resource_base;
      if(tiled_terrain)counts.buffers+=2*tiled_terrain->resident_tile_count();
      if(offscreen.target()){++counts.render_targets;++counts.textures;}
      return counts;
    };
    const auto terrain_bytes=[&] {
      auto bytes=terrain_byte_base;
      if(tiled_terrain)bytes.buffer_bytes+=tiled_terrain->resident_bytes();
      if(const auto target=offscreen.target())
        bytes.render_target_bytes+=offscreen_storage_bytes({target->width,target->height});
      return bytes;
    };
    const auto load_imported=[&]()->std::unique_ptr<ImportedGpu> {
      std::optional<terraflux::scene::Scene> loaded_scene;
      std::optional<terraflux::runtime::ResidencyState> loaded_state;
      terraflux::assets::SceneModelAssets scene_assets;
      if(!options.scene_file.empty()) {
        loaded_scene=terraflux::scene::deserialize_scene(read_file(options.scene_file));
        if(!options.load_state.empty()) {
          if(std::filesystem::file_size(options.load_state)>65536)
            throw std::invalid_argument("residency state exceeds size bound");
          loaded_state=terraflux::runtime::deserialize_residency_state(read_file(options.load_state));
          terraflux::runtime::validate_residency_state(*loaded_state,*loaded_scene,options.obj_file);
        }
        if(loaded_scene->mesh_assets().empty()) {
          if(options.obj_file.empty())throw std::invalid_argument("legacy --scene requires --obj");
        }else{
          if(!options.obj_file.empty())
            throw std::invalid_argument("asset scenes declare their models; omit --obj");
          scene_assets=terraflux::assets::load_scene_assets(*loaded_scene,options.scene_file,
              options.cpu_recipe_budget_bytes.value_or(std::numeric_limits<std::uint64_t>::max()));
        }
      }
      const bool asset_scene=!scene_assets.meshes.empty();
      if (!options.obj_file.empty() || !options.gltf_file.empty() || asset_scene) {
        const bool use_gltf=!options.gltf_file.empty();
        const bool use_pbr=use_gltf || scene_assets.has_gltf;
        const auto source_file=use_gltf?options.gltf_file:
            asset_scene?scene_assets.sources.front().string():options.obj_file;
        const auto source_folder=std::filesystem::path(source_file).parent_path();
        using terraflux::assets::LocatedMaterial;
        terraflux::assets::LocatedMaterials material_files;
        terraflux::assets::Mesh object;
        std::map<std::string,terraflux::assets::ImageRgba8> gltf_images;
        std::vector<terraflux::assets::Mesh> extra_meshes;
        std::vector<std::pair<std::uint32_t,terraflux::math::Mat4>> node_worlds;
        if(use_gltf) {
          const auto resolver=[&](const std::string& uri) {
            const auto path=asset_path(source_folder,uri);
            const auto size=std::filesystem::file_size(path);
            if(size>64'000'000)
              throw std::invalid_argument("external glTF buffer larger than 64MB");
            const auto contents=read_file(path.string());
            return std::vector<std::uint8_t>(contents.begin(),contents.end());
          };
          const auto source_contents=read_file(source_file);
          const auto is_glb=source_contents.size()>=4 &&
               source_contents.substr(0,4)==std::string("glTF",4);
          const auto asset_bytes=std::span(
              reinterpret_cast<const std::uint8_t*>(source_contents.data()),
              source_contents.size());
          auto asset=is_glb?terraflux::assets::import_glb(asset_bytes,resolver):
                            terraflux::assets::import_gltf(source_contents,resolver);
          if(asset.meshes.empty())throw std::invalid_argument("glTF contains no meshes");
          object=std::move(asset.meshes.front());
          gltf_images=std::move(asset.images);
          for(std::size_t i=1;i<asset.meshes.size();++i)
            extra_meshes.push_back(std::move(asset.meshes[i]));
          for(const auto& instance:asset.instances)
            node_worlds.emplace_back(instance.mesh_index,instance.world);
          for(const auto& [name,material]:asset.materials)
            material_files.emplace(name,LocatedMaterial{material,source_folder});
        } else if(asset_scene) {
          // Keep per-file material names distinct even when OBJ libraries reuse
          // names. Each mesh and its material sections are uploaded only once.
          gltf_images=std::move(scene_assets.images);
          for(const auto& [name,material]:scene_assets.materials)
            material_files.emplace(name,LocatedMaterial{material,scene_assets.material_folders.at(name)});
          for(std::size_t i=0;i<scene_assets.meshes.size();++i) {
            if(scene_assets.gltf_meshes[i])continue;
            auto& mesh=scene_assets.meshes[i];
            auto materials=terraflux::assets::load_obj_materials(mesh,
                scene_assets.sources[i].parent_path(),"scene/"+std::to_string(i)+"/",true);
            material_files.merge(materials);
          }
          object=std::move(scene_assets.meshes.front());
          for(std::size_t i=1;i<scene_assets.meshes.size();++i)
            extra_meshes.push_back(std::move(scene_assets.meshes[i]));
          for(std::size_t id=1;id<loaded_scene->size();++id) {
            const auto node=static_cast<terraflux::scene::NodeId>(id);
            const auto& reference=loaded_scene->get(node).mesh_asset;
            if(reference) {
              const auto model=scene_assets.model_indices.at(*reference);
              for(const auto& instance:scene_assets.model_instances.at(model)) {
                if(node_worlds.size()>=1024)throw std::length_error("scene exceeds 1024 expanded mesh instances");
                node_worlds.emplace_back(instance.mesh_index,
                    loaded_scene->world_matrix(node)*instance.local);
              }
            }
          }
        } else {
          std::ifstream model_file(options.obj_file);
          if (!model_file) throw std::runtime_error("cannot open OBJ "+options.obj_file);
          object=terraflux::assets::import_obj(model_file);
          material_files=terraflux::assets::load_obj_materials(object,source_folder);
        }
        if(object.vertices.empty())throw std::invalid_argument("imported mesh has no vertices");
        const auto get_mesh=[&](std::uint32_t id)->const terraflux::assets::Mesh& {
          return id==0?object:extra_meshes.at(id-1);
        };
        auto min_pos=object.vertices.front().position;
        auto max_pos=min_pos;
        if(use_gltf || asset_scene) {
          std::vector<std::pair<terraflux::math::Vec3,terraflux::math::Vec3>> bounds;
          for(std::size_t mesh_id=0;mesh_id<extra_meshes.size()+1;++mesh_id) {
            const auto& mesh=get_mesh(static_cast<std::uint32_t>(mesh_id));
            if(mesh.vertices.empty())throw std::invalid_argument("glTF mesh is empty");
            auto lo=mesh.vertices.front().position,hi=lo;
            for(const auto& vertex:mesh.vertices){
              const auto& p=vertex.position;
              lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
              hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);
            }
            bounds.emplace_back(lo,hi);
          }
          bool initialized=false;
          for(const auto& [mesh_index,world]:node_worlds){
            const auto& [lo,hi]=bounds.at(mesh_index);
            for(int x=0;x<2;++x)for(int y=0;y<2;++y)for(int z=0;z<2;++z){
              const auto point=world.transformPoint({x?hi.x:lo.x,y?hi.y:lo.y,z?hi.z:lo.z});
              if(!initialized){min_pos=max_pos=point;initialized=true;}
              else {
                min_pos.x=std::min(min_pos.x,point.x);
                min_pos.y=std::min(min_pos.y,point.y);
                min_pos.z=std::min(min_pos.z,point.z);
                max_pos.x=std::max(max_pos.x,point.x);
                max_pos.y=std::max(max_pos.y,point.y);
                max_pos.z=std::max(max_pos.z,point.z);
              }
            }
          }
          if(!initialized)throw std::invalid_argument("asset scene contains no mesh instances");
        }else{
          for(const auto& vertex:object.vertices) {
            const auto& p=vertex.position;
            min_pos.x=std::min(min_pos.x,p.x);
            min_pos.y=std::min(min_pos.y,p.y);
            min_pos.z=std::min(min_pos.z,p.z);
            max_pos.x=std::max(max_pos.x,p.x);
            max_pos.y=std::max(max_pos.y,p.y);
            max_pos.z=std::max(max_pos.z,p.z);
          }
        }
        const double extent = std::max({max_pos.x-min_pos.x, max_pos.y-min_pos.y,
                                        max_pos.z-min_pos.z});
        if (!(extent > 0.0)) throw std::runtime_error("imported model has no spatial extent");
        const double gltf_scale=options.asset_preview?0.43:0.22;
        const double scale=(scene_camera.starting_distance*(use_pbr?gltf_scale:0.095))/extent;
        auto cpu_payload=terraflux::assets::mesh_recipe_bytes(object);
        cpu_payload.include(terraflux::assets::cpu_recipe_bytes(extra_meshes,gltf_images));
        terraflux::assets::check_cpu_recipe_budget(cpu_payload,
            options.cpu_recipe_budget_bytes.value_or(std::numeric_limits<std::uint64_t>::max()));
        const auto geometry = prepare_textured_asset_upload(object); // local + UV
        auto candidate=std::make_unique<ImportedGpu>(device);
        auto& obj=*candidate;
        obj.resources=std::make_unique<RenderResourceOwner>(device);
        auto& asset_owned=*obj.resources;
        obj.cpu_payload=cpu_payload;
        obj.is_gltf=use_gltf;
        obj.uses_pbr=use_pbr;
        obj.primary_bounds=mesh_bounds(object);
        for(const auto& mesh:extra_meshes)
          obj.additional_bounds.push_back(mesh_bounds(mesh));
        obj.indices = static_cast<std::uint32_t>(geometry.indices.size());
        obj.separation = extent * 0.95;
        obj.local_center = {(min_pos.x + max_pos.x)*0.5, min_pos.y,
                            (min_pos.z + max_pos.z)*0.5};
        if(!use_gltf && !options.scene_file.empty()) {
          obj.uses_scene_file=true;
          obj.authored_signature=terraflux::runtime::residency_signature(*loaded_scene,options.obj_file);
          obj.scene=std::move(*loaded_scene);
          obj.scene_anchor=terraflux::math::Mat4::translation(scene_camera.target) *
              terraflux::math::Mat4::scaling({scale,scale,scale}) *
              terraflux::math::Mat4::translation(obj.local_center*-1.0);
          for(std::size_t id=1;id<obj.scene.size();++id) {
            const auto& node=obj.scene.get(static_cast<terraflux::scene::NodeId>(id));
            if(node.mesh_instance) {
              // Hold node identifiers until the corresponding UBOs are created.
              const auto scene_node=static_cast<terraflux::scene::NodeId>(id);
              if(asset_scene) {
                const auto model=scene_assets.model_indices.at(*node.mesh_asset);
                for(const auto& instance:scene_assets.model_instances.at(model))
                  obj.scene_instances.push_back({{},scene_node,instance.mesh_index,instance.local});
              }else obj.scene_instances.push_back({{},scene_node,0});
            }
          }
          if(obj.scene_instances.empty() || obj.scene_instances.size()>1024)
            throw std::invalid_argument("scene requires 1..1024 mesh instances");
          obj.residency.set_budget({options.max_resident_meshes,options.max_scene_instances});
          if(options.start_inactive) {
            while(!obj.scene_instances.empty())
              obj.residency.detach(obj.scene,obj.scene_instances,obj.scene_instances.front().node,
                  [](auto& instance){instance.uniform={};instance.uniform_owner.reset();},[]{},[]{});
          }
          if(loaded_state) {
            for(auto node:loaded_state->inactive)
              obj.residency.detach(obj.scene,obj.scene_instances,node,
                  [](auto& instance){instance.uniform={};instance.uniform_owner.reset();},[]{},[]{});
            if(loaded_state->selected)obj.residency.select(obj.scene_instances,*loaded_state->selected);
          }
          if(options.selected_node)obj.residency.select(obj.scene_instances,*options.selected_node);
          obj.residency.check_budget(obj.scene_instances);
        }else if(!use_gltf) {
          const auto anchor = obj.scene.create_node("Terrain anchor");
          obj.left_pivot = obj.scene.create_node("Rotating left", anchor);
          obj.right_pivot = obj.scene.create_node("Rotating right", anchor);
          obj.left_mesh = obj.scene.create_node("Mesh left", obj.left_pivot);
          obj.right_mesh = obj.scene.create_node("Mesh right", obj.right_pivot);
          obj.scene.set_transform(anchor, {
              scene_camera.target, {}, {scale,scale,scale}});
          obj.scene.set_transform(obj.left_mesh, {
              obj.local_center * -1.0, {}, {1,1,1}});
          obj.scene.set_transform(obj.right_mesh, {
              obj.local_center * -1.0, {}, {1,1,1}});
        }
        const auto build_sections=[material_files=std::move(material_files),
            gltf_images=std::move(gltf_images),use_pbr,use_gltf,
            texture_override=options.texture_file](ImportedGpu& target,RenderResourceOwner& mesh_owner,
                                                 const terraflux::assets::Mesh& mesh) {
          const auto cached_texture=[&](const std::filesystem::path& source,
                                       terraflux::TextureSampling sampling) {
          const auto key=terraflux::assets::texture_cache_key(source,sampling,use_pbr);
          if(auto existing=target.textures.find(key))return existing;
          const auto image=terraflux::assets::resolve_texture_image(source,use_pbr,gltf_images);
          const auto bytes=std::as_bytes(std::span(image.pixels));
          return target.textures.acquire(key,{image.width,image.height,sampling},bytes);
        };
          std::vector<ImportedGpu::Section> result;
          result.reserve(mesh.sections.size());
          for(const auto& section:mesh.sections) {
            terraflux::math::Vec3 color{1,1,1};
            terraflux::TextureSampling sampling{};
            double metallic=use_gltf?1:0,roughness=1,normal_scale=1,occlusion_strength=1;
            double alpha_factor=1,alpha_cutoff=.5;
            bool alpha_mask=false,alpha_blend=false;
            terraflux::math::Vec3 emissive_factor{};
            std::filesystem::path texture_file,metallic_file,normal_file,occlusion_file,emissive_file;
            terraflux::TextureSampling metallic_sampling{},normal_sampling{},occlusion_sampling{},emissive_sampling{};
            if(!section.material_name.empty()) {
              const auto it=material_files.find(section.material_name);
              if(it==material_files.end())
                throw std::invalid_argument("undefined imported material: "+section.material_name);
              color=it->second.material.base_color;
              sampling=it->second.material.albedo_sampling;
              metallic=it->second.material.metallic_factor;
              roughness=it->second.material.roughness_factor;
              normal_scale=it->second.material.normal_scale;
              occlusion_strength=it->second.material.occlusion_strength;
              emissive_factor=it->second.material.emissive_factor;
              alpha_factor=it->second.material.alpha_factor;
              alpha_cutoff=it->second.material.alpha_cutoff;
              alpha_mask=it->second.material.alpha_mode==
                  terraflux::assets::Material::AlphaMode::Mask;
              alpha_blend=it->second.material.alpha_mode==
                  terraflux::assets::Material::AlphaMode::Blend;
              occlusion_sampling=it->second.material.occlusion_sampling;
              emissive_sampling=it->second.material.emissive_sampling;
              if(!it->second.material.occlusion_map.empty())
                occlusion_file=it->second.material.occlusion_map;
              if(!it->second.material.emissive_map.empty())
                emissive_file=it->second.material.emissive_map;
              metallic_sampling=it->second.material.metallic_roughness_sampling;
              normal_sampling=it->second.material.normal_sampling;
              if(!it->second.material.metallic_roughness_map.empty())
                metallic_file=it->second.material.metallic_roughness_map;
              if(!it->second.material.normal_map.empty())
                normal_file=it->second.material.normal_map;
              if(!it->second.material.albedo_map.empty())
                texture_file=(use_pbr && gltf_images.contains(it->second.material.albedo_map))?
                    std::filesystem::path(it->second.material.albedo_map):
                    terraflux::assets::resolve_asset_path(it->second.folder,it->second.material.albedo_map,32'000'000);
            }
            if(!texture_override.empty())texture_file=texture_override;
            std::vector<TextureLease> leases;
            const auto retain_texture=[&](const std::filesystem::path& source,terraflux::TextureSampling state) {
              auto lease=cached_texture(source,state);
              const auto handle=lease->handle();
              leases.push_back(std::move(lease));
              return handle;
            };
            const auto handle=retain_texture(texture_file,sampling);
            TextureHandle mr_handle{},normal_handle{},occlusion_handle{},emissive_handle{};
            if(use_pbr) {
              mr_handle=retain_texture(metallic_file.empty()?
                      std::filesystem::path("__default_mr__"):metallic_file,
                      metallic_sampling);
              normal_handle=retain_texture(normal_file.empty()?
                      std::filesystem::path("__default_normal__"):normal_file,
                      normal_sampling);
              occlusion_handle=retain_texture(occlusion_file.empty()?
                      std::filesystem::path("__default_occlusion__"):occlusion_file,
                      occlusion_sampling);
              emissive_handle=retain_texture(emissive_file.empty()?
                      std::filesystem::path("__default_emissive__"):emissive_file,
                      emissive_sampling);
            }
            BufferHandle pbr_uniform{};
            if(use_pbr) {
              PbrMaterialUniform pbr{};
              pbr.factors={static_cast<float>(metallic),static_cast<float>(roughness),
                           static_cast<float>(normal_scale),static_cast<float>(occlusion_strength)};
              pbr.emissive={static_cast<float>(emissive_factor.x),
                            static_cast<float>(emissive_factor.y),
                            static_cast<float>(emissive_factor.z),
                            static_cast<float>(alpha_cutoff)};
              pbr.options={static_cast<float>(alpha_factor),
                           alpha_blend?2.0f:(alpha_mask?1.0f:0.0f),0,0};
              const auto bytes=std::as_bytes(std::span(&pbr,1));
              pbr_uniform=mesh_owner.create_buffer({BufferUsage::Uniform,bytes.size()},bytes);
            }
            result.push_back({section.first_index,section.index_count,
                              handle,mr_handle,normal_handle,occlusion_handle,emissive_handle,color,
                              pbr_uniform,static_cast<float>(metallic),
                              static_cast<float>(roughness),static_cast<float>(normal_scale),
                              static_cast<float>(occlusion_strength),
                              static_cast<float>(alpha_factor),static_cast<float>(alpha_cutoff),
                              alpha_mask,alpha_blend,emissive_factor,std::move(leases)});
          }
          if(result.empty())throw std::invalid_argument("mesh has no drawable material sections");
          return result;
        };
        obj.cpu_meshes.push_back(std::move(object));
        for(auto& mesh:extra_meshes)obj.cpu_meshes.push_back(std::move(mesh));
        obj.additional_meshes.resize(obj.cpu_meshes.size()-1);
        obj.ensure_mesh=[build_sections,device_ptr=&device](ImportedGpu& target,std::uint32_t index) {
          if((index==0?target.vb:target.additional_meshes.at(index-1).vb).id)return;
          ImportedGpu::AdditionalMesh ready{};
          ready.resources=std::make_unique<RenderResourceOwner>(*device_ptr);
          const auto& mesh=target.cpu_meshes.at(index);
          const auto upload=prepare_textured_asset_upload(mesh);
          const auto vertices=bytes_of(upload.vertices),indices=bytes_of(upload.indices);
          ready.sections=build_sections(target,*ready.resources,mesh);
          ready.vb=ready.resources->create_buffer({BufferUsage::Vertex,vertices.size()},vertices);
          ready.ib=ready.resources->create_buffer({BufferUsage::Index,indices.size()},indices);
          ready.indices=static_cast<std::uint32_t>(upload.indices.size());
          if(index==0) {
            target.vb=ready.vb;target.ib=ready.ib;
            target.sections=std::move(ready.sections);
            target.primary_resources=std::move(ready.resources);
          }else target.additional_meshes.at(index-1)=std::move(ready);
        };
        if(obj.uses_scene_file) {
          for(const auto& instance:obj.scene_instances)obj.ensure_mesh(obj,instance.mesh_index);
        }else for(std::uint32_t index=0;index<obj.cpu_meshes.size();++index)obj.ensure_mesh(obj,index);

        const auto init_object = make_object_uniform(terraflux::math::Mat4::identity());
        const auto init_bytes = std::as_bytes(std::span(&init_object,1));
        if(use_gltf) {
          const auto focus=scene_camera.target+
              terraflux::math::Vec3{0,options.asset_preview?
                    scene_camera.starting_distance*0.045:0,0};
          const auto anchor=terraflux::math::Mat4::translation(focus) *
              terraflux::math::Mat4::scaling({scale,scale,scale}) *
              terraflux::math::Mat4::translation(obj.local_center*-1.0);
          if(node_worlds.size()>1024)
            throw std::length_error("glTF viewer supports at most 1024 mesh instances");
          for(const auto& [mesh_index,world]:node_worlds) {
            const auto model=anchor*world;
            const auto uniform=make_object_uniform(model,{1,1,1});
            const auto bytes=std::as_bytes(std::span(&uniform,1));
            const auto handle=asset_owned.create_buffer({BufferUsage::Uniform,bytes.size()},bytes);
            obj.gltf_instances.push_back({handle,mesh_index,model});
          }
        }else if(obj.uses_scene_file){
          for(auto& instance:obj.scene_instances) {
            const auto model=obj.scene_anchor*obj.scene.world_matrix(instance.node)*instance.asset_local;
            const auto uniforms=make_object_uniform(model,{0.16,0.57,0.95});
            const auto bytes=std::as_bytes(std::span(&uniforms,1));
            instance.uniform_owner=std::make_shared<RenderResourceOwner>(device);
            instance.uniform=instance.uniform_owner->create_buffer({BufferUsage::Uniform,bytes.size()},bytes);
          }
        }else{
          obj.object_uniform_left = asset_owned.create_buffer(
              {BufferUsage::Uniform,init_bytes.size()},init_bytes);
          obj.object_uniform_right = asset_owned.create_buffer(
              {BufferUsage::Uniform,init_bytes.size()},init_bytes);
        }
        obj.pipeline = asset_owned.create_pipeline({
            use_pbr?ShaderProgramId::PbrObject:ShaderProgramId::TexturedObject,
            use_pbr?pbr_textured_object_shader_layout(false):
                     textured_object_shader_layout(),true});
        if(use_pbr)
          obj.transparent_pipeline=asset_owned.create_pipeline({
              ShaderProgramId::PbrObject,pbr_textured_object_shader_layout(false),true,true,false});
        if(!options.no_instancing){
          obj.instanced_pipeline=asset_owned.create_pipeline(
              {use_pbr?ShaderProgramId::PbrInstanced:ShaderProgramId::InstancedTexturedObject,
               use_pbr?pbr_textured_object_shader_layout(true):
                        instanced_textured_object_shader_layout(),true});
          obj.instance_ring=std::make_unique<OpaqueRenderQueue::InstanceBufferRing>(device);
        }
        std::size_t triangle_count=obj.indices/3;
        std::size_t sections_count=obj.sections.size();
        for(std::size_t index=1;index<obj.cpu_meshes.size();++index){
          triangle_count+=obj.cpu_meshes[index].indices.size()/3;
          sections_count+=obj.cpu_meshes[index].sections.size();
        }
        std::cout << (asset_scene?"Imported scene: ":use_gltf?"Imported glTF: ":"Imported OBJ: ") << triangle_count
                  << " triangles, " << (obj.additional_meshes.size()+1)
                  << " meshes, " << sections_count << " material ranges, "
                  << obj.textures.size() << " textures, "
                  << (use_gltf?obj.gltf_instances.size():
                      obj.uses_scene_file?obj.scene_instances.size():2)
                  << " instances sharing per-mesh geometry\n";
        return candidate;
      }
      return nullptr;
    };
    terraflux::graphics::AssetSlot<ImportedGpu> imported;
    const bool has_asset=!options.obj_file.empty() || !options.gltf_file.empty() ||
                         !options.scene_file.empty();
    if(has_asset)imported.reload(load_imported);
    std::optional<terraflux::runtime::ResidencyState> last_state;
    const auto capture_state=[&] {
      if(!imported || !imported->uses_scene_file)return;
      terraflux::runtime::ResidencyState state;
      state.scene_signature=imported->authored_signature;
      state.selected=imported->residency.selected(imported->scene_instances);
      for(const auto& [node,saved]:imported->residency.detached())state.inactive.push_back(node);
      last_state=std::move(state);
    };
    const auto unload_imported=[&] {
      capture_state();
      imported.unload();
      if(device.live_resource_counts()!=terrain_resources() || device.live_resource_bytes()!=terrain_bytes())
        throw std::runtime_error("asset unload did not restore terrain resource baseline");
      std::cout<<"Asset unloaded; terrain resources preserved\n";
    };
    if(has_asset && !options.smoke_test && !options.benchmark_frames)
      std::cout<<"Asset controls: U unload | R reload | [ ] select | D detach selected | A activate selected | P pause scene | N single tick (failed reload preserves current asset)\n";

    const auto report_residency=[&] {
      if(!(options.residency_report || options.profile) || !imported || !imported->uses_scene_file)return;
      const auto stats=imported->residency.statistics(imported->scene_instances);
      const auto budget=imported->residency.budget();
      const auto selected=imported->residency.selected(imported->scene_instances);
      const auto resources=device.live_resource_counts();
      const auto bytes=device.live_resource_bytes();
      const auto telemetry=device.resource_telemetry();
      std::string rejected="null";
      if(telemetry.last_rejection) {
        const auto& failure=*telemetry.last_rejection;
        rejected="{\"used_bytes\":"+std::to_string(failure.used_bytes)+
            ",\"requested_bytes\":"+std::to_string(failure.requested_bytes)+
            ",\"limit_bytes\":"+std::to_string(failure.limit_bytes)+"}";
      }
      std::cout<<"RESIDENCY_JSON={\"instances\":"<<stats.instances
          <<",\"referenced_meshes\":"<<stats.referenced_meshes
          <<",\"selected_node\":"<<(selected?std::to_string(*selected):"null")
          <<",\"detached_nodes\":"<<stats.detached_nodes
          <<",\"saved_instances\":"<<stats.saved_instances
          <<",\"max_meshes\":"<<budget.max_meshes<<",\"max_instances\":"<<budget.max_instances
          <<",\"live_buffers\":"<<resources.buffers<<",\"live_textures\":"<<resources.textures
          <<",\"live_pipelines\":"<<resources.pipelines<<",\"live_render_targets\":"<<resources.render_targets
          <<",\"buffer_bytes\":"<<bytes.buffer_bytes<<",\"texture_bytes\":"<<bytes.texture_bytes<<",\"render_target_bytes\":"<<bytes.render_target_bytes
          <<",\"cpu_vertex_bytes\":"<<imported->cpu_payload.vertex_bytes
          <<",\"cpu_index_bytes\":"<<imported->cpu_payload.index_bytes
          <<",\"cpu_image_bytes\":"<<imported->cpu_payload.image_bytes
          <<",\"cpu_recipe_bytes\":"<<imported->cpu_payload.total()
          <<",\"cpu_recipe_budget_bytes\":"<<(options.cpu_recipe_budget_bytes?std::to_string(*options.cpu_recipe_budget_bytes):"null")
          <<",\"total_resource_bytes\":"<<bytes.total()
          <<",\"peak_buffer_bytes\":"<<telemetry.peak_bytes.buffer_bytes
          <<",\"peak_texture_bytes\":"<<telemetry.peak_bytes.texture_bytes<<",\"peak_render_target_bytes\":"<<telemetry.peak_bytes.render_target_bytes
          <<",\"peak_total_bytes\":"<<telemetry.peak_total_bytes
          <<",\"allocation_count\":"<<telemetry.allocation_count
          <<",\"budget_rejections\":"<<telemetry.budget_rejections
          <<",\"headroom_bytes\":"<<telemetry.headroom_bytes
          <<",\"over_byte_budget\":"<<(telemetry.over_budget?"true":"false")
          <<",\"last_byte_rejection\":"<<rejected
          <<",\"resource_budget_bytes\":"<<(telemetry.limit_bytes==std::numeric_limits<std::uint64_t>::max()?"null":std::to_string(telemetry.limit_bytes))
          <<"}\n";
    };
    report_residency();
    const auto evict_unused=[&](ImportedGpu& asset) {
      const auto references=terraflux::runtime::SceneResidency<ImportedGpu::SceneInstance>::reference_counts(
          asset.scene_instances,asset.additional_meshes.size()+1);
      for(std::size_t mesh=0;mesh<references.size();++mesh) {
        auto& vertex=mesh==0?asset.vb:asset.additional_meshes[mesh-1].vb;
        auto& index=mesh==0?asset.ib:asset.additional_meshes[mesh-1].ib;
        auto& sections=mesh==0?asset.sections:asset.additional_meshes[mesh-1].sections;
        if(references[mesh] || vertex.id==0)continue;
        sections.clear();
        if(mesh==0)asset.primary_resources.reset();
        else asset.additional_meshes[mesh-1].resources.reset(); // releases texture leases only when their last user leaves
        vertex={};index={};
      }
      asset.textures.prune();
    };
    const auto detach_node=[&](terraflux::scene::NodeId node) {
      if(!imported || !imported->uses_scene_file)return;
      auto& asset=*imported.operator->();
      const bool changed=asset.residency.detach(asset.scene,asset.scene_instances,node,
          [](auto& instance){instance.uniform={};instance.uniform_owner.reset();},
          [&]{asset.instance_ring.reset();},[&]{evict_unused(asset);});
      if(!changed)return;
      if(!options.no_instancing)
        asset.instance_ring=std::make_unique<OpaqueRenderQueue::InstanceBufferRing>(device);
      std::cout<<"Detached scene node "<<node<<"; "<<asset.scene_instances.size()<<" mesh instances remain\n";
      report_residency();
    };

    const auto reattach_node=[&](terraflux::scene::NodeId node) {
      if(!imported || !imported->uses_scene_file)return;
      auto& asset=*imported.operator->();
      const bool changed=asset.residency.reattach(asset.scene,asset.scene_instances,node,
          [&](auto& instance) {
            asset.ensure_mesh(asset,instance.mesh_index);
            const auto model=asset.scene_anchor*asset.scene.world_matrix(node)*instance.asset_local;
            const auto uniforms=make_object_uniform(model,{1,1,1});
            const auto bytes=std::as_bytes(std::span(&uniforms,1));
            instance.uniform_owner=std::make_shared<RenderResourceOwner>(device);
            instance.uniform=instance.uniform_owner->create_buffer({BufferUsage::Uniform,bytes.size()},bytes);
          },[&]{evict_unused(asset);});
      if(!changed)return;
      std::cout<<"Reattached scene node "<<node<<"; "<<asset.scene_instances.size()<<" mesh instances resident\n";
      report_residency();
    };

    if(options.activate_node)reattach_node(*options.activate_node);

    constexpr double kDegrees=3.14159265358979323846/180.0;
    double yaw = options.view_yaw?*options.view_yaw*kDegrees:0.45, pitch = options.view_pitch?std::clamp(*options.view_pitch,6.0,84.0)*kDegrees:(options.asset_preview?0.42:0.75);
    double distance=scene_camera.starting_distance*options.view_zoom.value_or(options.asset_preview?0.75:1.0);
    // Switching views: the first-person camera stands under the orbit target
    // and looks the orbit's way; coming back, the orbit circles the point you
    // stood on, behind the way you looked, at its previous pitch and distance.
    // First person captures the cursor for mouse look; the world view frees it.
    bool previous_view_key=false,mouse_look_tracking=false,previous_jump_key=false,previous_interact_key=false,previous_throw_key=false;
    bool previous_tool_keys[3]={false,false,false};
    double mouse_look_x=0,mouse_look_y=0;
    const auto set_view_mode=[&](terraflux::graphics::CameraMode mode,bool interactive)->bool {
      using terraflux::graphics::CameraMode;
      if(mode==view_mode)return true;
      if(mode==CameraMode::FirstPerson) {
        const terraflux::graphics::OrbitCamera orbit{scene_camera.target,yaw,pitch,distance};
        if(!terraflux::graphics::enter_first_person(person,orbit,standing_height,ground_sampler.lower_bound(),ground_sampler.upper_bound(),
                                                    standing_search_step,standing_search_radius)) {
          std::cerr<<"First-person view rejected: no ground to stand on near the camera target; world view preserved\n";
          return false;
        }
        // Entering over a bridge deck (or a road) starts on it, not in the water or on the ground under it.
        if(const auto built=built_surface(person.feet.x,person.feet.z,person.feet.y+60.0);built && *built>person.feet.y)person.feet.y=*built;  // the highest surface here
        (void)terraflux::graphics::settle(person,walk_env); // never start inside a trunk
        terraflux::graphics::reset_vitals(vitals);person.stamina=1.0;person.exhausted=false;person.stunned_seconds=0;person.stun_crouch_meters=0;
        collapse_message.clear();if(lightning_message_until>engine_time+100.0)lightning_message_until=0.0;
      } else {
        terraflux::graphics::OrbitCamera orbit{scene_camera.target,yaw,pitch,distance};
        terraflux::graphics::enter_world_view(orbit,person);
        // The orbit target hovers above the water the walker stood in.
        scene_camera.target=snap_orbit_target(orbit.target);yaw=orbit.yaw;
      }
      view_mode=mode;
      if(interactive) {
        const bool first_person=mode==CameraMode::FirstPerson;
        glfwSetInputMode(window.get(),GLFW_CURSOR,first_person?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);
        if(first_person && glfwRawMouseMotionSupported())glfwSetInputMode(window.get(),GLFW_RAW_MOUSE_MOTION,GLFW_TRUE);
        mouse_look_tracking=false; // the first cursor sample after a switch is a reference, not a turn
      }
      const bool first_person=view_mode==CameraMode::FirstPerson;
      const auto eye=first_person?person.eye():terraflux::graphics::OrbitCamera{scene_camera.target,yaw,pitch,distance}.eye();
      std::cout<<"CAMERA_JSON={\"view\":\""<<terraflux::graphics::camera_mode_name(view_mode)
               <<"\",\"eye\":["<<eye.x<<","<<eye.y<<","<<eye.z<<"],\"feet\":["<<person.feet.x<<","<<person.feet.y<<","<<person.feet.z
               <<"],\"yaw_deg\":"<<(first_person?person.yaw:yaw)/kDegrees<<",\"pitch_deg\":"<<(first_person?person.pitch:pitch)/kDegrees
               <<",\"walk_speed_mps\":"<<person.walk_speed_mps<<",\"gravity_mps2\":"<<walk_env.physics.gravity_mps2
               <<",\"jump_height_m\":"<<walk_env.physics.jump_height_meters<<",\"gait\":"<<(walk_env.gait.enabled?"true":"false")<<"}\n";
      return true;
    };
    if(options.view_mode=="first-person" && !set_view_mode(terraflux::graphics::CameraMode::FirstPerson,!options.smoke_test && !options.benchmark_frames))
      throw std::runtime_error("--view first-person: no ground to stand on near the camera target");
    if(options.view_mode=="first-person" && options.view_pitch)person.pitch=std::clamp(*options.view_pitch,-85.0,85.0)*kDegrees; // looking up or down from the start
    double previous_time = glfwGetTime();
    terraflux::runtime::FixedStepScheduler scheduler(1.0/60.0,0.25,8);
    terraflux::runtime::ScenePlayback scene_playback;
    bool previous_pause=false,previous_step=false,previous_terrain_key=false,previous_fire_key=false;
    bool previous_vegetation_key=false,previous_tree_key=false,previous_grass_key=false;
    bool previous_cloud_key=false,previous_earlier_key=false,previous_later_key=false,previous_wind_key=false,previous_weather_key=false;
    std::vector<std::uint8_t> vegetation_reference,vegetation_frame,fire_reference,fire_frame;
    std::vector<int> fire_top_row;
    std::vector<std::uint8_t> layers_both,layers_ground,layers_fire,shading_reference,water_reference,water_waves;
    std::vector<std::uint8_t> weather_reference,weather_before_rain,weather_cumulus;
    std::vector<std::uint8_t> view_test_reference,view_test_frame;
    terraflux::graphics::OrbitCamera view_test_orbit;
    terraflux::graphics::FirstPersonCamera view_test_person;
    double view_test_walked=0;
    struct CollisionProbe { terraflux::math::Vec3 start,target; double radius=0,walked=0,requested=0,clearance=0,rise=0; };
    CollisionProbe collision_probe;
    std::vector<std::uint8_t> collision_frame;
    struct JumpProbe { double ground=0,apex=0,air_time=0,dip=0,carried=0,expected_carry=0; bool landed=false; };
    JumpProbe jump_probe;
    std::vector<std::uint8_t> jump_reference;
    std::vector<std::uint8_t> tree_fire_reference,tree_fire_frame;
    std::vector<std::uint8_t> body_reference,body_frame;
    struct ItemsProbe { std::string did; std::size_t stones_present=0; double landed_distance=0; std::uint32_t stone=0; std::size_t branches_burning=0; int tree=-1; };
    struct ToolsProbe { std::string did; int tree=-1; int chops=0, expected_chops=0; double saw_seconds=0, expected_saw_seconds=0, fuel_before=0; double fall_alignment=0; std::size_t branches=0; double crushed=0; double face=0; };
    ToolsProbe tools_probe;
    terraflux::math::Vec3 tools_reference_feet;
    ItemsProbe items_probe;
    struct VitalsProbe { double heat_exposure=0,smoke_exposure=0,impact=0,health_after_fall=1; };
    VitalsProbe vitals_probe;
    std::vector<std::uint8_t> vitals_reference;
    struct ClimbProbe { double start_y=0,rise=0,slope=0; bool climbed=false; std::string surface,refusal; double stamina=1; };
    ClimbProbe climb_probe;
    std::vector<std::uint8_t> climb_reference;
    struct FloodProbe { double x=0,z=0,depth=0,current=0,carried=0,flash=0; bool swept=false,swimming=false; std::size_t triangles=0; };
    FloodProbe flood_probe;
    std::vector<std::uint8_t> flood_reference;
    struct RoadsProbe {
      std::uint32_t bridge=0,road=0; int tree=-1; double on_road_m=0,off_road_m=0,deck_y=0,ground_under_deck=0,offset_after_strafe=0,feet_after_collapse=0,health_before=1;
      std::size_t blockages=0; double damage=0; bool blocked_sample=false,on_platform=false,collapsed_under=false,fell=false,swimming=false;
    };
    RoadsProbe roads_probe;
    std::vector<std::uint8_t> roads_reference;
    terraflux::graphics::OrbitCamera roads_orbit;
    struct CityProbe { std::string zone; int lanes=0; std::size_t towers_near=0; double deck_over_ground=0; bool on_deck=false,avenue_below=false,on_runway=false,runway_wide=false; std::size_t unconnected=0; };
    CityProbe city_probe;
    std::vector<std::uint8_t> city_reference;
    struct BuildingsProbe { std::size_t target=0; double door_distance=0,path_speed=0; bool on_path=false,path_meets_road=false,inside=false,stopped=false; std::string at; };
    BuildingsProbe buildings_probe;
    std::vector<std::uint8_t> buildings_reference;
    struct InteriorProbe { std::size_t target=0; double door_distance=0,floor=0,feet_y=0,table_gap=0,leaf_open=0; bool at_door=false,inside=false,on_floor=false,kept_in=false,out=false; std::string hint,did; };
    InteriorProbe interior_probe;
    struct HazardsProbe { std::size_t bungalow=0,house=0,hospital=0,tower=0; std::string hint,did,destroyed_by,house_roof_cause; double heat=0,flames=0,walls_damage=0,health_lost=0,feet_y=0,floor=0;
                          bool alight=false,roof_fell_on_me=false,destroyed=false,on_rubble=false,house_roof_fell=false,windows_burst=false,house_stands=false,struck=false,washed_out=false,sound=false;
                          std::size_t events=0,trees_lit=0; std::uint64_t version=0; };
    HazardsProbe hazards_probe;
    struct StormProbe { double strike_x=0,strike_z=0,distance=0,stunned=0,shake=0,walked=0,drift=0; int struck_tree=-1; std::size_t windthrown=0; };
    StormProbe storm_probe;
    std::uint64_t tree_fire_obstacle_version_start=0;
    struct GaitProbe { double slowest=1e300,fastest=0,max_bob=0,max_sway=0,min_sway=0,stop_distance=0,release_x=0,release_z=0; std::uint64_t footfalls=0; };
    GaitProbe gait_probe;
    std::vector<std::uint8_t> gait_reference;
    std::vector<bool> weather_sky_mask;
    double weather_day_brightness=0,weather_night_brightness=0,weather_cumulus_wind=0;
    std::vector<bool> water_mask;
    std::vector<std::uint32_t> layers_field_snapshot;
    std::size_t layers_frame_snapshot=0;
    glfwSetWindowTitle(window.get(),(std::string("TerraFlux")+layer_title()).c_str());
    if(!options.smoke_test && !options.benchmark_frames)
      std::cout<<"Layer controls: T terrain | F fire (visibility only; replay continues)\n"
               <<"Vegetation controls: V density | B tree meshes | G grass distance\n"
               <<"View controls: Tab world view <-> first person | W/A/S/D pan the target or walk | Shift run | Space jump (first person) | arrows orbit or look (mouse looks in first person) | +/- zoom or walking speed\n";
    std::string last_scene_title,last_weather_title=weather_title();
    std::optional<terraflux::scene::Scene> expected_controls_scene;
    if(options.scene_controls_test) {
      expected_controls_scene=imported->scene;
      scene_playback.set_paused(true);
    }
    std::optional<terraflux::replay::ReplayClock> replay_clock;
    if(playback){replay_clock.emplace(playback->frames.size());replay_clock->set_speed(options.replay_speed);}
    std::array<bool,5> previous_speed_keys{};
    std::vector<std::uint8_t> replay_initial_pixels,replay_paused_pixels;
    bool layers_playing_snapshot=false;
    bool previous_unload=false,previous_reload=false,previous_detach=false,previous_attach=false,previous_prev_node=false,previous_next_node=false;
    bool previous_space = false, previous_home = false, previous_back = false, previous_forward = false;
    unsigned rendered_frames = 0;
    std::vector<std::uint8_t> screenshot_pixels;
    int screenshot_width=0,screenshot_height=0;
    std::vector<std::uint8_t> lifecycle_terrain;
    terraflux::scene::NodeId residency_node=0;
    std::vector<BufferHandle> residency_handles;
    std::uint64_t profile_frames = 0;
    terraflux::runtime::FrameProfiler cpu_profiler(120);
    constexpr unsigned kBenchmarkWarmup=8;
    double benchmark_cpu_update=0,benchmark_cpu_render=0;
    std::uint64_t benchmark_draws=0,benchmark_visible=0,benchmark_culled=0;
    if(options.profile && !options.benchmark_frames)device.enable_gpu_timing();

    if(playback && options.replay_frame) {
      if(*options.replay_frame>=playback->frames.size())throw std::invalid_argument("--replay-frame is beyond the replay");
      replay_clock->seek(*options.replay_frame); // paused on the requested frame
      playback_frame=replay_clock->frame();
    } else if(playback && options.tree_fire_test) {
      replay_clock->seek(playback->frames.size()/2); // paused mid-replay: a front with trees in it
      playback_frame=replay_clock->frame();
    } else if(playback && options.smoke_test && playback->frames.size()>1 && !options.replay_controls_test) {
      replay_clock->seek(1);replay_clock->set_playing(true);
      playback_frame=replay_clock->frame(); // Exercise non-initial fire state in smoke tests.
    }

    AtmosphereState weather_state;  // of the frame (the previous one's until recomputed)
    while (!glfwWindowShouldClose(window.get())) {
      glfwPollEvents();
      if (glfwGetKey(window.get(), GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
      const bool unload=glfwGetKey(window.get(),GLFW_KEY_U)==GLFW_PRESS;
      const bool reload=glfwGetKey(window.get(),GLFW_KEY_R)==GLFW_PRESS;
      if(has_asset && !options.smoke_test && !options.benchmark_frames) {
        if(unload && !previous_unload)unload_imported();
        if(reload && !previous_reload) {
          try{
            imported.reload(load_imported);
            scene_playback.reset();
            std::cout<<"Asset reloaded\n";
            report_residency();
          }
          catch(const std::exception& error){
            std::cerr<<"Asset reload failed; current asset preserved: "<<error.what()<<'\n';report_residency();
          }
          previous_time=glfwGetTime(); // disk/shader loading isn't simulated time
        }
      }
      const bool prev_node=glfwGetKey(window.get(),GLFW_KEY_LEFT_BRACKET)==GLFW_PRESS;
      const bool next_node=glfwGetKey(window.get(),GLFW_KEY_RIGHT_BRACKET)==GLFW_PRESS;
      if(imported && imported->uses_scene_file && !options.smoke_test && !options.benchmark_frames) {
        if(prev_node && !previous_prev_node)imported->residency.cycle(imported->scene_instances,false);
        if(next_node && !previous_next_node)imported->residency.cycle(imported->scene_instances,true);
        if((prev_node && !previous_prev_node) || (next_node && !previous_next_node))report_residency();
      }
      previous_prev_node=prev_node;previous_next_node=next_node;
      const bool walking=view_mode==terraflux::graphics::CameraMode::FirstPerson;
      const bool detach=!walking && glfwGetKey(window.get(),GLFW_KEY_D)==GLFW_PRESS;
      const bool attach=!walking && glfwGetKey(window.get(),GLFW_KEY_A)==GLFW_PRESS;
      if(imported && imported->uses_scene_file && !options.smoke_test && !options.benchmark_frames) {
        const auto selected=imported->residency.selected(imported->scene_instances);
        if(selected && detach && !previous_detach)detach_node(*selected);
        if(selected && attach && !previous_attach) {
          try{reattach_node(*selected);}
          catch(const std::exception& error){std::cerr<<"Node activation rejected; state preserved: "<<error.what()<<'\n';report_residency();}
          previous_time=glfwGetTime();
        }
      }
      previous_attach=attach;
      previous_detach=detach;
      previous_unload=unload;previous_reload=reload;
      const double now = glfwGetTime();
      const double profile_update_start = glfwGetTime();
      const double elapsed=options.replay_controls_test?0.1:
          (options.benchmark_frames || options.scene_controls_test || options.layers_test ||
           options.water_test || options.terrain_shading_test || options.vegetation_test || options.fire_test ||
           options.first_person_test || options.collision_test || options.jump_test || options.gait_test || options.tree_fire_test ||
           options.storm_test || options.flood_test || options.climb_test || options.vitals_test || options.body_test || options.items_test || options.tools_test ||
           options.roads_test || options.buildings_test || options.interior_test || options.hazards_test || options.city_test)?
          (1.0/60.0):std::max(0.0,now-previous_time);
      const double delta = std::clamp(elapsed, 0.0, 0.1);
      previous_time = now;
      // Clamp large stalls and catch-up work while keeping updates at 60 Hz.
      // Render/input continues once per frame, independent of fixed updates.
      double weather_step=0.0;
      const auto fixed_report=scheduler.advance(elapsed,[&](double dt) {
        engine_time+=dt;
        weather_step+=dt;
        if(replay_clock)replay_clock->tick();
      });
      (void)fixed_report;
      const bool pause_key=options.scene_controls_test?rendered_frames==3:
          glfwGetKey(window.get(),GLFW_KEY_P)==GLFW_PRESS;
      const bool step_key=options.scene_controls_test?rendered_frames==1:
          glfwGetKey(window.get(),GLFW_KEY_N)==GLFW_PRESS;
      if(imported && !imported->is_gltf) {
        const auto update_scene=[&](double dt) {
          if(imported->uses_scene_file)
            terraflux::runtime::update_scene_components(imported->scene,dt);
        };
        const bool controls_enabled=!options.benchmark_frames &&
                                    (!options.smoke_test || options.scene_controls_test);
        if(controls_enabled && step_key && !previous_step) {
          scene_playback.step(update_scene);
          std::cout<<"Scene paused; advanced one 1/60-second tick\n";
        }else{
          if(controls_enabled && pause_key && !previous_pause) {
            scene_playback.set_paused(!scene_playback.paused());
            std::cout<<(scene_playback.paused()?"Scene paused\n":"Scene resumed\n");
          }
          scene_playback.advance(elapsed,update_scene);
        }
      }
      previous_pause=pause_key;previous_step=step_key;
      if(options.scene_controls_test) {
        if(rendered_frames==1 || rendered_frames==3)
          terraflux::runtime::update_scene_components(*expected_controls_scene,1.0/60.0);
        if(terraflux::scene::serialize_scene(imported->scene)!=
           terraflux::scene::serialize_scene(*expected_controls_scene))
          throw std::runtime_error("scene controls changed an unexpected animation tick");
        const double expected_time=(rendered_frames==0?0:rendered_frames==3?2:1)/60.0;
        if(std::abs(scene_playback.simulated_seconds()-expected_time)>1e-9 ||
           scene_playback.paused()!=(rendered_frames!=3) ||
           std::abs(engine_time-(rendered_frames+1)/60.0)>1e-9)
          throw std::runtime_error("scene clock pause/step affected independent engine time");
      }

      {
        // V cycles vegetation density, B the tree mesh style, G the grass distance.
        const bool vegetation_key=glfwGetKey(window.get(),GLFW_KEY_V)==GLFW_PRESS;
        const bool tree_key=glfwGetKey(window.get(),GLFW_KEY_B)==GLFW_PRESS;
        const bool grass_key=glfwGetKey(window.get(),GLFW_KEY_G)==GLFW_PRESS;
        if(!options.smoke_test && !options.benchmark_frames) {
          try {
            if(vegetation_key && !previous_vegetation_key) {
              vegetation_level=vegetation_level=="off"?"low":vegetation_level=="low"?"medium":vegetation_level=="medium"?"high":"off";
              apply_vegetation_level(vegetation_level);rebuild_vegetation();rebuild_tree_fire();report_vegetation("level");
              std::cout<<"Vegetation: "<<vegetation_level<<'\n';
            }
            if(tree_key && !previous_tree_key && vegetation) {
              tree_style=tree_style=="conifer"?"broadleaf":tree_style=="broadleaf"?"mixed":tree_style=="mixed"?(custom_tree?"custom":"conifer"):"conifer";
              vegetation->set_tree_meshes(tree_meshes_for(tree_style));rebuild_tree_obstacles();rebuild_tree_fire();report_vegetation("tree_style");
              std::cout<<"Tree meshes: "<<tree_style<<'\n';
            }
            if(grass_key && !previous_grass_key) {
              const double d=vegetation_style.grass_distance_meters;
              vegetation_style.grass=true;
              vegetation_style.grass_distance_meters=d<=0?150:d<=150?300:d<=300?600:0;
              if(vegetation_style.grass_distance_meters<=0)vegetation_style.grass=false;
              rebuild_vegetation();rebuild_tree_fire();report_vegetation("grass");
              std::cout<<"Grass distance: "<<vegetation_style.grass_distance_meters<<" m\n";
            }
          } catch(const std::exception& error) {
            std::cerr<<"Vegetation change rejected; previous state preserved: "<<error.what()<<'\n';
          }
          if((vegetation_key && !previous_vegetation_key) || (tree_key && !previous_tree_key) || (grass_key && !previous_grass_key))
            previous_time=glfwGetTime();
        }
        previous_vegetation_key=vegetation_key;previous_tree_key=tree_key;previous_grass_key=grass_key;
      }
      bool weather_changed=false;
      {
        // Weather controls: C cycles the cloud form, J/K move the solar clock
        // an hour back/forward, X cycles wind presets, O toggles the weather.
        const bool cloud_key=glfwGetKey(window.get(),GLFW_KEY_C)==GLFW_PRESS;
        const bool earlier_key=glfwGetKey(window.get(),GLFW_KEY_J)==GLFW_PRESS;
        const bool later_key=glfwGetKey(window.get(),GLFW_KEY_K)==GLFW_PRESS;
        const bool wind_key=glfwGetKey(window.get(),GLFW_KEY_X)==GLFW_PRESS;
        const bool weather_key=glfwGetKey(window.get(),GLFW_KEY_O)==GLFW_PRESS;
        if(!options.smoke_test && !options.benchmark_frames && sky) {
          auto config=weather.config();
          if(cloud_key && !previous_cloud_key) {
            config.clouds=static_cast<CloudType>((static_cast<std::size_t>(config.clouds)+1)%kCloudTypeCount);
            config.coverage.reset();config.cloud_base_meters.reset();
            std::cout<<"Clouds: "<<cloud_type_name(config.clouds)<<" - "<<cloud_profile(config.clouds).expected_weather<<'\n';
            weather_changed=true;
          }
          if((earlier_key && !previous_earlier_key) || (later_key && !previous_later_key)) {
            const double hour=weather.sun().hour+(later_key && !previous_later_key?1.0:-1.0);
            // Move the start hour so the running clock lands on the new hour.
            const double elapsed_hours=config.daylight.seconds_per_hour>0?weather.time()/config.daylight.seconds_per_hour:0.0;
            config.daylight.start_hour=hour-24.0*std::floor(hour/24.0);
            config.daylight.start_hour-=elapsed_hours-24.0*std::floor(elapsed_hours/24.0);
            if(config.daylight.start_hour<0)config.daylight.start_hour+=24.0;
            weather_changed=true;
          }
          if(wind_key && !previous_wind_key) {
            constexpr std::array<std::pair<double,double>,4> presets{{{0.0,1.0},{2.0,6.0},{6.0,12.0},{12.0,24.0}}};
            std::size_t next=0;
            for(std::size_t i=0;i<presets.size();++i)if(std::abs(config.wind.speed_max_mps-presets[i].second)<1e-9)next=(i+1)%presets.size();
            config.wind.speed_min_mps=presets[next].first;config.wind.speed_max_mps=presets[next].second;
            std::cout<<"Wind: "<<config.wind.speed_min_mps<<".."<<config.wind.speed_max_mps<<" m/s\n";
            weather_changed=true;
          }
          if(weather_key && !previous_weather_key){config.enabled=!config.enabled;weather_changed=true;}
          if(weather_changed) {
            try{weather.set_config(config);report_weather("changed");}
            catch(const std::exception& error){std::cerr<<"Weather change rejected; previous state preserved: "<<error.what()<<'\n';}
            previous_time=glfwGetTime();
          }
        }
        previous_cloud_key=cloud_key;previous_earlier_key=earlier_key;previous_later_key=later_key;
        previous_wind_key=wind_key;previous_weather_key=weather_key;
        // Smoke runs freeze the weather clock (the scripted test drives it);
        // interactive runs integrate cloud drift with the engine clock.
        if(!options.smoke_test)weather.advance(weather_step);
        // The flood wave follows the rain of the moment on the weather's accelerated clock.
        if(!options.smoke_test && weather.config().enabled && weather_step>0)
          flood.advance(weather_step*weather.clock_acceleration(),last_rain_mm_h);
        else if(!options.smoke_test && !weather.config().enabled && weather_step>0)flood.advance(weather_step*60.0,0.0);
        if(weather.config().enabled) {
          // The gusting wind steers the plants, the flames and the smoke, and leans on the walker.
          const auto wind=weather.wind();
          vegetation_style.wind_direction_degrees=wind.direction_degrees;
          vegetation_style.wind_speed_mps=std::min(30.0,wind.speed_mps);
          fire_style.wind_direction_degrees=wind.direction_degrees;
          fire_style.wind_speed_mps=std::min(30.0,wind.speed_mps);
          walk_env.wind_velocity_mps={wind.travel.x*wind.speed_mps,wind.travel.y*wind.speed_mps};
        } else walk_env.wind_velocity_mps={};
        // Without a replay the trees live on the engine clock: a storm's gusts and
        // strikes act on them in real time.
        if(tree_fire && !playback && !options.storm_test && weather_step>0) {
          const double wind_speed=weather.config().enabled?std::min(60.0,weather.wind().speed_mps):fire_style.wind_speed_mps;
          const double wind_dir=weather.config().enabled?weather.wind().direction_degrees:fire_style.wind_direction_degrees;
          tree_fire->advance(weather_step,wind_speed,wind_dir);
          if(view_mode==terraflux::graphics::CameraMode::FirstPerson)trunks_landing();
          logs_on_roads();
        }
      }
      // The roads live on the engine clock too (scripted road tests drive them themselves); the doors swing on it.
      if(!options.roads_test)advance_roads(weather_step);
      if(!options.hazards_test)advance_buildings(weather_step);
      {
        // The title's clock and wind follow the simulation (once a minute of solar time).
        const auto current=weather_title();
        if(current!=last_weather_title){last_weather_title=current;weather_changed=true;}
      }
      bool view_changed=false;
      {
        // Tab switches between the world view (orbit) and first person and back.
        const bool view_key=glfwGetKey(window.get(),GLFW_KEY_TAB)==GLFW_PRESS;
        if(view_key && !previous_view_key && !options.smoke_test && !options.benchmark_frames) {
          using terraflux::graphics::CameraMode;
          if(set_view_mode(walking?CameraMode::World:CameraMode::FirstPerson,true)) {
            view_changed=true;
            std::cout<<(view_mode==CameraMode::FirstPerson?
                "View: first person | W/A/S/D walk | Shift run | Space jump | arrows or mouse look | +/- walking speed | Tab world view\n":
                "View: world | W/A/S/D pan | arrows orbit | +/- zoom | Tab first person\n");
          }
          previous_time=glfwGetTime();
        }
        previous_view_key=view_key;
      }
      const bool terrain_key=glfwGetKey(window.get(),GLFW_KEY_T)==GLFW_PRESS;
      const bool fire_key=glfwGetKey(window.get(),GLFW_KEY_F)==GLFW_PRESS;
      const bool layer_changed=(terrain_key && !previous_terrain_key) || (fire_key && !previous_fire_key);
      if(terrain_key && !previous_terrain_key)layer_visibility.terrain=!layer_visibility.terrain;
      if(fire_key && !previous_fire_key)layer_visibility.fire=!layer_visibility.fire;
      previous_terrain_key=terrain_key;previous_fire_key=fire_key;
      if(options.layers_test) {
        constexpr std::array<LayerVisibility,5> states{{{true,true},{true,false},{false,true},{false,false},{true,true}}};
        layer_visibility=states.at(rendered_frames);
      }
      if(layer_changed || weather_changed || view_changed)glfwSetWindowTitle(window.get(),(std::string("TerraFlux")+layer_title()).c_str());
      if(options.terrain_shading_test) {
        // Frames 0 and 2: base materials without contours or detail (CPU reference
        // parity). Frame 1: eight contour levels. Frame 3: full procedural detail.
        auto staged=terrain_style;
        staged.contour_interval_meters=0;staged.detail_strength=0;
        const double range=terrain_style.max_elevation_meters-terrain_style.min_elevation_meters;
        if(!(range>0))throw std::runtime_error("terrain shading test requires terrain with relief");
        if(rendered_frames==1)staged.contour_interval_meters=range/8.0;
        if(rendered_frames==3)staged.detail_strength=1.0;
        rendered_rock_relief=staged.detail_strength*staged.rock_relief_meters;
        const auto packed=pack_terrain_style(staged);
        device.update_buffer(style_buffer,0,std::as_bytes(std::span(packed)));
      }
      const bool space = glfwGetKey(window.get(), GLFW_KEY_SPACE) == GLFW_PRESS;
      const bool home = glfwGetKey(window.get(), GLFW_KEY_HOME) == GLFW_PRESS;
      const bool back = glfwGetKey(window.get(), GLFW_KEY_COMMA) == GLFW_PRESS;
      const bool forward = glfwGetKey(window.get(), GLFW_KEY_PERIOD) == GLFW_PRESS;
      if(replay_clock) {
        if(space && !previous_space && !walking)replay_clock->toggle(); // in first person Space jumps
        if(home && !previous_home)replay_clock->seek(0);
        if(back && !previous_back)replay_clock->step_back();
        if(forward && !previous_forward)replay_clock->step_forward();
        constexpr std::array<double,5> speeds{0.25,0.5,1,2,4};
        bool speed_changed=false;
        for(std::size_t i=0;i<speeds.size();++i) {
          const bool pressed=glfwGetKey(window.get(),GLFW_KEY_1+static_cast<int>(i))==GLFW_PRESS;
          if(pressed && !previous_speed_keys[i]){replay_clock->set_speed(speeds[i]);speed_changed=true;}
          previous_speed_keys[i]=pressed;
        }
        if(options.replay_controls_test) {
          switch(rendered_frames) {
            case 0:replay_clock->seek(0);replay_clock->set_speed(1);replay_clock->set_playing(true);break;
            case 1:replay_clock->set_speed(4);break;
            case 2:replay_clock->set_playing(false);break;
            case 3:replay_clock->step_forward();break;
            case 4:replay_clock->step_back();break;
            case 5:replay_clock->toggle();break;
            case 6:replay_clock->seek(0);break;
          }
          constexpr std::array<std::size_t,7> frames{0,0,1,2,1,1,0};
          constexpr std::array<bool,7> running{true,true,false,false,false,true,false};
          if(replay_clock->frame()!=frames.at(rendered_frames) || replay_clock->playing()!=running.at(rendered_frames) ||
             std::abs(replay_clock->pending_seconds()-(rendered_frames==1?0.1:0.0))>1e-9 ||
             replay_clock->speed()!=(rendered_frames?4:1) || std::abs(engine_time-(rendered_frames+1)*0.1)>1e-9)
            throw std::runtime_error("replay control sequence changed expected timing or engine independence");
        }
        playback_frame=replay_clock->frame();
        const bool frame_changed=playback_frame!=uploaded_frame;
        // The tree fire follows the replay's own time: it advances while the replay
        // plays (scaled by its speed), holds while paused, and starts over from the
        // field when the replay is wound back.
        const double replay_seconds=static_cast<double>(playback_frame)*0.35+replay_clock->pending_seconds();
        const bool wound_back=replay_seconds_seen>=0 && replay_seconds<replay_seconds_seen-1e-9;
        if(wound_back && tree_fire){tree_fire->reset();}
        if(frame_changed) {
          terraflux::replay::apply_frame(playback->frames[playback_frame],*fire_field);
          device.update_buffer(*fire_buffer,0,std::as_bytes(std::span(fire_field->states)));
          fire_emitters_dirty=true;
          uploaded_frame=playback_frame;
          if(replay_weather){apply_replay_weather(playback_frame);if(options.smoke_test || options.profile)report_weather("replay");}
        }
        if(tree_fire && (frame_changed || wound_back)) {
          if(wound_back){
            // Rebuild the field from scratch so trees in cells burned before the seek settle as burned out.
            *fire_field=terraflux::replay::make_fire_field(grid);
            for(std::size_t i=0;i<=playback_frame;++i)terraflux::replay::apply_frame(playback->frames[i],*fire_field);
          }
          tree_fire_sync(wound_back);
          if(options.smoke_test || options.profile)report_tree_fire(wound_back?"seek":"frame");
        }
        if(tree_fire && !options.tree_fire_test && !options.city_test) {  // the city test holds the fire so its reference frame is restorable
          const double dt_replay=replay_seconds_seen<0 || wound_back?0.0:std::max(0.0,replay_seconds-replay_seconds_seen);
          const double wind_speed=weather.config().enabled?std::min(30.0,weather.wind().speed_mps):fire_style.wind_speed_mps;
          const double wind_dir=weather.config().enabled?weather.wind().direction_degrees:fire_style.wind_direction_degrees;
          if(dt_replay>0){tree_fire->advance(dt_replay,wind_speed,wind_dir);if(view_mode==terraflux::graphics::CameraMode::FirstPerson)trunks_landing();logs_on_roads();}
        }
        replay_seconds_seen=replay_seconds;
        if(frame_changed || speed_changed || options.smoke_test || weather_changed || view_changed || (space && !previous_space) ||
           (home && !previous_home) || (back && !previous_back) || (forward && !previous_forward)) {
          const auto& current=playback->frames[playback_frame];
          std::string title="TerraFlux - "+playback->grid_key+" | step "+std::to_string(current.step+1)+"/"+
              std::to_string(playback->frames.size())+" | "+replay_clock->speed_label()+
              (replay_clock->playing()?" playing":" paused")+
              " | active "+std::to_string(current.active.size())+" | burned "+std::to_string(current.burned.size())+
              " | Space pause | Home reset | , . step | 1-5 speed | arrows orbit | +/- zoom"+layer_title();
          glfwSetWindowTitle(window.get(),title.c_str());
          if(options.smoke_test || options.profile || options.residency_report)
            std::cout<<"REPLAY_JSON={\"frame\":"<<playback_frame<<",\"playing\":"<<(replay_clock->playing()?"true":"false")
                     <<",\"speed\":"<<replay_clock->speed()<<",\"pending_seconds\":"<<replay_clock->pending_seconds()<<"}\n";
        }
      }
      previous_space = space; previous_home = home;
      previous_back = back; previous_forward = forward;

      if(imported && !imported->is_gltf && !options.smoke_test && !options.benchmark_frames) {
        std::string title=std::string("TerraFlux | Scene ")+
            (scene_playback.paused()?"paused":"playing")+" | P pause/resume | N tick | D detach | A reattach | U unload | R reload";
        if(imported->uses_scene_file) {
          const auto selected=imported->residency.selected(imported->scene_instances);
          if(selected)title+=" | Node "+std::to_string(*selected)+": "+imported->scene.get(*selected).name+
              (imported->residency.detached().contains(*selected)?" (inactive)":" (active)")+" | [ ] select";
        }
        if(playback)title+=" | Fire "+std::to_string(playback_frame+1)+"/"+
            std::to_string(playback->frames.size())+" | "+replay_clock->speed_label()+" | 1-5 speed | Space pause fire";
        title+=layer_title();
        if(title!=last_scene_title) {
          glfwSetWindowTitle(window.get(),title.c_str());
          last_scene_title=std::move(title);
        }
      }

      const bool interactive_camera=!options.smoke_test && !options.benchmark_frames;
      if(view_mode==terraflux::graphics::CameraMode::FirstPerson) {
        // First person: W/A/S/D walk and strafe along the look direction over
        // the ground (Shift runs), arrows and the mouse look around, +/- change
        // the walking speed. Holes and the terrain edge stop you; the feet follow
        // the ground, the water surface and a rising flood.
        terraflux::graphics::WalkInput input;
        if(glfwGetKey(window.get(),GLFW_KEY_W)==GLFW_PRESS)input.forward+=1;
        if(glfwGetKey(window.get(),GLFW_KEY_S)==GLFW_PRESS)input.forward-=1;
        if(glfwGetKey(window.get(),GLFW_KEY_D)==GLFW_PRESS)input.strafe+=1;
        if(glfwGetKey(window.get(),GLFW_KEY_A)==GLFW_PRESS)input.strafe-=1;
        input.run=glfwGetKey(window.get(),GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS || glfwGetKey(window.get(),GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS;
        const bool jump_key=glfwGetKey(window.get(),GLFW_KEY_SPACE)==GLFW_PRESS;
        input.jump=jump_key && !previous_jump_key;
        input.crouch=glfwGetKey(window.get(),GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS || glfwGetKey(window.get(),GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS;
        // 1/2/3 take the rope, the axe or the chainsaw in hand (again: away).
        const bool tool_keys[3]={glfwGetKey(window.get(),GLFW_KEY_1)==GLFW_PRESS,glfwGetKey(window.get(),GLFW_KEY_2)==GLFW_PRESS,glfwGetKey(window.get(),GLFW_KEY_3)==GLFW_PRESS};
        if(interactive_camera && person.stunned_seconds<=0) {
          static const terraflux::graphics::ToolKind kinds[3]={terraflux::graphics::ToolKind::Rope,terraflux::graphics::ToolKind::Axe,terraflux::graphics::ToolKind::Chainsaw};
          for(int k=0;k<3;++k)if(tool_keys[k] && !previous_tool_keys[k]) {
            if(k==0 && pull_rope_in()){std::cout<<"You pulled the rope in\n";continue;}
            const auto now=belt.select(kinds[k]);
            std::cout<<(now==terraflux::graphics::ToolKind::None?std::string("You put the ")+terraflux::graphics::tool_kind_name(kinds[k])+" away":
                std::string("You take the ")+terraflux::graphics::tool_kind_name(now)+(held.kind!=terraflux::graphics::HeldKind::Nothing?" (the "+std::string(terraflux::graphics::held_kind_name(held.kind))+" in the left hand)":""))<<'\n';
          }
        }
        for(int k=0;k<3;++k)previous_tool_keys[k]=tool_keys[k];
        // E uses the tool in hand, else interacts with what is in reach; Q throws what is held.
        const bool interact_key=glfwGetKey(window.get(),GLFW_KEY_E)==GLFW_PRESS,throw_key=glfwGetKey(window.get(),GLFW_KEY_Q)==GLFW_PRESS;
        if(interactive_camera && person.stunned_seconds<=0) {
          if(interact_key && !previous_interact_key) {
            std::string what;
            if(person.on_rope){if(rope.branch_tree<0 && person.rope_t_meters<=1e-6 && terraflux::graphics::step_off_rope_top(person,rope,walk_env,rope_rules))what="stepped off the rope onto the edge";}
            else {what=use_tool();if(what.empty())what=interact();}
            if(!what.empty())std::cout<<(std::isupper(static_cast<unsigned char>(what[0]))?"":"You ")<<what<<'\n';
          }
          if(throw_key && !previous_throw_key && !person.on_rope){const auto what=throw_held();if(!what.empty())std::cout<<"You "<<what<<'\n';}
        }
        previous_interact_key=interact_key;previous_throw_key=throw_key;
        previous_jump_key=jump_key;
        double turn=0,tilt=0;
        if(glfwGetKey(window.get(),GLFW_KEY_LEFT)==GLFW_PRESS)turn-=delta;
        if(glfwGetKey(window.get(),GLFW_KEY_RIGHT)==GLFW_PRESS)turn+=delta;
        if(glfwGetKey(window.get(),GLFW_KEY_UP)==GLFW_PRESS)tilt+=delta;
        if(glfwGetKey(window.get(),GLFW_KEY_DOWN)==GLFW_PRESS)tilt-=delta;
        if(interactive_camera) {
          double cursor_x=0,cursor_y=0;
          glfwGetCursorPos(window.get(),&cursor_x,&cursor_y);
          if(mouse_look_tracking && std::isfinite(cursor_x) && std::isfinite(cursor_y)) {
            constexpr double kRadiansPerPixel=0.0025;
            turn+=(cursor_x-mouse_look_x)*kRadiansPerPixel;
            tilt-=(cursor_y-mouse_look_y)*kRadiansPerPixel;
          }
          mouse_look_x=cursor_x;mouse_look_y=cursor_y;mouse_look_tracking=true;
          if(turn!=0 || tilt!=0)terraflux::graphics::look(person,turn,tilt);
          double octaves=0;
          if(glfwGetKey(window.get(),GLFW_KEY_EQUAL)==GLFW_PRESS || glfwGetKey(window.get(),GLFW_KEY_KP_ADD)==GLFW_PRESS)octaves+=delta;
          if(glfwGetKey(window.get(),GLFW_KEY_MINUS)==GLFW_PRESS || glfwGetKey(window.get(),GLFW_KEY_KP_SUBTRACT)==GLFW_PRESS)octaves-=delta;
          if(octaves!=0)terraflux::graphics::adjust_walk_speed(person,octaves);
          if(person.on_rope) {
            // On the rope: W up, S down, Space lets go; at the top of a pegged rope W steps off onto the edge.
            const auto r=terraflux::graphics::climb_rope(person,rope,input,delta,walk_env,rope_rules);
            if(r.let_go)std::cout<<"You let go of the rope\n";
            if(r.stepped_off)std::cout<<(r.sliding?"You slid down the rope to the ground\n":"You stepped off the rope\n");
            if(r.at_top && input.forward>0 && rope.branch_tree<0 && terraflux::graphics::step_off_rope_top(person,rope,walk_env,rope_rules))std::cout<<"You stepped off the rope onto the edge\n";
          } else terraflux::graphics::walk(person,input,delta,walk_env);
          if(held.kind==terraflux::graphics::HeldKind::Torch){held.torch_seconds-=delta;if(held.torch_seconds<=0){held.kind=terraflux::graphics::HeldKind::Branch;std::cout<<"The torch has burned out\n";}}
          land_projectiles(delta);
          {
            const auto say=[&](const std::string& what){if(what.empty())return;std::cout<<(std::isupper(static_cast<unsigned char>(what[0]))?"":"You ")<<what<<'\n';tool_message=what;};
            say(advance_tools(delta));
            say(saw_step(interact_key && person.stunned_seconds<=0 && !person.on_rope,delta));
          }
          interact_hint=belt.selected!=terraflux::graphics::ToolKind::None || person.on_rope?tool_hint():interact_options();
          if(interact_hint.empty() && belt.selected!=terraflux::graphics::ToolKind::None)interact_hint=interact_options();
          if(person.swept) {
            lightning_message="SWEPT AWAY by the flood: "+std::to_string(static_cast<int>(std::lround(person.current_mps*10)))+" dm/s current, "+
                std::to_string(static_cast<int>(std::lround(person.water_depth_meters*100)))+" cm deep";
            lightning_message_until=engine_time+6.0;
            std::cout<<lightning_message<<'\n';
          }
          // Standing still the feet still follow the surface (a flood rising, the
          // crag relief changing) and stay clear of assets that animate into you.
          if(!person.on_rope)(void)terraflux::graphics::settle(person,walk_env);
        }
      } else {
        // World view: W/A/S/D pan the orbit target over the ground (faster when
        // zoomed out); the target height follows the terrain so the camera can
        // walk among the vegetation. Home/End are unaffected; the framing distance is kept.
        const double step=std::max(5.0,distance*0.6)*delta;
        const auto forward=terraflux::graphics::horizontal_forward(yaw),right=terraflux::graphics::horizontal_right(yaw);
        terraflux::math::Vec3 pan{};
        if(glfwGetKey(window.get(),GLFW_KEY_W)==GLFW_PRESS)pan=pan+forward*step;
        if(glfwGetKey(window.get(),GLFW_KEY_S)==GLFW_PRESS)pan=pan-forward*step;
        if(glfwGetKey(window.get(),GLFW_KEY_D)==GLFW_PRESS && !(imported && imported->uses_scene_file))pan=pan+right*step;
        if(glfwGetKey(window.get(),GLFW_KEY_A)==GLFW_PRESS && !(imported && imported->uses_scene_file))pan=pan-right*step;
        if(pan.length()>0 && interactive_camera)scene_camera.target=snap_orbit_target(scene_camera.target+pan);
        if (glfwGetKey(window.get(), GLFW_KEY_LEFT) == GLFW_PRESS) yaw -= delta;
        if (glfwGetKey(window.get(), GLFW_KEY_RIGHT) == GLFW_PRESS) yaw += delta;
        if (glfwGetKey(window.get(), GLFW_KEY_UP) == GLFW_PRESS) pitch += delta;
        if (glfwGetKey(window.get(), GLFW_KEY_DOWN) == GLFW_PRESS) pitch -= delta;
        pitch = std::clamp(pitch, 0.1, 1.48);
        if (glfwGetKey(window.get(), GLFW_KEY_EQUAL) == GLFW_PRESS ||
            glfwGetKey(window.get(), GLFW_KEY_KP_ADD) == GLFW_PRESS)
          distance -= scene_camera.starting_distance * 0.65 * delta;
        if (glfwGetKey(window.get(), GLFW_KEY_MINUS) == GLFW_PRESS ||
            glfwGetKey(window.get(), GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS)
          distance += scene_camera.starting_distance * 0.65 * delta;
        // Zoom in far enough to walk among the trees and grass (3 % of the start
        // distance, at least 25 m) and out to four times the framing distance.
        distance = std::clamp(distance, std::max(25.0, scene_camera.starting_distance * 0.03),
                             scene_camera.starting_distance * 4.0);
      }

      int width = 0, height = 0;
      glfwGetFramebufferSize(window.get(), &width, &height);
      if (width <= 0 || height <= 0) {
        if (options.smoke_test) throw std::runtime_error("no framebuffer in smoke test");
        continue;
      }
      const RenderTargetDesc window_target{static_cast<std::uint32_t>(width),
                                           static_cast<std::uint32_t>(height)};
      if(options.offscreen) {
        const ResizeAttempt attempt{width,height,device.live_resource_bytes().total(),
                                    device.resource_telemetry().limit_bytes};
        if(!failed_resize || *failed_resize!=attempt) {
          try {
            if(offscreen.resize(window_target.width,window_target.height)) {
              failed_resize.reset();
              if(options.smoke_test || options.residency_report || options.profile)
                std::cout<<"OFFSCREEN_JSON={\"width\":"<<width<<",\"height\":"<<height
                         <<",\"resize_failures\":"<<resize_failures<<",\"fallback\":false}\n";
              report_residency();
            }
          } catch(const std::exception& error) {
            ++resize_failures;failed_resize=attempt;
            const auto previous=offscreen.target();
            std::cerr<<"Offscreen resize failed: "<<error.what()
                     <<(previous?"; retaining previous target\n":"; rendering directly to window\n");
            std::cout<<"OFFSCREEN_JSON={\"width\":"<<(previous?previous->width:0)
                     <<",\"height\":"<<(previous?previous->height:0)
                     <<",\"resize_failures\":"<<resize_failures<<",\"fallback\":true}\n";
            report_residency();
          }
        }
      }
      const auto scene_target=offscreen.target().value_or(window_target);
      const int scene_width=static_cast<int>(scene_target.width);
      const int scene_height=static_cast<int>(scene_target.height);
      if(options.benchmark_frames && rendered_frames==kBenchmarkWarmup)
        device.enable_gpu_timing();
      const bool record_gpu=options.profile || (options.benchmark_frames &&
                                rendered_frames>=kBenchmarkWarmup);
      const double profile_render_start=glfwGetTime();
      std::uint32_t frame_draws=0;
      if(options.terrain_retention_test) {
        const bool close=rendered_frames==1 || rendered_frames>=3;
        distance=scene_camera.starting_distance*(close?0.2:1.0);
        yaw=0.45;pitch=close?0.55:0.75;
      }
      if(options.terrain_residency_test) {
        const bool close=rendered_frames==1 || rendered_frames==3;
        distance=scene_camera.starting_distance*(close?0.2:1.0);
        yaw=rendered_frames==3?1.95:0.45;pitch=close?0.55:0.75;
      }
      if(options.vegetation_test) {
        // Close enough to the ground that grass tufts are inside their draw distance.
        distance=scene_camera.starting_distance*0.12;yaw=0.4;pitch=0.3;
      }
      if(options.weather_test) {
        // Low enough that the sky fills the upper part of the frame. Frames:
        // 0 weather off (reference), 1 clear noon, 2 clear midnight, 3 clear
        // civil twilight, 4 stratus at noon (overcast, drizzle, summit in
        // cloud), 5 cumulus at t = 0, 6 cumulus 120 s later, 7 a thunderstorm
        // at the moment of a strike over the middle of the terrain, 8 four hours
        // of nimbostratus rain (puddles, runoff and a flood rise), 9 the same rain
        // in freezing air (snow and snow cover), 10 weather off.
        distance=scene_camera.starting_distance*0.3;yaw=1.05;pitch=0.42;
        auto config=weather_config;
        config.enabled=rendered_frames>=1 && rendered_frames<=9;
        config.daylight.seconds_per_hour=rendered_frames>=8?10.0:0.0;
        config.clouds=rendered_frames==4?CloudType::Stratus:rendered_frames==7?CloudType::Cumulonimbus:rendered_frames>=8?CloudType::Nimbostratus:
                      (rendered_frames>=5?CloudType::Cumulus:CloudType::Clear);
        if(rendered_frames==9)config.temperature_celsius=-3.0;
        config.daylight.start_hour=12;
        if(rendered_frames==2){config.daylight.start_hour=0;config.daylight.moon_phase=0.5;} // a full moon due south
        if(rendered_frames==3) {
          // Bisect the evening hour at which the sun stands three degrees below the horizon.
          double lo=12,hi=24;
          for(int i=0;i<60;++i){const double mid=0.5*(lo+hi);if(sun_for_hour(config.daylight,mid).altitude_degrees>-3)lo=mid;else hi=mid;}
          config.daylight.start_hour=0.5*(lo+hi);
        }
        weather.set_config(config);
        weather.reset();
        if(rendered_frames==6)weather.advance(120.0);
        if(rendered_frames==8){for(int i=0;i<2400;++i)weather.advance(0.1);weather.set_time(0.0);} // four weather hours of rain, frozen sun
        if(rendered_frames==9){for(int i=0;i<2400;++i)weather.advance(0.1);weather.set_time(0.0);} // four weather hours of snow
        if(rendered_frames==7) {
          // The first bright strike that lands near the middle of the terrain.
          double strike_time=-1;
          for(double t=0.05;t<3600 && strike_time<0;t+=0.05) {
            const auto strike=lightning_strike_at(t,1.0,config.seed);
            if(strike.active && strike.intensity>0.85 && std::abs(strike.u-0.5)<0.12 && std::abs(strike.v-0.5)<0.12)strike_time=t;
          }
          if(strike_time<0)throw std::runtime_error("no central lightning strike within an hour of storm");
          weather.set_time(strike_time);
        }
        // Frames 1..3 compare against the CPU lighting twin and frame 4 against
        // frame 1's brightness: plain materials without contours or detail.
        auto staged=terrain_style;
        if(rendered_frames>=1 && rendered_frames<=4){staged.contour_interval_meters=0;staged.detail_strength=0;}
        rendered_rock_relief=staged.detail_strength*staged.rock_relief_meters;
        const auto packed=pack_terrain_style(staged);
        device.update_buffer(style_buffer,0,std::as_bytes(std::span(packed)));
      }
      if(options.terrain_tile_test && rendered_frames>0) {
        distance=scene_camera.starting_distance*0.2;
        yaw=rendered_frames==1?0.45:1.95;pitch=0.55;
      }
      // Scripted walking tests need dry, open ground: the nearest point to the
      // centre with ground, no water and no trunk within arm's reach.
      const auto stand_on_dry_ground=[&]() {
        for(double radius=0;radius<=800;radius+=10) {
          const int n=radius<1?1:16;
          for(int k=0;k<n;++k) {
            const double a=k*2.0*3.14159265358979323846/n;
            const double x=scene_camera.target.x+radius*std::cos(a),z=scene_camera.target.z+radius*std::sin(a);
            const auto g=ground_height(x,z);
            if(!g)continue;
            if(walk_env.water(x,z,*g).depth_meters>0)continue;
            const auto trunk=tree_field.clearance(x,z,2.0);
            if(trunk && *trunk<1.0)continue;
            person.feet={x,*g,z};person.grounded=true;
            if(terraflux::graphics::settle(person,walk_env) && person.water_depth_meters==0)return true;
          }
        }
        return false;
      };
      // What the walker is exposed to this frame, and what it costs.
      const auto gather_exposure=[&]()->terraflux::graphics::Exposure {
        terraflux::graphics::Exposure e;
        e.temperature_celsius=weather_state.enabled?weather_state.temperature_celsius:18.0;
        e.wind_mps=weather_state.enabled?std::min(100.0,weather_state.wind.speed_mps):0.0;
        e.rain_mm_h=last_rain_mm_h;
        e.water_depth_meters=std::max(0.0,person.water_depth_meters);
        e.swimming=person.swimming;e.exhausted=person.exhausted;e.crouched=person.crouched;
        double heat=0,smoke=0;
        if(tree_fire){const auto x=tree_fire->exposure_at(person.feet.x,person.feet.z,fire_style.wind_direction_degrees);heat=x.heat;smoke=x.smoke;}
        if(buildings){const auto x=buildings->exposure_at(person.feet.x,person.feet.z);heat=std::max(heat,x.heat);smoke=std::min(1.0,smoke+x.smoke);e.sheltered=buildings->sheltered(person.feet.x,person.feet.z);}
        if(fire_field) {
          const auto front=fire_front_at(*fire_field,mesh.origin_grid_x,mesh.origin_grid_y,cell_size,person.feet.x,person.feet.z);
          if(fire_front_burning(front,fire_edge_noise(person.feet.x,person.feet.z))){heat=std::max(heat,0.8);smoke=std::max(smoke,0.5);}
        }
        if(held.kind==terraflux::graphics::HeldKind::Torch)heat=std::max(heat,0.18);  // a torch in the hand warms, never burns
        if(person.underwater()){heat=0.0;smoke=0.0;}  // under the surface neither the flames nor the smoke reach you
        vitals_heat_here=heat;vitals_smoke_here=smoke;
        e.fire_heat=heat;e.smoke=smoke;
        e.lightning=pending_lightning;pending_lightning=terraflux::graphics::LightningHazard::None;
        e.fall_impact_mps=person.landed?person.last_impact_mps:0.0;
        e.crushed=std::clamp(pending_crush,0.0,1.0);pending_crush=0.0;
        e.collapse=std::clamp(pending_collapse,0.0,1.0);pending_collapse=0.0;
        return e;
      };
      const auto apply_vitals=[&](const terraflux::graphics::VitalsReport& report) {
        if(report.collapsed && collapse_message.empty()) {
          collapse_message=std::string("You collapsed (")+terraflux::graphics::damage_cause_name(vitals.last_cause)+"). Tab to the world view and back to start again";
          std::cout<<collapse_message<<'\n';
          terraflux::graphics::knock_down(person,600.0,1.2);
          lightning_message=collapse_message;lightning_message_until=engine_time+600.0;
        }
      };
      if(options.vitals_test) {
        // Frames: 0 fresh on dry ground in a mild clear day (reference, the bars
        // drawn); 1 two metres from a tree set alight: five seconds of heat and
        // smoke burn the health; 2 half a minute of freezing rain, wind and
        // water: soaked, the body heat gone toward hypothermia; 3 a twelve-metre
        // fall and a direct strike: collapsed; 4 out to the world view and back:
        // a fresh body.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        if(rendered_frames==0) {
          auto config=weather_config;config.enabled=true;config.clouds=CloudType::Clear;config.daylight.start_hour=12;config.daylight.seconds_per_hour=0;
          config.temperature_celsius=20;config.wind.speed_min_mps=0;config.wind.speed_max_mps=1;
          weather.set_config(config);weather.reset();
          if(!set_view_mode(CameraMode::FirstPerson,false) || !stand_on_dry_ground())throw std::runtime_error("vitals test found no dry ground");
          vitals_probe=VitalsProbe{};
        } else if(rendered_frames==1) {
          if(!tree_fire)throw std::runtime_error("vitals test needs the tree fire model");
          std::optional<std::size_t> pick;double best=1e300;
          const auto& trees=vegetation->placement().trees;
          for(std::size_t i=0;i<trees.size();++i){const double d=std::hypot(trees[i].position[0]-person.feet.x,trees[i].position[2]-person.feet.z);if(d<best){best=d;pick=i;}}
          if(!pick)throw std::runtime_error("vitals test found no tree");
          tree_fire->ignite(static_cast<std::uint32_t>(*pick),IgnitionSource::Cell);
          tree_fire->advance(2.0,0.0,90.0);
          const auto& t=trees[*pick];
          const auto g=ground_height(t.position[0]-2.0,t.position[2]);
          if(!g)throw std::runtime_error("vitals test has no ground beside the tree");
          person.feet={t.position[0]-2.0,*g,t.position[2]};person.grounded=true;
          (void)terraflux::graphics::settle(person,walk_env);
          person.yaw=std::atan2(-(t.position[0]-person.feet.x),-(t.position[2]-person.feet.z));
          for(int i=0;i<300;++i)apply_vitals(terraflux::graphics::advance_vitals(vitals,gather_exposure(),1.0/60.0,vitals_style));
          vitals_probe.heat_exposure=vitals_heat_here;vitals_probe.smoke_exposure=vitals_smoke_here;
        } else if(rendered_frames==2) {
          terraflux::graphics::Exposure cold;cold.temperature_celsius=-15;cold.wind_mps=18;cold.rain_mm_h=20;cold.water_depth_meters=1.0;
          for(int i=0;i<1800;++i)apply_vitals(terraflux::graphics::advance_vitals(vitals,cold,1.0/60.0,vitals_style));
        } else if(rendered_frames==3) {
          person.feet.y+=12.0;person.grounded=false;person.vertical_velocity_mps=0.0;person.velocity_mps={};
          for(int i=0;i<600 && !person.grounded;++i) {
            terraflux::graphics::walk(person,WalkInput{},1.0/60.0,walk_env);
            if(person.landed){vitals_probe.impact=person.last_impact_mps;apply_vitals(terraflux::graphics::advance_vitals(vitals,gather_exposure(),1.0/60.0,vitals_style));}
          }
          vitals_probe.health_after_fall=vitals.health;
          terraflux::graphics::Exposure bolt;bolt.lightning=terraflux::graphics::LightningHazard::Struck;
          apply_vitals(terraflux::graphics::advance_vitals(vitals,bolt,1.0/60.0,vitals_style));
        } else if(rendered_frames==4) {
          if(!set_view_mode(CameraMode::World,false) || !set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("vitals test could not switch views");
        }
      }
      if(options.items_test) {
        // Frames: 0 in front of a stone, looking at it; 1 E: picked up, held in the
        // hand, gone from the ground; 2 Q: thrown, flying and landing as a stone
        // again; 3 beside a tree: E breaks off a branch; 4 beside a burning tree:
        // E lights it into a torch (which warms); 5 beside an unburned tree: E
        // sets it alight with the torch; 6 Q: the torch thrown lands as a burning branch.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        using terraflux::graphics::HeldKind;
        const auto count_present=[&](){std::size_t n=0;for(const auto& st:stones)if(st.present)++n;return n;};
        const auto face_point=[&](terraflux::math::Vec3 target,double distance) {
          const double yaw=std::atan2(-(target.x-person.feet.x),-(target.z-person.feet.z));
          const auto fwd=terraflux::graphics::horizontal_forward(yaw);
          const auto foot=target-fwd*distance;
          const auto g=ground_height(foot.x,foot.z);
          if(!g)return false;
          person.feet={foot.x,*g,foot.z};person.grounded=true;person.yaw=yaw;
          person.pitch=std::atan2(target.y-person.eye().y,distance);
          return terraflux::graphics::settle(person,walk_env);
        };
        items_probe.did.clear();
        if(rendered_frames==0) {
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("items test found no ground to stand on");
          if(stones.empty())throw std::runtime_error("items test found no stones");
          std::optional<std::size_t> pick;double best=1e300;
          for(std::size_t i=0;i<stones.size();++i) {
            const double d=std::hypot(stones[i].position.x-scene_camera.target.x,stones[i].position.z-scene_camera.target.z);
            if(d<best && (!tree_field.clearance(stones[i].position.x,stones[i].position.z,2.5))){best=d;pick=i;}
          }
          if(!pick || !face_point(stones[*pick].position,1.2))throw std::runtime_error("items test could not stand before a stone");
          items_probe.stone=static_cast<std::uint32_t>(*pick);items_probe.stones_present=count_present();
        } else if(rendered_frames==1) {
          items_probe.did=interact();
        } else if(rendered_frames==2) {
          person.pitch=0.1;  // a throw ahead, not into the ground
          items_probe.did=throw_held();
          const auto start=person.feet;
          for(int i=0;i<600 && !projectiles.empty();++i)land_projectiles(1.0/60.0);
          const auto& st=stones[items_probe.stone];
          items_probe.landed_distance=std::hypot(st.position.x-start.x,st.position.z-start.z);
        } else if(rendered_frames==3) {
          const auto& trees=vegetation->placement().trees;
          std::optional<std::size_t> pick;double best=1e300;
          for(std::size_t i=0;i<trees.size();++i){const double d=std::hypot(trees[i].position[0]-person.feet.x,trees[i].position[2]-person.feet.z);if(d<best){best=d;pick=i;}}
          if(!pick)throw std::runtime_error("items test found no tree");
          const auto& t=trees[*pick];
          if(!face_point({t.position[0],t.position[1]+1.2,t.position[2]},tree_fire->bodies()[*pick].trunk_radius_meters+0.9))throw std::runtime_error("items test could not stand by a tree");
          items_probe.tree=static_cast<int>(*pick);
          items_probe.did=interact();
        } else if(rendered_frames==4) {
          if(!tree_fire || items_probe.tree<0)throw std::runtime_error("items test needs the tree fire model");
          tree_fire->ignite(static_cast<std::uint32_t>(items_probe.tree),IgnitionSource::Cell);
          tree_fire->advance(2.0,0.0,90.0);
          items_probe.did=interact();
        } else if(rendered_frames==5) {
          const auto& trees=vegetation->placement().trees;
          std::optional<std::size_t> pick;double best=1e300;
          for(std::size_t i=0;i<trees.size();++i) {
            if(tree_fire->states()[i].stage!=TreeFireStage::Unburned)continue;
            const double d=std::hypot(trees[i].position[0]-person.feet.x,trees[i].position[2]-person.feet.z);
            if(d<best){best=d;pick=i;}
          }
          if(!pick)throw std::runtime_error("items test found no unburned tree");
          const auto& t=trees[*pick];
          if(!face_point({t.position[0],t.position[1]+1.2,t.position[2]},tree_fire->bodies()[*pick].trunk_radius_meters+0.9))throw std::runtime_error("items test could not stand by the tree");
          items_probe.tree=static_cast<int>(*pick);
          items_probe.did=interact();
        } else if(rendered_frames==6) {
          person.yaw+=3.14159265358979323846;person.pitch=0.2;
          items_probe.branches_burning=tree_fire->branches().size();
          items_probe.did=throw_held();
          for(int i=0;i<600 && !projectiles.empty();++i)land_projectiles(1.0/60.0);
        }
      }
      if(options.tools_test) {
        // Frames: 0 at the top of a cliff, the rope in hand (coil drawn); 1 the
        // rope thrown over the edge: hung down the face, pegged at the lip; 2 taken
        // hold of and let down halfway (on the rope, the arms climbing); 3 let down
        // to the foot, stepped off, the rope pulled in; 4 the axe: a tree chopped
        // down in the counted chops, falling away from the walker; 5 the chainsaw:
        // a tree sawed through in the time its diameter says, the fuel spent; 6 a
        // branch cut in two by the axe and in two again by the saw, a quarter-metre
        // piece refused; 7 the rope thrown up into a tree wraps round a branch and is
        // climbed to a perch; 8 let go of, and a tree felled toward the walker lands on it.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        using terraflux::graphics::ToolKind;
        using terraflux::graphics::RopeState;
        tools_probe.did.clear();
        const auto face_trunk=[&](std::size_t i,double distance) {
          const auto& t=vegetation->placement().trees[i];
          const double yaw=std::atan2(-(t.position[0]-person.feet.x),-(t.position[2]-person.feet.z));
          const auto fwd=terraflux::graphics::horizontal_forward(yaw);
          const terraflux::math::Vec3 foot{t.position[0]-fwd.x*distance,0,t.position[2]-fwd.z*distance};
          const auto g=ground_height(foot.x,foot.z);
          if(!g)return false;
          person.feet={foot.x,*g,foot.z};person.grounded=true;person.yaw=yaw;person.pitch=0.15;person.on_rope=false;person.climbing=false;
          return terraflux::graphics::settle(person,walk_env);
        };
        const auto nearest_standing_tree=[&](double min_height)->std::optional<std::size_t> {
          std::optional<std::size_t> pick;double best=1e300;
          const auto& trees=vegetation->placement().trees;
          for(std::size_t i=0;i<trees.size() && i<tree_fire->size();++i) {
            if(!tree_fire->standing(static_cast<std::uint32_t>(i)) || tree_fire->bodies()[i].height_meters<min_height)continue;
            if(tree_fire->states()[i].stage!=TreeFireStage::Unburned)continue;
            const double d=std::hypot(trees[i].position[0]-person.feet.x,trees[i].position[2]-person.feet.z);
            if(d<best && d>3.0){best=d;pick=i;}
          }
          return pick;
        };
        const auto run_swing=[&]() {
          std::string what=use_tool();
          for(int i=0;i<200 && belt.swing>0;++i){const auto w=advance_tools(1.0/60.0);if(!w.empty())what=w;}
          return what;
        };
        if(rendered_frames==0) {
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("tools test found no ground to stand on");
          // The tallest face: from each steep facet, march uphill to where the
          // ground flattens (the top, walkable) and downhill to where it flattens
          // (the foot); the face is the drop between them, at least six metres.
          std::optional<terraflux::math::Vec3> top;double best=2.5,top_yaw=0;
          const auto march=[&](terraflux::math::Vec3 from,terraflux::math::Vec3 dir)->std::optional<terraflux::math::Vec3> {
            auto h0=ground_height(from.x,from.z);
            if(!h0)return std::nullopt;
            terraflux::math::Vec3 p{from.x,*h0,from.z};
            for(int k=0;k<30;++k) {
              const auto q=p+dir*1.0;
              const auto h=ground_height(q.x,q.z);
              if(!h)return std::nullopt;
              const auto q2=q+dir*2.0;
              const auto h2=ground_height(q2.x,q2.z);
              if(!h2)return std::nullopt;
              if(std::abs(*h2-*h)<0.8)return terraflux::math::Vec3{q.x,*h,q.z};  // flattened out (under 22 degrees over the next two metres)
              p={q.x,*h,q.z};
            }
            return std::nullopt;
          };
          for(std::size_t i=0;i<mesh.vertices.size();++i) {
            const auto& v=mesh.vertices[i];
            const auto n=v.normal.normalized();
            const double slope=std::acos(std::clamp(n.y,-1.0,1.0))*180.0/3.14159265358979323846;
            if(slope<55.0)continue;
            if(std::hypot(v.position.x-scene_camera.target.x,v.position.z-scene_camera.target.z)>1200.0)continue;
            const double horizontal=std::max(std::hypot(n.x,n.z),1e-6);
            const terraflux::math::Vec3 downhill{n.x/horizontal,0,n.z/horizontal};
            const auto up=march(v.position,downhill*-1.0),down=march(v.position,downhill);
            if(!up || !down)continue;
            if(water_style && down->y<water_style->level_meters+0.5)continue;
            const double face=up->y-down->y;
            if(face<=best)continue;
            const auto stand=*up+downhill*-0.5;
            const auto hs=ground_height(stand.x,stand.z);
            if(!hs || std::abs(*hs-up->y)>0.4)continue;
            if(tree_field.clearance(stand.x,stand.z,1.0))continue;
            best=face;top=terraflux::math::Vec3{stand.x,*hs,stand.z};top_yaw=std::atan2(-downhill.x,-downhill.z);
          }
          if(top) {
            tools_probe.face=best;
            person.feet=*top;person.grounded=true;person.yaw=top_yaw;person.pitch=-0.35;
            if(!terraflux::graphics::settle(person,walk_env))throw std::runtime_error("tools test could not stand at the cliff top");
          } else {
            // No face on this terrain (a gentle replay scenario): the rope is thrown on the flat instead and only lies there.
            tools_probe.face=0;
            if(!stand_on_dry_ground())throw std::runtime_error("tools test found no dry ground");
            person.pitch=-0.2;
          }
          belt.select(ToolKind::Rope);
          tools_probe.did="took the rope";
        } else if(rendered_frames==1) {
          tools_probe.did=use_tool();
          for(int i=0;i<900 && rope.state==RopeState::Flying;++i){const auto w=advance_tools(1.0/60.0);if(!w.empty())tools_probe.did=w;}
          person.pitch=-0.6;  // look down the face at the hanging rope
        } else if(rendered_frames==2 && tools_probe.face<=0) {
          tools_probe.did=use_tool();  // nothing to take hold of: a rope lying on the ground
          if(person.on_rope || !tools_probe.did.empty())throw std::runtime_error("a rope lying on the flat was climbed");
        } else if(rendered_frames==3 && tools_probe.face<=0) {
          tools_probe.did=pull_rope_in()?"pulled the rope in":"could not pull the rope in";
        } else if(rendered_frames==2) {
          // To the lip, take hold, let down halfway.
          terraflux::math::Vec3 inward{rope.anchor.x-rope.end.x,0,rope.anchor.z-rope.end.z};
          if(inward.length()<1e-6)inward=terraflux::graphics::horizontal_forward(person.yaw)*-1.0;
          inward=inward.normalized();
          const auto at=rope.anchor+inward*0.7;
          const auto g=ground_height(at.x,at.z);
          if(!g)throw std::runtime_error("tools test found no ground at the lip");
          person.feet={at.x,*g,at.z};person.grounded=true;
          tools_probe.did=use_tool();
          if(!person.on_rope)throw std::runtime_error("tools test could not take hold of the rope: "+tools_probe.did);
          WalkInput down;down.forward=-1;
          for(int i=0;i<3600 && person.on_rope && person.rope_t_meters<rope.span_meters()*0.5;++i)terraflux::graphics::climb_rope(person,rope,down,1.0/60.0,walk_env,rope_rules);
          person.pitch=-0.5;
        } else if(rendered_frames==3) {
          WalkInput down;down.forward=-1;
          terraflux::graphics::RopeClimbResult r;
          for(int i=0;i<3600 && person.on_rope;++i)r=terraflux::graphics::climb_rope(person,rope,down,1.0/60.0,walk_env,rope_rules);
          if(!r.stepped_off) {
            const auto g=ground_height(person.feet.x,person.feet.z);
            throw std::runtime_error("letting down the rope did not reach the foot of the face: t "+std::to_string(person.rope_t_meters)+" span "+std::to_string(rope.span_meters())+
                " moved "+std::to_string(r.moved_meters)+" ground "+(g?std::to_string(*g):"none")+" feet "+std::to_string(person.feet.y)+" end "+std::to_string(rope.end.y)+" let_go "+std::to_string(r.let_go)+" on "+std::to_string(person.on_rope)+" exhausted "+std::to_string(person.exhausted)+" stamina "+std::to_string(person.stamina));
          }
          tools_probe.did=pull_rope_in()?"pulled the rope in":"could not pull the rope in";  // within reach of the rope's end
          person.pitch=0.1;
        } else if(rendered_frames==4) {
          belt.select(ToolKind::Axe);
          const auto pick=nearest_standing_tree(6.0);
          if(!pick || !face_trunk(*pick,tree_fire->bodies()[*pick].trunk_radius_meters+1.0))throw std::runtime_error("tools test found no tree to chop");
          tools_probe.tree=static_cast<int>(*pick);
          tools_probe.expected_chops=terraflux::graphics::chops_to_fell(tree_fire->bodies()[*pick].trunk_radius_meters,axe_rules);
          tools_probe.chops=0;
          while(tree_fire->standing(static_cast<std::uint32_t>(*pick)) && tools_probe.chops<200){tools_probe.did=run_swing();++tools_probe.chops;}
          const auto& st=tree_fire->states()[*pick];
          const auto dir=toward_target(static_cast<int>(*pick));
          tools_probe.fall_alignment=st.fall_direction.x*dir.x+st.fall_direction.y*dir.y;
          tree_fire->advance(0.5,0.0,90.0);
          belt.swing=0.45;  // drawn mid-chop
        } else if(rendered_frames==5) {
          belt.swing=0;chop_target=-1;
          belt.select(ToolKind::Chainsaw);
          const auto pick=nearest_standing_tree(6.0);
          if(!pick || !face_trunk(*pick,tree_fire->bodies()[*pick].trunk_radius_meters+1.0))throw std::runtime_error("tools test found no tree to saw");
          tools_probe.tree=static_cast<int>(*pick);
          tools_probe.expected_saw_seconds=terraflux::graphics::seconds_to_saw(tree_fire->bodies()[*pick].trunk_radius_meters,saw_rules);
          tools_probe.fuel_before=belt.chainsaw_fuel_seconds;
          int steps=0;
          while(tree_fire->standing(static_cast<std::uint32_t>(*pick)) && steps<60*600){const auto w=saw_step(true,1.0/60.0);if(!w.empty())tools_probe.did=w;++steps;}
          tools_probe.saw_seconds=steps/60.0;
          const auto& st=tree_fire->states()[*pick];
          const auto dir=toward_target(static_cast<int>(*pick));
          tools_probe.fall_alignment=st.fall_direction.x*dir.x+st.fall_direction.y*dir.y;
          tree_fire->advance(0.5,0.0,90.0);
          belt.sawing=true;belt.saw_phase=0.3;person.pitch=-0.35;  // drawn sawing, the saw in view
        } else if(rendered_frames==6) {
          belt.sawing=false;
          // Out in the open (no trunk within the tools' reach), a long branch at the
          // feet: the axe halves it, the saw halves a half, the axe refuses a quarter-metre piece.
          {
            bool placed=false;
            for(double r=0;r<=40.0 && !placed;r+=2.0)for(int k=0;k<12 && !placed;++k) {
              const double a=k*(2.0*3.14159265358979323846/12.0);
              const double x=person.feet.x+std::cos(a)*r,z=person.feet.z+std::sin(a)*r;
              const auto g=ground_height(x,z);
              if(!g || tree_field.clearance(x,z,3.0))continue;
              if(water_style && *g<water_style->level_meters+0.3)continue;
              person.feet={x,*g,z};person.grounded=true;placed=terraflux::graphics::settle(person,walk_env) && !tree_in_reach(2.5);
            }
            if(!placed)throw std::runtime_error("tools test found no open ground for the branch");
          }
          const auto fwd=look_forward();
          const terraflux::math::Vec3 at{person.feet.x+fwd.x*0.9,person.feet.y,person.feet.z+fwd.z*0.9};
          const auto g=ground_height(at.x,at.z);
          dropped_branches.clear();
          dropped_branches.push_back({{at.x,g?*g:at.y,at.z},1.6,person.yaw,true});
          person.pitch=-0.7;
          belt.select(ToolKind::Axe);
          tools_probe.did=run_swing();
          if(dropped_branches.size()!=2)throw std::runtime_error("the axe did not cut the branch in two: "+tools_probe.did);
          belt.select(ToolKind::Chainsaw);
          for(int i=0;i<120 && dropped_branches.size()<3;++i)saw_step(true,1.0/60.0);
          if(dropped_branches.size()!=3)throw std::runtime_error("the chainsaw did not cut a half in two");
          belt.select(ToolKind::Axe);
          // Aim at a quarter-metre... the halves are 0.8 and the quarters 0.4: the shortest in reach is refused.
          std::size_t shortest=0;for(std::size_t i=1;i<dropped_branches.size();++i)if(dropped_branches[i].length<dropped_branches[shortest].length)shortest=i;
          chop_branch=shortest;chop_target=-2;
          tools_probe.did=cut_branch(-2);
          tools_probe.branches=dropped_branches.size();
          belt.swing=0;chop_target=-1;
        } else if(rendered_frames==7) {
          belt.select(ToolKind::Rope);
          const auto pick=nearest_standing_tree(9.0);
          if(!pick || !face_trunk(*pick,5.0))throw std::runtime_error("tools test found no tree to rope up");
          tools_probe.tree=static_cast<int>(*pick);
          const auto& t=vegetation->placement().trees[*pick];
          const double h=tree_fire->bodies()[*pick].height_meters;
          person.pitch=std::atan2(t.position[1]+0.6*h-person.eye().y,5.0);
          tools_probe.did=use_tool();
          for(int i=0;i<900 && rope.state==RopeState::Flying;++i){const auto w=advance_tools(1.0/60.0);if(!w.empty())tools_probe.did=w;}
          if(rope.state!=RopeState::Hung || rope.branch_tree<0)throw std::runtime_error("the rope thrown into the crown did not wrap round a branch: "+tools_probe.did);
          // Under it, take hold and climb to the branch.
          const auto g=ground_height(rope.end.x+0.3,rope.end.z);
          person.feet={rope.end.x+0.3,g?*g:rope.end.y,rope.end.z};person.grounded=true;
          const auto took=use_tool();
          if(!person.on_rope)throw std::runtime_error("could not take hold of the hanging rope: "+took);
          WalkInput up;up.forward=1;
          terraflux::graphics::RopeClimbResult r;
          for(int i=0;i<3600 && !r.at_top;++i)r=terraflux::graphics::climb_rope(person,rope,up,1.0/60.0,walk_env,rope_rules);
          if(!r.at_top)throw std::runtime_error("the rope was not climbed to the branch");
          person.pitch=-0.4;
        } else if(rendered_frames==8) {
          // Let go and fall; then a tree cut to fall toward the walker lands on it.
          WalkInput jump;jump.jump=true;
          terraflux::graphics::climb_rope(person,rope,jump,1.0/60.0,walk_env,rope_rules);
          for(int i=0;i<300 && !person.grounded;++i)terraflux::graphics::walk(person,WalkInput{},1.0/60.0,walk_env);
          if(!person.grounded)throw std::runtime_error("letting go of the rope did not bring the walker to the ground");
          terraflux::graphics::coil_rope(rope);
          belt.select(ToolKind::None);belt.selected=ToolKind::None;
          const auto pick=nearest_standing_tree(8.0);
          if(!pick || !face_trunk(*pick,3.0))throw std::runtime_error("tools test found no tree to be felled by");
          tools_probe.tree=static_cast<int>(*pick);
          const auto dir=toward_target(static_cast<int>(*pick));
          tree_fire->cut(static_cast<std::uint32_t>(*pick),10.0,{-dir.x,-dir.y});  // toward the walker
          for(int i=0;i<60*12 && pending_crush<=0;++i){tree_fire->advance(1.0/60.0,0.0,90.0);trunks_landing();}
          tools_probe.crushed=pending_crush;
          apply_vitals(terraflux::graphics::advance_vitals(vitals,gather_exposure(),1.0/60.0,vitals_style));
        }
      }
      if(options.body_test) {
        // Frames: 0 standing on dry ground looking down at the hanging hands
        // (reference); 1 the arms hidden; 2 running mid-stride, the arms
        // pumping; 3 climbing, the hands reaching up; 4 afloat in the lake,
        // stroking; 5 diving, under water; 6 back to the start, the reference exactly.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        if(rendered_frames==0) {
          if(!set_view_mode(CameraMode::FirstPerson,false) || !stand_on_dry_ground())throw std::runtime_error("body test found no dry ground");
          person.pitch=-1.0;  // looking well down: the hanging hands come into view
          view_test_person=person;
          show_arms=true;
        } else if(rendered_frames==1) {
          show_arms=false;
        } else if(rendered_frames==2) {
          show_arms=true;person.gait_speed_mps=person.walk_speed_mps*person.run_multiplier;person.gait_phase=0.25;person.stance_foot=0;person.pitch=-0.2;
        } else if(rendered_frames==3) {
          person.gait_speed_mps=0;person.climbing=true;person.gait_phase=0.25;person.pitch=0.3;
        } else if(rendered_frames==4) {
          person.climbing=false;
          // The deepest lake point near the centre: afloat there.
          std::optional<std::size_t> pick;double best=0;
          for(std::size_t i=0;i<mesh.vertices.size();++i) {
            const auto& v=mesh.vertices[i];
            const double depth=water_style?water_style->level_meters-v.position.y:0.0;
            const double d=std::hypot(v.position.x-scene_camera.target.x,v.position.z-scene_camera.target.z);
            if(depth>best && d<400){best=depth;pick=i;}
          }
          if(!pick || best<walk_env.water_rules.swim_depth_meters+0.5)throw std::runtime_error("body test found no water deep enough to swim");
          const auto& v=mesh.vertices[*pick];
          person.feet={v.position.x,v.position.y,v.position.z};person.grounded=true;person.pitch=-0.3;
          for(int i=0;i<120;++i)terraflux::graphics::walk(person,WalkInput{1,0,false},1.0/60.0,walk_env);  // two seconds: more than a stroke
          person.swim_phase=0.5;
        } else if(rendered_frames==5) {
          for(int i=0;i<90;++i)terraflux::graphics::walk(person,WalkInput{1,0,false,false,true},1.0/60.0,walk_env);
        } else if(rendered_frames==6) {
          person=view_test_person;
        }
      }
      if(options.climb_test) {
        // Frames: 0 standing three metres below the steepest climbable rock
        // facet, facing it (reference); 1 after twenty seconds of pressing
        // uphill: climbed, higher, strength spent; 2 below the terrain's biggest
        // waterfall with the flood wave running: the climb refused; 3 the wave
        // gone and the walker back: the reference exactly.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        const auto stand_below=[&](std::size_t vertex) {
          const auto& v=mesh.vertices[vertex];
          const auto n=v.normal.normalized();
          const double horizontal=std::max(std::hypot(n.x,n.z),1e-6);
          const terraflux::math::Vec3 downhill{n.x/horizontal,0,n.z/horizontal};
          const auto foot=v.position+downhill*3.0;
          const auto h=ground_height(foot.x,foot.z);
          if(!h)return false;
          person.feet={foot.x,*h,foot.z};person.grounded=true;person.stamina=1.0;person.exhausted=false;
          person.yaw=std::atan2(-(v.position.x-foot.x),-(v.position.z-foot.z));person.pitch=0.4;
          return terraflux::graphics::settle(person,walk_env);
        };
        if(rendered_frames==0) {
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("climb test found no ground to stand on");
          // The steepest facet between the walking limit and the rock limit that is dry rock.
          std::optional<std::size_t> pick;double best=0;
          for(std::size_t i=0;i<mesh.vertices.size();++i) {
            const auto n=mesh.vertices[i].normal.normalized();
            const double slope=std::acos(std::clamp(n.y,-1.0,1.0))*180.0/3.14159265358979323846;
            if(slope<=walk_env.body.max_slope_degrees+5.0 || slope>72.0)continue;
            const auto c=walk_env.climb(mesh.vertices[i].position.x,mesh.vertices[i].position.z);
            if(!c.climbable || std::string(c.name)!="rock" || slope>c.max_slope_degrees)continue;
            if(slope>best){best=slope;pick=i;}
          }
          if(!pick || !stand_below(*pick))throw std::runtime_error("climb test found no climbable rock face");
          climb_probe=ClimbProbe{};climb_probe.start_y=person.feet.y;climb_probe.slope=best;
          view_test_person=person;
        } else if(rendered_frames==1) {
          for(int i=0;i<60*20;++i) {
            terraflux::graphics::walk(person,WalkInput{1,0,false},1.0/60.0,walk_env);
            if(person.climbing){climb_probe.climbed=true;climb_probe.surface=person.climb_surface;}
          }
          climb_probe.rise=person.feet.y-climb_probe.start_y;climb_probe.stamina=person.stamina;
        } else if(rendered_frames==2) {
          flood.set_flash(1.0);
          // The steepest facet under the biggest waterfall texel.
          std::optional<std::size_t> pick;double best=0;
          for(std::size_t i=0;i<mesh.vertices.size();++i) {
            const auto& v=mesh.vertices[i];
            if(flood.waterfall_at(v.position.x,v.position.z)<0.3)continue;
            const auto n=v.normal.normalized();
            const double slope=std::acos(std::clamp(n.y,-1.0,1.0))*180.0/3.14159265358979323846;
            if(slope>best){best=slope;pick=i;}
          }
          if(!pick || !stand_below(*pick))throw std::runtime_error("climb test found no waterfall to stand under");
          const double y0=person.feet.y;
          climb_probe=ClimbProbe{};climb_probe.start_y=y0;climb_probe.slope=best;
          for(int i=0;i<60*5;++i) {
            terraflux::graphics::walk(person,WalkInput{1,0,false},1.0/60.0,walk_env);
            if(person.climbing){climb_probe.climbed=true;climb_probe.surface=person.climb_surface;}
            if(person.climb_refusal[0])climb_probe.refusal=person.climb_refusal;
          }
          climb_probe.rise=person.feet.y-y0;
        } else if(rendered_frames==3) {
          flood.set_flash(0.0);
          person=view_test_person;
        }
      }
      if(options.roads_test) {
        // Frames: 0 the world view over the bridge, the roads hidden (reference);
        // 1 the roads shown; 2 first person on the road west of the bridge: two
        // seconds of walking cover the road factor more than the same walk on
        // open ground; 3 on the deck mid-span: carried over the water at the
        // deck's height, the railing holding against a strafe; 4 a burning tree
        // beside a road felled onto it: the road blocked where it lies, the
        // bonus gone, the surface damaged as it burns; 5 the deck destroyed
        // under the walker: hurt by the collapse, knocked down and fallen into
        // the water below; 6 everything reset and the roads hidden again: the
        // reference exactly.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        using terraflux::graphics::RoadKind;
        if(!roads || !tree_fire || !vegetation)throw std::runtime_error("roads test needs the road network, the trees and the tree fire model");
        const auto stand_on=[&](double x,double z,double yaw) {
          const auto deck=roads->platform_height(x,z);
          const auto g=ground_height(x,z);
          if(!g)throw std::runtime_error("roads test point has no ground");
          person.feet={x,deck?std::max(*deck,*g):*g,z};person.grounded=true;person.yaw=yaw;person.pitch=-0.2;
          person.vertical_velocity_mps=0;person.velocity_mps={};person.stunned_seconds=0;person.stun_crouch_meters=0;person.crouch_meters=0;
          person.gait_speed_mps=0;person.gait_phase=0;
        };
        if(rendered_frames==0) {
          std::optional<std::uint32_t> bridge;
          for(std::uint32_t i=0;i<roads->size();++i)if(roads->def().segments[i].kind==RoadKind::Bridge && (!bridge || roads->length(i)>roads->length(*bridge)))bridge=i;
          if(!bridge)throw std::runtime_error("roads test found no bridge in the network");
          roads_probe.bridge=*bridge;
          // The road that meets the bridge at its first node.
          std::optional<std::uint32_t> road;
          for(std::uint32_t i=0;i<roads->size();++i) {
            const auto& seg=roads->def().segments[i];
            if(seg.kind==RoadKind::Road && (seg.a==roads->def().segments[*bridge].a || seg.b==roads->def().segments[*bridge].a))road=i;
          }
          if(!road)throw std::runtime_error("roads test found no road joining the bridge");
          roads_probe.road=*road;
          const auto& seg=roads->def().segments[*bridge];
          const auto a=roads->def().nodes[seg.a].position,b=roads->def().nodes[seg.b].position;
          const auto mid=(a+b)*0.5;
          roads_orbit={scene_camera.target,yaw,pitch,distance};
          scene_camera.target=snap_orbit_target({mid.x,0,mid.y});
          distance=std::max(60.0,std::min(scene_camera.starting_distance*0.25,roads->length(*bridge)*1.5));yaw=0.9;pitch=0.45;
          roads_visible=false;
        } else if(rendered_frames==1) {
          roads_visible=true;
        } else if(rendered_frames==2) {
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("roads test found no ground to stand on");
          // Forty metres along the road from the bridge, walking toward it.
          const auto& seg=roads->def().segments[roads_probe.road];
          const auto& bseg=roads->def().segments[roads_probe.bridge];
          const bool toward_b=seg.b==bseg.a;  // the road runs a -> b into the bridge
          const auto dir=roads->direction(roads_probe.road)*(toward_b?1.0:-1.0);
          const auto end=roads->def().nodes[toward_b?seg.b:seg.a].position;
          const double back=std::min(40.0,roads->length(roads_probe.road)*0.6);
          const auto start=end-dir*back;
          const double heading=std::atan2(-dir.x,-dir.y);
          stand_on(start.x,start.y,heading);
          (void)terraflux::graphics::settle(person,walk_env);
          roads_probe.on_platform=person.on_platform;
          const auto feet=person.feet;
          for(int i=0;i<120;++i)terraflux::graphics::walk(person,WalkInput{1,0,false},1.0/60.0,walk_env);
          roads_probe.on_road_m=std::hypot(person.feet.x-feet.x,person.feet.z-feet.z);
          roads_probe.on_platform=person.on_platform;
          auto plain=walk_env;plain.surface_speed={};plain.platforms={};
          terraflux::graphics::FirstPersonCamera other=person;other.feet=feet;other.gait_speed_mps=0;other.gait_phase=0;other.on_platform=false;
          (void)terraflux::graphics::settle(other,plain);
          for(int i=0;i<120;++i)terraflux::graphics::walk(other,WalkInput{1,0,false},1.0/60.0,plain);
          roads_probe.off_road_m=std::hypot(other.feet.x-feet.x,other.feet.z-feet.z);
          stand_on(start.x,start.y,heading);  // back to the start for the picture
          person.pitch=-0.15;
        } else if(rendered_frames==3) {
          const auto& seg=roads->def().segments[roads_probe.bridge];
          const auto a=roads->def().nodes[seg.a].position,b=roads->def().nodes[seg.b].position;
          const auto mid=(a+b)*0.5;
          const auto dir=roads->direction(roads_probe.bridge);
          stand_on(mid.x,mid.y,std::atan2(-dir.x,-dir.y));
          roads_probe.deck_y=person.feet.y;
          roads_probe.ground_under_deck=ground_height(mid.x,mid.y).value_or(person.feet.y);
          double lowest=person.feet.y;
          for(int i=0;i<60;++i){terraflux::graphics::walk(person,WalkInput{1,0,false},1.0/60.0,walk_env);lowest=std::min(lowest,person.feet.y);}
          roads_probe.on_platform=person.on_platform && std::abs(lowest-roads_probe.deck_y)<0.05;
          for(int i=0;i<120;++i)terraflux::graphics::walk(person,WalkInput{0,1,false},1.0/60.0,walk_env);
          roads_probe.offset_after_strafe=std::abs(roads->sample(person.feet.x,person.feet.z).offset_meters);
          roads_probe.fell=!person.grounded;
          person.pitch=-0.1;
        } else if(rendered_frames==4) {
          // The nearest sound tree that can reach a road when it falls.
          const auto& trees=vegetation->placement().trees;
          std::optional<std::size_t> pick;double best=1e300;terraflux::math::Vec2 toward{1,0};
          const double half=0.5*terraflux::graphics::road_width(road_style);
          for(std::size_t i=0;i<trees.size();++i) {
            if(tree_fire->states()[i].stage!=TreeFireStage::Unburned || !tree_fire->standing(static_cast<std::uint32_t>(i)))continue;
            const terraflux::math::Vec2 p{trees[i].position[0],trees[i].position[2]};
            for(std::uint32_t sidx=0;sidx<roads->size();++sidx) {
              if(roads->def().segments[sidx].kind!=RoadKind::Road)continue;
              const auto a=roads->def().nodes[roads->def().segments[sidx].a].position,b=roads->def().nodes[roads->def().segments[sidx].b].position;
              const auto d=b-a;const double len=d.length();
              const double along=std::clamp((p-a).dot(d)/len,0.0,len);
              const auto c=a+d*(along/len);
              const double dist=(c-p).length();
              if(dist<=half || dist>tree_fire->bodies()[i].height_meters*0.8 || along<5 || along>len-5)continue;
              const double score=dist+0.0005*std::hypot(p.x-scene_camera.target.x,p.y-scene_camera.target.z);
              if(score<best){best=score;pick=i;toward=(c-p)/dist;}
            }
          }
          if(!pick)throw std::runtime_error("roads test found no tree that could fall across a road");
          roads_probe.tree=static_cast<int>(*pick);
          const auto& t=trees[*pick];
          const terraflux::math::Vec2 side{-toward.y,toward.x};
          const auto stand=terraflux::math::Vec2{t.position[0],t.position[2]}+side*12.0;
          stand_on(stand.x,stand.y,std::atan2(-(t.position[0]-stand.x),-(t.position[2]-stand.y)));
          (void)terraflux::graphics::settle(person,walk_env);
          tree_fire->ignite(static_cast<std::uint32_t>(*pick),IgnitionSource::Cell);
          tree_fire->advance(1.0,0.0,90.0);
          tree_fire->cut(static_cast<std::uint32_t>(*pick),2.0*tree_fire->bodies()[*pick].trunk_radius_meters,toward);
          for(int i=0;i<60*15 && roads->blockages().empty();++i){tree_fire->advance(1.0/60.0,0.0,90.0);logs_on_roads();}
          rebuild_tree_obstacles();
          advance_roads(2.0);
          roads_probe.blockages=roads->blockages().size();
          if(!roads->blockages().empty()) {
            const auto& bl=roads->blockages().front();
            roads_probe.damage=roads->states()[bl.segment].damage;
            const auto c=roads->def().nodes[roads->def().segments[bl.segment].a].position+roads->direction(bl.segment)*bl.along_meters;
            roads_probe.blocked_sample=roads->sample(c.x,c.y).blocked && roads->sample(c.x,c.y).speed_factor==1.0;
          }
        } else if(rendered_frames==5) {
          const auto& seg=roads->def().segments[roads_probe.bridge];
          const auto a=roads->def().nodes[seg.a].position,b=roads->def().nodes[seg.b].position;
          const auto spot=(a+b)*0.5+roads->direction(roads_probe.bridge)*5.0;  // mid-span, between the piers rather than over one
          const auto dir=roads->direction(roads_probe.bridge);
          stand_on(spot.x,spot.y,std::atan2(-dir.x,-dir.y));
          (void)terraflux::graphics::settle(person,walk_env);
          roads_probe.deck_y=person.feet.y;roads_probe.health_before=vitals.health;
          roads->destroy(roads_probe.bridge,"test");
          handle_road_events();
          rebuild_road_obstacles();
          roads_probe.collapsed_under=pending_collapse>0 && person.stunned_seconds>0;
          apply_vitals(terraflux::graphics::advance_vitals(vitals,gather_exposure(),1.0/60.0,vitals_style));
          for(int i=0;i<60*6 && !(person.landed || person.swimming);++i)terraflux::graphics::walk(person,WalkInput{},1.0/60.0,walk_env);
          roads_probe.feet_after_collapse=person.feet.y;roads_probe.fell=person.landed || person.swimming;roads_probe.swimming=person.swimming;
          person.pitch=0.1;
        } else if(rendered_frames==6) {
          roads->reset();
          tree_fire->reset();
          rebuild_tree_obstacles();
          if(!set_view_mode(CameraMode::World,false))throw std::runtime_error("roads test could not return to the world view");
          scene_camera.target=roads_orbit.target;yaw=roads_orbit.yaw;pitch=roads_orbit.pitch;distance=roads_orbit.distance;
          const auto& seg=roads->def().segments[roads_probe.bridge];
          const auto mid=(roads->def().nodes[seg.a].position+roads->def().nodes[seg.b].position)*0.5;
          scene_camera.target=snap_orbit_target({mid.x,0,mid.y});
          distance=std::max(60.0,std::min(scene_camera.starting_distance*0.25,roads->length(roads_probe.bridge)*1.5));yaw=0.9;pitch=0.45;
          roads_visible=false;
        }
      }
      if(options.city_test) {
        // Frames: 0 the world view over the CBD (reference); 1 first person on a
        // CBD avenue among the towers; 2 on the crest of a highway interchange's
        // viaduct, the avenue passing beneath; 3 on the airport's runway; 4 back
        // to the world view: the reference exactly.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::RoadClass;
        using terraflux::graphics::RoadKind;
        using terraflux::graphics::ZoneKind;
        if(!city || !city_plan || !roads || !buildings)throw std::runtime_error("city test needs the city, its roads and its buildings");
        const auto stand_at=[&](double x,double z,double yaw,double lift) {
          const auto g=ground_height(x,z);
          if(!g)throw std::runtime_error("city test point has no ground");
          const auto built=built_surface(x,z,*g+lift);
          person.feet={x,built?std::max(*built,*g):*g,z};person.grounded=true;person.yaw=yaw;person.pitch=0.05;
          person.vertical_velocity_mps=0;person.velocity_mps={};person.gait_speed_mps=0;person.gait_phase=0;
          (void)terraflux::graphics::settle(person,walk_env);
        };
        const auto* cbd=[&]()->const terraflux::graphics::CityZone*{for(const auto& z:city_plan->zones)if(z.kind==ZoneKind::Cbd)return &z;return nullptr;}();
        if(!cbd)throw std::runtime_error("city test needs a CBD");
        if(rendered_frames==0) {
          roads_orbit={scene_camera.target,yaw,pitch,distance};
          scene_camera.target=snap_orbit_target({cbd->centre.x,0,cbd->centre.y});
          distance=std::max(100.0,std::min(scene_camera.starting_distance*0.3,900.0));yaw=0.8;pitch=0.5;
          city_probe=CityProbe{};
          for(const auto& b:buildings->buildings())if(!roads->sample(b.path_end.x+b.facing.x*0.1,b.path_end.y+b.facing.y*0.1).on)++city_probe.unconnected;
        } else if(rendered_frames==1) {
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("city test found no ground to stand on");
          // The CBD avenue nearest its centre.
          std::optional<std::uint32_t> pick;double best=1e300;
          for(std::uint32_t i=0;i<roads->size();++i) {
            const auto& seg=roads->def().segments[i];
            if(seg.cls!=RoadClass::Avenue || seg.kind!=RoadKind::Road)continue;
            const auto mid=(roads->def().nodes[seg.a].position+roads->def().nodes[seg.b].position)*0.5;
            const double d=(mid-cbd->centre).length();
            if(d<best){best=d;pick=i;}
          }
          if(!pick)throw std::runtime_error("city test found no avenue in the CBD");
          const auto mid=(roads->def().nodes[roads->def().segments[*pick].a].position+roads->def().nodes[roads->def().segments[*pick].b].position)*0.5;
          const auto dir=roads->direction(*pick);
          stand_at(mid.x,mid.y,std::atan2(-dir.x,-dir.y),0.0);
          const auto at=roads->sample(person.feet.x,person.feet.z,person.feet.y);
          city_probe.lanes=at.on?terraflux::graphics::road_class_lanes(at.cls):0;
          if(const auto* z=zone_at(person.feet.x,person.feet.z))city_probe.zone=z->name;
          for(const auto& b:buildings->buildings())if(b.kind==terraflux::graphics::BuildingKind::Skyscraper && (b.centre-mid).length()<160)++city_probe.towers_near;
        } else if(rendered_frames==2) {
          std::optional<terraflux::graphics::CityInterchange> diamond;
          for(const auto& ic:city->interchanges)if(ic.diamond){diamond=ic;break;}
          if(!diamond)throw std::runtime_error("city test found no diamond interchange");
          const auto g=*ground_height(diamond->at.x,diamond->at.y);
          const auto deck=roads->sample(diamond->at.x,diamond->at.y,g+city_plan->interchange_lift_meters);
          const auto dir=roads->direction(deck.segment);
          stand_at(diamond->at.x,diamond->at.y,std::atan2(-dir.x,-dir.y),city_plan->interchange_lift_meters);
          city_probe.on_deck=deck.on && deck.kind==RoadKind::Bridge && deck.cls==RoadClass::Highway;
          city_probe.deck_over_ground=person.feet.y-g;
          const auto below=roads->sample(diamond->at.x,diamond->at.y,g);
          city_probe.avenue_below=below.on && below.cls==RoadClass::Avenue;
          person.pitch=-0.15;
        } else if(rendered_frames==3) {
          std::optional<std::uint32_t> pick;
          for(std::uint32_t i=0;i<roads->size();++i)if(roads->def().segments[i].cls==RoadClass::Runway && roads->length(i)>100){pick=i;break;}
          if(!pick)throw std::runtime_error("city test found no runway");
          const auto mid=(roads->def().nodes[roads->def().segments[*pick].a].position+roads->def().nodes[roads->def().segments[*pick].b].position)*0.5;
          const auto dir=roads->direction(*pick);
          stand_at(mid.x,mid.y,std::atan2(-dir.x,-dir.y),0.0);
          const auto here=roads->sample(person.feet.x,person.feet.z,person.feet.y);
          city_probe.on_runway=here.on && here.cls==RoadClass::Runway;
          const terraflux::math::Vec2 right{-dir.y,dir.x};
          city_probe.runway_wide=roads->sample(mid.x+right.x*20,mid.y+right.y*20).on && roads->sample(mid.x-right.x*20,mid.y-right.y*20).on && !roads->sample(mid.x+right.x*26,mid.y+right.y*26).on;
        } else if(rendered_frames==4) {
          if(!set_view_mode(CameraMode::World,false))throw std::runtime_error("city test could not return to the world view");
          scene_camera.target=snap_orbit_target({cbd->centre.x,0,cbd->centre.y});
          distance=std::max(100.0,std::min(scene_camera.starting_distance*0.3,900.0));yaw=0.8;pitch=0.5;
        }
      }
      if(options.buildings_test) {
        // Frames: 0 the world view over the buildings, hidden (reference); 1 the
        // buildings shown; 2 first person on the road before the hospital's
        // footpath, walking to its door: the footpath carries and hurries the
        // walker, the front wall stops it at the door, the path meets the
        // carriageway; 3 hidden again: the reference exactly.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        using terraflux::graphics::BuildingKind;
        if(!roads || !buildings)throw std::runtime_error("buildings test needs the road network and the buildings");
        if(rendered_frames==0) {
          const auto st=buildings->stats();
          if(st.bungalows<1 || st.houses<1 || st.hospitals<1 || st.skyscrapers<1)throw std::runtime_error("buildings test needs a bungalow, a house, a hospital and a skyscraper");
          terraflux::math::Vec2 centroid;
          for(const auto& b:buildings->buildings())centroid=centroid+b.centre*(1.0/buildings->size());
          for(std::size_t i=0;i<buildings->size();++i)if(buildings->buildings()[i].kind==BuildingKind::Hospital)buildings_probe.target=i;
          roads_orbit={scene_camera.target,yaw,pitch,distance};
          scene_camera.target=snap_orbit_target({centroid.x,0,centroid.y});
          distance=std::max(60.0,std::min(scene_camera.starting_distance*0.2,420.0));yaw=0.7;pitch=0.38;
          buildings_visible=false;
        } else if(rendered_frames==1) {
          buildings_visible=true;
        } else if(rendered_frames==2) {
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("buildings test found no ground to stand on");
          const auto& b=buildings->buildings()[buildings_probe.target];
          const auto start=b.path_end-b.facing*2.0;  // on the carriageway, two metres before the footpath
          const auto gg=ground_height(start.x,start.y);
          if(!gg)throw std::runtime_error("buildings test start has no ground");
          const auto g=built_surface(start.x,start.y,*gg);
          person.feet={start.x,g?std::max(*g,*gg):*gg,start.y};person.grounded=true;person.yaw=std::atan2(b.facing.x,b.facing.y);person.pitch=0.1;
          person.vertical_velocity_mps=0;person.velocity_mps={};person.gait_speed_mps=0;person.gait_phase=0;
          (void)terraflux::graphics::settle(person,walk_env);
          buildings_probe=BuildingsProbe{};
          for(std::size_t i=0;i<buildings->size();++i)if(buildings->buildings()[i].kind==BuildingKind::Hospital)buildings_probe.target=i;
          for(int i=0;i<60*12;++i) {
            terraflux::graphics::walk(person,WalkInput{1,0,false},1.0/60.0,walk_env);
            if(buildings->platform_height(person.feet.x,person.feet.z) && person.on_platform){buildings_probe.on_path=true;buildings_probe.path_speed=std::max(buildings_probe.path_speed,person.surface_speed_factor);}
          }
          buildings_probe.door_distance=(terraflux::math::Vec2{person.feet.x,person.feet.z}-b.door).length();
          buildings_probe.inside=buildings->building_at(person.feet.x,person.feet.z).has_value();
          buildings_probe.stopped=buildings_probe.door_distance<2.5;
          buildings_probe.path_meets_road=roads->sample(b.path_end.x+b.facing.x*0.1,b.path_end.y+b.facing.y*0.1).on;
          buildings_probe.at=buildings->entrance_near(person.feet.x,person.feet.z,4.0)?terraflux::graphics::building_kind_name(b.kind):"";
          person.pitch=0.25;  // look up at the cross
        } else if(rendered_frames==3) {
          if(!set_view_mode(CameraMode::World,false))throw std::runtime_error("buildings test could not return to the world view");
          terraflux::math::Vec2 centroid;
          for(const auto& b:buildings->buildings())centroid=centroid+b.centre*(1.0/buildings->size());
          scene_camera.target=snap_orbit_target({centroid.x,0,centroid.y});
          distance=std::max(60.0,std::min(scene_camera.starting_distance*0.2,420.0));yaw=0.7;pitch=0.38;
          buildings_visible=false;
        }
      }
      if(options.interior_test) {
        // Frames: 0 first person on the bungalow's footpath, walking to its
        // closed door: the ramp carries the walker up to the floor's level and
        // the leaf stops it at the threshold, E offers to open it; 1 the door
        // opened (it swings in 0.6 s), walking in onto the timber floor until
        // the stove stops the walker, looking at the table; 2 walking at the
        // table: the chairs stop the walker; 3 the door closed from inside:
        // the walker cannot leave; 4 opened again and out down the ramp.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        using terraflux::graphics::BuildingKind;
        if(!roads || !buildings)throw std::runtime_error("interior test needs the road network and the buildings");
        auto& pr=interior_probe;
        const auto scripted_walk=[&](int frames,double forward) {
          for(int i=0;i<frames;++i) {
            buildings->advance(1.0/60.0);
            if(buildings->door_version()!=door_obstacles_version)rebuild_door_obstacles();
            terraflux::graphics::walk(person,WalkInput{forward,0,false},1.0/60.0,walk_env);
          }
        };
        const auto& b=buildings->buildings()[pr.target];
        const auto local=[&]() { const terraflux::math::Vec2 f{person.feet.x,person.feet.z}; return std::pair{(f-b.centre).dot(b.along),(f-b.centre).dot(b.facing)}; };
        if(rendered_frames==0) {
          std::optional<std::size_t> bungalow;
          for(std::size_t i=0;i<buildings->size();++i)if(buildings->buildings()[i].kind==BuildingKind::Bungalow){bungalow=i;break;}
          if(!bungalow)throw std::runtime_error("interior test needs a bungalow");
          pr.target=*bungalow;
          const auto& bb=buildings->buildings()[*bungalow];
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("interior test found no ground to stand on");
          const auto start=bb.door+bb.facing*3.5;  // on the footpath
          const auto gg=ground_height(start.x,start.y);
          if(!gg)throw std::runtime_error("interior test start has no ground");
          const auto g=built_surface(start.x,start.y,*gg);
          person.feet={start.x,g?std::max(*g,*gg):*gg,start.y};person.grounded=true;person.yaw=std::atan2(bb.facing.x,bb.facing.y);person.pitch=0.0;
          person.vertical_velocity_mps=0;person.velocity_mps={};person.gait_speed_mps=0;person.gait_phase=0;
          (void)terraflux::graphics::settle(person,walk_env);
          scripted_walk(60*4,1.0);
          pr.floor=bb.floor_meters;pr.feet_y=person.feet.y;
          pr.door_distance=(terraflux::math::Vec2{person.feet.x,person.feet.z}-bb.door).length();
          pr.at_door=door_in_reach().has_value();pr.inside=buildings->inside_of(person.feet.x,person.feet.z).has_value();
          pr.hint=interact_options();
        } else if(rendered_frames==1) {
          pr.did=interact();
          scripted_walk(60,0.0);  // the leaf swings
          pr.leaf_open=buildings->doors()[pr.target].open;
          scripted_walk(60*3,1.0);
          pr.inside=buildings->inside_of(person.feet.x,person.feet.z)==pr.target;
          pr.on_floor=person.on_platform && std::abs(person.feet.y-b.floor_meters)<1e-6;
          pr.feet_y=person.feet.y;
          // Look at the table from where the stove stopped the walker.
          const auto table=b.centre+b.along*1.6+b.facing*0.2;
          const auto to=table-terraflux::math::Vec2{person.feet.x,person.feet.z};
          person.yaw=std::atan2(-to.x,-to.y);person.pitch=-0.25;
          pr.hint=interact_options();
        } else if(rendered_frames==2) {
          const auto table=b.centre+b.along*1.6+b.facing*0.2;
          const auto before=table+b.facing*2.2;
          person.feet={before.x,b.floor_meters,before.y};person.yaw=std::atan2(b.facing.x,b.facing.y);person.pitch=-0.2;
          person.velocity_mps={};person.gait_speed_mps=0;
          scripted_walk(60*3,1.0);
          pr.table_gap=(terraflux::math::Vec2{person.feet.x,person.feet.z}-table).dot(b.facing);
          pr.inside=buildings->inside_of(person.feet.x,person.feet.z)==pr.target;
        } else if(rendered_frames==3) {
          // Back to the door from inside, close it, try to leave.
          const auto at=b.door-b.facing*1.2;
          person.feet={at.x,b.floor_meters,at.y};person.yaw=std::atan2(-b.facing.x,-b.facing.y);person.pitch=0.0;
          person.velocity_mps={};person.gait_speed_mps=0;
          pr.at_door=door_in_reach().has_value();
          pr.hint=interact_options();
          pr.did=interact();
          scripted_walk(60,0.0);
          pr.leaf_open=buildings->doors()[pr.target].open;
          scripted_walk(60*3,1.0);
          pr.kept_in=buildings->inside_of(person.feet.x,person.feet.z)==pr.target && local().second<0.5*b.spec.depth_meters;
        } else if(rendered_frames==4) {
          pr.did=interact();
          scripted_walk(60,0.0);
          scripted_walk(60*5,1.0);
          pr.out=!buildings->inside_of(person.feet.x,person.feet.z) && local().second>0.5*b.spec.depth_meters+2.0;
          pr.feet_y=person.feet.y;
          pr.door_distance=(terraflux::math::Vec2{person.feet.x,person.feet.z}-b.door).length();
          person.yaw=std::atan2(b.facing.x,b.facing.y);  // turn round and look back at the open door
        }
      }
      if(options.hazards_test) {
        // Frames: 0 a torch at the bungalow's door sets it alight: the fire
        // spreads over the thatch and the planks, its heat reaches the walker,
        // flames and smoke are drawn; 1 inside as the thatch burns through:
        // the roof falls in on the walker; out again as the walls burn through
        // and the bungalow collapses into rubble, walked over slowly, the
        // chimney standing; 2 a two-tonne trunk lands on the house: the tiles
        // are smashed and the roof falls in, the stone walls only cracked; its
        // timber door lit with the torch: the glass bursts in the heat, the
        // stone does not burn; 3 lightning strikes the skyscraper (scorched,
        // not lit) and a torrent at the hospital's walls scours its
        // foundation: washed out; 4 everything reset: sound again.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        using terraflux::graphics::BuildingKind;
        using terraflux::graphics::BuildingElement;
        using terraflux::graphics::BuildingEvent;
        if(!roads || !buildings || !tree_fire)throw std::runtime_error("hazards test needs the road network, the buildings and the tree fire model");
        auto& pr=hazards_probe;
        const auto stand_at=[&](terraflux::math::Vec2 p,double yaw) {
          const auto gg=ground_height(p.x,p.y);
          if(!gg)throw std::runtime_error("hazards test point has no ground");
          const auto g=built_surface(p.x,p.y,*gg);
          person.feet={p.x,g?std::max(*g,*gg):*gg,p.y};person.grounded=true;person.yaw=yaw;person.pitch=0.05;
          person.vertical_velocity_mps=0;person.velocity_mps={};person.gait_speed_mps=0;person.gait_phase=0;person.stunned_seconds=0;person.stun_crouch_meters=0;person.crouch_meters=0;
          (void)terraflux::graphics::settle(person,walk_env);
        };
        const auto run=[&](double seconds) { for(int i=0;i<static_cast<int>(seconds*60);++i)advance_buildings(1.0/60.0); };
        const auto saw=[&](BuildingEvent::Kind kind,std::size_t building) { for(const auto& e:buildings->events())if(e.kind==kind && e.building==building)return true; return false; };
        if(rendered_frames==0) {
          for(std::size_t i=0;i<buildings->size();++i) {
            switch(buildings->buildings()[i].kind) {
              case BuildingKind::Bungalow:pr.bungalow=i;break;
              case BuildingKind::House:pr.house=i;break;
              case BuildingKind::Hospital:pr.hospital=i;break;
              case BuildingKind::Skyscraper:pr.tower=i;break;
              default:break;
            }
          }
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("hazards test found no ground to stand on");
          const auto& b=buildings->buildings()[pr.bungalow];
          stand_at(b.door+b.along*3.0+b.facing*1.0,std::atan2(b.facing.x,b.facing.y));  // beside the front wall, off the door
          held.kind=terraflux::graphics::HeldKind::Torch;held.size=0.8;held.torch_seconds=terraflux::graphics::kTorchSeconds;
          pr.hint=interact_options();
          pr.did=interact();
          pr.version=buildings->version();
          const auto lit_before=tree_fire->stats().ignited_by_radiation;
          run(8.0);
          pr.trees_lit=tree_fire->stats().ignited_by_radiation-lit_before;
          const auto& st=buildings->states()[pr.bungalow];
          pr.alight=st.alight() && st.element(BuildingElement::Walls).alight && st.element(BuildingElement::Roof).alight && st.element(BuildingElement::Door).alight;
          pr.heat=buildings->exposure_at(person.feet.x,person.feet.z).heat;
          std::vector<FireInstance> flames,smoke;
          buildings->emit(flames,smoke,fire_style);
          pr.flames=static_cast<double>(flames.size());
          pr.events=buildings->events().size();
          held.kind=terraflux::graphics::HeldKind::Nothing;
          stand_at(b.door+b.facing*9.0,std::atan2(b.facing.x,b.facing.y));
          person.pitch=0.15;
        } else if(rendered_frames==1) {
          const auto& b=buildings->buildings()[pr.bungalow];
          person.feet={b.centre.x,b.floor_meters,b.centre.y};person.grounded=true;person.yaw=std::atan2(b.facing.x,b.facing.y);person.pitch=0.3;
          person.stunned_seconds=0;person.velocity_mps={};
          pending_collapse=0;
          const double health_before=vitals.health;
          bool fell=false;
          for(int i=0;i<60*40 && !fell;++i){advance_buildings(1.0/60.0);if(saw(BuildingEvent::Kind::RoofFell,pr.bungalow))fell=true;}
          pr.roof_fell_on_me=fell && pending_collapse>0 && person.stunned_seconds>0;
          apply_vitals(terraflux::graphics::advance_vitals(vitals,gather_exposure(),1.0/60.0,vitals_style));
          pr.health_lost=health_before-vitals.health;
          stand_at(b.door+b.facing*9.0,std::atan2(b.facing.x,b.facing.y));
          for(int i=0;i<60*120 && !buildings->states()[pr.bungalow].destroyed;++i)advance_buildings(1.0/60.0);
          pr.destroyed=buildings->states()[pr.bungalow].destroyed;pr.destroyed_by=buildings->states()[pr.bungalow].destroyed_by;
          if(buildings->version()!=building_obstacles_version)rebuild_building_obstacles();
          stand_at(b.centre,std::atan2(b.facing.x,b.facing.y));
          for(int i=0;i<30;++i)terraflux::graphics::walk(person,WalkInput{0.3,0,false},1.0/60.0,walk_env);
          pr.feet_y=person.feet.y;pr.floor=b.floor_meters;
          pr.on_rubble=person.on_platform && std::abs(person.feet.y-(b.floor_meters+0.45))<1e-6 && std::abs(walk_env.surface_speed(person.feet.x,person.feet.z)-building_style.rubble_speed_factor)<1e-9 &&
                       std::abs(person.surface_speed_factor-building_style.rubble_speed_factor)<1e-9;
          stand_at(b.door+b.facing*8.0,std::atan2(b.facing.x,b.facing.y));
          person.pitch=0.05;
        } else if(rendered_frames==2) {
          const auto& h=buildings->buildings()[pr.house];
          // The torch at the house's stone wall lights nothing; its timber door is lit by hand (as the fire would from beside it).
          stand_at(h.door+h.along*3.0+h.facing*1.0,std::atan2(h.facing.x,h.facing.y));
          held.kind=terraflux::graphics::HeldKind::Torch;held.size=0.8;held.torch_seconds=terraflux::graphics::kTorchSeconds;
          pr.hint=interact_options();
          pr.did="";
          held.kind=terraflux::graphics::HeldKind::Nothing;
          buildings->ignite(pr.house,BuildingElement::Door,"torch");
          handle_building_events();
          run(12.0);
          const auto& st=buildings->states()[pr.house];
          pr.windows_burst=st.element(BuildingElement::Windows).damage>=1.0 && st.element(BuildingElement::Windows).cause=="heat";
          pr.house_stands=!st.destroyed && !st.element(BuildingElement::Walls).alight && st.element(BuildingElement::Door).alight && st.element(BuildingElement::Walls).damage<0.05;
          // Then a two-tonne trunk lands across it.
          buildings->log_landed(0,h.centre-h.along*20.0,h.along,30.0,false,2000.0);
          handle_building_events();
          pr.house_roof_fell=st.roof_fallen && saw(BuildingEvent::Kind::Hit,pr.house) && saw(BuildingEvent::Kind::RoofFell,pr.house);
          pr.house_roof_cause=st.element(BuildingElement::Roof).cause;
          pr.walls_damage=st.element(BuildingElement::Walls).damage;
          stand_at(h.door+h.facing*10.0,std::atan2(h.facing.x,h.facing.y));
          person.pitch=0.2;
        } else if(rendered_frames==3) {
          const auto& t=buildings->buildings()[pr.tower];
          const auto struck=buildings->strike(t.centre.x+0.5*t.spec.width_meters+4.0,t.centre.y);
          handle_building_events();
          pr.struck=struck==pr.tower && buildings->states()[pr.tower].strikes==1 && !buildings->states()[pr.tower].alight() &&
                    std::abs(buildings->states()[pr.tower].element(BuildingElement::Roof).damage-building_style.strike_scorch)<1e-9;
          // A torrent at the hospital's walls (the flood model's water, were the channels there).
          const auto& hosp=buildings->buildings()[pr.hospital];
          terraflux::graphics::BuildingHazards torrent;
          torrent.water=[&](double x,double z,double ground_y) {
            terraflux::graphics::FloodSample w;
            if(buildings->building_at(x,z,3.0)==pr.hospital){w.depth_meters=1.5;w.surface_meters=ground_y+1.5;w.current_mps=4.0;w.current={1,0};w.channel=true;}
            else w.surface_meters=ground_y;
            return w;
          };
          for(int i=0;i<static_cast<int>((building_style.flood_scour_seconds+2.0)*4) && !buildings->states()[pr.hospital].destroyed;++i){buildings->advance(0.25,torrent);building_events_handled=0;handle_building_events();}
          pr.washed_out=buildings->states()[pr.hospital].destroyed && buildings->states()[pr.hospital].destroyed_by=="flood" && !buildings->states()[pr.tower].destroyed;
          stand_at(hosp.path_end+hosp.facing*2.0,std::atan2(hosp.facing.x,hosp.facing.y));
          person.pitch=0.1;
        } else if(rendered_frames==4) {
          buildings->reset();
          building_events_handled=0;
          const auto st=buildings->stats();
          pr.sound=st.alight==0 && st.damaged==0 && st.destroyed==0 && st.roofless==0 && st.struck==0 && st.logs==0;
          const auto& b=buildings->buildings()[pr.bungalow];
          stand_at(b.door+b.facing*8.0,std::atan2(b.facing.x,b.facing.y));
        }
      }
      if(options.flood_test) {
        // Frames: 0 dry, standing in first person in the terrain's largest
        // drainage channel (reference); 1 after half an hour of thunderstorm
        // rain: the channel runs, the flood surface is drawn, the walker stands
        // in water with a current (and is swept off its feet if it is fast and
        // deep enough); 2 three seconds of trying to walk upstream: carried
        // downstream; 3 the wave gone and the walker back: the reference exactly.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        if(rendered_frames==0) {
          // The wettest point of the terrain under a full wave: deepest channel water with a current.
          flood.set_flash(1.0);
          double best=0;std::size_t pick=0;
          for(std::size_t i=0;i<mesh.vertices.size();++i) {
            const auto& v=mesh.vertices[i];
            const auto w=flood.sample(v.position.x,v.position.z,v.position.y,water_style?std::optional<double>(water_style->level_meters):std::nullopt);
            if(!w.channel || w.current_mps<=0)continue;
            const double score=w.depth_meters*(1.0+0.2*w.current_mps);
            if(score>best){best=score;pick=i;}
          }
          if(!(best>0))throw std::runtime_error("flood test found no channel on this terrain");
          flood.set_flash(0.0);
          const auto& v=mesh.vertices[pick];
          scene_camera.target={v.position.x,v.position.y,v.position.z};yaw=0.0;
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("flood test found no ground to stand on");
          const auto h=ground_height(v.position.x,v.position.z);
          if(!h)throw std::runtime_error("flood test channel point has no ground");
          person.feet={v.position.x,*h,v.position.z};person.grounded=true;
          const auto dry=flood.sample(v.position.x,v.position.z,*h,std::nullopt);
          (void)dry;
          flood_probe=FloodProbe{};flood_probe.x=v.position.x;flood_probe.z=v.position.z;
          view_test_person=person;
          person.pitch=-0.35; view_test_person.pitch=person.pitch;
        } else if(rendered_frames==1) {
          flood.advance(1800.0,25.0);  // half an hour of a thunderstorm's rain
          flood_probe.flash=flood.flash();
          terraflux::graphics::walk(person,WalkInput{},1.0/60.0,walk_env);
          flood_probe.depth=person.water_depth_meters;flood_probe.current=person.current_mps;
          flood_probe.swept=person.swept || person.stunned_seconds>0;flood_probe.swimming=person.swimming;
        } else if(rendered_frames==2) {
          const auto start=person.feet;
          const auto here=walk_env.water(person.feet.x,person.feet.z,person.feet.y);
          person.yaw=std::atan2(here.current.x,here.current.y); // forward (-sin, -cos) = -current: upstream
          for(int i=0;i<180;++i)terraflux::graphics::walk(person,WalkInput{1,0,false},1.0/60.0,walk_env);
          flood_probe.carried=(person.feet.x-start.x)*here.current.x+(person.feet.z-start.z)*here.current.y;
        } else if(rendered_frames==3) {
          flood.set_flash(0.0);
          person=view_test_person;
        }
      }
      if(options.storm_test) {
        // Frames: 0 a thunderstorm over the terrain, standing in first person at
        // its centre; 1 a bright strike ten metres away, next to a tree: the tree
        // is split and set alight, the walker struck down and shaking; 2 six
        // seconds later: the tree is down and burning, the walker up and moving
        // again; 3 a 40 m/s gust front for eight seconds: trees uprooted, the
        // standing walker dragged downwind (a 36 m/s gust uproots the weakest-rooted trees, not the wood).
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        if(!tree_fire || !sky)throw std::runtime_error("storm test needs the tree fire model and the sky");
        if(rendered_frames==0) {
          auto config=weather_config;
          config.enabled=true;config.clouds=CloudType::Cumulonimbus;config.daylight.start_hour=15;config.daylight.seconds_per_hour=0.0;
          weather.set_config(config);weather.reset();
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("storm test found no ground to stand on");
          storm_probe=StormProbe{};
        } else if(rendered_frames==1) {
          const auto* field=weather.field();
          if(!field || !field->width)throw std::runtime_error("storm test has no orographic field yet");
          const auto& config=weather.config();
          bool found=false;
          for(double t=0.05;t<7200 && !found;t+=0.05) {
            const auto strike=lightning_strike_at(t,1.0,config.seed);
            if(!strike.active || strike.intensity<0.85 || strike.age_seconds>0.05)continue;
            const double x=field->x0+strike.u*field->spacing*(field->width-1),z=field->z0+strike.v*field->spacing*(field->height-1);
            const auto trunk=tree_field.clearance(x,z,tree_fire->style().strike_radius_meters);
            if(!trunk)continue;
            // Stand ten metres from the bolt, on ground, clear of trunks.
            bool placed=false;
            for(int k=0;k<8 && !placed;++k) {
              const double a=k*3.14159265358979323846/4.0;
              const double px=x+10.0*std::cos(a),pz=z+10.0*std::sin(a);
              const auto h=standing_height(px,pz);
              const auto near_trunk=tree_field.clearance(px,pz,1.5);
              if(!h || (near_trunk && *near_trunk<0.5))continue;
              person.feet={px,*h,pz};person.grounded=true;person.yaw=std::atan2(-(x-px),-(z-pz));person.pitch=0.3;
              placed=terraflux::graphics::settle(person,walk_env);
            }
            if(!placed)continue;
            storm_probe.strike_x=x;storm_probe.strike_z=z;
            weather.set_time(t);
            found=true;
          }
          if(!found)throw std::runtime_error("storm test found no bright strike beside a tree");
        } else if(rendered_frames==2) {
          tree_fire->advance(6.0,std::min(60.0,weather.wind().speed_mps),weather.wind().direction_degrees);
          const auto start=person.feet;
          for(int i=0;i<360;++i)terraflux::graphics::walk(person,WalkInput{1,0,false},1.0/60.0,walk_env);
          storm_probe.walked=std::hypot(person.feet.x-start.x,person.feet.z-start.z);
        } else if(rendered_frames==3) {
          const auto before=tree_fire->stats().windthrown;
          tree_fire->advance(8.0,36.0,90.0);
          storm_probe.windthrown=tree_fire->stats().windthrown-before;
          auto gale=walk_env;gale.wind_velocity_mps={36.0,0.0};
          const auto start=person.feet;
          for(int i=0;i<120;++i)terraflux::graphics::walk(person,WalkInput{},1.0/60.0,gale);
          storm_probe.drift=person.feet.x-start.x;
        }
      }
      if(options.tree_fire_test) {
        // Frames: 0 standing twelve metres from the nearest tree the replay's
        // front has just ignited (reference, model at time zero); 1 three
        // replay seconds later (flames, branches); 2 fifteen seconds later
        // (trees down, burned out, neighbours lit by trees, logs on the ground);
        // 3 the model reset and re-synced, which must restore the reference exactly.
        using terraflux::graphics::CameraMode;
        if(!tree_fire)throw std::runtime_error("tree fire test needs the tree fire model");
        if(rendered_frames==0) {
          std::optional<std::size_t> pick;double best=1e300;
          const auto& trees=vegetation->placement().trees;
          for(std::size_t i=0;i<trees.size();++i) {
            if(tree_fire->states()[i].stage!=TreeFireStage::Ignited)continue;
            const double d=std::hypot(trees[i].position[0]-scene_camera.target.x,trees[i].position[2]-scene_camera.target.z);
            if(d<best){best=d;pick=i;}
          }
          if(!pick)throw std::runtime_error("tree fire test found no tree ignited by the replay frame");
          const auto& t=trees[*pick];
          scene_camera.target={t.position[0]-12.0,t.position[1],t.position[2]};yaw=0.0;
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("tree fire test found no ground to stand on");
          person.yaw=std::atan2(-(t.position[0]-person.feet.x),-(t.position[2]-person.feet.z));person.pitch=0.15;
          tree_fire_obstacle_version_start=tree_fire->obstacle_version();
        } else if(rendered_frames==1) {
          tree_fire->advance(3.0,fire_style.wind_speed_mps,fire_style.wind_direction_degrees);
        } else if(rendered_frames==2) {
          tree_fire->advance(15.0,fire_style.wind_speed_mps,fire_style.wind_direction_degrees);
        } else if(rendered_frames==3) {
          tree_fire->reset();
          tree_fire->ignite_from_field(*fire_field,mesh.origin_grid_x,mesh.origin_grid_y,cell_size,true);
        }
        report_tree_fire("test");
      }
      if(options.gait_test) {
        // Frames: 0 standing under the terrain centre (reference); 1 mid-step
        // after a second of walking along the most level clear heading (head
        // bobbing and swaying, the speed pulsing per step); 2 a second after
        // releasing the keys: stopped on a footfall with the head exactly at
        // rest; 3 back where it started, which must restore the reference exactly.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        constexpr double kTick=1.0/60.0;
        if(rendered_frames==0) {
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("gait test found no ground to stand on");
          if(!stand_on_dry_ground())throw std::runtime_error("gait test found no dry ground");
          gait_probe=GaitProbe{};view_test_person=person;
        } else if(rendered_frames==1) {
          double best_yaw=person.yaw,best_rise=1e300;
          for(int i=0;i<16;++i) {
            const double yaw=i*3.14159265358979323846/8.0;
            const auto forward=terraflux::graphics::horizontal_forward(yaw);
            double rise=0;bool ok=true;
            for(double d=1;d<=24 && ok;d+=1) {
              const double x=person.feet.x+forward.x*d,z=person.feet.z+forward.z*d;
              const auto h=walk_env.standing_height(x,z);
              const auto trunk=tree_field.clearance(x,z,1.5);
              if(!h || (trunk && *trunk<1.0) || walk_env.water(x,z,*h).depth_meters>0)ok=false;else rise=std::max(rise,std::abs(*h-person.feet.y));
            }
            if(ok && rise<best_rise){best_rise=rise;best_yaw=yaw;}
          }
          if(best_rise>=1e300)throw std::runtime_error("gait test found no level walk");
          person.yaw=best_yaw;
          for(int i=0;i<60;++i)terraflux::graphics::walk(person,WalkInput{1,0,false},kTick,walk_env);
          const auto at_one_second=person.footfalls;
          // Walk on until mid-step so the frame shows the head up and to the side.
          for(int i=0;i<60;++i) {
            const double d=terraflux::graphics::walk(person,WalkInput{1,0,false},kTick,walk_env);
            gait_probe.slowest=std::min(gait_probe.slowest,d/kTick);gait_probe.fastest=std::max(gait_probe.fastest,d/kTick);
            gait_probe.max_bob=std::max(gait_probe.max_bob,person.bob_meters);
            gait_probe.max_sway=std::max(gait_probe.max_sway,person.sway_meters);gait_probe.min_sway=std::min(gait_probe.min_sway,person.sway_meters);
            if(i>=30 && person.gait_phase>0.4 && person.gait_phase<0.6)break;
          }
          gait_probe.footfalls=person.footfalls-at_one_second;
        } else if(rendered_frames==2) {
          gait_probe.release_x=person.feet.x;gait_probe.release_z=person.feet.z;
          for(int i=0;i<120;++i)terraflux::graphics::walk(person,WalkInput{},kTick,walk_env);
          gait_probe.stop_distance=std::hypot(person.feet.x-gait_probe.release_x,person.feet.z-gait_probe.release_z);
        } else if(rendered_frames==3) {
          person=view_test_person;
        }
      }
      if(options.jump_test) {
        // Frames: 0 standing on dry ground near the terrain centre (reference);
        // 1 at the apex of a standing jump; 2 just landed, eye dipped; 3 two
        // seconds later, recovered, which must restore the reference exactly;
        // 4 after a running jump along gentle ground, landed further on.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        constexpr double kTick=1.0/60.0;
        if(rendered_frames==0) {
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("jump test found no ground to stand on");
          if(!stand_on_dry_ground())throw std::runtime_error("jump test found no dry ground");
          jump_probe=JumpProbe{};jump_probe.ground=person.feet.y;
        } else if(rendered_frames==1) {
          terraflux::graphics::walk(person,WalkInput{0,0,false,true},kTick,walk_env);
          for(int i=0;i<600 && person.vertical_velocity_mps>0;++i) {
            terraflux::graphics::walk(person,WalkInput{},kTick,walk_env);
            jump_probe.apex=std::max(jump_probe.apex,person.feet.y-jump_probe.ground);
          }
        } else if(rendered_frames==2) {
          for(int i=0;i<600 && !person.grounded;++i) {
            const double airborne=person.air_time_seconds;
            terraflux::graphics::walk(person,WalkInput{},kTick,walk_env);
            if(person.grounded)jump_probe.air_time=airborne+kTick;
          }
          jump_probe.landed=person.grounded;jump_probe.dip=person.crouch_meters;
        } else if(rendered_frames==3) {
          for(int i=0;i<120;++i)terraflux::graphics::walk(person,WalkInput{},kTick,walk_env);
        } else if(rendered_frames==4) {
          // The most level of sixteen headings clear of trunks for the run-up and
          // the flight (half a second of walking plus the carry), then jump.
          double best_yaw=person.yaw,best_rise=1e300;
          const double needed=person.walk_speed_mps*(0.5+terraflux::graphics::jump_air_time(walk_env.physics))+3.0;
          for(int i=0;i<16;++i) {
            const double yaw=i*3.14159265358979323846/8.0;
            const auto forward=terraflux::graphics::horizontal_forward(yaw);
            double rise=0;bool ok=true;
            for(double d=1;d<=needed && ok;d+=1) {
              const double x=person.feet.x+forward.x*d,z=person.feet.z+forward.z*d;
              const auto h=walk_env.standing_height(x,z);
              const auto trunk=tree_field.clearance(x,z,1.5);
              if(!h || (trunk && *trunk<1.0) || walk_env.water(x,z,*h).depth_meters>0)ok=false;
              else rise=std::max(rise,std::abs(*h-person.feet.y));
            }
            if(ok && rise<best_rise){best_rise=rise;best_yaw=yaw;}
          }
          if(best_rise>=1e300)throw std::runtime_error("jump test found no level run-up");
          person.yaw=best_yaw;
          const auto start=person.feet;
          for(int i=0;i<30;++i)terraflux::graphics::walk(person,WalkInput{1,0,false},kTick,walk_env);
          const auto takeoff=person.feet;
          terraflux::graphics::walk(person,WalkInput{1,0,false,true},kTick,walk_env);
          double airborne=0;
          for(int i=0;i<600 && !person.grounded;++i){airborne=person.air_time_seconds;terraflux::graphics::walk(person,WalkInput{1,0,false},kTick,walk_env);}
          jump_probe.landed=person.grounded;
          jump_probe.carried=std::hypot(person.feet.x-takeoff.x,person.feet.z-takeoff.z);
          jump_probe.expected_carry=person.walk_speed_mps*(airborne+kTick);
          (void)start;
        }
      }
      if(options.collision_test) {
        // Frames: 0 four metres from a tree trunk, facing it, after two seconds
        // of running at it; 1 three metres below the steepest facet of the
        // terrain, after a second of walking up it; 2 two metres from a grass
        // tuft in the open, after a second of walking through it; 3 the same
        // run at the trunk with collision disabled, which must pass through.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        if(rendered_frames==0 && !set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("collision test found no ground to stand on");
        collision_probe=CollisionProbe{};
        const auto face=[&](terraflux::math::Vec3 from,terraflux::math::Vec3 toward) {
          person.feet=from;person.pitch=0;
          person.yaw=std::atan2(-(toward.x-from.x),-(toward.z-from.z)); // forward is (-sin yaw, 0, -cos yaw)
          if(!terraflux::graphics::settle(person,walk_env))throw std::runtime_error("collision test could not stand at its probe");
          collision_probe.start=person.feet;collision_probe.target=toward;
        };
        const auto gentle_around=[&](double x,double z,double reach) {
          double lo=1e300,hi=-1e300;
          for(int i=0;i<8;++i) {
            const double a=i*3.14159265358979323846/4.0;
            const auto h=standing_height(x+reach*std::cos(a),z+reach*std::sin(a));
            if(!h)return false;
            lo=std::min(lo,*h);hi=std::max(hi,*h);
          }
          return hi-lo<reach*0.5;
        };
        // Dry ground all along a run (wading slows the walker, which is not what these probes measure).
        const auto dry_run=[&](double x0,double z0,double x1,double z1) {
          if(!water_style)return true;
          for(int i=0;i<=20;++i) {
            const double f=i/20.0;
            const auto h=standing_height(x0+(x1-x0)*f,z0+(z1-z0)*f);
            if(!h || *h<water_style->level_meters+0.3)return false;
          }
          return true;
        };
        if(rendered_frames==0 || rendered_frames==3) {
          // The tree nearest the terrain centre standing on gentle, open ground.
          if(!vegetation || tree_field.empty())throw std::runtime_error("collision test needs placed trees");
          const auto& trees=vegetation->placement().trees;
          const auto centre=scene_camera.target;
          std::optional<std::size_t> pick;double best=1e300;
          double approach=6.0;
          for(std::size_t i=0;i<trees.size();++i) {
            const auto& t=trees[i];
            const double d=std::hypot(t.position[0]-centre.x,t.position[2]-centre.z);
            if(d>=best)continue;
            const double reach=tree_field.obstacles()[i].radius+6.0; // start six metres short of the footprint
            if(!gentle_around(t.position[0],t.position[2],reach))continue;
            if(!dry_run(t.position[0]-reach,t.position[2],t.position[0]+reach*3.0,t.position[2]))continue; // the run through (with --no-collision) stays dry
            const auto neighbour=tree_field.clearance(t.position[0]-reach,t.position[2],5.0);
            if(neighbour && *neighbour<1.5)continue; // another trunk within the approach
            pick=i;best=d;approach=reach;
          }
          if(!pick)throw std::runtime_error("collision test found no tree on gentle open ground");
          const auto& t=trees[*pick];
          const terraflux::math::Vec3 trunk{t.position[0],t.position[1],t.position[2]};
          collision_probe.radius=tree_field.obstacles()[*pick].radius;
          face({trunk.x-approach,trunk.y,trunk.z},trunk);
          if(rendered_frames==3){walk_env.obstacles.clear();walk_env.body.max_slope_degrees=89.0;}
          collision_probe.requested=std::max(person.walk_speed_mps*person.run_multiplier*2.0,approach*3.0);
          collision_probe.walked=terraflux::graphics::walk(person,WalkInput{1,0,true},collision_probe.requested/(person.walk_speed_mps*person.run_multiplier),walk_env);
          collision_probe.clearance=std::hypot(person.feet.x-trunk.x,person.feet.z-trunk.z)-collision_probe.radius;
        } else if(rendered_frames==1) {
          // The steepest render facet: its upward normal leans downhill.
          std::size_t steepest=0;double lowest_ny=2;
          for(std::size_t i=0;i<mesh.vertices.size();++i) {
            const auto n=mesh.vertices[i].normal.normalized();
            if(n.y<lowest_ny && n.y>0){lowest_ny=n.y;steepest=i;}
          }
          const auto& v=mesh.vertices[steepest];
          const double slope=std::acos(std::clamp(lowest_ny,-1.0,1.0))*180.0/3.14159265358979323846;
          if(slope<=walk_env.body.max_slope_degrees+5.0) {
            // Nothing steep enough to be a wall on this terrain (replay data can be gentle): the cliff check is skipped.
            std::cout<<"COLLISION_TEST_NOTE=no facet steeper than "<<walk_env.body.max_slope_degrees+5.0<<" degrees (steepest "<<slope<<"); cliff check skipped\n";
            collision_probe.requested=-1;
          } else {
          const auto n=v.normal.normalized();
          const double horizontal=std::hypot(n.x,n.z);
          const terraflux::math::Vec3 downhill{n.x/horizontal,0,n.z/horizontal};
          const auto foot=v.position+downhill*3.0;
          face({foot.x,foot.y,foot.z},v.position);
          collision_probe.requested=person.walk_speed_mps*1.0;
          collision_probe.walked=terraflux::graphics::walk(person,WalkInput{1,0,false},1.0,walk_env);
          collision_probe.rise=person.feet.y-collision_probe.start.y;
          collision_probe.clearance=v.position.y-person.feet.y;
          }
        } else if(rendered_frames==2) {
          // A grass tuft in the open: nothing to bump into.
          const auto& grass=vegetation->placement().grass;
          std::optional<std::size_t> pick;double best=1e300;
          for(std::size_t i=0;i<grass.size();++i) {
            const auto& g=grass[i];
            const double d=std::hypot(g.position[0]-scene_camera.target.x,g.position[2]-scene_camera.target.z);
            if(d>=best || !gentle_around(g.position[0],g.position[2],6.0) || tree_field.clearance(g.position[0],g.position[2],12.0))continue;
            if(!dry_run(g.position[0]-2.5,g.position[2],g.position[0]+8.0,g.position[2]))continue;
            pick=i;best=d;
          }
          if(!pick)throw std::runtime_error("collision test found no grass tuft in the open");
          const auto& g=grass[*pick];
          const terraflux::math::Vec3 tuft{g.position[0],g.position[1],g.position[2]};
          face({tuft.x-2.0,tuft.y,tuft.z},tuft);
          collision_probe.requested=person.walk_speed_mps*3.0;
          collision_probe.walked=terraflux::graphics::walk(person,WalkInput{1,0,false},3.0,walk_env);
          collision_probe.clearance=person.feet.x-tuft.x; // positive: the tuft is behind us
        }
      }
      if(options.first_person_test) {
        // Frames: 0 the world view of the terrain centre with its target on the
        // ground (reference), 1 first person standing under it, 2 a quarter turn
        // to the right, 3 a second of running, 4 back where you entered and
        // back to the world view, which must restore the reference exactly.
        using terraflux::graphics::CameraMode;
        using terraflux::graphics::WalkInput;
        if(rendered_frames==0) {
          distance=scene_camera.starting_distance*0.3;yaw=0.45;pitch=0.75;
          scene_camera.target=snap_orbit_target(scene_camera.target);
          view_test_orbit={scene_camera.target,yaw,pitch,distance};
        } else if(rendered_frames==1) {
          if(!set_view_mode(CameraMode::FirstPerson,false))throw std::runtime_error("first-person test found no ground to stand on");
          view_test_person=person;
        } else if(rendered_frames==2) {
          terraflux::graphics::look(person,3.14159265358979323846/2.0,0.0);
        } else if(rendered_frames==3) {
          // Run for a second; a hole or the edge right in front turns you around.
          for(const WalkInput input:{WalkInput{1,0,true},WalkInput{-1,0,true},WalkInput{0,1,true},WalkInput{0,-1,true}}) {
            view_test_walked=terraflux::graphics::walk(person,input,1.0,walk_env);
            if(view_test_walked>0)break;
          }
          if(!(view_test_walked>0))throw std::runtime_error("first-person test could not walk in any direction");
        } else if(rendered_frames==4) {
          person=view_test_person;
          if(!set_view_mode(CameraMode::World,false))throw std::runtime_error("first-person test could not return to the world view");
        }
      }
      const bool draw_surface=layer_visibility.surface_visible(playback.has_value());
      if(layer_buffer) {
        const auto flags=layer_visibility.uniform();
        device.update_buffer(*layer_buffer,0,std::as_bytes(std::span(&flags,1)));
      }
      if(options.smoke_test || layer_changed)
        std::cout<<"LAYERS_JSON={\"terrain\":"<<(layer_visibility.terrain?"true":"false")
                 <<",\"fire\":"<<(layer_visibility.fire?"true":"false")
                 <<",\"fire_available\":"<<(playback?"true":"false")
                 <<",\"surface_visible\":"<<(draw_surface?"true":"false")<<"}\n";
      const bool first_person=view_mode==terraflux::graphics::CameraMode::FirstPerson;
      const CameraPose pose=first_person?first_person_pose(person,scene_camera.starting_distance):
          orbit_pose({scene_camera.target,yaw,pitch,distance});
      const auto camera_frame=update_camera(device, camera, scene_width, scene_height, pose,
                    cell_size, fire_field ? fire_field->width : 1,
                    fire_field ? fire_field->height : 1,
                    (options.layers_test || options.replay_controls_test || options.terrain_shading_test || options.water_test ||
                     options.vegetation_test || options.fire_test || options.first_person_test || options.collision_test || options.jump_test || options.gait_test || options.tree_fire_test || options.storm_test || options.flood_test || options.climb_test || options.vitals_test || options.body_test || options.items_test || options.tools_test || options.roads_test || options.buildings_test || options.interior_test || options.hazards_test || options.city_test)?0.0:engine_time);
      const auto& camera_vp=camera_frame.view_projection;
      const auto terrain_frustum=Frustum::from_view_projection(camera_vp);
      // Atmosphere for this frame: the camera basis for the sky rays, the sun,
      // the cloud layer and the wind; the orographic field follows the wind
      // and is recomputed between passes whenever it veers or gusts enough.
      weather_state=AtmosphereState{};
      if(weather.config().enabled) {
        const auto wind=weather.wind();
        if(weather.field_stale(wind)) {
          const auto field=compute_orographic_field(height_field,wind,weather.layer(),weather.config().stability_hz);
          atmosphere.update_field(field);
          weather.mark_field(wind,field);
          report_weather("field");
        }
        CameraBasis basis;
        basis.eye=camera_frame.eye;
        basis.forward=(pose.target-camera_frame.eye).normalized();
        basis.right=basis.forward.cross({0,1,0}).normalized();
        basis.up=basis.right.cross(basis.forward).normalized();
        basis.tan_half_fov_y=std::tan(3.14159265358979323846/6.0);
        basis.tan_half_fov_x=basis.tan_half_fov_y*static_cast<double>(scene_width)/static_cast<double>(scene_height);
        // The ground altitude under the viewer: the standing point in first person, the orbit target otherwise.
        weather_state=weather.state(basis,first_person?person.feet.y:scene_camera.target.y);
        weather_state.rock_relief_meters=std::clamp(rendered_rock_relief,0.0,50.0);
      }
      atmosphere.update(weather_state);
      last_rain_mm_h=weather_state.enabled?weather_state.precipitation_mm_h:0.0;
      last_runoff=weather_state.enabled?weather_state.runoff:0.0;
      last_snow_cover=weather_state.enabled?weather_state.snow_cover:0.0;
      last_temperature=weather_state.enabled?weather_state.temperature_celsius:18.0;
      // A new lightning strike: it splits and lights trees, and it is a danger
      // to someone standing in the scene.
      if(weather_state.enabled && weather_state.strike.active && weather_state.strike.slot!=last_strike_slot) {
        last_strike_slot=weather_state.strike.slot;
        const double sx=weather_state.strike_position.x,sz=weather_state.strike_position.z;
        int struck_tree=-1;
        if(tree_fire)struck_tree=tree_fire->strike(sx,sz,std::min(60.0,weather_state.wind.speed_mps),weather_state.wind.direction_degrees);
        std::optional<std::size_t> struck_building;
        if(buildings){struck_building=buildings->strike(sx,sz);handle_building_events();}
        terraflux::graphics::LightningEffect effect;
        double distance=-1.0;
        const bool under_a_roof=first_person && buildings && buildings->sheltered(person.feet.x,person.feet.z);
        if(first_person && under_a_roof) {
          // Inside a building the walker is sheltered from the bolt (and from the rain).
          distance=std::hypot(person.feet.x-sx,person.feet.z-sz);
          if(distance<60.0){terraflux::graphics::shake(person,0.04);lightning_message="LIGHTNING "+std::to_string(static_cast<int>(std::lround(distance)))+" m away: sheltered inside";lightning_message_until=engine_time+6.0;std::cout<<lightning_message<<'\n';}
        } else if(first_person) {
          distance=std::hypot(person.feet.x-sx,person.feet.z-sz);
          // Crouched, the walker is a smaller target: the strike counts as farther away.
          effect=terraflux::graphics::lightning_effect(person.crouched?distance/(1.0-vitals_style.crouched_shelter):distance);
          pending_lightning=effect.hazard;
          if(effect.knock_down_seconds>0)terraflux::graphics::knock_down(person,effect.knock_down_seconds,effect.crouch_meters);
          if(effect.shake_meters>0)terraflux::graphics::shake(person,effect.shake_meters);
          if(effect.hazard!=terraflux::graphics::LightningHazard::None) {
            lightning_message=std::string("LIGHTNING ")+std::to_string(static_cast<int>(std::lround(distance)))+" m away: "+
                (effect.hazard==terraflux::graphics::LightningHazard::Struck?"you were struck down":
                 effect.hazard==terraflux::graphics::LightningHazard::Dangerous?"knocked off your feet":"the shock wave hits you");
            lightning_message_until=engine_time+6.0;
            std::cout<<lightning_message<<'\n';
          }
        }
        ++lightning_strikes;
        if(options.smoke_test || options.profile || options.storm_test)
          std::cout<<"LIGHTNING_JSON={\"strike\":"<<lightning_strikes<<",\"position\":["<<sx<<","<<sz<<"],\"intensity\":"<<weather_state.strike.intensity
                   <<",\"struck_tree\":"<<struck_tree<<",\"struck_building\":"<<(struck_building?static_cast<long long>(*struck_building):-1LL)<<",\"distance_m\":"<<distance<<",\"hazard\":\""<<terraflux::graphics::lightning_hazard_name(effect.hazard)
                   <<"\",\"knock_down_s\":"<<effect.knock_down_seconds<<",\"shake_m\":"<<effect.shake_meters<<"}\n";
      }
      if(first_person && !options.smoke_test)apply_vitals(terraflux::graphics::advance_vitals(vitals,gather_exposure(),delta,vitals_style));
      if(weather_state.enabled && std::abs(weather_state.flood_rise_meters-built_flood_rise)>0.15)rebuild_water(weather_state.flood_rise_meters);
      else if(!weather_state.enabled && built_flood_rise>0.0)rebuild_water(0.0);
      if(std::abs(flood.flash()-built_flash)>0.05 || (flood.flash()==0 && built_flash!=0))rebuild_flood();
      // Fire weather: hot dry air lifts the flames, rain beats them down (visual only; the
      // replay decided which cells burn under the same conditions through the v3 contract).
      fire_style.vigour=weather_state.enabled?std::clamp(1.0+2.5*weather_state.fire_weather,0.25,1.6):1.0;
      TerrainResidencyChange terrain_residency_change;
      if(options.terrain_residency) {
        terrain_residency_change=draw_surface?tiled_terrain->synchronize_visible(terrain_frustum,!options.no_culling):
            tiled_terrain->set_resident_tiles({});
        if(terrain_residency_change.evicted)terrain_saw_eviction=true;
        if(rendered_frames>0 && terrain_residency_change.admitted)terrain_saw_readmission=true;
        if(options.residency_report && (terrain_residency_change.admitted || terrain_residency_change.evicted))
          report_residency();
      }
      if(record_gpu)device.begin_gpu_timing();
      DrawIndexed command{pipeline,vb,ib,0,static_cast<std::uint32_t>(upload.indices.size()),
                          {{0,camera},{kTerrainStyleSlot,style_buffer},{kAtmosphereSlot,atmosphere.binding().block}}, {},
                          {{kOrographicFieldSlot,atmosphere.binding().field},{kRunoffFieldSlot,atmosphere.binding().runoff}}};
      if(fire_buffer)command.storage_buffers.push_back({1,*fire_buffer});
      if(layer_buffer)command.uniform_buffers.push_back({10,*layer_buffer});
      std::vector<std::uint8_t> terrain_reference;
      if(tiled_terrain && options.smoke_test) {
        RenderPassScope reference_pass(device,{scene_target});
        if(draw_surface){device.draw_indexed(command);++frame_draws;}
        terrain_reference=device.capture_rgba(scene_width,scene_height);
      }
      RenderPassScope terrain_pass(device,{scene_target});
      TerrainTileDrawStats terrain_stats;
      if(tiled_terrain) {
        if(draw_surface)terrain_stats=tiled_terrain->draw(pipeline,camera,atmosphere.binding(),fire_buffer,terrain_frustum,!options.no_culling,layer_buffer,style_buffer);
        else terrain_stats.total=tiled_terrain->tile_count();
        frame_draws+=static_cast<std::uint32_t>(terrain_stats.draws);
        if(options.smoke_test && device.capture_rgba(scene_width,scene_height)!=terrain_reference)
          throw std::runtime_error("tiled terrain differs from monolithic reference pixels");
        tile_test_culled+=terrain_stats.culled;
        if(options.smoke_test || ((options.profile || options.residency_report) && profile_frames%120==0) ||
           (options.residency_report && (terrain_residency_change.admitted || terrain_residency_change.evicted)))
          std::cout<<"TERRAIN_JSON={\"tiles\":"<<terrain_stats.total<<",\"visible\":"<<terrain_stats.visible
                   <<",\"culled\":"<<terrain_stats.culled<<",\"draws\":"<<terrain_stats.draws
                   <<",\"triangles\":"<<terrain_stats.triangles
                   <<",\"surface_visible\":"<<(draw_surface?"true":"false")
                   <<",\"layer_skipped\":"<<(draw_surface?0:terrain_stats.total)
                   <<",\"geometry_bytes\":"<<tiled_terrain->geometry_bytes()
                   <<",\"resident_tiles\":"<<tiled_terrain->resident_tile_count()
                   <<",\"resident_geometry_bytes\":"<<tiled_terrain->resident_bytes()
                   <<",\"resident_tile_limit\":"<<tiled_terrain->residency_limit()
                   <<",\"admitted\":"<<terrain_residency_change.admitted
                   <<",\"evicted\":"<<terrain_residency_change.evicted
                   <<",\"retention_frames\":"<<tiled_terrain->retention_frames()
                   <<",\"retained_hidden\":"<<terrain_residency_change.retained
                   <<",\"pressure_trimmed\":"<<terrain_residency_change.trimmed<<"}\n";
      } else if(draw_surface){device.draw_indexed(command);++frame_draws;}
      if(options.terrain_shading_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto sample=[&](int px,int py,int channel) {
          return static_cast<int>(pixels[(static_cast<std::size_t>(py)*scene_width+px)*4+channel]);
        };
        const auto base_style=[&] {
          auto style=terrain_style;style.contour_interval_meters=0;style.detail_strength=0;return style;
        }();
        if(rendered_frames==0) {
          // Compare the GPU image with the CPU reference color at every interior,
          // unburned, on-screen vertex. Occluded vertices legitimately differ, so a
          // majority must match overall and within every material class that has
          // enough candidates; the matched set must also span the height ramp,
          // show a slope-dependent hillshade range and include rock where it exists.
          constexpr int kTolerance=12;
          const double range=base_style.max_elevation_meters-base_style.min_elevation_meters;
          const auto light=base_style.light_direction.normalized();
          auto soil_only=base_style; // Same style without any rock blend, to find where rock is visible.
          soil_only.rock_slope_start_degrees=89.0;soil_only.rock_slope_full_degrees=90.0;
          std::size_t candidates=0,matched=0,rock_candidates=0,rock_matched=0;
          double min_height=1,max_height=0,min_diffuse=1,max_diffuse=0;
          constexpr std::size_t kClasses=kTerrainMaterialClassCount;
          std::array<std::size_t,kClasses> class_candidates{},class_matched{};
          for(const auto& vertex:mesh.vertices) {
            // Render vertices lie inside or on a simulation quad; require the
            // surrounding 4x4 block of measured cells so no edge or hole is near.
            const std::int64_t sim_x=mesh.origin_grid_x+(static_cast<std::int64_t>(vertex.grid_x)-mesh.origin_grid_x)/mesh.refinement;
            const std::int64_t sim_y=mesh.origin_grid_y+(static_cast<std::int64_t>(vertex.grid_y)-mesh.origin_grid_y)/mesh.refinement;
            bool interior=true;
            for(int dy=-1;dy<=2 && interior;++dy)for(int dx=-1;dx<=2;++dx) {
              const auto neighbour=grid.find(static_cast<int>(sim_x+dx),static_cast<int>(sim_y+dy));
              if(neighbour==terraflux::compute::kAbsent || !grid.cells[static_cast<std::size_t>(neighbour)].elevation_meters) {
                interior=false;break;
              }
            }
            if(!interior)continue;
            if(fire_field) { // Fire footprints reach into neighbouring cells; stay clear of them.
              bool near_fire=false;
              for(int dy=-2;dy<=3 && !near_fire;++dy)for(int dx=-2;dx<=3;++dx) {
                const std::int64_t fx=sim_x+dx-fire_field->min_x;
                const std::int64_t fy=sim_y+dy-fire_field->min_y;
                if(fx<0 || fy<0 || fx>=fire_field->width || fy>=fire_field->height)continue;
                const auto state=fire_field->states[static_cast<std::size_t>(fy)*fire_field->width+static_cast<std::size_t>(fx)];
                if(state==1 || state==2){near_fire=true;break;}
              }
              if(near_fire)continue;
            }
            const auto clip=camera_vp.transform({vertex.position.x,vertex.position.y,vertex.position.z,1});
            if(!(clip.w>0))continue;
            const double nx=clip.x/clip.w,ny=clip.y/clip.w;
            if(nx<-0.98 || nx>0.98 || ny<-0.98 || ny>0.98)continue;
            const int px=std::clamp(static_cast<int>(std::floor((nx*0.5+0.5)*scene_width)),0,scene_width-1);
            const int py=std::clamp(static_cast<int>(std::floor((ny*0.5+0.5)*scene_height)),0,scene_height-1);
            const TerrainSurfacePoint point{vertex.position.y,vertex.normal,vertex.fuel_load,vertex.fuel_moisture,vertex.curvature};
            const auto expected=terrain_surface_color(point,base_style);
            const auto soil=terrain_surface_color(point,soil_only);
            const auto material_weights=terrain_material(point,base_style);
            const auto material=static_cast<std::size_t>(dominant_material(material_weights));
            // Per-class statistics use vertices firmly inside their class: on the
            // render lattice neighbouring vertices are only a few pixels apart, so a
            // pixel at a shoreline or floor-zone transition legitimately blends classes.
            const double floor_peak=std::max({material_weights.floor_sand,material_weights.floor_vegetation,
                                              material_weights.floor_mud,material_weights.floor_stones});
            const bool firm=material_weights.submerged<0.02 || (material_weights.submerged>0.98 && floor_peak>=0.75);
            const double height=(vertex.position.y-base_style.min_elevation_meters)/range;
            const double diffuse=std::max(vertex.normal.dot(light),0.0);
            const double rock_shift=std::max({std::abs(expected.x-soil.x),std::abs(expected.y-soil.y),std::abs(expected.z-soil.z)})*255.0;
            const bool rock_visible=rock_shift>2*kTolerance; // Rock changes this vertex beyond tolerance.
            ++candidates;if(firm)++class_candidates.at(material);if(rock_visible)++rock_candidates;
            const double channels[3]={expected.x,expected.y,expected.z};
            bool ok=true;
            for(int c=0;c<3;++c) {
              const int want=static_cast<int>(std::lround(std::clamp(channels[c],0.0,1.0)*255.0));
              if(std::abs(want-sample(px,py,c))>kTolerance)ok=false;
            }
            if(!ok)continue;
            ++matched;if(firm)++class_matched[material];if(rock_visible)++rock_matched;
            min_height=std::min(min_height,height);max_height=std::max(max_height,height);
            min_diffuse=std::min(min_diffuse,diffuse);max_diffuse=std::max(max_diffuse,diffuse);
          }
          std::cout<<"TERRAIN_SHADING_JSON={\"candidates\":"<<candidates<<",\"matched\":"<<matched
                   <<",\"height_span\":["<<min_height<<","<<max_height<<"]"
                   <<",\"diffuse_span\":["<<min_diffuse<<","<<max_diffuse<<"]"
                   <<",\"rock_candidates\":"<<rock_candidates<<",\"rock_matched\":"<<rock_matched<<"}\n";
          std::cout<<"TERRAIN_MATERIAL_JSON={";
          std::size_t present_classes=0;
          for(std::size_t i=0;i<kClasses;++i) {
            const auto name=terrain_material_name(static_cast<TerrainMaterialClass>(i));
            std::cout<<(i?",":"")<<'"'<<name<<"\":["<<class_candidates[i]<<","<<class_matched[i]<<"]";
            if(class_candidates[i]>=20)++present_classes;
          }
          std::cout<<"}\n";
          if(candidates<100)throw std::runtime_error("terrain shading test has too few comparable vertices");
          if(matched*10<candidates*6)throw std::runtime_error("terrain pixels do not match the CPU shading reference");
          if(max_height-min_height<0.5)throw std::runtime_error("terrain shading did not span the height ramp");
          if(max_diffuse-min_diffuse<0.05)throw std::runtime_error("terrain shading shows no slope-dependent hillshade");
          if(rock_matched*10<rock_candidates*6)throw std::runtime_error("steep terrain did not render as rock");
          for(std::size_t i=0;i<kClasses;++i)
            if(class_candidates[i]>=20 && class_matched[i]*10<class_candidates[i]*6)
              throw std::runtime_error(std::string("material did not match the CPU reference: ")+
                                       terrain_material_name(static_cast<TerrainMaterialClass>(i)));
          // The demo terrain is authored to show every material family.
          if(!playback && present_classes<7)throw std::runtime_error("demo terrain does not exhibit the expected material variety");
          shading_reference=pixels;
        } else {
          std::size_t terrain_pixels=0,changed=0,brightened=0,silhouette=0;
          double absolute_difference=0;
          const auto clear=RenderPassDesc{}.clear_color; // Sky pixels keep the pass clear color.
          std::array<std::uint8_t,3> sky{};
          for(std::size_t c=0;c<3;++c)sky[c]=static_cast<std::uint8_t>(std::lround(clear[c]*255.0));
          for(std::size_t i=0;i<pixels.size();i+=4) {
            if(shading_reference[i]==sky[0] && shading_reference[i+1]==sky[1] && shading_reference[i+2]==sky[2]) {
              if(pixels[i]!=sky[0] || pixels[i+1]!=sky[1] || pixels[i+2]!=sky[2])++silhouette;
              continue;
            }
            ++terrain_pixels;
            bool differs=false,lighter=false;
            for(std::size_t c=0;c<3;++c) {
              const int delta=int(pixels[i+c])-int(shading_reference[i+c]);
              lighter=lighter || delta>1;
              absolute_difference+=std::abs(delta);
              differs=differs || delta!=0;
            }
            if(differs)++changed;
            if(lighter)++brightened;
          }
          const double mean_difference=terrain_pixels?absolute_difference/(3.0*terrain_pixels):0.0;
          // Only rock relief (frame 3) may move the silhouette, and only a little.
          if(silhouette && (rendered_frames!=3 || silhouette*33>terrain_pixels))
            throw std::runtime_error("terrain style changed sky pixels");
          if(rendered_frames==1) {
            std::cout<<"TERRAIN_CONTOUR_JSON={\"terrain_pixels\":"<<terrain_pixels<<",\"changed\":"<<changed<<"}\n";
            // Contours darken light ground and lift only very dark ground (dense forest).
            if(brightened*2>changed)throw std::runtime_error("contour lines brightened most terrain pixels");
            if(!terrain_pixels || changed*200<terrain_pixels || changed*10>terrain_pixels*4)
              throw std::runtime_error("contour lines changed an implausible share of terrain pixels");
          } else if(rendered_frames==2) {
            if(pixels!=shading_reference)throw std::runtime_error("restoring the terrain style changed reference pixels");
          } else {
            std::cout<<"TERRAIN_DETAIL_JSON={\"terrain_pixels\":"<<terrain_pixels<<",\"changed\":"<<changed
                     <<",\"mean_abs_difference\":"<<mean_difference<<",\"silhouette_pixels\":"<<silhouette<<"}\n";
            // Procedural detail must texture most of the surface while staying a
            // modulation of the base materials rather than a different picture.
            if(!terrain_pixels || changed*10<terrain_pixels*5)throw std::runtime_error("procedural detail left the terrain untextured");
            if(mean_difference<0.6 || mean_difference>40.0)throw std::runtime_error("procedural detail deviates implausibly from the base materials");
          }
        }
      }
      if(options.layers_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        if(rendered_frames==0) {
          layers_both=pixels;layers_field_snapshot=fire_field->states;layers_frame_snapshot=playback_frame;
          layers_playing_snapshot=replay_clock->playing();
          if(!device.count_fire_pixels(scene_width,scene_height))throw std::runtime_error("layer test has no initial visible fire");
        } else if(rendered_frames==1) {
          layers_ground=pixels;
          if(pixels==layers_both || device.count_fire_pixels(scene_width,scene_height))
            throw std::runtime_error("hidden fire did not restore clean terrain shading");
        } else if(rendered_frames==2) {
          layers_fire=pixels;
          if(pixels==layers_both || !device.count_fire_pixels(scene_width,scene_height))
            throw std::runtime_error("fire-only layer did not retain visible fire");
        } else if(rendered_frames==3) {
          bool removed_ground=false,has_fire=false,has_ground=false;
          for(std::size_t i=0;i<pixels.size();i+=4) {
            bool fire_is_sky=true,ground_is_sky=true;
            for(std::size_t c=0;c<4;++c) {
              if(pixels[i+c]!=pixels[c])throw std::runtime_error("hidden layers wrote surface pixels");
              fire_is_sky=fire_is_sky && layers_fire[i+c]==pixels[i+c];
              ground_is_sky=ground_is_sky && layers_ground[i+c]==pixels[i+c];
            }
            removed_ground=removed_ground || (fire_is_sky && !ground_is_sky);
            has_fire=has_fire || !fire_is_sky;has_ground=has_ground || !ground_is_sky;
          }
          if(!removed_ground || !has_fire || !has_ground || terrain_stats.draws)
            throw std::runtime_error("layer isolation failed sky/ground/fire assertions");
        } else if(rendered_frames==4 && pixels!=layers_both)
          throw std::runtime_error("restoring layer visibility changed reference pixels");
        if(fire_field->states!=layers_field_snapshot || playback_frame!=layers_frame_snapshot ||
           std::abs(engine_time-(rendered_frames+1)/60.0)>1e-9 || replay_clock->playing()!=layers_playing_snapshot ||
           std::abs(replay_clock->pending_seconds()-(layers_playing_snapshot?engine_time*replay_clock->speed():0.0))>1e-9)
          throw std::runtime_error("layer visibility changed replay data or engine clock");
      }
      if(options.replay_controls_test) {
        auto expected=terraflux::replay::make_fire_field(playback->grid);
        terraflux::replay::apply_frame(playback->frames[playback_frame],expected);
        if(expected.states!=fire_field->states)throw std::runtime_error("replay controls selected incorrect fire data");
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        if(rendered_frames==0)replay_initial_pixels=pixels;
        if(rendered_frames==2)replay_paused_pixels=pixels;
        if((rendered_frames==4 && pixels!=replay_paused_pixels) ||
           (rendered_frames==6 && pixels!=replay_initial_pixels))
          throw std::runtime_error("replay backward/reset pixels did not restore selected frame");
      }
      // The sky dome fills what no terrain covered (far depth), before the
      // transparent water so reflections and waves blend over it.
      if(sky && weather_state.enabled){sky->draw(atmosphere.binding());++frame_draws;}
      if(options.weather_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto clear=RenderPassDesc{}.clear_color;
        std::array<std::uint8_t,3> clear_rgb{};
        for(std::size_t c=0;c<3;++c)clear_rgb[c]=static_cast<std::uint8_t>(std::lround(clear[c]*255.0));
        const std::size_t count=pixels.size()/4;
        const auto luminance=[&](std::size_t i){return (0.299*pixels[i*4]+0.587*pixels[i*4+1]+0.114*pixels[i*4+2])/255.0;};
        // Mean brightness of the ground (non-sky) and the sky regions.
        double ground_sum=0,sky_sum=0,sky_r=0,sky_b=0;std::size_t ground_n=0,sky_n=0,sky_changed=0,bright_sky=0,grey_sky=0;
        for(std::size_t i=0;i<count;++i) {
          const bool is_sky=!weather_sky_mask.empty() && weather_sky_mask[i];
          if(is_sky) {
            ++sky_n;sky_sum+=luminance(i);sky_r+=pixels[i*4];sky_b+=pixels[i*4+2];
            if(pixels[i*4]!=clear_rgb[0] || pixels[i*4+1]!=clear_rgb[1] || pixels[i*4+2]!=clear_rgb[2])++sky_changed;
            if(luminance(i)>0.4)++bright_sky;
            const int hi=std::max({pixels[i*4],pixels[i*4+1],pixels[i*4+2]}),lo=std::min({pixels[i*4],pixels[i*4+1],pixels[i*4+2]});
            if(hi-lo<28)++grey_sky;
          } else {++ground_n;ground_sum+=luminance(i);}
        }
        const double ground_mean=ground_n?ground_sum/ground_n:0,sky_mean=sky_n?sky_sum/sky_n:0;
        // CPU twin of the lighting and haze at every visible interior vertex (clear sky frames).
        std::size_t candidates=0,matched=0;
        if(rendered_frames>=1 && rendered_frames<=3) {
          auto base_style=terrain_style;base_style.contour_interval_meters=0;base_style.detail_strength=0;
          constexpr int kTolerance=12;
          for(const auto& vertex:mesh.vertices) {
            const std::int64_t sim_x=mesh.origin_grid_x+(static_cast<std::int64_t>(vertex.grid_x)-mesh.origin_grid_x)/mesh.refinement;
            const std::int64_t sim_y=mesh.origin_grid_y+(static_cast<std::int64_t>(vertex.grid_y)-mesh.origin_grid_y)/mesh.refinement;
            bool interior=true;
            for(int dy=-1;dy<=2 && interior;++dy)for(int dx=-1;dx<=2;++dx) {
              const auto neighbour=grid.find(static_cast<int>(sim_x+dx),static_cast<int>(sim_y+dy));
              if(neighbour==terraflux::compute::kAbsent || !grid.cells[static_cast<std::size_t>(neighbour)].elevation_meters){interior=false;break;}
            }
            if(!interior)continue;
            const auto clip=camera_vp.transform({vertex.position.x,vertex.position.y,vertex.position.z,1});
            if(!(clip.w>0))continue;
            const double nx=clip.x/clip.w,ny=clip.y/clip.w;
            if(nx<-0.98 || nx>0.98 || ny<-0.98 || ny>0.98)continue;
            const int px=std::clamp(static_cast<int>(std::floor((nx*0.5+0.5)*scene_width)),0,scene_width-1);
            const int py=std::clamp(static_cast<int>(std::floor((ny*0.5+0.5)*scene_height)),0,scene_height-1);
            const TerrainSurfacePoint point{vertex.position.y,vertex.normal,vertex.fuel_load,vertex.fuel_moisture,vertex.curvature};
            const auto material=terrain_material(point,base_style);
            const auto lit=atmosphere_shade(material.albedo,vertex.normal,weather_state);
            const auto expected=atmosphere_haze(lit,vertex.position,weather_state);
            const double channels[3]={expected.x,expected.y,expected.z};
            bool ok=true;
            for(int c=0;c<3;++c) {
              const int want=static_cast<int>(std::lround(std::clamp(channels[c],0.0,1.0)*255.0));
              if(std::abs(want-static_cast<int>(pixels[(static_cast<std::size_t>(py)*scene_width+px)*4+c]))>kTolerance)ok=false;
            }
            ++candidates;if(ok)++matched;
          }
        }
        const auto* field=weather.field();
        std::cout<<"WEATHER_TEST_JSON={\"frame\":"<<rendered_frames<<",\"enabled\":"<<(weather_state.enabled?"true":"false")
                 <<",\"phase\":\""<<daylight_phase_name(weather_state.sun.phase)<<"\",\"sun_altitude_deg\":"<<weather_state.sun.altitude_degrees
                 <<",\"ground_mean\":"<<ground_mean<<",\"sky_mean\":"<<sky_mean<<",\"sky_pixels\":"<<sky_n<<",\"sky_changed\":"<<sky_changed
                 <<",\"bright_sky\":"<<bright_sky<<",\"grey_sky\":"<<grey_sky<<",\"sky_red_over_blue\":"<<(sky_b>0?sky_r/sky_b:0)
                 <<",\"candidates\":"<<candidates<<",\"matched\":"<<matched
                 <<",\"wind_mps\":"<<weather_state.wind.speed_mps<<",\"precipitation\":"<<weather_state.precipitation
                 <<",\"in_cloud_texels\":"<<(field?field->in_cloud_texels:0)<<",\"windward_texels\":"<<(field?field->windward_texels:0)<<"}\n";
        const auto wc=weather.effective_wind();
        if(weather_state.enabled && (weather_state.wind.speed_mps<wc.speed_min_mps-1e-9 || weather_state.wind.speed_mps>wc.speed_max_mps+1e-9))
          throw std::runtime_error("wind left its thresholds");
        if(rendered_frames==7 && wc.speed_max_mps<=weather_config.wind.speed_max_mps+6.0)
          throw std::runtime_error("the thunderstorm did not widen the wind thresholds");
        if(rendered_frames==0) {
          if(weather_state.enabled)throw std::runtime_error("weather test frame 0 must be neutral");
          weather_reference=pixels;
          weather_sky_mask.assign(count,false);
          std::size_t sky_count=0;
          for(std::size_t i=0;i<count;++i) {
            weather_sky_mask[i]=pixels[i*4]==clear_rgb[0] && pixels[i*4+1]==clear_rgb[1] && pixels[i*4+2]==clear_rgb[2];
            if(weather_sky_mask[i])++sky_count;
          }
          if(sky_count*10<count*2)throw std::runtime_error("weather test needs the sky in at least a fifth of the frame");
        } else if(rendered_frames==1) {
          if(sky_changed*100<sky_n*99)throw std::runtime_error("the sky dome did not cover the clear color");
          if(bright_sky*10<sky_n*6 || sky_r>=sky_b)throw std::runtime_error("the noon sky is not bright and blue");
          if(candidates<100 || matched*10<candidates*6)throw std::runtime_error("daylight terrain pixels do not match the CPU lighting twin");
          weather_day_brightness=ground_mean;
        } else if(rendered_frames==2) {
          if(weather_state.sun.phase!=DaylightPhase::Night)throw std::runtime_error("midnight is not night");
          if(ground_mean>weather_day_brightness*0.3)throw std::runtime_error("the ground is not dark at night");
          if(sky_mean>0.12)throw std::runtime_error("the night sky is not dark");
          std::size_t stars=0;
          for(std::size_t i=0;i<count;++i)if(weather_sky_mask[i] && luminance(i)>0.3)++stars;
          if(stars<10)throw std::runtime_error("no stars at night");
          if(candidates<100 || matched*10<candidates*6)throw std::runtime_error("moonlit terrain pixels do not match the CPU lighting twin");
          if(weather_state.sun.moon_intensity<0.08 || weather_state.sun.light_direction.dot(weather_state.sun.moon_direction)<0.999)
            throw std::runtime_error("the full moon is not the night's light");
          weather_night_brightness=ground_mean;
        } else if(rendered_frames==3) {
          if(weather_state.sun.phase!=DaylightPhase::CivilTwilight)throw std::runtime_error("the twilight frame is not civil twilight");
          if(ground_mean<=weather_night_brightness*1.25 || ground_mean>=weather_day_brightness*0.8)
            throw std::runtime_error("civil twilight is not between night and day");
          // The glow above the horizon is warm: sample the sky just above the silhouette.
          double r=0,b=0;std::size_t n=0;
          for(int x=0;x<scene_width;++x) {
            int horizon=-1;
            for(int y=0;y<scene_height && horizon<0;++y)if(weather_sky_mask[static_cast<std::size_t>(y)*scene_width+x])horizon=y;
            if(horizon<0)continue;
            for(int y=horizon;y<std::min(scene_height,horizon+24);++y) {
              const std::size_t i=static_cast<std::size_t>(y)*scene_width+x;
              if(!weather_sky_mask[i])continue;
              r+=pixels[i*4];b+=pixels[i*4+2];++n;
            }
          }
          if(!n || r<=b*1.15)throw std::runtime_error("the twilight horizon does not glow warm");
          if(candidates<100 || matched*10<candidates*6)throw std::runtime_error("twilight terrain pixels do not match the CPU lighting twin");
        } else if(rendered_frames==4) {
          if(grey_sky*10<sky_n*8)throw std::runtime_error("the stratus sky is not overcast grey");
          if(ground_mean>=weather_day_brightness*0.95)throw std::runtime_error("the overcast ground is not darker than the clear noon");
          if(!field || field->in_cloud_texels==0 || field->windward_texels==0)
            throw std::runtime_error("stratus did not reach the summit or thicken on the windward slope");
          weather_before_rain=pixels;
        } else if(rendered_frames==5) {
          weather_cumulus=pixels;
        } else if(rendered_frames==6) {
          std::size_t moved=0;
          for(std::size_t i=0;i<count;++i)
            if(weather_sky_mask[i] && (pixels[i*4]!=weather_cumulus[i*4] || pixels[i*4+1]!=weather_cumulus[i*4+1] || pixels[i*4+2]!=weather_cumulus[i*4+2]))++moved;
          if(moved*20<sky_n)throw std::runtime_error("the clouds did not drift with the wind");
          if(std::abs(weather_state.wind.speed_mps-weather_cumulus_wind)<1e-6)throw std::runtime_error("the wind did not gust between the frames");
        } else if(rendered_frames==7) {
          if(weather_state.cloud.storm<0.99 || weather_state.lightning<0.5)throw std::runtime_error("the thunderstorm frame has no flash");
          if(weather_state.precipitation<=0.3)throw std::runtime_error("the thunderstorm frame is not raining");
        } else if(rendered_frames==8) {
          std::cout<<"WEATHER_FLOOD_JSON={\"rainfall_mm\":"<<weather_state.rainfall_mm<<",\"ponding\":"<<weather_state.ponding
                   <<",\"flood_rise_m\":"<<weather_state.flood_rise_meters<<",\"water_level_m\":"<<(water_style?water_style->level_meters:0.0)
                   <<",\"base_level_m\":"<<base_water_level<<"}\n";
          if(weather_state.rainfall_mm<8.0 || weather_state.ponding<0.3 || weather_state.flood_rise_meters<0.15)
            throw std::runtime_error("four hours of rain left no puddles or flood rise");
          if(!water_style || water_style->level_meters<base_water_level+0.15)throw std::runtime_error("the flood did not raise the water");
          if(weather_state.runoff<0.5 || weather_state.snow || weather_state.freezing_level_meters<weather_state.camera.eye.y)
            throw std::runtime_error("warm rain did not run off below a freezing level above the scene");
          if(runoff_field.channel_texels==0 || runoff_field.waterfall_texels==0)throw std::runtime_error("the demo terrain has no channels or waterfalls");
        } else if(rendered_frames==9) {
          std::cout<<"WEATHER_SNOW_JSON={\"snow\":"<<(weather_state.snow?"true":"false")<<",\"freezing_level_m\":"<<weather_state.freezing_level_meters
                   <<",\"snow_cover\":"<<weather_state.snow_cover<<",\"runoff\":"<<weather_state.runoff<<",\"surface_temperature_c\":"<<weather_state.temperature_celsius<<"}\n";
          if(!weather_state.snow || weather_state.freezing_level_meters>scene_camera.target.y+1.0 || weather_state.snow_cover<0.3 || weather_state.runoff>0.0)
            throw std::runtime_error("freezing rain did not turn to snow with a cover on the ground and no runoff");
        } else if(pixels!=weather_reference)throw std::runtime_error("disabling the weather did not restore the reference");
        if(rendered_frames==5)weather_cumulus_wind=weather_state.wind.speed_mps;
      }
      // Roads and bridges lie on the ground and stand over the water: drawn with
      // the terrain, before the water, so a deck over the lake occludes it.
      road_draws=0;
      if(roads && roads->version()!=road_obstacles_version)rebuild_road_obstacles();
      if(roads && road_renderer && layer_visibility.terrain && roads_visible) {
        if(roads->version()!=roads_uploaded_version){upload_roads();report_roads("rebuilt");}
        road_draws=road_renderer->draw(camera);
        frame_draws+=static_cast<std::uint32_t>(road_draws);
      }
      building_draws=0;
      if(buildings && buildings->version()!=building_obstacles_version)rebuild_building_obstacles();
      if(buildings && buildings->door_version()!=door_obstacles_version)rebuild_door_obstacles();
      if(buildings && building_renderer && layer_visibility.terrain && buildings_visible) {
        if(buildings->version()!=buildings_uploaded_version)upload_buildings();
        if(buildings->door_version()!=doors_uploaded_version)upload_doors();
        building_draws=building_renderer->draw(camera)+door_renderer->draw(camera);
        frame_draws+=static_cast<std::uint32_t>(building_draws);
      }
      // Water belongs to the surface layer: it follows the terrain in the same
      // pass so asset diagnostics still compare against the complete ground.
      std::vector<std::uint8_t> water_before;
      if(water && layer_visibility.terrain) {
        // The wind raises the sea: storms bring high, fast, choppy waves; a calm flattens them.
        auto staged=weather_state.enabled?weather.water_for(*water_style):*water_style;
        // Smoke diagnostics compare frames, so their water does not animate.
        double water_time=options.smoke_test?0.0:engine_time;
        if(options.water_test) {
          // Frames 0 and 3: flat water at t=0 (CPU reference parity). Frame 1:
          // configured waves at t=0. Frame 2: the same waves 0.7 s later.
          if(rendered_frames==0 || rendered_frames==3)staged.wave_height_meters=0;
          water_time=rendered_frames==2?0.7:0.0;
          water_before=device.capture_rgba(scene_width,scene_height);
        }
        const auto packed=pack_water_style(staged,camera_frame.eye,water_time);
        device.update_buffer(water->style,0,std::as_bytes(std::span(packed)));
        device.draw_indexed({water->pipeline,water->vb,water->ib,0,water->indices,
                             {{0,camera},{kWaterStyleSlot,water->style},{kAtmosphereSlot,atmosphere.binding().block}},{},
                             {{kOrographicFieldSlot,atmosphere.binding().field}}});
        ++frame_draws;
      }
      if(flood_gpu && flood_gpu->indices && layer_visibility.terrain) {
        auto staged=flood_water_style;
        if(weather_state.enabled)staged.wave_direction_degrees=weather_state.wind.direction_degrees;
        const auto packed=pack_water_style(staged,camera_frame.eye,options.smoke_test?0.0:engine_time);
        device.update_buffer(flood_gpu->style,0,std::as_bytes(std::span(packed)));
        device.draw_indexed({flood_gpu->pipeline,flood_gpu->vb,flood_gpu->ib,0,flood_gpu->indices,
                             {{0,camera},{kWaterStyleSlot,flood_gpu->style},{kAtmosphereSlot,atmosphere.binding().block}},{},
                             {{kOrographicFieldSlot,atmosphere.binding().field}}});
        ++frame_draws;
      }
      if(options.water_test) {
        if(!water)throw std::runtime_error("water test has no water surface");
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        if(rendered_frames==0) {
          // Flat water: every on-screen surface vertex deeper than 0.3 m must blend
          // the CPU reference color over the ground pixel captured before the draw.
          constexpr int kTolerance=12;
          auto flat=*water_style;flat.wave_height_meters=0;
          std::size_t candidates=0,matched=0;
          double min_depth=1e9,max_depth=0;
          for(const auto& v:water_upload.vertices) {
            const double depth=flat.level_meters-v.bed_height;
            if(depth<0.3)continue;
            const auto clip=camera_vp.transform({v.position[0],v.position[1],v.position[2],1});
            if(!(clip.w>0))continue;
            const double nx=clip.x/clip.w,ny=clip.y/clip.w;
            if(nx<-0.98 || nx>0.98 || ny<-0.98 || ny>0.98)continue;
            const int px=std::clamp(static_cast<int>(std::floor((nx*0.5+0.5)*scene_width)),0,scene_width-1);
            const int py=std::clamp(static_cast<int>(std::floor((ny*0.5+0.5)*scene_height)),0,scene_height-1);
            const auto sample=water_surface_sample({v.position[0],v.position[1],v.position[2]},depth,camera_frame.eye,flat);
            const std::size_t offset=(static_cast<std::size_t>(py)*scene_width+px)*4;
            const double water_rgb[3]={sample.color.x,sample.color.y,sample.color.z};
            bool ok=true;
            for(int c=0;c<3;++c) {
              const double blended=std::clamp(water_rgb[c],0.0,1.0)*sample.alpha+(water_before[offset+c]/255.0)*(1-sample.alpha);
              if(std::abs(static_cast<int>(std::lround(blended*255.0))-static_cast<int>(pixels[offset+c]))>kTolerance)ok=false;
            }
            ++candidates;if(ok)++matched;
            min_depth=std::min(min_depth,depth);max_depth=std::max(max_depth,depth);
          }
          std::size_t water_pixels=0;
          for(std::size_t i=0;i<pixels.size();i+=4)
            if(pixels[i]!=water_before[i] || pixels[i+1]!=water_before[i+1] || pixels[i+2]!=water_before[i+2])++water_pixels;
          std::cout<<"WATER_TEST_JSON={\"candidates\":"<<candidates<<",\"matched\":"<<matched
                   <<",\"depth_span\":["<<min_depth<<","<<max_depth<<"],\"water_pixels\":"<<water_pixels<<"}\n";
          if(candidates<50)throw std::runtime_error("water test has too few visible surface vertices");
          if(matched*10<candidates*6)throw std::runtime_error("flat water pixels do not match the CPU reference");
          if(max_depth-min_depth<1.0)throw std::runtime_error("water test needs a range of depths");
          if(water_pixels<500)throw std::runtime_error("water surface covered too few pixels");
          water_reference=pixels;
          // Flat water is nearly invisible in the last centimeters of depth, so the
          // coverage mask is dilated by three pixels to include the shoreline fringe
          // where foam legitimately appears.
          std::vector<bool> covered(pixels.size()/4,false);
          for(std::size_t i=0;i<pixels.size();i+=4)
            covered[i/4]=pixels[i]!=water_before[i] || pixels[i+1]!=water_before[i+1] || pixels[i+2]!=water_before[i+2];
          water_mask.assign(covered.size(),false);
          for(int y=0;y<scene_height;++y)for(int x=0;x<scene_width;++x) {
            bool near_water=false;
            for(int dy=-3;dy<=3 && !near_water;++dy)for(int dx=-3;dx<=3;++dx) {
              const int sx=x+dx,sy=y+dy;
              if(sx<0 || sy<0 || sx>=scene_width || sy>=scene_height)continue;
              if(covered[static_cast<std::size_t>(sy)*scene_width+sx]){near_water=true;break;}
            }
            water_mask[static_cast<std::size_t>(y)*scene_width+x]=near_water;
          }
        } else {
          std::size_t water_pixels=0,changed=0,land_changed=0,bright=0;
          for(std::size_t i=0;i<pixels.size();i+=4) {
            const auto& previous=rendered_frames==2?water_waves:water_reference;
            const bool differs=pixels[i]!=previous[i] || pixels[i+1]!=previous[i+1] || pixels[i+2]!=previous[i+2];
            if(!water_mask[i/4]){if(differs)++land_changed;continue;}
            ++water_pixels;if(differs)++changed;
            if(pixels[i]>200 && pixels[i+1]>200 && pixels[i+2]>200)++bright; // foam
          }
          std::cout<<"WATER_WAVES_JSON={\"frame\":"<<rendered_frames<<",\"water_pixels\":"<<water_pixels
                   <<",\"changed\":"<<changed<<",\"land_changed\":"<<land_changed<<",\"foam_pixels\":"<<bright<<"}\n";
          // Shallow puddles that flat water left invisible may still gain foam.
          if(land_changed*100>water_pixels)throw std::runtime_error("waves changed pixels outside the water surface");
          if(rendered_frames==1) {
            if(changed*10<water_pixels*3)throw std::runtime_error("waves left the water surface unchanged");
            if(!bright)throw std::runtime_error("waves produced no foam along the shore");
            water_waves=pixels;
          } else if(rendered_frames==2) {
            if(changed*10<water_pixels*3)throw std::runtime_error("water did not animate between frames");
          } else if(pixels!=water_reference)
            throw std::runtime_error("flat water did not restore reference pixels");
        }
      }
      const auto terrain_snapshot=(options.smoke_test &&
          (options.asset_lifecycle_test || (imported && imported->uses_pbr)))?
          device.capture_rgba(scene_width,scene_height):std::vector<std::uint8_t>{};
      if(options.asset_lifecycle_test && !playback) {
        if(lifecycle_terrain.empty())lifecycle_terrain=terrain_snapshot;
        else if(lifecycle_terrain!=terrain_snapshot)
          throw std::runtime_error("terrain framebuffer changed during asset lifecycle");
      }
      terrain_pass.end();
      // Preserve terrain color and depth for opaque/transparent object draws.
      RenderPassScope object_pass(device,{scene_target,AttachmentLoadOp::Load,AttachmentLoadOp::Load});
      if(options.smoke_test && !terrain_snapshot.empty() &&
         device.capture_rgba(scene_width,scene_height)!=terrain_snapshot)
        throw std::runtime_error("load pass changed preserved terrain color");
      // Vegetation: opaque instanced plants over the terrain, before imported objects.
      VegetationDrawStats vegetation_stats;
      bool draw_vegetation=vegetation && layer_visibility.terrain;
      if(options.vegetation_test) {
        // Frames: 0 off, 1 on, 2 other tree meshes, 3 same meshes 1.5 s later, 4 off.
        draw_vegetation=vegetation && rendered_frames>=1 && rendered_frames<=3;
        if(rendered_frames==2 && vegetation) {
          tree_style=tree_style=="conifer"?"broadleaf":"conifer";
          vegetation->set_tree_meshes(tree_meshes_for(tree_style));
        }
      }
      if(draw_vegetation) {
        const double vegetation_time=options.vegetation_test?(rendered_frames==3?1.5:0.0):(options.smoke_test?0.0:engine_time);
        if(tree_fire && tree_fire->size()==vegetation->placement().trees.size()){
          tree_fire->apply_to_placement(vegetation->placement_mutable());
          // Fallen trunks join the walking obstacles; during a big fire trees fall
          // every frame, so the field is rebuilt at most twice a second.
          if(tree_fire->obstacle_version()!=tree_fire_obstacles_seen && rendered_frames%30==0)rebuild_tree_obstacles();
        }
        const auto packed=pack_vegetation_style(vegetation_style,camera_frame.eye,vegetation_time,vegetation_fire,tree_fire!=nullptr);
        device.update_buffer(*vegetation_style_buffer,0,std::as_bytes(std::span(packed)));
        vegetation_stats=vegetation->draw(*vegetation_pipeline,camera,*vegetation_style_buffer,vegetation_fire_buffer,atmosphere.binding(),
                                          terrain_frustum,camera_frame.eye,vegetation_style,!options.no_culling);
        frame_draws+=static_cast<std::uint32_t>(vegetation_stats.draws);
        if(options.smoke_test || (options.profile && profile_frames%120==0))
          std::cout<<"VEGETATION_DRAW_JSON={\"chunks_visible\":"<<vegetation_stats.chunks_visible<<",\"chunks\":"<<vegetation_stats.chunks_total
                   <<",\"trees\":"<<vegetation_stats.tree_instances<<",\"grass\":"<<vegetation_stats.grass_instances
                   <<",\"draws\":"<<vegetation_stats.draws<<",\"triangles\":"<<vegetation_stats.triangles<<"}\n";
      }
      if(options.vegetation_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto clear=RenderPassDesc{}.clear_color;
        std::array<std::uint8_t,3> sky{};
        for(std::size_t c=0;c<3;++c)sky[c]=static_cast<std::uint8_t>(std::lround(clear[c]*255.0));
        const auto differs=[&](const std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b,std::size_t i) {
          return a[i]!=b[i] || a[i+1]!=b[i+1] || a[i+2]!=b[i+2];
        };
        if(rendered_frames==0) {
          if(vegetation_stats.draws)throw std::runtime_error("vegetation drew in its off frame");
          vegetation_reference=pixels;
        } else if(rendered_frames==1 || rendered_frames==2 || rendered_frames==3) {
          std::size_t changed=0,above_horizon=0;
          std::set<std::uint32_t> colors;
          for(std::size_t i=0;i<pixels.size();i+=4) {
            if(!differs(pixels,vegetation_reference,i))continue;
            ++changed;
            if(vegetation_reference[i]==sky[0] && vegetation_reference[i+1]==sky[1] && vegetation_reference[i+2]==sky[2])++above_horizon;
            colors.insert((pixels[i]/16u)<<8 | (pixels[i+1]/16u)<<4 | (pixels[i+2]/16u));
          }
          std::cout<<"VEGETATION_TEST_JSON={\"frame\":"<<rendered_frames<<",\"changed\":"<<changed<<",\"above_horizon\":"<<above_horizon
                   <<",\"color_buckets\":"<<colors.size()<<",\"trees_drawn\":"<<vegetation_stats.tree_instances
                   <<",\"grass_drawn\":"<<vegetation_stats.grass_instances<<"}\n";
          if(!vegetation_stats.tree_instances || !vegetation_stats.grass_instances || !vegetation_stats.draws)
            throw std::runtime_error("vegetation test drew no trees or no grass");
          if(changed*50<pixels.size()/4)throw std::runtime_error("vegetation changed too few pixels");
          if(!above_horizon)throw std::runtime_error("trees have no height: nothing rises above the terrain silhouette");
          if(colors.size()<24)throw std::runtime_error("vegetation shows too little color variation");
          if(rendered_frames==1)vegetation_frame=pixels;
          else {
            std::size_t moved=0;
            for(std::size_t i=0;i<pixels.size();i+=4)if(differs(pixels,vegetation_frame,i))++moved;
            if(moved*10<changed)throw std::runtime_error(rendered_frames==2?"switching tree meshes changed nothing":"wind sway changed nothing");
            vegetation_frame=pixels;
          }
        } else if(pixels!=vegetation_reference)throw std::runtime_error("disabling vegetation did not restore the reference");
      }
      OpaqueRenderQueue draw_queue;
      TransparentRenderQueue transparent_queue;
      std::size_t visible_instances=0,culled_instances=0;
      const terraflux::math::Vec3 camera_eye=camera_frame.eye;
      if(imported && imported->uses_pbr) {
        // Keep view vector in material UBO, not the terrain camera UBO,
        // whose 80-byte fire-grid layout is shared by existing shaders.
        const auto& eye=camera_eye;
        const auto upload_pbr=[&](const std::vector<ImportedGpu::Section>& sections) {
          for(const auto& s:sections) {
            PbrMaterialUniform pbr{};
            pbr.factors={s.metallic,s.roughness,s.normal_scale,s.occlusion_strength};
            pbr.emissive={static_cast<float>(s.emissive_factor.x),
                          static_cast<float>(s.emissive_factor.y),
                          static_cast<float>(s.emissive_factor.z),s.alpha_cutoff};
            pbr.options={s.alpha_factor,
                         s.alpha_blend?2.0f:(s.alpha_mask?1.0f:0.0f),0,0};
            pbr.eye={static_cast<float>(eye.x),static_cast<float>(eye.y),
                     static_cast<float>(eye.z),1};
            device.update_buffer(s.pbr_uniform,0,std::as_bytes(std::span(&pbr,1)));
          }
        };
        upload_pbr(imported->sections);
        for(const auto& mesh:imported->additional_meshes)
          upload_pbr(mesh.sections);
      }
      asset_obstacles.clear();
      if (imported) {
        const auto& frustum=terrain_frustum;
        // Asset queue ordering is independent of opaque terrain tile submission.
        const auto enqueue=[&](BufferHandle vertex,BufferHandle index,
                               const std::vector<ImportedGpu::Section>& sections,
                               BufferHandle object_uniform,const terraflux::math::Mat4& model,
                               BoundingSphere bounds,terraflux::math::Vec3 tint) {
          asset_obstacles.push_back(terraflux::graphics::asset_obstacle(world_bounds(bounds,model)));
          if(!options.no_culling &&
             !frustum.intersects(world_bounds(bounds,model))) {
            ++culled_instances;
            return;
          }
          ++visible_instances;
          for(const auto& section:sections) {
            const auto& color=section.base_color;
            const auto material=make_object_uniform(model,{
                tint.x*color.x,tint.y*color.y,tint.z*color.z});
            std::vector<UniformBufferBinding> uniforms{{0,camera},{2,object_uniform}};
            if(imported->uses_pbr)uniforms.push_back({5,section.pbr_uniform});
            std::vector<TextureBinding> textures{{3,section.albedo}};
            if(imported->uses_pbr){
              textures.push_back({6,section.metallic_roughness});
              textures.push_back({7,section.normal});
              textures.push_back({8,section.occlusion});
              textures.push_back({9,section.emissive});
            }
            PreparedDraw draw{{section.alpha_blend?imported->transparent_pipeline:
                                imported->pipeline,vertex,index,
                section.first,section.count,
                std::move(uniforms),{},std::move(textures)},
                object_uniform,material};
            if(section.alpha_blend) {
              const auto center=world_bounds(bounds,model).center;
              const auto offset=center-camera_eye;
              transparent_queue.add({std::move(draw),offset.dot(offset)});
            }else draw_queue.add(std::move(draw));
          }
        };
        if(imported->is_gltf) {
          for(const auto& instance:imported->gltf_instances) {
            auto vertex=imported->vb,index=imported->ib;
            const auto* sections=&imported->sections;
            auto bounds=imported->primary_bounds;
            if(instance.mesh_index!=0) {
              const auto& mesh=imported->additional_meshes.at(instance.mesh_index-1);
              vertex=mesh.vb;index=mesh.ib;
              sections=&mesh.sections;
              bounds=imported->additional_bounds.at(instance.mesh_index-1);
            }
            enqueue(vertex,index,*sections,instance.uniform,instance.model,
                    bounds,{1,1,1});
          }
        }else if(imported->uses_scene_file) {
          for(const auto& instance:imported->scene_instances) {
            const auto model=imported->scene_anchor*
                imported->scene.world_matrix(instance.node)*instance.asset_local;
            auto vertex=imported->vb,index=imported->ib;
            const auto* sections=&imported->sections;
            auto bounds=imported->primary_bounds;
            if(instance.mesh_index>0) {
              const auto& mesh=imported->additional_meshes.at(instance.mesh_index-1);
              vertex=mesh.vb;index=mesh.ib;sections=&mesh.sections;
              bounds=imported->additional_bounds.at(instance.mesh_index-1);
            }
            enqueue(vertex,index,*sections,instance.uniform,
                    model,bounds,imported->uses_pbr?terraflux::math::Vec3{1,1,1}:
                        terraflux::math::Vec3{0.16,0.57,0.95});
          }
        }else {
          using terraflux::scene::Transform;
          const double spread=imported->separation;
          imported->scene.set_transform(imported->left_pivot,
              Transform{{-spread,0,0},{0,scene_playback.simulated_seconds()*0.7,0},{1,1,1}});
          imported->scene.set_transform(imported->right_pivot,
              Transform{{spread,0.10*spread*std::sin(scene_playback.simulated_seconds()*1.4),0},
                        {0,-scene_playback.simulated_seconds()*0.43,0},{1,1,1}});
          enqueue(imported->vb,imported->ib,imported->sections,
                  imported->object_uniform_left,
                  imported->scene.world_matrix(imported->left_mesh),
                  imported->primary_bounds,{0.16,0.57,0.95});
          enqueue(imported->vb,imported->ib,imported->sections,
                  imported->object_uniform_right,
                  imported->scene.world_matrix(imported->right_mesh),
                  imported->primary_bounds,{0.79,0.43,0.92});
        }
      }
      // The walker meets the assets where they are drawn this frame (unloaded assets vanish).
      if(!options.no_collision && (asset_obstacles.size()!=asset_field.size() || !asset_obstacles.empty()))
        asset_field=terraflux::graphics::ObstacleField(asset_obstacles);
      const auto queue_stats=(imported && !options.no_instancing)?
          draw_queue.submit_instanced_streamed(device,imported->instanced_pipeline,
                                               *imported->instance_ring,256):
          draw_queue.submit(device);
      frame_draws+=static_cast<std::uint32_t>(queue_stats.submitted);
      // Transparent draws must follow terrain and all opaque geometry.
      // Per-object depth sorting does not solve intersecting triangles
      // within one mesh, but is correct for distinct primitive/instance layers.
      const auto transparent_draws=transparent_queue.submit(device);
      frame_draws+=static_cast<std::uint32_t>(transparent_draws);
      // Flames and smoke last: transparent, depth-tested against everything above.
      FireDrawStats fire_stats;
      bool draw_fire_effects=fire_effects && layer_visibility.fire;
      if(options.fire_test)draw_fire_effects=fire_effects && rendered_frames>=1 && rendered_frames<=2;
      if(draw_fire_effects) {
        if(fire_emitters_dirty)rebuild_fire_emitters();
        if(tree_fire) {
          FireEmitters dynamic;
          tree_fire->emit(dynamic.flames,dynamic.smoke,fire_style);
          if(roads)roads->emit(dynamic.flames,dynamic.smoke,fire_style);
          if(buildings)buildings->emit(dynamic.flames,dynamic.smoke,fire_style);
          if(first_person && held.kind==terraflux::graphics::HeldKind::Torch) {
            FireInstance fl{};
            fl.position[0]=static_cast<float>(held_world_position.x);fl.position[1]=static_cast<float>(held_world_position.y);fl.position[2]=static_cast<float>(held_world_position.z);
            fl.size=static_cast<float>(0.45*fire_style.flame_scale);fl.phase=0.3f;fl.intensity=0.9f;fl.kind=0.0f;fl.seed=7.0f;
            dynamic.flames.push_back(fl);
          }
          for(const auto& p:projectiles)if(p.item.kind==terraflux::graphics::HeldKind::Torch) {
            FireInstance fl{};
            fl.position[0]=static_cast<float>(p.position.x);fl.position[1]=static_cast<float>(p.position.y);fl.position[2]=static_cast<float>(p.position.z);
            fl.size=0.5f;fl.phase=0.1f;fl.intensity=0.9f;fl.kind=3.0f;fl.seed=3.0f;
            dynamic.flames.push_back(fl);
          }
          fire_effects->set_dynamic(std::move(dynamic));
        }
        const double fire_time=options.fire_test?(rendered_frames==2?0.6:0.0):(options.smoke_test?0.0:engine_time);
        const auto packed=pack_fire_style(fire_style,camera_frame.eye,fire_time);
        device.update_buffer(*fire_style_buffer,0,std::as_bytes(std::span(packed)));
        fire_stats=fire_effects->draw(*fire_pipeline,camera,*fire_style_buffer,atmosphere.binding(),terrain_frustum,camera_frame.eye,
                                      fire_style.draw_distance_meters,!options.no_culling);
        frame_draws+=static_cast<std::uint32_t>(fire_stats.draws);
        if(options.smoke_test || (options.profile && profile_frames%120==0))
          std::cout<<"FIRE_DRAW_JSON={\"flames\":"<<fire_stats.flames<<",\"smoke\":"<<fire_stats.smoke<<",\"draws\":"<<fire_stats.draws<<"}\n";
      }
      // Lightning channels from the cloud base to the ground while a flash lasts.
      LightningDrawStats lightning_stats;
      std::vector<std::uint8_t> before_lightning;
      if(options.weather_test && rendered_frames==7)before_lightning=device.capture_rgba(scene_width,scene_height);
      if(lightning && weather_state.enabled && weather_state.strike.active) {
        const auto& strike=weather_state.strike;
        const terraflux::math::Vec3 top{weather_state.strike_position.x,weather_state.cloud.base_meters,weather_state.strike_position.z};
        const auto ground=ground_sampler.sample(top.x,top.z);
        const double ground_y=ground?ground->position.y:mesh.min_elevation_meters;
        lightning_stats=lightning->draw(camera,atmosphere.binding(),strike,top,ground_y,camera_frame.eye);
        frame_draws+=static_cast<std::uint32_t>(lightning_stats.draws);
      }
      if(options.weather_test && rendered_frames==7) {
        const auto after=device.capture_rgba(scene_width,scene_height);
        std::size_t bolt_pixels=0;
        for(std::size_t i=0;i<after.size();i+=4)
          if(after[i]!=before_lightning[i] || after[i+1]!=before_lightning[i+1] || after[i+2]!=before_lightning[i+2])++bolt_pixels;
        std::cout<<"WEATHER_STORM_JSON={\"strike\":"<<(weather_state.strike.active?"true":"false")<<",\"flash\":"<<weather_state.strike.intensity
                 <<",\"segments\":"<<lightning_stats.segments<<",\"bolt_pixels\":"<<bolt_pixels<<",\"rain_mm_h\":"<<weather_state.precipitation_mm_h
                 <<",\"surface_temperature_c\":"<<weather_state.temperature_celsius<<",\"fire_weather\":"<<weather_state.fire_weather
                 <<",\"wind_mps\":"<<weather_state.wind.speed_mps<<",\"wave_height_m\":"<<weather.water_for(*water_style).wave_height_meters<<"}\n";
        if(!weather_state.strike.active || weather_state.strike.intensity<0.5 || !lightning_stats.segments || bolt_pixels<40)
          throw std::runtime_error("the thunderstorm drew no lightning channel");
        if(weather_state.precipitation_mm_h<5.0 || weather_state.fire_weather>-0.1)
          throw std::runtime_error("the thunderstorm brings no heavy rain that cools the fire weather");
        if(weather_state.temperature_celsius>=weather_config.temperature_celsius-3.0)
          throw std::runtime_error("the downpour did not cool the surface air");
        if(weather.water_for(*water_style).wave_height_meters<=water_style->wave_height_meters)
          throw std::runtime_error("the storm wind did not raise the waves");
      }
      // Waterfall spray: drizzle off the lips, splash off the ledges and plumes from the
      // plunge pools while rain water runs; lit and bowed drop by drop.
      SprayDrawStats spray_stats;
      std::vector<std::uint8_t> before_spray;
      const bool spray_report=options.smoke_test && (options.weather_test?rendered_frames==8:rendered_frames==0) && weather_state.enabled && weather_state.runoff>0.0;
      if(spray_report)before_spray=device.capture_rgba(scene_width,scene_height);
      if(spray && weather_state.enabled && weather_state.runoff>0.0) {
        spray_stats=spray->draw(camera,atmosphere.binding(),weather_state.runoff);
        frame_draws+=static_cast<std::uint32_t>(spray_stats.draws);
      }
      if(spray_report) {
        const auto after=device.capture_rgba(scene_width,scene_height);
        std::size_t spray_pixels=0,lit_pixels=0,tinted_pixels=0;
        for(std::size_t i=0;i<after.size();i+=4) {
          const int dr=int(after[i])-before_spray[i],dg=int(after[i+1])-before_spray[i+1],db=int(after[i+2])-before_spray[i+2];
          if(dr || dg || db)++spray_pixels;
          if(dr+dg+db>30)++lit_pixels;
          // A bow fragment: a brightening with an unequal spread across the channels.
          if(dr+dg+db>12 && std::max({dr,dg,db})-std::min({dr,dg,db})>10)++tinted_pixels;
        }
        std::cout<<"WEATHER_SPRAY_JSON={\"emitters\":"<<spray_stats.emitters<<",\"particles\":"<<spray_stats.particles<<",\"draws\":"<<spray_stats.draws
                 <<",\"changed\":"<<spray_pixels<<",\"lit\":"<<lit_pixels<<",\"tinted\":"<<tinted_pixels<<",\"runoff\":"<<weather_state.runoff
                 <<",\"lips\":"<<runoff_field.lip_texels<<",\"plunges\":"<<runoff_field.plunge_texels<<",\"max_drop_m\":"<<runoff_field.max_drop_meters<<"}\n";
        if(options.weather_test && (!spray_stats.draws || !spray_stats.emitters || runoff_field.lip_texels==0 || runoff_field.plunge_texels==0 || spray_pixels<100))
          throw std::runtime_error("running water drew no visible waterfall spray");
      }
      if(options.weather_test && rendered_frames==9 && spray_stats.draws)throw std::runtime_error("frozen ground sprayed a waterfall");
      // Rain or snow last: streaks around the camera, blended over everything.
      PrecipitationDrawStats precipitation_stats;
      std::vector<std::uint8_t> dry_pixels;
      if(options.weather_test && (rendered_frames==4 || rendered_frames==9))dry_pixels=device.capture_rgba(scene_width,scene_height);
      if(precipitation && weather_state.enabled && weather_state.precipitation>0.0) {
        precipitation_stats=precipitation->draw(camera,atmosphere.binding(),weather_state.precipitation);
        frame_draws+=static_cast<std::uint32_t>(precipitation_stats.draws);
      }
      if(options.weather_test && (rendered_frames==4 || rendered_frames==9)) {
        const auto wet_pixels=device.capture_rgba(scene_width,scene_height);
        std::size_t streaks=0;
        for(std::size_t i=0;i<wet_pixels.size();i+=4)
          if(wet_pixels[i]!=dry_pixels[i] || wet_pixels[i+1]!=dry_pixels[i+1] || wet_pixels[i+2]!=dry_pixels[i+2])++streaks;
        std::cout<<"WEATHER_RAIN_JSON={\"particles\":"<<precipitation_stats.particles<<",\"draws\":"<<precipitation_stats.draws
                 <<",\"changed\":"<<streaks<<",\"snow\":"<<(weather_state.snow?"true":"false")<<"}\n";
        if(!precipitation_stats.draws || streaks<200)throw std::runtime_error(rendered_frames==4?"stratus drizzle drew no visible precipitation":"snow drew no visible flakes");
      }
      if(options.fire_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto is_fire=[&](const std::vector<std::uint8_t>& p,std::size_t i) {
          const int r=p[i],g=p[i+1],b=p[i+2];return r>=175 && r>g*1.7 && r>b*2.0;
        };
        if(rendered_frames==0) {
          fire_reference=pixels;
          // Per column: the highest row (GL rows grow upward) showing ground fire.
          fire_top_row.assign(static_cast<std::size_t>(scene_width),-1);
          for(int y=0;y<scene_height;++y)for(int x=0;x<scene_width;++x)
            if(is_fire(pixels,(static_cast<std::size_t>(y)*scene_width+x)*4))fire_top_row[static_cast<std::size_t>(x)]=y;
          if(std::all_of(fire_top_row.begin(),fire_top_row.end(),[](int v){return v<0;}))
            throw std::runtime_error("fire test has no visible ground fire to build on");
        } else if(rendered_frames<=2) {
          std::size_t changed=0,risen=0,fire_pixels=0;
          for(int y=0;y<scene_height;++y)for(int x=0;x<scene_width;++x) {
            const std::size_t i=(static_cast<std::size_t>(y)*scene_width+x)*4;
            if(is_fire(pixels,i))++fire_pixels;
            if(pixels[i]==fire_reference[i] && pixels[i+1]==fire_reference[i+1] && pixels[i+2]==fire_reference[i+2])continue;
            ++changed;
            const int top=fire_top_row[static_cast<std::size_t>(x)];
            if(top>=0 && y>top+3)++risen;
          }
          std::cout<<"FIRE_TEST_JSON={\"frame\":"<<rendered_frames<<",\"changed\":"<<changed<<",\"risen\":"<<risen
                   <<",\"fire_pixels\":"<<fire_pixels<<",\"flames_drawn\":"<<fire_stats.flames<<",\"smoke_drawn\":"<<fire_stats.smoke<<"}\n";
          if(!fire_stats.flames || !fire_stats.smoke)throw std::runtime_error("fire test drew no flames or no smoke");
          if(changed<200)throw std::runtime_error("fire effects changed too few pixels");
          if(risen<40)throw std::runtime_error("flames do not rise above the ground footprint");
          if(rendered_frames==1)fire_frame=pixels;
          else {
            std::size_t moved=0;
            for(std::size_t i=0;i<pixels.size();i+=4)
              if(pixels[i]!=fire_frame[i] || pixels[i+1]!=fire_frame[i+1] || pixels[i+2]!=fire_frame[i+2])++moved;
            if(moved*5<changed)throw std::runtime_error("fire effects did not animate between frames");
          }
        } else if(pixels!=fire_reference)throw std::runtime_error("hiding fire effects did not restore the reference");
      }
      if(options.climb_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        std::cout<<"CLIMB_TEST_JSON={\"frame\":"<<rendered_frames<<",\"facet_slope_deg\":"<<climb_probe.slope<<",\"climbed\":"<<(climb_probe.climbed?"true":"false")
                 <<",\"surface\":\""<<climb_probe.surface<<"\",\"rise_m\":"<<climb_probe.rise<<",\"stamina\":"<<climb_probe.stamina
                 <<",\"refusal\":\""<<climb_probe.refusal<<"\",\"feet_y\":"<<person.feet.y<<"}\n";
        if(rendered_frames==0) {
          if(!first_person || person.stamina!=1.0)throw std::runtime_error("the climb test did not start fresh in first person");
          climb_reference=pixels;
        } else if(rendered_frames==1) {
          if(!climb_probe.climbed || climb_probe.surface!="rock")throw std::runtime_error("the rock face was not climbed");
          if(climb_probe.rise<1.5)throw std::runtime_error("twenty seconds of climbing gained too little height");
          if(!(climb_probe.stamina<0.9))throw std::runtime_error("climbing spent no strength");
          if(pixels==climb_reference)throw std::runtime_error("the climb did not change the view");
        } else if(rendered_frames==2) {
          if(climb_probe.refusal!="waterfall")throw std::runtime_error("the running waterfall did not refuse the climb");
          if(climb_probe.rise>2.0)throw std::runtime_error("the walker climbed a running waterfall"); // the dry rock beside it may be climbed up to the water
        } else {
          if(pixels!=climb_reference)throw std::runtime_error("returning below the face did not restore the reference exactly");
        }
      }
      if(options.roads_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto st=roads->stats();
        const auto& pr=roads_probe;
        std::cout<<"ROADS_TEST_JSON={\"frame\":"<<rendered_frames<<",\"bridge\":"<<pr.bridge<<",\"road\":"<<pr.road<<",\"road_draws\":"<<road_draws
                 <<",\"on_road_m\":"<<pr.on_road_m<<",\"off_road_m\":"<<pr.off_road_m<<",\"on_platform\":"<<(pr.on_platform?"true":"false")
                 <<",\"deck_y\":"<<pr.deck_y<<",\"ground_under_deck\":"<<pr.ground_under_deck<<",\"offset_after_strafe_m\":"<<pr.offset_after_strafe
                 <<",\"tree\":"<<pr.tree<<",\"blockages\":"<<pr.blockages<<",\"damage\":"<<pr.damage<<",\"blocked_sample\":"<<(pr.blocked_sample?"true":"false")
                 <<",\"collapsed_under\":"<<(pr.collapsed_under?"true":"false")<<",\"fell\":"<<(pr.fell?"true":"false")<<",\"swimming\":"<<(pr.swimming?"true":"false")
                 <<",\"feet_after_collapse\":"<<pr.feet_after_collapse<<",\"health\":"<<vitals.health<<",\"cause\":\""<<terraflux::graphics::damage_cause_name(vitals.last_cause)
                 <<"\",\"destroyed\":"<<st.destroyed<<",\"blocked\":"<<st.blocked<<",\"feet\":["<<person.feet.x<<","<<person.feet.y<<","<<person.feet.z<<"]}\n";
        const auto changed_pixels=[&](const std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b) {
          std::size_t n=0;for(std::size_t i=0;i<a.size() && i<b.size();i+=4)if(a[i]!=b[i] || a[i+1]!=b[i+1] || a[i+2]!=b[i+2])++n;return n;
        };
        if(rendered_frames==0) {
          if(road_draws || st.bridges==0)throw std::runtime_error("the roads test did not start with the roads hidden over a bridge");
          roads_reference=pixels;
        } else if(rendered_frames==1) {
          if(!road_draws)throw std::runtime_error("the roads drew nothing");
          if(changed_pixels(pixels,roads_reference)*1000<pixels.size()/4)throw std::runtime_error("the roads and the bridge changed too few pixels");
        } else if(rendered_frames==2) {
          if(!pr.on_platform)throw std::runtime_error("the walker does not stand on the road surface");
          if(pr.on_road_m<pr.off_road_m*(road_style.road_speed_factor-0.08) || pr.on_road_m>pr.off_road_m*(road_style.road_speed_factor+0.08))
            throw std::runtime_error("walking on the road was not faster by the road factor");
        } else if(rendered_frames==3) {
          if(!pr.on_platform || pr.deck_y-pr.ground_under_deck<1.0)throw std::runtime_error("the deck did not carry the walker over what lies below");
          if(pr.fell || pr.offset_after_strafe>0.5*terraflux::graphics::road_width(road_style))throw std::runtime_error("the railing did not keep the walker on the deck");
        } else if(rendered_frames==4) {
          if(!pr.blockages || !st.blocked)throw std::runtime_error("the felled tree did not block the road");
          if(!(pr.damage>0) || !pr.blocked_sample)throw std::runtime_error("the burning trunk did not damage the road or take its bonus");
        } else if(rendered_frames==5) {
          if(!pr.collapsed_under || vitals.health>=pr.health_before || vitals.last_cause!=terraflux::graphics::DamageCause::Collapse)
            throw std::runtime_error("the deck giving way under the walker did not hurt it");
          if(!pr.fell || pr.feet_after_collapse>pr.deck_y-1.0)throw std::runtime_error("the walker did not fall with the collapsed deck");
          if(!st.destroyed)throw std::runtime_error("the bridge is not destroyed");
        } else {
          if(road_draws || st.destroyed || st.blocked)throw std::runtime_error("reset did not restore the network");
          if(pixels!=roads_reference)throw std::runtime_error("hiding the reset roads did not restore the reference exactly");
        }
      }
      if(options.city_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto cs=city->stats;
        const auto bs=buildings->stats();
        const auto rs=roads->stats();
        const auto& pr=city_probe;
        std::cout<<"CITY_TEST_JSON={\"frame\":"<<rendered_frames<<",\"zones\":"<<cs.zones<<",\"suburbs\":"<<cs.suburbs<<",\"interchanges\":"<<cs.interchanges
                 <<",\"skyscrapers\":"<<bs.skyscrapers<<",\"police\":"<<bs.police_stations<<",\"highway_m\":"<<rs.highway_meters<<",\"ramps\":"<<rs.ramps<<",\"runway_m\":"<<rs.runway_meters
                 <<",\"curve_joints\":"<<rs.curve_joints<<",\"unconnected\":"<<pr.unconnected<<",\"zone\":\""<<pr.zone<<"\",\"lanes\":"<<pr.lanes<<",\"towers_near\":"<<pr.towers_near
                 <<",\"on_deck\":"<<(pr.on_deck?"true":"false")<<",\"deck_over_ground_m\":"<<pr.deck_over_ground<<",\"avenue_below\":"<<(pr.avenue_below?"true":"false")
                 <<",\"on_runway\":"<<(pr.on_runway?"true":"false")<<",\"runway_wide\":"<<(pr.runway_wide?"true":"false")<<",\"road_draws\":"<<road_draws<<",\"building_draws\":"<<building_draws
                 <<",\"feet\":["<<person.feet.x<<","<<person.feet.y<<","<<person.feet.z<<"]}\n";
        if(rendered_frames==0) {
          if(cs.cbds<1 || cs.suburbs<5 || cs.airports<1 || cs.interchanges<1)throw std::runtime_error("the city lacks a CBD, five suburbs, an airport or an interchange");
          if(bs.skyscrapers<8 || bs.police_stations<2 || bs.hospitals<1 || bs.terminals<1 || bs.towers<1 || bs.hangars<1)throw std::runtime_error("the city lacks its towers, police stations, hospital or airport buildings");
          if(rs.highway_meters<1000 || rs.ramps<4 || rs.runway_meters<300 || rs.curve_joints<20)throw std::runtime_error("the city lacks highways with ramps, a runway or curved streets");
          if(pr.unconnected)throw std::runtime_error("a building's footpath does not meet a road");
          if(!road_draws || !building_draws)throw std::runtime_error("the city drew nothing");
          city_reference=pixels;
        } else if(rendered_frames==1) {
          if(pr.lanes!=4 || pr.towers_near<3)throw std::runtime_error("the walker does not stand on a four-lane CBD avenue among the towers");
          if(pr.zone.empty())throw std::runtime_error("the walker's zone is unknown");
        } else if(rendered_frames==2) {
          if(!pr.on_deck || pr.deck_over_ground<city_plan->interchange_lift_meters-0.6 || !pr.avenue_below)throw std::runtime_error("the interchange's viaduct does not carry the highway over the avenue");
        } else if(rendered_frames==3) {
          if(!pr.on_runway || !pr.runway_wide)throw std::runtime_error("the runway is not a 45 m pavement");
        } else {
          if(pixels!=city_reference) {
            std::size_t changed=0;for(std::size_t i=0;i<pixels.size() && i<city_reference.size();i+=4)if(pixels[i]!=city_reference[i] || pixels[i+1]!=city_reference[i+1] || pixels[i+2]!=city_reference[i+2])++changed;
            throw std::runtime_error("returning to the world view did not restore the reference exactly ("+std::to_string(changed)+" pixels differ)");
          }
        }
      }
      if(options.buildings_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto st=buildings->stats();
        const auto& pr=buildings_probe;
        std::cout<<"BUILDINGS_TEST_JSON={\"frame\":"<<rendered_frames<<",\"building_draws\":"<<building_draws<<",\"buildings\":"<<st.buildings<<",\"tallest_m\":"<<st.tallest_meters
                 <<",\"most_storeys\":"<<st.most_storeys<<",\"target\":"<<pr.target<<",\"on_path\":"<<(pr.on_path?"true":"false")<<",\"path_speed\":"<<pr.path_speed
                 <<",\"door_distance_m\":"<<pr.door_distance<<",\"inside\":"<<(pr.inside?"true":"false")<<",\"stopped\":"<<(pr.stopped?"true":"false")
                 <<",\"path_meets_road\":"<<(pr.path_meets_road?"true":"false")<<",\"at\":\""<<pr.at<<"\",\"feet\":["<<person.feet.x<<","<<person.feet.y<<","<<person.feet.z<<"]}\n";
        const auto changed_pixels=[&](const std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b) {
          std::size_t n=0;for(std::size_t i=0;i<a.size() && i<b.size();i+=4)if(a[i]!=b[i] || a[i+1]!=b[i+1] || a[i+2]!=b[i+2])++n;return n;
        };
        if(rendered_frames==0) {
          if(building_draws || st.buildings<4 || st.most_storeys<20 || st.tallest_meters<70)throw std::runtime_error("the buildings test did not start with the four buildings hidden");
          buildings_reference=pixels;
        } else if(rendered_frames==1) {
          if(!building_draws)throw std::runtime_error("the buildings drew nothing");
          if(changed_pixels(pixels,buildings_reference)*200<pixels.size()/4)throw std::runtime_error("the buildings changed too few pixels");
        } else if(rendered_frames==2) {
          if(!pr.on_path || pr.path_speed<road_style.road_speed_factor-1e-6)throw std::runtime_error("the footpath did not carry and hurry the walker");
          if(!pr.stopped || pr.inside)throw std::runtime_error("the hospital's front wall did not stop the walker at its door");
          if(!pr.path_meets_road || pr.at!="hospital")throw std::runtime_error("the footpath does not join the road at the hospital's entrance");
        } else {
          if(building_draws)throw std::runtime_error("the buildings were not hidden again");
          if(pixels!=buildings_reference)throw std::runtime_error("hiding the buildings did not restore the reference exactly");
        }
      }
      if(options.hazards_test) {
        const auto& pr=hazards_probe;
        const auto st=buildings->stats();
        std::cout<<"HAZARDS_TEST_JSON={\"frame\":"<<rendered_frames<<",\"building_draws\":"<<building_draws<<",\"hint\":\""<<pr.hint<<"\",\"did\":\""<<pr.did<<"\",\"alight\":"<<(pr.alight?"true":"false")
                 <<",\"heat\":"<<pr.heat<<",\"flames\":"<<pr.flames<<",\"trees_lit\":"<<pr.trees_lit<<",\"roof_fell_on_me\":"<<(pr.roof_fell_on_me?"true":"false")<<",\"health_lost\":"<<pr.health_lost
                 <<",\"destroyed\":"<<(pr.destroyed?"true":"false")<<",\"destroyed_by\":\""<<pr.destroyed_by<<"\",\"on_rubble\":"<<(pr.on_rubble?"true":"false")<<",\"feet_y\":"<<pr.feet_y<<",\"floor_m\":"<<pr.floor
                 <<",\"house_roof_fell\":"<<(pr.house_roof_fell?"true":"false")<<",\"house_roof_cause\":\""<<pr.house_roof_cause<<"\",\"windows_burst\":"<<(pr.windows_burst?"true":"false")<<",\"walls_damage\":"<<pr.walls_damage
                 <<",\"house_stands\":"<<(pr.house_stands?"true":"false")<<",\"struck\":"<<(pr.struck?"true":"false")<<",\"washed_out\":"<<(pr.washed_out?"true":"false")<<",\"sound\":"<<(pr.sound?"true":"false")
                 <<",\"stats\":{\"alight\":"<<st.alight<<",\"damaged\":"<<st.damaged<<",\"roofless\":"<<st.roofless<<",\"destroyed\":"<<st.destroyed<<",\"struck\":"<<st.struck<<",\"logs\":"<<st.logs<<"}"
                 <<",\"fire_flames\":"<<fire_stats.flames<<",\"feet\":["<<person.feet.x<<","<<person.feet.y<<","<<person.feet.z<<"]}\n";
        if(!building_draws)throw std::runtime_error("the buildings drew nothing");
        if(rendered_frames==0) {
          if(pr.hint!="E: set the bungalow's walls alight | Q: throw" || pr.did!="set the bungalow's walls alight")throw std::runtime_error("the torch did not offer to light the bungalow's walls");
          if(!pr.alight || st.alight<1)throw std::runtime_error("the fire did not spread over the bungalow's walls, thatch and door");
          if(pr.heat<0.9 || pr.flames<10 || fire_stats.flames<10)throw std::runtime_error("the blaze's heat did not reach the walker or its flames were not drawn");
          if(buildings->version()==pr.version)throw std::runtime_error("the char did not change the drawing");
        } else if(rendered_frames==1) {
          if(!pr.roof_fell_on_me || pr.health_lost<=0)throw std::runtime_error("the roof falling in did not hurt the walker inside");
          if(!pr.destroyed || pr.destroyed_by!="fire")throw std::runtime_error("the bungalow did not burn down");
          if(!pr.on_rubble)throw std::runtime_error("the rubble is not a slow surface to walk over");
        } else if(rendered_frames==2) {
          if(!pr.house_roof_fell || pr.house_roof_cause!="impact")throw std::runtime_error("the trunk did not smash the house's tiled roof");
          if(pr.hint!="E: put the torch down | Q: throw")throw std::runtime_error("the torch should find nothing to light on a stone wall");
          if(!pr.windows_burst)throw std::runtime_error("the burning door did not burst the house's windows");
          if(pr.walls_damage<=0 || pr.walls_damage>0.2 || !pr.house_stands)throw std::runtime_error("the stone house should stand, cracked a little, its walls unlit");
        } else if(rendered_frames==3) {
          if(!pr.struck)throw std::runtime_error("the strike did not scorch the skyscraper");
          if(!pr.washed_out)throw std::runtime_error("the torrent did not wash the hospital out");
        } else {
          if(!pr.sound)throw std::runtime_error("reset did not restore the buildings");
        }
      }
      if(options.interior_test) {
        const auto& pr=interior_probe;
        const auto& b=buildings->buildings()[pr.target];
        std::cout<<"INTERIOR_TEST_JSON={\"frame\":"<<rendered_frames<<",\"building_draws\":"<<building_draws<<",\"target\":"<<pr.target<<",\"floor_m\":"<<pr.floor<<",\"feet_y\":"<<pr.feet_y
                 <<",\"door_distance_m\":"<<pr.door_distance<<",\"at_door\":"<<(pr.at_door?"true":"false")<<",\"inside\":"<<(pr.inside?"true":"false")<<",\"on_floor\":"<<(pr.on_floor?"true":"false")
                 <<",\"leaf_open\":"<<pr.leaf_open<<",\"table_gap_m\":"<<pr.table_gap<<",\"kept_in\":"<<(pr.kept_in?"true":"false")<<",\"out\":"<<(pr.out?"true":"false")
                 <<",\"hint\":\""<<pr.hint<<"\",\"did\":\""<<pr.did<<"\",\"door_obstacles\":"<<door_field.size()<<",\"feet\":["<<person.feet.x<<","<<person.feet.y<<","<<person.feet.z<<"]}\n";
        if(!building_draws)throw std::runtime_error("the bungalow drew nothing");
        if(rendered_frames==0) {
          if(std::abs(pr.feet_y-pr.floor)>0.15 || pr.door_distance>1.0 || pr.inside)throw std::runtime_error("the closed door did not stop the walker at the threshold at floor level");
          if(!pr.at_door || pr.hint!="E: open the door")throw std::runtime_error("the door within reach did not offer to be opened");
        } else if(rendered_frames==1) {
          if(pr.did!="opened the door" || pr.leaf_open<1.0)throw std::runtime_error("E did not open the door");
          if(!pr.inside || !pr.on_floor)throw std::runtime_error("the walker did not get in onto the floor");
        } else if(rendered_frames==2) {
          if(!pr.inside || pr.table_gap<1.0 || pr.table_gap>1.5)throw std::runtime_error("the table and chairs did not stop the walker");
        } else if(rendered_frames==3) {
          if(!pr.at_door || pr.hint!="E: close the door" || pr.did!="closed the door" || pr.leaf_open>0.0)throw std::runtime_error("the door was not closed from inside");
          if(!pr.kept_in)throw std::runtime_error("the closed door did not keep the walker in");
        } else {
          if(pr.did!="opened the door" || !pr.out || pr.feet_y>b.floor_meters-0.02 || pr.door_distance<2.0)throw std::runtime_error("the walker did not leave down the ramp once the door was opened again");
        }
      }
      if(options.flood_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        flood_probe.triangles=flood_triangles;
        std::cout<<"FLOOD_TEST_JSON={\"frame\":"<<rendered_frames<<",\"point\":["<<flood_probe.x<<","<<flood_probe.z<<"],\"flash\":"<<flood.flash()
                 <<",\"depth_m\":"<<flood_probe.depth<<",\"current_mps\":"<<flood_probe.current<<",\"swept\":"<<(flood_probe.swept?"true":"false")
                 <<",\"swimming\":"<<(flood_probe.swimming?"true":"false")<<",\"carried_m\":"<<flood_probe.carried<<",\"triangles\":"<<flood_triangles
                 <<",\"feet_y\":"<<person.feet.y<<",\"eye_y\":"<<camera_frame.eye.y<<"}\n";
        if(rendered_frames==0) {
          if(flood.flash()!=0 || flood_triangles || person.water_depth_meters!=0)throw std::runtime_error("the flood test did not start dry");
          flood_reference=pixels;
        } else if(rendered_frames==1) {
          if(flood.flash()<0.5)throw std::runtime_error("half an hour of storm rain raised no flood wave");
          if(!flood_triangles || !flood_gpu || !flood_gpu->indices)throw std::runtime_error("the flood wave drew no surface");
          if(flood_probe.depth<0.2 || flood_probe.current<=0)throw std::runtime_error("the channel point is not under running water");
          if(flood_probe.current>=walk_env.water_rules.sweep_current_mps && flood_probe.depth>=walk_env.water_rules.sweep_depth_meters && !flood_probe.swept)
            throw std::runtime_error("a fast, deep current did not sweep the walker");
          if(pixels==flood_reference)throw std::runtime_error("the flood did not change the image");
        } else if(rendered_frames==2) {
          // Swept (deep and fast): carried downstream. Merely wading: held back well short of a dry walk.
          if(flood_probe.swept && flood_probe.carried<=0.5)throw std::runtime_error("walking upstream in the flood, the walker was not carried downstream");
          if(!flood_probe.swept && flood_probe.carried<=-person.walk_speed_mps*3.0*0.8)throw std::runtime_error("wading upstream was as fast as a dry walk");
        } else {
          if(flood_triangles || person.water_depth_meters!=0)throw std::runtime_error("the wave did not drain");
          if(pixels!=flood_reference)throw std::runtime_error("draining the flood did not restore the reference exactly");
        }
      }
      if(options.storm_test) {
        const auto st=tree_fire->stats();
        storm_probe.stunned=person.stunned_seconds;storm_probe.shake=person.shake_meters;
        storm_probe.distance=std::hypot(person.feet.x-storm_probe.strike_x,person.feet.z-storm_probe.strike_z);
        std::cout<<"STORM_TEST_JSON={\"frame\":"<<rendered_frames<<",\"strike\":["<<storm_probe.strike_x<<","<<storm_probe.strike_z
                 <<"],\"distance_m\":"<<storm_probe.distance<<",\"stunned_s\":"<<storm_probe.stunned<<",\"shake_m\":"<<storm_probe.shake
                 <<",\"struck\":"<<st.struck<<",\"ignited_by_lightning\":"<<st.ignited_by_lightning<<",\"burning\":"<<st.burning
                 <<",\"falling\":"<<st.falling<<",\"fallen\":"<<st.fallen<<",\"windthrown\":"<<storm_probe.windthrown
                 <<",\"walked_m\":"<<storm_probe.walked<<",\"drift_m\":"<<storm_probe.drift<<",\"wind_mps\":"<<weather.wind().speed_mps<<"}\n";
        if(rendered_frames==0) {
          if(!weather_state.enabled || weather_state.cloud.storm<=0)throw std::runtime_error("the storm test has no thunderstorm");
          if(!first_person || person.stunned_seconds>0)throw std::runtime_error("the storm test did not start standing in first person");
        } else if(rendered_frames==1) {
          if(!weather_state.strike.active)throw std::runtime_error("the chosen strike is not active in its frame");
          if(st.struck!=1 || !st.ignited_by_lightning)throw std::runtime_error("the strike split and lit no tree");
          if(!(st.falling+st.fallen>=1) || !(st.burning+st.falling>=1))throw std::runtime_error("the struck tree is not coming down alight");
          if(person.stunned_seconds<=0 || person.shake_meters<=0 || storm_probe.distance>15.5)throw std::runtime_error("a strike ten metres away did not strike the walker down");
          if(person.eye().y>person.feet.y+person.eye_height_meters-0.5)throw std::runtime_error("the struck walker is not down"); // rendered from the next frame on
          if(lightning_message.empty())throw std::runtime_error("no lightning warning was raised");
        } else if(rendered_frames==2) {
          if(person.stunned_seconds!=0 || storm_probe.walked<0.5 || storm_probe.walked>6.0)throw std::runtime_error("the walker did not get up after the stun and walk on");
          if(st.fallen+st.burned_out<1 || fire_effects->dynamic().flames.empty())throw std::runtime_error("the split tree did not come down burning");
        } else {
          if(!storm_probe.windthrown)throw std::runtime_error("a 36 m/s gust front uprooted no tree");
          if(storm_probe.windthrown*2>st.trees)throw std::runtime_error("a 36 m/s gust front flattened the whole wood");
          if(storm_probe.drift<2.0)throw std::runtime_error("the gust front did not drag the standing walker");
        }
      }
      if(options.tree_fire_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto st=tree_fire->stats();
        const auto& dyn=fire_effects->dynamic();
        std::size_t embers=0,low_logs=0;
        for(const auto& fl:dyn.flames)if(fl.kind>2.5f)++embers;
        for(const auto& o:tree_field.obstacles())if(o.top-o.bottom<2.5)++low_logs;
        std::cout<<"TREE_FIRE_TEST_JSON={\"frame\":"<<rendered_frames<<",\"dynamic_flames\":"<<dyn.flames.size()<<",\"dynamic_smoke\":"<<dyn.smoke.size()
                 <<",\"embers\":"<<embers<<",\"low_logs\":"<<low_logs<<",\"obstacle_version\":"<<tree_fire->obstacle_version()<<"}\n";
        const std::size_t by_trees=st.ignited_by_contact+st.ignited_by_radiation+st.ignited_by_ember+st.ignited_by_collapse;
        if(rendered_frames==0) {
          if(!st.ignited || st.burning || st.seconds!=0)throw std::runtime_error("the replay frame did not ignite trees at time zero");
          if(st.heaviest_kg<=st.lightest_kg*1.5 || st.strongest_nm<=st.weakest_nm*1.5)throw std::runtime_error("trees do not vary in weight and strength");
          tree_fire_reference=pixels;
        } else if(rendered_frames==1) {
          if(!st.burning || dyn.flames.empty() || dyn.smoke.empty())throw std::runtime_error("ignited trees did not flame up");
          if(!st.branches_dropped || !embers)throw std::runtime_error("burning crowns dropped no burning branches");
          if(pixels==tree_fire_reference)throw std::runtime_error("the burning trees did not change the image");
          tree_fire_frame=pixels;
        } else if(rendered_frames==2) {
          if(!st.grounded && !st.leaning)throw std::runtime_error("no burned-through tree fell");
          if(!st.burned_out)throw std::runtime_error("no tree burned out");
          if(options.tree_fire=="model" && !by_trees)throw std::runtime_error("the fire did not spread from tree to tree");
          if(options.tree_fire=="grid" && by_trees)throw std::runtime_error("grid mode spread the fire between trees");
          if(tree_fire->obstacle_version()==tree_fire_obstacle_version_start || !low_logs)throw std::runtime_error("fallen trunks did not become obstacles on the ground");
          if(pixels==tree_fire_frame)throw std::runtime_error("fifteen seconds of fire changed nothing");
        } else {
          if(st.seconds!=0 || st.burning || st.branches_burning)throw std::runtime_error("the model did not reset");
          if(pixels!=tree_fire_reference)throw std::runtime_error("resetting the tree fire did not restore the reference exactly");
        }
      }
      if(options.gait_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto& probe=gait_probe;
        const auto& gait=walk_env.gait;
        std::cout<<"GAIT_TEST_JSON={\"frame\":"<<rendered_frames<<",\"speed_mps\":"<<person.gait_speed_mps<<",\"phase\":"<<person.gait_phase
                 <<",\"footfalls\":"<<probe.footfalls<<",\"slowest_mps\":"<<probe.slowest<<",\"fastest_mps\":"<<probe.fastest
                 <<",\"max_bob_m\":"<<probe.max_bob<<",\"sway_m\":["<<probe.min_sway<<","<<probe.max_sway<<"],\"bob_m\":"<<person.bob_meters
                 <<",\"roll_deg\":"<<person.roll_radians*180.0/3.14159265358979323846<<",\"stop_distance_m\":"<<probe.stop_distance
                 <<",\"step_length_m\":"<<terraflux::graphics::gait_step_length(gait,person.walk_speed_mps)<<"}\n";
        const double v=person.walk_speed_mps;
        if(rendered_frames==0) {
          if(!first_person || person.gait_speed_mps!=0)throw std::runtime_error("the gait test did not start standing in first person");
          gait_reference=pixels;
        } else if(rendered_frames==1) {
          if(std::abs(person.gait_speed_mps-v)>1e-9)throw std::runtime_error("the walking speed was not reached");
          if(probe.slowest>=v*(1-gait.speed_pulse*0.6) || probe.fastest<=v*(1+gait.speed_pulse*0.6))throw std::runtime_error("the speed does not pulse with the steps");
          if(probe.footfalls<2 || probe.footfalls>static_cast<std::uint64_t>(v/terraflux::graphics::gait_step_length(gait,v))+2)throw std::runtime_error("the footfalls do not follow the step length");
          if(!(probe.max_bob>0.005) || !(probe.max_sway>0) || !(probe.min_sway<0))throw std::runtime_error("the head does not bob and sway with the steps");
          if(!(person.bob_meters>0) || person.sway_meters==0 || person.roll_radians==0)throw std::runtime_error("the mid-step frame is not rendered mid-step");
          if(pixels==gait_reference)throw std::runtime_error("the mid-step view equals the standing view");
        } else if(rendered_frames==2) {
          if(person.gait_speed_mps!=0 || person.gait_phase!=0)throw std::runtime_error("releasing the keys did not stop on a footfall");
          if(probe.stop_distance<0.3 || probe.stop_distance>2*terraflux::graphics::gait_step_length(gait,v)+1e-9)throw std::runtime_error("the stop did not finish the step");
          if(person.bob_meters!=0 || person.sway_meters!=0 || person.roll_radians!=0 || person.footfall_dip_meters!=0 ||
             std::abs(camera_frame.eye.y-(person.feet.y+person.eye_height_meters))>1e-12)throw std::runtime_error("standing, the head is not exactly at rest");
        } else {
          if(pixels!=gait_reference)throw std::runtime_error("returning to the start did not restore the standing frame exactly");
        }
      }
      if(options.jump_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto& probe=jump_probe;
        const auto& physics=walk_env.physics;
        std::cout<<"JUMP_TEST_JSON={\"frame\":"<<rendered_frames<<",\"feet_y\":"<<person.feet.y<<",\"ground_y\":"<<probe.ground
                 <<",\"eye_y\":"<<camera_frame.eye.y<<",\"grounded\":"<<(person.grounded?"true":"false")<<",\"apex_m\":"<<probe.apex
                 <<",\"air_time_s\":"<<probe.air_time<<",\"expected_air_time_s\":"<<terraflux::graphics::jump_air_time(physics)
                 <<",\"dip_m\":"<<probe.dip<<",\"carried_m\":"<<probe.carried<<",\"expected_carry_m\":"<<probe.expected_carry<<"}\n";
        if(rendered_frames==0) {
          if(!first_person || !person.grounded)throw std::runtime_error("the jump test did not start standing in first person");
          jump_reference=pixels;
        } else if(rendered_frames==1) {
          if(person.grounded || std::abs(probe.apex-physics.jump_height_meters)>0.05)throw std::runtime_error("the jump did not peak at the jump height");
          if(std::abs((person.feet.y-probe.ground)-probe.apex)>0.02)throw std::runtime_error("the apex frame is not rendered at the apex");
          if(pixels==jump_reference)throw std::runtime_error("the view from the apex equals the view from the ground");
        } else if(rendered_frames==2) {
          if(!probe.landed || std::abs(person.feet.y-probe.ground)>1e-9)throw std::runtime_error("the jump did not land back on the ground");
          if(std::abs(probe.air_time-terraflux::graphics::jump_air_time(physics))>1.0/60.0+1e-9)throw std::runtime_error("the air time does not follow the gravity");
          if(!(probe.dip>0.05) || !(camera_frame.eye.y<probe.ground+person.eye_height_meters-0.05))throw std::runtime_error("the landing did not dip the eye");
          if(pixels==jump_reference)throw std::runtime_error("the landing frame equals the standing frame");
        } else if(rendered_frames==3) {
          if(person.crouch_meters!=0 || std::abs(camera_frame.eye.y-(probe.ground+person.eye_height_meters))>1e-12)throw std::runtime_error("the landing dip did not recover");
          if(pixels!=jump_reference)throw std::runtime_error("recovering from the jump did not restore the standing frame exactly");
        } else {
          if(!probe.landed)throw std::runtime_error("the running jump did not land");
          if(probe.carried<probe.expected_carry*0.8 || probe.carried>probe.expected_carry*1.1)throw std::runtime_error("the running jump did not carry the run through the air");
        }
      }
      if(options.collision_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto& probe=collision_probe;
        std::cout<<"COLLISION_TEST_JSON={\"frame\":"<<rendered_frames<<",\"start\":["<<probe.start.x<<","<<probe.start.y<<","<<probe.start.z
                 <<"],\"feet\":["<<person.feet.x<<","<<person.feet.y<<","<<person.feet.z<<"],\"walked_m\":"<<probe.walked
                 <<",\"requested_m\":"<<probe.requested<<",\"trunk_radius_m\":"<<probe.radius<<",\"clearance_m\":"<<probe.clearance
                 <<",\"rise_m\":"<<probe.rise<<"}\n";
        const double body=walk_env.body.radius;
        if(rendered_frames==0) {
          if(probe.walked<=0 || probe.walked>=probe.requested*0.5)throw std::runtime_error("the trunk did not stop the walker");
          if(probe.clearance<body-1e-6 || probe.clearance>body+0.1)throw std::runtime_error("the walker did not stop against the trunk");
          collision_frame=pixels;
        } else if(rendered_frames==1 && probe.requested>0) {
          if(probe.walked>=probe.requested-1e-6)throw std::runtime_error("the cliff did not stop the walker");
          if(probe.rise>probe.walked*std::tan(walk_env.body.max_slope_degrees*3.14159265358979323846/180.0)+0.5 || probe.clearance<=0)
            throw std::runtime_error("the walker climbed the cliff");
        } else if(rendered_frames==2) {
          if(probe.walked<probe.requested*0.8 || probe.clearance<3.0)throw std::runtime_error("grass blocked the walker"); // 0.8: the gait accelerates from rest
        } else if(rendered_frames==3) {
          if(probe.walked<probe.requested*0.9 || probe.clearance<10.0)throw std::runtime_error("--no-collision did not pass through the trunk");
          if(pixels==collision_frame)throw std::runtime_error("passing through the tree rendered the same image as stopping at it");
        }
      }
      if(options.first_person_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        const auto clear=RenderPassDesc{}.clear_color;
        std::array<std::uint8_t,3> sky{};
        for(std::size_t c=0;c<3;++c)sky[c]=static_cast<std::uint8_t>(std::lround(clear[c]*255.0));
        const auto is_sky=[&](std::size_t i){return pixels[i]==sky[0] && pixels[i+1]==sky[1] && pixels[i+2]==sky[2];};
        // GL rows grow upward: the lowest twentieth of the image is at the feet.
        std::size_t ground_at_feet=0,sky_pixels=0;
        const std::size_t foot_rows=static_cast<std::size_t>(std::max(1,scene_height/20));
        for(std::size_t y=0;y<foot_rows;++y)for(std::size_t x=0;x<static_cast<std::size_t>(scene_width);++x)
          if(!is_sky((y*static_cast<std::size_t>(scene_width)+x)*4))++ground_at_feet;
        for(std::size_t i=0;i<pixels.size();i+=4)if(is_sky(i))++sky_pixels;
        const auto expected_eye=first_person?person.eye():terraflux::graphics::OrbitCamera{scene_camera.target,yaw,pitch,distance}.eye();
        if(camera_frame.eye.x!=expected_eye.x || camera_frame.eye.y!=expected_eye.y || camera_frame.eye.z!=expected_eye.z)
          throw std::runtime_error("the rendered eye does not match the active camera");
        std::cout<<"FIRST_PERSON_TEST_JSON={\"frame\":"<<rendered_frames<<",\"view\":\""<<terraflux::graphics::camera_mode_name(view_mode)
                 <<"\",\"eye\":["<<camera_frame.eye.x<<","<<camera_frame.eye.y<<","<<camera_frame.eye.z<<"],\"feet\":["
                 <<person.feet.x<<","<<person.feet.y<<","<<person.feet.z<<"],\"walked_m\":"<<view_test_walked
                 <<",\"ground_at_feet\":"<<ground_at_feet<<",\"foot_pixels\":"<<foot_rows*static_cast<std::size_t>(scene_width)
                 <<",\"sky_pixels\":"<<sky_pixels<<"}\n";
        if(rendered_frames==0) {
          if(first_person)throw std::runtime_error("the first-person test did not start in the world view");
          view_test_reference=pixels;
        } else if(rendered_frames==1) {
          if(!first_person)throw std::runtime_error("entering first person failed");
          const auto ground=ground_height(person.feet.x,person.feet.z);
          if(!ground)throw std::runtime_error("the first-person feet are not over ground");
          const auto water=walk_env.water(person.feet.x,person.feet.z,*ground);
          const double expected_feet=water.depth_meters>=walk_env.water_rules.swim_depth_meters?water.surface_meters-walk_env.water_rules.swim_depth_meters:*ground;
          if(std::abs(person.feet.y-expected_feet)>1e-9)throw std::runtime_error("the first-person feet are neither on the ground nor afloat");
          if(std::abs(camera_frame.eye.y-(person.feet.y+person.eye_height_meters))>1e-9)
            throw std::runtime_error("the first-person eye is not at eye height above the feet");
          if(std::hypot(person.feet.x-view_test_orbit.target.x,person.feet.z-view_test_orbit.target.z)>1e-9)
            throw std::runtime_error("first person did not start under the orbit target");
          if(person.yaw!=view_test_orbit.yaw || person.pitch!=0.0)throw std::runtime_error("first person did not keep the orbit heading, level");
          if(pixels==view_test_reference)throw std::runtime_error("the first-person view shows the world view's image");
          if(ground_at_feet*2<foot_rows*static_cast<std::size_t>(scene_width))throw std::runtime_error("no ground at the feet of the first-person view");
          view_test_frame=pixels;
        } else if(rendered_frames==2 || rendered_frames==3) {
          if(pixels==view_test_frame)throw std::runtime_error(rendered_frames==2?"turning changed nothing":"walking changed nothing");
          if(rendered_frames==3) {
            const double moved=std::hypot(person.feet.x-view_test_person.feet.x,person.feet.z-view_test_person.feet.z);
            if(std::abs(moved-view_test_walked)>1e-6 || !(moved>0))throw std::runtime_error("walking did not move the feet by the walked distance");
            if(view_test_walked>person.walk_speed_mps*person.run_multiplier+1e-9)throw std::runtime_error("running exceeded the running speed");
            // (Stale flags are refreshed on the next step; here the support is checked directly.)
            const auto ground=ground_height(person.feet.x,person.feet.z);
            if(!ground)throw std::runtime_error("walking left the terrain");
            const auto water=walk_env.water(person.feet.x,person.feet.z,*ground);
            const double afloat=water.surface_meters-walk_env.water_rules.swim_depth_meters;
            const bool on_ground=std::abs(person.feet.y-*ground)<1e-9, floating=water.depth_meters>=walk_env.water_rules.swim_depth_meters && std::abs(person.feet.y-afloat)<1e-9;
            if(person.grounded && !on_ground && !floating)throw std::runtime_error("walking left the ground");
          }
          view_test_frame=pixels;
        } else {
          if(first_person)throw std::runtime_error("returning to the world view failed");
          if(scene_camera.target.x!=view_test_orbit.target.x || scene_camera.target.y!=view_test_orbit.target.y ||
             scene_camera.target.z!=view_test_orbit.target.z || yaw!=view_test_orbit.yaw || pitch!=view_test_orbit.pitch ||
             distance!=view_test_orbit.distance)
            throw std::runtime_error("returning to the world view did not restore the orbit camera exactly");
          if(pixels!=view_test_reference)throw std::runtime_error("returning to the world view did not restore the reference image");
        }
      }
      // The walker's arms: posed from the rig's state, placed in front of the eye.
      body_draws=0;
      if(first_person && show_arms) {
        auto state=terraflux::graphics::body_state_from(person,person.walk_speed_mps,person.run_multiplier);
        state.held=held.kind;state.held_size=held.size;
        state.tool=belt.selected;state.swing=belt.swing;state.sawing=belt.sawing;state.saw_phase=belt.saw_phase;
        if(rope.state!=terraflux::graphics::RopeState::Coiled && state.tool==terraflux::graphics::ToolKind::Rope)state.tool=terraflux::graphics::ToolKind::None;  // the rope is out in the world, not in the hand
        if(!options.body_pose_override.empty()) {
          static const std::pair<const char*,terraflux::graphics::BodyActivity> poses[]={
            {"idle",terraflux::graphics::BodyActivity::Idle},{"walk",terraflux::graphics::BodyActivity::Walk},{"run",terraflux::graphics::BodyActivity::Run},
            {"crouch",terraflux::graphics::BodyActivity::Crouch},{"climb",terraflux::graphics::BodyActivity::Climb},{"swim",terraflux::graphics::BodyActivity::Swim},
            {"crawl",terraflux::graphics::BodyActivity::Crawl},{"dive",terraflux::graphics::BodyActivity::Dive},{"down",terraflux::graphics::BodyActivity::Down}};
          for(const auto& [name,activity]:poses)if(options.body_pose_override==name){state.activity=activity;state.phase=0.25;state.speed_fraction=1.0;}
        }
        body_activity=state.activity;
        const auto arms=terraflux::graphics::body_pose(state,body_style);
        const auto forward=(pose.target-camera_frame.eye).normalized();
        const auto right=forward.cross(pose.up).normalized();
        const auto up=right.cross(forward).normalized();
        const auto to_world=terraflux::graphics::camera_to_world(camera_frame.eye,right,up,forward);
        body_draws=body.draw(camera,arms,to_world);
        frame_draws+=static_cast<std::uint32_t>(body_draws);
        if(held.kind!=terraflux::graphics::HeldKind::Nothing) {
          const auto& item=arms[terraflux::graphics::kHeldSegment];
          held_world_position=to_world.transformPoint(item.transform.transformPoint({0,0,item.size.z*0.45}));  // the far end of a branch
        }
      }
      // Stones and branches lying about, near the eye.
      prop_draws=0;
      {
        auto near_props=terraflux::graphics::props_near(stones,dropped_branches,camera_frame.eye,first_person?90.0:40.0,160);
        for(auto& rp:terraflux::graphics::rope_props(rope,person.eye()+look_forward()*0.3+terraflux::math::Vec3{0,-0.4,0},24))near_props.push_back(rp);
        std::vector<terraflux::graphics::Prop> all(near_props.begin(),near_props.end());
        for(const auto& p:projectiles) {
          terraflux::math::Mat4 m=terraflux::math::Mat4::identity();m.at(0,3)=p.position.x;m.at(1,3)=p.position.y;m.at(2,3)=p.position.z;
          all.push_back({m,p.item.kind==terraflux::graphics::HeldKind::Stone?p.item.stone_size:terraflux::math::Vec3{0.04,0.04,p.item.size},
                         p.item.kind==terraflux::graphics::HeldKind::Stone?terraflux::math::Vec3{0.42,0.41,0.38}:terraflux::math::Vec3{0.36,0.27,0.16}});
        }
        if(!all.empty()){prop_draws=props.draw(camera,all);frame_draws+=static_cast<std::uint32_t>(prop_draws);}
      }
      // Under water everything is seen through the green-blue of the lake.
      if(first_person && person.underwater()) {
        const terraflux::graphics::HudBar veil{-1.0f,-1.0f,2.0f,2.0f,1.0f,{0.10f,0.35f,0.40f,0.55f},{0.0f,0.0f,0.0f,0.0f}};
        hud.draw(std::span<const terraflux::graphics::HudBar>(&veil,1));
        frame_draws+=1;
      }
      // The body's bars over everything, in first person (smoke runs only when scripted).
      hud_bars_drawn=0;
      if(first_person && (!options.smoke_test || options.vitals_test)) {
        std::array<terraflux::graphics::HudBar,4> bars;
        const float x=-0.96f,w=0.28f,h=0.035f,gap=0.012f;
        bars[0]={x,-0.78f,w,h,static_cast<float>(vitals.health),{0.85f,0.15f,0.12f,0.92f},{0.05f,0.05f,0.05f,0.65f}};
        const float warm=static_cast<float>(vitals.heat);
        bars[1]={x,-0.78f-(h+gap),w,h,warm,{0.25f+0.7f*warm,0.45f*warm+0.25f*(1-warm),0.95f*(1-warm)+0.1f,0.92f},{0.05f,0.05f,0.05f,0.65f}};
        bars[2]={x,-0.78f-2*(h+gap),w,h,static_cast<float>(person.stamina),{0.2f,0.8f,0.25f,0.92f},{0.05f,0.05f,0.05f,0.65f}};
        bars[3]={x,-0.78f-3*(h+gap),w,h*0.5f,static_cast<float>(vitals.wetness),{0.45f,0.7f,0.95f,0.85f},{0.05f,0.05f,0.05f,0.5f}};
        hud_bars_drawn=hud.draw(bars);
        frame_draws+=1;
      }
      if(options.items_test) {
        std::size_t present=0;for(const auto& st:stones)if(st.present)++present;
        const auto exposure=gather_exposure();
        std::cout<<"ITEMS_TEST_JSON={\"frame\":"<<rendered_frames<<",\"did\":\""<<items_probe.did<<"\",\"held\":\""<<terraflux::graphics::held_kind_name(held.kind)
                 <<"\",\"stones\":"<<stones.size()<<",\"present\":"<<present<<",\"landed_m\":"<<items_probe.landed_distance<<",\"projectiles\":"<<projectiles.size()
                 <<",\"prop_draws\":"<<prop_draws<<",\"body_draws\":"<<body_draws<<",\"torch_s\":"<<held.torch_seconds<<",\"fire_heat\":"<<exposure.fire_heat
                 <<",\"burning_branches\":"<<(tree_fire?tree_fire->branches().size():0)<<",\"hint\":\""<<interact_options()
                 <<"\",\"feet\":["<<person.feet.x<<","<<person.feet.y<<","<<person.feet.z<<"],\"yaw_deg\":"<<person.yaw*180.0/3.14159265358979323846<<"}\n";
        using terraflux::graphics::HeldKind;
        if(rendered_frames==0) {
          if(held.kind!=HeldKind::Nothing || interact_options()!="E: pick up the stone" || !prop_draws)throw std::runtime_error("no stone in reach to pick up");
        } else if(rendered_frames==1) {
          if(held.kind!=HeldKind::Stone || present!=items_probe.stones_present-1 || items_probe.did!="picked up a stone")throw std::runtime_error("E did not pick up the stone");
          if(body_draws<=2*terraflux::graphics::kArmSegments)throw std::runtime_error("the held stone is not drawn in the hand");
        } else if(rendered_frames==2) {
          if(held.kind!=HeldKind::Nothing || !projectiles.empty() || present!=items_probe.stones_present)throw std::runtime_error("the thrown stone did not land as a stone");
          if(items_probe.landed_distance<3.0 || items_probe.landed_distance>25.0)throw std::runtime_error("the stone did not fly a throw's distance");
        } else if(rendered_frames==3) {
          if(held.kind!=HeldKind::Branch || items_probe.did!="broke off a branch")throw std::runtime_error("E by a tree did not break off a branch");
        } else if(rendered_frames==4) {
          if(held.kind!=HeldKind::Torch || items_probe.did!="lit the branch" || held.torch_seconds<=0)throw std::runtime_error("E by a burning tree did not light the branch");
          if(exposure.fire_heat<0.15)throw std::runtime_error("the torch does not warm");
        } else if(rendered_frames==5) {
          if(items_probe.did!="set the tree alight" || held.kind!=HeldKind::Torch)throw std::runtime_error("the torch did not set the tree alight");
          if(tree_fire->states()[static_cast<std::size_t>(items_probe.tree)].source!=IgnitionSource::Ember)throw std::runtime_error("the tree was not lit by the torch");
        } else {
          if(held.kind!=HeldKind::Nothing || tree_fire->branches().size()<=items_probe.branches_burning)throw std::runtime_error("the thrown torch did not land as a burning branch");
        }
      }
      if(options.tools_test) {
        using terraflux::graphics::ToolKind;using terraflux::graphics::RopeState;using terraflux::graphics::BodyActivity;
        const auto stats=tree_fire->stats();
        std::cout<<"TOOLS_TEST_JSON={\"frame\":"<<rendered_frames<<",\"did\":\""<<tools_probe.did<<"\",\"tool\":\""<<terraflux::graphics::tool_kind_name(belt.selected)
                 <<"\",\"rope\":\""<<terraflux::graphics::rope_state_name(rope.state)<<"\",\"rope_branch\":"<<rope.branch_tree<<",\"rope_height_m\":"<<rope.anchor_height_meters
                 <<",\"rope_span_m\":"<<rope.span_meters()<<",\"on_rope\":"<<(person.on_rope?"true":"false")<<",\"rope_t_m\":"<<person.rope_t_meters
                 <<",\"feet\":["<<person.feet.x<<","<<person.feet.y<<","<<person.feet.z<<"],\"yaw_deg\":"<<person.yaw*180.0/3.14159265358979323846<<",\"felled\":"<<stats.felled<<",\"chops\":"<<tools_probe.chops
                 <<",\"expected_chops\":"<<tools_probe.expected_chops<<",\"saw_s\":"<<tools_probe.saw_seconds<<",\"expected_saw_s\":"<<tools_probe.expected_saw_seconds
                 <<",\"fuel_s\":"<<belt.chainsaw_fuel_seconds<<",\"fall_alignment\":"<<tools_probe.fall_alignment<<",\"branches\":"<<dropped_branches.size()
                 <<",\"crushed\":"<<tools_probe.crushed<<",\"face_m\":"<<tools_probe.face<<",\"health\":"<<vitals.health<<",\"body_draws\":"<<body_draws<<",\"prop_draws\":"<<prop_draws
                 <<",\"activity\":\""<<terraflux::graphics::body_activity_name(body_activity)<<"\",\"hint\":\""<<(belt.selected!=ToolKind::None || person.on_rope?tool_hint():interact_options())<<"\"}\n";
        const std::size_t arms=2*terraflux::graphics::kArmSegments;
        if(rendered_frames==0) {
          if(belt.selected!=ToolKind::Rope || rope.state!=RopeState::Coiled || body_draws!=arms+1)throw std::runtime_error("the coiled rope is not in the hand");
          tools_reference_feet=person.feet;
        } else if(rendered_frames==1 && tools_probe.face<=0) {
          if(rope.state!=RopeState::Laid || prop_draws<1 || body_draws!=arms)throw std::runtime_error("the rope thrown on the flat does not lie there drawn: "+tools_probe.did);
        } else if(rendered_frames==2 && tools_probe.face<=0) {
          if(person.on_rope || rope.state!=RopeState::Laid)throw std::runtime_error("a rope lying on the flat is nothing to climb");
        } else if(rendered_frames==3 && tools_probe.face<=0) {
          if(rope.state!=RopeState::Coiled)throw std::runtime_error("the rope was not pulled in: "+tools_probe.did);
        } else if(rendered_frames==1) {
          if(rope.state!=RopeState::Hung || rope.branch_tree>=0 || rope.anchor_height_meters<std::min(4.0,0.6*tools_probe.face))throw std::runtime_error("the rope thrown over the edge did not hang down the face: "+tools_probe.did);
          if(std::abs(rope.anchor.y-tools_reference_feet.y)>4.0 || (rope.anchor-tools_reference_feet).length()>rope_rules.length_meters)throw std::runtime_error("the rope is not pegged at the lip");
          if(prop_draws<1 || body_draws!=arms)throw std::runtime_error("the hanging rope is not drawn (or the coil still is)");
        } else if(rendered_frames==2) {
          if(!person.on_rope || person.feet.y>rope.anchor.y-2.0 || body_activity!=BodyActivity::Climb)throw std::runtime_error("the walker is not climbing down the rope");
        } else if(rendered_frames==3) {
          if(person.on_rope || !person.grounded)throw std::runtime_error("the walker did not step off at the foot of the face");
          if(person.feet.y>tools_reference_feet.y-std::min(4.0,0.6*tools_probe.face))throw std::runtime_error("the walker is not down the face");
          if(rope.state!=RopeState::Coiled)throw std::runtime_error("the rope was not pulled in: "+tools_probe.did);
        } else if(rendered_frames==4) {
          if(tools_probe.tree<0 || tree_fire->standing(static_cast<std::uint32_t>(tools_probe.tree)) || !tree_fire->states()[static_cast<std::size_t>(tools_probe.tree)].felled)throw std::runtime_error("the axe did not fell the tree");
          if(tools_probe.chops!=tools_probe.expected_chops)throw std::runtime_error("the tree took "+std::to_string(tools_probe.chops)+" chops, not "+std::to_string(tools_probe.expected_chops));
          if(tools_probe.fall_alignment<0.9)throw std::runtime_error("the felled tree did not fall away from the walker");
          if(body_draws!=arms+2)throw std::runtime_error("the axe is not drawn in the hands");
        } else if(rendered_frames==5) {
          if(tools_probe.tree<0 || tree_fire->standing(static_cast<std::uint32_t>(tools_probe.tree)) || stats.felled<2)throw std::runtime_error("the chainsaw did not fell the tree");
          if(std::abs(tools_probe.saw_seconds-tools_probe.expected_saw_seconds)>0.25)throw std::runtime_error("the saw took "+std::to_string(tools_probe.saw_seconds)+" s, not "+std::to_string(tools_probe.expected_saw_seconds));
          if(std::abs((tools_probe.fuel_before-belt.chainsaw_fuel_seconds)-tools_probe.saw_seconds)>0.1)throw std::runtime_error("the fuel burned does not match the sawing time");
          if(tools_probe.fall_alignment<0.9 || body_draws!=arms+2)throw std::runtime_error("the sawed tree did not fall away, or the saw is not drawn");
        } else if(rendered_frames==6) {
          if(dropped_branches.size()!=3 || tools_probe.did.find("too short")==std::string::npos)throw std::runtime_error("the branch was not cut in pieces as expected: "+tools_probe.did);
          double shortest=10;for(const auto& b:dropped_branches)shortest=std::min(shortest,b.length);
          if(std::abs(shortest-0.4)>1e-6)throw std::runtime_error("the pieces are not the halves expected");
        } else if(rendered_frames==7) {
          if(rope.state!=RopeState::Hung || rope.branch_tree<0 || rope.anchor_height_meters<3.0)throw std::runtime_error("the rope did not wrap round a branch");
          if(!person.on_rope || person.feet.y<rope.anchor.y-0.2 || body_activity!=BodyActivity::Climb)throw std::runtime_error("the walker is not perched up the rope");
        } else {
          if(tools_probe.crushed<=0 || person.stunned_seconds<=0)throw std::runtime_error("the tree felled toward the walker did not land on it");
          if(vitals.health>=1.0 || vitals.last_cause!=terraflux::graphics::DamageCause::Crushed)throw std::runtime_error("being crushed cost no health");
        }
      }
      if(options.body_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        // Skin-colored pixels in the lower half: the hands.
        std::size_t skin=0;
        for(int y=0;y<scene_height/2;++y)for(int x=0;x<scene_width;++x) {
          const std::size_t i=(static_cast<std::size_t>(y)*scene_width+x)*4;
          const int r=pixels[i],g=pixels[i+1],b=pixels[i+2];
          if(r>120 && r>g+25 && g>b+10 && r<250)++skin;
        }
        std::cout<<"BODY_TEST_JSON={\"frame\":"<<rendered_frames<<",\"activity\":\""<<terraflux::graphics::body_activity_name(body_activity)
                 <<"\",\"draws\":"<<body_draws<<",\"skin_pixels\":"<<skin<<",\"swimming\":"<<(person.swimming?"true":"false")
                 <<",\"diving\":"<<(person.diving?"true":"false")<<",\"underwater\":"<<(person.underwater()?"true":"false")
                 <<",\"breath\":"<<person.breath<<",\"strokes\":"<<person.strokes<<",\"eye_y\":"<<camera_frame.eye.y<<"}\n";
        if(rendered_frames==0) {
          if(body_draws!=2*terraflux::graphics::kArmSegments || body_activity!=terraflux::graphics::BodyActivity::Idle)throw std::runtime_error("the idle arms were not drawn");
          if(skin<200)throw std::runtime_error("looking down, the hands are not in view");
          body_reference=pixels;
        } else if(rendered_frames==1) {
          if(body_draws!=0 || pixels==body_reference)throw std::runtime_error("hiding the arms changed nothing");
        } else if(rendered_frames==2) {
          if(body_activity!=terraflux::graphics::BodyActivity::Run || pixels==body_reference || skin<50)throw std::runtime_error("the running arms are not in view");
          body_frame=pixels;
        } else if(rendered_frames==3) {
          if(body_activity!=terraflux::graphics::BodyActivity::Climb || pixels==body_frame || skin<50)throw std::runtime_error("the climbing hands are not in view");
        } else if(rendered_frames==4) {
          if(!person.swimming || body_activity!=terraflux::graphics::BodyActivity::Swim || skin<30)throw std::runtime_error("afloat, the stroking hands are not in view");
          if(person.strokes==0)throw std::runtime_error("swimming made no strokes");
        } else if(rendered_frames==5) {
          if(!person.diving || !person.underwater() || body_activity!=terraflux::graphics::BodyActivity::Dive)throw std::runtime_error("Ctrl did not dive");
          if(person.breath>=1.0 || camera_frame.eye.y>=water_style->level_meters)throw std::runtime_error("the diver's eye is not under the surface");
        } else {
          if(pixels!=body_reference)throw std::runtime_error("returning to the start did not restore the reference exactly");
        }
      }
      if(options.vitals_test) {
        const auto pixels=device.capture_rgba(scene_width,scene_height);
        std::cout<<"VITALS_TEST_JSON={\"frame\":"<<rendered_frames<<",\"health\":"<<vitals.health<<",\"heat\":"<<vitals.heat<<",\"wetness\":"<<vitals.wetness
                 <<",\"alive\":"<<(vitals.alive?"true":"false")<<",\"cause\":\""<<terraflux::graphics::damage_cause_name(vitals.last_cause)
                 <<"\",\"fire_heat\":"<<vitals_probe.heat_exposure<<",\"smoke\":"<<vitals_probe.smoke_exposure<<",\"impact_mps\":"<<vitals_probe.impact
                 <<",\"hud_bars\":"<<hud_bars_drawn<<",\"stunned_s\":"<<person.stunned_seconds<<"}\n";
        // The bars' rows: NDC -0.78 .. -0.78 - 3 x 0.047, at the left.
        const auto bar_pixels=[&](const std::vector<std::uint8_t>& p) {
          std::vector<std::uint8_t> out;
          const int y0=static_cast<int>((1.0-0.78-0.15)*0.5*scene_height),y1=static_cast<int>((1.0-0.78+0.05)*0.5*scene_height);
          const int x1=static_cast<int>((1.0-0.96+0.30)*0.5*scene_width);
          for(int y=std::max(0,y0);y<std::min(scene_height,y1);++y)for(int x=0;x<x1;++x)for(int c=0;c<3;++c)out.push_back(p[(static_cast<std::size_t>(y)*scene_width+x)*4+c]);
          return out;
        };
        if(rendered_frames==0) {
          if(hud_bars_drawn!=4 || vitals.health!=1 || vitals.heat!=1 || vitals.wetness!=0)throw std::runtime_error("the vitals test did not start fresh with its bars drawn");
          vitals_reference=bar_pixels(pixels);
        } else if(rendered_frames==1) {
          if(vitals_probe.heat_exposure<vitals_style.burn_threshold)throw std::runtime_error("two metres from a burning tree is not hot");
          if(vitals.health>=1 || vitals.last_cause!=terraflux::graphics::DamageCause::Fire)throw std::runtime_error("the fire did not burn the health");
          if(bar_pixels(pixels)==vitals_reference)throw std::runtime_error("the health bar did not change");
        } else if(rendered_frames==2) {
          if(vitals.wetness<0.99 || vitals.heat>0.5)throw std::runtime_error("freezing rain, wind and water did not chill the soaked walker");
        } else if(rendered_frames==3) {
          if(vitals_probe.impact<10 || vitals_probe.health_after_fall>=1.0)throw std::runtime_error("the fall did not land hard");
          if(vitals.alive || vitals.health!=0 || person.stunned_seconds<=0 || collapse_message.empty())throw std::runtime_error("a hard fall and a direct strike did not collapse the walker");
        } else {
          if(!vitals.alive || vitals.health!=1 || person.stunned_seconds!=0 || !collapse_message.empty())throw std::runtime_error("a new attempt did not start with a fresh body");
        }
      }
      if(options.smoke_test || (options.profile && profile_frames%120==0)) {
        std::cout << "Visibility: " << visible_instances << " visible, "
                  << culled_instances << " culled, "
                  << queue_stats.submitted << " opaque draws, "
                  << queue_stats.instances << " object instances, "
                  << queue_stats.state_groups << " state groups, "
                  << transparent_draws << " transparent draws\n";
      }
      object_pass.end();
      if(options.smoke_test && imported &&
         (!imported->uses_scene_file || !imported->scene_instances.empty())) {
        if(imported->uses_pbr) {
          const auto frame=device.capture_rgba(scene_width,scene_height);
          if(frame.size()!=terrain_snapshot.size())
            throw std::runtime_error("glTF smoke framebuffer size mismatch");
          std::size_t changed=0;
          for(std::size_t i=0;i<frame.size();i+=4){
            int difference=0;
            for(int channel=0;channel<3;++channel)
              difference+=std::abs(int(frame[i+channel])-int(terrain_snapshot[i+channel]));
            if(difference>=6)++changed;
          }
          if(changed==0)
            throw std::runtime_error("glTF smoke test rendered no pixels over terrain");
          std::cout<<(imported->is_gltf?"Visible glTF changed pixels: ":"Visible PBR scene changed pixels: ")<<changed<<'\n';
        }else{
          const auto visible_asset=device.count_asset_pixels(scene_width,scene_height);
          if(visible_asset==0)
            throw std::runtime_error("smoke test found no visible OBJ pixels");
          std::cout<<"Visible OBJ pixels: "<<visible_asset<<'\n';
        }
      }
      // Standing on the ground the fire may be behind the viewer or hidden by
      // the terrain, so only world-view frames have to show it.
      if (options.smoke_test && playback && layer_visibility.fire && !playback->frames[playback_frame].active.empty() && !first_person) {
        const auto visible_fire = device.count_fire_pixels(scene_width, scene_height);
        if (visible_fire == 0)
          throw std::runtime_error("smoke test found no visible fire pixels");
        std::cout << "Visible fire pixels: " << visible_fire << '\n';
      }
      if(offscreen.target()) {
        const auto source=options.smoke_test?device.capture_rgba(scene_width,scene_height):
                                            std::vector<std::uint8_t>{};
        RenderPassScope composite_pass(device,{window_target});
        compositor->draw(offscreen.color());++frame_draws;
        composite_pass.end();
        if(options.smoke_test) {
          const auto presented=device.capture_rgba(width,height);
          for(std::uint64_t y=0;y<window_target.height;++y)
            for(std::uint64_t x=0;x<window_target.width;++x) {
              const auto sx=((2*x+1)*scene_target.width)/(2*window_target.width);
              const auto sy=((2*y+1)*scene_target.height)/(2*window_target.height);
              for(std::size_t channel=0;channel<4;++channel)
                if(presented[(y*window_target.width+x)*4+channel]!=
                   source[(sy*scene_target.width+sx)*4+channel])
                  throw std::runtime_error("offscreen viewer composite pixel mismatch");
            }
          std::cout<<"Offscreen composed: "<<scene_width<<"x"<<scene_height
                   <<" -> "<<width<<"x"<<height<<'\n';
        }
      }
      if(record_gpu)device.end_gpu_timing();
      const double profile_render_end=glfwGetTime();
      cpu_profiler.record({profile_render_start-profile_update_start,
                           profile_render_end-profile_render_start,frame_draws});
      if(options.benchmark_frames && rendered_frames>=kBenchmarkWarmup) {
        benchmark_cpu_update+=profile_render_start-profile_update_start;
        benchmark_cpu_render+=profile_render_end-profile_render_start;
        benchmark_draws+=frame_draws;
        benchmark_terrain_draws+=tiled_terrain?terrain_stats.draws:(draw_surface?1:0);
        benchmark_terrain_culled+=terrain_stats.culled;
        benchmark_terrain_uploads+=terrain_residency_change.admitted;
        benchmark_terrain_evictions+=terrain_residency_change.evicted;
        benchmark_terrain_retained+=terrain_residency_change.retained;
        benchmark_terrain_trimmed+=terrain_residency_change.trimmed;
        benchmark_visible+=visible_instances;
        benchmark_culled+=culled_instances;
      }
      ++profile_frames;
      if(options.profile && profile_frames%120==0) {
        report_residency();
        const auto measured=cpu_profiler.summary();
        const auto gpu=device.gpu_timing_summary();
        std::cout << "CPU profile (last " << measured.samples << " frames): update "
                  << measured.mean_update_ms << " ms, render "
                  << measured.mean_render_ms << " ms, worst "
                  << measured.max_frame_ms << " ms, draw calls/frame "
                  << measured.mean_draw_calls << " | GPU clear+draw mean "
                  << gpu.mean_ms << " ms (" << gpu.samples
                  << " samples, " << gpu.skipped << " skipped)\n";
      }
      if(!options.screenshot_file.empty()) {
        // The window back buffer is undefined after presentation; read it first.
        screenshot_pixels=device.capture_rgba(width,height);
        screenshot_width=width;screenshot_height=height;
        // TERRAFLUX_SCREENSHOT_EVERY_FRAME=1 also writes every frame of a scripted run (name-N.ppm).
        if(std::getenv("TERRAFLUX_SCREENSHOT_EVERY_FRAME")) {
          std::string name=options.screenshot_file;
          const auto dot=name.rfind('.');
          name.insert(dot==std::string::npos?name.size():dot,"-"+std::to_string(rendered_frames));
          write_ppm(name,width,height,screenshot_pixels);
        }
      }
      glfwSwapBuffers(window.get());
      ++rendered_frames;
      if(options.replay_controls_test) {
        if(rendered_frames==7) {
          std::cout<<"Replay controls passed: speed, pause, forward, back, resume, reset; frame pixels and engine clock preserved\n";
          break;
        }
      }else if(options.fire_test) {
        if(rendered_frames==4) {
          // Charred plants: trees in burned cells must render darker than living ones.
          std::cout<<"Fire effects passed: fuel-gated flames and smoke rise from burning cells, animate, char the vegetation and hide cleanly\n";
          break;
        }
      }else if(options.weather_test) {
        if(rendered_frames==11) {
          std::cout<<"Weather passed: sky dome, daylight and moonlight match the CPU twin, twilight glows, stratus fogs the summit and drizzles, cumulus drifts with a gusting wind, the thunderstorm strikes, pours, squalls and cools, rain runs off, floods and sprays off the cliffs, freezing rain snows, disabling restores the reference\n";
          break;
        }
      }else if(options.vegetation_test) {
        if(rendered_frames==5) {
          std::cout<<"Vegetation passed: trees and grass with height and varied color appear, meshes swap at run time, wind sways them, disabling restores the reference\n";
          break;
        }
      }else if(options.tools_test) {
        if(rendered_frames==9) {
          std::cout<<"Tools passed: a rope thrown over a cliff hangs down the face to be climbed down and up, thrown into a crown it wraps round a branch and is climbed to a perch; the axe chops a tree down in its counted chops and the chainsaw saws one through in its time and fuel, both falling away from the walker; branches are cut in halves down to a quarter metre; a tree felled toward the walker lands on it\n";
          break;
        }
      }else if(options.items_test) {
        if(rendered_frames==7) {
          std::cout<<"Items passed: a stone is picked up, carried in the hand and thrown to land as a stone; a branch is broken off a tree, lit at a burning one into a warming torch, used to set a tree alight, and thrown to burn on the ground\n";
          break;
        }
      }else if(options.body_test) {
        if(rendered_frames==7) {
          std::cout<<"Body passed: the hands hang into view when looking down, pump when running, reach when climbing, stroke when swimming and go forward diving under the surface; hidden they vanish and the reference is restored exactly\n";
          break;
        }
      }else if(options.vitals_test) {
        if(rendered_frames==5) {
          std::cout<<"Vitals passed: a burning tree burns the health and the bars show it, freezing rain, wind and water soak and chill the body, a hard fall and a direct strike collapse the walker, and a new attempt starts fresh\n";
          break;
        }
      }else if(options.climb_test) {
        if(rendered_frames==4) {
          std::cout<<"Climb passed: a rock face too steep to walk is climbed, slowly and at a cost of strength; a running waterfall refuses the climb; returning restores the reference\n";
          break;
        }
      }else if(options.city_test) {
        if(rendered_frames==5) {
          std::cout<<"City passed: a CBD of skyscrapers, five or more suburbs of houses with police stations, an airport with runway, taxiway, apron, terminal, tower and hangars, curved streets, and multi-lane highways with diamond interchanges whose viaducts carry the highway over the avenues; every building stands beside a road; the world view is restored exactly\n";
          break;
        }
      }else if(options.hazards_test) {
        if(rendered_frames==5) {
          std::cout<<"Hazards passed: a torch lights the bungalow's plank walls and the fire takes the thatch, the door and the room, its heat reaching the walker and the trees; the roof falls in on the walker inside and the bungalow collapses into rubble; a trunk smashes the house's tiles and its burning door bursts the glass while the stone stands; lightning scorches the skyscraper and a torrent washes the hospital out; reset restores everything\n";
          break;
        }
      }else if(options.interior_test) {
        if(rendered_frames==5) {
          std::cout<<"Interior passed: the bungalow's door stops the walker on the ramp at its threshold, E swings it open, the walker steps in onto the timber floor where the furniture stops it, the door closed from inside keeps it in, and opened again lets it out\n";
          break;
        }
      }else if(options.buildings_test) {
        if(rendered_frames==4) {
          std::cout<<"Buildings passed: a bungalow, a two-storey house, a five-storey hospital with its red cross and a twenty-storey glass skyscraper stand beside the roads with their doors to the road and footpaths to the carriageway; the footpath carries the walker to the hospital's door, where the wall stops it; hiding them restores the reference\n";
          break;
        }
      }else if(options.roads_test) {
        if(rendered_frames==7) {
          std::cout<<"Roads passed: a connected network of two-way roads and a bridge is drawn over the terrain, the walker goes faster on the road, crosses the water on the deck behind its railings, a burning tree felled across the road blocks and damages it, the deck destroyed under the walker hurts it and drops it into the water, and resetting restores the reference\n";
          break;
        }
      }else if(options.flood_test) {
        if(rendered_frames==4) {
          std::cout<<"Flood passed: a thunderstorm's rain fills the drainage channels with running water that is drawn, waded and, when fast and deep, sweeps the walker downstream; draining restores the reference\n";
          break;
        }
      }else if(options.storm_test) {
        if(rendered_frames==4) {
          std::cout<<"Storm passed: a bright strike splits and lights the tree beside it and strikes the walker down, who gets up as the tree burns on the ground; a gust front uproots trees and drags the standing walker downwind\n";
          break;
        }
      }else if(options.tree_fire_test) {
        if(rendered_frames==4) {
          std::cout<<"Tree fire passed: trees of different weight and strength ignite from the replay's front, flame, drop burning branches, burn through, fall onto the ground or their neighbours, light them, burn out, and lie as obstacles; resetting restores the reference\n";
          break;
        }
      }else if(options.gait_test) {
        if(rendered_frames==4) {
          std::cout<<"Gait passed: the walker accelerates from rest, moves in steps whose speed pulses from footfall to push-off, bobs, sways and rolls with the stance foot, and stops on a footfall with the head exactly at rest\n";
          break;
        }
      }else if(options.jump_test) {
        if(rendered_frames==5) {
          std::cout<<"Jump passed: Space leaves the ground, peaks at the jump height, lands after the Earth-gravity air time with an eye dip that recovers exactly, and a running jump carries the run\n";
          break;
        }
      }else if(options.collision_test) {
        if(rendered_frames==4) {
          std::cout<<"Collision passed: trunks stop the walker at body radius, a cliff face cannot be climbed, grass is walked through, and --no-collision walks through everything\n";
          break;
        }
      }else if(options.first_person_test) {
        if(rendered_frames==5) {
          std::cout<<"First person passed: stands at eye height on the ground under the world view's target, turns, walks over the terrain, and returning restores the world view exactly\n";
          break;
        }
      }else if(options.water_test) {
        if(rendered_frames==4) {
          std::cout<<"Water passed: flat surface matches the CPU reference; waves and foam animate without touching land; restoration exact\n";
          break;
        }
      }else if(options.terrain_shading_test) {
        if(rendered_frames==4) {
          std::cout<<"Terrain shading passed: materials, height ramp, slope hillshade and rock match the CPU reference; contours toggle cleanly; detail textures the surface\n";
          break;
        }
      }else if(options.layers_test) {
        if(rendered_frames==5) {
          std::cout<<"Layer visibility passed: both, terrain, fire, hidden, restore; replay data and clock preserved\n";
          break;
        }
      }else if(options.terrain_retention_test) {
        if(rendered_frames==1) {
          terrain_initial_residents=tiled_terrain->resident_tiles();
          for(const auto id:terrain_initial_residents)terrain_initial_handles.push_back(tiled_terrain->resident_vertex_buffer(id));
        } else if(rendered_frames<=5) {
          if(terrain_residency_change.admitted || terrain_residency_change.evicted ||
             tiled_terrain->resident_tiles()!=terrain_initial_residents)
            throw std::runtime_error("terrain retention changed buffers before expiry");
          if(rendered_frames!=3 && !terrain_residency_change.retained)
            throw std::runtime_error("terrain retention test held no hidden tiles");
          for(std::size_t i=0;i<terrain_initial_residents.size();++i)
            if(tiled_terrain->resident_vertex_buffer(terrain_initial_residents[i])!=terrain_initial_handles[i])
              throw std::runtime_error("terrain retention replaced a retained GPU handle");
        } else if(rendered_frames==6) {
          if(!terrain_residency_change.evicted || tiled_terrain->resident_tile_count()>=terrain_initial_residents.size())
            throw std::runtime_error("terrain retention did not expire hidden tiles");
          tiled_terrain->set_resident_tiles({});
          if(tiled_terrain->resident_bytes())throw std::runtime_error("terrain retention cleanup retained GPU bytes");
          std::cout<<"Terrain retention passed: hold, return without uploads, reset age, expire, cleanup\n";
          break;
        }
      }else if(options.terrain_residency_test) {
        if(rendered_frames==1) {
          terrain_initial_residents=tiled_terrain->resident_tiles();
          for(const auto id:terrain_initial_residents)terrain_initial_handles.push_back(tiled_terrain->resident_vertex_buffer(id));
        }else if(rendered_frames==2) {
          if(!terrain_saw_eviction || tiled_terrain->resident_tile_count()>=terrain_initial_residents.size())
            throw std::runtime_error("terrain residency test evicted no hidden tiles");
          const auto before=tiled_terrain->resident_tiles();
          const auto bytes=device.live_resource_bytes();const auto counts=device.live_resource_counts();
          device.set_resource_byte_budget(bytes.total());bool rejected=false;
          try{tiled_terrain->set_resident_tiles(terrain_initial_residents);}
          catch(const std::length_error&){rejected=true;}
          device.set_resource_byte_budget(std::numeric_limits<std::uint64_t>::max());
          if(!rejected || tiled_terrain->resident_tiles()!=before || device.live_resource_bytes()!=bytes ||
             device.live_resource_counts()!=counts)
            throw std::runtime_error("rejected terrain readmission changed existing resources");
        }else if(rendered_frames==3) {
          if(!terrain_saw_readmission || tiled_terrain->resident_tiles()!=terrain_initial_residents)
            throw std::runtime_error("terrain readmission did not recover the original visible set");
          bool replaced=false;
          for(std::size_t i=0;i<terrain_initial_residents.size();++i)
            if(tiled_terrain->resident_vertex_buffer(terrain_initial_residents[i])!=terrain_initial_handles[i])replaced=true;
          if(!replaced)throw std::runtime_error("cold terrain tiles retained old GPU handles");
        }else if(rendered_frames==4) {
          tiled_terrain->set_resident_tiles({});
          if(tiled_terrain->resident_bytes() || tiled_terrain->resident_tile_count())
            throw std::runtime_error("terrain full eviction retained GPU storage");
        }else if(rendered_frames==5) {
          if(tiled_terrain->resident_tiles()!=terrain_initial_residents)
            throw std::runtime_error("terrain cold rehydration changed visible set");
          tiled_terrain->set_resident_tiles({});
          std::cout<<"Terrain residency passed: evict, reject, preserve, retry, full eviction, cold readmission\n";
          break;
        }
      }else if(options.terrain_tile_test) {
        if(rendered_frames==3) {
          if(!tile_test_culled)throw std::runtime_error("terrain tile test culled no tiles");
          std::cout<<"Terrain tiles passed: three camera views match monolithic pixels; culled "
                   <<tile_test_culled<<" tile draws\n";
          break;
        }
      }else if(options.offscreen_resize_test) {
        const auto target=offscreen.target();
        if(!target)throw std::runtime_error("resize test has no offscreen target");
        if(rendered_frames==1) {
          resize_test_original=target;
          device.set_resource_byte_budget(device.live_resource_bytes().total());
          glfwSetWindowSize(window.get(),768,480);glfwPollEvents();
        } else if(rendered_frames==2) {
          if(!resize_failures || target->handle!=resize_test_original->handle ||
             target->width==static_cast<std::uint32_t>(width))
            throw std::runtime_error("resize rejection did not preserve old target");
          device.set_resource_byte_budget(std::numeric_limits<std::uint64_t>::max());
        } else if(rendered_frames==3) {
          if(target->handle==resize_test_original->handle || target->width!=static_cast<std::uint32_t>(width) ||
             target->height!=static_cast<std::uint32_t>(height))
            throw std::runtime_error("same-extent resize did not recover after budget increase");
          glfwSetWindowSize(window.get(),1280,800);glfwPollEvents();
        } else if(rendered_frames==4) {
          if(target->width!=1280 || target->height!=800)
            throw std::runtime_error("second window resize did not replace target");
          offscreen.reset();
          if(device.live_resource_bytes().render_target_bytes || device.live_resource_counts().render_targets)
            throw std::runtime_error("viewer target cleanup leaked storage");
          std::cout<<"Offscreen resize passed: allocate, reject, retain, recover, resize, cleanup\n";
          break;
        }
      }else if(options.resource_recovery_test) {
        if(rendered_frames==1) {
          const auto before=device.live_resource_bytes();
          const auto counts=device.live_resource_counts();
          device.set_resource_byte_budget(before.total());
          std::array<std::byte,16> probe{};
          bool rejected=false;
          try{
            const auto unexpected=device.create_buffer({BufferUsage::Storage,probe.size()},probe);
            device.destroy_buffer(unexpected);
          }catch(const std::length_error&){rejected=true;}
          if(!rejected || device.live_resource_bytes()!=before || device.live_resource_counts()!=counts)
            throw std::runtime_error("byte budget rejection changed current scene resources");
          report_residency();
          residency_node=imported->scene_instances.front().node;
          while(!imported->scene_instances.empty())detach_node(imported->scene_instances.front().node);
        }else if(rendered_frames==2) {
          reattach_node(residency_node);
        }else if(rendered_frames==3) {
          const auto telemetry=device.resource_telemetry();
          if(!telemetry.budget_rejections || telemetry.over_budget || !telemetry.headroom_bytes)
            throw std::runtime_error("resource budget did not recover headroom after eviction");
          report_residency();
          std::cout<<"Resource recovery passed: rejected safely, evicted, reallocated, rendered within cap\n";
          unload_imported();
          break;
        }
      }else if(options.scene_residency_test) {
        if(rendered_frames==1) {
          residency_node=imported->scene_instances.front().node;
          // Prefer a node sharing a mesh with another node, if available.
          for(const auto& first:imported->scene_instances)
            for(const auto& second:imported->scene_instances)
              if(first.node!=second.node && first.mesh_index==second.mesh_index)
                residency_node=first.node;
          detach_node(residency_node);
          residency_handles.push_back(imported->vb);
          for(const auto& mesh:imported->additional_meshes)residency_handles.push_back(mesh.vb);
        }else if(rendered_frames==2) {
          reattach_node(residency_node);
          for(std::size_t index=0;index<residency_handles.size();++index) {
            const auto current=index==0?imported->vb:imported->additional_meshes[index-1].vb;
            if(residency_handles[index].id && current!=residency_handles[index])
              throw std::runtime_error("reattachment replaced still-resident geometry");
          }
        }else if(rendered_frames==3) {
          while(!imported->scene_instances.empty())detach_node(imported->scene_instances.front().node);
          const auto counts=device.live_resource_counts();
          if(counts.buffers!=terrain_resources().buffers || counts.textures!=terrain_resources().textures)
            throw std::runtime_error("cold residency retained drawable resources");
        }else if(rendered_frames==4) {
          reattach_node(residency_node);
          if(imported->scene_instances.empty())throw std::runtime_error("cold reattachment produced no instances");
        }else if(rendered_frames==5) {
          unload_imported();
          std::cout<<"Scene residency passed: shared reuse, full eviction, cold reattachment, rendered recovery\n";
          break;
        }
      }else if(options.scene_detach_test) {
        if(!imported->scene_instances.empty()) {
          const auto node=imported->scene_instances.front().node;
          std::vector<BufferHandle> previous_vertices{imported->vb};
          for(const auto& mesh:imported->additional_meshes)previous_vertices.push_back(mesh.vb);
          detach_node(node);
          for(std::size_t mesh=0;mesh<previous_vertices.size();++mesh) {
            const bool used=std::any_of(imported->scene_instances.begin(),imported->scene_instances.end(),
                [&](const auto& item){return item.mesh_index==mesh;});
            const auto vertex=mesh==0?imported->vb:imported->additional_meshes[mesh-1].vb;
            if((used && vertex!=previous_vertices[mesh]) || (!used && vertex.id))
              throw std::runtime_error("detach invalidated shared geometry or retained unused geometry");
          }
        }else{
          const auto counts=device.live_resource_counts();
          if(counts.buffers!=terrain_resources().buffers || counts.textures!=terrain_resources().textures)
            throw std::runtime_error("last detach retained geometry, material uniforms or textures");
          unload_imported();
          std::cout<<"Scene detach passed: shared meshes preserved, unused geometry evicted, terrain retained\n";
          break;
        }
      }else if(options.asset_lifecycle_test) {
        if(rendered_frames==1) {
          const auto before=device.live_resource_counts();
          const auto original=imported->vb;
          struct ReloadProbeFailure {};
          try{
            imported.reload([&]()->std::unique_ptr<ImportedGpu>{
              auto candidate=load_imported();
              throw ReloadProbeFailure{}; // after complete GPU candidate creation
            });
          }catch(const ReloadProbeFailure&){}
          if(imported->vb!=original || device.live_resource_counts()!=before)
            throw std::runtime_error("failed reload changed the asset or leaked resources");
          std::cout<<"Asset lifecycle: failed replacement preserved current asset\n";
        }else if(rendered_frames==2)unload_imported();
        else if(rendered_frames==3) {
          if(imported || device.live_resource_counts()!=terrain_resources())
            throw std::runtime_error("terrain-only frame retained asset resources");
          imported.reload(load_imported);
          std::cout<<"Asset lifecycle: reloaded after terrain-only frame\n";
        }else if(rendered_frames==4) {
          // Also replace a populated asset slot; retire the old SSBO ring and
          // all old GPU handles without invalidating the new candidate.
          imported.reload(load_imported);
          unload_imported();
          std::cout<<"Asset lifecycle passed: render, failed reload, render, unload, terrain, reload, render, replace, unload\n";
          break;
        }
      }else if(options.scene_controls_test) {
        if(rendered_frames==4) {
          std::cout<<"Scene controls passed: pause, step, hold, resume; engine clock independent\n";
          break;
        }
      }else if(options.smoke_test)break;
      if(options.benchmark_frames &&
         rendered_frames>=options.benchmark_frames+kBenchmarkWarmup)break;
    }

    if(options.benchmark_frames) {
      const auto gpu=device.gpu_timing_summary(true);
      if(!gpu.samples)
        throw std::runtime_error("GPU timer returned no usable samples");
      const double n=static_cast<double>(options.benchmark_frames);
      // A stable single-line JSON record makes comparisons reproducible on
      // one machine; numbers are observations, not universal speedup claims.
      std::cout << "BENCHMARK_JSON={\"frames\":"<<options.benchmark_frames
                <<",\"warmup\":"<<kBenchmarkWarmup
                <<",\"culling\":"<<(options.no_culling?"false":"true")
                <<",\"instancing\":"<<(options.no_instancing?"false":"true")
                <<",\"offscreen\":"<<(options.offscreen?"true":"false")
                <<",\"replay_speed\":"<<(replay_clock?std::to_string(replay_clock->speed()):"null")
                <<",\"terrain_visible\":"<<(layer_visibility.terrain?"true":"false")
                <<",\"fire_visible\":"<<(layer_visibility.fire?"true":"false")
                <<",\"terrain_tiled\":"<<(options.terrain_tiles?"true":"false")
                <<",\"terrain_tiles\":"<<(tiled_terrain?tiled_terrain->tile_count():0)
                <<",\"terrain_residency\":"<<(options.terrain_residency?"true":"false")
                <<",\"terrain_retention_frames\":"<<options.terrain_retention_frames
                <<",\"terrain_retained_hidden_per_frame\":"<<static_cast<double>(benchmark_terrain_retained)/n
                <<",\"terrain_pressure_trimmed_per_frame\":"<<static_cast<double>(benchmark_terrain_trimmed)/n
                <<",\"terrain_resident_tiles\":"<<(tiled_terrain?tiled_terrain->resident_tile_count():0)
                <<",\"terrain_resident_geometry_bytes\":"<<(tiled_terrain?tiled_terrain->resident_bytes():0)
                <<",\"terrain_admitted_per_frame\":"<<static_cast<double>(benchmark_terrain_uploads)/n
                <<",\"terrain_evicted_per_frame\":"<<static_cast<double>(benchmark_terrain_evictions)/n
                <<",\"terrain_draws_per_frame\":"<<static_cast<double>(benchmark_terrain_draws)/n
                <<",\"terrain_culled_per_frame\":"<<static_cast<double>(benchmark_terrain_culled)/n
                <<",\"terrain_geometry_bytes\":"<<(tiled_terrain?tiled_terrain->geometry_bytes():vertices.size()+indices.size())
                <<",\"resolution\":["<<1280<<","<<800<<"]"
                <<",\"cpu_update_mean_ms\":"<<benchmark_cpu_update*1000/n
                <<",\"cpu_render_mean_ms\":"<<benchmark_cpu_render*1000/n
                <<",\"gpu_draw_mean_ms\":"<<gpu.mean_ms
                <<",\"gpu_min_ms\":"<<gpu.min_ms
                <<",\"gpu_max_ms\":"<<gpu.max_ms
                <<",\"gpu_samples\":"<<gpu.samples
                <<",\"gpu_skipped\":"<<gpu.skipped
                <<",\"stream_buffers\":"<<(imported && imported->instance_ring?
                     imported->instance_ring->live_buffers():0)
                <<",\"stream_orphans\":"<<(imported && imported->instance_ring?
                     imported->instance_ring->orphan_count():0)
                <<",\"draws_per_frame\":"<<static_cast<double>(benchmark_draws)/n
                <<",\"visible_per_frame\":"<<static_cast<double>(benchmark_visible)/n
                <<",\"culled_per_frame\":"<<static_cast<double>(benchmark_culled)/n
                <<"}\n";
    } else if(options.profile){
      const auto gpu=device.gpu_timing_summary(true);
      std::cout << "GPU profile: "<<gpu.mean_ms<<" ms mean, "
                <<gpu.min_ms<<" ms min, "<<gpu.max_ms<<" ms max ("
                <<gpu.samples<<" samples, "<<gpu.skipped<<" skipped)\n";
    }
    if(!options.screenshot_file.empty()) {
      if(screenshot_pixels.empty())throw std::runtime_error("no frame was rendered for --screenshot");
      write_ppm(options.screenshot_file,screenshot_width,screenshot_height,screenshot_pixels);
      std::cout<<"Screenshot written: "<<options.screenshot_file<<" ("<<screenshot_width<<"x"<<screenshot_height<<")\n";
    }
    if(!options.save_state.empty()) {
      capture_state();
      if(last_state) {
        terraflux::runtime::save_residency_state(options.save_state,*last_state);
        std::cout<<"Residency state saved: "<<options.save_state<<'\n';
      }
    }
    // Explicitly drain streamed SSBO allocations before other resources
    // and before releasing the current OpenGL context.
    imported.unload();
    // Owned GPU resources release before OpenGL context teardown.
    tiled_terrain.reset();
    owned.reset();
    if(options.smoke_test) {
      const auto measured=cpu_profiler.summary();
      std::cout << "CPU frame profile: update " << measured.mean_update_ms
                << " ms, render " << measured.mean_render_ms
                << " ms, draw calls " << measured.mean_draw_calls << '\n';
    }
    if (options.smoke_test)
      std::cout << "OpenGL wildfire replay smoke test rendered " << rendered_frames << " frame(s)\n";
  } catch (const std::exception& e) {
    std::cerr << "TerraFlux viewer error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
