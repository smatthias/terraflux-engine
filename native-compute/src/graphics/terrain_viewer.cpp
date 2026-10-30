#include <terraflux/graphics/opengl_device.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
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
#include <terraflux/grid.hpp>
#include <terraflux/math.hpp>
#include <terraflux/replay.hpp>
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
#include <iostream>
#include <memory>
#include <optional>
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
  bool asset_lifecycle_test=false,scene_controls_test=false;
  std::uint32_t benchmark_frames=0;
  std::string request_file, response_file, obj_file, gltf_file, texture_file, scene_file;
};

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--smoke-test") options.smoke_test = true;
    else if (arg == "--scene-controls-test") options.scene_controls_test=true;
    else if (arg == "--asset-lifecycle-test") options.asset_lifecycle_test=true;
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
    else if (arg == "--scene" && i + 1 < argc) options.scene_file = argv[++i];
    else throw std::invalid_argument(
        "usage: terraflux-terrain-viewer [--request request.json --response response.json] [--obj model.obj | --gltf model.gltf] [--texture texture.ppm] [--scene scene.json (legacy scenes require --obj)] [--profile] [--no-culling] [--no-instancing] [--asset-preview with --gltf] [--benchmark-frames 60] [--smoke-test] [--asset-lifecycle-test with --smoke-test]");
  }
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
  if(options.no_culling && !options.benchmark_frames && !options.profile)
    throw std::invalid_argument("--no-culling requires --profile or --benchmark-frames");
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
  return options;
}

terraflux::compute::Grid make_demo_terrain() {
  constexpr int kSide = 72;
  std::vector<terraflux::compute::GridCell> cells;
  cells.reserve(kSide * kSide);
  for (int y = 0; y < kSide; ++y) {
    for (int x = 0; x < kSide; ++x) {
      terraflux::compute::GridCell cell{};
      cell.x = x; cell.y = y;
      cell.fuel_load = 1.0; cell.fuel_moisture = 0.15; cell.burn_susceptibility = 0.8;
      const double hill = 55.0 * std::sin(x * 0.065) * std::cos(y * 0.075);
      const double ridge = 25.0 * std::exp(-std::pow((x-48.0)/16,2) - std::pow((y-26.0)/13,2));
      cell.elevation_meters = 70.0 + hill + ridge;
      cells.push_back(cell);
    }
  }
  return terraflux::compute::Grid(std::move(cells));
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
  if (mesh.vertices.empty()) throw std::runtime_error("terrain mesh contains no vertices");
  auto lower = mesh.vertices.front().position;
  auto upper = lower;
  for (const auto& vertex : mesh.vertices) {
    const auto& p = vertex.position;
    lower.x = std::min(lower.x, p.x); lower.y = std::min(lower.y, p.y); lower.z = std::min(lower.z, p.z);
    upper.x = std::max(upper.x, p.x); upper.y = std::max(upper.y, p.y); upper.z = std::max(upper.z, p.z);
  }
  return {{(lower.x + upper.x) * 0.5, (lower.y + upper.y) * 0.5,
           (lower.z + upper.z) * 0.5},
          std::max(200.0, 2.1 * std::max(upper.x - lower.x, upper.z - lower.z))};
}

terraflux::math::Mat4 update_camera(terraflux::graphics::RenderDevice& device,
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
  return vp;
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
    auto& grid = playback ? playback->grid : *demo;
    const double cell_size = playback ? playback->cell_size_meters : 30.0;

    // This is visualization-only preprocessing. Native compute outputs and
    // regression semantics are unchanged. Unmeasured rivers remain holes.
    terraflux::compute::fill_elevation_voids(grid);
    for (auto& cell : grid.cells)
      if (!cell.elevation_meters && cell.fuel_load > 0) cell.elevation_meters = 0.0;
    const auto mesh = terraflux::terrain::build_terrain_mesh(grid, cell_size);
    auto upload = terraflux::graphics::prepare_terrain_upload(mesh);
    std::optional<terraflux::replay::FireField> fire_field;
    if (playback) fire_field.emplace(terraflux::replay::make_fire_field(grid));
    if (upload.indices.empty()) throw std::runtime_error("scenario terrain has no drawable triangles");
    const auto scene_camera = camera_geometry(mesh);

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
    RenderResourceOwner owned(device);
    const auto vertices = bytes_of(upload.vertices);
    const auto indices = bytes_of(upload.indices);
    const auto vb = owned.create_buffer({BufferUsage::Vertex, vertices.size()}, vertices);
    const auto ib = owned.create_buffer({BufferUsage::Index, indices.size()}, indices);
    std::optional<BufferHandle> fire_buffer;
    if (fire_field) {
      const auto fire_bytes = std::as_bytes(std::span(fire_field->states));
      fire_buffer = owned.create_buffer({BufferUsage::Storage, fire_bytes.size()}, fire_bytes);
    }
    std::array<float, 20> initial_camera{};
    const auto camera_bytes = std::as_bytes(std::span(initial_camera));
    const auto camera = owned.create_buffer({BufferUsage::Uniform, camera_bytes.size()}, camera_bytes);
    const auto pipeline = owned.create_pipeline({
        playback ? wildfire_shader_layout() : terrain_shader_layout(), true});

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
      struct AdditionalMesh {
        BufferHandle vb, ib;
        std::vector<Section> sections;
        std::uint32_t indices=0;
      };
      std::vector<AdditionalMesh> additional_meshes;
      PipelineHandle pipeline,instanced_pipeline,transparent_pipeline;
      std::unique_ptr<OpaqueRenderQueue::InstanceBufferRing> instance_ring;
      std::uint32_t indices = 0;
      terraflux::scene::Scene scene;
      terraflux::scene::NodeId left_pivot = 0, right_pivot = 0;
      terraflux::scene::NodeId left_mesh = 0, right_mesh = 0;
      double separation = 1.0;
      terraflux::math::Vec3 local_center{};
    };
    const auto terrain_resources=device.live_resource_counts();
    const auto load_imported=[&]()->std::unique_ptr<ImportedGpu> {
      std::optional<terraflux::scene::Scene> loaded_scene;
      terraflux::assets::SceneModelAssets scene_assets;
      if(!options.scene_file.empty()) {
        loaded_scene=terraflux::scene::deserialize_scene(read_file(options.scene_file));
        if(loaded_scene->mesh_assets().empty()) {
          if(options.obj_file.empty())throw std::invalid_argument("legacy --scene requires --obj");
        }else{
          if(!options.obj_file.empty())
            throw std::invalid_argument("asset scenes declare their models; omit --obj");
          scene_assets=terraflux::assets::load_scene_assets(*loaded_scene,options.scene_file);
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
        const auto geometry = prepare_textured_asset_upload(object); // local + UV
        const auto vertex_bytes = bytes_of(geometry.vertices);
        const auto index_bytes = bytes_of(geometry.indices);
        auto candidate=std::make_unique<ImportedGpu>(device);
        auto& obj=*candidate;
        obj.resources=std::make_unique<RenderResourceOwner>(device);
        auto& asset_owned=*obj.resources;
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
        const auto cached_texture=[&](const std::filesystem::path& source,
                                       terraflux::TextureSampling sampling) {
          const auto key=terraflux::assets::texture_cache_key(source,sampling,use_pbr);
          if(auto existing=obj.textures.find(key))return existing;
          const auto image=terraflux::assets::resolve_texture_image(source,use_pbr,gltf_images);
          const auto bytes=std::as_bytes(std::span(image.pixels));
          return obj.textures.acquire(key,{image.width,image.height,sampling},bytes);
        };
        const auto build_sections=[&](const terraflux::assets::Mesh& mesh) {
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
            if(!options.texture_file.empty())texture_file=options.texture_file;
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
              pbr_uniform=asset_owned.create_buffer({BufferUsage::Uniform,bytes.size()},bytes);
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
        obj.sections=build_sections(object);
        obj.vb=asset_owned.create_buffer({BufferUsage::Vertex,vertex_bytes.size()},vertex_bytes);
        obj.ib=asset_owned.create_buffer({BufferUsage::Index,index_bytes.size()},index_bytes);
        for(const auto& mesh:extra_meshes) {
          const auto upload=prepare_textured_asset_upload(mesh);
          const auto bytes=bytes_of(upload.vertices);
          const auto indices_bytes=bytes_of(upload.indices);
          ImportedGpu::AdditionalMesh extra{};
          extra.sections=build_sections(mesh);
          extra.indices=static_cast<std::uint32_t>(upload.indices.size());
          extra.vb=asset_owned.create_buffer({BufferUsage::Vertex,bytes.size()},bytes);
          extra.ib=asset_owned.create_buffer({BufferUsage::Index,indices_bytes.size()},indices_bytes);
          obj.additional_meshes.push_back(std::move(extra));
        }
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
            instance.uniform=asset_owned.create_buffer({BufferUsage::Uniform,bytes.size()},bytes);
          }
        }else{
          obj.object_uniform_left = asset_owned.create_buffer(
              {BufferUsage::Uniform,init_bytes.size()},init_bytes);
          obj.object_uniform_right = asset_owned.create_buffer(
              {BufferUsage::Uniform,init_bytes.size()},init_bytes);
        }
        obj.pipeline = asset_owned.create_pipeline({
            use_pbr?pbr_textured_object_shader_layout(false):
                     textured_object_shader_layout(),true});
        if(use_pbr)
          obj.transparent_pipeline=asset_owned.create_pipeline({
              pbr_textured_object_shader_layout(false),true,true,false});
        if(!options.no_instancing){
          obj.instanced_pipeline=asset_owned.create_pipeline(
              {use_pbr?pbr_textured_object_shader_layout(true):
                        instanced_textured_object_shader_layout(),true});
          obj.instance_ring=std::make_unique<OpaqueRenderQueue::InstanceBufferRing>(device);
        }
        std::size_t triangle_count=obj.indices/3;
        std::size_t sections_count=obj.sections.size();
        for(const auto& mesh:obj.additional_meshes){
          triangle_count+=mesh.indices/3;
          sections_count+=mesh.sections.size();
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
    const auto unload_imported=[&] {
      imported.unload();
      if(device.live_resource_counts()!=terrain_resources)
        throw std::runtime_error("asset unload did not restore terrain resource baseline");
      std::cout<<"Asset unloaded; terrain resources preserved\n";
    };
    if(has_asset && !options.smoke_test && !options.benchmark_frames)
      std::cout<<"Asset controls: U unload | R reload | P pause scene | N single tick (failed reload preserves current asset)\n";

    double yaw = 0.45, pitch = options.asset_preview?0.42:0.75;
    double distance=scene_camera.starting_distance*(options.asset_preview?0.75:1.0);
    double previous_time = glfwGetTime();
    terraflux::runtime::FixedStepScheduler scheduler(1.0/60.0,0.25,8);
    terraflux::runtime::ScenePlayback scene_playback;
    bool previous_pause=false,previous_step=false;
    std::string last_scene_title;
    std::optional<terraflux::scene::Scene> expected_controls_scene;
    if(options.scene_controls_test) {
      expected_controls_scene=imported->scene;
      scene_playback.set_paused(true);
    }
    double engine_time=0.0, playback_accumulator=0.0;
    std::size_t playback_frame = 0;
    std::size_t uploaded_frame = static_cast<std::size_t>(-1);
    bool playing = playback.has_value();
    bool previous_unload=false,previous_reload=false;
    bool previous_space = false, previous_home = false, previous_back = false, previous_forward = false;
    unsigned rendered_frames = 0;
    std::vector<std::uint8_t> lifecycle_terrain;
    std::uint64_t profile_frames = 0;
    terraflux::runtime::FrameProfiler cpu_profiler(120);
    constexpr unsigned kBenchmarkWarmup=8;
    double benchmark_cpu_update=0,benchmark_cpu_render=0;
    std::uint64_t benchmark_draws=0,benchmark_visible=0,benchmark_culled=0;
    if(options.profile && !options.benchmark_frames)device.enable_gpu_timing();

    if (playback && options.smoke_test && playback->frames.size() > 1)
      playback_frame = 1; // exercise non-initial fire state in GPU smoke test

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
          }
          catch(const std::exception& error){
            std::cerr<<"Asset reload failed; current asset preserved: "<<error.what()<<'\n';
          }
          previous_time=glfwGetTime(); // disk/shader loading isn't simulated time
        }
      }
      previous_unload=unload;previous_reload=reload;
      const double now = glfwGetTime();
      const double profile_update_start = glfwGetTime();
      const double elapsed = (options.benchmark_frames || options.scene_controls_test)?
                             (1.0/60.0):std::max(0.0,now-previous_time);
      const double delta = std::clamp(elapsed, 0.0, 0.1);
      previous_time = now;
      // Clamp large stalls and catch-up work while keeping updates at 60 Hz.
      // Render/input continues once per frame, independent of fixed updates.
      const auto fixed_report=scheduler.advance(elapsed,[&](double dt) {
        engine_time+=dt;
        if(playback && playing)playback_accumulator+=dt;
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

      const bool space = glfwGetKey(window.get(), GLFW_KEY_SPACE) == GLFW_PRESS;
      const bool home = glfwGetKey(window.get(), GLFW_KEY_HOME) == GLFW_PRESS;
      const bool back = glfwGetKey(window.get(), GLFW_KEY_COMMA) == GLFW_PRESS;
      const bool forward = glfwGetKey(window.get(), GLFW_KEY_PERIOD) == GLFW_PRESS;
      if (playback) {
        if (space && !previous_space) {
          if (!playing && playback_frame + 1 == playback->frames.size()) playback_frame = 0;
          playing = !playing;
          playback_accumulator = 0.0;
        }
        if (home && !previous_home) {
          playback_frame = 0; playing = false; playback_accumulator=0;
        }
        if (back && !previous_back) {
          playback_frame = playback_frame == 0 ? 0 : playback_frame - 1;
          playing = false; playback_accumulator=0;
        }
        if (forward && !previous_forward) {
          playback_frame = std::min(playback_frame + 1, playback->frames.size() - 1);
          playing = false; playback_accumulator=0;
        }
        while (playing && playback_accumulator >= 0.35) {
          playback_accumulator-=0.35;
          if (playback_frame + 1 < playback->frames.size()) ++playback_frame;
          else { playing=false; playback_accumulator=0; }
        }
        if (playback_frame != uploaded_frame) {
          terraflux::replay::apply_frame(playback->frames[playback_frame], *fire_field);
          device.update_buffer(*fire_buffer, 0, std::as_bytes(std::span(fire_field->states)));
          uploaded_frame = playback_frame;
          const auto& current = playback->frames[playback_frame];
          std::string title = "TerraFlux - " + playback->grid_key +
              " | step " + std::to_string(current.step + 1) + "/" +
              std::to_string(playback->frames.size()) +
              " | active " + std::to_string(current.active.size()) +
              " | burned " + std::to_string(current.burned.size()) +
              " | Space pause | Home reset | , . step | arrows orbit | +/- zoom";
          glfwSetWindowTitle(window.get(), title.c_str());
        }
      }
      previous_space = space; previous_home = home;
      previous_back = back; previous_forward = forward;

      if(imported && !imported->is_gltf && !options.smoke_test && !options.benchmark_frames) {
        std::string title=std::string("TerraFlux | Scene ")+
            (scene_playback.paused()?"paused":"playing")+" | P pause/resume | N tick | U unload | R reload";
        if(playback)title+=" | Fire "+std::to_string(playback_frame+1)+"/"+
            std::to_string(playback->frames.size())+" | Space pause fire";
        if(title!=last_scene_title) {
          glfwSetWindowTitle(window.get(),title.c_str());
          last_scene_title=std::move(title);
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
      distance = std::clamp(distance, scene_camera.starting_distance * 0.2,
                           scene_camera.starting_distance * 4.0);

      int width = 0, height = 0;
      glfwGetFramebufferSize(window.get(), &width, &height);
      if (width <= 0 || height <= 0) {
        if (options.smoke_test) throw std::runtime_error("no framebuffer in smoke test");
        continue;
      }
      if(options.benchmark_frames && rendered_frames==kBenchmarkWarmup)
        device.enable_gpu_timing();
      const bool record_gpu=options.profile || (options.benchmark_frames &&
                                rendered_frames>=kBenchmarkWarmup);
      const double profile_render_start=glfwGetTime();
      std::uint32_t frame_draws=0;
      const auto camera_vp=update_camera(device, camera, width, height, scene_camera,
                    yaw, pitch, distance,
                    cell_size, fire_field ? fire_field->width : 1,
                    fire_field ? fire_field->height : 1, engine_time);
      if(record_gpu)device.begin_gpu_timing();
      device.clear_frame(width, height);
      DrawIndexed command{pipeline, vb, ib, 0,
          static_cast<std::uint32_t>(upload.indices.size()), {{0, camera}}, {}};
      if (fire_buffer) command.storage_buffers.push_back({1, *fire_buffer});
      device.draw_indexed(command);
      ++frame_draws;
      const auto terrain_snapshot=(options.smoke_test &&
          (options.asset_lifecycle_test || (imported && imported->uses_pbr)))?
          device.capture_rgba(width,height):std::vector<std::uint8_t>{};
      if(options.asset_lifecycle_test && !playback) {
        if(lifecycle_terrain.empty())lifecycle_terrain=terrain_snapshot;
        else if(lifecycle_terrain!=terrain_snapshot)
          throw std::runtime_error("terrain framebuffer changed during asset lifecycle");
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
        const auto frustum=Frustum::from_view_projection(camera_vp);
        // Opaque assets only. Terrain/fire passes retain original ordering.
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
      if(options.smoke_test || (options.profile && profile_frames%120==0)) {
        std::cout << "Visibility: " << visible_instances << " visible, "
                  << culled_instances << " culled, "
                  << queue_stats.submitted << " opaque draws, "
                  << queue_stats.instances << " object instances, "
                  << queue_stats.state_groups << " state groups, "
                  << transparent_draws << " transparent draws\n";
      }
      if(record_gpu)device.end_gpu_timing();
      if(options.smoke_test && imported) {
        if(imported->uses_pbr) {
          const auto frame=device.capture_rgba(width,height);
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
          const auto visible_asset=device.count_asset_pixels(width,height);
          if(visible_asset==0)
            throw std::runtime_error("smoke test found no visible OBJ pixels");
          std::cout<<"Visible OBJ pixels: "<<visible_asset<<'\n';
        }
      }
      if (options.smoke_test && playback && !playback->frames[playback_frame].active.empty()) {
        const auto visible_fire = device.count_fire_pixels(width, height);
        if (visible_fire == 0)
          throw std::runtime_error("smoke test found no visible fire pixels");
        std::cout << "Visible fire pixels: " << visible_fire << '\n';
      }
      const double profile_render_end=glfwGetTime();
      cpu_profiler.record({profile_render_start-profile_update_start,
                           profile_render_end-profile_render_start,frame_draws});
      if(options.benchmark_frames && rendered_frames>=kBenchmarkWarmup) {
        benchmark_cpu_update+=profile_render_start-profile_update_start;
        benchmark_cpu_render+=profile_render_end-profile_render_start;
        benchmark_draws+=frame_draws;
        benchmark_visible+=visible_instances;
        benchmark_culled+=culled_instances;
      }
      ++profile_frames;
      if(options.profile && profile_frames%120==0) {
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
      glfwSwapBuffers(window.get());
      ++rendered_frames;
      if(options.asset_lifecycle_test) {
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
          if(imported || device.live_resource_counts()!=terrain_resources)
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
    // Explicitly drain streamed SSBO allocations before other resources
    // and before releasing the current OpenGL context.
    imported.unload();
    // Owned GPU resources release before OpenGL context teardown.
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
