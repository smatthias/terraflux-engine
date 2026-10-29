#include <terraflux/graphics/visibility.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace terraflux::graphics {
namespace {
bool finite(math::Vec3 p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
using Key=std::tuple<std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t>;
Key key(const PreparedDraw& item) {
  const auto& cmd=item.command;
  const auto texture=cmd.textures.empty()?0:cmd.textures.front().texture.id;
  return {cmd.pipeline.id,cmd.vertex_buffer.id,cmd.index_buffer.id,texture};
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
  }
  pending_.clear();
  return stats;
}

} // namespace terraflux::graphics
