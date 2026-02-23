#include "DigiProcessor.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"
#include "util/CsvUtils.hh"

namespace phase4 {
namespace {

using json = nlohmann::json;

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

}  // namespace

DigiProcessor::DigiProcessor(const PhaseIVConfig& config)
    : config_(config) {}

std::string DigiProcessor::ChannelKey(const std::string& detector_id, int channel_id) const {
  return detector_id + "#" + std::to_string(channel_id);
}

int DigiProcessor::ResolveGlobalChannel(const std::string& detector_id, int channel_id) const {
  if (channel_id < 0) {
    return -1;
  }
  auto it = global_channel_by_key_.find(ChannelKey(detector_id, channel_id));
  if (it == global_channel_by_key_.end()) {
    return -1;
  }
  return it->second;
}

bool DigiProcessor::BuildChannelLayout() {
  detector_layouts_.clear();
  detector_index_by_id_.clear();
  global_channel_by_key_.clear();
  calibrations_.clear();

  int base_channel = 0;
  for (size_t i = 0; i < config_.detectors.size(); ++i) {
    DetectorLayout layout;
    layout.config = config_.detectors[i];
    layout.base_channel = base_channel;

    detector_layouts_.push_back(layout);
    detector_index_by_id_[layout.config.id] = static_cast<int>(i);
    for (int local = 0; local < layout.config.channels; ++local) {
      const int global = layout.base_channel + local;
      global_channel_by_key_[ChannelKey(layout.config.id, local)] = global;

      ChannelCalib calib;
      calib.detector_index = static_cast<int>(i);
      calib.local_channel = local;
      calib.pde_scale = layout.config.pde_scale;
      calib.time_offset_ns = layout.config.time_offset_ns;
      calib.dcr_scale = layout.config.dcr_scale;
      calib.gain_scale = layout.config.gain_scale;
      calib.v_bias = layout.config.v_bias;
      calib.v_bd = layout.config.v_bd;
      calib.sigma_tts_ns = layout.config.sigma_tts_ns;
      calib.tau_recovery_ns = layout.config.tau_recovery_ns;
      calibrations_[global] = calib;
    }
    base_channel += layout.config.channels;
  }

  for (const auto& entry : config_.channel_overrides) {
    const int global = ResolveGlobalChannel(entry.detector_id, entry.channel_id);
    if (global < 0) {
      last_error_ = "Channel override references unknown detector/channel: " +
                    entry.detector_id + ":" + std::to_string(entry.channel_id);
      return false;
    }
    auto it = calibrations_.find(global);
    if (it == calibrations_.end()) {
      last_error_ = "Internal calibration map error for channel override on channel " +
                    std::to_string(global);
      return false;
    }
    it->second.pde_scale *= entry.pde_scale;
    it->second.time_offset_ns += entry.time_offset_ns;
    it->second.dcr_scale *= entry.dcr_scale;
    it->second.gain_scale *= entry.gain_scale;
    if (std::isfinite(entry.v_bias)) {
      it->second.v_bias = entry.v_bias;
    }
    if (std::isfinite(entry.v_bd)) {
      it->second.v_bd = entry.v_bd;
    }
    if (std::isfinite(entry.sigma_tts_ns)) {
      it->second.sigma_tts_ns = entry.sigma_tts_ns;
    }
    if (std::isfinite(entry.tau_recovery_ns)) {
      it->second.tau_recovery_ns = entry.tau_recovery_ns;
    }
  }

  return true;
}

bool DigiProcessor::LoadHits() {
  last_error_.clear();
  const std::string path = config_.input.phase3_output + "/" + config_.input.hits_csv;
  if (!hits_.LoadFromCsv(path)) {
    last_error_ = "Failed to load Phase III hits: " + path;
    return false;
  }
  if (!BuildChannelLayout()) {
    if (last_error_.empty()) {
      last_error_ = "Failed to construct detector/channel layout.";
    }
    return false;
  }

  for (int event_index = 0; event_index < hits_.EventCount(); ++event_index) {
    const auto& event_hits = hits_.HitsForEvent(event_index);
    for (const auto& hit : event_hits) {
      const int channel = ResolveGlobalChannel(hit.detector_id, hit.channel_id);
      if (channel < 0 || channel >= ChannelCount()) {
        last_error_ = "Hit contains unmapped detector/channel: detector_id='" + hit.detector_id +
                      "' channel_id=" + std::to_string(hit.channel_id);
        WriteFailureDebug("channel_mapping", last_error_, &hit);
        return false;
      }
    }
  }

  if (!LoadCalibration()) {
    return false;
  }
  return LoadTransportMetadata();
}

bool DigiProcessor::LoadCalibration() {
  if (config_.calibration.calibration_csv.empty()) {
    return true;
  }
  std::ifstream input(config_.calibration.calibration_csv);
  if (!input) {
    last_error_ = "Failed to open calibration CSV: " + config_.calibration.calibration_csv;
    return false;
  }

  std::string line;
  if (!std::getline(input, line)) {
    return true;
  }
  auto header = csvutil::SplitCsv(line);
  auto index_of = [&](const std::string& key) -> size_t {
    auto it = std::find(header.begin(), header.end(), key);
    return (it == header.end()) ? header.size() : static_cast<size_t>(it - header.begin());
  };

  const size_t detector_idx = index_of("detector_id");
  const size_t channel_idx = index_of("channel_id");
  const size_t global_idx = index_of("global_channel_id");
  const size_t pde_idx = index_of("pde_scale");
  const size_t t_idx = index_of("time_offset_ns");
  const size_t dcr_idx = index_of("dcr_scale");
  const size_t gain_idx = index_of("gain_scale");
  const size_t v_bias_idx = index_of("v_bias");
  const size_t v_bd_idx = index_of("v_bd");
  const size_t sigma_tts_idx = index_of("sigma_tts_ns");
  const size_t tau_recovery_idx = index_of("tau_recovery_ns");
  if (channel_idx == header.size() && global_idx == header.size()) {
    last_error_ = "Calibration CSV requires either channel_id or global_channel_id column.";
    return false;
  }

  int row_num = 1;
  while (std::getline(input, line)) {
    ++row_num;
    auto cols = csvutil::SplitCsv(line);

    int global_channel = -1;
    if (global_idx < cols.size()) {
      if (!csvutil::TryParseInt(cols, global_idx, global_channel) || global_channel < 0) {
        last_error_ = "Invalid global_channel_id in calibration CSV at row " + std::to_string(row_num);
        return false;
      }
    } else {
      int channel_id = -1;
      if (!csvutil::TryParseInt(cols, channel_idx, channel_id) || channel_id < 0) {
        last_error_ = "Invalid channel_id in calibration CSV at row " + std::to_string(row_num);
        return false;
      }
      if (detector_idx < cols.size() && !cols[detector_idx].empty()) {
        global_channel = ResolveGlobalChannel(cols[detector_idx], channel_id);
      } else {
        global_channel = channel_id;
      }
    }
    auto calib_it = calibrations_.find(global_channel);
    if (calib_it == calibrations_.end()) {
      last_error_ = "Calibration row references unknown global channel " + std::to_string(global_channel) +
                    " at row " + std::to_string(row_num);
      return false;
    }

    if (pde_idx < cols.size()) {
      double value = 1.0;
      if (!csvutil::TryParseDouble(cols, pde_idx, value) || !std::isfinite(value) || value < 0.0) {
        last_error_ = "Invalid pde_scale in calibration CSV row " + std::to_string(row_num);
        return false;
      }
      calib_it->second.pde_scale *= value;
    }
    if (t_idx < cols.size()) {
      double value = 0.0;
      if (!csvutil::TryParseDouble(cols, t_idx, value) || !std::isfinite(value)) {
        last_error_ = "Invalid time_offset_ns in calibration CSV row " + std::to_string(row_num);
        return false;
      }
      calib_it->second.time_offset_ns += value;
    }
    if (dcr_idx < cols.size()) {
      double value = 1.0;
      if (!csvutil::TryParseDouble(cols, dcr_idx, value) || !std::isfinite(value) || value < 0.0) {
        last_error_ = "Invalid dcr_scale in calibration CSV row " + std::to_string(row_num);
        return false;
      }
      calib_it->second.dcr_scale *= value;
    }
    if (gain_idx < cols.size()) {
      double value = 1.0;
      if (!csvutil::TryParseDouble(cols, gain_idx, value) || !std::isfinite(value) || value < 0.0) {
        last_error_ = "Invalid gain_scale in calibration CSV row " + std::to_string(row_num);
        return false;
      }
      calib_it->second.gain_scale *= value;
    }
    if (v_bias_idx < cols.size()) {
      double value = 0.0;
      if (!csvutil::TryParseDouble(cols, v_bias_idx, value) || !std::isfinite(value) || value < 0.0) {
        last_error_ = "Invalid v_bias in calibration CSV row " + std::to_string(row_num);
        return false;
      }
      calib_it->second.v_bias = value;
    }
    if (v_bd_idx < cols.size()) {
      double value = 0.0;
      if (!csvutil::TryParseDouble(cols, v_bd_idx, value) || !std::isfinite(value) || value < 0.0) {
        last_error_ = "Invalid v_bd in calibration CSV row " + std::to_string(row_num);
        return false;
      }
      calib_it->second.v_bd = value;
    }
    if (sigma_tts_idx < cols.size()) {
      double value = 0.0;
      if (!csvutil::TryParseDouble(cols, sigma_tts_idx, value) || !std::isfinite(value) || value < 0.0) {
        last_error_ = "Invalid sigma_tts_ns in calibration CSV row " + std::to_string(row_num);
        return false;
      }
      calib_it->second.sigma_tts_ns = value;
    }
    if (tau_recovery_idx < cols.size()) {
      double value = 0.0;
      if (!csvutil::TryParseDouble(cols, tau_recovery_idx, value) || !std::isfinite(value) || value < 0.0) {
        last_error_ = "Invalid tau_recovery_ns in calibration CSV row " + std::to_string(row_num);
        return false;
      }
      calib_it->second.tau_recovery_ns = value;
    }
  }
  return true;
}

bool DigiProcessor::LoadTransportMetadata() {
  transport_metadata_ = TransportMetadata{};

  const std::string metrics_path = config_.input.phase3_output + "/" + config_.input.phase3_metrics_json;
  std::ifstream metrics_input(metrics_path);
  if (!metrics_input) {
    last_error_ = "Missing Phase III metrics JSON: " + metrics_path;
    WriteFailureDebug("transport_meta_input", last_error_, nullptr);
    return false;
  }
  try {
    json payload;
    metrics_input >> payload;
    transport_metadata_.phase3_metrics_loaded = true;
    transport_metadata_.phase3_total_photons = payload.at("total_photons").get<int>();
    transport_metadata_.phase3_hit_count = payload.at("hit_count").get<int>();
    transport_metadata_.phase3_absorbed_count = payload.at("absorbed_count").get<int>();
    transport_metadata_.phase3_lost_count = payload.at("lost_count").get<int>();
    transport_metadata_.phase3_boundary_reflections = payload.at("boundary_reflections").get<int>();
  } catch (const std::exception& ex) {
    last_error_ = "Failed to parse Phase III metrics JSON: " + metrics_path + " (" + ex.what() + ")";
    WriteFailureDebug("transport_meta_parse", last_error_, nullptr);
    return false;
  }

  if (hits_.TotalHits() < transport_metadata_.phase3_hit_count) {
    std::ostringstream oss;
    oss << "Phase III/IV hit-count mismatch: metrics.hit_count="
        << transport_metadata_.phase3_hit_count
        << " but loaded hits="
        << hits_.TotalHits();
    last_error_ = oss.str();
    WriteFailureDebug("phase_boundary_accounting", last_error_, nullptr);
    return false;
  }

  const std::string boundary_path = config_.input.phase3_output + "/" + config_.input.boundary_csv;
  std::ifstream boundary_input(boundary_path);
  if (boundary_input) {
    std::string line;
    if (std::getline(boundary_input, line)) {
      auto header = csvutil::SplitCsv(line);
      auto index_of = [&](const std::string& key) -> size_t {
        auto it = std::find(header.begin(), header.end(), key);
        return (it == header.end()) ? header.size() : static_cast<size_t>(it - header.begin());
      };
      const size_t status_idx = index_of("status");
      if (status_idx < header.size()) {
        transport_metadata_.boundary_events_loaded = true;
        while (std::getline(boundary_input, line)) {
          auto cols = csvutil::SplitCsv(line);
          std::string raw = (status_idx < cols.size()) ? cols[status_idx] : "";
          if (raw.empty()) {
            raw = "Unknown";
          }
          transport_metadata_.boundary_event_count += 1;
          transport_metadata_.boundary_status_counts[raw] += 1;

          const std::string status = ToLower(raw);
          if (status.find("reflection") != std::string::npos || status == "backscattering") {
            transport_metadata_.boundary_reflection_count += 1;
            continue;
          }
          if (status.find("absorption") != std::string::npos || status.find("absorbed") != std::string::npos) {
            transport_metadata_.boundary_absorption_count += 1;
            continue;
          }
          if (status.find("detect") != std::string::npos || status.find("sensor") != std::string::npos) {
            transport_metadata_.boundary_detection_count += 1;
            continue;
          }
          if (status.find("lost") != std::string::npos || status.find("outofworld") != std::string::npos ||
              status.find("norindex") != std::string::npos) {
            transport_metadata_.boundary_lost_count += 1;
            continue;
          }
        }
      }
    }
  }

  const std::string transport_path = config_.input.phase3_output + "/" + config_.input.transport_csv;
  std::ifstream transport_input(transport_path);
  if (transport_input) {
    std::string line;
    if (std::getline(transport_input, line)) {
      auto header = csvutil::SplitCsv(line);
      auto index_of = [&](const std::string& key) -> size_t {
        auto it = std::find(header.begin(), header.end(), key);
        return (it == header.end()) ? header.size() : static_cast<size_t>(it - header.begin());
      };
      const size_t type_idx = index_of("type");
      if (type_idx < header.size()) {
        transport_metadata_.transport_events_loaded = true;
        while (std::getline(transport_input, line)) {
          auto cols = csvutil::SplitCsv(line);
          std::string type = (type_idx < cols.size()) ? cols[type_idx] : "";
          if (type.empty()) {
            type = "Unknown";
          }
          transport_metadata_.transport_event_count += 1;
          transport_metadata_.transport_type_counts[type] += 1;
        }
      }
    }
  }

  return true;
}

void DigiProcessor::WriteFailureDebug(const std::string& category,
                                      const std::string& detail,
                                      const HitRecord* hit) const {
  if (!config_.engineering.write_failure_debug) {
    return;
  }
  try {
    std::filesystem::create_directories(config_.output.dir);
    json payload;
    payload["category"] = category;
    payload["detail"] = detail;
    payload["phase3_output"] = config_.input.phase3_output;
    payload["strict_channel_mapping"] = config_.engineering.strict_channel_mapping;
    if (hit != nullptr) {
      payload["event_id"] = hit->event_id;
      payload["track_id"] = hit->track_id;
      payload["detector_id"] = hit->detector_id;
      payload["channel_id"] = hit->channel_id;
      payload["x_mm"] = hit->x_mm;
      payload["y_mm"] = hit->y_mm;
      payload["z_mm"] = hit->z_mm;
      payload["t_ns"] = hit->t_ns;
      payload["lambda_nm"] = hit->lambda_nm;
      payload["incidence_angle_deg"] = hit->incidence_angle_deg;
    }
    const std::filesystem::path out =
        std::filesystem::path(config_.output.dir) / config_.engineering.failure_debug_json;
    std::ofstream output(out);
    if (output) {
      output << payload.dump(2) << "\n";
    }
  } catch (...) {
    // Best-effort debug output; do not throw from error reporting path.
  }
}

}  // namespace phase4
