#include <terraflux/assets/cpu_recipe_budget.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){
  bool caught=false;try{action();}catch(const std::exception&){caught=true;}check(caught,message);
}
}
int main(){try{
  using namespace terraflux::assets;
  Mesh mesh;mesh.vertices.reserve(100);mesh.vertices.resize(2);mesh.indices={0,1,0};
  const auto measured=mesh_recipe_bytes(mesh);
  check(measured.vertex_bytes==2*sizeof(MeshVertex) && measured.index_bytes==12,
        "logical payload counts sizes rather than reserved capacity");
  std::vector<Mesh> meshes;meshes.push_back(std::move(mesh));
  std::map<std::string,ImageRgba8> images{{"decoded",{1,1,{1,2,3,255}}}};
  const auto payload=cpu_recipe_bytes(meshes,images);
  check(payload.image_bytes==4 && payload.total()==measured.total()+4,"decoded RGBA payload included");
  check_cpu_recipe_budget(payload,payload.total());
  rejects([&]{check_cpu_recipe_budget(payload,payload.total()-1);},"one-byte overage rejected");
  check_cpu_recipe_budget(cpu_recipe_bytes(std::span<const Mesh>{}),0);
  rejects([&]{check_cpu_recipe_budget(payload,0);},"zero cap rejects retained payload");
  const auto maximum=std::numeric_limits<std::uint64_t>::max();
  check(checked_recipe_add(maximum-1,1)==maximum,"exact addition boundary");
  rejects([&]{(void)checked_recipe_add(maximum,1);},"addition overflow rejected");
  rejects([&]{(void)checked_recipe_multiply(maximum,2);},"multiplication overflow rejected");
  CpuRecipeBytes stable{1,2,3};
  rejects([&]{stable.include({maximum,0,0});},"accumulator overflow rejected");
  check(stable.vertex_bytes==1 && stable.total()==6,"failed accumulation preserves earlier totals");
  rejects([&]{(void)CpuRecipeBytes{maximum,1,0}.total();},"combined total overflow rejected");
  std::cout<<"CPU recipe payload: accounting, limits and overflow checks passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
