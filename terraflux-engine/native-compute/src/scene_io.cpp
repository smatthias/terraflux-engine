#include <terraflux/scene_io.hpp>
#include <terraflux/json.hpp>

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::scene {
namespace {
constexpr std::size_t kMaxNodes=4096;
constexpr std::size_t kMaxJsonBytes=2'000'000;
using terraflux::json::Json;
const Json::Array& array(const Json& value) {
  if(!std::holds_alternative<Json::Array>(value.value))
    throw std::invalid_argument("scene field must be an array");
  return std::get<Json::Array>(value.value);
}
bool bool_value(const Json& value) {
  if(!std::holds_alternative<bool>(value.value))
    throw std::invalid_argument("scene mesh_instance must be boolean");
  return std::get<bool>(value.value);
}
std::uint32_t node_id(const Json& value,std::size_t upper) {
  const double n=terraflux::json::number(value);
  if(n<0 || n>=static_cast<double>(upper) || std::trunc(n)!=n)
    throw std::invalid_argument("scene parent node ID invalid");
  return static_cast<std::uint32_t>(n);
}
math::Vec3 vec3(const Json::Object& object,const std::string& field) {
  const auto& components=array(terraflux::json::field(object,field));
  if(components.size()!=3)throw std::invalid_argument("scene TRS vector needs 3 components");
  return {terraflux::json::number(components[0]),
          terraflux::json::number(components[1]),
          terraflux::json::number(components[2])};
}
std::string escaped(const std::string& text) {
  std::string result;
  result.reserve(text.size()+8);
  const char* hex="0123456789abcdef";
  result+='"';
  for(unsigned char c:text) {
    if(c=='"' || c=='\\'){result+='\\';result+=static_cast<char>(c);}
    else if(c=='\n')result+="\\n";
    else if(c=='\r')result+="\\r";
    else if(c=='\t')result+="\\t";
    else if(c<0x20){
      result+="\\u00";
      result+=hex[c>>4];result+=hex[c&15];
    }else result+=static_cast<char>(c);
  }
  result+='"';
  return result;
}
void vec3_out(std::ostream& out,math::Vec3 vec) {
  out<<'['<<vec.x<<','<<vec.y<<','<<vec.z<<']';
}
} // namespace

std::string serialize_scene(const Scene& scene) {
  if(scene.size()==0 || scene.size()>kMaxNodes)
    throw std::invalid_argument("scene node count exceeds serialization limit");
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out<<std::setprecision(std::numeric_limits<double>::max_digits10);
  out<<"{\n  \"format\":\"terraflux.scene\",\n  \"version\":"
     <<(scene.mesh_assets().empty()?1:2)<<",\n";
  if(!scene.mesh_assets().empty()) {
    out<<"  \"assets\":{";
    bool first=true;
    for(const auto& [name,path]:scene.mesh_assets()) {
      if(!first)out<<',';
      out<<escaped(name)<<':'<<escaped(path);
      first=false;
    }
    out<<"},\n";
  }
  out<<"  \"nodes\":[\n";
  for(std::size_t id=0;id<scene.size();++id){
    const auto& node=scene.get(static_cast<NodeId>(id));
    if(node.name.empty() || node.name.size()>256)
      throw std::invalid_argument("scene node name length invalid");
    (void)node.local.matrix();
    if(!scene.mesh_assets().empty() && node.mesh_instance && !node.mesh_asset)
      throw std::invalid_argument("asset scenes require named references on all mesh instances");
    if(id==0 && node.mesh_instance)
      throw std::invalid_argument("scene root cannot be drawable");
    out<<"    {\"name\":"<<escaped(node.name)
       <<",\"parent\":"<<node.parent
       <<",\"mesh_instance\":"<<(node.mesh_instance?"true":"false")
       <<",\"translation\":";
    vec3_out(out,node.local.translation);
    out<<",\"rotation\":";
    vec3_out(out,node.local.rotation_radians);
    out<<",\"scale\":";
    vec3_out(out,node.local.scale);
    if(node.mesh_asset)out<<",\"mesh_asset\":"<<escaped(*node.mesh_asset);
    if(node.angular_velocity) {
      out<<",\"spin\":";
      vec3_out(out,*node.angular_velocity);
    }
    if(node.linear_velocity) {
      out<<",\"motion\":";
      vec3_out(out,*node.linear_velocity);
    }
    out<<'}'<<(id+1<scene.size()?",":"")<<'\n';
  }
  out<<"  ]\n}\n";
  const auto encoded=out.str();
  if(encoded.size()>kMaxJsonBytes)
    throw std::length_error("serialized scene exceeds 2MB");
  return encoded;
}

Scene deserialize_scene(const std::string& json_text) {
  if(json_text.empty() || json_text.size()>kMaxJsonBytes)
    throw std::invalid_argument("scene JSON is empty or exceeds 2MB");
  const auto root=terraflux::json::Parser(json_text).parse_document();
  const auto& doc=terraflux::json::object(root);
  const auto version=terraflux::json::number(terraflux::json::field(doc,"version"));
  if(terraflux::json::string(terraflux::json::field(doc,"format"))!="terraflux.scene" ||
     (version!=1 && version!=2))
    throw std::invalid_argument("unsupported TerraFlux scene document");
  const auto& nodes=array(terraflux::json::field(doc,"nodes"));
  if(nodes.empty() || nodes.size()>kMaxNodes)
    throw std::invalid_argument("scene node count must be 1..4096");
  Scene scene;
  if(doc.contains("assets")) {
    if(version!=2)throw std::invalid_argument("scene assets require version 2");
    const auto& assets=terraflux::json::object(doc.at("assets"));
    for(const auto& [name,path]:assets)
      scene.declare_mesh_asset(name,terraflux::json::string(path));
  }
  std::vector<NodeId> parents(nodes.size());
  std::vector<Transform> transforms(nodes.size());
  std::vector<bool> mesh_flags(nodes.size());
  std::vector<std::optional<std::string>> mesh_assets(nodes.size());
  std::vector<std::optional<math::Vec3>> spin_components(nodes.size());
  std::vector<std::optional<math::Vec3>> motion_components(nodes.size());
  for(std::size_t id=0;id<nodes.size();++id) {
    const auto& obj=terraflux::json::object(nodes[id]);
    const auto name=terraflux::json::string(terraflux::json::field(obj,"name"));
    if(name.empty() || name.size()>256)
      throw std::invalid_argument("scene node name length invalid");
    parents[id]=node_id(terraflux::json::field(obj,"parent"),nodes.size());
    transforms[id]={
      vec3(obj,"translation"),
      vec3(obj,"rotation"),
      vec3(obj,"scale")
    };
    (void)transforms[id].matrix();
    const auto it=obj.find("mesh_instance");
    mesh_flags[id]=it!=obj.end() && bool_value(it->second);
    if(obj.contains("mesh_asset")) {
      if(version!=2)throw std::invalid_argument("mesh_asset requires scene version 2");
      mesh_assets[id]=terraflux::json::string(obj.at("mesh_asset"));
      if(!scene.mesh_assets().contains(*mesh_assets[id]))
        throw std::invalid_argument("unknown scene asset reference");
      if(it!=obj.end() && !mesh_flags[id])
        throw std::invalid_argument("mesh_asset conflicts with mesh_instance false");
      mesh_flags[id]=true;
    }
    if(version==2 && mesh_flags[id] && !mesh_assets[id])
      throw std::invalid_argument("version 2 mesh instances require mesh_asset");
    if(obj.contains("spin")) {
      auto v=vec3(obj,"spin");
      // Validate before modifying the scene.
      if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
         std::abs(v.x)>1000 || std::abs(v.y)>1000 || std::abs(v.z)>1000)
        throw std::invalid_argument("invalid Spin component");
      spin_components[id]=v;
    }
    if(obj.contains("motion"))motion_components[id]=vec3(obj,"motion");
    if(id==0){
      if(name!="Root" || parents[id]!=0 || mesh_flags[id] || spin_components[id] || motion_components[id])
        throw std::invalid_argument("scene root must have name Root, parent 0 and no mesh or runtime components");
    }else{
      scene.create_node(name);
      if(parents[id]==id)
        throw std::invalid_argument("scene node cannot parent itself");
    }
  }
  // Two-pass parenting allows an existing node to be reparented to a node
  // created later, while Scene::set_parent guards arbitrary cycles.
  for(std::size_t id=1;id<nodes.size();++id)
    scene.set_parent(static_cast<NodeId>(id),parents[id]);
  for(std::size_t id=0;id<nodes.size();++id){
    scene.set_transform(static_cast<NodeId>(id),transforms[id]);
    if(mesh_flags[id])scene.set_mesh_instance(static_cast<NodeId>(id),true);
    if(mesh_assets[id])scene.set_mesh_asset(static_cast<NodeId>(id),*mesh_assets[id]);
    if(motion_components[id])
      scene.set_linear_velocity(static_cast<NodeId>(id),motion_components[id]);
    if(spin_components[id])
      scene.set_angular_velocity(static_cast<NodeId>(id),spin_components[id]);
  }
  return scene;
}

}  // namespace terraflux::scene
