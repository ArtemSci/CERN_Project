#include "PhaseIIIConfig.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

#include "nlohmann/json.hpp"
#include "util/JsonConfigUtils.hh"

namespace phase3 {
namespace {

using json = nlohmann::json;
using configutil::GetOr;

std::string Normalize(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

void ReadTable(const json& j, TableConfig& table) {
  if (!j.contains("lambda_nm") || !j.contains("values")) {
    return;
  }
  const auto& lambdas = j.at("lambda_nm");
  const auto& values = j.at("values");
  if (!lambdas.is_array() || !values.is_array()) {
    return;
  }
  table.lambda_nm.clear();
  table.values.clear();
  for (const auto& value : lambdas) {
    table.lambda_nm.push_back(value.get<double>());
  }
  for (const auto& value : values) {
    table.values.push_back(value.get<double>());
  }
  if (j.contains("extrapolation")) {
    table.extrapolation = j.at("extrapolation").get<std::string>();
  }
}

void ReadSpectrum(const json& j, SpectrumConfig& spectrum) {
  if (!j.contains("lambda_nm") || !j.contains("weights")) {
    return;
  }
  const auto& lambdas = j.at("lambda_nm");
  const auto& weights = j.at("weights");
  if (!lambdas.is_array() || !weights.is_array()) {
    return;
  }
  spectrum.lambda_nm.clear();
  spectrum.weights.clear();
  for (const auto& value : lambdas) {
    spectrum.lambda_nm.push_back(value.get<double>());
  }
  for (const auto& value : weights) {
    spectrum.weights.push_back(value.get<double>());
  }
}

void ReadMaterial(const json& j, MaterialConfig& material) {
  material.name = GetOr(j, "name", material.name);
  material.base_material = GetOr(j, "base_material", material.base_material);
  material.constant_index = GetOr(j, "constant_index", material.constant_index);
  material.mie_g = GetOr(j, "mie_g", material.mie_g);
  material.mie_forward_ratio = GetOr(j, "mie_forward_ratio", material.mie_forward_ratio);
  material.wls_time_constant_ns = GetOr(j, "wls_time_constant_ns", material.wls_time_constant_ns);
  material.quantum_yield = GetOr(j, "quantum_yield", material.quantum_yield);

  if (j.contains("refractive_index_table")) {
    ReadTable(j.at("refractive_index_table"), material.refractive_index_table);
  }
  if (j.contains("absorption_length_table")) {
    ReadTable(j.at("absorption_length_table"), material.absorption_length_table);
  }
  if (j.contains("rayleigh_length_table")) {
    ReadTable(j.at("rayleigh_length_table"), material.rayleigh_length_table);
  }
  if (j.contains("mie_length_table")) {
    ReadTable(j.at("mie_length_table"), material.mie_length_table);
  }
  if (j.contains("wls_absorption_length_table")) {
    ReadTable(j.at("wls_absorption_length_table"), material.wls_absorption_length_table);
  }
  if (j.contains("wls_emission_spectrum")) {
    ReadSpectrum(j.at("wls_emission_spectrum"), material.wls_emission_spectrum);
  }
}

void ReadSurface(const json& j, SurfaceConfig& surface) {
  surface.reflectivity = GetOr(j, "reflectivity", surface.reflectivity);
  surface.diffuse_fraction = GetOr(j, "diffuse_fraction", surface.diffuse_fraction);
  surface.roughness_sigma_alpha = GetOr(j, "roughness_sigma_alpha", surface.roughness_sigma_alpha);
  if (j.contains("reflectivity_table")) {
    ReadTable(j.at("reflectivity_table"), surface.reflectivity_table);
  }
  if (j.contains("specular_lobe_table")) {
    ReadTable(j.at("specular_lobe_table"), surface.specular_lobe_table);
  }
  if (j.contains("specular_spike_table")) {
    ReadTable(j.at("specular_spike_table"), surface.specular_spike_table);
  }
  if (j.contains("backscatter_table")) {
    ReadTable(j.at("backscatter_table"), surface.backscatter_table);
  }
  if (j.contains("transmittance_table")) {
    ReadTable(j.at("transmittance_table"), surface.transmittance_table);
  }
  if (j.contains("efficiency_table")) {
    ReadTable(j.at("efficiency_table"), surface.efficiency_table);
  }
}

void ReadGradient(const json& j, GradientConfig& gradient) {
  gradient.layer = GetOr(j, "layer", gradient.layer);
  gradient.axis = Normalize(GetOr(j, "axis", gradient.axis));
  gradient.slices = GetOr(j, "slices", gradient.slices);
  gradient.index_offset_per_mm = GetOr(j, "index_offset_per_mm", gradient.index_offset_per_mm);
  gradient.index_offset_total = GetOr(j, "index_offset_total", gradient.index_offset_total);
  gradient.absorption_scale_per_mm = GetOr(j, "absorption_scale_per_mm", gradient.absorption_scale_per_mm);
  gradient.absorption_scale_total = GetOr(j, "absorption_scale_total", gradient.absorption_scale_total);
  gradient.rayleigh_scale_per_mm = GetOr(j, "rayleigh_scale_per_mm", gradient.rayleigh_scale_per_mm);
  gradient.rayleigh_scale_total = GetOr(j, "rayleigh_scale_total", gradient.rayleigh_scale_total);
}

void ReadInterface(const json& j, InterfaceConfig& iface) {
  iface.enabled = GetOr(j, "enabled", iface.enabled);
  iface.from_layer = GetOr(j, "from_layer", iface.from_layer);
  iface.to_layer = GetOr(j, "to_layer", iface.to_layer);
  iface.material_a = GetOr(j, "material_a", iface.material_a);
  iface.material_b = GetOr(j, "material_b", iface.material_b);
  if (j.contains("surface")) {
    ReadSurface(j.at("surface"), iface.surface);
  }
}

void ReadDetector(const json& j, DetectorConfig& detector) {
  detector.id = GetOr(j, "id", detector.id);
  detector.shape = Normalize(GetOr(j, "shape", detector.shape));
  detector.material = GetOr(j, "material", detector.material);
  detector.x_mm = GetOr(j, "x_mm", detector.x_mm);
  detector.y_mm = GetOr(j, "y_mm", detector.y_mm);
  detector.z_mm = GetOr(j, "z_mm", detector.z_mm);
  detector.half_x_mm = GetOr(j, "half_x_mm", detector.half_x_mm);
  detector.half_y_mm = GetOr(j, "half_y_mm", detector.half_y_mm);
  detector.half_z_mm = GetOr(j, "half_z_mm", detector.half_z_mm);
  detector.radius_mm = GetOr(j, "radius_mm", detector.radius_mm);
  detector.mesh_path = GetOr(j, "mesh_path", detector.mesh_path);
  detector.channel_mode = Normalize(GetOr(j, "channel_mode", detector.channel_mode));
  detector.channels_x = GetOr(j, "channels_x", detector.channels_x);
  detector.channels_y = GetOr(j, "channels_y", detector.channels_y);
  detector.channels_z = GetOr(j, "channels_z", detector.channels_z);
  if (j.contains("rotation_deg") && j.at("rotation_deg").is_array()) {
    detector.rotation_deg.clear();
    for (const auto& value : j.at("rotation_deg")) {
      detector.rotation_deg.push_back(value.get<double>());
    }
  }
  if (j.contains("surface")) {
    ReadSurface(j.at("surface"), detector.surface);
  }
}

void ValidateTable(const std::string& label, const TableConfig& table) {
  if (table.lambda_nm.empty() && table.values.empty()) {
    return;
  }
  if (table.lambda_nm.size() != table.values.size() || table.lambda_nm.size() < 2) {
    throw std::runtime_error(label + " requires >= 2 lambda/value pairs with equal length.");
  }
  for (size_t i = 0; i < table.lambda_nm.size(); ++i) {
    if (!std::isfinite(table.lambda_nm[i]) || !std::isfinite(table.values[i])) {
      throw std::runtime_error(label + " contains non-finite values.");
    }
    if (table.lambda_nm[i] <= 0.0) {
      throw std::runtime_error(label + " wavelengths must be > 0.");
    }
    if (i > 0 && table.lambda_nm[i] <= table.lambda_nm[i - 1]) {
      throw std::runtime_error(label + " wavelengths must be strictly increasing.");
    }
  }
}

void ValidateSpectrum(const std::string& label, const SpectrumConfig& spectrum) {
  if (spectrum.lambda_nm.empty() && spectrum.weights.empty()) {
    return;
  }
  if (spectrum.lambda_nm.size() != spectrum.weights.size() || spectrum.lambda_nm.size() < 2) {
    throw std::runtime_error(label + " requires >= 2 wavelength/weight pairs with equal length.");
  }
  double weight_sum = 0.0;
  for (size_t i = 0; i < spectrum.lambda_nm.size(); ++i) {
    if (!std::isfinite(spectrum.lambda_nm[i]) || !std::isfinite(spectrum.weights[i])) {
      throw std::runtime_error(label + " contains non-finite values.");
    }
    if (spectrum.lambda_nm[i] <= 0.0) {
      throw std::runtime_error(label + " wavelengths must be > 0.");
    }
    if (i > 0 && spectrum.lambda_nm[i] <= spectrum.lambda_nm[i - 1]) {
      throw std::runtime_error(label + " wavelengths must be strictly increasing.");
    }
    if (spectrum.weights[i] < 0.0) {
      throw std::runtime_error(label + " weights must be non-negative.");
    }
    weight_sum += spectrum.weights[i];
  }
  if (weight_sum <= 0.0) {
    throw std::runtime_error(label + " must contain at least one positive weight.");
  }
}

void ValidateSurface(const std::string& label, const SurfaceConfig& surface) {
  if (!std::isfinite(surface.reflectivity) ||
      surface.reflectivity < 0.0 || surface.reflectivity > 1.0) {
    throw std::runtime_error(label + ".reflectivity must be within [0, 1].");
  }
  if (!std::isfinite(surface.diffuse_fraction) ||
      surface.diffuse_fraction < 0.0 || surface.diffuse_fraction > 1.0) {
    throw std::runtime_error(label + ".diffuse_fraction must be within [0, 1].");
  }
  if (!std::isfinite(surface.roughness_sigma_alpha) || surface.roughness_sigma_alpha < 0.0) {
    throw std::runtime_error(label + ".roughness_sigma_alpha must be >= 0.");
  }
  ValidateTable(label + ".reflectivity_table", surface.reflectivity_table);
  ValidateTable(label + ".specular_lobe_table", surface.specular_lobe_table);
  ValidateTable(label + ".specular_spike_table", surface.specular_spike_table);
  ValidateTable(label + ".backscatter_table", surface.backscatter_table);
  ValidateTable(label + ".transmittance_table", surface.transmittance_table);
  ValidateTable(label + ".efficiency_table", surface.efficiency_table);
  auto validate_unit_interval_table = [&](const std::string& table_label, const TableConfig& table) {
    for (double value : table.values) {
      if (value < 0.0 || value > 1.0 || !std::isfinite(value)) {
        throw std::runtime_error(table_label + " values must be finite and within [0, 1].");
      }
    }
  };
  validate_unit_interval_table(label + ".reflectivity_table", surface.reflectivity_table);
  validate_unit_interval_table(label + ".specular_lobe_table", surface.specular_lobe_table);
  validate_unit_interval_table(label + ".specular_spike_table", surface.specular_spike_table);
  validate_unit_interval_table(label + ".backscatter_table", surface.backscatter_table);
  validate_unit_interval_table(label + ".transmittance_table", surface.transmittance_table);
  validate_unit_interval_table(label + ".efficiency_table", surface.efficiency_table);
}

void ValidateMaterial(const MaterialConfig& material, const std::string& label_prefix) {
  ValidateTable(label_prefix + ".refractive_index_table", material.refractive_index_table);
  ValidateTable(label_prefix + ".absorption_length_table", material.absorption_length_table);
  ValidateTable(label_prefix + ".rayleigh_length_table", material.rayleigh_length_table);
  ValidateTable(label_prefix + ".mie_length_table", material.mie_length_table);
  ValidateTable(label_prefix + ".wls_absorption_length_table", material.wls_absorption_length_table);
  ValidateSpectrum(label_prefix + ".wls_emission_spectrum", material.wls_emission_spectrum);

  if (!std::isfinite(material.constant_index) || material.constant_index < 0.0) {
    throw std::runtime_error(label_prefix + ".constant_index must be finite and >= 0.");
  }
  if (!std::isfinite(material.mie_g) || material.mie_g < -1.0 || material.mie_g > 1.0) {
    throw std::runtime_error(label_prefix + ".mie_g must be within [-1, 1].");
  }
  if (!std::isfinite(material.mie_forward_ratio) ||
      material.mie_forward_ratio < 0.0 || material.mie_forward_ratio > 1.0) {
    throw std::runtime_error(label_prefix + ".mie_forward_ratio must be within [0, 1].");
  }
  if (!std::isfinite(material.wls_time_constant_ns) || material.wls_time_constant_ns < 0.0) {
    throw std::runtime_error(label_prefix + ".wls_time_constant_ns must be >= 0.");
  }
  if (!std::isfinite(material.quantum_yield) || material.quantum_yield < 0.0) {
    throw std::runtime_error(label_prefix + ".quantum_yield must be >= 0.");
  }
}

void ValidateGradient(const GradientConfig& gradient) {
  if (gradient.layer.empty()) {
    throw std::runtime_error("material_gradients entries require non-empty layer.");
  }
  if (gradient.axis != "z") {
    throw std::runtime_error("material_gradients.axis currently supports only 'z'; unsupported axis '" +
                             gradient.axis + "'.");
  }
  if (gradient.slices < 1) {
    throw std::runtime_error("material_gradients.slices must be >= 1 for layer '" + gradient.layer + "'.");
  }
  const std::array<double, 6> values = {
      gradient.index_offset_per_mm,
      gradient.index_offset_total,
      gradient.absorption_scale_per_mm,
      gradient.absorption_scale_total,
      gradient.rayleigh_scale_per_mm,
      gradient.rayleigh_scale_total};
  for (double value : values) {
    if (!std::isfinite(value)) {
      throw std::runtime_error("material_gradients contains non-finite parameters for layer '" +
                               gradient.layer + "'.");
    }
  }
}

void ValidateDetector(const DetectorConfig& detector) {
  if (detector.id.empty()) {
    throw std::runtime_error("detectors entries require non-empty id.");
  }
  if (detector.material.empty()) {
    throw std::runtime_error("detectors[" + detector.id + "] requires non-empty material.");
  }
  if (detector.rotation_deg.size() != 3) {
    throw std::runtime_error("detectors[" + detector.id + "].rotation_deg must have exactly 3 values.");
  }
  for (double value : detector.rotation_deg) {
    if (!std::isfinite(value)) {
      throw std::runtime_error("detectors[" + detector.id + "].rotation_deg contains non-finite values.");
    }
  }
  if (detector.shape != "box" && detector.shape != "cylinder" && detector.shape != "mesh") {
    throw std::runtime_error("detectors[" + detector.id + "].shape must be one of: box, cylinder, mesh.");
  }
  if (detector.shape == "box") {
    if (!(detector.half_x_mm > 0.0 && detector.half_y_mm > 0.0 && detector.half_z_mm > 0.0)) {
      throw std::runtime_error("detectors[" + detector.id + "] box dimensions half_x_mm/half_y_mm/half_z_mm must be > 0.");
    }
  }
  if (detector.shape == "cylinder") {
    if (!(detector.radius_mm > 0.0 && detector.half_z_mm > 0.0)) {
      throw std::runtime_error("detectors[" + detector.id + "] cylinder dimensions radius_mm/half_z_mm must be > 0.");
    }
  }
  if (detector.shape == "mesh" && detector.mesh_path.empty()) {
    throw std::runtime_error("detectors[" + detector.id + "] mesh_path is required when shape='mesh'.");
  }
  if (detector.channel_mode != "single" && detector.channel_mode != "grid_xy" && detector.channel_mode != "grid_z") {
    throw std::runtime_error("detectors[" + detector.id + "].channel_mode must be one of: single, grid_xy, grid_z.");
  }
  if (detector.channel_mode == "grid_xy") {
    if (detector.channels_x <= 0 || detector.channels_y <= 0) {
      throw std::runtime_error("detectors[" + detector.id + "].channels_x and channels_y must be > 0 for channel_mode='grid_xy'.");
    }
    if (detector.shape == "mesh") {
      throw std::runtime_error("detectors[" + detector.id + "] mesh supports channel_mode='single' only.");
    }
  }
  if (detector.channel_mode == "grid_z") {
    if (detector.channels_z <= 0) {
      throw std::runtime_error("detectors[" + detector.id + "].channels_z must be > 0 for channel_mode='grid_z'.");
    }
    if (detector.shape == "mesh") {
      throw std::runtime_error("detectors[" + detector.id + "] mesh supports channel_mode='single' only.");
    }
  }
  ValidateSurface("detectors[" + detector.id + "].surface", detector.surface);
}

}  // namespace

PhaseIIIConfig PhaseIIIConfig::LoadFromFile(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open config: " + path);
  }
  json j;
  input >> j;

  PhaseIIIConfig cfg;
  const json& root = j.contains("phase3") ? j.at("phase3") : j;

  cfg.phase1_input = GetOr(root, "phase1_input", cfg.phase1_input);
  cfg.phase2_output = GetOr(root, "phase2_output", cfg.phase2_output);
  cfg.phase2_photons_csv = GetOr(root, "phase2_photons_csv", cfg.phase2_photons_csv);

  if (root.contains("output")) {
    const auto& o = root.at("output");
    cfg.output.dir = GetOr(o, "dir", cfg.output.dir);
    cfg.output.hits_csv = GetOr(o, "hits_csv", cfg.output.hits_csv);
    cfg.output.boundary_csv = GetOr(o, "boundary_csv", cfg.output.boundary_csv);
    cfg.output.transport_csv = GetOr(o, "transport_csv", cfg.output.transport_csv);
    cfg.output.steps_csv = GetOr(o, "steps_csv", cfg.output.steps_csv);
    cfg.output.metrics_json = GetOr(o, "metrics_json", cfg.output.metrics_json);
    cfg.output.write_boundary_csv = GetOr(o, "write_boundary_csv", cfg.output.write_boundary_csv);
    cfg.output.write_transport_csv = GetOr(o, "write_transport_csv", cfg.output.write_transport_csv);
  }

  if (root.contains("tracking")) {
    const auto& t = root.at("tracking");
    cfg.tracking.record_steps = GetOr(t, "record_steps", cfg.tracking.record_steps);
    cfg.tracking.max_trace_photons = GetOr(t, "max_trace_photons", cfg.tracking.max_trace_photons);
    cfg.tracking.max_steps_per_photon = GetOr(t, "max_steps_per_photon", cfg.tracking.max_steps_per_photon);
  }

  if (root.contains("rng")) {
    const auto& r = root.at("rng");
    cfg.rng.seed = GetOr(r, "seed", cfg.rng.seed);
  }

  if (root.contains("engineering")) {
    const auto& e = root.at("engineering");
    cfg.engineering.strict_geometry_input =
        GetOr(e, "strict_geometry_input", cfg.engineering.strict_geometry_input);
    cfg.engineering.fail_on_unknown_material =
        GetOr(e, "fail_on_unknown_material", cfg.engineering.fail_on_unknown_material);
    cfg.engineering.strict_csv_parsing =
        GetOr(e, "strict_csv_parsing", cfg.engineering.strict_csv_parsing);
    cfg.engineering.write_failure_debug =
        GetOr(e, "write_failure_debug", cfg.engineering.write_failure_debug);
    cfg.engineering.failure_debug_json =
        GetOr(e, "failure_debug_json", cfg.engineering.failure_debug_json);
  }

  if (root.contains("surfaces")) {
    const auto& s = root.at("surfaces");
    if (s.contains("wall")) {
      ReadSurface(s.at("wall"), cfg.wall_surface);
    }
    if (s.contains("window")) {
      cfg.window_surface.roughness_sigma_alpha =
          GetOr(s.at("window"), "roughness_sigma_alpha", cfg.window_surface.roughness_sigma_alpha);
    }
  }

  if (root.contains("detectors") && root.at("detectors").is_array()) {
    for (const auto& d : root.at("detectors")) {
      DetectorConfig detector;
      ReadDetector(d, detector);
      cfg.detectors.push_back(detector);
    }
  }

  if (root.contains("materials") && root.at("materials").is_array()) {
    for (const auto& m : root.at("materials")) {
      MaterialConfig material;
      ReadMaterial(m, material);
      cfg.materials.push_back(material);
    }
  }

  if (root.contains("default_material")) {
    ReadMaterial(root.at("default_material"), cfg.default_material);
  }

  if (root.contains("material_aliases") && root.at("material_aliases").is_object()) {
    for (const auto& entry : root.at("material_aliases").items()) {
      cfg.material_aliases[entry.key()] = entry.value().get<std::string>();
    }
  }

  if (root.contains("material_gradients") && root.at("material_gradients").is_array()) {
    for (const auto& entry : root.at("material_gradients")) {
      GradientConfig gradient;
      ReadGradient(entry, gradient);
      cfg.material_gradients.push_back(gradient);
    }
  }

  if (root.contains("interfaces") && root.at("interfaces").is_array()) {
    for (const auto& entry : root.at("interfaces")) {
      InterfaceConfig iface;
      ReadInterface(entry, iface);
      cfg.interfaces.push_back(iface);
    }
  }

  if (cfg.phase1_input.empty()) {
    cfg.phase1_input = path;
  }
  if (cfg.phase2_output.empty()) {
    cfg.phase2_output = ".";
  }

  if (cfg.output.dir.empty()) {
    throw std::runtime_error("output.dir is required.");
  }
  if (cfg.detectors.empty()) {
    throw std::runtime_error("detectors array is required and must contain at least one detector.");
  }

  ValidateSurface("surfaces.wall", cfg.wall_surface);
  if (!std::isfinite(cfg.window_surface.roughness_sigma_alpha) ||
      cfg.window_surface.roughness_sigma_alpha < 0.0) {
    throw std::runtime_error("surfaces.window.roughness_sigma_alpha must be >= 0.");
  }

  std::set<std::string> detector_ids;
  for (const auto& detector : cfg.detectors) {
    ValidateDetector(detector);
    if (!detector_ids.insert(detector.id).second) {
      throw std::runtime_error("Duplicate detector id: " + detector.id);
    }
  }

  for (const auto& material : cfg.materials) {
    if (material.name.empty()) {
      throw std::runtime_error("materials entries require non-empty name.");
    }
    ValidateMaterial(material, material.name);
  }
  ValidateMaterial(cfg.default_material, "default_material");

  for (const auto& entry : cfg.material_aliases) {
    if (entry.first.empty() || entry.second.empty()) {
      throw std::runtime_error("material_aliases entries must have non-empty keys and values.");
    }
  }
  for (const auto& gradient : cfg.material_gradients) {
    ValidateGradient(gradient);
  }
  for (const auto& iface : cfg.interfaces) {
    if (!iface.enabled) {
      continue;
    }
    if (iface.from_layer.empty() && iface.to_layer.empty() &&
        iface.material_a.empty() && iface.material_b.empty()) {
      throw std::runtime_error("interfaces entries must define from/to layers or material_a/material_b.");
    }
    ValidateSurface("interfaces.surface", iface.surface);
  }

  // Engineering policy: malformed inputs must fail hard; permissive fallbacks are disabled.
  cfg.engineering.strict_geometry_input = true;
  cfg.engineering.fail_on_unknown_material = true;
  cfg.engineering.strict_csv_parsing = true;
  return cfg;
}

}  // namespace phase3
