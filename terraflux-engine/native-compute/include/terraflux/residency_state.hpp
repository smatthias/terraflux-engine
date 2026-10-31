#pragma once
#include <terraflux/scene.hpp>
#include <filesystem>

namespace terraflux::runtime {
struct ResidencyState {
  std::string scene_signature;
  std::vector<scene::NodeId> inactive;
  std::optional<scene::NodeId> selected;
};
// Deterministic compatibility fingerprint, not a cryptographic integrity check.
std::string residency_signature(const scene::Scene& graph,const std::string& primary_asset={});
std::string serialize_residency_state(const ResidencyState& state);
ResidencyState deserialize_residency_state(const std::string& text);
void validate_residency_state(const ResidencyState& state,const scene::Scene& authored,
                              const std::string& primary_asset={});
void save_residency_state(const std::filesystem::path& path,const ResidencyState& state);
} // namespace terraflux::runtime
