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
void runtime_components_and_profiler(){
  using terraflux::runtime::update_scene_components;
  terraflux::scene::Scene graph;
  const auto pivot=graph.create_node("Pivot");
  const auto mesh=graph.create_node("Mesh",pivot);
  graph.set_angular_velocity(pivot,terraflux::math::Vec3{0,2,0});
  check(update_scene_components(graph,.1)==1,"one active Spin component");
  check(near(graph.get(pivot).local.rotation_radians.y,.2),
        "Spin component integrates fixed dt");
  check(near(graph.get(mesh).local.rotation_radians.y,0),
        "unregistered component nodes remain static");
  auto original=terraflux::scene::serialize_scene(graph);
  auto restored=terraflux::scene::deserialize_scene(original);
  check(restored.get(pivot).angular_velocity.has_value() &&
        near(restored.get(pivot).angular_velocity->y,2),
        "Spin component JSON roundtrip");
  check(update_scene_components(restored,.1)==1 &&
        near(restored.get(pivot).local.rotation_radians.y,.4),
        "serialized behavior updates after loading");
  restored.set_angular_velocity(pivot,std::nullopt);
  check(update_scene_components(restored,.1)==0,
        "removed component stops simulation");
  rejects([&]{graph.set_angular_velocity(0,terraflux::math::Vec3{0,1,0});},
          "root cannot spin");
  rejects([&]{graph.set_angular_velocity(pivot,terraflux::math::Vec3{1001,0,0});},
          "excessive angular velocity rejected");
  rejects([&]{(void)update_scene_components(graph,-.1);},"negative timestep");
  terraflux::runtime::FrameProfiler profiler(2);
  auto empty=profiler.summary();
  check(empty.samples==0 && near(empty.mean_render_ms,0),"empty profiler summary");
  profiler.record({.001,.004,2});
  profiler.record({.003,.002,4});
  auto summary=profiler.summary();
  check(summary.samples==2 && near(summary.mean_update_ms,2) &&
        near(summary.mean_render_ms,3) && near(summary.mean_draw_calls,3),
        "CPU frame profiler summaries");
  profiler.record({.002,.003,6});
  summary=profiler.summary();
  check(summary.samples==2 && near(summary.mean_draw_calls,5),
        "rolling profiler evicts old frames");
  rejects([&]{profiler.record({-1,0,0});},"bad profiler sample");
  profiler.clear();
  check(profiler.summary().samples==0,"profiler clear");
}
void motion_components() {
  using terraflux::math::Vec3;
  using terraflux::runtime::update_scene_components;
  terraflux::scene::Scene graph;
  const auto parent=graph.create_node("Moving parent");
  const auto child=graph.create_node("Spinning child",parent);
  const auto idle=graph.create_node("Static");
  graph.set_transform(parent,{{0,0,0},{0,0,0},{2,3,4}});
  graph.set_linear_velocity(parent,Vec3{2,-1,.5});
  graph.set_linear_velocity(child,Vec3{1,0,0});
  graph.set_angular_velocity(child,Vec3{0,2,0});
  auto batched=graph;
  terraflux::runtime::FixedStepScheduler a(.1,1,10),b(.1,1,10);
  const auto tick=[&](double dt){
    check(update_scene_components(graph,dt)==2,"count updated nodes, not components");
  };
  a.advance(.2,tick);
  b.advance(.06,[&](double dt){update_scene_components(batched,dt);});
  b.advance(.14,[&](double dt){update_scene_components(batched,dt);});
  check(near(graph.get(parent).local.translation.x,.4) &&
        near(graph.get(parent).local.translation.y,-.2) &&
        near(graph.get(parent).local.translation.z,.1),"integrate local Motion in all axes");
  check(near(graph.get(child).local.rotation_radians.y,.4),"Spin and Motion coexist");
  const auto world=graph.world_matrix(child).transformPoint({0,0,0});
  check(near(world.x,.8) && near(world.y,-.2) && near(world.z,.1),
        "parent scale and translation apply to moving child");
  check(near(graph.get(idle).local.translation.x,0),"static node stays still");
  check(terraflux::scene::serialize_scene(graph)==terraflux::scene::serialize_scene(batched),
        "Motion is deterministic across wall-clock frame partitions");
  const auto encoded=terraflux::scene::serialize_scene(graph);
  auto restored=terraflux::scene::deserialize_scene(encoded);
  check(restored.get(child).linear_velocity.has_value() &&
        terraflux::scene::serialize_scene(restored)==encoded,"Motion JSON roundtrip");
  update_scene_components(graph,.1);
  update_scene_components(restored,.1);
  check(terraflux::scene::serialize_scene(restored)==terraflux::scene::serialize_scene(graph),
        "loaded components continue identically");
  restored.set_linear_velocity(parent,std::nullopt);
  const auto frozen=restored.get(parent).local.translation;
  check(update_scene_components(restored,.1)==1 &&
        near(restored.get(parent).local.translation.x,frozen.x),"removing Motion stops translation");
  rejects([&]{graph.set_linear_velocity(0,Vec3{});},"root cannot move");
  rejects([&]{graph.set_linear_velocity(999,Vec3{});},"unknown Motion node rejected");
  for(double bad:{1001.0,-1001.0,std::numeric_limits<double>::infinity(),
                  std::numeric_limits<double>::quiet_NaN()}) {
    rejects([&]{graph.set_linear_velocity(child,Vec3{0,bad,0});},"invalid Motion velocity rejected");
  }
  // Malformed serialized components must fail rather than disappear on load.
  const auto corrupt=[&](const std::string& value) {
    auto text=encoded;
    const auto key=text.find("\"motion\":[");
    const auto end=text.find(']',key);
    text.replace(key,end-key+1,"\"motion\":"+value);
    return text;
  };
  for(const std::string value:{"[1,2]","[1,2,3,4]","[0,1001,0]","[0,\"x\",0]","null"})
    rejects([&]{(void)terraflux::scene::deserialize_scene(corrupt(value));},
            "malformed Motion JSON rejected");
  auto root_motion=encoded;
  root_motion.insert(root_motion.find("\"parent\":0"),"\"motion\":[0,0,0],");
  rejects([&]{(void)terraflux::scene::deserialize_scene(root_motion);},"serialized root Motion rejected");
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
  check(scene.get(2).angular_velocity.has_value(),
        "sample scene Spin component parsed");
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
    runtime_components_and_profiler();
    motion_components();
    example_scene(argv[1]);
    std::cout<<"TerraFlux runtime: 6 test groups passed\n";
  }catch(const std::exception& e){
    std::cerr<<"Runtime test failed: "<<e.what()<<'\n';
    return 1;
  }
}
