#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phase2 {

struct SpectrumConfig {
  std::vector<double> lambda_nm;
  std::vector<double> weights;
};

struct TableConfig {
  std::vector<double> lambda_nm;
  std::vector<double> values;
  std::string extrapolation = "error";
};

struct ScintillationConfig {
  bool enabled = false;
  double yield_per_mev = 0.0;
  double tau_fast_ns = 0.0;
  double tau_slow_ns = 0.0;
  double fast_fraction = 0.0;
  bool apply_birks = false;
  double birks_constant_mm_per_mev = 0.0;
  double fano_factor = 1.0;
  bool anisotropic = false;
  double anisotropy_strength = 0.0;
  bool anisotropy_align_to_track = true;
  std::vector<double> anisotropy_axis = {0.0, 0.0, 1.0};
  SpectrumConfig spectrum;
};

struct MaterialConfig {
  std::string name;
  std::string model = "cauchy";
  std::vector<double> coeffs;
  std::vector<double> coeffs_secondary;
  TableConfig refractive_index_table;
  TableConfig absorption_length_table;
  TableConfig transmission_table;
  double lambda_cut_nm = 200.0;
  double temperature_k = 293.15;
  double table_temperature_k = 293.15;
  double dn_dT = 0.0;
  double pressure_pa = 101325.0;
  double table_pressure_pa = 101325.0;
  double dn_dP = 0.0;
  bool is_window = false;
  double window_emission_scale = 1.0;
  ScintillationConfig scintillation;
  double n_offset = 0.0;
};

struct WavelengthConfig {
  double min_nm = 200.0;
  double max_nm = 700.0;
  double ref_nm = 400.0;
  int samples = 240;
};

struct BandpassConfig {
  double response_threshold = 1e-3;
  double absorption_length_cm = 100.0;
};

struct GenerationConfig {
  std::string source_mode = "geant4";
  bool enable_cherenkov = true;
  bool enable_scintillation = true;
  bool enable_window_emission = true;
  bool photon_thinning = false;
  double thinning_keep_fraction = 1.0;
  int max_photons_per_step = 20000;
  bool use_phase1_refractive_index = true;
};

struct SensorConfig {
  TableConfig pde;
  TableConfig filter;
};

struct FieldConfig {
  std::string model = "constant";
  double value = 0.0;
  std::vector<double> gradient_per_mm = {0.0, 0.0, 0.0};
};

struct EnvironmentConfig {
  FieldConfig temperature_k{"constant", 293.15, {0.0, 0.0, 0.0}};
  FieldConfig pressure_pa{"constant", 101325.0, {0.0, 0.0, 0.0}};
};

struct OutputConfig {
  std::string dir = "output_phase2";
  std::string photons_csv = "photons.csv";
  std::string summary_csv = "photon_summary.csv";
  std::string metrics_json = "metrics.json";
  std::string photons_soa_bin = "photons_soa.bin";
  bool write_soa = true;
};

struct RNGConfig {
  uint64_t seed = 12345;
};

struct SafetyConfig {
  bool fail_on_extrapolation = true;
  bool warn_on_extrapolation = true;
  bool fail_on_runtime_extrapolation = true;
  bool fail_on_unknown_material = true;
  bool strict_csv_parsing = true;
  bool write_failure_debug = true;
  std::string failure_debug_json = "phase2_failure_debug.json";
};

struct PhaseIIConfig {
  std::string phase1_output;
  std::string phase1_input;
  std::string phase1_nodes_csv = "track_nodes.csv";
  WavelengthConfig wavelength;
  BandpassConfig bandpass;
  GenerationConfig generation;
  SensorConfig sensor;
  EnvironmentConfig environment;
  OutputConfig output;
  RNGConfig rng;
  SafetyConfig safety;
  std::vector<MaterialConfig> materials;
  MaterialConfig default_material;
  std::map<std::string, std::string> material_aliases;

  static PhaseIIConfig LoadFromFile(const std::string& path);
};

}  // namespace phase2
