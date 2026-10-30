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
void textured_materials(const std::string& json) {
  const auto asset=terraflux::assets::import_gltf(json);
  require(asset.images.size()==4,"PNG and JPEG base, MR and normal images decoded");
  const auto& a=asset.images.at("gltf-image-0");
  const auto& b=asset.images.at("gltf-image-1");
  require(a.width==16 && a.height==16 && b.width==16 && b.height==16,
          "decoded image dimensions");
  require(a.pixels.size()==16*16*4 && b.pixels.size()==16*16*4,
          "decoded RGBA pixels for GPU upload");
  require(a.pixels[0]!=b.pixels[0],"PNG/JPEG decoded to different colors");
  require(asset.materials.at("gltf-material-0").albedo_sampling.min==
              terraflux::TextureMinFilter::LinearMipLinear &&
          asset.materials.at("gltf-material-0").albedo_sampling.wrap_s==
              terraflux::TextureWrap::ClampToEdge &&
          asset.materials.at("gltf-material-0").albedo_sampling.wrap_t==
              terraflux::TextureWrap::MirroredRepeat &&
          asset.materials.at("gltf-material-1").albedo_sampling.min==
              terraflux::TextureMinFilter::Linear &&
          asset.materials.at("gltf-material-1").albedo_sampling.mag==
              terraflux::TextureMagFilter::Nearest,
          "glTF min/mag/mip/wrap samplers imported");
  const auto& masked=asset.materials.at("gltf-material-0");
  require(masked.occlusion_map=="gltf-image-2" &&
          masked.emissive_map=="gltf-image-0" &&
          std::abs(masked.occlusion_strength-.65)<1e-8 &&
          std::abs(masked.emissive_factor.y-.3)<1e-8 &&
          masked.alpha_mode==terraflux::assets::Material::AlphaMode::Mask &&
          std::abs(masked.alpha_factor-.9)<1e-8 &&
          std::abs(masked.alpha_cutoff-.4)<1e-8,
          "occlusion, emissive and alpha mask material data imported");
  require(asset.materials.at("gltf-material-1").alpha_mode==
              terraflux::assets::Material::AlphaMode::Opaque &&
          asset.materials.at("gltf-material-1").occlusion_map.empty(),
          "legacy opaque materials retain neutral defaults");
  require(asset.materials.at("gltf-material-0").metallic_roughness_map=="gltf-image-2" &&
          asset.materials.at("gltf-material-0").normal_map=="gltf-image-3" &&
          std::abs(asset.materials.at("gltf-material-0").normal_scale-.75)<1e-8,
          "metallic/roughness and tangent-space normal maps imported");
  require(asset.materials.at("gltf-material-0").albedo_map=="gltf-image-0" &&
          asset.materials.at("gltf-material-1").albedo_map=="gltf-image-1",
          "material texture indices resolved");
  require(std::abs(asset.materials.at("gltf-material-0").metallic_factor-.2)<1e-8 &&
          std::abs(asset.materials.at("gltf-material-1").roughness_factor-.85)<1e-8,
          "metallic/roughness factors preserved");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"index\": 0,","\"index\": 99,"));},
    "invalid texture index rejected");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"metallicFactor\": 0.2","\"metallicFactor\": -1"));},
    "invalid metallic factor rejected");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"mimeType\": \"image/png\"","\"mimeType\": \"image/jpeg\""));},
    "mime mismatch rejected");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"minFilter\": 9987","\"minFilter\": 9999"));},
    "invalid sampler min filter rejected");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"wrapS\": 33071","\"wrapS\": 123"));},
    "invalid sampler wrap rejected");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"scale\": 0.75","\"scale\": -1"));},
    "negative normal scale rejected");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"metallicRoughnessTexture\": {","\"metallicRoughnessTexture\": {\"texCoord\": 1,"));},
    "unsupported MR UV set rejected");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"strength\": 0.65","\"strength\": 1.4"));},
    "occlusion strength out of range rejected");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"alphaMode\": \"MASK\"","\"alphaMode\": \"BLEND\""));},
    "unsupported alpha blend mode rejected");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"alphaCutoff\": 0.4","\"alphaCutoff\": -0.2"));},
    "negative alpha cutoff rejected");
  rejects([&]{(void)terraflux::assets::import_gltf(
    replace_first(json,"\"emissiveFactor\": [","\"emissiveFactor\": [99,"));},
    "invalid emissive RGB factor rejected");
}

std::vector<std::uint8_t> glb_from_gltf(std::string json) {
  const auto prefix=std::string("\"uri\": \"data:application/octet-stream;base64,");
  const auto pos=json.find(prefix);
  require(pos!=std::string::npos,"embedded geometry needed to build GLB");
  const auto start=pos+prefix.size(),end=json.find('"',start);
  require(end!=std::string::npos,"data URI terminated");
  const std::string b64=json.substr(start,end-start);
  const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<std::uint8_t> bin;
  std::uint32_t accum=0;
  int bits=0;
  for(const char c:b64){
    if(c=='=')break;
    const auto value=alphabet.find(c);
    require(value!=std::string::npos,"fixture base64 valid");
    accum=(accum<<6)|static_cast<std::uint32_t>(value);
    bits+=6;
    if(bits>=8){bits-=8;bin.push_back(static_cast<std::uint8_t>((accum>>bits)&255));}
  }
  require(bin.size()==152,"GLB source geometry is 152 bytes");
  json.replace(pos,end-pos+1,"\"extras\": 0"); // the URI-less GLB buffer 0
  while(json.size()%4)json.push_back(' ');
  while(bin.size()%4)bin.push_back(0);
  std::vector<std::uint8_t> out;
  const auto put=[&](std::uint32_t n){
    for(int i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>((n>>(8*i))&255));
  };
  put(0x46546c67);put(2);
  put(static_cast<std::uint32_t>(12+8+json.size()+8+bin.size()));
  put(static_cast<std::uint32_t>(json.size()));put(0x4e4f534a);
  out.insert(out.end(),json.begin(),json.end());
  put(static_cast<std::uint32_t>(bin.size()));put(0x004e4942);
  out.insert(out.end(),bin.begin(),bin.end());
  return out;
}
void binary_glb_import(const std::string& json){
  const auto data=glb_from_gltf(json);
  const auto model=terraflux::assets::import_glb(data);
  require(model.meshes.size()==1 && model.meshes[0].indices.size()==12 &&
          model.instances.size()==2,"GLB BIN and JSON produce equivalent scene");
  const auto broken=[&](std::size_t pos,std::uint8_t value){
    auto bytes=data;bytes.at(pos)=value;
    rejects([&]{(void)terraflux::assets::import_glb(bytes);},"malformed GLB rejected");
  };
  broken(0,0); // magic
  broken(4,1); // version
  broken(8,0); // total byte length
  broken(16,0); // first chunk type
  broken(data.size()-152-4,1); // corrupt BIN header/chunk length
  auto truncated=data;truncated.pop_back();
  rejects([&]{(void)terraflux::assets::import_glb(truncated);},"GLB truncation rejected");
  auto wrong=data;
  // Preserve valid alignment but make BIN chunk length inconsistent.
  wrong[wrong.size()-152-8]=0;
  rejects([&]{(void)terraflux::assets::import_glb(wrong);},
          "GLB bad BIN chunk length rejected");
  rejects([&]{(void)terraflux::assets::import_glb({});},"empty GLB rejected");
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
    if(argc!=5)throw std::invalid_argument("gltf-tests expects base, multi-mesh, textured and tangent fixture paths");
    std::ifstream file(argv[1],std::ios::binary);
    if(!file)throw std::runtime_error("cannot open glTF test fixture");
    const std::string json((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    valid_mesh(json);
    binary_glb_import(json);
    rejects_invalid_contracts(json);
    invalid_scene_hierarchy(json);
    matrix_and_scene_selection(json);
    external_buffer_callback(json);
    std::ifstream multi_file(argv[2],std::ios::binary);
    if(!multi_file)throw std::runtime_error("cannot open glTF multi-mesh fixture");
    const std::string multi_json((std::istreambuf_iterator<char>(multi_file)),
                                  std::istreambuf_iterator<char>());
    multiple_distinct_meshes(multi_json);
    std::ifstream texfile(argv[3],std::ios::binary);
    if(!texfile)throw std::runtime_error("cannot open glTF texture fixture");
    const std::string texjson((std::istreambuf_iterator<char>(texfile)),
                               std::istreambuf_iterator<char>());
    textured_materials(texjson);
    std::ifstream blendfile(argv[4],std::ios::binary);
    if(!blendfile)throw std::runtime_error("missing tangent/blend glTF fixture");
    const std::string blendjson((std::istreambuf_iterator<char>(blendfile)),
                                std::istreambuf_iterator<char>());
    const auto blend=terraflux::assets::import_gltf(blendjson);
    require(blend.materials.at("gltf-material-0").alpha_mode==
            terraflux::assets::Material::AlphaMode::Blend &&
            std::abs(blend.materials.at("gltf-material-0").alpha_factor-.45)<1e-8,
            "glTF alpha BLEND mode and factor imported");
    require(blend.meshes.at(0).vertices.size()==8 &&
            std::abs(blend.meshes.at(0).vertices[0].tangent[3]-1)<1e-6 &&
            std::abs(blend.meshes.at(0).vertices[1].tangent[3]+1)<1e-6 &&
            blend.meshes.at(0).vertices[4].tangent[3]==0,
            "first primitive uses explicit signed tangents, second retains derivative fallback");
    const auto tangent_gpu=terraflux::graphics::prepare_textured_asset_upload(blend.meshes[0]);
    require(std::abs(tangent_gpu.vertices[0].tangent[3]-1)<1e-6 &&
            std::abs(tangent_gpu.vertices[1].tangent[3]+1)<1e-6,
            "GPU upload preserves signed tangent basis");
    rejects([&]{(void)terraflux::assets::import_gltf(
      replace_first(blendjson,"\"componentType\": 5126,\n      \"count\": 4,\n      \"type\": \"VEC4\"",
                                  "\"componentType\": 5126,\n      \"count\": 4,\n      \"type\": \"VEC3\""));},
      "TANGENT requires a VEC4 accessor");
    std::cout<<"glTF 2.0: 9 test groups passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
