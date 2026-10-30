#include <terraflux/graphics/visibility.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace terraflux::graphics {
namespace {
bool finite(math::Vec3 p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
using Key=std::tuple<std::uint32_t,std::uint32_t,std::uint32_t,
                     std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,
                     std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,
                     std::uint32_t>;
Key key(const PreparedDraw& item) {
  const auto& cmd=item.command;
  const auto find_texture=[&](std::uint32_t slot) {
    const auto it=std::find_if(cmd.textures.begin(),cmd.textures.end(),
        [&](const TextureBinding& binding){return binding.slot==slot;});
    return it==cmd.textures.end()?0:it->texture.id;
  };
  const auto texture=find_texture(3);
  const auto camera=std::find_if(cmd.uniform_buffers.begin(),cmd.uniform_buffers.end(),
      [](const UniformBufferBinding& b){return b.slot==0;});
  const auto pbr=std::find_if(cmd.uniform_buffers.begin(),cmd.uniform_buffers.end(),
      [](const UniformBufferBinding& b){return b.slot==5;});
  return {cmd.pipeline.id,cmd.vertex_buffer.id,cmd.index_buffer.id,texture,
          cmd.first_index,cmd.index_count,
          camera==cmd.uniform_buffers.end()?0:camera->buffer.id,
          pbr==cmd.uniform_buffers.end()?0:pbr->buffer.id,
          find_texture(6),find_texture(7),find_texture(8),find_texture(9)};
}
}
BoundingSphere mesh_bounds(const assets::Mesh& mesh) {
  if(mesh.vertices.empty())throw std::invalid_argument("cannot cull empty mesh");
  auto lo=mesh.vertices.front().position,hi=lo;
  if(!finite(lo))throw std::invalid_argument("nonfinite mesh bounds");
  for(const auto& vertex:mesh.vertices) {
    const auto p=vertex.position;
    if(!finite(p))throw std::invalid_argument("nonfinite mesh bounds");
    lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
    hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);
  }
  const auto center=(lo+hi)*0.5;
  double radius2=0;
  for(const auto& vertex:mesh.vertices) {
    const auto d=vertex.position-center;
    radius2=std::max(radius2,d.dot(d));
  }
  if(!std::isfinite(radius2))throw std::invalid_argument("mesh bounds overflow");
  return {center,std::sqrt(radius2)};
}
BoundingSphere world_bounds(BoundingSphere local,const math::Mat4& model) {
  if(!finite(local.center) || !(local.radius>=0) || !std::isfinite(local.radius))
    throw std::invalid_argument("invalid local sphere");
  const auto center=model.transformPoint(local.center);
  // Spectral norm <= sqrt(||M||1 * ||M||inf). This bounds rotation,
  // nonuniform scale and shear without accidentally culling visible geometry.
  double max_col=0,max_row=0;
  for(int c=0;c<3;++c){
    double sum=0;
    for(int r=0;r<3;++r)sum+=std::abs(model.at(r,c));
    max_col=std::max(max_col,sum);
  }
  for(int r=0;r<3;++r){
    double sum=0;
    for(int c=0;c<3;++c)sum+=std::abs(model.at(r,c));
    max_row=std::max(max_row,sum);
  }
  const double radius=local.radius*std::sqrt(max_col*max_row);
  if(!finite(center) || !std::isfinite(radius))
    throw std::invalid_argument("world bounds overflow");
  return {center,radius};
}
Frustum Frustum::from_view_projection(const math::Mat4& vp) {
  Frustum result;
  const std::array<std::pair<int,int>,6> terms{{
      {0,1},{0,-1},{1,1},{1,-1},{2,1},{2,-1}
  }};
  for(std::size_t i=0;i<terms.size();++i) {
    const auto [axis,sign]=terms[i];
    std::array<double,4> p{};
    for(int col=0;col<4;++col)
      p[col]=vp.at(3,col)+sign*vp.at(axis,col);
    const double length=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
    if(!(length>1e-15) || !std::isfinite(length))
      throw std::invalid_argument("invalid view-projection frustum");
    auto& plane=result.planes_[i];
    plane.normal={p[0]/length,p[1]/length,p[2]/length};
    plane.distance=p[3]/length;
    if(!finite(plane.normal) || !std::isfinite(plane.distance))
      throw std::invalid_argument("invalid frustum plane");
  }
  return result;
}
bool Frustum::intersects_aabb(math::Vec3 lo,math::Vec3 hi) const {
  if(!finite(lo) || !finite(hi) || lo.x>hi.x || lo.y>hi.y || lo.z>hi.z)
    throw std::invalid_argument("invalid frustum AABB");
  const auto scale=std::max({1.0,std::abs(lo.x),std::abs(lo.y),std::abs(lo.z),
                                 std::abs(hi.x),std::abs(hi.y),std::abs(hi.z)});
  for(const auto& plane:planes_) {
    const auto& n=plane.normal;
    const math::Vec3 positive{n.x>=0?hi.x:lo.x,n.y>=0?hi.y:lo.y,n.z>=0?hi.z:lo.z};
    const auto margin=64*std::numeric_limits<float>::epsilon()*std::max(scale,std::abs(plane.distance));
    if(n.dot(positive)+plane.distance < -margin)return false;
  }
  return true;
}

bool Frustum::intersects(BoundingSphere sphere) const {
  if(!finite(sphere.center) || !(sphere.radius>=0) ||
     !std::isfinite(sphere.radius))
    throw std::invalid_argument("invalid world sphere");
  for(const auto& plane:planes_)
    if(plane.normal.dot(sphere.center)+plane.distance < -sphere.radius)
      return false;
  return true;
}
void OpaqueRenderQueue::add(PreparedDraw item) {
  if(!item.object_uniform)
    throw std::invalid_argument("opaque draw must have an object uniform");
  pending_.push_back(std::move(item));
}
QueueStats OpaqueRenderQueue::submit(RenderDevice& device) {
  // Stable material ordering for equal keys avoids unnecessary reordering.
  std::stable_sort(pending_.begin(),pending_.end(),
      [](const PreparedDraw& a,const PreparedDraw& b){return key(a)<key(b);});
  QueueStats stats;
  Key previous{};
  bool first=true;
  for(const auto& item:pending_) {
    const auto current=key(item);
    if(first || current!=previous)++stats.state_groups;
    first=false;previous=current;
    device.update_buffer(item.object_uniform,0,
        std::as_bytes(std::span(&item.material,1)));
    device.draw_indexed(item.command);
    ++stats.submitted;
    ++stats.instances;
  }
  pending_.clear();
  return stats;
}

QueueStats OpaqueRenderQueue::submit_instanced(RenderDevice& device,
    PipelineHandle pipeline,BufferHandle storage,std::uint32_t capacity){
  if(!pipeline || !storage || capacity==0 || capacity>65535)
    throw std::invalid_argument("instancing requires a pipeline, SSBO and valid capacity");
  // Validate the whole batch before any draw or writes.
  for(const auto& item:pending_){
    if(!item.object_uniform || (item.command.uniform_buffers.size()!=2 && item.command.uniform_buffers.size()!=3) ||
       !item.command.storage_buffers.empty() || (item.command.textures.size()!=1 && item.command.textures.size()!=3 &&
        item.command.textures.size()!=5) ||
       item.command.instance_count!=1)
      throw std::invalid_argument("instanced queue requires ordinary textured object draws");
    const auto& bindings=item.command.uniform_buffers;
    const auto camera=std::find_if(bindings.begin(),bindings.end(),
        [](const UniformBufferBinding& b){return b.slot==0;});
    const auto object=std::find_if(bindings.begin(),bindings.end(),
        [](const UniformBufferBinding& b){return b.slot==2;});
    if(camera==bindings.end() || object==bindings.end() ||
       object->buffer!=item.object_uniform || !camera->buffer)
      throw std::invalid_argument("invalid camera or object uniform in instance queue");
  }
  std::stable_sort(pending_.begin(),pending_.end(),
    [](const PreparedDraw& a,const PreparedDraw& b){return key(a)<key(b);});
  QueueStats stats;
  Key previous{};
  bool first=true;
  std::vector<ObjectUniform> batch;
  batch.reserve(capacity);
  const auto flush=[&](const PreparedDraw& item,const std::vector<ObjectUniform>& instances){
    // Rewriting the instance buffer between draws preserves the same
    // fixed-size SSBO regardless of the total number of scene nodes.
    device.update_buffer(storage,0,std::as_bytes(std::span(instances)));
    auto cmd=item.command;
    cmd.pipeline=pipeline;
    cmd.uniform_buffers.erase(std::remove_if(cmd.uniform_buffers.begin(),cmd.uniform_buffers.end(),
        [](const UniformBufferBinding& b){return b.slot==2;}),cmd.uniform_buffers.end());
    cmd.storage_buffers={{4,storage}};
    cmd.instance_count=static_cast<std::uint32_t>(instances.size());
    device.draw_indexed(cmd);
    ++stats.submitted;
    stats.instances+=instances.size();
  };
  const PreparedDraw* leader=nullptr;
  for(const auto& item:pending_){
    const auto current=key(item);
    if(first || current!=previous || batch.size()==capacity){
      if(leader)flush(*leader,batch);
      batch.clear();
      ++stats.state_groups;
      leader=&item;
    }
    batch.push_back(item.material);
    first=false;previous=current;
  }
  if(leader)flush(*leader,batch);
  pending_.clear();
  return stats;
}

OpaqueRenderQueue::InstanceBufferRing::~InstanceBufferRing() noexcept {
  for(auto& frame:frames_)
    for(const auto& slot:frame)
      try{device_.destroy_buffer(slot.buffer);}catch(...){}
}
void OpaqueRenderQueue::InstanceBufferRing::begin_frame(){
  if(started_)current_frame_=(current_frame_+1)%frames_.size();
  else started_=true;
  used_=0;
}
BufferHandle OpaqueRenderQueue::InstanceBufferRing::acquire(std::uint32_t count){
  if(!started_)throw std::logic_error("begin_frame required before acquire");
  if(count==0 || count>65535)
    throw std::invalid_argument("instance batch count must be 1..65535");
  const std::size_t needed=static_cast<std::size_t>(count)*sizeof(ObjectUniform);
  auto& slots=frames_[current_frame_];
  if(used_==slots.size()){
    std::vector<std::byte> zeros(needed);
    const auto handle=device_.create_buffer({BufferUsage::Storage,needed},zeros);
    try{slots.push_back({handle,needed});}
    catch(...){device_.destroy_buffer(handle);throw;}
  }else if(slots[used_].bytes<needed){
    std::vector<std::byte> zeros(needed);
    const auto handle=device_.create_buffer({BufferUsage::Storage,needed},zeros);
    const auto old=slots[used_].buffer;
    slots[used_]={handle,needed};
    device_.destroy_buffer(old);
  }else{
    device_.orphan_storage_buffer(slots[used_].buffer);
    ++orphans_;
  }
  return slots[used_++].buffer;
}
std::size_t OpaqueRenderQueue::InstanceBufferRing::live_buffers() const {
  std::size_t count=0;
  for(const auto& frame:frames_)count+=frame.size();
  return count;
}

QueueStats OpaqueRenderQueue::submit_instanced_streamed(RenderDevice& device,
    PipelineHandle pipeline,InstanceBufferRing& ring,std::uint32_t capacity){
  if(!pipeline || capacity==0 || capacity>65535)
    throw std::invalid_argument("instancing requires a pipeline and valid capacity");
  for(const auto& item:pending_){
    if(!item.object_uniform || (item.command.uniform_buffers.size()!=2 && item.command.uniform_buffers.size()!=3) ||
       !item.command.storage_buffers.empty() || (item.command.textures.size()!=1 && item.command.textures.size()!=3 &&
        item.command.textures.size()!=5) ||
       item.command.instance_count!=1)
      throw std::invalid_argument("streamed queue requires ordinary textured object draws");
    const auto& bindings=item.command.uniform_buffers;
    const auto camera=std::find_if(bindings.begin(),bindings.end(),
        [](const UniformBufferBinding& b){return b.slot==0;});
    const auto object=std::find_if(bindings.begin(),bindings.end(),
        [](const UniformBufferBinding& b){return b.slot==2;});
    if(camera==bindings.end() || object==bindings.end() ||
       object->buffer!=item.object_uniform || !camera->buffer)
      throw std::invalid_argument("invalid camera/object binding for instancing");
  }
  std::stable_sort(pending_.begin(),pending_.end(),
      [](const PreparedDraw& a,const PreparedDraw& b){return key(a)<key(b);});
  ring.begin_frame();
  QueueStats stats;
  Key previous{};
  bool first=true;
  std::vector<ObjectUniform> batch;
  batch.reserve(capacity);
  const auto flush=[&](const PreparedDraw& leader){
    const auto storage=ring.acquire(static_cast<std::uint32_t>(batch.size()));
    device.update_buffer(storage,0,std::as_bytes(std::span(batch)));
    auto cmd=leader.command;
    cmd.pipeline=pipeline;
    cmd.uniform_buffers.erase(std::remove_if(cmd.uniform_buffers.begin(),
        cmd.uniform_buffers.end(),[](const UniformBufferBinding& b){return b.slot==2;}),
        cmd.uniform_buffers.end());
    cmd.storage_buffers={{4,storage}};
    cmd.instance_count=static_cast<std::uint32_t>(batch.size());
    device.draw_indexed(cmd);
    ++stats.submitted;
    stats.instances+=batch.size();
  };
  const PreparedDraw* leader=nullptr;
  for(const auto& item:pending_){
    const auto current=key(item);
    if(first || current!=previous || batch.size()==capacity){
      if(leader)flush(*leader);
      batch.clear();
      leader=&item;
      ++stats.state_groups;
    }
    batch.push_back(item.material);
    first=false;previous=current;
  }
  if(leader)flush(*leader);
  pending_.clear();
  return stats;
}

void TransparentRenderQueue::add(TransparentDraw item) {
  if(!item.draw.object_uniform ||
     !std::isfinite(item.distance_squared) || item.distance_squared<0)
    throw std::invalid_argument("invalid transparent item or distance");
  draws_.push_back(std::move(item));
}
void TransparentRenderQueue::sort_back_to_front() {
  std::stable_sort(draws_.begin(),draws_.end(),
      [](const TransparentDraw& a,const TransparentDraw& b){
        return a.distance_squared>b.distance_squared;
      });
}
std::size_t TransparentRenderQueue::submit(RenderDevice& device) {
  sort_back_to_front();
  for(const auto& item:draws_) {
    device.update_buffer(item.draw.object_uniform,0,
        std::as_bytes(std::span(&item.draw.material,1)));
    device.draw_indexed(item.draw.command);
  }
  const auto count=draws_.size();
  draws_.clear();
  return count;
}

} // namespace terraflux::graphics
