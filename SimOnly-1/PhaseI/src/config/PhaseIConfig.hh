#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "G4ThreeVector.hh"

namespace phase1 {

struct BowingConfig {
  bool enabled = false;
  std::string model = "clamped_plate";
  double pressure_pa = 0.0;
  double youngs_modulus_pa = 0.0;
  double poisson = 0.0;
  double radius_mm = 0.0;
  double override_max_deflection_mm = -1.0;
  int radial_samples = 40;
  int phi_samples = 64;
};

struct ElementFraction {
  std::string symbol;
  double fraction = 0.0;
};

struct LayerConfig {
  std::string name;
  std::string material;
  double thickness_mm = 0.0;
  double refractive_index = 1.0;
  bool is_window = false;
  BowingConfig bowing;
  bool custom_material = false;
  double density_g_cm3 = 0.0;
  std::vector<ElementFraction> elements;
};

struct GeometryConfig {
  enum class ApertureShape {
    Cylinder,
    Box
  };

  double world_radius_mm = 2000.0;
  double world_half_z_mm = 2000.0;
  ApertureShape aperture_shape = ApertureShape::Cylinder;
  double aperture_radius_mm = 500.0;
  double aperture_half_x_mm = 500.0;
  double aperture_half_y_mm = 500.0;
  bool wall_enabled = true;
  double wall_thickness_mm = 10.0;
  std::string wall_material = "G4_Al";
  bool wall_absorb = true;
  double stack_center_z_mm = 0.0;
  std::vector<LayerConfig> layers;
};

struct FieldConfig {
  enum class Type {
    None,
    Uniform,
    Map
  };

  Type type = Type::None;
  G4ThreeVector uniform_b_tesla{0.0, 0.0, 0.0};
  std::string map_path;
};

struct TrackingConfig {
  struct DeltaEngineeringConfig {
    bool strict_mode = false;
    double electron_cut_mm = 0.0;
    double positron_cut_mm = 0.0;
    double gamma_cut_mm = 0.0;
    double proton_cut_mm = 0.0;
    double min_energy_mev = 0.0;
    double max_energy_mev = 0.0;
  };

  std::string stepper = "helix";
  double max_step_mm = 1.0;
  double min_step_mm = 0.01;
  double eps_abs = 1e-6;
  double eps_rel = 1e-6;
  bool record_secondaries = true;
  bool enable_delta_rays = true;
  bool store_step_trace = true;
  double range_out_energy_mev = 0.001;
  DeltaEngineeringConfig delta_engineering;
};

struct OutputConfig {
  std::string dir = "output";
  std::string nodes_csv = "track_nodes.csv";
  std::string summary_csv = "track_summary.csv";
  std::string surfaces_csv = "surfaces.csv";
  std::string step_trace_csv = "step_trace.csv";
  std::string metrics_json = "metrics.json";
  bool write_failure_debug = true;
  std::string failure_debug_json = "phase1_failure_debug.json";
};

struct TrackConfig {
  int track_id = 0;
  int parent_id = 0;
  std::string particle = "mu-";
  double charge = -1.0;
  G4ThreeVector pos_mm{0.0, 0.0, 0.0};
  G4ThreeVector mom_mev{0.0, 0.0, 0.0};
  double time_ns = 0.0;
};

struct RNGConfig {
  uint64_t seed = 12345;
};

struct PhaseIConfig {
  GeometryConfig geometry;
  FieldConfig field;
  TrackingConfig tracking;
  OutputConfig output;
  RNGConfig rng;
  std::vector<TrackConfig> tracks;

  static PhaseIConfig LoadFromFile(const std::string& path);
};

}  // namespace phase1
