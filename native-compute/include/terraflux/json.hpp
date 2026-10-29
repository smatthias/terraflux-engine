#pragma once
#include <terraflux/contracts.hpp>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace terraflux::json {
using terraflux::compute::GridCell;

// Shared zero-dependency parser, lifted from the native compute runner.
// parse_request streams the bulk cell array into GridCell (avoids per-cell
// JSON variants); parse_document reads ordinary response/fixture JSON.
struct Json {
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value;
};

// Recursive-descent JSON parser. Generic values build a small Json tree; the "cells" array of the
// request - which is the bulk of the payload - is parsed straight into GridCell structs.
class Parser {
 public:
  explicit Parser(const std::string& input) : input_(input) {}

  // Parses the root request object. Every field except "cells" is returned in the Json object;
  // the cells go to `cells`. `saw_cells` tells whether the key was present at all.
  Json parse_document() {
    Json result = value();
    finish();
    return result;
  }

  Json::Object parse_request(std::vector<GridCell>& cells, bool& saw_cells) {
    saw_cells = false;
    expect('{'); Json::Object result; whitespace();
    if (position_ < input_.size() && input_[position_] == '}') { ++position_; finish(); return result; }
    while (true) {
      const auto key = string(); expect(':');
      if (key == "cells" && !saw_cells) { saw_cells = true; cells_array(cells); }
      else result.emplace(key, value());
      whitespace(); const char delimiter = take();
      if (delimiter == '}') { finish(); return result; }
      if (delimiter != ',') throw std::runtime_error("invalid JSON object");
    }
  }

 private:
  const std::string& input_;
  std::size_t position_ = 0;

  void finish() { whitespace(); if (position_ != input_.size()) throw std::runtime_error("unexpected trailing JSON"); }
  void whitespace() { while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_; }
  char take() { if (position_ >= input_.size()) throw std::runtime_error("unexpected end of JSON"); return input_[position_++]; }
  char peek() { whitespace(); if (position_ >= input_.size()) throw std::runtime_error("unexpected end of JSON"); return input_[position_]; }
  void expect(char expected) { whitespace(); if (take() != expected) throw std::runtime_error("unexpected JSON token"); }
  void literal(const std::string& expected) { if (input_.compare(position_, expected.size(), expected) != 0) throw std::runtime_error("invalid JSON literal"); position_ += expected.size(); }

  Json value() {
    const char next = peek();
    if (next == '{') return object();
    if (next == '[') return array();
    if (next == '"') return Json{string()};
    if (next == 't') { literal("true"); return Json{true}; }
    if (next == 'f') { literal("false"); return Json{false}; }
    if (next == 'n') { literal("null"); return Json{nullptr}; }
    return Json{number()};
  }
  std::string string() {
    expect('"'); std::string output;
    while (true) {
      const char c = take();
      if (c == '"') return output;
      if (c != '\\') { output += c; continue; }
      const char escaped = take();
      if (escaped == '"' || escaped == '\\' || escaped == '/') output += escaped;
      else if (escaped == 'b') output += '\b';
      else if (escaped == 'f') output += '\f';
      else if (escaped == 'n') output += '\n';
      else if (escaped == 'r') output += '\r';
      else if (escaped == 't') output += '\t';
      else if (escaped == 'u') { for (int i = 0; i < 4; ++i) take(); output += '?'; }
      else throw std::runtime_error("invalid JSON escape");
    }
  }
  double number() {
    whitespace(); const char* begin = input_.c_str() + position_; char* end = nullptr;
    const double result = std::strtod(begin, &end);
    // strtod also accepts "inf"/"nan"/overflowing exponents; none of those are JSON numbers.
    if (end == begin || !std::isfinite(result)) throw std::runtime_error("invalid JSON number");
    position_ += static_cast<std::size_t>(end - begin); return result;
  }
  Json object() {
    expect('{'); Json::Object result; whitespace(); if (position_ < input_.size() && input_[position_] == '}') { ++position_; return Json{result}; }
    while (true) { const auto key = string(); expect(':'); result.emplace(key, value()); whitespace(); const char delimiter = take(); if (delimiter == '}') return Json{result}; if (delimiter != ',') throw std::runtime_error("invalid JSON object"); }
  }
  Json array() {
    expect('['); Json::Array result; whitespace(); if (position_ < input_.size() && input_[position_] == ']') { ++position_; return Json{result}; }
    while (true) { result.push_back(value()); whitespace(); const char delimiter = take(); if (delimiter == ']') return Json{result}; if (delimiter != ',') throw std::runtime_error("invalid JSON array"); }
  }

  // null -> nullopt, number -> value, anything else -> error (same acceptance as the Pydantic model).
  std::optional<double> optional_number_value() { if (peek() == 'n') { literal("null"); return std::nullopt; } return number(); }

  void cells_array(std::vector<GridCell>& cells) {
    expect('['); whitespace();
    if (position_ < input_.size() && input_[position_] == ']') { ++position_; return; }
    while (true) {
      cells.push_back(cell_object());
      if (cells.size() > terraflux::compute::kMaxCells) throw std::runtime_error("too many cells");
      whitespace(); const char delimiter = take();
      if (delimiter == ']') return;
      if (delimiter != ',') throw std::runtime_error("invalid JSON array");
    }
  }
  GridCell cell_object() {
    expect('{'); GridCell cell; bool seen[5] = {false, false, false, false, false};
    static const char* const kRequired[5] = {"x", "y", "fuel_load", "fuel_moisture", "burn_susceptibility"};
    whitespace();
    if (position_ < input_.size() && input_[position_] == '}') throw std::runtime_error("missing field: x");
    while (true) {
      const auto key = string(); expect(':');
      if (key == "x" && !seen[0]) { cell.x = static_cast<std::int32_t>(number()); seen[0] = true; }
      else if (key == "y" && !seen[1]) { cell.y = static_cast<std::int32_t>(number()); seen[1] = true; }
      else if (key == "fuel_load" && !seen[2]) { cell.fuel_load = number(); seen[2] = true; }
      else if (key == "fuel_moisture" && !seen[3]) { cell.fuel_moisture = number(); seen[3] = true; }
      else if (key == "burn_susceptibility" && !seen[4]) { cell.burn_susceptibility = number(); seen[4] = true; }
      else if (key == "elevation_meters" && !cell.elevation_meters) cell.elevation_meters = optional_number_value();
      else if (key == "population" && !cell.population) cell.population = optional_number_value();
      else value();  // unknown or repeated key: parse and ignore (Pydantic ignores extras; first value wins)
      whitespace(); const char delimiter = take();
      if (delimiter == '}') break;
      if (delimiter != ',') throw std::runtime_error("invalid JSON object");
    }
    for (int i = 0; i < 5; ++i) if (!seen[i]) throw std::runtime_error(std::string("missing field: ") + kRequired[i]);
    return cell;
  }
};

inline const Json::Object& object(const Json& json) { if (!std::holds_alternative<Json::Object>(json.value)) throw std::runtime_error("JSON object required"); return std::get<Json::Object>(json.value); }
inline const Json& field(const Json::Object& object, const std::string& key) { const auto found = object.find(key); if (found == object.end()) throw std::runtime_error("missing field: " + key); return found->second; }
double number(const Json& json) { if (!std::holds_alternative<double>(json.value)) throw std::runtime_error("JSON number required"); return std::get<double>(json.value); }
inline std::string string(const Json& json) { if (!std::holds_alternative<std::string>(json.value)) throw std::runtime_error("JSON string required"); return std::get<std::string>(json.value); }
inline double optional_number(const Json::Object& value, const std::string& key, double fallback) { const auto found = value.find(key); return found == value.end() || std::holds_alternative<std::nullptr_t>(found->second.value) ? fallback : number(found->second); }

}  // namespace terraflux::json
