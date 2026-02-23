#include "DetectorConstruction.hh"
#include "DetectorConstructionUtils.hh"

#include <algorithm>
#include <stdexcept>

#include "G4Material.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4NistManager.hh"
#include "G4SystemOfUnits.hh"

namespace phase3 {

G4Material* DetectorConstruction::BuildMaterial(const std::string& name,
                                                const MaterialConfig* config,
                                                const MaterialConfig* fallback) const {
  auto* nist = G4NistManager::Instance();
  std::string base_name = name;
  if (config && !config->base_material.empty()) {
    base_name = config->base_material;
  } else if (fallback && !fallback->base_material.empty()) {
    base_name = fallback->base_material;
  }
  auto* base = nist->FindOrBuildMaterial(base_name, false);
  if (!base) {
    throw std::runtime_error("Failed to build Geant base material '" + base_name +
                             "' while resolving '" + name + "'.");
  }
  if (base_name == name) {
    return base;
  }
  const auto density = base->GetDensity();
  const auto n_elements = base->GetNumberOfElements();
  auto* material = new G4Material(name, density, n_elements);
  for (size_t i = 0; i < n_elements; ++i) {
    material->AddElement(const_cast<G4Element*>(base->GetElement(i)), base->GetFractionVector()[i]);
  }
  return material;
}

G4Material* DetectorConstruction::ResolveMaterial(const std::string& name) {
  auto found = material_cache_.find(name);
  if (found != material_cache_.end()) {
    return found->second;
  }

  const MaterialConfig* config = nullptr;
  const MaterialConfig* fallback = nullptr;
  ResolveMaterialConfig(name, config, fallback);
  if (!config && !fallback) {
    throw std::runtime_error("Unknown optical material mapping for '" + name +
                             "'. Add a material config or alias.");
  }

  auto* material = BuildMaterial(name, config, fallback);
  if (config) {
    ApplyOpticalProperties(material, *config);
  } else if (fallback) {
    ApplyOpticalProperties(material, *fallback);
  }
  material_cache_[name] = material;
  return material;
}

void DetectorConstruction::ResolveMaterialConfig(const std::string& name,
                                                 const MaterialConfig*& config,
                                                 const MaterialConfig*& fallback) const {
  config = nullptr;
  fallback = nullptr;
  auto it = material_table_.find(name);
  if (it != material_table_.end()) {
    config = &it->second;
    return;
  }
  auto alias_it = config_.material_aliases.find(name);
  if (alias_it != config_.material_aliases.end()) {
    auto alt = material_table_.find(alias_it->second);
    if (alt != material_table_.end()) {
      fallback = &alt->second;
      return;
    }
  }
}

MaterialConfig DetectorConstruction::BuildGradientConfig(const MaterialConfig* base_config,
                                                         const MaterialConfig* fallback,
                                                         const GradientConfig& gradient,
                                                         double layer_thickness_mm,
                                                         double slice_offset_mm) const {
  if (gradient.axis != "z") {
    throw std::runtime_error("Unsupported material gradient axis '" + gradient.axis +
                             "'. Only 'z' is currently implemented.");
  }
  MaterialConfig cfg;
  if (base_config) {
    cfg = *base_config;
  } else if (fallback) {
    cfg = *fallback;
  }

  const double thickness_mm = (layer_thickness_mm > 0.0) ? layer_thickness_mm : 1.0;
  const double norm = slice_offset_mm / thickness_mm;
  const double index_offset =
      gradient.index_offset_per_mm * slice_offset_mm + gradient.index_offset_total * norm;
  const double absorption_scale =
      1.0 + gradient.absorption_scale_per_mm * slice_offset_mm + gradient.absorption_scale_total * norm;
  const double rayleigh_scale =
      1.0 + gradient.rayleigh_scale_per_mm * slice_offset_mm + gradient.rayleigh_scale_total * norm;

  if (!cfg.refractive_index_table.values.empty()) {
    for (auto& value : cfg.refractive_index_table.values) {
      value = std::max(1.0, value + index_offset);
    }
  } else if (cfg.constant_index > 0.0) {
    cfg.constant_index = std::max(1.0, cfg.constant_index + index_offset);
  }

  if (!cfg.absorption_length_table.values.empty() && absorption_scale > 0.0) {
    for (auto& value : cfg.absorption_length_table.values) {
      value = std::max(1e-6, value * absorption_scale);
    }
  }

  if (!cfg.rayleigh_length_table.values.empty() && rayleigh_scale > 0.0) {
    for (auto& value : cfg.rayleigh_length_table.values) {
      value = std::max(1e-6, value * rayleigh_scale);
    }
  }

  return cfg;
}

G4Material* DetectorConstruction::ResolveGradientMaterial(const std::string& base_name,
                                                          const GradientConfig& gradient,
                                                          double layer_thickness_mm,
                                                          double slice_offset_mm,
                                                          const std::string& material_name) {
  auto found = gradient_material_cache_.find(material_name);
  if (found != gradient_material_cache_.end()) {
    return found->second;
  }

  const MaterialConfig* config = nullptr;
  const MaterialConfig* fallback = nullptr;
  ResolveMaterialConfig(base_name, config, fallback);

  auto* material = BuildMaterial(material_name, config, fallback);
  const MaterialConfig grad_cfg = BuildGradientConfig(config, fallback, gradient, layer_thickness_mm, slice_offset_mm);
  ApplyOpticalProperties(material, grad_cfg);
  gradient_material_cache_[material_name] = material;
  return material;
}

void DetectorConstruction::ApplyOpticalProperties(G4Material* material,
                                                  const MaterialConfig& config) const {
  auto* mpt = new G4MaterialPropertiesTable();

  TableConfig rindex = config.refractive_index_table;
  if (rindex.lambda_nm.empty() || rindex.values.empty()) {
    double n0 = config.constant_index;
    if (n0 <= 0.0) {
      n0 = 1.0;
    }
    rindex.lambda_nm = {200.0, 700.0};
    rindex.values = {n0, n0};
  }

  if (!rindex.lambda_nm.empty() && !rindex.values.empty()) {
    detector_utils::AddProperty(mpt, "RINDEX", rindex.lambda_nm, rindex.values, 1.0);
    const auto group_vel = detector_utils::BuildGroupVelocity(rindex.lambda_nm, rindex.values);
    detector_utils::AddProperty(mpt, "GROUPVEL", rindex.lambda_nm, group_vel, 1.0);
  }

  if (!config.absorption_length_table.lambda_nm.empty()) {
    detector_utils::AddProperty(mpt, "ABSLENGTH", config.absorption_length_table.lambda_nm,
                config.absorption_length_table.values, mm);
  }

  if (!config.rayleigh_length_table.lambda_nm.empty()) {
    detector_utils::AddProperty(mpt, "RAYLEIGH", config.rayleigh_length_table.lambda_nm,
                config.rayleigh_length_table.values, mm);
  }

  if (!config.mie_length_table.lambda_nm.empty()) {
    detector_utils::AddProperty(mpt, "MIEHG", config.mie_length_table.lambda_nm,
                config.mie_length_table.values, mm);
    if (config.mie_g > 0.0) {
      mpt->AddConstProperty("MIEHG_G", config.mie_g);
    }
    if (config.mie_forward_ratio > 0.0) {
      mpt->AddConstProperty("MIEHG_FORWARD_RATIO", config.mie_forward_ratio);
    }
  }

  if (!config.wls_absorption_length_table.lambda_nm.empty()) {
    detector_utils::AddProperty(mpt, "WLSABSLENGTH", config.wls_absorption_length_table.lambda_nm,
                config.wls_absorption_length_table.values, mm);
  }
  if (!config.wls_emission_spectrum.lambda_nm.empty()) {
    detector_utils::AddProperty(mpt, "WLSCOMPONENT", config.wls_emission_spectrum.lambda_nm,
                config.wls_emission_spectrum.weights, 1.0);
  }
  if (config.wls_time_constant_ns > 0.0) {
    mpt->AddConstProperty("WLSTIMECONSTANT", config.wls_time_constant_ns * ns);
  }
  if (config.quantum_yield > 0.0) {
    mpt->AddConstProperty("WLSMEANNUMBERPHOTONS", config.quantum_yield);
  }

  material->SetMaterialPropertiesTable(mpt);
}


const GradientConfig* DetectorConstruction::FindGradient(const std::string& layer_name) const {
  auto it = gradient_table_.find(layer_name);
  if (it == gradient_table_.end()) {
    return nullptr;
  }
  return &it->second;
}

}  // namespace phase3

