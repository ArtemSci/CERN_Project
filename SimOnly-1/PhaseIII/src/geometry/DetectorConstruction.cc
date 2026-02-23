#include "DetectorConstruction.hh"

#include <fstream>
#include <stdexcept>

#include "nlohmann/json.hpp"

namespace phase3 {

using json = nlohmann::json;

DetectorConstruction::DetectorConstruction(const PhaseIIIConfig& config)
    : config_(config) {
  for (const auto& material : config_.materials) {
    material_table_[material.name] = material;
  }
  if (!config_.default_material.name.empty()) {
    material_table_[config_.default_material.name] = config_.default_material;
  }
  if (material_table_.find("G4_AIR") == material_table_.end()) {
    MaterialConfig air;
    air.name = "G4_AIR";
    air.base_material = "G4_AIR";
    air.constant_index = 1.0;
    material_table_[air.name] = air;
  }
  if (material_table_.find("G4_Galactic") == material_table_.end()) {
    MaterialConfig vacuum;
    vacuum.name = "G4_Galactic";
    vacuum.base_material = "G4_Galactic";
    vacuum.constant_index = 1.0;
    material_table_[vacuum.name] = vacuum;
  }
  for (const auto& gradient : config_.material_gradients) {
    if (!gradient.layer.empty()) {
      gradient_table_[gradient.layer] = gradient;
    }
  }
}

DetectorConstruction::GeometryInfo DetectorConstruction::LoadGeometry() const {
  GeometryInfo geom;
  std::ifstream input(config_.phase1_input);
  if (!input) {
    throw std::runtime_error("Failed to open Phase I geometry input: " + config_.phase1_input);
  }
  json j;
  input >> j;
  const json& root = j.contains("phase1") ? j.at("phase1") : j;
  if (!root.contains("geometry")) {
    throw std::runtime_error("Phase I geometry block missing in input: " + config_.phase1_input);
  }
  const auto& g = root.at("geometry");
  geom.world_radius_mm = g.value("world_radius_mm", geom.world_radius_mm);
  geom.world_half_z_mm = g.value("world_half_z_mm", geom.world_half_z_mm);
  geom.aperture_shape = g.value("aperture_shape", geom.aperture_shape);
  geom.aperture_radius_mm = g.value("aperture_radius_mm", geom.aperture_radius_mm);
  geom.aperture_half_x_mm = g.value("aperture_half_x_mm", geom.aperture_half_x_mm);
  geom.aperture_half_y_mm = g.value("aperture_half_y_mm", geom.aperture_half_y_mm);
  geom.stack_center_z_mm = g.value("stack_center_z_mm", geom.stack_center_z_mm);
  geom.wall_enabled = g.value("wall_enabled", geom.wall_enabled);
  geom.wall_thickness_mm = g.value("wall_thickness_mm", geom.wall_thickness_mm);
  geom.wall_material = g.value("wall_material", geom.wall_material);
  geom.wall_absorb = g.value("wall_absorb", geom.wall_absorb);

  if (g.contains("layers") && g.at("layers").is_array()) {
    for (const auto& layer : g.at("layers")) {
      LayerInfo info;
      info.name = layer.value("name", "Layer");
      info.material = layer.value("material", "G4_AIR");
      info.thickness_mm = layer.value("thickness_mm", 0.0);
      info.is_window = layer.value("is_window", false);
      info.custom_material = layer.contains("material_override");
      if (info.custom_material) {
        info.material = info.name + "_Custom";
      }
      if (layer.contains("bowing")) {
        const auto& b = layer.at("bowing");
        info.bowing.enabled = b.value("enabled", info.bowing.enabled);
        info.bowing.model = b.value("model", info.bowing.model);
        info.bowing.pressure_pa = b.value("pressure_pa", info.bowing.pressure_pa);
        info.bowing.youngs_modulus_pa = b.value("youngs_modulus_pa", info.bowing.youngs_modulus_pa);
        info.bowing.poisson = b.value("poisson", info.bowing.poisson);
        info.bowing.radius_mm = b.value("radius_mm", info.bowing.radius_mm);
        info.bowing.override_max_deflection_mm =
            b.value("override_max_deflection_mm", info.bowing.override_max_deflection_mm);
        info.bowing.radial_samples = b.value("radial_samples", info.bowing.radial_samples);
        info.bowing.phi_samples = b.value("phi_samples", info.bowing.phi_samples);
      }
      geom.layers.push_back(info);
    }
  }
  if (geom.layers.empty()) {
    throw std::runtime_error("Phase I geometry contains no layers: " + config_.phase1_input);
  }
  return geom;
}


}  // namespace phase3
