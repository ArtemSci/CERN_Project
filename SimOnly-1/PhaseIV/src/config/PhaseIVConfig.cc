#include "PhaseIVConfig.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "nlohmann/json.hpp"
#include "util/JsonConfigUtils.hh"

namespace phase4 {
namespace {

using json = nlohmann::json;
using configutil::GetOr;

std::string Normalize(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

void ReadTable(const json& j, TableConfig& table) {
  if (!j.contains("x") || !j.contains("values")) {
    return;
  }
  const auto& xs = j.at("x");
  const auto& values = j.at("values");
  if (!xs.is_array() || !values.is_array()) {
    return;
  }
  table.x.clear();
  table.values.clear();
  for (const auto& value : xs) {
    table.x.push_back(value.get<double>());
  }
  for (const auto& value : values) {
    table.values.push_back(value.get<double>());
  }
  if (j.contains("extrapolation")) {
    table.extrapolation = Normalize(j.at("extrapolation").get<std::string>());
  }
}

void ReadAngular(const json& j, AngularEfficiencyConfig& angular) {
  angular.model = Normalize(GetOr(j, "model", angular.model));
  angular.cos_power = GetOr(j, "cos_power", angular.cos_power);
  angular.n_incident = GetOr(j, "n_incident", angular.n_incident);
  angular.n_sensor = GetOr(j, "n_sensor", angular.n_sensor);
}

void ReadDetector(const json& j, DetectorReadoutConfig& detector) {
  detector.id = GetOr(j, "id", detector.id);
  detector.channels = GetOr(j, "channels", detector.channels);
  detector.topology = Normalize(GetOr(j, "topology", detector.topology));
  detector.grid_x = GetOr(j, "grid_x", detector.grid_x);
  detector.grid_y = GetOr(j, "grid_y", detector.grid_y);
  detector.pde_scale = GetOr(j, "pde_scale", detector.pde_scale);
  detector.dcr_scale = GetOr(j, "dcr_scale", detector.dcr_scale);
  detector.gain_scale = GetOr(j, "gain_scale", detector.gain_scale);
  detector.time_offset_ns = GetOr(j, "time_offset_ns", detector.time_offset_ns);
  if (j.contains("v_bias")) {
    detector.v_bias = j.at("v_bias").get<double>();
  }
  if (j.contains("v_bd")) {
    detector.v_bd = j.at("v_bd").get<double>();
  }
  if (j.contains("sigma_tts_ns")) {
    detector.sigma_tts_ns = j.at("sigma_tts_ns").get<double>();
  }
  if (j.contains("tau_recovery_ns")) {
    detector.tau_recovery_ns = j.at("tau_recovery_ns").get<double>();
  }
}

void ReadChannelOverride(const json& j, ChannelOverrideConfig& override_cfg) {
  override_cfg.detector_id = GetOr(j, "detector_id", override_cfg.detector_id);
  override_cfg.channel_id = GetOr(j, "channel_id", override_cfg.channel_id);
  override_cfg.pde_scale = GetOr(j, "pde_scale", override_cfg.pde_scale);
  override_cfg.dcr_scale = GetOr(j, "dcr_scale", override_cfg.dcr_scale);
  override_cfg.gain_scale = GetOr(j, "gain_scale", override_cfg.gain_scale);
  override_cfg.time_offset_ns = GetOr(j, "time_offset_ns", override_cfg.time_offset_ns);
  if (j.contains("v_bias")) {
    override_cfg.v_bias = j.at("v_bias").get<double>();
  }
  if (j.contains("v_bd")) {
    override_cfg.v_bd = j.at("v_bd").get<double>();
  }
  if (j.contains("sigma_tts_ns")) {
    override_cfg.sigma_tts_ns = j.at("sigma_tts_ns").get<double>();
  }
  if (j.contains("tau_recovery_ns")) {
    override_cfg.tau_recovery_ns = j.at("tau_recovery_ns").get<double>();
  }
}

void ValidateTable(const std::string& label, const TableConfig& table, bool require_non_empty) {
  if (table.x.empty() && table.values.empty()) {
    if (require_non_empty) {
      throw std::runtime_error(label + " is required and cannot be empty.");
    }
    return;
  }
  if (table.x.size() != table.values.size() || table.x.empty()) {
    throw std::runtime_error(label + " must contain equal-length non-empty x/values arrays.");
  }
  for (size_t i = 0; i < table.x.size(); ++i) {
    if (!std::isfinite(table.x[i]) || !std::isfinite(table.values[i])) {
      throw std::runtime_error(label + " contains non-finite values.");
    }
    if (i > 0 && table.x[i] <= table.x[i - 1]) {
      throw std::runtime_error(label + ".x must be strictly increasing.");
    }
  }
  if (table.extrapolation != "clamp" && table.extrapolation != "zero") {
    throw std::runtime_error(label + ".extrapolation must be one of: clamp, zero.");
  }
}

void ValidateProbabilityTable(const std::string& label,
                              const TableConfig& table,
                              bool require_non_empty) {
  ValidateTable(label, table, require_non_empty);
  for (double value : table.values) {
    if (value < 0.0 || value > 1.0 || !std::isfinite(value)) {
      throw std::runtime_error(label + " values must be finite and within [0, 1].");
    }
  }
}

}  // namespace

PhaseIVConfig PhaseIVConfig::LoadFromFile(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open config: " + path);
  }
  json j;
  input >> j;

  PhaseIVConfig cfg;
  const json& root = j.contains("phase4") ? j.at("phase4") : j;

  if (root.contains("input")) {
    const auto& in = root.at("input");
    cfg.input.phase3_output = GetOr(in, "phase3_output", cfg.input.phase3_output);
    cfg.input.hits_csv = GetOr(in, "hits_csv", cfg.input.hits_csv);
    cfg.input.boundary_csv = GetOr(in, "boundary_csv", cfg.input.boundary_csv);
    cfg.input.transport_csv = GetOr(in, "transport_csv", cfg.input.transport_csv);
    cfg.input.phase3_metrics_json = GetOr(in, "phase3_metrics_json", cfg.input.phase3_metrics_json);
  } else {
    cfg.input.phase3_output = GetOr(root, "phase3_output", cfg.input.phase3_output);
    cfg.input.hits_csv = GetOr(root, "hits_csv", cfg.input.hits_csv);
    cfg.input.boundary_csv = GetOr(root, "boundary_csv", cfg.input.boundary_csv);
    cfg.input.transport_csv = GetOr(root, "transport_csv", cfg.input.transport_csv);
    cfg.input.phase3_metrics_json = GetOr(root, "phase3_metrics_json", cfg.input.phase3_metrics_json);
  }

  if (root.contains("output")) {
    const auto& out = root.at("output");
    cfg.output.dir = GetOr(out, "dir", cfg.output.dir);
    cfg.output.digi_csv = GetOr(out, "digi_csv", cfg.output.digi_csv);
    cfg.output.waveform_csv = GetOr(out, "waveform_csv", cfg.output.waveform_csv);
    cfg.output.metrics_json = GetOr(out, "metrics_json", cfg.output.metrics_json);
    cfg.output.transport_meta_json = GetOr(out, "transport_meta_json", cfg.output.transport_meta_json);
  }

  if (root.contains("rng")) {
    const auto& r = root.at("rng");
    cfg.rng.seed = GetOr(r, "seed", cfg.rng.seed);
  }

  if (root.contains("sensor")) {
    const auto& s = root.at("sensor");
    cfg.sensor.type = GetOr(s, "type", cfg.sensor.type);
    cfg.sensor.v_bias = GetOr(s, "v_bias", cfg.sensor.v_bias);
    cfg.sensor.v_bd = GetOr(s, "v_bd", cfg.sensor.v_bd);
  }

  if (root.contains("detectors") && root.at("detectors").is_array()) {
    for (const auto& entry : root.at("detectors")) {
      DetectorReadoutConfig detector;
      ReadDetector(entry, detector);
      cfg.detectors.push_back(detector);
    }
  }

  if (root.contains("channel_overrides") && root.at("channel_overrides").is_array()) {
    for (const auto& entry : root.at("channel_overrides")) {
      ChannelOverrideConfig override_cfg;
      ReadChannelOverride(entry, override_cfg);
      cfg.channel_overrides.push_back(override_cfg);
    }
  }

  if (root.contains("active_area")) {
    const auto& a = root.at("active_area");
    cfg.active_area.mode = Normalize(GetOr(a, "mode", cfg.active_area.mode));
    cfg.active_area.fill_factor = GetOr(a, "fill_factor", cfg.active_area.fill_factor);
    cfg.active_area.grid_rows = GetOr(a, "grid_rows", cfg.active_area.grid_rows);
    cfg.active_area.grid_cols = GetOr(a, "grid_cols", cfg.active_area.grid_cols);
    if (a.contains("grid") && a.at("grid").is_array()) {
      cfg.active_area.grid.clear();
      for (const auto& value : a.at("grid")) {
        cfg.active_area.grid.push_back(value.get<double>());
      }
    }
  }

  if (root.contains("detection")) {
    const auto& d = root.at("detection");
    cfg.detection.use_direct_pde = GetOr(d, "use_direct_pde", cfg.detection.use_direct_pde);
    cfg.detection.pde_scale = GetOr(d, "pde_scale", cfg.detection.pde_scale);
    cfg.detection.sigma_gain = GetOr(d, "sigma_gain", cfg.detection.sigma_gain);
    if (d.contains("pde_table")) {
      ReadTable(d.at("pde_table"), cfg.detection.pde_table);
    }
    if (d.contains("internal_qe_table")) {
      ReadTable(d.at("internal_qe_table"), cfg.detection.internal_qe_table);
    }
    if (d.contains("avalanche_prob_table")) {
      ReadTable(d.at("avalanche_prob_table"), cfg.detection.avalanche_prob_table);
    }
    if (d.contains("angular_efficiency")) {
      ReadAngular(d.at("angular_efficiency"), cfg.detection.angular_efficiency);
    }
  }

  if (root.contains("noise")) {
    const auto& n = root.at("noise");
    cfg.noise.dcr_hz = GetOr(n, "dcr_hz", cfg.noise.dcr_hz);
    cfg.noise.xt_prob = GetOr(n, "xt_prob", cfg.noise.xt_prob);
    cfg.noise.ap_prob = GetOr(n, "ap_prob", cfg.noise.ap_prob);
    cfg.noise.ap_tau_ns = GetOr(n, "ap_tau_ns", cfg.noise.ap_tau_ns);
    cfg.noise.xt_neighbor_mode = Normalize(GetOr(n, "xt_neighbor_mode", cfg.noise.xt_neighbor_mode));
  }

  if (root.contains("timing")) {
    const auto& t = root.at("timing");
    cfg.timing.sigma_tts_ns = GetOr(t, "sigma_tts_ns", cfg.timing.sigma_tts_ns);
    cfg.timing.sigma_t0_ns = GetOr(t, "sigma_t0_ns", cfg.timing.sigma_t0_ns);
    cfg.timing.tau_rise_ns = GetOr(t, "tau_rise_ns", cfg.timing.tau_rise_ns);
    cfg.timing.tau_decay_ns = GetOr(t, "tau_decay_ns", cfg.timing.tau_decay_ns);
    cfg.timing.tau_dead_ns = GetOr(t, "tau_dead_ns", cfg.timing.tau_dead_ns);
  }

  if (root.contains("window")) {
    const auto& w = root.at("window");
    cfg.window.sim_window_ns = GetOr(w, "sim_window_ns", cfg.window.sim_window_ns);
    cfg.window.padding_ns = GetOr(w, "padding_ns", cfg.window.padding_ns);
  }

  if (root.contains("electronics")) {
    const auto& e = root.at("electronics");
    cfg.electronics.v_thresh_pe = GetOr(e, "v_thresh_pe", cfg.electronics.v_thresh_pe);
    cfg.electronics.adc_ped = GetOr(e, "adc_ped", cfg.electronics.adc_ped);
    cfg.electronics.sigma_noise_pe = GetOr(e, "sigma_noise_pe", cfg.electronics.sigma_noise_pe);
    cfg.electronics.t_pre_ns = GetOr(e, "t_pre_ns", cfg.electronics.t_pre_ns);
    cfg.electronics.t_gate_ns = GetOr(e, "t_gate_ns", cfg.electronics.t_gate_ns);
    cfg.electronics.n_bits = GetOr(e, "n_bits", cfg.electronics.n_bits);
    cfg.electronics.gain_calib = GetOr(e, "gain_calib", cfg.electronics.gain_calib);
    cfg.electronics.delta_clock_ns = GetOr(e, "delta_clock_ns", cfg.electronics.delta_clock_ns);
    cfg.electronics.tau_recovery_ns = GetOr(e, "tau_recovery_ns", cfg.electronics.tau_recovery_ns);
    cfg.electronics.sample_dt_ns = GetOr(e, "sample_dt_ns", cfg.electronics.sample_dt_ns);
    cfg.electronics.max_samples = GetOr(e, "max_samples", cfg.electronics.max_samples);
    cfg.electronics.voltage_scale = GetOr(e, "voltage_scale", cfg.electronics.voltage_scale);
  }

  if (root.contains("dnl")) {
    const auto& dnl = root.at("dnl");
    cfg.dnl.mode = Normalize(GetOr(dnl, "mode", cfg.dnl.mode));
    cfg.dnl.sigma_dnl = GetOr(dnl, "sigma_dnl", cfg.dnl.sigma_dnl);
    if (dnl.contains("inl_coeffs") && dnl.at("inl_coeffs").is_array()) {
      cfg.dnl.inl_coeffs.clear();
      for (const auto& value : dnl.at("inl_coeffs")) {
        cfg.dnl.inl_coeffs.push_back(value.get<double>());
      }
    }
  }

  if (root.contains("trigger")) {
    const auto& tr = root.at("trigger");
    cfg.trigger.mode = Normalize(GetOr(tr, "mode", cfg.trigger.mode));
    cfg.trigger.multiplicity_req = GetOr(tr, "multiplicity_req", cfg.trigger.multiplicity_req);
    cfg.trigger.q_trig_pe = GetOr(tr, "q_trig_pe", cfg.trigger.q_trig_pe);
    cfg.trigger.t_coinc_ns = GetOr(tr, "t_coinc_ns", cfg.trigger.t_coinc_ns);
  }

  if (root.contains("zero_suppression")) {
    const auto& zs = root.at("zero_suppression");
    cfg.zero_suppression.enabled = GetOr(zs, "enabled", cfg.zero_suppression.enabled);
    cfg.zero_suppression.neighbor_radius = GetOr(zs, "neighbor_radius", cfg.zero_suppression.neighbor_radius);
  }

  if (root.contains("calibration")) {
    const auto& c = root.at("calibration");
    cfg.calibration.calibration_csv = GetOr(c, "calibration_csv", cfg.calibration.calibration_csv);
  }

  if (root.contains("engineering")) {
    const auto& e = root.at("engineering");
    cfg.engineering.strict_channel_mapping =
        GetOr(e, "strict_channel_mapping", cfg.engineering.strict_channel_mapping);
    cfg.engineering.write_failure_debug =
        GetOr(e, "write_failure_debug", cfg.engineering.write_failure_debug);
    cfg.engineering.failure_debug_json =
        GetOr(e, "failure_debug_json", cfg.engineering.failure_debug_json);
  }

  if (cfg.output.dir.empty()) {
    throw std::runtime_error("output.dir is required.");
  }
  if (!std::isfinite(cfg.sensor.v_bias) || !std::isfinite(cfg.sensor.v_bd)) {
    throw std::runtime_error("sensor.v_bias and sensor.v_bd must be finite.");
  }
  if (cfg.detectors.empty()) {
    throw std::runtime_error("detectors array is required and must contain at least one detector.");
  }

  std::set<std::string> detector_ids;
  std::unordered_map<std::string, int> detector_channels;
  int total_channels = 0;
  for (const auto& detector : cfg.detectors) {
    if (detector.id.empty()) {
      throw std::runtime_error("detectors entries require non-empty id.");
    }
    if (!detector_ids.insert(detector.id).second) {
      throw std::runtime_error("Duplicate detector id in Phase IV config: " + detector.id);
    }
    if (detector.channels <= 0) {
      throw std::runtime_error("detectors[" + detector.id + "].channels must be > 0.");
    }
    if (detector.topology != "none" && detector.topology != "line" && detector.topology != "grid_xy") {
      throw std::runtime_error("detectors[" + detector.id + "].topology must be one of: none, line, grid_xy.");
    }
    if (detector.topology == "grid_xy") {
      if (detector.grid_x <= 0 || detector.grid_y <= 0) {
        throw std::runtime_error(
            "detectors[" + detector.id + "].grid_x and grid_y must be > 0 for topology='grid_xy'.");
      }
      if (detector.grid_x * detector.grid_y != detector.channels) {
        throw std::runtime_error(
            "detectors[" + detector.id + "] requires grid_x * grid_y == channels.");
      }
    }

    const std::array<double, 9> params = {
        detector.pde_scale,
        detector.dcr_scale,
        detector.gain_scale,
        detector.time_offset_ns,
        detector.v_bias,
        detector.v_bd,
        detector.sigma_tts_ns,
        detector.tau_recovery_ns,
        static_cast<double>(detector.channels)};
    for (size_t i = 0; i < params.size(); ++i) {
      if (i == 4 || i == 5 || i == 6 || i == 7) {
        continue;
      }
      if (!std::isfinite(params[i])) {
        throw std::runtime_error("detectors[" + detector.id + "] contains non-finite parameters.");
      }
    }
    if (detector.pde_scale < 0.0 || detector.dcr_scale < 0.0 || detector.gain_scale < 0.0) {
      throw std::runtime_error(
          "detectors[" + detector.id + "] pde_scale/dcr_scale/gain_scale must be >= 0.");
    }
    if (std::isfinite(detector.v_bias) && detector.v_bias < 0.0) {
      throw std::runtime_error("detectors[" + detector.id + "].v_bias must be >= 0 when provided.");
    }
    if (std::isfinite(detector.v_bd) && detector.v_bd < 0.0) {
      throw std::runtime_error("detectors[" + detector.id + "].v_bd must be >= 0 when provided.");
    }
    if (std::isfinite(detector.sigma_tts_ns) && detector.sigma_tts_ns < 0.0) {
      throw std::runtime_error("detectors[" + detector.id + "].sigma_tts_ns must be >= 0 when provided.");
    }
    if (std::isfinite(detector.tau_recovery_ns) && detector.tau_recovery_ns < 0.0) {
      throw std::runtime_error(
          "detectors[" + detector.id + "].tau_recovery_ns must be >= 0 when provided.");
    }

    detector_channels[detector.id] = detector.channels;
    total_channels += detector.channels;
  }

  for (const auto& override_cfg : cfg.channel_overrides) {
    if (override_cfg.detector_id.empty()) {
      throw std::runtime_error("channel_overrides entries require non-empty detector_id.");
    }
    auto it = detector_channels.find(override_cfg.detector_id);
    if (it == detector_channels.end()) {
      throw std::runtime_error("channel_overrides references unknown detector_id: " + override_cfg.detector_id);
    }
    if (override_cfg.channel_id < 0 || override_cfg.channel_id >= it->second) {
      throw std::runtime_error("channel_overrides channel_id out of range for detector_id: " +
                               override_cfg.detector_id);
    }
    if (!std::isfinite(override_cfg.pde_scale) || !std::isfinite(override_cfg.dcr_scale) ||
        !std::isfinite(override_cfg.gain_scale) || !std::isfinite(override_cfg.time_offset_ns)) {
      throw std::runtime_error("channel_overrides contains non-finite scale/offset values.");
    }
    if (override_cfg.pde_scale < 0.0 || override_cfg.dcr_scale < 0.0 || override_cfg.gain_scale < 0.0) {
      throw std::runtime_error("channel_overrides pde_scale/dcr_scale/gain_scale must be >= 0.");
    }
    if (std::isfinite(override_cfg.v_bias) && override_cfg.v_bias < 0.0) {
      throw std::runtime_error("channel_overrides.v_bias must be >= 0 when provided.");
    }
    if (std::isfinite(override_cfg.v_bd) && override_cfg.v_bd < 0.0) {
      throw std::runtime_error("channel_overrides.v_bd must be >= 0 when provided.");
    }
    if (std::isfinite(override_cfg.sigma_tts_ns) && override_cfg.sigma_tts_ns < 0.0) {
      throw std::runtime_error("channel_overrides.sigma_tts_ns must be >= 0 when provided.");
    }
    if (std::isfinite(override_cfg.tau_recovery_ns) && override_cfg.tau_recovery_ns < 0.0) {
      throw std::runtime_error("channel_overrides.tau_recovery_ns must be >= 0 when provided.");
    }
  }

  if (cfg.active_area.mode != "fill_factor" && cfg.active_area.mode != "grid") {
    throw std::runtime_error("active_area.mode must be one of: fill_factor, grid.");
  }
  if (!std::isfinite(cfg.active_area.fill_factor) ||
      cfg.active_area.fill_factor < 0.0 || cfg.active_area.fill_factor > 1.0) {
    throw std::runtime_error("active_area.fill_factor must be finite and within [0, 1].");
  }
  for (double value : cfg.active_area.grid) {
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
      throw std::runtime_error("active_area.grid values must be finite and within [0, 1].");
    }
  }
  if (cfg.active_area.mode == "grid") {
    if (cfg.active_area.grid.empty()) {
      throw std::runtime_error("active_area.mode='grid' requires non-empty active_area.grid.");
    }
    if (static_cast<int>(cfg.active_area.grid.size()) != total_channels) {
      throw std::runtime_error(
          "active_area.grid size must equal total configured channel count across detectors.");
    }
    if (cfg.active_area.grid_rows > 0 && cfg.active_area.grid_cols > 0 &&
        cfg.active_area.grid_rows * cfg.active_area.grid_cols != static_cast<int>(cfg.active_area.grid.size())) {
      throw std::runtime_error("active_area.grid_rows * active_area.grid_cols must match active_area.grid size.");
    }
  }

  if (!std::isfinite(cfg.detection.pde_scale) || cfg.detection.pde_scale < 0.0 ||
      !std::isfinite(cfg.detection.sigma_gain) || cfg.detection.sigma_gain < 0.0) {
    throw std::runtime_error("detection.pde_scale and detection.sigma_gain must be finite and >= 0.");
  }
  if (!std::isfinite(cfg.detection.angular_efficiency.cos_power) ||
      cfg.detection.angular_efficiency.cos_power < 0.0) {
    throw std::runtime_error("detection.angular_efficiency.cos_power must be finite and >= 0.");
  }
  if (!std::isfinite(cfg.detection.angular_efficiency.n_incident) ||
      !std::isfinite(cfg.detection.angular_efficiency.n_sensor) ||
      cfg.detection.angular_efficiency.n_incident <= 0.0 ||
      cfg.detection.angular_efficiency.n_sensor <= 0.0) {
    throw std::runtime_error("detection.angular_efficiency refractive indices must be finite and > 0.");
  }
  const std::string angular_model = Normalize(cfg.detection.angular_efficiency.model);
  if (angular_model != "cos_power" && angular_model != "flat" && angular_model != "fresnel") {
    throw std::runtime_error("detection.angular_efficiency.model must be one of: cos_power, flat, fresnel.");
  }
  if (cfg.detection.use_direct_pde) {
    ValidateProbabilityTable("detection.pde_table", cfg.detection.pde_table, true);
  } else {
    ValidateProbabilityTable("detection.internal_qe_table", cfg.detection.internal_qe_table, true);
    ValidateProbabilityTable("detection.avalanche_prob_table", cfg.detection.avalanche_prob_table, true);
  }

  if (!std::isfinite(cfg.noise.dcr_hz) || cfg.noise.dcr_hz < 0.0 ||
      !std::isfinite(cfg.noise.xt_prob) || cfg.noise.xt_prob < 0.0 || cfg.noise.xt_prob > 1.0 ||
      !std::isfinite(cfg.noise.ap_prob) || cfg.noise.ap_prob < 0.0 || cfg.noise.ap_prob > 1.0 ||
      !std::isfinite(cfg.noise.ap_tau_ns) || cfg.noise.ap_tau_ns < 0.0) {
    throw std::runtime_error("noise fields must be finite and inside valid ranges.");
  }
  if (cfg.noise.ap_prob > 0.0 && cfg.noise.ap_tau_ns <= 0.0) {
    throw std::runtime_error("noise.ap_tau_ns must be > 0 when noise.ap_prob > 0.");
  }
  if (cfg.noise.xt_neighbor_mode != "4" && cfg.noise.xt_neighbor_mode != "8") {
    throw std::runtime_error("noise.xt_neighbor_mode must be '4' or '8'.");
  }

  if (!std::isfinite(cfg.timing.sigma_tts_ns) || cfg.timing.sigma_tts_ns < 0.0 ||
      !std::isfinite(cfg.timing.sigma_t0_ns) || cfg.timing.sigma_t0_ns < 0.0 ||
      !std::isfinite(cfg.timing.tau_rise_ns) || cfg.timing.tau_rise_ns <= 0.0 ||
      !std::isfinite(cfg.timing.tau_decay_ns) || cfg.timing.tau_decay_ns <= 0.0 ||
      !std::isfinite(cfg.timing.tau_dead_ns) || cfg.timing.tau_dead_ns < 0.0) {
    throw std::runtime_error("timing fields must be finite and within valid ranges.");
  }
  if (!std::isfinite(cfg.window.padding_ns) || cfg.window.padding_ns < 0.0 ||
      !std::isfinite(cfg.window.sim_window_ns) || cfg.window.sim_window_ns < 0.0) {
    throw std::runtime_error("window fields must be finite and >= 0.");
  }

  if (!std::isfinite(cfg.electronics.v_thresh_pe) ||
      !std::isfinite(cfg.electronics.adc_ped) ||
      !std::isfinite(cfg.electronics.sigma_noise_pe) ||
      !std::isfinite(cfg.electronics.t_pre_ns) ||
      !std::isfinite(cfg.electronics.t_gate_ns) ||
      !std::isfinite(cfg.electronics.gain_calib) ||
      !std::isfinite(cfg.electronics.delta_clock_ns) ||
      !std::isfinite(cfg.electronics.tau_recovery_ns) ||
      !std::isfinite(cfg.electronics.sample_dt_ns) ||
      !std::isfinite(cfg.electronics.voltage_scale)) {
    throw std::runtime_error("electronics fields must be finite.");
  }
  if (cfg.electronics.sigma_noise_pe < 0.0 || cfg.electronics.t_pre_ns < 0.0 ||
      cfg.electronics.t_gate_ns < 0.0 || cfg.electronics.tau_recovery_ns < 0.0 ||
      cfg.electronics.n_bits <= 0 || cfg.electronics.n_bits > 16 ||
      cfg.electronics.gain_calib <= 0.0 || cfg.electronics.delta_clock_ns <= 0.0 ||
      cfg.electronics.sample_dt_ns <= 0.0 || cfg.electronics.max_samples < 0) {
    throw std::runtime_error("electronics fields contain invalid ranges.");
  }

  if (!std::isfinite(cfg.dnl.sigma_dnl) || cfg.dnl.sigma_dnl < 0.0) {
    throw std::runtime_error("dnl.sigma_dnl must be finite and >= 0.");
  }
  for (double coeff : cfg.dnl.inl_coeffs) {
    if (!std::isfinite(coeff)) {
      throw std::runtime_error("dnl.inl_coeffs must contain finite values.");
    }
  }
  if (cfg.dnl.mode != "none" && cfg.dnl.mode != "random" && cfg.dnl.mode != "odd_even") {
    throw std::runtime_error("dnl.mode must be one of: none, random, odd_even.");
  }

  if (cfg.trigger.mode != "off" && cfg.trigger.mode != "daq") {
    throw std::runtime_error("trigger.mode must be one of: off, daq.");
  }
  if (cfg.trigger.multiplicity_req <= 0 || !std::isfinite(cfg.trigger.q_trig_pe) ||
      cfg.trigger.q_trig_pe < 0.0 || !std::isfinite(cfg.trigger.t_coinc_ns) ||
      cfg.trigger.t_coinc_ns < 0.0) {
    throw std::runtime_error("trigger fields contain invalid ranges.");
  }

  if (cfg.zero_suppression.neighbor_radius < 0) {
    throw std::runtime_error("zero_suppression.neighbor_radius must be >= 0.");
  }

  cfg.engineering.strict_channel_mapping = true;
  return cfg;
}

}  // namespace phase4
