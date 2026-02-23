#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace phase4 {

struct TableConfig {
  std::vector<double> x;
  std::vector<double> values;
  std::string extrapolation = "clamp";
};

struct InputConfig {
  std::string phase3_output = ".";
  std::string hits_csv = "detector_hits.csv";
  std::string boundary_csv = "boundary_events.csv";
  std::string transport_csv = "transport_events.csv";
  std::string phase3_metrics_json = "metrics.json";
};

struct OutputConfig {
  std::string dir = "output_phase4";
  std::string digi_csv = "digi_hits.csv";
  std::string waveform_csv = "pulse_waveform.csv";
  std::string metrics_json = "metrics.json";
  std::string transport_meta_json = "transport_meta.json";
};

struct RNGConfig {
  uint64_t seed = 12345;
};

struct SensorConfig {
  std::string type = "SiPM";
  double v_bias = 0.0;
  double v_bd = 0.0;
};

struct DetectorReadoutConfig {
  std::string id;
  int channels = 1;
  std::string topology = "line";  // none, line, grid_xy
  int grid_x = 0;
  int grid_y = 0;
  double pde_scale = 1.0;
  double dcr_scale = 1.0;
  double gain_scale = 1.0;
  double time_offset_ns = 0.0;
  double v_bias = std::numeric_limits<double>::quiet_NaN();
  double v_bd = std::numeric_limits<double>::quiet_NaN();
  double sigma_tts_ns = std::numeric_limits<double>::quiet_NaN();
  double tau_recovery_ns = std::numeric_limits<double>::quiet_NaN();
};

struct ChannelOverrideConfig {
  std::string detector_id;
  int channel_id = 0;
  double pde_scale = 1.0;
  double dcr_scale = 1.0;
  double gain_scale = 1.0;
  double time_offset_ns = 0.0;
  double v_bias = std::numeric_limits<double>::quiet_NaN();
  double v_bd = std::numeric_limits<double>::quiet_NaN();
  double sigma_tts_ns = std::numeric_limits<double>::quiet_NaN();
  double tau_recovery_ns = std::numeric_limits<double>::quiet_NaN();
};

struct ActiveAreaConfig {
  std::string mode = "fill_factor";
  double fill_factor = 1.0;
  std::vector<double> grid;
  int grid_rows = 0;
  int grid_cols = 0;
};

struct AngularEfficiencyConfig {
  std::string model = "cos_power";
  double cos_power = 1.0;
  double n_incident = 1.0;
  double n_sensor = 1.5;
};

struct DetectionConfig {
  bool use_direct_pde = false;
  TableConfig pde_table;
  TableConfig internal_qe_table;
  TableConfig avalanche_prob_table;
  double pde_scale = 1.0;
  double sigma_gain = 0.1;
  AngularEfficiencyConfig angular_efficiency;
};

struct NoiseConfig {
  double dcr_hz = 0.0;
  double xt_prob = 0.0;
  double ap_prob = 0.0;
  double ap_tau_ns = 50.0;
  std::string xt_neighbor_mode = "4";
};

struct TimingConfig {
  double sigma_tts_ns = 0.0;
  double sigma_t0_ns = 0.0;
  double tau_rise_ns = 1.0;
  double tau_decay_ns = 10.0;
  double tau_dead_ns = 0.0;
};

struct WindowConfig {
  double sim_window_ns = 0.0;
  double padding_ns = 50.0;
};

struct ElectronicsConfig {
  double v_thresh_pe = 1.0;
  double adc_ped = 0.0;
  double sigma_noise_pe = 0.0;
  double t_pre_ns = 2.0;
  double t_gate_ns = 10.0;
  int n_bits = 12;
  double gain_calib = 1.0;
  double delta_clock_ns = 0.025;
  double tau_recovery_ns = 0.0;
  double sample_dt_ns = 0.1;
  int max_samples = 20000;
  double voltage_scale = 1.0;
};

struct DnlConfig {
  std::string mode = "none";
  double sigma_dnl = 0.0;
  std::vector<double> inl_coeffs;
};

struct TriggerConfig {
  std::string mode = "off";
  int multiplicity_req = 1;
  double q_trig_pe = 1.0;
  double t_coinc_ns = 5.0;
};

struct ZeroSuppressionConfig {
  bool enabled = true;
  int neighbor_radius = 0;
};

struct CalibrationConfig {
  std::string calibration_csv;
};

struct EngineeringConfig {
  bool strict_channel_mapping = true;
  bool write_failure_debug = true;
  std::string failure_debug_json = "phase4_failure_debug.json";
};

struct PhaseIVConfig {
  InputConfig input;
  OutputConfig output;
  RNGConfig rng;
  SensorConfig sensor;
  std::vector<DetectorReadoutConfig> detectors;
  std::vector<ChannelOverrideConfig> channel_overrides;
  ActiveAreaConfig active_area;
  DetectionConfig detection;
  NoiseConfig noise;
  TimingConfig timing;
  WindowConfig window;
  ElectronicsConfig electronics;
  DnlConfig dnl;
  TriggerConfig trigger;
  ZeroSuppressionConfig zero_suppression;
  CalibrationConfig calibration;
  EngineeringConfig engineering;

  static PhaseIVConfig LoadFromFile(const std::string& path);
};

}  // namespace phase4
