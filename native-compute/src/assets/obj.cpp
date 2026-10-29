#include <terraflux/assets/obj.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraflux::assets {
namespace {

double coordinate(const std::string& token, std::size_t line) {
  std::size_t consumed = 0;
  double value = 0;
  try { value = std::stod(token, &consumed); }
  catch (const std::exception&) { throw std::invalid_argument("invalid OBJ coordinate at line " + std::to_string(line)); }
  if (consumed != token.size() || !std::isfinite(value))
    throw std::invalid_argument("non-finite or invalid OBJ coordinate");
  return value;
}

std::size_t index(const std::string& token, std::size_t count) {
  if (token.empty()) throw std::invalid_argument("missing OBJ vertex/normal index");
  std::size_t end = 0;
  long long number = 0;
  try { number = std::stoll(token, &end); }
  catch (const std::exception&) { throw std::invalid_argument("invalid OBJ face index"); }
  if (end != token.size() || number == 0 || count > static_cast<std::size_t>(std::numeric_limits<long long>::max()))
    throw std::invalid_argument("invalid OBJ face index");
  const long long resolved = number > 0 ? number - 1 : static_cast<long long>(count) + number;
  if (resolved < 0 || static_cast<unsigned long long>(resolved) >= count)
    throw std::out_of_range("OBJ face references absent vertex or normal");
  return static_cast<std::size_t>(resolved);
}

struct Corner {
  std::size_t position=0, normal=0, texcoord=0;
  bool has_normal=false, has_texcoord=false;
};
Corner corner(const std::string& text, std::size_t positions,
              std::size_t texcoords, std::size_t normals) {
  const auto slash=text.find('/');
  if(slash==std::string::npos) return {index(text,positions),0,0,false,false};
  const auto second=text.find('/',slash+1);
  if(second!=std::string::npos && text.find('/',second+1)!=std::string::npos)
    throw std::invalid_argument("invalid OBJ face corner");
  Corner value{};
  value.position=index(text.substr(0,slash),positions);
  const auto uv_end=second==std::string::npos ? text.size() : second;
  const auto uv=text.substr(slash+1,uv_end-slash-1);
  if(!uv.empty()) {
    value.texcoord=index(uv,texcoords);
    value.has_texcoord=true;
  } else if(second==std::string::npos)
    throw std::invalid_argument("missing OBJ texture index");
  if(second!=std::string::npos) {
    if(second+1==text.size()) throw std::invalid_argument("missing OBJ normal index");
    value.normal=index(text.substr(second+1),normals);
    value.has_normal=true;
  }
  return value;
}

math::Vec3 normalized_normal(math::Vec3 n) {
  const double squared = n.dot(n);
  if (!(squared > 1e-24) || !std::isfinite(squared))
    throw std::invalid_argument("degenerate OBJ face or normal");
  return n / std::sqrt(squared);
}
}  // namespace

Mesh import_obj(std::istream& source) {
  Mesh mesh;
  std::vector<math::Vec3> positions, normals;
  std::string current_material;
  std::vector<math::Vec2> texcoords;
  std::string line;
  std::size_t line_number = 0;
  constexpr std::size_t kMaxPositions = 2'000'000;
  constexpr std::size_t kMaxOutputVertices = 6'000'000;
  while (std::getline(source, line)) {
    ++line_number;
    if (line.size() > 65536) throw std::invalid_argument("OBJ input line too long");
    std::istringstream row(line);
    std::string tag;
    if (!(row >> tag) || tag.starts_with('#')) continue;
    if (tag == "v" || tag == "vn") {
      std::string x, y, z;
      if (!(row >> x >> y >> z)) throw std::invalid_argument("incomplete OBJ vertex");
      const math::Vec3 parsed{coordinate(x, line_number), coordinate(y, line_number),
                              coordinate(z, line_number)};
      if (tag == "v") {
        if (positions.size() >= kMaxPositions) throw std::length_error("OBJ vertex limit exceeded");
        positions.push_back(parsed);
      } else {
        if (normals.size() >= kMaxPositions) throw std::length_error("OBJ normal limit exceeded");
        normals.push_back(normalized_normal(parsed));
      }
    } else if (tag == "vt") {
      std::string u,v;
      if (!(row >> u >> v)) throw std::invalid_argument("incomplete OBJ UV");
      if (texcoords.size() >= kMaxPositions)
        throw std::length_error("OBJ UV limit exceeded");
      texcoords.push_back({coordinate(u,line_number),coordinate(v,line_number)});
    } else if (tag == "f") {
      std::vector<Corner> face;
      std::string token;
      while (row >> token) {
        if (token.starts_with('#')) break;
        if (face.size() >= 64) throw std::length_error("OBJ polygon exceeds 64 corners");
        face.push_back(corner(token, positions.size(), texcoords.size(), normals.size()));
      }
      if (face.size() < 3) throw std::invalid_argument("OBJ face needs 3+ corners");
      if (mesh.sections.empty() || mesh.sections.back().material_name != current_material) {
        if (mesh.sections.size() >= 100'000)
          throw std::length_error("too many OBJ material groups");
        mesh.sections.push_back({static_cast<std::uint32_t>(mesh.indices.size()),0,current_material});
      }
      for (std::size_t i = 1; i + 1 < face.size(); ++i) {
        if (mesh.vertices.size() > kMaxOutputVertices - 3)
          throw std::length_error("OBJ triangulation vertex limit exceeded");
        const Corner triangle[3] = {face[0], face[i], face[i+1]};
        const auto& a = positions[triangle[0].position];
        const auto& b = positions[triangle[1].position];
        const auto& c = positions[triangle[2].position];
        const auto flat = normalized_normal((b - a).cross(c - a));
        for (const auto& entry : triangle) {
          const auto normal = entry.has_normal ? normals[entry.normal] : flat;
          mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
          mesh.vertices.push_back({positions[entry.position], normal,
                                   entry.has_texcoord ? texcoords[entry.texcoord] : math::Vec2{}});
        }
        mesh.sections.back().index_count += 3;
      }
    } else if (tag == "mtllib") {
      std::string library;
      while (row >> library) {
        if (library.starts_with('#')) break;
        if (mesh.material_libraries.size() >= 16)
          throw std::length_error("too many OBJ material libraries");
        mesh.material_libraries.push_back(library);
      }
      if (mesh.material_libraries.empty())
        throw std::invalid_argument("mtllib requires a relative library path");
    } else if (tag == "usemtl") {
      if (!(row >> current_material) || current_material.starts_with('#'))
        throw std::invalid_argument("usemtl requires material name");
    } else if (tag == "o" || tag == "g" || tag == "s") {
      // Scene/object metadata accepted, not material groups.
    } else {
      throw std::invalid_argument("unsupported OBJ directive: " + tag);
    }
  }
  if (!source.eof() && source.fail()) throw std::runtime_error("error reading OBJ stream");
  if (mesh.indices.empty()) throw std::invalid_argument("OBJ contains no renderable faces");
  return mesh;
}

}  // namespace terraflux::assets
