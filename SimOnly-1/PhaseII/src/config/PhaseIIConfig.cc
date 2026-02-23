#include "PhaseIIConfig.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include "nlohmann/json.hpp"
#include "util/JsonConfigUtils.hh"

namespace phase2 {
namespace {

using json = nlohmann::json;
using configutil::GetOr;

void ReadSpectrum(const json& j, SpectrumConfig& spectrum) {
  if (!j.contains("spectrum_nm") || !j.contains("spectrum_weights")) {
    return;
  }
  const auto& lambdas = j.at("spectrum_nm");
  const auto& weights = j.at("spectrum_weights");
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

void ReadScintillation(const json& j, ScintillationConfig& scint) {
  scint.enabled = GetOr(j, "enabled", scint.enabled);
  scint.yield_per_mev = GetOr(j, "yield_per_mev", scint.yield_per_mev);
  scint.tau_fast_ns = GetOr(j, "tau_fast_ns", scint.tau_fast_ns);
  scint.tau_slow_ns = GetOr(j, "tau_slow_ns", scint.tau_slow_ns);
  scint.fast_fraction = GetOr(j, "fast_fraction", scint.fast_fraction);
  scint.apply_birks = GetOr(j, "apply_birks", scint.apply_birks);
  scint.birks_constant_mm_per_mev = GetOr(j, "birks_constant_mm_per_mev", scint.birks_constant_mm_per_mev);
  scint.fano_factor = GetOr(j, "fano_factor", scint.fano_factor);
  scint.anisotropic = GetOr(j, "anisotropic", scint.anisotropic);
  scint.anisotropy_strength = GetOr(j, "anisotropy_strength", scint.anisotropy_strength);
  scint.anisotropy_align_to_track = GetOr(j, "anisotropy_align_to_track", scint.anisotropy_align_to_track);
  if (j.contains("anisotropy_axis") && j.at("anisotropy_axis").is_array()) {
    scint.anisotropy_axis.clear();
    for (const auto& value : j.at("anisotropy_axis")) {
      scint.anisotropy_axis.push_back(value.get<double>());
    }
  }
  ReadSpectrum(j, scint.spectrum);
}

void ReadMaterial(const json& j, MaterialConfig& material) {
  material.name = GetOr(j, "name", material.name);
  material.model = GetOr(j, "model", material.model);
  material.lambda_cut_nm = GetOr(j, "lambda_cut_nm", material.lambda_cut_nm);
  material.temperature_k = GetOr(j, "temperature_k", material.temperature_k);
  material.table_temperature_k = GetOr(j, "table_temperature_k", material.table_temperature_k);
  material.dn_dT = GetOr(j, "dn_dT", material.dn_dT);
  material.pressure_pa = GetOr(j, "pressure_pa", material.pressure_pa);
  material.table_pressure_pa = GetOr(j, "table_pressure_pa", material.table_pressure_pa);
  material.dn_dP = GetOr(j, "dn_dP", material.dn_dP);
  material.is_window = GetOr(j, "is_window", material.is_window);
  material.window_emission_scale = GetOr(j, "window_emission_scale", material.window_emission_scale);

  if (j.contains("coeffs") && j.at("coeffs").is_array()) {
    material.coeffs.clear();
    for (const auto& value : j.at("coeffs")) {
      material.coeffs.push_back(value.get<double>());
    }
  }
  if (j.contains("coeffs_secondary") && j.at("coeffs_secondary").is_array()) {
    material.coeffs_secondary.clear();
    for (const auto& value : j.at("coeffs_secondary")) {
      material.coeffs_secondary.push_back(value.get<double>());
    }
  }
  if (j.contains("refractive_index_table")) {
    ReadTable(j.at("refractive_index_table"), material.refractive_index_table);
  }
  if (j.contains("absorption_length_table")) {
    ReadTable(j.at("absorption_length_table"), material.absorption_length_table);
  }
  if (j.contains("transmission_table")) {
    ReadTable(j.at("transmission_table"), material.transmission_table);
  }
  if (j.contains("scintillation")) {
    ReadScintillation(j.at("scintillation"), material.scintillation);
  }
}

void ReadField(const json& j, FieldConfig& field) {
  field.model = GetOr(j, "model", field.model);
  field.value = GetOr(j, "value", field.value);
  if (j.contains("gradient_per_mm") && j.at("gradient_per_mm").is_array()) {
    field.gradient_per_mm.clear();
    for (const auto& value : j.at("gradient_per_mm")) {
      field.gradient_per_mm.push_back(value.get<double>());
    }
  }
}

void ValidateTable(const std::string& label, const TableConfig& table) {
  if (table.lambda_nm.empty() && table.values.empty()) {
    return;
  }
  if (table.lambda_nm.size() != table.values.size() || table.lambda_nm.size() < 2) {
    throw std::runtime_error(label + " requires >= 2 lambda/value pairs of equal length.");
  }
  for (size_t i = 0; i < table.lambda_nm.size(); ++i) {
    if (!std::isfinite(table.lambda_nm[i]) || !std::isfinite(table.values[i])) {
      throw std::runtime_error(label + " contains non-finite values.");
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
    throw std::runtime_error(label + " requires >= 2 wavelength/weight pairs of equal length.");
  }
  double weight_sum = 0.0;
  for (size_t i = 0; i < spectrum.lambda_nm.size(); ++i) {
    if (!std::isfinite(spectrum.lambda_nm[i]) || !std::isfinite(spectrum.weights[i])) {
      throw std::runtime_error(label + " contains non-finite values.");
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

void ValidateField(const std::string& label, const FieldConfig& field) {
  if (!std::isfinite(field.value)) {
    throw std::runtime_error(label + ".value must be finite.");
  }
  if (field.model == "linear") {
    if (field.gradient_per_mm.size() != 3) {
      throw std::runtime_error(label + ".gradient_per_mm must contain exactly 3 values for linear model.");
    }
    for (double value : field.gradient_per_mm) {
      if (!std::isfinite(value)) {
        throw std::runtime_error(label + ".gradient_per_mm contains non-finite values.");
      }
    }
  }
}

void ValidateScintillation(const std::string& label, const ScintillationConfig& scint) {
  if (!std::isfinite(scint.yield_per_mev) || scint.yield_per_mev < 0.0) {
    throw std::runtime_error(label + ".yield_per_mev must be finite and >= 0.");
  }
  if (!std::isfinite(scint.tau_fast_ns) || scint.tau_fast_ns < 0.0) {
    throw std::runtime_error(label + ".tau_fast_ns must be finite and >= 0.");
  }
  if (!std::isfinite(scint.tau_slow_ns) || scint.tau_slow_ns < 0.0) {
    throw std::runtime_error(label + ".tau_slow_ns must be finite and >= 0.");
  }
  if (!std::isfinite(scint.fast_fraction) ||
      scint.fast_fraction < 0.0 || scint.fast_fraction > 1.0) {
    throw std::runtime_error(label + ".fast_fraction must be within [0, 1].");
  }
  if (!std::isfinite(scint.birks_constant_mm_per_mev) || scint.birks_constant_mm_per_mev < 0.0) {
    throw std::runtime_error(label + ".birks_constant_mm_per_mev must be finite and >= 0.");
  }
  if (!std::isfinite(scint.fano_factor) || scint.fano_factor <= 0.0) {
    throw std::runtime_error(label + ".fano_factor must be finite and > 0.");
  }
  if (!std::isfinite(scint.anisotropy_strength) || scint.anisotropy_strength < 0.0) {
    throw std::runtime_error(label + ".anisotropy_strength must be finite and >= 0.");
  }
  if (scint.anisotropic && !scint.anisotropy_align_to_track) {
    if (scint.anisotropy_axis.size() != 3) {
      throw std::runtime_error(label + ".anisotropy_axis must contain exactly 3 values when anisotropy_align_to_track is false.");
    }
    double norm2 = 0.0;
    for (double value : scint.anisotropy_axis) {
      if (!std::isfinite(value)) {
        throw std::runtime_error(label + ".anisotropy_axis contains non-finite values.");
      }
      norm2 += value * value;
    }
    if (norm2 <= 0.0) {
      throw std::runtime_error(label + ".anisotropy_axis must have non-zero magnitude when anisotropy_align_to_track is false.");
    }
  }
  if (scint.enabled && scint.yield_per_mev > 0.0 &&
      scint.tau_fast_ns <= 0.0 && scint.tau_slow_ns <= 0.0) {
    throw std::runtime_error(label + " requires tau_fast_ns > 0 and/or tau_slow_ns > 0 when enabled with positive yield.");
  }
}

}  // namespace

PhaseIIConfig PhaseIIConfig::LoadFromFile(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open config: " + path);
  }
  json j;
  input >> j;

  PhaseIIConfig cfg;
  const json& root = j.contains("phase2") ? j.at("phase2") : j;

  cfg.phase1_output = GetOr(root, "phase1_output", cfg.phase1_output);
  cfg.phase1_input = GetOr(root, "phase1_input", cfg.phase1_input);
  cfg.phase1_nodes_csv = GetOr(root, "phase1_nodes_csv", cfg.phase1_nodes_csv);

  if (root.contains("wavelength")) {
    const auto& w = root.at("wavelength");
    cfg.wavelength.min_nm = GetOr(w, "min_nm", cfg.wavelength.min_nm);
    cfg.wavelength.max_nm = GetOr(w, "max_nm", cfg.wavelength.max_nm);
    cfg.wavelength.ref_nm = GetOr(w, "ref_nm", cfg.wavelength.ref_nm);
    cfg.wavelength.samples = GetOr(w, "samples", cfg.wavelength.samples);
  }

  if (root.contains("bandpass")) {
    const auto& b = root.at("bandpass");
    cfg.bandpass.response_threshold = GetOr(b, "response_threshold", cfg.bandpass.response_threshold);
    cfg.bandpass.absorption_length_cm = GetOr(b, "absorption_length_cm", cfg.bandpass.absorption_length_cm);
  }

  if (root.contains("generation")) {
    const auto& g = root.at("generation");
    cfg.generation.source_mode = GetOr(g, "source_mode", cfg.generation.source_mode);
    cfg.generation.enable_cherenkov = GetOr(g, "enable_cherenkov", cfg.generation.enable_cherenkov);
    cfg.generation.enable_scintillation = GetOr(g, "enable_scintillation", cfg.generation.enable_scintillation);
    cfg.generation.enable_window_emission =
        GetOr(g, "enable_window_emission", cfg.generation.enable_window_emission);
    cfg.generation.photon_thinning = GetOr(g, "photon_thinning", cfg.generation.photon_thinning);
    cfg.generation.thinning_keep_fraction =
        GetOr(g, "thinning_keep_fraction", cfg.generation.thinning_keep_fraction);
    cfg.generation.max_photons_per_step =
        GetOr(g, "max_photons_per_step", cfg.generation.max_photons_per_step);
    cfg.generation.use_phase1_refractive_index =
        GetOr(g, "use_phase1_refractive_index", cfg.generation.use_phase1_refractive_index);
  }

  if (root.contains("sensor")) {
    const auto& s = root.at("sensor");
    if (s.contains("pde")) {
      ReadTable(s.at("pde"), cfg.sensor.pde);
    }
    if (s.contains("filter")) {
      ReadTable(s.at("filter"), cfg.sensor.filter);
    }
  }

  if (root.contains("environment")) {
    const auto& env = root.at("environment");
    if (env.contains("temperature_k")) {
      ReadField(env.at("temperature_k"), cfg.environment.temperature_k);
    }
    if (env.contains("pressure_pa")) {
      ReadField(env.at("pressure_pa"), cfg.environment.pressure_pa);
    }
  }

  if (root.contains("output")) {
    const auto& o = root.at("output");
    cfg.output.dir = GetOr(o, "dir", cfg.output.dir);
    cfg.output.photons_csv = GetOr(o, "photons_csv", cfg.output.photons_csv);
    cfg.output.summary_csv = GetOr(o, "summary_csv", cfg.output.summary_csv);
    cfg.output.metrics_json = GetOr(o, "metrics_json", cfg.output.metrics_json);
    cfg.output.photons_soa_bin = GetOr(o, "photons_soa_bin", cfg.output.photons_soa_bin);
    cfg.output.write_soa = GetOr(o, "write_soa", cfg.output.write_soa);
  }

  if (root.contains("rng")) {
    const auto& r = root.at("rng");
    cfg.rng.seed = GetOr(r, "seed", cfg.rng.seed);
  }

  if (root.contains("safety")) {
    const auto& s = root.at("safety");
    cfg.safety.fail_on_extrapolation = GetOr(s, "fail_on_extrapolation", cfg.safety.fail_on_extrapolation);
    cfg.safety.warn_on_extrapolation = GetOr(s, "warn_on_extrapolation", cfg.safety.warn_on_extrapolation);
    cfg.safety.fail_on_runtime_extrapolation =
        GetOr(s, "fail_on_runtime_extrapolation", cfg.safety.fail_on_runtime_extrapolation);
    cfg.safety.fail_on_unknown_material =
        GetOr(s, "fail_on_unknown_material", cfg.safety.fail_on_unknown_material);
    cfg.safety.strict_csv_parsing =
        GetOr(s, "strict_csv_parsing", cfg.safety.strict_csv_parsing);
    cfg.safety.write_failure_debug =
        GetOr(s, "write_failure_debug", cfg.safety.write_failure_debug);
    cfg.safety.failure_debug_json =
        GetOr(s, "failure_debug_json", cfg.safety.failure_debug_json);
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

  if (cfg.phase1_input.empty()) {
    cfg.phase1_input = path;
  }

  if (cfg.phase1_output.empty()) {
    throw std::runtime_error("phase1_output is required.");
  }

  if (!(cfg.wavelength.max_nm > cfg.wavelength.min_nm)) {
    throw std::runtime_error("wavelength.max_nm must be greater than wavelength.min_nm.");
  }
  if (cfg.wavelength.samples < 2) {
    throw std::runtime_error("wavelength.samples must be >= 2.");
  }
  if (!std::isfinite(cfg.wavelength.ref_nm)) {
    throw std::runtime_error("wavelength.ref_nm must be finite.");
  }
  if (!(cfg.bandpass.response_threshold > 0.0 && cfg.bandpass.response_threshold < 1.0)) {
    throw std::runtime_error("bandpass.response_threshold must be in the open interval (0, 1).");
  }
  if (!std::isfinite(cfg.bandpass.absorption_length_cm) || cfg.bandpass.absorption_length_cm <= 0.0) {
    throw std::runtime_error("bandpass.absorption_length_cm must be > 0.");
  }
  if (!(cfg.generation.thinning_keep_fraction > 0.0 && cfg.generation.thinning_keep_fraction <= 1.0)) {
    throw std::runtime_error("generation.thinning_keep_fraction must be in (0, 1].");
  }
  if (cfg.generation.max_photons_per_step == 0) {
    throw std::runtime_error("generation.max_photons_per_step cannot be 0; use a positive cap or a negative value for uncapped.");
  }
  std::transform(cfg.generation.source_mode.begin(),
                 cfg.generation.source_mode.end(),
                 cfg.generation.source_mode.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (cfg.generation.source_mode != "geant4") {
    throw std::runtime_error(
        "generation.source_mode must be 'geant4' for engineering-grade production.");
  }
  ValidateField("environment.temperature_k", cfg.environment.temperature_k);
  ValidateField("environment.pressure_pa", cfg.environment.pressure_pa);

  ValidateTable("sensor.pde", cfg.sensor.pde);
  ValidateTable("sensor.filter", cfg.sensor.filter);
  ValidateTable("default_material.refractive_index_table", cfg.default_material.refractive_index_table);
  ValidateTable("default_material.absorption_length_table", cfg.default_material.absorption_length_table);
  ValidateTable("default_material.transmission_table", cfg.default_material.transmission_table);
  ValidateSpectrum("default_material.scintillation.spectrum", cfg.default_material.scintillation.spectrum);
  ValidateScintillation("default_material.scintillation", cfg.default_material.scintillation);

  for (const auto& material : cfg.materials) {
    if (material.name.empty()) {
      throw std::runtime_error("materials entries require non-empty name.");
    }
    ValidateTable(material.name + ".refractive_index_table", material.refractive_index_table);
    ValidateTable(material.name + ".absorption_length_table", material.absorption_length_table);
    ValidateTable(material.name + ".transmission_table", material.transmission_table);
    ValidateSpectrum(material.name + ".scintillation.spectrum", material.scintillation.spectrum);
    ValidateScintillation(material.name + ".scintillation", material.scintillation);
  }

  // Engineering policy: malformed/unknown inputs must fail hard, never silently fallback.
  cfg.safety.fail_on_extrapolation = true;
  cfg.safety.fail_on_runtime_extrapolation = true;
  cfg.safety.fail_on_unknown_material = true;
  cfg.safety.strict_csv_parsing = true;

  return cfg;
}

}  // namespace phase2
