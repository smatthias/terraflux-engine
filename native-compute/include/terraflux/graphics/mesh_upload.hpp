#pragma once

#include <terraflux/assets/obj.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/math.hpp>

namespace terraflux::graphics {

// Upload-ready world-space asset data using the established 28-byte vertex
// format. Normal directions are transformed by the inverse transpose of the
// model matrix (so nonuniform scales remain correctly lit).
TerrainUpload prepare_asset_upload(const assets::Mesh& mesh, const math::Mat4& world);

}  // namespace terraflux::graphics
