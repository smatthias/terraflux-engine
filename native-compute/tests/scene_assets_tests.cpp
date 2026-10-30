#include <terraflux/assets/scene_assets.hpp>
#include <terraflux/scene_io.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void check(bool valid,const char* detail) {
  if(!valid)throw std::runtime_error(detail);
}
template<class F>void rejects(F callback,const char* detail) {
  bool threw=false;
  try{callback();}catch(const std::exception&){threw=true;}
  check(threw,detail);
}
std::string read(const std::filesystem::path& file) {
  std::ifstream input(file);
  if(!input)throw std::runtime_error("cannot read fixture");
  return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void schema_and_example(const std::filesystem::path& fixture) {
  const auto graph=terraflux::scene::deserialize_scene(read(fixture));
  check(graph.mesh_assets().size()==3 && graph.size()==5,"named assets and nodes loaded");
  check(graph.get(2).mesh_asset=="pyramid" && graph.get(3).mesh_asset=="painted",
        "different node assets preserved");
  const auto text=terraflux::scene::serialize_scene(graph);
  check(terraflux::scene::serialize_scene(terraflux::scene::deserialize_scene(text))==text,
        "deterministic version 2 scene roundtrip");
  const auto assets=terraflux::assets::load_scene_obj_assets(graph,fixture);
  check(assets.meshes.size()==2 && assets.sources.size()==2,"distinct OBJ files loaded once");
  check(assets.mesh_indices.at("pyramid")==assets.mesh_indices.at("pyramid_alias"),
        "normalized path aliases share mesh");
  check(assets.mesh_indices.at("painted")!=assets.mesh_indices.at("pyramid"),
        "distinct sources have separate GPU mesh indices");
  check(!assets.meshes.at(assets.mesh_indices.at("painted")).material_libraries.empty(),
        "per-model material libraries preserved");
  auto changed=graph;
  rejects([&]{changed.set_mesh_asset(0,"pyramid");},"root asset rejected");
  rejects([&]{changed.set_mesh_asset(2,"unknown");},"unknown reference rejected");
  changed.set_mesh_instance(2,false);
  check(!changed.get(2).mesh_asset && !changed.get(2).mesh_instance,"disable clears asset reference");
  auto legacy=terraflux::scene::Scene{};
  const auto node=legacy.create_node("Legacy");
  legacy.set_mesh_instance(node,true);
  const auto old=terraflux::scene::serialize_scene(legacy);
  check(old.find("\"version\":1")!=std::string::npos &&
        terraflux::scene::deserialize_scene(old).get(node).mesh_instance,
        "legacy primary-mesh scene remains version 1");
  const auto corrupt=[&](const std::string& from,const std::string& to) {
    auto result=text;
    const auto where=result.find(from);
    check(where!=std::string::npos,"test replacement exists");
    result.replace(where,from.size(),to);
    return result;
  };
  rejects([&]{(void)terraflux::scene::deserialize_scene(corrupt("\"version\":2","\"version\":1"));},
          "version 1 cannot silently accept assets");
  rejects([&]{(void)terraflux::scene::deserialize_scene(corrupt("\"mesh_asset\":\"pyramid\"",
                                                               "\"mesh_asset\":\"missing\""));},
          "serialized dangling reference rejected");
  rejects([&]{(void)terraflux::scene::deserialize_scene(corrupt("\"mesh_instance\":true",
                                                               "\"mesh_instance\":false"));},
          "conflicting mesh flag rejected");
  rejects([&]{(void)terraflux::scene::deserialize_scene(corrupt(",\"mesh_asset\":\"pyramid\"",""));},
          "version 2 cannot silently use primary asset");
}
void mixed_models(const std::filesystem::path& fixture) {
  using terraflux::assets::load_scene_assets;
  const auto graph=terraflux::scene::deserialize_scene(read(fixture));
  const auto text=terraflux::scene::serialize_scene(graph);
  check(terraflux::scene::serialize_scene(terraflux::scene::deserialize_scene(text))==text,
        "mixed format schema roundtrip");
  const auto assets=load_scene_assets(graph,fixture);
  check(assets.has_gltf && assets.model_instances.size()==4 && assets.meshes.size()==7,
        "mixed scene loads four canonical models and seven mesh definitions");
  check(assets.model_indices.at("gltf")==assets.model_indices.at("gltf_alias"),
        "repeated multi-mesh glTF references share one model");
  const auto& gltf=assets.model_instances.at(assets.model_indices.at("gltf"));
  const auto& glb=assets.model_instances.at(assets.model_indices.at("glb"));
  const auto& obj=assets.model_instances.at(assets.model_indices.at("obj"));
  check(gltf.size()==4 && glb.size()==4 && obj.size()==1,"authored glTF and GLB active instances preserved");
  const auto direct=terraflux::assets::import_gltf(read(fixture.parent_path()/"assets/textured_multi_mesh.gltf"));
  for(std::size_t i=0;i<gltf.size();++i) {
    for(const terraflux::math::Vec3 point: {terraflux::math::Vec3{},terraflux::math::Vec3{1,2,3}}) {
    const auto expected=direct.instances.at(i).world.transformPoint(point);
    const auto actual=gltf[i].local.transformPoint(point);
    check(std::abs(expected.x-actual.x)<1e-9 && std::abs(expected.y-actual.y)<1e-9 &&
          std::abs(expected.z-actual.z)<1e-9,"authored hierarchy matrices survive model expansion");
    const auto binary=glb[i].local.transformPoint(point);
    check(std::abs(binary.x-actual.x)<1e-9 && std::abs(binary.y-actual.y)<1e-9 &&
          std::abs(binary.z-actual.z)<1e-9,"GLB and glTF hierarchy agree");
    }
  }
  // Same source mesh instanced in several glTF nodes remains one geometry definition.
  check(gltf[0].mesh_index==gltf[1].mesh_index || gltf[0].mesh_index==gltf[2].mesh_index ||
        gltf[0].mesh_index==gltf[3].mesh_index,"glTF instances share mesh indices");
  std::size_t textured=0;
  bool blended=false;
  for(const auto& [name,material]:assets.materials) {
    check(assets.material_folders.contains(name),"material source folder retained");
    for(const auto* map:{&material.albedo_map,&material.normal_map,&material.metallic_roughness_map,
                        &material.occlusion_map,&material.emissive_map}) {
      if(!map->empty()) {
        check(assets.images.contains(*map),"material image names resolve within their model namespace");
        ++textured;
      }
    }
    blended=blended || material.alpha_mode==terraflux::assets::Material::AlphaMode::Blend;
  }
  check(textured>0 && blended,"textures and transparency preserved in mixed scene");
  rejects([&]{(void)terraflux::assets::load_scene_obj_assets(graph,fixture);},
          "OBJ-only compatibility loader rejects mixed formats");
}
void paths_and_limits() {
  terraflux::scene::Scene graph;
  for(const std::string path:{"../model.obj","/tmp/model.obj","C:/model.obj",
                             "nested/../../model.obj","nested\\model.obj","model.fbx",""})
    rejects([&]{graph.declare_mesh_asset("bad",path);},"invalid model path rejected");
  rejects([&]{graph.declare_mesh_asset("", "model.obj");},"empty asset name rejected");
  graph.declare_mesh_asset("one","nested/./model.obj");
  check(graph.mesh_assets().at("one")=="nested/model.obj","asset path normalized");
  rejects([&]{graph.declare_mesh_asset("one","other.obj");},"duplicate asset name rejected");
  for(int i=1;i<128;++i)graph.declare_mesh_asset(std::to_string(i),"model.obj");
  rejects([&]{graph.declare_mesh_asset("overflow","model.obj");},"bounded asset registry");
}
struct TempDirectory {
  std::filesystem::path path=std::filesystem::temp_directory_path()/
      ("terraflux-scene-assets-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TempDirectory(){std::filesystem::create_directories(path/"scene");}
  ~TempDirectory(){std::error_code error;std::filesystem::remove_all(path,error);}
};
void missing_and_symlinks(const std::filesystem::path& fixture) {
  TempDirectory temp;
  const auto file=temp.path/"scene"/"test.json";
  terraflux::scene::Scene missing;
  missing.declare_mesh_asset("missing","missing.obj");
  rejects([&]{(void)terraflux::assets::load_scene_obj_assets(missing,file);},"missing source rejected");
  std::ofstream(temp.path/"outside.obj")<<"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  std::filesystem::create_symlink(temp.path/"outside.obj",temp.path/"scene"/"escape.obj");
  terraflux::scene::Scene escape;
  escape.declare_mesh_asset("escape","escape.obj");
  rejects([&]{(void)terraflux::assets::load_scene_obj_assets(escape,file);},"symlink outside scene rejected");
  auto external=read(fixture.parent_path()/"assets/textured_multi_mesh.gltf");
  const auto uri=external.find("data:application/octet-stream;base64,");
  check(uri!=std::string::npos,"fixture geometry data URI exists");
  external.replace(uri,external.find('"',uri)-uri,"escape.bin");
  std::ofstream(temp.path/"scene"/"external.gltf")<<external;
  std::ofstream(temp.path/"outside.bin")<<"outside";
  std::filesystem::create_symlink(temp.path/"outside.bin",temp.path/"scene"/"escape.bin");
  terraflux::scene::Scene external_graph;
  external_graph.declare_mesh_asset("model","external.gltf");
  bool escaped=false;
  try{(void)terraflux::assets::load_scene_assets(external_graph,file);}
  catch(const std::invalid_argument& error){escaped=std::string(error.what()).find("symlink")!=std::string::npos;}
  check(escaped,"external glTF buffer symlink escape rejected by scene resolver");
  std::ofstream(temp.path/"scene"/"empty.obj")<<"# empty\n";
  terraflux::scene::Scene empty;
  empty.declare_mesh_asset("empty","empty.obj");
  rejects([&]{(void)terraflux::assets::load_scene_obj_assets(empty,file);},"empty mesh rejected");
}
}
int main(int argc,char** argv) {
  try{
    if(argc!=3)throw std::invalid_argument("scene assets tests need scene fixture");
    schema_and_example(argv[1]);mixed_models(argv[2]);paths_and_limits();missing_and_symlinks(argv[1]);
    std::cout<<"TerraFlux scene assets: 4 test groups passed\n";
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
