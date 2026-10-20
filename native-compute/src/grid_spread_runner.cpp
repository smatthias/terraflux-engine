#include <terraflux/contracts.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace {
struct Json {
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value;
};

class Parser {
 public:
  explicit Parser(const std::string& input) : input_(input) {}
  Json parse() { auto result = value(); whitespace(); if (position_ != input_.size()) throw std::runtime_error("unexpected trailing JSON"); return result; }
 private:
  const std::string& input_;
  std::size_t position_ = 0;
  void whitespace() { while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_; }
  char take() { if (position_ >= input_.size()) throw std::runtime_error("unexpected end of JSON"); return input_[position_++]; }
  void expect(char expected) { whitespace(); if (take() != expected) throw std::runtime_error("unexpected JSON token"); }
  Json value() {
    whitespace();
    if (position_ >= input_.size()) throw std::runtime_error("missing JSON value");
    const char next = input_[position_];
    if (next == '{') return object();
    if (next == '[') return array();
    if (next == '"') return Json{string()};
    if (next == 't') { literal("true"); return Json{true}; }
    if (next == 'f') { literal("false"); return Json{false}; }
    if (next == 'n') { literal("null"); return Json{nullptr}; }
    return Json{number()};
  }
  void literal(const std::string& expected) { if (input_.substr(position_, expected.size()) != expected) throw std::runtime_error("invalid JSON literal"); position_ += expected.size(); }
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
    if (end == begin) throw std::runtime_error("invalid JSON number");
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
};

const Json::Object& object(const Json& json) { if (!std::holds_alternative<Json::Object>(json.value)) throw std::runtime_error("JSON object required"); return std::get<Json::Object>(json.value); }
const Json::Array& array(const Json& json) { if (!std::holds_alternative<Json::Array>(json.value)) throw std::runtime_error("JSON array required"); return std::get<Json::Array>(json.value); }
const Json& field(const Json::Object& object, const std::string& key) { const auto found = object.find(key); if (found == object.end()) throw std::runtime_error("missing field: " + key); return found->second; }
double number(const Json& json) { if (!std::holds_alternative<double>(json.value)) throw std::runtime_error("JSON number required"); return std::get<double>(json.value); }
std::string string(const Json& json) { if (!std::holds_alternative<std::string>(json.value)) throw std::runtime_error("JSON string required"); return std::get<std::string>(json.value); }
double optional_number(const Json::Object& value, const std::string& key, double fallback) { const auto found = value.find(key); return found == value.end() || std::holds_alternative<std::nullptr_t>(found->second.value) ? fallback : number(found->second); }

std::string quote(const std::string& value) {
  std::string output = "\"";
  for (const char c : value) { if (c == '"' || c == '\\') output += '\\'; output += c; }
  return output + "\"";
}
void coordinates(std::ostringstream& output, const std::set<std::pair<int, int>>& values) {
  output << '['; bool first = true;
  for (const auto& [x, y] : values) { if (!first) output << ','; first = false; output << '[' << x << ',' << y << ']'; }
  output << ']';
}
}  // namespace

int main() {
  try {
    std::stringstream input; input << std::cin.rdbuf();
    const auto root = object(Parser(input.str()).parse());
    if (string(field(root, "contract_version")) != terraflux::compute::kContractVersion) throw std::runtime_error("unsupported contract_version");

    const auto& cells_json = array(field(root, "cells"));
    std::map<std::pair<int, int>, terraflux::compute::GridCell> cells;
    for (const auto& item : cells_json) {
      const auto& source = object(item);
      terraflux::compute::GridCell cell;
      cell.x = static_cast<int>(number(field(source, "x")));
      cell.y = static_cast<int>(number(field(source, "y")));
      cell.fuel_load = number(field(source, "fuel_load"));
      cell.fuel_moisture = number(field(source, "fuel_moisture"));
      cell.burn_susceptibility = number(field(source, "burn_susceptibility"));
      if (!cells.emplace(std::make_pair(cell.x, cell.y), cell).second) throw std::runtime_error("duplicate grid cell");
    }
    const auto& ignition_json = object(field(root, "ignition"));
    const std::pair<int, int> ignition{static_cast<int>(number(field(ignition_json, "x"))), static_cast<int>(number(field(ignition_json, "y")))};
    if (!cells.contains(ignition)) throw std::runtime_error("ignition cell is not present in cells");

    const int steps = static_cast<int>(optional_number(root, "steps", 12));
    const double wind_speed = optional_number(root, "wind_speed_kmh", 0);
    const double wind_direction = optional_number(root, "wind_direction_degrees", 0);
    const double threshold = optional_number(root, "spread_threshold", .5);
    if (steps < 1 || steps > 720 || threshold < 0 || threshold > 1) throw std::runtime_error("invalid simulation parameters");

    std::set<std::pair<int, int>> burning{ignition}, burned{ignition};
    std::vector<std::tuple<int, std::set<std::pair<int, int>>, std::set<std::pair<int, int>>>> frames;
    constexpr std::pair<int, int> neighbours[] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
    for (int step = 0; step < steps; ++step) {
      std::set<std::pair<int, int>> next;
      for (const auto& [x, y] : burning) for (const auto& [dx, dy] : neighbours) {
        const std::pair<int, int> target{x + dx, y + dy};
        if (burned.contains(target) || !cells.contains(target)) continue;
        const auto& cell = cells.at(target);
        const double bearing = std::fmod(std::atan2(static_cast<double>(dx), static_cast<double>(dy)) * 180.0 / std::acos(-1.0) + 360.0, 360.0);
        const double alignment = (std::cos((bearing - wind_direction) * std::acos(-1.0) / 180.0) + 1.0) / 2.0;
        const auto clamp = [](double value, double low, double high) { return std::max(low, std::min(high, value)); };
        const double score = clamp(cell.fuel_load * .35 + (1.0 - cell.fuel_moisture) * .25 + cell.burn_susceptibility * .25 + clamp(wind_speed / 80.0, 0, 1) * alignment * .15, 0, 1);
        if (score >= threshold) { next.insert(target); burned.insert(target); }
      }
      burning = std::move(next); frames.emplace_back(step, burning, burned);
      if (burning.empty()) break;
    }

    std::ostringstream output;
    output << "{\"contract_version\":\"" << terraflux::compute::kContractVersion << "\",\"engine\":\"cpp-native-v1\",\"deterministic\":true,\"grid_key\":" << quote(string(field(root, "grid_key"))) << ",\"frames\":[";
    for (std::size_t index = 0; index < frames.size(); ++index) {
      if (index) output << ','; const auto& [step, active, consumed] = frames[index];
      output << "{\"step\":" << step << ",\"active_cells\":"; coordinates(output, active);
      output << ",\"burned_cells\":"; coordinates(output, consumed);
      output << ",\"active_count\":" << active.size() << ",\"burned_count\":" << consumed.size() << '}';
    }
    output << "],\"metrics\":{\"burned_cells\":" << burned.size() << ",\"completed_steps\":" << frames.size() << "}}";
    std::cout << output.str() << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 2;
  }
}
