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

struct Corner { std::size_t position; std::size_t normal; bool has_normal; };

Corner corner(const std::string& token, std::size_t position_count, std::size_t normal_count) {
  const auto slash = token.find('/');
  if (slash == std::string::npos)
    return {index(token, position_count), 0, false};
  const auto second = token.find('/', slash + 1);
  if (second != std::string::npos && token.find('/', second + 1) != std::string::npos)
    throw std::invalid_argument("invalid OBJ face corner");
  const auto position = index(token.substr(0, slash), position_count);
  if (second == std::string::npos) {
    if (slash + 1 == token.size()) throw std::invalid_argument("empty OBJ texture index");
    return {position, 0, false};
  }
  if (second == slash + 1 && second + 1 == token.size())
    throw std::invalid_argument("empty OBJ normal index");
  if (second != slash + 1 && second != token.size() - 1) {
    // Texture coordinates are accepted as syntax, but not yet stored.
    const auto tex_token = token.substr(slash + 1, second - slash - 1);
    if (tex_token == "0") throw std::invalid_argument("invalid OBJ texture index");
  }
  if (second + 1 == token.size())
    throw std::invalid_argument("empty OBJ normal index");
  return {position, index(token.substr(second + 1), normal_count), true};
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
    } else if (tag == "f") {
      std::vector<Corner> face;
      std::string token;
      while (row >> token) {
        if (token.starts_with('#')) break;
        if (face.size() >= 64) throw std::length_error("OBJ polygon exceeds 64 corners");
        face.push_back(corner(token, positions.size(), normals.size()));
      }
      if (face.size() < 3) throw std::invalid_argument("OBJ face needs 3+ corners");
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
          mesh.vertices.push_back({positions[entry.position], normal});
        }
      }
    } else if (tag == "vt" || tag == "o" || tag == "g" || tag == "s" ||
               tag == "usemtl" || tag == "mtllib") {
      // Accepted metadata. UVs/materials are reserved for the next phase.
    } else {
      throw std::invalid_argument("unsupported OBJ directive: " + tag);
    }
  }
  if (!source.eof() && source.fail()) throw std::runtime_error("error reading OBJ stream");
  if (mesh.indices.empty()) throw std::invalid_argument("OBJ contains no renderable faces");
  return mesh;
}

}  // namespace terraflux::assets
