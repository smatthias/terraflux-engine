#include <terraflux/assets/scene_assets.hpp>
#include <terraflux/scene_io.hpp>
#include <chrono>
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
void paths_and_limits() {
  terraflux::scene::Scene graph;
  for(const std::string path:{"../model.obj","/tmp/model.obj","C:/model.obj",
                             "nested/../../model.obj","nested\\model.obj","model.gltf",""})
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
void missing_and_symlinks() {
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
  std::ofstream(temp.path/"scene"/"empty.obj")<<"# empty\n";
  terraflux::scene::Scene empty;
  empty.declare_mesh_asset("empty","empty.obj");
  rejects([&]{(void)terraflux::assets::load_scene_obj_assets(empty,file);},"empty mesh rejected");
}
}
int main(int argc,char** argv) {
  try{
    if(argc!=2)throw std::invalid_argument("scene assets tests need scene fixture");
    schema_and_example(argv[1]);paths_and_limits();missing_and_symlinks();
    std::cout<<"TerraFlux scene assets: 3 test groups passed\n";
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
