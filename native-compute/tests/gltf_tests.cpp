#include <terraflux/assets/gltf.hpp>
#include <terraflux/graphics/mesh_upload.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
template<class F>void rejects(F&& callback,const char* message) {
  bool thrown=false;
  try{callback();}catch(const std::exception&){thrown=true;}
  require(thrown,message);
}
std::string replace_first(std::string text,const std::string& from,
                          const std::string& to){
  const auto p=text.find(from);
  if(p==std::string::npos)throw std::runtime_error("test fixture substring not found");
  text.replace(p,from.size(),to);
  return text;
}
void valid_mesh(const std::string& json) {
  const auto model=terraflux::assets::import_gltf(json);
  require(model.mesh.vertices.size()==8 && model.mesh.indices.size()==12,
          "two glTF primitives preserve vertex and index data");
  require(model.mesh.sections.size()==2 && model.mesh.sections[0].index_count==6 &&
          model.mesh.sections[1].first_index==6,"per-primitive draw ranges");
  require(model.mesh.sections[0].material_name=="gltf-material-0" &&
          model.mesh.sections[1].material_name=="gltf-material-1","material mapping");
  require(std::abs(model.materials.at("gltf-material-0").base_color.y-0.7)<1e-6,
          "PBR baseColorFactor imported");
  require(std::abs(model.mesh.vertices[2].position.z-0.65)<1e-5 &&
          std::abs(model.mesh.vertices[2].uv.x-0.5)<1e-6,"position and UV extraction");
  auto gpu=terraflux::graphics::prepare_textured_asset_upload(model.mesh);
  require(gpu.vertices.size()==8 && gpu.indices.size()==12,"GPU upload glTF");
}
void rejects_invalid_contracts(const std::string& json) {
  rejects([&]{(void)terraflux::assets::import_gltf(
      replace_first(json,"\"version\": \"2.0\"","\"version\": \"1.0\""));},
      "version requirement");
  rejects([&]{(void)terraflux::assets::import_gltf(
      replace_first(json,"\"componentType\": 5126","\"componentType\": 5123"));},
      "POSITION attribute type requirement");
  rejects([&]{(void)terraflux::assets::import_gltf(
      replace_first(json,"\"mode\": 4","\"mode\": 5"));},"triangle mode only");
  rejects([&]{(void)terraflux::assets::import_gltf(
      replace_first(json,"\"byteLength\": 152","\"byteLength\": 180"));},
      "buffer length cannot exceed decoded bytes");
  rejects([&]{(void)terraflux::assets::import_gltf(
      replace_first(json,"\"material\": 0","\"material\": 999"));},
      "out-of-bounds glTF material index");
  rejects([&]{(void)terraflux::assets::import_gltf(
      replace_first(json,"\"byteOffset\": 128","\"byteOffset\": 149"));},
      "out-of-range buffer view");
}
void external_buffer_callback(const std::string& json) {
  const auto original="data:application/octet-stream;base64,";
  const auto pos=json.find(original);
  require(pos!=std::string::npos,"fixture embedded buffer exists");
  const auto end=json.find('"',pos);
  require(end!=std::string::npos,"fixture closing quote");
  auto external=json;
  external.replace(pos,end-pos,"geometry.bin");
  rejects([&]{(void)terraflux::assets::import_gltf(external);},
          "external uri needs explicit resolver");
  rejects([&]{(void)terraflux::assets::import_gltf(external,
       [](const std::string&){return std::vector<std::uint8_t>(9,0);});},
       "incomplete external buffer rejected");
  // The callback is deliberately exercised with a distinct tiny valid
  // byte buffer test in future; this fixture tests resolver invocation.
  bool invoked=false;
  rejects([&]{(void)terraflux::assets::import_gltf(external,
       [&](const std::string& uri)->std::vector<std::uint8_t>{
         invoked=true;
         require(uri=="geometry.bin","URI passed unchanged");
         return {};
       });},"empty callback output rejected");
  require(invoked,"glTF external URI resolver invoked");

  // Supply the same valid binary data with the external-buffer callback.
  std::vector<std::uint8_t> raw;
  const auto append_word=[&](std::uint32_t value) {
    for(int i=0;i<4;++i) raw.push_back(
        static_cast<std::uint8_t>((value>>(i*8))&255));
  };
  for(const float value:std::vector<float>{
      -.6f,0,-.5f, .6f,0,-.5f, 0,0,.65f, 0,1,0,
      -.55f,.25f,-.75f, .55f,.25f,-.75f, 0,.3f,.95f, 0,1,0,
      0,0,1,0,.5f,1,.5f,.5f
  })append_word(std::bit_cast<std::uint32_t>(value));
  for(const std::uint16_t value:std::vector<std::uint16_t>{
      0,2,1,0,1,3, 1,2,3,2,0,3
  }) {
    raw.push_back(static_cast<std::uint8_t>(value&255));
    raw.push_back(static_cast<std::uint8_t>(value>>8));
  }
  require(raw.size()==152,"fixture external buffer length");
  const auto resolved=terraflux::assets::import_gltf(external,
       [&](const std::string& uri) {
         require(uri=="geometry.bin","external URI resolved by caller");
         return raw;
       });
  require(resolved.mesh.indices.size()==12 &&
          resolved.mesh.sections.size()==2,
          "external glTF .bin produces complete mesh");
}
}
int main(int argc,char** argv) {
  try {
    if(argc!=2)throw std::invalid_argument("gltf-tests expects fixture path");
    std::ifstream file(argv[1],std::ios::binary);
    if(!file)throw std::runtime_error("cannot open glTF test fixture");
    const std::string json((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    valid_mesh(json);
    rejects_invalid_contracts(json);
    external_buffer_callback(json);
    std::cout<<"glTF 2.0: 3 test groups passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
