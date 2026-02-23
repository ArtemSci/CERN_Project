#include "PhaseIConfig.hh"

#include <cmath>
#include <cctype>
#include <fstream>
#include <stdexcept>

#include "nlohmann/json.hpp"
#include "util/JsonConfigUtils.hh"

namespace phase1 {
namespace {

using json = nlohmann::json;

G4ThreeVector ReadVec3(const json& j, const std::string& key, const G4ThreeVector& fallback) {
  if (!j.contains(key)) {
    return fallback;
  }
  const auto& arr = j.at(key);
  if (!arr.is_array() || arr.size() != 3) {
    throw std::runtime_error("Expected array of 3 for key: " + key);
  }
  return G4ThreeVector(arr[0].get<double>(), arr[1].get<double>(), arr[2].get<double>());
}

using configutil::GetOr;

double RequireFiniteDouble(const json& j, const char* key) {
  if (!j.contains(key)) {
    throw std::runtime_error(std::string("Missing required numeric key: ") + key);
  }
  if (!j.at(key).is_number()) {
    throw std::runtime_error(std::string("Expected numeric key: ") + key);
  }
  const double value = j.at(key).get<double>();
  if (!std::isfinite(value)) {
    throw std::runtime_error(std::string("Expected finite numeric key: ") + key);
  }
  return value;
}

bool RequireBool(const json& j, const char* key) {
  if (!j.contains(key)) {
    throw std::runtime_error(std::string("Missing required boolean key: ") + key);
  }
  if (!j.at(key).is_boolean()) {
    throw std::runtime_error(std::string("Expected boolean key: ") + key);
  }
  return j.at(key).get<bool>();
}

GeometryConfig::ApertureShape ParseApertureShape(const std::string& value) {
  std::string lower;
  lower.reserve(value.size());
  for (char c : value) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (lower == "box" || lower == "rect" || lower == "rectangular") {
    return GeometryConfig::ApertureShape::Box;
  }
  return GeometryConfig::ApertureShape::Cylinder;
}

void ValidateLayer(const LayerConfig& layer) {
  if (layer.thickness_mm <= 0.0) {
    throw std::runtime_error("Layer thickness must be positive for layer: " + layer.name);
  }
  if (layer.refractive_index < 1.0) {
    throw std::runtime_error("Refractive index must be >= 1.0 for layer: " + layer.name);
  }
  if (layer.material.empty()) {
    throw std::runtime_error("Material name required for layer: " + layer.name);
  }
  if (layer.is_window && layer.bowing.enabled && layer.bowing.radius_mm <= 0.0) {
    throw std::runtime_error("Bowed window requires radius_mm > 0.");
  }
  if (layer.custom_material) {
    if (layer.density_g_cm3 <= 0.0) {
      throw std::runtime_error("Custom material requires density_g_cm3 > 0.");
    }
    if (layer.elements.empty()) {
      throw std::runtime_error("Custom material requires at least one element.");
    }
    double sum = 0.0;
    for (const auto& element : layer.elements) {
      if (element.symbol.empty() || element.fraction <= 0.0) {
        throw std::runtime_error("Custom material elements must have symbol and positive fraction.");
      }
      sum += element.fraction;
    }
    if (sum <= 0.0 || std::abs(sum - 1.0) > 0.05) {
      throw std::runtime_error("Custom material element fractions must sum to ~1.0.");
    }
  }
}

}  // namespace

PhaseIConfig PhaseIConfig::LoadFromFile(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open config: " + path);
  }

  json j;
  input >> j;

  PhaseIConfig cfg;
  const json& root = j.contains("phase1") ? j.at("phase1") : j;

  if (root.contains("geometry")) {
    const auto& g = root.at("geometry");
    cfg.geometry.world_radius_mm = GetOr(g, "world_radius_mm", cfg.geometry.world_radius_mm);
    cfg.geometry.world_half_z_mm = GetOr(g, "world_half_z_mm", cfg.geometry.world_half_z_mm);
    cfg.geometry.aperture_shape =
        ParseApertureShape(GetOr(g, "aperture_shape", std::string("cylinder")));
    cfg.geometry.aperture_radius_mm = GetOr(g, "aperture_radius_mm", cfg.geometry.aperture_radius_mm);
    cfg.geometry.aperture_half_x_mm = GetOr(g, "aperture_half_x_mm", cfg.geometry.aperture_radius_mm);
    cfg.geometry.aperture_half_y_mm = GetOr(g, "aperture_half_y_mm", cfg.geometry.aperture_radius_mm);
    cfg.geometry.wall_enabled = GetOr(g, "wall_enabled", cfg.geometry.wall_enabled);
    cfg.geometry.wall_thickness_mm = GetOr(g, "wall_thickness_mm", cfg.geometry.wall_thickness_mm);
    cfg.geometry.wall_material = GetOr(g, "wall_material", cfg.geometry.wall_material);
    cfg.geometry.wall_absorb = GetOr(g, "wall_absorb", cfg.geometry.wall_absorb);
    cfg.geometry.stack_center_z_mm = GetOr(g, "stack_center_z_mm", cfg.geometry.stack_center_z_mm);

    if (!g.contains("layers") || !g.at("layers").is_array()) {
      throw std::runtime_error("geometry.layers array required");
    }
    for (const auto& layer_json : g.at("layers")) {
      LayerConfig layer;
      layer.name = GetOr(layer_json, "name", std::string("Layer"));
      layer.material = GetOr(layer_json, "material", std::string("G4_AIR"));
      layer.thickness_mm = GetOr(layer_json, "thickness_mm", 0.0);
      layer.refractive_index = GetOr(layer_json, "refractive_index", 1.0);
      layer.is_window = GetOr(layer_json, "is_window", false);

      if (layer_json.contains("bowing")) {
        const auto& b = layer_json.at("bowing");
        layer.bowing.enabled = GetOr(b, "enabled", false);
        layer.bowing.model = GetOr(b, "model", std::string("clamped_plate"));
        layer.bowing.pressure_pa = GetOr(b, "pressure_pa", 0.0);
        layer.bowing.youngs_modulus_pa = GetOr(b, "youngs_modulus_pa", 0.0);
        layer.bowing.poisson = GetOr(b, "poisson", 0.0);
        layer.bowing.radius_mm = GetOr(b, "radius_mm", 0.0);
        layer.bowing.override_max_deflection_mm = GetOr(b, "override_max_deflection_mm", -1.0);
        layer.bowing.radial_samples = GetOr(b, "radial_samples", layer.bowing.radial_samples);
        layer.bowing.phi_samples = GetOr(b, "phi_samples", layer.bowing.phi_samples);
      }

      if (layer_json.contains("material_override")) {
        const auto& m = layer_json.at("material_override");
        layer.custom_material = true;
        layer.density_g_cm3 = GetOr(m, "density_g_cm3", 0.0);
        if (m.contains("elements") && m.at("elements").is_array()) {
          for (const auto& element : m.at("elements")) {
            ElementFraction entry;
            entry.symbol = GetOr(element, "symbol", std::string());
            entry.fraction = GetOr(element, "fraction", 0.0);
            layer.elements.push_back(entry);
          }
        }
      }

      ValidateLayer(layer);
      cfg.geometry.layers.push_back(layer);
    }
  }

  if (root.contains("field")) {
    const auto& f = root.at("field");
    const std::string type = GetOr(f, "type", std::string("none"));
    if (type == "uniform") {
      cfg.field.type = FieldConfig::Type::Uniform;
      cfg.field.uniform_b_tesla = ReadVec3(f, "b_tesla", cfg.field.uniform_b_tesla);
    } else if (type == "map") {
      cfg.field.type = FieldConfig::Type::Map;
      cfg.field.map_path = GetOr(f, "map_path", std::string());
    } else {
      cfg.field.type = FieldConfig::Type::None;
    }
  }

  if (root.contains("tracking")) {
    const auto& t = root.at("tracking");
    cfg.tracking.stepper = GetOr(t, "stepper", cfg.tracking.stepper);
    cfg.tracking.max_step_mm = GetOr(t, "max_step_mm", cfg.tracking.max_step_mm);
    cfg.tracking.min_step_mm = GetOr(t, "min_step_mm", cfg.tracking.min_step_mm);
    cfg.tracking.eps_abs = GetOr(t, "eps_abs", cfg.tracking.eps_abs);
    cfg.tracking.eps_rel = GetOr(t, "eps_rel", cfg.tracking.eps_rel);
    cfg.tracking.record_secondaries = RequireBool(t, "record_secondaries");
    cfg.tracking.enable_delta_rays = RequireBool(t, "enable_delta_rays");
    cfg.tracking.store_step_trace = RequireBool(t, "store_step_trace");
    cfg.tracking.range_out_energy_mev = RequireFiniteDouble(t, "range_out_energy_mev");
    if (!t.contains("delta_engineering") || !t.at("delta_engineering").is_object()) {
      throw std::runtime_error(
          "tracking.delta_engineering object is required for strict engineering-grade delta mode.");
    }
    const auto& de = t.at("delta_engineering");
    cfg.tracking.delta_engineering.strict_mode = RequireBool(de, "strict_mode");
    cfg.tracking.delta_engineering.electron_cut_mm = RequireFiniteDouble(de, "electron_cut_mm");
    cfg.tracking.delta_engineering.positron_cut_mm = RequireFiniteDouble(de, "positron_cut_mm");
    cfg.tracking.delta_engineering.gamma_cut_mm = RequireFiniteDouble(de, "gamma_cut_mm");
    cfg.tracking.delta_engineering.proton_cut_mm = RequireFiniteDouble(de, "proton_cut_mm");
    cfg.tracking.delta_engineering.min_energy_mev = RequireFiniteDouble(de, "min_energy_mev");
    cfg.tracking.delta_engineering.max_energy_mev = RequireFiniteDouble(de, "max_energy_mev");
  } else {
    throw std::runtime_error("tracking object is required for strict engineering-grade delta mode.");
  }

  if (root.contains("output")) {
    const auto& o = root.at("output");
    cfg.output.dir = GetOr(o, "dir", cfg.output.dir);
    cfg.output.nodes_csv = GetOr(o, "nodes_csv", cfg.output.nodes_csv);
    cfg.output.summary_csv = GetOr(o, "summary_csv", cfg.output.summary_csv);
    cfg.output.surfaces_csv = GetOr(o, "surfaces_csv", cfg.output.surfaces_csv);
    cfg.output.step_trace_csv = GetOr(o, "step_trace_csv", cfg.output.step_trace_csv);
    cfg.output.metrics_json = GetOr(o, "metrics_json", cfg.output.metrics_json);
    cfg.output.write_failure_debug = GetOr(o, "write_failure_debug", cfg.output.write_failure_debug);
    cfg.output.failure_debug_json = GetOr(o, "failure_debug_json", cfg.output.failure_debug_json);
  }

  if (root.contains("rng")) {
    const auto& r = root.at("rng");
    cfg.rng.seed = GetOr(r, "seed", cfg.rng.seed);
  }

  if (!root.contains("tracks") || !root.at("tracks").is_array()) {
    throw std::runtime_error("tracks array required");
  }
  for (const auto& t : root.at("tracks")) {
    TrackConfig track;
    track.track_id = GetOr(t, "track_id", 0);
    track.parent_id = GetOr(t, "parent_id", 0);
    track.particle = GetOr(t, "particle", std::string("mu-"));
    track.charge = RequireFiniteDouble(t, "charge");
    track.pos_mm = ReadVec3(t, "pos_mm", track.pos_mm);
    track.mom_mev = ReadVec3(t, "mom_mev", track.mom_mev);
    track.time_ns = GetOr(t, "time_ns", 0.0);
    cfg.tracks.push_back(track);
  }

  if (cfg.geometry.layers.empty()) {
    throw std::runtime_error("At least one geometry layer is required");
  }
  if (cfg.geometry.aperture_shape == GeometryConfig::ApertureShape::Cylinder) {
    if (cfg.geometry.aperture_radius_mm <= 0.0) {
      throw std::runtime_error("aperture_radius_mm must be positive for cylindrical geometry.");
    }
  } else {
    if (cfg.geometry.aperture_half_x_mm <= 0.0 || cfg.geometry.aperture_half_y_mm <= 0.0) {
      throw std::runtime_error("aperture_half_x_mm and aperture_half_y_mm must be positive for box geometry.");
    }
  }
  if (cfg.geometry.wall_enabled && cfg.geometry.wall_thickness_mm <= 0.0) {
    throw std::runtime_error("wall_thickness_mm must be positive when wall_enabled is true.");
  }
  if (!cfg.tracking.record_secondaries) {
    throw std::runtime_error("tracking.record_secondaries must be true for engineering-grade optical emission.");
  }
  if (!cfg.tracking.store_step_trace) {
    throw std::runtime_error("tracking.store_step_trace must be true; Phase II requires strict optical step input.");
  }
  if (!cfg.tracking.enable_delta_rays) {
    throw std::runtime_error("tracking.enable_delta_rays must be true in strict engineering-grade delta mode.");
  }
  if (!cfg.tracking.delta_engineering.strict_mode) {
    throw std::runtime_error("tracking.delta_engineering.strict_mode must be true.");
  }
  if (cfg.tracking.delta_engineering.electron_cut_mm <= 0.0 ||
      cfg.tracking.delta_engineering.positron_cut_mm <= 0.0 ||
      cfg.tracking.delta_engineering.gamma_cut_mm <= 0.0 ||
      cfg.tracking.delta_engineering.proton_cut_mm <= 0.0) {
    throw std::runtime_error("tracking.delta_engineering production cuts must be > 0 mm.");
  }
  if (cfg.tracking.delta_engineering.min_energy_mev <= 0.0 ||
      cfg.tracking.delta_engineering.max_energy_mev <= cfg.tracking.delta_engineering.min_energy_mev) {
    throw std::runtime_error(
        "tracking.delta_engineering energy bounds must satisfy 0 < min_energy_mev < max_energy_mev.");
  }

  return cfg;
}

}  // namespace phase1
