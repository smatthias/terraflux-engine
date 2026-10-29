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
  require(model.meshes.size()==1 &&
          model.meshes.at(0).vertices.size()==8 && model.meshes.at(0).indices.size()==12,
          "two glTF primitives preserve vertex and index data");
  require(model.meshes.at(0).sections.size()==2 && model.meshes.at(0).sections[0].index_count==6 &&
          model.meshes.at(0).sections[1].first_index==6,"per-primitive draw ranges");
  require(model.meshes.at(0).sections[0].material_name=="gltf-material-0" &&
          model.meshes.at(0).sections[1].material_name=="gltf-material-1","material mapping");
  require(std::abs(model.materials.at("gltf-material-0").base_color.y-0.7)<1e-6,
          "PBR baseColorFactor imported");
  require(std::abs(model.meshes.at(0).vertices[2].position.z-0.65)<1e-5 &&
          std::abs(model.meshes.at(0).vertices[2].uv.x-0.5)<1e-6,"position and UV extraction");
  require(model.nodes.size()==4 && model.instances.size()==2 &&
          model.instances[0].mesh_index==0 && model.instances[1].mesh_index==0,
          "scene hierarchy instances share one mesh");
  require(model.nodes[0].children.size()==2 && !model.nodes[3].in_scene,
          "scene selects only the root and its descendants");
  const auto left=model.instances[0].world.transformPoint({0,0,0});
  const auto right=model.instances[1].world.transformPoint({0,0,0});
  require(std::abs(left.x+1.15)<1e-5 && std::abs(right.x-1.15)<1e-5,
          "glTF parent-child translations");
  const auto rotated=model.instances[0].world.transformDirection({1,0,0});
  require(std::abs(rotated.z+0.70710678)<1e-5,
          "glTF quaternion Y rotation");
  const auto scaled=model.instances[1].world.transformDirection({0,1,0});
  require(std::abs(scaled.y-1.2)<1e-5, "glTF node nonuniform scale");
  auto gpu=terraflux::graphics::prepare_textured_asset_upload(model.meshes.at(0));
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
void invalid_scene_hierarchy(const std::string& json) {
  rejects([&]{(void)terraflux::assets::import_gltf(replace_first(
      json,"\"children\": [\n        1,\n        2\n      ]",
      "\"children\": [0,1,2]"));},"self-referential node");
  rejects([&]{(void)terraflux::assets::import_gltf(replace_first(
      json,"\"children\": [\n        1,\n        2\n      ]",
      "\"children\": [1,1,2]"));},"two parents from duplicate child");
  rejects([&]{(void)terraflux::assets::import_gltf(replace_first(
      json,"\"translation\": [\n        -1.15,\n        0,\n        0\n      ]",
      "\"translation\": [1,2]"));},"bad glTF translation");
  rejects([&]{(void)terraflux::assets::import_gltf(replace_first(
      json,"\"rotation\": [\n        0,\n        0.382683432365,\n        0,\n        0.923879532511\n      ]",
      "\"rotation\": [0,0,0,0]"));},"zero quaternion");
  rejects([&]{(void)terraflux::assets::import_gltf(replace_first(
      json,"\"scale\": [\n        0.75,\n        1.2,\n        0.75\n      ]",
      "\"scale\": [0,1,1]"));},"singular glTF instance");
  rejects([&]{(void)terraflux::assets::import_gltf(replace_first(
      json,"\"scene\": 0", "\"scene\": 8"));},"invalid selected scene");
}
void matrix_and_scene_selection(const std::string& json) {
  const auto alternate=replace_first(json,"\"scene\": 0","\"scene\": 1");
  const auto alt=terraflux::assets::import_gltf(alternate);
  require(alt.instances.size()==1 && alt.instances[0].node_index==3,
          "secondary glTF scene chooses a different root");
  const auto origin=alt.instances[0].world.transformPoint({0,0,0});
  require(std::abs(origin.z-10)<1e-8,"selected alternate scene world transform");

  const auto from=R"JSON("translation": [
        1.15,
        0,
        0
      ],
      "scale": [
        0.75,
        1.2,
        0.75
      ])JSON";
  const auto to=R"JSON("matrix": [
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        1.5, 2, 0, 1
      ])JSON";
  const auto matrix_doc=replace_first(json,from,to);
  const auto transformed=terraflux::assets::import_gltf(matrix_doc);
  require(transformed.instances.size()==2,"matrix transform preserves two instances");
  const auto p=transformed.instances[1].world.transformPoint({0,0,0});
  require(std::abs(p.x-1.5)<1e-8 && std::abs(p.y-2.0)<1e-8,
          "column-major explicit glTF matrix transform");

  const auto conflict=replace_first(json,from,
     std::string(to)+R"JSON(,
      "translation": [0, 0, 0])JSON");
  rejects([&]{(void)terraflux::assets::import_gltf(conflict);},
          "matrix and TRS on one node rejected");
}

void multiple_distinct_meshes(const std::string& json) {
  const auto model=terraflux::assets::import_gltf(json);
  require(model.meshes.size()==2, "two distinct imported meshes");
  require(model.meshes[0].indices.size()==12 && model.meshes[1].indices.size()==6,
          "distinct mesh geometry and index buffers");
  require(model.meshes[0].sections.size()==2 && model.meshes[1].sections.size()==1,
          "each mesh owns its own material sections");
  require(model.meshes[0].sections[0].material_name=="gltf-material-0" &&
          model.meshes[1].sections[0].material_name=="gltf-material-1",
          "distinct mesh materials");
  require(model.instances.size()==4, "four selected mesh-node instances");
  require(model.instances[0].mesh_index==0 && model.instances[1].mesh_index==0 &&
          model.instances[2].mesh_index==1 && model.instances[3].mesh_index==1,
          "mesh index preserved through hierarchy");
  require(model.nodes[3].in_scene==false, "inactive alternate scene excluded");
  for(const auto& mesh:model.meshes) {
    const auto gpu=terraflux::graphics::prepare_textured_asset_upload(mesh);
    require(gpu.vertices.size()==mesh.vertices.size() &&
            gpu.indices.size()==mesh.indices.size(), "both meshes upload independently");
  }
  const auto missing_mesh=replace_first(json,"\"mesh\": 1","\"mesh\": 1000");
  rejects([&]{(void)terraflux::assets::import_gltf(missing_mesh);},
          "missing referenced mesh rejected");
  const auto too_many=replace_first(json,"\"mesh\": 1","\"mesh\": -1");
  rejects([&]{(void)terraflux::assets::import_gltf(too_many);},
          "negative mesh index rejected");
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
  require(resolved.meshes.at(0).indices.size()==12 &&
          resolved.meshes.at(0).sections.size()==2,
          "external glTF .bin produces complete mesh");
}
}
int main(int argc,char** argv) {
  try {
    if(argc!=3)throw std::invalid_argument("gltf-tests expects base and multi-mesh fixture paths");
    std::ifstream file(argv[1],std::ios::binary);
    if(!file)throw std::runtime_error("cannot open glTF test fixture");
    const std::string json((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    valid_mesh(json);
    rejects_invalid_contracts(json);
    invalid_scene_hierarchy(json);
    matrix_and_scene_selection(json);
    external_buffer_callback(json);
    std::ifstream multi_file(argv[2],std::ios::binary);
    if(!multi_file)throw std::runtime_error("cannot open glTF multi-mesh fixture");
    const std::string multi_json((std::istreambuf_iterator<char>(multi_file)),
                                  std::istreambuf_iterator<char>());
    multiple_distinct_meshes(multi_json);
    std::cout<<"glTF 2.0: 6 test groups passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
