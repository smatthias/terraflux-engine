#pragma once
#include <array>

namespace terraflux::graphics {
struct alignas(16) LayerVisibilityUniform {std::array<float,4> flags;};
static_assert(sizeof(LayerVisibilityUniform)==16);
struct LayerVisibility {
  bool terrain=true,fire=true;
  bool surface_visible(bool has_fire_field) const {return terrain || (fire && has_fire_field);}
  LayerVisibilityUniform uniform() const {return {{terrain?1.0f:0.0f,fire?1.0f:0.0f,0,0}};}
};
} // namespace terraflux::graphics
