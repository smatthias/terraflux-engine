#include <terraflux/graphics/opengl_device.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/mesh_upload.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/visibility.hpp>
#include <terraflux/assets/obj.hpp>
#include <terraflux/assets/gltf.hpp>
#include <terraflux/assets/material.hpp>
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
  bool smoke_test = false, profile = false;
  std::string request_file, response_file, obj_file, gltf_file, texture_file, scene_file;
};

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--smoke-test") options.smoke_test = true;
    else if (arg == "--profile") options.profile = true;
    else if (arg == "--request" && i + 1 < argc) options.request_file = argv[++i];
    else if (arg == "--response" && i + 1 < argc) options.response_file = argv[++i];
    else if (arg == "--obj" && i + 1 < argc) options.obj_file = argv[++i];
    else if (arg == "--gltf" && i + 1 < argc) options.gltf_file = argv[++i];
    else if (arg == "--texture" && i + 1 < argc) options.texture_file = argv[++i];
    else if (arg == "--scene" && i + 1 < argc) options.scene_file = argv[++i];
    else throw std::invalid_argument(
        "usage: terraflux-terrain-viewer [--request request.json --response response.json] [--obj model.obj | --gltf model.gltf] [--texture texture.ppm] [--scene scene.json with --obj] [--profile] [--smoke-test]");
  }
  if (options.request_file.empty() != options.response_file.empty())
    throw std::invalid_argument("--request and --response must be provided together");
  if (!options.obj_file.empty() && !options.gltf_file.empty())
    throw std::invalid_argument("--obj and --gltf are mutually exclusive");
  if (!options.texture_file.empty() && options.obj_file.empty() && options.gltf_file.empty())
    throw std::invalid_argument("--texture requires --obj or --gltf");
  if (!options.scene_file.empty() && options.obj_file.empty())
    throw std::invalid_argument("--scene requires --obj; glTF already has authored scene nodes");
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
    glfwWindowHint(GLFW_VISIBLE, options.smoke_test ? GLFW_FALSE : GLFW_TRUE);
    Window window(glfwCreateWindow(1280, 800, "TerraFlux 3D Wildfire Viewer", nullptr, nullptr));
    if (!window) throw std::runtime_error("OpenGL 4.3 context could not be created");
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(options.smoke_test ? 0 : 1);

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
      };
      std::vector<SceneInstance> scene_instances;
      terraflux::math::Mat4 scene_anchor=terraflux::math::Mat4::identity();
      bool is_gltf=false, uses_scene_file=false;
      BoundingSphere primary_bounds{};
      std::vector<BoundingSphere> additional_bounds;
      struct Section {
        std::uint32_t first=0,count=0;
        TextureHandle albedo{};
        terraflux::math::Vec3 base_color{1,1,1};
      };
      std::map<std::string,TextureHandle> textures;
      std::vector<Section> sections;
      struct AdditionalMesh {
        BufferHandle vb, ib;
        std::vector<Section> sections;
        std::uint32_t indices=0;
      };
      std::vector<AdditionalMesh> additional_meshes;
      PipelineHandle pipeline;
      std::uint32_t indices = 0;
      terraflux::scene::Scene scene;
      terraflux::scene::NodeId left_pivot = 0, right_pivot = 0;
      terraflux::scene::NodeId left_mesh = 0, right_mesh = 0;
      double separation = 1.0;
      terraflux::math::Vec3 local_center{};
    };
    std::optional<ImportedGpu> imported;
    if (!options.obj_file.empty() || !options.gltf_file.empty()) {
      const bool use_gltf=!options.gltf_file.empty();
      const auto source_file=use_gltf?options.gltf_file:options.obj_file;
      const auto source_folder=std::filesystem::path(source_file).parent_path();
      struct LocatedMaterial {
        terraflux::assets::Material material;
        std::filesystem::path folder;
      };
      std::map<std::string,LocatedMaterial> material_files;
      terraflux::assets::Mesh object;
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
        auto asset=terraflux::assets::import_gltf(read_file(source_file),resolver);
        if(asset.meshes.empty())throw std::invalid_argument("glTF contains no meshes");
        object=std::move(asset.meshes.front());
        for(std::size_t i=1;i<asset.meshes.size();++i)
          extra_meshes.push_back(std::move(asset.meshes[i]));
        for(const auto& instance:asset.instances)
          node_worlds.emplace_back(instance.mesh_index,instance.world);
        for(const auto& [name,material]:asset.materials)
          material_files.emplace(name,LocatedMaterial{material,source_folder});
      } else {
        std::ifstream model_file(options.obj_file);
        if (!model_file) throw std::runtime_error("cannot open OBJ "+options.obj_file);
        object=terraflux::assets::import_obj(model_file);
        for(const auto& reference:object.material_libraries) {
          const auto path=asset_path(source_folder,reference);
          std::ifstream file(path);
          if(!file) throw std::runtime_error("cannot open material library: "+path.string());
          const auto parsed=terraflux::assets::parse_mtl(file);
          for(const auto& [name,material]:parsed)
            if(!material_files.emplace(name,LocatedMaterial{material,path.parent_path()}).second)
              throw std::invalid_argument("duplicate material in libraries: "+name);
        }
      }
      if(object.vertices.empty())throw std::invalid_argument("imported mesh has no vertices");
      const auto get_mesh=[&](std::uint32_t id)->const terraflux::assets::Mesh& {
        return id==0?object:extra_meshes.at(id-1);
      };
      auto min_pos=object.vertices.front().position;
      auto max_pos=min_pos;
      if(use_gltf) {
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
        if(!initialized)throw std::invalid_argument("glTF scene contains no mesh instances");
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
      const double scale = (scene_camera.starting_distance * (use_gltf ? 0.22 : 0.095)) / extent;
      const auto geometry = prepare_textured_asset_upload(object); // local + UV
      const auto vertex_bytes = bytes_of(geometry.vertices);
      const auto index_bytes = bytes_of(geometry.indices);
      ImportedGpu obj{};
      obj.is_gltf=use_gltf;
      obj.primary_bounds=mesh_bounds(object);
      for(const auto& mesh:extra_meshes)
        obj.additional_bounds.push_back(mesh_bounds(mesh));
      obj.indices = static_cast<std::uint32_t>(geometry.indices.size());
      obj.separation = extent * 0.95;
      obj.local_center = {(min_pos.x + max_pos.x)*0.5, min_pos.y,
                          (min_pos.z + max_pos.z)*0.5};
      if(!use_gltf && !options.scene_file.empty()) {
        obj.uses_scene_file=true;
        obj.scene=terraflux::scene::deserialize_scene(read_file(options.scene_file));
        obj.scene_anchor=terraflux::math::Mat4::translation(scene_camera.target) *
            terraflux::math::Mat4::scaling({scale,scale,scale}) *
            terraflux::math::Mat4::translation(obj.local_center*-1.0);
        for(std::size_t id=1;id<obj.scene.size();++id) {
          const auto& node=obj.scene.get(static_cast<terraflux::scene::NodeId>(id));
          if(node.mesh_instance) {
            // Hold node identifiers until the corresponding UBOs are created.
            obj.scene_instances.push_back({{},static_cast<terraflux::scene::NodeId>(id)});
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
      const auto cached_texture=[&](const std::filesystem::path& source) {
        const std::string key=source.empty()?
             (use_gltf?"__white__":"__checkerboard__"):
                              source.lexically_normal().generic_string();
        if(const auto it=obj.textures.find(key);it!=obj.textures.end())
          return it->second;
        terraflux::assets::ImageRgba8 image;
        if(source.empty())
          image=use_gltf?terraflux::assets::ImageRgba8{1,1,{255,255,255,255}}:
                         terraflux::assets::checkerboard_image();
        else {
          std::ifstream file(source);
          if(!file) throw std::runtime_error("cannot open material texture: "+source.string());
          image=terraflux::assets::read_ppm_p3(file);
        }
        const auto bytes=std::as_bytes(std::span(image.pixels));
        const auto id=owned.create_texture2d({image.width,image.height},bytes);
        obj.textures.emplace(key,id);
        return id;
      };
      const auto build_sections=[&](const terraflux::assets::Mesh& mesh) {
        std::vector<ImportedGpu::Section> result;
        result.reserve(mesh.sections.size());
        for(const auto& section:mesh.sections) {
          terraflux::math::Vec3 color{1,1,1};
          std::filesystem::path texture_file;
          if(!section.material_name.empty()) {
            const auto it=material_files.find(section.material_name);
            if(it==material_files.end())
              throw std::invalid_argument("undefined imported material: "+section.material_name);
            color=it->second.material.base_color;
            if(!it->second.material.albedo_map.empty())
              texture_file=asset_path(it->second.folder,it->second.material.albedo_map);
          }
          if(!options.texture_file.empty())texture_file=options.texture_file;
          const auto handle=cached_texture(texture_file);
          result.push_back({section.first_index,section.index_count,handle,color});
        }
        if(result.empty())throw std::invalid_argument("mesh has no drawable material sections");
        return result;
      };
      obj.sections=build_sections(object);
      obj.vb=owned.create_buffer({BufferUsage::Vertex,vertex_bytes.size()},vertex_bytes);
      obj.ib=owned.create_buffer({BufferUsage::Index,index_bytes.size()},index_bytes);
      for(const auto& mesh:extra_meshes) {
        const auto upload=prepare_textured_asset_upload(mesh);
        const auto bytes=bytes_of(upload.vertices);
        const auto indices_bytes=bytes_of(upload.indices);
        ImportedGpu::AdditionalMesh extra{};
        extra.sections=build_sections(mesh);
        extra.indices=static_cast<std::uint32_t>(upload.indices.size());
        extra.vb=owned.create_buffer({BufferUsage::Vertex,bytes.size()},bytes);
        extra.ib=owned.create_buffer({BufferUsage::Index,indices_bytes.size()},indices_bytes);
        obj.additional_meshes.push_back(std::move(extra));
      }
      const auto init_object = make_object_uniform(terraflux::math::Mat4::identity());
      const auto init_bytes = std::as_bytes(std::span(&init_object,1));
      if(use_gltf) {
        const auto anchor=terraflux::math::Mat4::translation(scene_camera.target) *
            terraflux::math::Mat4::scaling({scale,scale,scale}) *
            terraflux::math::Mat4::translation(obj.local_center*-1.0);
        if(node_worlds.size()>1024)
          throw std::length_error("glTF viewer supports at most 1024 mesh instances");
        for(const auto& [mesh_index,world]:node_worlds) {
          const auto model=anchor*world;
          const auto uniform=make_object_uniform(model,{1,1,1});
          const auto bytes=std::as_bytes(std::span(&uniform,1));
          const auto handle=owned.create_buffer({BufferUsage::Uniform,bytes.size()},bytes);
          obj.gltf_instances.push_back({handle,mesh_index,model});
        }
      }else if(obj.uses_scene_file){
        for(auto& instance:obj.scene_instances) {
          const auto model=obj.scene_anchor*obj.scene.world_matrix(instance.node);
          const auto uniforms=make_object_uniform(model,{0.16,0.57,0.95});
          const auto bytes=std::as_bytes(std::span(&uniforms,1));
          instance.uniform=owned.create_buffer({BufferUsage::Uniform,bytes.size()},bytes);
        }
      }else{
        obj.object_uniform_left = owned.create_buffer(
            {BufferUsage::Uniform,init_bytes.size()},init_bytes);
        obj.object_uniform_right = owned.create_buffer(
            {BufferUsage::Uniform,init_bytes.size()},init_bytes);
      }
      obj.pipeline = owned.create_pipeline({textured_object_shader_layout(),true});
      imported = std::move(obj);
      std::size_t triangle_count=imported->indices/3;
      std::size_t sections_count=imported->sections.size();
      for(const auto& mesh:imported->additional_meshes){
        triangle_count+=mesh.indices/3;
        sections_count+=mesh.sections.size();
      }
      std::cout << (use_gltf?"Imported glTF: ":"Imported OBJ: ") << triangle_count
                << " triangles, " << (imported->additional_meshes.size()+1)
                << " meshes, " << sections_count << " material ranges, "
                << imported->textures.size() << " textures, "
                << (use_gltf?imported->gltf_instances.size():
                    imported->uses_scene_file?imported->scene_instances.size():2)
                << " instances sharing per-mesh geometry\n";
    }

    double yaw = 0.45, pitch = 0.75, distance = scene_camera.starting_distance;
    double previous_time = glfwGetTime();
    terraflux::runtime::FixedStepScheduler scheduler(1.0/60.0,0.25,8);
    double engine_time=0.0, playback_accumulator=0.0;
    std::size_t playback_frame = 0;
    std::size_t uploaded_frame = static_cast<std::size_t>(-1);
    bool playing = playback.has_value();
    bool previous_space = false, previous_home = false, previous_back = false, previous_forward = false;
    unsigned rendered_frames = 0;
    std::uint64_t profile_frames = 0;
    terraflux::runtime::FrameProfiler cpu_profiler(120);

    if (playback && options.smoke_test && playback->frames.size() > 1)
      playback_frame = 1; // exercise non-initial fire state in GPU smoke test

    while (!glfwWindowShouldClose(window.get())) {
      glfwPollEvents();
      if (glfwGetKey(window.get(), GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
      const double now = glfwGetTime();
      const double profile_update_start = glfwGetTime();
      const double elapsed = std::max(0.0, now - previous_time);
      const double delta = std::clamp(elapsed, 0.0, 0.1);
      previous_time = now;
      // Clamp large stalls and catch-up work while keeping updates at 60 Hz.
      // Render/input continues once per frame, independent of fixed updates.
      const auto fixed_report=scheduler.advance(elapsed,[&](double dt) {
        engine_time+=dt;
        if(playback && playing)playback_accumulator+=dt;
        if(imported && imported->uses_scene_file)
          terraflux::runtime::update_scene_components(imported->scene,dt);
      });
      (void)fixed_report;
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
      const double profile_render_start=glfwGetTime();
      std::uint32_t frame_draws=0;
      const auto camera_vp=update_camera(device, camera, width, height, scene_camera,
                    yaw, pitch, distance,
                    cell_size, fire_field ? fire_field->width : 1,
                    fire_field ? fire_field->height : 1, engine_time);
      device.clear_frame(width, height);
      DrawIndexed command{pipeline, vb, ib, 0,
          static_cast<std::uint32_t>(upload.indices.size()), {{0, camera}}, {}};
      if (fire_buffer) command.storage_buffers.push_back({1, *fire_buffer});
      device.draw_indexed(command);
      ++frame_draws;
      OpaqueRenderQueue draw_queue;
      std::size_t visible_instances=0,culled_instances=0;
      if (imported) {
        const auto frustum=Frustum::from_view_projection(camera_vp);
        // Opaque assets only. Terrain/fire passes retain original ordering.
        const auto enqueue=[&](BufferHandle vertex,BufferHandle index,
                               const std::vector<ImportedGpu::Section>& sections,
                               BufferHandle object_uniform,const terraflux::math::Mat4& model,
                               BoundingSphere bounds,terraflux::math::Vec3 tint) {
          if(!frustum.intersects(world_bounds(bounds,model))) {
            ++culled_instances;
            return;
          }
          ++visible_instances;
          for(const auto& section:sections) {
            const auto& color=section.base_color;
            const auto material=make_object_uniform(model,{
                tint.x*color.x,tint.y*color.y,tint.z*color.z});
            draw_queue.add({{imported->pipeline,vertex,index,
                section.first,section.count,
                {{0,camera},{2,object_uniform}},{},{{3,section.albedo}}},
                object_uniform,material});
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
                imported->scene.world_matrix(instance.node);
            enqueue(imported->vb,imported->ib,imported->sections,instance.uniform,
                    model,imported->primary_bounds,{0.16,0.57,0.95});
          }
        }else {
          using terraflux::scene::Transform;
          const double spread=imported->separation;
          imported->scene.set_transform(imported->left_pivot,
              Transform{{-spread,0,0},{0,engine_time*0.7,0},{1,1,1}});
          imported->scene.set_transform(imported->right_pivot,
              Transform{{spread,0.10*spread*std::sin(engine_time*1.4),0},
                        {0,-engine_time*0.43,0},{1,1,1}});
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
      const auto queue_stats=draw_queue.submit(device);
      frame_draws+=static_cast<std::uint32_t>(queue_stats.submitted);
      if(options.smoke_test || (options.profile && profile_frames%120==0)) {
        std::cout << "Visibility: " << visible_instances << " visible, "
                  << culled_instances << " culled, "
                  << queue_stats.submitted << " opaque draws, "
                  << queue_stats.state_groups << " state groups\n";
      }
      if (options.smoke_test && imported) {
        const auto visible_asset = device.count_asset_pixels(width, height);
        if (visible_asset == 0)
          throw std::runtime_error("smoke test found no visible imported-object pixels");
        std::cout << "Visible OBJ pixels: " << visible_asset << '\n';
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
      ++profile_frames;
      if(options.profile && profile_frames%120==0) {
        const auto measured=cpu_profiler.summary();
        std::cout << "CPU profile (last " << measured.samples << " frames): update "
                  << measured.mean_update_ms << " ms, render "
                  << measured.mean_render_ms << " ms, worst "
                  << measured.max_frame_ms << " ms, draw calls/frame "
                  << measured.mean_draw_calls << '\n';
      }
      glfwSwapBuffers(window.get());
      ++rendered_frames;
      if (options.smoke_test) break;
    }

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
