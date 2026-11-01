#include <terraflux/replay.hpp>
#include <terraflux/replay_clock.hpp>
#include <array>
#include <cmath>
#include <limits>
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
void test_clock_rates() {
  using terraflux::replay::ReplayClock;
  const std::array<double,5> rates{0.25,0.5,1,2,4};
  const std::array<std::size_t,5> expected{1,2,4,8,16};
  for(std::size_t i=0;i<rates.size();++i) {
    ReplayClock clock(720);clock.set_speed(rates[i]);
    for(int tick=0;tick<84;++tick)clock.tick();
    check(clock.frame()==expected[i] && clock.pending_seconds()==0 && clock.speed()==rates[i],"exact rational speed progression");
  }
  ReplayClock clock(20);for(int i=0;i<20;++i)clock.tick();
  const auto phase=clock.pending_seconds();clock.set_playing(true);
  check(clock.pending_seconds()==phase,"unchanged play state preserves partial progress");clock.set_speed(4);
  check(clock.frame()==0 && clock.pending_seconds()==phase,"speed change retains fractional progress without seeking");
  clock.tick();check(clock.frame()==1 && std::abs(clock.pending_seconds()-0.05)<1e-12,"new rate applies on next engine tick");
  for(double invalid:{0.0,-1.0,0.3,8.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    rejects([&]{clock.set_speed(invalid);},"unsupported replay speed rejected");
  check(clock.speed()==4 && clock.frame()==1,"failed speed change preserves clock state");
}
void test_clock_controls() {
  using terraflux::replay::ReplayClock;
  rejects([]{ReplayClock invalid(0);},"empty clock rejected");
  rejects([]{ReplayClock invalid(721);},"oversized clock rejected");
  ReplayClock clock(3);clock.tick();clock.set_playing(false);
  for(int i=0;i<100;++i)clock.tick();
  check(clock.frame()==0 && clock.pending_seconds()==0 && !clock.playing(),"paused ticks have no replay backlog");
  clock.toggle();for(int i=0;i<21;++i)clock.tick();
  check(clock.frame()==1 && clock.playing(),"resume advances exactly one normal interval");
  clock.step_forward();check(clock.frame()==2 && !clock.playing() && !clock.pending_seconds(),"manual frame step pauses and clears phase");
  clock.step_forward();check(clock.frame()==2,"forward step clamps at end");
  clock.toggle();check(clock.frame()==0 && clock.playing(),"play at end restarts from first frame");
  clock.set_speed(4);for(int i=0;i<11;++i)clock.tick();
  check(clock.frame()==2 && !clock.playing() && !clock.pending_seconds(),"arrival at final frame stops immediately");
  clock.step_back();check(clock.frame()==1 && !clock.playing(),"backward step pauses");
  clock.seek(0);clock.step_back();check(clock.frame()==0 && clock.speed()==4,"reset/back clamp preserve speed");
  rejects([&]{clock.seek(3);},"invalid seek rejected");
  check(clock.frame()==0 && !clock.playing(),"invalid seek leaves current frame unchanged");
  ReplayClock single(1);single.toggle();single.tick();single.step_forward();
  check(single.frame()==0 && !single.playing(),"single-frame replay remains bounded and paused");
}

void test_playback() {
  auto playback=terraflux::replay::load(request,response);
  check(playback.frames.size()==3 && playback.grid.cells.size()==4, "parsed request and frames");
  auto field=terraflux::replay::make_fire_field(playback.grid);
  check(field.width==2 && field.height==2, "field matches grid size");
  terraflux::replay::apply_frame(playback.frames[0],field);
  check(terraflux::replay::state_at(field,0,0)==2 &&
        terraflux::replay::state_at(field,1,0)==0, "initial ignition");
  terraflux::replay::apply_frame(playback.frames[1],field);
  check(terraflux::replay::state_at(field,0,0)==1 &&
        terraflux::replay::state_at(field,1,0)==2, "new fire active");
  terraflux::replay::apply_frame(playback.frames[2],field);
  check(terraflux::replay::state_at(field,0,0)==1 &&
        terraflux::replay::state_at(field,1,0)==1, "burned history persists");
}
void test_grid_boundaries() {
  auto playback=terraflux::replay::load(request,response);
  const auto field=terraflux::replay::make_fire_field(playback.grid);
  rejects([&] { (void)terraflux::replay::state_at(field,-1,0); },
          "fire coordinates must not leave left boundary");
  rejects([&] { (void)terraflux::replay::state_at(field,2,0); },
          "fire coordinates must not leave right boundary");
  auto copy=field;
  terraflux::replay::Frame invalid{3,{{3,3}},{{3,3}}};
  rejects([&] {terraflux::replay::apply_frame(invalid,copy);},
          "off-grid fire frame must be rejected");
}
void test_missing_cell_mask() {
  terraflux::compute::GridCell a{}, b{};
  a.x = 0; a.y = 0; a.fuel_load = 1.0;
  b.x = 2; b.y = 2; b.fuel_load = 1.0;
  terraflux::compute::Grid sparse({a,b});
  auto field = terraflux::replay::make_fire_field(sparse);
  check(field.width==3 && field.height==3, "sparse bounds retained");
  check(terraflux::replay::state_at(field,1,1)==3, "missing cell excluded from fire grid");
  terraflux::replay::Frame invalid{1,{{1,1}},{{1,1}}};
  rejects([&] { terraflux::replay::apply_frame(invalid,field); },
          "missing cell must reject rendering fire");
  terraflux::compute::GridCell far{};
  far.x = 1000000; far.y = 1000000;
  terraflux::compute::Grid oversized({a,far});
  rejects([&] { (void)terraflux::replay::make_fire_field(oversized); },
          "oversized sparse rectangle must not allocate unbounded GPU storage");
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
const std::string cells_json = R"JSON("cells":[
    {"x":0,"y":0,"fuel_load":1,"fuel_moisture":0,"burn_susceptibility":1,"elevation_meters":10},
    {"x":1,"y":0,"fuel_load":1,"fuel_moisture":0,"burn_susceptibility":1,"elevation_meters":20},
    {"x":0,"y":1,"fuel_load":1,"fuel_moisture":0,"burn_susceptibility":1,"elevation_meters":10},
    {"x":1,"y":1,"fuel_load":1,"fuel_moisture":0,"burn_susceptibility":1,"elevation_meters":20}
  ])JSON";
std::string v3_request(const std::string& weather) {
  return std::string(R"JSON({"contract_version":"terraflux.compute/v3","grid_key":"test-replay","cell_size_meters":30,)JSON")+
         R"JSON("wind_speed_kmh":30,"wind_direction_degrees":270,"weather":)JSON"+weather+","+cells_json+"}";
}
void test_weather_series() {
  using terraflux::replay::load;
  auto v3_response=response;
  v3_response.replace(v3_response.find("terraflux.compute/v2"),20,"terraflux.compute/v3");
  const std::string hot=R"JSON({"temperature_celsius":32,"relative_humidity":0.2})JSON";
  const std::string wet=R"JSON({"temperature_celsius":18,"relative_humidity":0.9,"precipitation_mm_h":12})JSON";
  const auto playback=load(v3_request("["+hot+","+wet+"]"),v3_response);
  check(playback.contract_version=="terraflux.compute/v3" && playback.wind_speed_kmh==30 && playback.wind_direction_degrees==270,
        "request drivers are exposed to the viewer");
  check(playback.weather.size()==2 && playback.weather[0].temperature_celsius==32 && playback.weather[0].precipitation_mm_h==0 &&
        playback.weather[1].precipitation_mm_h==12,"weather series parsed with neutral defaults for missing keys");
  check(playback.weather_for_frame(0)->temperature_celsius==32 && playback.weather_for_frame(1)->precipitation_mm_h==12 &&
        playback.weather_for_frame(7)->precipitation_mm_h==12,"the last sample persists past the end of the series");
  const auto one=load(v3_request(hot),v3_response);
  check(one.weather.size()==1 && one.weather_for_frame(5)->temperature_celsius==32,"a single weather object applies to every frame");
  check(load(v3_request("null"),v3_response).weather.empty() && load(v3_request("[]"),v3_response).weather.empty(),"null and empty series mean no weather");
  auto v2_with_weather=v3_request(wet);
  v2_with_weather.replace(v2_with_weather.find("terraflux.compute/v3"),20,"terraflux.compute/v2");
  check(load(v2_with_weather,response).weather.empty() && !load(request,response).weather_for_frame(0),"v2 ignores a weather block");
  rejects([&]{load(v3_request(R"JSON({"relative_humidity":1.2})JSON"),v3_response);},"out-of-range humidity rejected");
  rejects([&]{load(v3_request(R"JSON({"precipitation_mm_h":-1})JSON"),v3_response);},"negative rain rejected");
  rejects([&]{load(v3_request("7"),v3_response);},"a bare number is not a weather block");
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
    test_playback(); test_grid_boundaries(); test_missing_cell_mask(); test_reject_mismatch(); test_reject_nonmonotonic();
    test_reject_offgrid(); test_reject_invalid_counts();test_weather_series();test_clock_rates();test_clock_controls();
    std::cout<<"TerraFlux replay tests: 9 passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n'; return 1;}
}
