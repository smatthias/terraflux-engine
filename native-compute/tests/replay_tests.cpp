#include <terraflux/replay.hpp>
#include <terraflux/terrain_mesh.hpp>
#include <terraflux/graphics/terrain_upload.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool b, const char* what) { if (!b) throw std::runtime_error(what); }
template <typename F> void rejects(F&& f, const char* what) {
  bool threw = false;
  try { f(); } catch(const std::exception&) { threw = true; }
  check(threw, what);
}
const std::string request = R"JSON({
  "contract_version":"terraflux.compute/v2","grid_key":"test-replay",
  "cell_size_meters":30,
  "cells":[
    {"x":0,"y":0,"fuel_load":1,"fuel_moisture":0,"burn_susceptibility":1,"elevation_meters":10},
    {"x":1,"y":0,"fuel_load":1,"fuel_moisture":0,"burn_susceptibility":1,"elevation_meters":20},
    {"x":0,"y":1,"fuel_load":1,"fuel_moisture":0,"burn_susceptibility":1,"elevation_meters":10},
    {"x":1,"y":1,"fuel_load":1,"fuel_moisture":0,"burn_susceptibility":1,"elevation_meters":20}
  ]
})JSON";
const std::string response = R"JSON({
  "contract_version":"terraflux.compute/v2","grid_key":"test-replay",
  "frames":[
    {"step":0,"active_cells":[[0,0]],"burned_cells":[[0,0]],"active_count":1,"burned_count":1},
    {"step":1,"active_cells":[[1,0]],"burned_cells":[[0,0],[1,0]],"active_count":1,"burned_count":2},
    {"step":2,"active_cells":[],"burned_cells":[[0,0],[1,0]],"active_count":0,"burned_count":2}
  ]
})JSON";
void test_playback() {
  auto playback=terraflux::replay::load(request,response);
  check(playback.frames.size()==3 && playback.grid.cells.size()==4, "parsed request and frames");
  auto mesh=terraflux::terrain::build_terrain_mesh(playback.grid,playback.cell_size_meters);
  auto upload=terraflux::graphics::prepare_terrain_upload(mesh);
  terraflux::replay::apply_frame(mesh,playback.frames[0],upload);
  check(upload.vertices[0].fire_state==1 && upload.vertices[1].fire_state==0, "initial ignition");
  terraflux::replay::apply_frame(mesh,playback.frames[1],upload);
  check(upload.vertices[0].fire_state==0.5f && upload.vertices[1].fire_state==1, "new fire is active");
  terraflux::replay::apply_frame(mesh,playback.frames[2],upload);
  check(upload.vertices[0].fire_state==0.5f && upload.vertices[1].fire_state==0.5f, "burned history persists");
}
void test_reject_mismatch() {
  auto bad=response;
  const auto where=bad.find("test-replay");
  bad.replace(where,11,"wrong-grid!");
  rejects([&]{(void)terraflux::replay::load(request,bad);},"grid mismatch rejected");
}
void test_reject_nonmonotonic() {
  auto bad=response;
  const auto where=bad.rfind("[0,0],[1,0]");
  bad.replace(where,11,"[0,0]");
  rejects([&]{(void)terraflux::replay::load(request,bad);},"burned history/count mismatch rejected");
}
void test_reject_offgrid() {
  auto bad=response;
  const auto where=bad.find("[1,0]");
  bad.replace(where,5,"[9,9]");
  rejects([&]{(void)terraflux::replay::load(request,bad);},"off-grid active cell rejected");
}
void test_reject_invalid_counts() {
  auto bad=response;
  const auto where=bad.find("\"active_count\":1");
  bad.replace(where,16,"\"active_count\":8");
  rejects([&]{(void)terraflux::replay::load(request,bad);},"active count mismatch rejected");
}
} // namespace
int main(){
  try{
    test_playback(); test_reject_mismatch(); test_reject_nonmonotonic();
    test_reject_offgrid(); test_reject_invalid_counts();
    std::cout<<"TerraFlux replay tests: 5 passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n'; return 1;}
}
