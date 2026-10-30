#pragma once
#include <terraflux/scene.hpp>
#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace terraflux::runtime {
// Instance supplies node and mesh_index. Resource preparation/retirement is
// supplied by the host, so this API requires neither OpenGL nor a window.
struct ResidencyBudget {
  std::size_t max_meshes=512,max_instances=1024;
};
struct ResidencyStatistics {
  std::size_t instances=0,referenced_meshes=0,detached_nodes=0,saved_instances=0;
};
template<class Instance>class SceneResidency {
 public:
  void set_budget(ResidencyBudget budget) {budget_=budget;}
  ResidencyBudget budget() const {return budget_;}
  ResidencyStatistics statistics(const std::vector<Instance>& live) const {
    ResidencyStatistics stats;
    stats.instances=live.size();stats.detached_nodes=detached_.size();
    std::set<std::uint32_t> meshes;
    for(const auto& instance:live)meshes.insert(instance.mesh_index);
    stats.referenced_meshes=meshes.size();
    for(const auto& [node,saved]:detached_)stats.saved_instances+=saved.instances.size();
    return stats;
  }
  void check_budget(const std::vector<Instance>& live) const {
    const auto stats=statistics(live);
    if(stats.instances>budget_.max_instances || stats.referenced_meshes>budget_.max_meshes)
      throw std::length_error("scene residency budget exceeded");
  }
  struct SavedNode {std::optional<std::string> asset;std::vector<Instance> instances;};
  const std::map<scene::NodeId,SavedNode>& detached() const {return detached_;}
  static std::vector<std::size_t> reference_counts(const std::vector<Instance>& instances,
                                                  std::size_t mesh_count) {
    std::vector<std::size_t> result(mesh_count);
    for(const auto& instance:instances)++result.at(instance.mesh_index);
    return result;
  }
  // strip turns a copied live instance into a CPU recipe. retire/evict must
  // not throw: they release backend resources after the scene change commits.
  template<class Strip,class Retire,class Evict>
  bool detach(scene::Scene& graph,std::vector<Instance>& live,scene::NodeId node,
              Strip strip,Retire retire,Evict evict) {
    SavedNode saved;
    saved.asset=graph.get(node).mesh_asset;
    for(auto instance:live)if(instance.node==node) {
      strip(instance);saved.instances.push_back(std::move(instance));
    }
    if(saved.instances.empty())return false;
    detached_.insert_or_assign(node,std::move(saved));
    graph.set_mesh_instance(node,false);
    retire();
    std::erase_if(live,[&](const auto& instance){return instance.node==node;});
    evict();
    return true;
  }
  // prepare stages resources before publishing any instance. On failure,
  // temporary instances unwind and evict releases unused prepared geometry.
  template<class Prepare,class Evict>
  bool reattach(scene::Scene& graph,std::vector<Instance>& live,scene::NodeId node,
                Prepare prepare,Evict evict) {
    static_assert(std::is_nothrow_move_constructible_v<Instance> &&
                  std::is_nothrow_move_assignable_v<Instance>);
    const auto found=detached_.find(node);
    if(found==detached_.end())return false;
    // Admission precedes resource callbacks; aliases/shared meshes count once.
    auto proposed=live;
    proposed.insert(proposed.end(),found->second.instances.begin(),found->second.instances.end());
    check_budget(proposed);
    proposed.clear();
    auto candidates=found->second.instances;
    try{
      live.reserve(live.size()+candidates.size());
      for(auto& instance:candidates)prepare(instance);
      if(found->second.asset)graph.set_mesh_asset(node,*found->second.asset);
      else graph.set_mesh_instance(node,true);
      live.insert(live.end(),std::make_move_iterator(candidates.begin()),
                  std::make_move_iterator(candidates.end()));
      detached_.erase(found);
    }catch(...){candidates.clear();evict();throw;}
    return true;
  }
 private:
  ResidencyBudget budget_;
  std::map<scene::NodeId,SavedNode> detached_;
};
} // namespace terraflux::runtime
