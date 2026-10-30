#include <terraflux/assets/gltf.hpp>
#include <terraflux/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::assets {
namespace {
using terraflux::json::Json;
using terraflux::json::field;
using terraflux::json::object;
using terraflux::json::string;
using terraflux::json::number;

const Json* optional(const Json::Object& o,const std::string& key) {
  auto it=o.find(key);return it==o.end()?nullptr:&it->second;
}
const Json::Array& array(const Json& j) {
  if(!std::holds_alternative<Json::Array>(j.value))
    throw std::invalid_argument("glTF expected array");
  return std::get<Json::Array>(j.value);
}
std::uint32_t uint(const Json& j,const char* field_name) {
  const double n=number(j);
  if(n<0 || n>static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
     std::trunc(n)!=n)
    throw std::invalid_argument(std::string("invalid glTF integer: ")+field_name);
  return static_cast<std::uint32_t>(n);
}
std::uint32_t integer(const Json::Object& o,const std::string& key,
                      std::uint32_t fallback=0) {
  const auto* p=optional(o,key);return p?uint(*p,key.c_str()):fallback;
}
bool boolean(const Json& j) {
  if(!std::holds_alternative<bool>(j.value))
    throw std::invalid_argument("glTF expected boolean");
  return std::get<bool>(j.value);
}
std::vector<std::uint8_t> decode_base64(const std::string& uri) {
  const std::string prefix="data:application/octet-stream;base64,";
  const std::string prefix2="data:application/gltf-buffer;base64,";
  const std::string prefix_png="data:image/png;base64,";
  const std::string prefix_jpeg="data:image/jpeg;base64,";
  std::size_t start=uri.starts_with(prefix)?prefix.size() :
                    uri.starts_with(prefix2)?prefix2.size() :
                    uri.starts_with(prefix_png)?prefix_png.size() :
                    uri.starts_with(prefix_jpeg)?prefix_jpeg.size() : 0;
  if(!start) throw std::invalid_argument("unsupported glTF data URI");
  const std::string source=uri.substr(start);
  if(source.empty() || source.size()%4!=0 || source.size()>90'000'000)
    throw std::invalid_argument("invalid or oversized glTF base64 data");
  auto six=[](char ch)->int {
    if(ch>='A'&&ch<='Z')return ch-'A';
    if(ch>='a'&&ch<='z')return ch-'a'+26;
    if(ch>='0'&&ch<='9')return ch-'0'+52;
    if(ch=='+')return 62;
    if(ch=='/')return 63;
    throw std::invalid_argument("invalid glTF base64 digit");
  };
  std::vector<std::uint8_t> out;
  out.reserve(source.size()/4*3);
  for(std::size_t i=0;i<source.size();i+=4){
    bool end=i+4==source.size();
    bool pad2=source[i+2]=='=',pad3=source[i+3]=='=';
    if(((pad2 || pad3) && !end) || (pad2 && !pad3))
      throw std::invalid_argument("invalid glTF base64 padding");
    int a=six(source[i]),b=six(source[i+1]);
    int c=pad2?0:six(source[i+2]),d=pad3?0:six(source[i+3]);
    if((pad2 && (b&15)!=0) || (pad3&&!pad2 && (c&3)!=0))
      throw std::invalid_argument("noncanonical glTF base64 padding");
    out.push_back(static_cast<std::uint8_t>((a<<2)|(b>>4)));
    if(!pad2)out.push_back(static_cast<std::uint8_t>((b<<4)|(c>>2)));
    if(!pad3)out.push_back(static_cast<std::uint8_t>((c<<6)|d));
  }
  return out;
}
std::uint32_t word(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
    (static_cast<std::uint32_t>(p[1])<<8) |
    (static_cast<std::uint32_t>(p[2])<<16) |
    (static_cast<std::uint32_t>(p[3])<<24);
}
float float32(const std::uint8_t* p) {
  const float value=std::bit_cast<float>(word(p));
  if(!std::isfinite(value))throw std::invalid_argument("nonfinite glTF geometry");
  return value;
}
struct View {
  std::size_t buffer=0,offset=0,length=0,stride=0;
};
struct Accessor {
  const std::vector<std::uint8_t>* bytes=nullptr;
  std::size_t start=0,stride=0,count=0;
  std::uint32_t component=0;
  std::string type;
};
std::size_t in_range(std::uint32_t index,std::size_t count,const char* what) {
  if(index>=count)throw std::out_of_range(std::string("glTF index outside ")+what);
  return index;
}
std::size_t component_bytes(std::uint32_t type) {
  switch(type){
    case 5121: return 1;
    case 5123: return 2;
    case 5125: case 5126: return 4;
    default: throw std::invalid_argument("unsupported glTF component type");
  }
}
std::size_t components(const std::string& type){
  if(type=="SCALAR")return 1;
  if(type=="VEC2")return 2;
  if(type=="VEC3")return 3;
  throw std::invalid_argument("unsupported glTF accessor shape");
}
Accessor accessor(const Json::Array& accessors,const std::vector<View>& views,
                  const std::vector<std::vector<std::uint8_t>>& buffers,
                  std::uint32_t index) {
  const auto& o=object(accessors.at(in_range(index,accessors.size(),"accessors")));
  if(optional(o,"sparse") || optional(o,"extensions"))
    throw std::invalid_argument("sparse/extension glTF accessors unsupported");
  const auto view_id=uint(field(o,"bufferView"),"bufferView");
  const auto& view=views.at(in_range(view_id,views.size(),"bufferViews"));
  const auto& bytes=buffers.at(view.buffer);
  const auto type=string(field(o,"type"));
  const auto component=uint(field(o,"componentType"),"componentType");
  const auto count=uint(field(o,"count"),"count");
  const std::size_t element_size=components(type)*component_bytes(component);
  const std::size_t stride=view.stride?view.stride:element_size;
  const std::size_t offset=integer(o,"byteOffset");
  if(!count || count>2'000'000 || offset%component_bytes(component)!=0 ||
     (view.offset+offset)%component_bytes(component)!=0 ||
     stride<element_size || stride%component_bytes(component)!=0 ||
     (view.stride && (stride<4 || stride>252 || stride%4!=0)) ||
     offset>view.length ||
     (static_cast<std::uint64_t>(count-1)*stride+element_size)>view.length-offset)
    throw std::invalid_argument("invalid glTF accessor range or alignment");
  if(const auto* normalized=optional(o,"normalized");normalized && boolean(*normalized))
    throw std::invalid_argument("normalized glTF accessors not supported yet");
  if(view.stride && type=="SCALAR")
    throw std::invalid_argument("strided index accessors not supported");
  return {&bytes,view.offset+offset,stride,count,component,type};
}
std::array<double,3> vector3(const Accessor& a,std::size_t row) {
  if(a.component!=5126 || a.type!="VEC3")
    throw std::invalid_argument("glTF attribute requires FLOAT VEC3");
  const auto* p=a.bytes->data()+a.start+row*a.stride;
  return {float32(p),float32(p+4),float32(p+8)};
}
std::array<double,2> vector2(const Accessor& a,std::size_t row) {
  if(a.component!=5126 || a.type!="VEC2")
    throw std::invalid_argument("glTF UV requires FLOAT VEC2");
  const auto* p=a.bytes->data()+a.start+row*a.stride;
  return {float32(p),float32(p+4)};
}
std::uint32_t index_value(const Accessor& a,std::size_t row){
  if(a.type!="SCALAR" || (a.component!=5121 && a.component!=5123 && a.component!=5125))
    throw std::invalid_argument("glTF indices require unsigned scalar");
  const auto* p=a.bytes->data()+a.start+row*a.stride;
  if(a.component==5121)return p[0];
  if(a.component==5123)return static_cast<std::uint32_t>(p[0]) |
                                (static_cast<std::uint32_t>(p[1])<<8);
  return word(p);
}
math::Vec3 normalized(math::Vec3 n) {
  const double length_sq=n.dot(n);
  if(!(length_sq>1e-24) || !std::isfinite(length_sq))
    throw std::invalid_argument("degenerate glTF triangle or normal");
  return n/std::sqrt(length_sq);
}

math::Mat4 node_transform(const Json::Object& o) {
  const auto* matrix=optional(o,"matrix");
  const auto* t=optional(o,"translation");
  const auto* r=optional(o,"rotation");
  const auto* s=optional(o,"scale");
  if(matrix && (t || r || s))
    throw std::invalid_argument("glTF node matrix and TRS are mutually exclusive");
  if(matrix) {
    const auto& values=array(*matrix);
    if(values.size()!=16)throw std::invalid_argument("glTF node matrix needs 16 scalars");
    math::Mat4 m{};
    for(std::size_t i=0;i<16;++i)m.values[i]=number(values[i]);
    constexpr double eps=1e-8;
    if(std::abs(m.at(3,0))>eps || std::abs(m.at(3,1))>eps ||
       std::abs(m.at(3,2))>eps || std::abs(m.at(3,3)-1.0)>eps)
      throw std::invalid_argument("glTF node transform must be affine");
    return m;
  }
  math::Vec3 translation{},scale{1,1,1};
  if(t){
    const auto& v=array(*t);
    if(v.size()!=3)throw std::invalid_argument("glTF translation needs 3 numbers");
    translation={number(v[0]),number(v[1]),number(v[2])};
  }
  if(s){
    const auto& v=array(*s);
    if(v.size()!=3)throw std::invalid_argument("glTF scale needs 3 numbers");
    scale={number(v[0]),number(v[1]),number(v[2])};
    if(scale.x==0 || scale.y==0 || scale.z==0)
      throw std::invalid_argument("glTF node scale cannot be zero");
  }
  math::Mat4 rotation=math::Mat4::identity();
  if(r){
    const auto& v=array(*r);
    if(v.size()!=4)throw std::invalid_argument("glTF quaternion needs [x,y,z,w]");
    double x=number(v[0]),y=number(v[1]),z=number(v[2]),w=number(v[3]);
    const double n=x*x+y*y+z*z+w*w;
    if(!(n>1e-18) || !std::isfinite(n))
      throw std::invalid_argument("invalid glTF quaternion");
    const double inv=1.0/std::sqrt(n);
    x*=inv;y*=inv;z*=inv;w*=inv;
    rotation.at(0,0)=1-2*(y*y+z*z);
    rotation.at(0,1)=2*(x*y-z*w);
    rotation.at(0,2)=2*(x*z+y*w);
    rotation.at(1,0)=2*(x*y+z*w);
    rotation.at(1,1)=1-2*(x*x+z*z);
    rotation.at(1,2)=2*(y*z-x*w);
    rotation.at(2,0)=2*(x*z-y*w);
    rotation.at(2,1)=2*(y*z+x*w);
    rotation.at(2,2)=1-2*(x*x+y*y);
  }
  return math::Mat4::translation(translation)*rotation*math::Mat4::scaling(scale);
}
void validate_affine(const math::Mat4& m) {
  const double det =
      m.at(0,0)*(m.at(1,1)*m.at(2,2)-m.at(1,2)*m.at(2,1)) -
      m.at(0,1)*(m.at(1,0)*m.at(2,2)-m.at(1,2)*m.at(2,0)) +
      m.at(0,2)*(m.at(1,0)*m.at(2,1)-m.at(1,1)*m.at(2,0));
  if(!std::isfinite(det) || std::abs(det)<1e-14)
    throw std::invalid_argument("singular glTF node world transform");
}
void import_nodes(const Json::Object& doc,GltfModel& result) {
  const auto* nodes_field=optional(doc,"nodes");
  if(!nodes_field){
    // A mesh-only glTF document remains supported by the viewer.
    result.nodes.push_back({"Implicit mesh",math::Mat4::identity(),
                            math::Mat4::identity(),std::nullopt,0,{},true});
    result.instances.push_back({0,0,math::Mat4::identity()});
    return;
  }
  const auto& input=array(*nodes_field);
  if(input.empty() || input.size()>4096)
    throw std::invalid_argument("glTF scene needs 1..4096 nodes");
  result.nodes.reserve(input.size());
  for(std::size_t i=0;i<input.size();++i){
    const auto& o=object(input[i]);
    GltfNode node;
    if(const auto* label=optional(o,"name"))node.name=string(*label);
    if(node.name.empty())node.name="Node "+std::to_string(i);
    if(optional(o,"skin") || optional(o,"weights") || optional(o,"extensions"))
      throw std::invalid_argument("glTF skinned, morphed or extended nodes unsupported");
    node.local=node_transform(o);
    if(const auto* mesh=optional(o,"mesh")){
      const auto idx=uint(*mesh,"mesh");
      if(idx>=result.meshes.size())
        throw std::invalid_argument("glTF node references invalid mesh index");
      node.mesh_index=idx;
    }
    if(const auto* children=optional(o,"children")){
      const auto& values=array(*children);
      if(values.size()>4096)throw std::length_error("glTF node has too many children");
      for(const auto& child:values)
        node.children.push_back(uint(child,"child"));
    }
    result.nodes.push_back(std::move(node));
  }
  for(std::size_t i=0;i<result.nodes.size();++i){
    for(const auto child:result.nodes[i].children){
      if(child>=result.nodes.size() || child==i)
        throw std::invalid_argument("glTF node refers to itself or invalid child");
      auto& parent=result.nodes[child].parent;
      if(parent.has_value())
        throw std::invalid_argument("glTF node has multiple parents");
      parent=static_cast<std::uint32_t>(i);
    }
  }
  // Validate the entire graph, not only the selected scene. Iterative parent
  // walk prevents untrusted JSON from causing an unbounded recursive call.
  for(std::size_t i=0;i<result.nodes.size();++i){
    std::size_t steps=0;
    auto cursor=std::optional<std::uint32_t>(static_cast<std::uint32_t>(i));
    while(cursor){
      if(++steps>result.nodes.size())
        throw std::invalid_argument("cycle in glTF hierarchy");
      cursor=result.nodes[*cursor].parent;
    }
  }
  std::vector<std::uint32_t> roots;
  if(const auto* scenes=optional(doc,"scenes")){
    const auto& options=array(*scenes);
    if(options.empty())throw std::invalid_argument("glTF scenes cannot be empty");
    const auto scene=integer(doc,"scene");
    if(scene>=options.size())throw std::out_of_range("selected glTF scene missing");
    const auto& selected=object(options[scene]);
    if(const auto* scene_roots=optional(selected,"nodes"))
      for(const auto& value:array(*scene_roots))
        roots.push_back(uint(value,"scene root"));
  }else{
    if(optional(doc,"scene"))
      throw std::invalid_argument("glTF scene index without scenes");
    for(std::size_t i=0;i<result.nodes.size();++i)
      if(!result.nodes[i].parent)
        roots.push_back(static_cast<std::uint32_t>(i));
  }
  if(roots.empty())throw std::invalid_argument("glTF scene has no roots");
  std::vector<bool> active(result.nodes.size(),false);
  std::function<void(std::uint32_t,const math::Mat4&,std::size_t)> visit =
      [&](std::uint32_t id,const math::Mat4& parent,std::size_t depth){
    if(id>=result.nodes.size() || active[id] || depth>result.nodes.size())
      throw std::invalid_argument("duplicate or invalid glTF scene node");
    auto& node=result.nodes[id];
    // A scene root must actually be a root of the hierarchy.
    active[id]=true;
    node.world=parent*node.local;
    validate_affine(node.world);
    node.in_scene=true;
    if(node.mesh_index)
      result.instances.push_back({id,*node.mesh_index,node.world});
    for(auto child:node.children)visit(child,node.world,depth+1);
  };
  for(const auto id:roots){
    if(id>=result.nodes.size() || result.nodes[id].parent)
      throw std::invalid_argument("glTF scene root has parent or invalid index");
    visit(id,math::Mat4::identity(),0);
  }
  if(result.instances.empty())throw std::invalid_argument("selected glTF scene contains no mesh");
}
TextureMinFilter texture_min(std::size_t value){
  switch(value){
    case 9728:return TextureMinFilter::Nearest;
    case 9729:return TextureMinFilter::Linear;
    case 9984:return TextureMinFilter::NearestMipNearest;
    case 9985:return TextureMinFilter::LinearMipNearest;
    case 9986:return TextureMinFilter::NearestMipLinear;
    case 9987:return TextureMinFilter::LinearMipLinear;
    default:throw std::invalid_argument("unsupported glTF minFilter");
  }
}
TextureMagFilter texture_mag(std::size_t value){
  if(value==9728)return TextureMagFilter::Nearest;
  if(value==9729)return TextureMagFilter::Linear;
  throw std::invalid_argument("unsupported glTF magFilter");
}
TextureWrap texture_wrap(std::size_t value){
  if(value==10497)return TextureWrap::Repeat;
  if(value==33071)return TextureWrap::ClampToEdge;
  if(value==33648)return TextureWrap::MirroredRepeat;
  throw std::invalid_argument("unsupported glTF texture wrapping");
}
} // namespace

GltfModel import_gltf(const std::string& json_text,
                      const GltfBufferResolver& external_buffer,
                      const std::vector<std::uint8_t>* embedded_binary) {
  if(json_text.empty() || json_text.size()>16'000'000)
    throw std::invalid_argument("glTF JSON exceeds 16 MB limit");
  const Json root=terraflux::json::Parser(json_text).parse_document();
  const auto& doc=object(root);
  const auto& asset=object(field(doc,"asset"));
  if(string(field(asset,"version"))!="2.0")
    throw std::invalid_argument("glTF 2.0 required");
  if(const auto* required=optional(doc,"extensionsRequired");required && !array(*required).empty())
    throw std::invalid_argument("glTF required extensions unsupported");
  const auto& buffers_json=array(field(doc,"buffers"));
  const auto& views_json=array(field(doc,"bufferViews"));
  const auto& accessors=array(field(doc,"accessors"));
  const auto& meshes=array(field(doc,"meshes"));
  if(buffers_json.empty() || buffers_json.size()>16 || views_json.size()>100'000 ||
     accessors.size()>100'000 || meshes.empty() || meshes.size()>128)
    throw std::invalid_argument("glTF requires 1..128 meshes and 1..16 buffers");
  std::vector<std::vector<std::uint8_t>> buffers;
  buffers.reserve(buffers_json.size());
  for(std::size_t buffer_index=0;buffer_index<buffers_json.size();++buffer_index){
    const auto& entry=buffers_json[buffer_index];
    const auto& o=object(entry);
    const auto length=uint(field(o,"byteLength"),"byteLength");
    if(length==0 || length>64'000'000)throw std::invalid_argument("glTF buffer size invalid");
    const auto* supplied_uri=optional(o,"uri");
    std::vector<std::uint8_t> bytes;
    if(!supplied_uri){
      if(!embedded_binary || buffer_index!=0)
        throw std::invalid_argument("missing glTF buffer URI requires GLB BIN chunk");
      bytes=*embedded_binary;
      if(bytes.size()<length || bytes.size()-length>3)
        throw std::invalid_argument("GLB BIN chunk length or padding invalid");
    }else{
      const auto uri=string(*supplied_uri);
      bytes=uri.starts_with("data:")?decode_base64(uri):
            external_buffer?external_buffer(uri):std::vector<std::uint8_t>{};
    }
    if(bytes.size()<length || bytes.size()>64'000'000)
      throw std::invalid_argument("glTF buffer URI unresolved or truncated");
    buffers.push_back(std::move(bytes));
  }
  std::vector<View> views;
  views.reserve(views_json.size());
  for(const auto& entry:views_json){
    const auto& o=object(entry);
    const auto buf=uint(field(o,"buffer"),"buffer");
    const auto offset=integer(o,"byteOffset"),length=uint(field(o,"byteLength"),"byteLength");
    const auto stride=integer(o,"byteStride");
    const auto& data=buffers.at(in_range(buf,buffers.size(),"buffers"));
    if(length==0 || offset>data.size() || length>data.size()-offset)
      throw std::invalid_argument("glTF bufferView out of range");
    views.push_back({buf,offset,length,stride});
  }

  GltfModel result;
  // Images can reference external paths, data URIs, or an embedded bufferView.
  // Decode once per image index; materials retain references to these keys.
  const Json::Array* image_list=nullptr;
  if(const auto* p=optional(doc,"images"))image_list=&array(*p);
  const Json::Array* texture_list=nullptr;
  if(const auto* p=optional(doc,"textures"))texture_list=&array(*p);
  const Json::Array* samplers=nullptr;
  if(const auto* p=optional(doc,"samplers"))samplers=&array(*p);
  if((image_list && image_list->size()>256) ||
     (texture_list && texture_list->size()>256) ||
     (samplers && samplers->size()>256))
    throw std::invalid_argument("too many glTF images/textures/samplers");
  if(image_list)for(std::size_t i=0;i<image_list->size();++i){
    const auto& img=object((*image_list)[i]);
    const auto* mime_ptr=optional(img,"mimeType");
    const std::string mime=mime_ptr?string(*mime_ptr):"";
    const auto* uri_ptr=optional(img,"uri");
    const auto* view_ptr=optional(img,"bufferView");
    if((uri_ptr!=nullptr)==(view_ptr!=nullptr))
      throw std::invalid_argument("glTF image requires exactly one of uri or bufferView");
    std::vector<std::uint8_t> encoded;
    if(uri_ptr){
      const auto uri=string(*uri_ptr);
      encoded=uri.starts_with("data:")?decode_base64(uri):
              external_buffer?external_buffer(uri):std::vector<std::uint8_t>{};
    }else{
      if(mime.empty())
        throw std::invalid_argument("glTF bufferView image requires mimeType");
      const auto index=uint(*view_ptr,"image bufferView");
      const auto& view=views.at(in_range(index,views.size(),"image bufferViews"));
      const auto& data=buffers.at(view.buffer);
      encoded.assign(data.begin()+static_cast<std::ptrdiff_t>(view.offset),
                     data.begin()+static_cast<std::ptrdiff_t>(view.offset+view.length));
    }
    const auto key="gltf-image-"+std::to_string(i);
    result.images.emplace(key,decode_image(encoded,mime));
  }
  // Images are decoded by shared image index; texture objects can alias
  // the same source. Sampler overrides are limited to repeat/no-mip modes.
  std::vector<std::string> image_for_texture;
  std::vector<TextureSampling> sampling_for_texture;
  if(texture_list)for(const auto& item:*texture_list){
    const auto& tex=object(item);
    if(optional(tex,"extensions"))
      throw std::invalid_argument("compressed/extended glTF texture unsupported");
    const auto index=uint(field(tex,"source"),"texture source");
    if(!image_list || index>=image_list->size())
      throw std::invalid_argument("glTF texture references missing image");
    // glTF's defaults are LINEAR mag, LINEAR_MIPMAP_LINEAR min and REPEAT wrap.
    TextureSampling sampling{TextureMinFilter::LinearMipLinear,
                             TextureMagFilter::Linear,
                             TextureWrap::Repeat,TextureWrap::Repeat};
    if(const auto* sampler_id=optional(tex,"sampler")) {
      const auto idx=uint(*sampler_id,"sampler");
      if(!samplers || idx>=samplers->size())
        throw std::invalid_argument("glTF texture references missing sampler");
      const auto& sampler=object((*samplers)[idx]);
      sampling.min=texture_min(integer(sampler,"minFilter",9987));
      sampling.mag=texture_mag(integer(sampler,"magFilter",9729));
      sampling.wrap_s=texture_wrap(integer(sampler,"wrapS",10497));
      sampling.wrap_t=texture_wrap(integer(sampler,"wrapT",10497));
    }
    sampling_for_texture.push_back(sampling);
    image_for_texture.push_back("gltf-image-"+std::to_string(index));
  }
  const Json::Array* materials=nullptr;
  if(const auto* p=optional(doc,"materials"))materials=&array(*p);
  if(materials && materials->size()>4096)
    throw std::invalid_argument("too many glTF materials");
  if(materials)for(std::size_t i=0;i<materials->size();++i){
    const auto& o=object((*materials)[i]);
    Material m;
    if(const auto* pbr=optional(o,"pbrMetallicRoughness")){
      const auto& po=object(*pbr);
      if(const auto* factor=optional(po,"metallicFactor"))
        m.metallic_factor=number(*factor);
      if(const auto* factor=optional(po,"roughnessFactor"))
        m.roughness_factor=number(*factor);
      if(!std::isfinite(m.metallic_factor) || !std::isfinite(m.roughness_factor) ||
         m.metallic_factor<0 || m.metallic_factor>1 ||
         m.roughness_factor<0 || m.roughness_factor>1)
        throw std::invalid_argument("glTF PBR factors must be in [0,1]");
      if(const auto* texture=optional(po,"metallicRoughnessTexture")) {
        const auto& info=object(*texture);
        const auto index=uint(field(info,"index"),"metallicRoughnessTexture index");
        if(index>=image_for_texture.size() || integer(info,"texCoord")!=0 ||
           optional(info,"extensions"))
          throw std::invalid_argument("metallicRoughnessTexture requires valid TEXCOORD_0");
        m.metallic_roughness_map=image_for_texture[index];
        m.metallic_roughness_sampling=sampling_for_texture[index];
      }
      if(const auto* texture=optional(po,"baseColorTexture")){
        const auto& info=object(*texture);
        const auto index=uint(field(info,"index"),"baseColorTexture index");
        if(index>=image_for_texture.size())
          throw std::invalid_argument("glTF baseColorTexture index out of range");
        if(integer(info,"texCoord")!=0 || optional(info,"extensions"))
          throw std::invalid_argument("only baseColorTexture TEXCOORD_0 supported");
        m.albedo_map=image_for_texture[index];
        m.albedo_sampling=sampling_for_texture[index];
      }
      if(const auto* color=optional(po,"baseColorFactor")){
        const auto& values=array(*color);
        if(values.size()!=4)throw std::invalid_argument("glTF baseColorFactor must be RGBA");
        std::array<double,4> rgba{};
        for(std::size_t j=0;j<4;++j){
          rgba[j]=number(values[j]);
          if(rgba[j]<0 || rgba[j]>1)
            throw std::invalid_argument("glTF material factor outside [0,1]");
        }
        if(rgba[3]!=1.0)throw std::invalid_argument("transparent glTF materials unsupported");
        m.base_color={rgba[0],rgba[1],rgba[2]};
      }
    }
    if(const auto* normal=optional(o,"normalTexture")) {
      const auto& info=object(*normal);
      const auto index=uint(field(info,"index"),"normalTexture index");
      if(index>=image_for_texture.size() || integer(info,"texCoord")!=0 ||
         optional(info,"extensions"))
        throw std::invalid_argument("normalTexture requires valid TEXCOORD_0");
      m.normal_map=image_for_texture[index];
      m.normal_sampling=sampling_for_texture[index];
      if(const auto* scale=optional(info,"scale"))
        m.normal_scale=number(*scale);
      if(!std::isfinite(m.normal_scale) || m.normal_scale<0 || m.normal_scale>10)
        throw std::invalid_argument("normalTexture scale must be within [0,10]");
    }
    if(optional(o,"occlusionTexture") ||
       optional(o,"emissiveTexture") || optional(o,"extensions"))
      throw std::invalid_argument("unsupported glTF material texture or extension");
    if(const auto* alpha=optional(o,"alphaMode");alpha && string(*alpha)!="OPAQUE")
      throw std::invalid_argument("transparent glTF materials unsupported");
    result.materials.emplace("gltf-material-"+std::to_string(i),m);
  }

  result.meshes.reserve(meshes.size());
  std::size_t total_vertices=0,total_indices=0;
  for(const auto& mesh_entry:meshes) {
  Mesh mesh;
  const auto& mesh_o=object(mesh_entry);
  const auto& primitives=array(field(mesh_o,"primitives"));
  if(primitives.empty() || primitives.size()>4096)
    throw std::invalid_argument("glTF has no drawable primitives");
  for(const auto& item:primitives) {
    const auto& primitive=object(item);
    if(integer(primitive,"mode",4)!=4)
      throw std::invalid_argument("only glTF TRIANGLES primitive mode supported");
    if(optional(primitive,"targets") || optional(primitive,"extensions"))
      throw std::invalid_argument("glTF primitive morph targets/extensions unsupported");
    const auto& attributes=object(field(primitive,"attributes"));
    const auto positions=accessor(accessors,views,buffers,
                                  uint(field(attributes,"POSITION"),"POSITION"));
    if(positions.component!=5126 || positions.type!="VEC3")
      throw std::invalid_argument("POSITION must be FLOAT VEC3");
    const auto has_normals=optional(attributes,"NORMAL")!=nullptr;
    const auto has_uv=optional(attributes,"TEXCOORD_0")!=nullptr;
    Accessor normal{},uv{};
    if(has_normals)normal=accessor(accessors,views,buffers,
                                    uint(field(attributes,"NORMAL"),"NORMAL"));
    if(has_uv)uv=accessor(accessors,views,buffers,
                          uint(field(attributes,"TEXCOORD_0"),"TEXCOORD_0"));
    if((has_normals && (normal.count!=positions.count || normal.component!=5126 || normal.type!="VEC3")) ||
       (has_uv && (uv.count!=positions.count || uv.component!=5126 || uv.type!="VEC2")))
      throw std::invalid_argument("mismatched glTF attribute count/type");
    std::size_t index_count=positions.count;
    Accessor indices{};
    const bool indexed=optional(primitive,"indices")!=nullptr;
    if(indexed){
      indices=accessor(accessors,views,buffers,
                       uint(field(primitive,"indices"),"indices"));
      index_count=indices.count;
    }
    if(index_count<3 || index_count%3!=0)
      throw std::invalid_argument("glTF triangles require index count multiple of 3");
    const auto base=mesh.vertices.size();
    total_vertices+=positions.count;
    total_indices+=index_count;
    if(base+positions.count>2'000'000 || mesh.indices.size()+index_count>6'000'000 ||
       total_vertices+positions.count>2'000'000 ||
       total_indices+index_count>6'000'000)
      throw std::length_error("glTF geometry exceeds resource limit");
    for(std::size_t i=0;i<positions.count;++i) {
      const auto p=vector3(positions,i);
      math::Vec3 n{0,0,0};
      if(has_normals){const auto value=vector3(normal,i);
        n=normalized({value[0],value[1],value[2]});}
      math::Vec2 texture{};
      if(has_uv){const auto value=vector2(uv,i);texture={value[0],value[1]};}
      mesh.vertices.push_back({{p[0],p[1],p[2]},n,texture});
    }
    for(std::size_t i=0;i<index_count;++i){
      const auto local=indexed?index_value(indices,i):static_cast<std::uint32_t>(i);
      if(local>=positions.count)throw std::invalid_argument("glTF triangle index out of range");
      mesh.indices.push_back(static_cast<std::uint32_t>(base+local));
    }
    // Synthesize smooth normals only when absent, using area-weighted faces.
    if(!has_normals){
      for(std::size_t i=mesh.indices.size()-index_count;i<mesh.indices.size();i+=3){
        const auto a=mesh.indices[i],b=mesh.indices[i+1],
                   c=mesh.indices[i+2];
        const auto& va=mesh.vertices[a].position;
        const auto& vb=mesh.vertices[b].position;
        const auto& vc=mesh.vertices[c].position;
        const auto cross=(vb-va).cross(vc-va);
        if(!(cross.dot(cross)>1e-24))
          throw std::invalid_argument("degenerate glTF triangle");
        mesh.vertices[a].normal=mesh.vertices[a].normal+cross;
        mesh.vertices[b].normal=mesh.vertices[b].normal+cross;
        mesh.vertices[c].normal=mesh.vertices[c].normal+cross;
      }
      for(std::size_t i=base;i<mesh.vertices.size();++i)
        mesh.vertices[i].normal=normalized(mesh.vertices[i].normal);
    }
    std::string material_name;
    if(const auto* material=optional(primitive,"material")){
      const auto id=uint(*material,"material");
      if(!materials || id>=materials->size())
        throw std::invalid_argument("glTF material index out of range");
      material_name="gltf-material-"+std::to_string(id);
    }
    mesh.sections.push_back({static_cast<std::uint32_t>(
       mesh.indices.size()-index_count),static_cast<std::uint32_t>(index_count),material_name});
  }
  result.meshes.push_back(std::move(mesh));
  }
  import_nodes(doc,result);
  return result;
}

GltfModel import_glb(std::span<const std::uint8_t> bytes,
                     const GltfBufferResolver& external_buffer) {
  if(bytes.size()<20 || bytes.size()>80'000'000 || bytes.size()%4!=0)
    throw std::invalid_argument("invalid glb container byte size");
  const auto u32=[&](std::size_t pos)->std::uint32_t{
    if(pos>bytes.size() || bytes.size()-pos<4)
      throw std::invalid_argument("truncated glb chunk header");
    return std::uint32_t(bytes[pos]) | (std::uint32_t(bytes[pos+1])<<8) |
           (std::uint32_t(bytes[pos+2])<<16) | (std::uint32_t(bytes[pos+3])<<24);
  };
  constexpr std::uint32_t magic=0x46546c67, json_type=0x4e4f534a,bin_type=0x004e4942;
  if(u32(0)!=magic || u32(4)!=2 || u32(8)!=bytes.size())
    throw std::invalid_argument("glb magic, version or declared size invalid");
  const std::size_t json_len=u32(12);
  if(u32(16)!=json_type || !json_len || json_len>16'000'000 ||
     json_len%4!=0 || json_len>bytes.size()-20)
    throw std::invalid_argument("glb requires valid first JSON chunk");
  const std::string json_text(reinterpret_cast<const char*>(bytes.data()+20),json_len);
  std::size_t offset=20+json_len;
  std::vector<std::uint8_t> binary;
  const std::vector<std::uint8_t>* embedded=nullptr;
  if(offset<bytes.size()){
    if(bytes.size()-offset<8)
      throw std::invalid_argument("truncated glb BIN chunk header");
    const std::size_t bin_len=u32(offset);
    if(u32(offset+4)!=bin_type || bin_len%4!=0 ||
       bin_len>64'000'000 || bin_len!=bytes.size()-offset-8)
      throw std::invalid_argument("unsupported or malformed glb BIN chunk");
    binary.assign(bytes.begin()+static_cast<std::ptrdiff_t>(offset+8),bytes.end());
    embedded=&binary;
  }
  return import_gltf(json_text,external_buffer,embedded);
}

} // namespace terraflux::assets
