#include <terraflux/graphics/opengl_device.hpp>
#include <terraflux/graphics/composite.hpp>
#include <terraflux/graphics/render_pass.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/terrain_shading.hpp>
#include <terraflux/graphics/water_shading.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/graphics/fire_effects.hpp>
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
  std::optional<std::uint32_t> floor_seed;
  std::optional<double> floor_patch_size;
  std::uint32_t max_resident_terrain_tiles=4096,terrain_retention_frames=0;
  std::uint32_t terrain_tile_quads=32;
  std::optional<terraflux::scene::NodeId> selected_node,activate_node;
  std::uint32_t max_resident_meshes=512,max_scene_instances=1024;
  std::optional<std::uint64_t> resource_budget_bytes,cpu_recipe_budget_bytes;
  std::uint32_t benchmark_frames=0;
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
        "usage: terraflux-terrain-viewer [--request request.json --response response.json] [--obj model.obj | --gltf model.gltf] [--texture texture.ppm] [--scene scene.json (legacy scenes require --obj)] [--replay-speed 1] [--replay-controls-test with replay --smoke-test] [--hide-terrain] [--hide-fire] [--layers-test with replay --smoke-test] [--terrain-contours 50] [--render-cell 5] [--max-render-vertices 4000000] [--terrain-detail 1] [--rock-relief 5] [--snow-line 0.72] [--terrain-shading-test with --smoke-test] [--water-level 35] [--wave-height 0.6] [--wave-length 24] [--wave-speed 3] [--wave-direction 90] [--water-clarity 4] [--water-floor sand|vegetation|mud|stones|mixed|random] [--floor-seed 7] [--floor-patch-size 45] [--water-test with --smoke-test] [--vegetation off|low|medium|high] [--tree-style conifer|broadleaf|mixed|custom] [--tree-obj model.obj] [--tree-spacing 12] [--grass-spacing 2.5] [--grass-distance 300] [--tree-height 16] [--vegetation-seed 0] [--vegetation-test with --smoke-test] [--flame-scale 1] [--smoke-scale 1] [--fire-test with replay --smoke-test] [--terrain-tiles] [--terrain-residency] [--terrain-retain-frames 30] [--terrain-retention-test with --smoke-test --terrain-retain-frames 2] [--max-resident-terrain-tiles 4096] [--terrain-residency-test with --smoke-test --terrain-residency] [--terrain-tile-quads 32] [--terrain-tile-test with --smoke-test --terrain-tiles] [--offscreen] [--offscreen-resize-test with --offscreen --smoke-test] [--profile] [--no-culling] [--no-instancing] [--asset-preview with --gltf] [--benchmark-frames 60] [--screenshot frame.ppm] [--smoke-test] [--asset-lifecycle-test with --smoke-test]");
  }
  if(options.replay_speed_requested && options.request_file.empty())
    throw std::invalid_argument("--replay-speed requires replay input");
  if(options.replay_controls_test && (!options.smoke_test || options.request_file.empty() ||
      !options.scene_file.empty() || !options.obj_file.empty() || !options.gltf_file.empty() ||
      options.layers_test || options.hide_terrain || options.hide_fire || options.replay_speed!=1 ||
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

CameraFrame update_camera(terraflux::graphics::RenderDevice& device,
                   terraflux::graphics::BufferHandle uniform, int width, int height,
                   const CameraGeometry& camera_geometry,
                   double yaw, double pitch, double distance,
                   double cell_size, std::uint32_t fire_width,
                   std::uint32_t fire_height, double now) {
  using terraflux::math::Vec3;
  using terraflux::math::Mat4;
  const Vec3 target = camera_geometry.target;
  const Vec3 eye{
      target.x + distance * std::sin(yaw) * std::cos(pitch),
      target.y + distance * std::sin(pitch),
      target.z + distance * std::cos(yaw) * std::cos(pitch)
  };
  constexpr double kPi = 3.14159265358979323846;
  const Mat4 view = Mat4::lookAtRH(eye, target, {0, 1, 0});
  const Mat4 proj = Mat4::perspectiveRH_NO(kPi / 3.0,
      static_cast<double>(width) / static_cast<double>(height), 1.0,
      std::max(12000.0, distance * 8.0));
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
    const auto layer_title=[&] {
      return std::string(" | T terrain ")+(layer_visibility.terrain?"on":"off")+
             " | F fire "+(playback?(layer_visibility.fire?"on":"off"):"unavailable");
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
    bool fire_emitters_dirty=true;
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
               <<",\"geometry_bytes\":"<<(vegetation?vegetation->geometry_bytes():0)<<"}\n";
    };
    const auto rebuild_vegetation=[&] {
      terrain_style.canopy_on_ground=canopy_for(vegetation_style.trees);
      refresh_terrain_style();
      fire_emitters_dirty=true;
      if(!vegetation_style.trees && !vegetation_style.grass){vegetation.reset();return;}
      auto placement=place_vegetation(mesh,terrain_style,vegetation_style);
      if(vegetation)vegetation->set_placement(std::move(placement),vegetation_style.grass_distance_meters,vegetation_style.grass_spacing_meters);
      else vegetation=std::make_unique<VegetationRenderer>(device,std::move(placement),tree_meshes_for(tree_style),make_grass_mesh(3),
                                                           vegetation_style.grass_distance_meters,vegetation_style.grass_spacing_meters);
    };
    {
      const auto packed=pack_vegetation_style(vegetation_style,{},0);
      vegetation_style_buffer=owned.create_buffer({BufferUsage::Uniform,sizeof(packed)},std::as_bytes(std::span(packed)));
      vegetation_pipeline=owned.create_pipeline({ShaderProgramId::Vegetation,vegetation_shader_layout(),true});
      rebuild_vegetation();
      report_vegetation("initial");
    }
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
    if(fire_field) {
      const auto packed=pack_fire_style(fire_style,{},0);
      fire_style_buffer=owned.create_buffer({BufferUsage::Uniform,sizeof(packed)},std::as_bytes(std::span(packed)));
      fire_pipeline=owned.create_pipeline({ShaderProgramId::Fire,fire_shader_layout(),true,true,false});
      const std::size_t capacity=std::clamp<std::size_t>(grid.cells.size()*static_cast<std::size_t>(fire_style.flames_per_cell+3),4096,200'000);
      fire_effects=std::make_unique<FireEffectsRenderer>(device,capacity);
    }
    const auto rebuild_fire_emitters=[&] {
      if(!fire_effects || !fire_field)return;
      fire_effects->set_emitters(build_fire_emitters(*fire_field,mesh.origin_grid_x,mesh.origin_grid_y,cell_size,ground_sampler,
                                                     terrain_style,vegetation?&vegetation->placement():nullptr,fire_style));
      fire_emitters_dirty=false;
      if(options.smoke_test || options.profile || options.residency_report) {
        const auto& e=fire_effects->emitters();
        std::cout<<"FIRE_EFFECTS_JSON={\"active_cells\":"<<e.active_cells<<",\"burned_cells\":"<<e.burned_cells
                 <<",\"flames\":"<<e.flames.size()<<",\"crown_flames\":"<<e.crown_flames<<",\"smoke\":"<<e.smoke.size()
                 <<",\"fuelless_samples\":"<<e.fuelless_samples<<",\"capacity\":"<<fire_effects->capacity()<<"}\n";
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

    double yaw = 0.45, pitch = options.asset_preview?0.42:0.75;
    double distance=scene_camera.starting_distance*(options.asset_preview?0.75:1.0);
    double previous_time = glfwGetTime();
    terraflux::runtime::FixedStepScheduler scheduler(1.0/60.0,0.25,8);
    terraflux::runtime::ScenePlayback scene_playback;
    bool previous_pause=false,previous_step=false,previous_terrain_key=false,previous_fire_key=false;
    bool previous_vegetation_key=false,previous_tree_key=false,previous_grass_key=false;
    std::vector<std::uint8_t> vegetation_reference,vegetation_frame,fire_reference,fire_frame;
    std::vector<int> fire_top_row;
    std::vector<std::uint8_t> layers_both,layers_ground,layers_fire,shading_reference,water_reference,water_waves;
    std::vector<bool> water_mask;
    std::vector<std::uint32_t> layers_field_snapshot;
    std::size_t layers_frame_snapshot=0;
    glfwSetWindowTitle(window.get(),(std::string("TerraFlux")+layer_title()).c_str());
    if(!options.smoke_test && !options.benchmark_frames)
      std::cout<<"Layer controls: T terrain | F fire (visibility only; replay continues)\n"
               <<"Vegetation controls: V density | B tree meshes | G grass distance\n";
    std::string last_scene_title;
    std::optional<terraflux::scene::Scene> expected_controls_scene;
    if(options.scene_controls_test) {
      expected_controls_scene=imported->scene;
      scene_playback.set_paused(true);
    }
    double engine_time=0.0;
    std::optional<terraflux::replay::ReplayClock> replay_clock;
    if(playback){replay_clock.emplace(playback->frames.size());replay_clock->set_speed(options.replay_speed);}
    std::array<bool,5> previous_speed_keys{};
    std::vector<std::uint8_t> replay_initial_pixels,replay_paused_pixels;
    bool layers_playing_snapshot=false;
    std::size_t playback_frame = 0;
    std::size_t uploaded_frame = static_cast<std::size_t>(-1);
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

    if(playback && options.smoke_test && playback->frames.size()>1 && !options.replay_controls_test) {
      replay_clock->seek(1);replay_clock->set_playing(true);
      playback_frame=replay_clock->frame(); // Exercise non-initial fire state in smoke tests.
    }

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
      const bool detach=glfwGetKey(window.get(),GLFW_KEY_D)==GLFW_PRESS;
      const bool attach=glfwGetKey(window.get(),GLFW_KEY_A)==GLFW_PRESS;
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
           options.water_test || options.terrain_shading_test || options.vegetation_test || options.fire_test)?
          (1.0/60.0):std::max(0.0,now-previous_time);
      const double delta = std::clamp(elapsed, 0.0, 0.1);
      previous_time = now;
      // Clamp large stalls and catch-up work while keeping updates at 60 Hz.
      // Render/input continues once per frame, independent of fixed updates.
      const auto fixed_report=scheduler.advance(elapsed,[&](double dt) {
        engine_time+=dt;
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
              apply_vegetation_level(vegetation_level);rebuild_vegetation();report_vegetation("level");
              std::cout<<"Vegetation: "<<vegetation_level<<'\n';
            }
            if(tree_key && !previous_tree_key && vegetation) {
              tree_style=tree_style=="conifer"?"broadleaf":tree_style=="broadleaf"?"mixed":tree_style=="mixed"?(custom_tree?"custom":"conifer"):"conifer";
              vegetation->set_tree_meshes(tree_meshes_for(tree_style));report_vegetation("tree_style");
              std::cout<<"Tree meshes: "<<tree_style<<'\n';
            }
            if(grass_key && !previous_grass_key) {
              const double d=vegetation_style.grass_distance_meters;
              vegetation_style.grass=true;
              vegetation_style.grass_distance_meters=d<=0?150:d<=150?300:d<=300?600:0;
              if(vegetation_style.grass_distance_meters<=0)vegetation_style.grass=false;
              rebuild_vegetation();report_vegetation("grass");
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
      if(layer_changed)glfwSetWindowTitle(window.get(),(std::string("TerraFlux")+layer_title()).c_str());
      if(options.terrain_shading_test) {
        // Frames 0 and 2: base materials without contours or detail (CPU reference
        // parity). Frame 1: eight contour levels. Frame 3: full procedural detail.
        auto staged=terrain_style;
        staged.contour_interval_meters=0;staged.detail_strength=0;
        const double range=terrain_style.max_elevation_meters-terrain_style.min_elevation_meters;
        if(!(range>0))throw std::runtime_error("terrain shading test requires terrain with relief");
        if(rendered_frames==1)staged.contour_interval_meters=range/8.0;
        if(rendered_frames==3)staged.detail_strength=1.0;
        const auto packed=pack_terrain_style(staged);
        device.update_buffer(style_buffer,0,std::as_bytes(std::span(packed)));
      }
      const bool space = glfwGetKey(window.get(), GLFW_KEY_SPACE) == GLFW_PRESS;
      const bool home = glfwGetKey(window.get(), GLFW_KEY_HOME) == GLFW_PRESS;
      const bool back = glfwGetKey(window.get(), GLFW_KEY_COMMA) == GLFW_PRESS;
      const bool forward = glfwGetKey(window.get(), GLFW_KEY_PERIOD) == GLFW_PRESS;
      if(replay_clock) {
        if(space && !previous_space)replay_clock->toggle();
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
        if(frame_changed) {
          terraflux::replay::apply_frame(playback->frames[playback_frame],*fire_field);
          device.update_buffer(*fire_buffer,0,std::as_bytes(std::span(fire_field->states)));
          fire_emitters_dirty=true;
          uploaded_frame=playback_frame;
        }
        if(frame_changed || speed_changed || options.smoke_test || (space && !previous_space) ||
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

      {
        // W/A/S/D pan the orbit target over the ground (faster when zoomed out);
        // the target height follows the terrain so the camera can walk among
        // the vegetation. Home/End are unaffected; the framing distance is kept.
        const double step=std::max(5.0,distance*0.6)*delta;
        const terraflux::math::Vec3 forward{-std::sin(yaw),0,-std::cos(yaw)},right{std::cos(yaw),0,-std::sin(yaw)};
        terraflux::math::Vec3 pan{};
        if(glfwGetKey(window.get(),GLFW_KEY_W)==GLFW_PRESS)pan=pan+forward*step;
        if(glfwGetKey(window.get(),GLFW_KEY_S)==GLFW_PRESS)pan=pan-forward*step;
        if(glfwGetKey(window.get(),GLFW_KEY_D)==GLFW_PRESS && !(imported && imported->uses_scene_file))pan=pan+right*step;
        if(glfwGetKey(window.get(),GLFW_KEY_A)==GLFW_PRESS && !(imported && imported->uses_scene_file))pan=pan-right*step;
        if(pan.length()>0 && !options.smoke_test && !options.benchmark_frames) {
          auto target=scene_camera.target+pan;
          target.x=std::clamp(target.x,ground_sampler.lower_bound().x,ground_sampler.upper_bound().x);
          target.z=std::clamp(target.z,ground_sampler.lower_bound().z,ground_sampler.upper_bound().z);
          if(const auto ground=ground_sampler.sample(target.x,target.z))target.y=std::max(ground->position.y,water_style?water_style->level_meters:ground->position.y)+1.5;
          scene_camera.target=target;
        }
      }
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
      if(options.terrain_tile_test && rendered_frames>0) {
        distance=scene_camera.starting_distance*0.2;
        yaw=rendered_frames==1?0.45:1.95;pitch=0.55;
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
      const auto camera_frame=update_camera(device, camera, scene_width, scene_height, scene_camera,
                    yaw, pitch, distance,
                    cell_size, fire_field ? fire_field->width : 1,
                    fire_field ? fire_field->height : 1,
                    (options.layers_test || options.replay_controls_test || options.terrain_shading_test || options.water_test ||
                     options.vegetation_test || options.fire_test)?0.0:engine_time);
      const auto& camera_vp=camera_frame.view_projection;
      const auto terrain_frustum=Frustum::from_view_projection(camera_vp);
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
                          {{0,camera},{kTerrainStyleSlot,style_buffer}}, {}};
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
        if(draw_surface)terrain_stats=tiled_terrain->draw(pipeline,camera,fire_buffer,terrain_frustum,!options.no_culling,layer_buffer,style_buffer);
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
      // Water belongs to the surface layer: it follows the terrain in the same
      // pass so asset diagnostics still compare against the complete ground.
      std::vector<std::uint8_t> water_before;
      if(water && layer_visibility.terrain) {
        auto staged=*water_style;
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
                             {{0,camera},{kWaterStyleSlot,water->style}},{}});
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
        const auto packed=pack_vegetation_style(vegetation_style,camera_frame.eye,vegetation_time,vegetation_fire);
        device.update_buffer(*vegetation_style_buffer,0,std::as_bytes(std::span(packed)));
        vegetation_stats=vegetation->draw(*vegetation_pipeline,camera,*vegetation_style_buffer,vegetation_fire_buffer,terrain_frustum,
                                          camera_frame.eye,vegetation_style,!options.no_culling);
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
      const terraflux::math::Vec3 camera_eye{
        scene_camera.target.x+distance*std::sin(yaw)*std::cos(pitch),
        scene_camera.target.y+distance*std::sin(pitch),
        scene_camera.target.z+distance*std::cos(yaw)*std::cos(pitch)};
      if(imported && imported->uses_pbr) {
        // Keep view vector in material UBO, not the terrain camera UBO,
        // whose 80-byte fire-grid layout is shared by existing shaders.
        const auto& t=scene_camera.target;
        const terraflux::math::Vec3 eye{
          t.x+distance*std::sin(yaw)*std::cos(pitch),
          t.y+distance*std::sin(pitch),
          t.z+distance*std::cos(yaw)*std::cos(pitch)};
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
      if (imported) {
        const auto& frustum=terrain_frustum;
        // Asset queue ordering is independent of opaque terrain tile submission.
        const auto enqueue=[&](BufferHandle vertex,BufferHandle index,
                               const std::vector<ImportedGpu::Section>& sections,
                               BufferHandle object_uniform,const terraflux::math::Mat4& model,
                               BoundingSphere bounds,terraflux::math::Vec3 tint) {
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
        const double fire_time=options.fire_test?(rendered_frames==2?0.6:0.0):(options.smoke_test?0.0:engine_time);
        const auto packed=pack_fire_style(fire_style,camera_frame.eye,fire_time);
        device.update_buffer(*fire_style_buffer,0,std::as_bytes(std::span(packed)));
        fire_stats=fire_effects->draw(*fire_pipeline,camera,*fire_style_buffer,terrain_frustum,camera_frame.eye,
                                      fire_style.draw_distance_meters,!options.no_culling);
        frame_draws+=static_cast<std::uint32_t>(fire_stats.draws);
        if(options.smoke_test || (options.profile && profile_frames%120==0))
          std::cout<<"FIRE_DRAW_JSON={\"flames\":"<<fire_stats.flames<<",\"smoke\":"<<fire_stats.smoke<<",\"draws\":"<<fire_stats.draws<<"}\n";
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
      if (options.smoke_test && playback && layer_visibility.fire && !playback->frames[playback_frame].active.empty()) {
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
      }else if(options.vegetation_test) {
        if(rendered_frames==5) {
          std::cout<<"Vegetation passed: trees and grass with height and varied color appear, meshes swap at run time, wind sways them, disabling restores the reference\n";
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
