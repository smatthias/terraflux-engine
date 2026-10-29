#include <terraflux/runtime.hpp>
#include <terraflux/scene_io.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool valid,const char* detail) {
  if(!valid)throw std::runtime_error(detail);
}
bool near(double a,double b){return std::abs(a-b)<1e-9;}
template<class F>void rejects(F&& callback,const char* detail) {
  bool threw=false;
  try{callback();}catch(const std::exception&){threw=true;}
  check(threw,detail);
}
void fixed_scheduler() {
  terraflux::runtime::FixedStepScheduler clock(0.1,1.0,5);
  std::vector<double> steps;
  auto record=[&](double dt){steps.push_back(dt);};
  auto report=clock.advance(0.06,record);
  check(report.updates==0 && near(report.interpolation_alpha,.6),"interpolation after partial step");
  report=clock.advance(0.14,record);
  check(report.updates==2 && near(report.interpolation_alpha,0.0),"deterministic updates");
  check(steps.size()==2 && near(steps[0],.1) && near(clock.simulated_seconds(),.2),
        "constant update timestep");
  report=clock.advance(.85,record);
  check(report.updates==5 && report.discarded_seconds>.29,
        "bounded catch-up avoids update spiral");
  check(report.interpolation_alpha>=0 && report.interpolation_alpha<1,
        "bounded interpolation factor");
  clock.reset();
  check(near(clock.simulated_seconds(),0),"reset simulation clock");
  rejects([&]{(void)clock.advance(-1,record);},"negative delta rejected");
  rejects([&]{(void)clock.advance(std::numeric_limits<double>::infinity(),record);},
          "nonfinite delta rejected");
  rejects([&]{terraflux::runtime::FixedStepScheduler invalid(0,1,5);},
          "zero fixed timestep rejected");
  rejects([&]{terraflux::runtime::FixedStepScheduler invalid(.1,.05,5);},
          "shorter max frame rejected");
}
void save_and_load() {
  using terraflux::scene::Scene;
  terraflux::scene::Scene scene;
  const auto anchor=scene.create_node("Anchor");
  const auto first=scene.create_node("Left \"arch\"");
  const auto second=scene.create_node("Right\npillar");
  scene.set_parent(first,anchor);
  scene.set_parent(second,anchor);
  scene.set_transform(anchor,{{3,0,4},{0,.4,0},{1,2,1}});
  scene.set_transform(first,{{-1,0,0},{0,0,0},{1,1,1}});
  scene.set_transform(second,{{1,2,0},{0,0,.1},{2,1,.5}});
  scene.set_mesh_instance(first,true);
  scene.set_mesh_instance(second,true);
  const auto text=terraflux::scene::serialize_scene(scene);
  const auto loaded=terraflux::scene::deserialize_scene(text);
  check(loaded.size()==scene.size() && loaded.get(first).parent==anchor,
        "parent hierarchy survives serialization");
  check(loaded.get(first).name=="Left \"arch\"" &&
        loaded.get(second).name=="Right\npillar",
        "JSON string escape roundtrip");
  check(loaded.get(first).mesh_instance && loaded.get(second).mesh_instance,
        "mesh instance flags survive serialization");
  const auto actual=loaded.world_matrix(first).transformPoint({0,0,0});
  const auto expected=scene.world_matrix(first).transformPoint({0,0,0});
  check(near(actual.x,expected.x) && near(actual.y,expected.y) &&
        near(actual.z,expected.z),"world matrices survive serialization");
  check(terraflux::scene::serialize_scene(loaded)==text,
        "canonical JSON scene serialization remains deterministic");
}
void reject_invalid_scene() {
  using terraflux::scene::deserialize_scene;
  const std::string base=R"JSON({"format":"terraflux.scene","version":1,
     "nodes":[{"name":"Root","parent":0,"translation":[0,0,0],
     "rotation":[0,0,0],"scale":[1,1,1]},
     {"name":"A","parent":0,"mesh_instance":true,"translation":[0,0,0],
     "rotation":[0,0,0],"scale":[1,1,1]},
     {"name":"B","parent":1,"translation":[0,0,0],
     "rotation":[0,0,0],"scale":[1,1,1]}]})JSON";
  const auto replace=[&](const std::string& from,const std::string& to) {
    std::string copy=base;
    const auto pos=copy.find(from);
    if(pos==std::string::npos)throw std::runtime_error("test substring missing");
    copy.replace(pos,from.size(),to);
    return copy;
  };
  check(deserialize_scene(base).get(1).mesh_instance,"base scene parses");
  rejects([&]{(void)deserialize_scene(replace("\"version\":1","\"version\":2"));},
          "unknown scene schema version rejected");
  rejects([&]{(void)deserialize_scene(replace("\"parent\":1","\"parent\":2"));},
          "self parent rejected");
  rejects([&]{(void)deserialize_scene(replace("\"parent\":0,\"mesh_instance\":true",
                                               "\"parent\":2,\"mesh_instance\":true"));},
          "cycle rejected");
  rejects([&]{(void)deserialize_scene(replace("\"scale\":[1,1,1]}]",
                                               "\"scale\":[1,0,1]}]"));},
          "singular scale rejected");
  rejects([&]{(void)deserialize_scene(replace("\"parent\":1","\"parent\":99"));},
          "unknown parent rejected");
  rejects([&]{(void)deserialize_scene(replace("\"name\":\"Root\"","\"name\":\"Other\""));},
          "noncanonical root rejected");
}
void example_scene(const std::string& path){
  std::ifstream file(path);
  if(!file)throw std::runtime_error("scene example not found");
  const std::string raw((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
  auto scene=terraflux::scene::deserialize_scene(raw);
  check(scene.size()==4 && scene.get(2).mesh_instance && scene.get(3).mesh_instance,
        "sample scene contains two drawables");
  check(scene.get(2).parent==1 && scene.get(3).parent==1,
        "sample scene pivot hierarchy");
  auto serialized=terraflux::scene::serialize_scene(scene);
  auto again=terraflux::scene::deserialize_scene(serialized);
  check(again.size()==scene.size(),"sample scene roundtrip");
}
}
int main(int argc,char** argv) {
  try{
    if(argc!=2)throw std::invalid_argument("runtime tests need scene fixture");
    fixed_scheduler();
    save_and_load();
    reject_invalid_scene();
    example_scene(argv[1]);
    std::cout<<"TerraFlux runtime: 4 test groups passed\n";
  }catch(const std::exception& e){
    std::cerr<<"Runtime test failed: "<<e.what()<<'\n';
    return 1;
  }
}
