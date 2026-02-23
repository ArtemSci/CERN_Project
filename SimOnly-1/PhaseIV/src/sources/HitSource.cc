#include "HitSource.hh"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "util/CsvUtils.hh"

namespace phase4 {
namespace {

using csvutil::SplitCsv;

}  // namespace

bool HitSource::LoadFromCsv(const std::string& path) {
  hits_.clear();
  event_ids_.clear();
  std::ifstream input(path);
  if (!input) {
    return false;
  }

  std::string line;
  if (!std::getline(input, line)) {
    return false;
  }
  auto header = SplitCsv(line);
  auto index_of = [&](const std::string& key) -> size_t {
    auto it = std::find(header.begin(), header.end(), key);
    return (it == header.end()) ? header.size() : static_cast<size_t>(it - header.begin());
  };

  const size_t event_idx = index_of("event_id");
  const size_t track_idx = index_of("track_id");
  const size_t detector_idx = index_of("detector_id");
  const size_t channel_idx = index_of("channel_id");
  const size_t x_idx = index_of("x_mm");
  const size_t y_idx = index_of("y_mm");
  const size_t z_idx = index_of("z_mm");
  const size_t t_idx = index_of("t_ns");
  const size_t lambda_idx = index_of("lambda_nm");
  const size_t angle_idx = index_of("incidence_angle_deg");
  const size_t dir_x_idx = index_of("dir_x");
  const size_t dir_y_idx = index_of("dir_y");
  const size_t dir_z_idx = index_of("dir_z");
  const size_t pol_x_idx = index_of("pol_x");
  const size_t pol_y_idx = index_of("pol_y");
  const size_t pol_z_idx = index_of("pol_z");
  if (event_idx == header.size() || track_idx == header.size() ||
      detector_idx == header.size() || channel_idx == header.size() ||
      x_idx == header.size() || y_idx == header.size() || z_idx == header.size() ||
      t_idx == header.size() || lambda_idx == header.size() || angle_idx == header.size()) {
    return false;
  }

  while (std::getline(input, line)) {
    auto cols = SplitCsv(line);
    HitRecord record;
    if (!csvutil::TryParseInt(cols, event_idx, record.event_id) ||
        !csvutil::TryParseInt(cols, track_idx, record.track_id) ||
        !csvutil::TryParseInt(cols, channel_idx, record.channel_id) ||
        !csvutil::TryParseDouble(cols, x_idx, record.x_mm) ||
        !csvutil::TryParseDouble(cols, y_idx, record.y_mm) ||
        !csvutil::TryParseDouble(cols, z_idx, record.z_mm) ||
        !csvutil::TryParseDouble(cols, t_idx, record.t_ns) ||
        !csvutil::TryParseDouble(cols, lambda_idx, record.lambda_nm) ||
        !csvutil::TryParseDouble(cols, angle_idx, record.incidence_angle_deg)) {
      return false;
    }
    record.detector_id = (detector_idx < cols.size()) ? cols[detector_idx] : "";
    if (record.detector_id.empty()) {
      return false;
    }
    if (dir_x_idx < cols.size()) {
      csvutil::TryParseDouble(cols, dir_x_idx, record.dir_x);
    }
    if (dir_y_idx < cols.size()) {
      csvutil::TryParseDouble(cols, dir_y_idx, record.dir_y);
    }
    if (dir_z_idx < cols.size()) {
      csvutil::TryParseDouble(cols, dir_z_idx, record.dir_z);
    }
    if (pol_x_idx < cols.size()) {
      csvutil::TryParseDouble(cols, pol_x_idx, record.pol_x);
    }
    if (pol_y_idx < cols.size()) {
      csvutil::TryParseDouble(cols, pol_y_idx, record.pol_y);
    }
    if (pol_z_idx < cols.size()) {
      csvutil::TryParseDouble(cols, pol_z_idx, record.pol_z);
    }

    if (!std::isfinite(record.x_mm) || !std::isfinite(record.y_mm) || !std::isfinite(record.z_mm) ||
        !std::isfinite(record.t_ns) || !std::isfinite(record.lambda_nm) ||
        !std::isfinite(record.incidence_angle_deg) || !std::isfinite(record.dir_x) ||
        !std::isfinite(record.dir_y) || !std::isfinite(record.dir_z) ||
        !std::isfinite(record.pol_x) || !std::isfinite(record.pol_y) || !std::isfinite(record.pol_z) ||
        record.lambda_nm <= 0.0 || record.channel_id < 0) {
      return false;
    }
    hits_[record.event_id].push_back(record);
  }

  for (const auto& entry : hits_) {
    event_ids_.push_back(entry.first);
  }
  return true;
}

int HitSource::EventCount() const {
  return static_cast<int>(event_ids_.size());
}

int HitSource::TotalHits() const {
  int total = 0;
  for (const auto& entry : hits_) {
    total += static_cast<int>(entry.second.size());
  }
  return total;
}

const std::vector<int>& HitSource::EventIds() const {
  return event_ids_;
}

const std::vector<HitRecord>& HitSource::HitsForEvent(int event_index) const {
  static const std::vector<HitRecord> empty;
  if (event_index < 0 || event_index >= static_cast<int>(event_ids_.size())) {
    return empty;
  }
  auto it = hits_.find(event_ids_[event_index]);
  if (it == hits_.end()) {
    return empty;
  }
  return it->second;
}

}  // namespace phase4
