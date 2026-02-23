#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phase3 {

struct TableConfig {
  std::vector<double> lambda_nm;
  std::vector<double> values;
  std::string extrapolation = "error";
};

struct SpectrumConfig {
  std::vector<double> lambda_nm;
  std::vector<double> weights;
};

struct MaterialConfig {
  std::string name;
  std::string base_material;
  TableConfig refractive_index_table;
  TableConfig absorption_length_table;
  TableConfig rayleigh_length_table;
  TableConfig mie_length_table;
  double mie_g = 0.0;
  double mie_forward_ratio = 0.0;
  TableConfig wls_absorption_length_table;
  SpectrumConfig wls_emission_spectrum;
  double wls_time_constant_ns = 0.0;
  double quantum_yield = 0.0;
  double constant_index = 0.0;
};

struct SurfaceConfig {
  double reflectivity = 0.0;
  double diffuse_fraction = 1.0;
  double roughness_sigma_alpha = 0.0;
  TableConfig reflectivity_table;
  TableConfig specular_lobe_table;
  TableConfig specular_spike_table;
  TableConfig backscatter_table;
  TableConfig transmittance_table;
  TableConfig efficiency_table;
};

struct WindowSurfaceConfig {
  double roughness_sigma_alpha = 0.0;
};

struct GradientConfig {
  std::string layer;
  std::string axis = "z";
  int slices = 1;
  double index_offset_per_mm = 0.0;
  double index_offset_total = 0.0;
  double absorption_scale_per_mm = 0.0;
  double absorption_scale_total = 0.0;
  double rayleigh_scale_per_mm = 0.0;
  double rayleigh_scale_total = 0.0;
};

struct InterfaceConfig {
  bool enabled = true;
  std::string from_layer;
  std::string to_layer;
  std::string material_a;
  std::string material_b;
  SurfaceConfig surface;
};

struct DetectorConfig {
  std::string id = "detector0";
  std::string shape = "box";  // box, cylinder, mesh
  std::string material = "G4_Si";
  double x_mm = 0.0;
  double y_mm = 0.0;
  double z_mm = 0.0;
  std::vector<double> rotation_deg = {0.0, 0.0, 0.0};  // rx, ry, rz
  double half_x_mm = 5.0;
  double half_y_mm = 5.0;
  double half_z_mm = 0.5;
  double radius_mm = 5.0;
  std::string mesh_path;
  std::string channel_mode = "single";  // single, grid_xy, grid_z
  int channels_x = 1;
  int channels_y = 1;
  int channels_z = 1;
  SurfaceConfig surface;
};

struct OutputConfig {
  std::string dir = "output_phase3";
  std::string hits_csv = "detector_hits.csv";
  std::string boundary_csv = "boundary_events.csv";
  std::string transport_csv = "transport_events.csv";
  std::string steps_csv = "photon_steps.csv";
  std::string metrics_json = "metrics.json";
  bool write_boundary_csv = true;
  bool write_transport_csv = true;
};

struct TrackingConfig {
  bool record_steps = true;
  int max_trace_photons = 200;
  int max_steps_per_photon = 4000;
};

struct RNGConfig {
  uint64_t seed = 12345;
};

struct EngineeringConfig {
  bool strict_geometry_input = true;
  bool fail_on_unknown_material = true;
  bool strict_csv_parsing = true;
  bool write_failure_debug = true;
  std::string failure_debug_json = "phase3_failure_debug.json";
};

struct PhaseIIIConfig {
  std::string phase1_input;
  std::string phase2_output;
  std::string phase2_photons_csv = "photons.csv";
  OutputConfig output;
  TrackingConfig tracking;
  RNGConfig rng;
  EngineeringConfig engineering;
  std::vector<DetectorConfig> detectors;
  SurfaceConfig wall_surface;
  WindowSurfaceConfig window_surface;
  std::vector<MaterialConfig> materials;
  MaterialConfig default_material;
  std::map<std::string, std::string> material_aliases;
  std::vector<GradientConfig> material_gradients;
  std::vector<InterfaceConfig> interfaces;

  static PhaseIIIConfig LoadFromFile(const std::string& path);
};

}  // namespace phase3
