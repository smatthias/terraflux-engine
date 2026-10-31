#include <terraflux/residency_state.hpp>
#include <terraflux/scene_io.hpp>
#include <terraflux/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace terraflux::runtime {
namespace {
void validate(const ResidencyState& state) {
  if(state.scene_signature.size()!=16 || !std::all_of(state.scene_signature.begin(),state.scene_signature.end(),
      [](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}))
    throw std::invalid_argument("invalid residency scene signature");
  if(state.inactive.size()>4095)throw std::invalid_argument("too many inactive nodes");
  std::set<scene::NodeId> ids;
  for(auto id:state.inactive)
    if(!id || id>=4096 || !ids.insert(id).second)throw std::invalid_argument("invalid or duplicate inactive node");
  if(state.selected && (!*state.selected || *state.selected>=4096))
    throw std::invalid_argument("invalid selected node");
}
scene::NodeId id(const json::Json& value) {
  const auto number=json::number(value);
  if(!std::isfinite(number) || number<1 || number>=4096 || std::trunc(number)!=number)
    throw std::invalid_argument("invalid residency node ID");
  return static_cast<scene::NodeId>(number);
}
}
std::string residency_signature(const scene::Scene& graph,const std::string& primary_asset) {
  const auto canonical=scene::serialize_scene(graph)+"\nprimary:"+primary_asset;
  std::uint64_t hash=14695981039346656037ULL;
  for(unsigned char c:canonical){hash^=c;hash*=1099511628211ULL;}
  std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
  return out.str();
}
std::string serialize_residency_state(const ResidencyState& state) {
  validate(state);
  auto inactive=state.inactive;std::sort(inactive.begin(),inactive.end());
  std::ostringstream out;
  out<<"{\"format\":\"terraflux.residency\",\"version\":1,\"scene_signature\":\""
     <<state.scene_signature<<"\",\"selected\":";
  if(state.selected)out<<*state.selected;else out<<"null";
  out<<",\"inactive\":[";
  for(std::size_t i=0;i<inactive.size();++i){if(i)out<<',';out<<inactive[i];}
  out<<"]}\n";return out.str();
}
ResidencyState deserialize_residency_state(const std::string& text) {
  if(text.empty() || text.size()>65536)throw std::invalid_argument("residency state exceeds size bound");
  const auto document=json::Parser(text).parse_document();const auto& object=json::object(document);
  if(json::string(json::field(object,"format"))!="terraflux.residency" ||
     json::number(json::field(object,"version"))!=1)throw std::invalid_argument("unsupported residency state");
  ResidencyState result;
  result.scene_signature=json::string(json::field(object,"scene_signature"));
  const auto& selected=json::field(object,"selected");
  if(!std::holds_alternative<std::nullptr_t>(selected.value))result.selected=id(selected);
  const auto& inactive=json::field(object,"inactive");
  if(!std::holds_alternative<json::Json::Array>(inactive.value))throw std::invalid_argument("inactive must be an array");
  for(const auto& value:std::get<json::Json::Array>(inactive.value))result.inactive.push_back(id(value));
  validate(result);return result;
}
void validate_residency_state(const ResidencyState& state,const scene::Scene& graph,const std::string& primary_asset) {
  validate(state);
  if(state.scene_signature!=residency_signature(graph,primary_asset))
    throw std::invalid_argument("residency state does not match authored scene");
  const auto binding=[&](scene::NodeId node){
    if(!graph.get(node).mesh_instance)throw std::invalid_argument("state references a nonbinding scene node");
  };
  for(auto node:state.inactive)binding(node);
  if(state.selected)binding(*state.selected);
}
void save_residency_state(const std::filesystem::path& path,const ResidencyState& state) {
  const auto text=serialize_residency_state(state);
  const auto temporary=std::filesystem::path(path.string()+".tmp."+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try{
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
    if(!output)throw std::runtime_error("cannot create residency state file");
    output<<text;output.flush();
    if(!output)throw std::runtime_error("cannot write residency state file");
    output.close();if(!output)throw std::runtime_error("cannot close residency state file");
    std::filesystem::rename(temporary,path);
  }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
} // namespace terraflux::runtime
