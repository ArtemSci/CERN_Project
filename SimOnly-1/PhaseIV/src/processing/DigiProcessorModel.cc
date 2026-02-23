#include "DigiProcessor.hh"
#include "DigiProcessorUtils.hh"

#include <algorithm>
#include <cmath>
#include <limits>

namespace phase4 {

int DigiProcessor::ChannelCount() const {
  int total = 0;
  for (const auto& detector : detector_layouts_) {
    total += detector.config.channels;
  }
  return std::max(0, total);
}

DigiProcessor::ChannelCalib DigiProcessor::GetCalibration(int channel_id) const {
  auto it = calibrations_.find(channel_id);
  if (it == calibrations_.end()) {
    return ChannelCalib{};
  }
  return it->second;
}

const DigiProcessor::DetectorLayout* DigiProcessor::DetectorForChannel(int global_channel) const {
  if (global_channel < 0) {
    return nullptr;
  }
  for (const auto& layout : detector_layouts_) {
    if (global_channel >= layout.base_channel &&
        global_channel < layout.base_channel + layout.config.channels) {
      return &layout;
    }
  }
  return nullptr;
}

double DigiProcessor::EvalTable(const TableConfig& table, double x) const {
  if (table.x.empty() || table.values.empty()) {
    return 0.0;
  }
  const size_t n = std::min(table.x.size(), table.values.size());
  if (n == 1) {
    return table.values[0];
  }
  if (x <= table.x.front()) {
    if (table.extrapolation == "zero") {
      return 0.0;
    }
    return table.values.front();
  }
  if (x >= table.x.back()) {
    if (table.extrapolation == "zero") {
      return 0.0;
    }
    return table.values.back();
  }
  auto upper = std::upper_bound(table.x.begin(), table.x.begin() + static_cast<long>(n), x);
  size_t idx = static_cast<size_t>(upper - table.x.begin());
  if (idx == 0) {
    return table.values[0];
  }
  const double x0 = table.x[idx - 1];
  const double x1 = table.x[idx];
  const double y0 = table.values[idx - 1];
  const double y1 = table.values[idx];
  const double t = (x1 != x0) ? (x - x0) / (x1 - x0) : 0.0;
  return y0 + t * (y1 - y0);
}

double DigiProcessor::AngularEfficiency(double incidence_angle_deg) const {
  const double rad = incidence_angle_deg * (3.141592653589793 / 180.0);
  const double cos_val = std::cos(rad);
  if (cos_val <= 0.0) {
    return 0.0;
  }
  if (config_.detection.angular_efficiency.model == "cos_power") {
    return std::pow(cos_val, config_.detection.angular_efficiency.cos_power);
  }
  if (config_.detection.angular_efficiency.model == "fresnel") {
    const double n1 = config_.detection.angular_efficiency.n_incident;
    const double n2 = config_.detection.angular_efficiency.n_sensor;
    const double sin_i = std::sqrt(std::max(0.0, 1.0 - cos_val * cos_val));
    const double sin_t = (n1 / n2) * sin_i;
    if (sin_t >= 1.0) {
      return 0.0;
    }
    const double cos_t = std::sqrt(std::max(0.0, 1.0 - sin_t * sin_t));
    const double rs_num = n1 * cos_val - n2 * cos_t;
    const double rs_den = n1 * cos_val + n2 * cos_t;
    const double rp_num = n1 * cos_t - n2 * cos_val;
    const double rp_den = n1 * cos_t + n2 * cos_val;
    const double rs = (std::abs(rs_den) > 0.0) ? (rs_num / rs_den) : 1.0;
    const double rp = (std::abs(rp_den) > 0.0) ? (rp_num / rp_den) : 1.0;
    const double reflectance = 0.5 * (rs * rs + rp * rp);
    return digi_utils::Clamp(1.0 - reflectance, 0.0, 1.0);
  }
  return 1.0;
}

double DigiProcessor::ActiveAreaFraction(int channel_id) const {
  if (config_.active_area.mode == "grid") {
    if (channel_id >= 0 && channel_id < static_cast<int>(config_.active_area.grid.size())) {
      return digi_utils::Clamp(config_.active_area.grid[static_cast<size_t>(channel_id)], 0.0, 1.0);
    }
  }
  return digi_utils::Clamp(config_.active_area.fill_factor, 0.0, 1.0);
}

double DigiProcessor::DetectionProbability(const HitRecord& hit, const ChannelCalib& calib) const {
  const int global_channel = ComputeChannelId(hit);
  if (global_channel < 0) {
    return 0.0;
  }
  const double active = ActiveAreaFraction(global_channel);
  const double angular = AngularEfficiency(hit.incidence_angle_deg);
  const double pde_scale = calib.pde_scale * config_.detection.pde_scale;
  double pde = 0.0;
  if (config_.detection.use_direct_pde) {
    pde = EvalTable(config_.detection.pde_table, hit.lambda_nm);
  } else {
    const double v_bias = std::isfinite(calib.v_bias) ? calib.v_bias : config_.sensor.v_bias;
    const double v_bd = std::isfinite(calib.v_bd) ? calib.v_bd : config_.sensor.v_bd;
    const double v_ov = v_bias - v_bd;
    const double qe = EvalTable(config_.detection.internal_qe_table, hit.lambda_nm);
    const double avalanche = EvalTable(config_.detection.avalanche_prob_table, v_ov);
    pde = qe * avalanche;
  }
  return digi_utils::Clamp(active * angular * pde * pde_scale, 0.0, 1.0);
}

int DigiProcessor::ComputeChannelId(const HitRecord& hit) const {
  return ResolveGlobalChannel(hit.detector_id, hit.channel_id);
}

std::vector<int> DigiProcessor::NeighborChannels(int channel_id, const std::string& mode) const {
  std::vector<int> neighbors;
  const DetectorLayout* layout = DetectorForChannel(channel_id);
  if (!layout) {
    return neighbors;
  }

  const int local = channel_id - layout->base_channel;
  if (local < 0 || local >= layout->config.channels) {
    return neighbors;
  }
  if (layout->config.topology == "none") {
    return neighbors;
  }
  if (layout->config.topology == "line") {
    if (local > 0) {
      neighbors.push_back(layout->base_channel + (local - 1));
    }
    if (local + 1 < layout->config.channels) {
      neighbors.push_back(layout->base_channel + (local + 1));
    }
    return neighbors;
  }
  if (layout->config.topology != "grid_xy" || layout->config.grid_x <= 0 || layout->config.grid_y <= 0) {
    return neighbors;
  }

  const int nx = layout->config.grid_x;
  const int ny = layout->config.grid_y;
  const int ix = local % nx;
  const int iy = local / nx;
  const auto add = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= nx || y >= ny) {
      return;
    }
    neighbors.push_back(layout->base_channel + (y * nx + x));
  };

  add(ix - 1, iy);
  add(ix + 1, iy);
  add(ix, iy - 1);
  add(ix, iy + 1);
  if (mode == "8") {
    add(ix - 1, iy - 1);
    add(ix + 1, iy - 1);
    add(ix - 1, iy + 1);
    add(ix + 1, iy + 1);
  }
  return neighbors;
}

}  // namespace phase4
