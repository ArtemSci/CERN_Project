#include "PhotonSource.hh"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>

#include "util/CsvUtils.hh"

namespace phase3 {
namespace {

using csvutil::SafeParseDouble;
using csvutil::SafeParseInt;
using csvutil::SplitCsv;

}  // namespace

void PhotonSource::SetStrictCsvParsing(bool enabled) {
  strict_csv_parsing_ = enabled;
}

bool PhotonSource::LoadFromCsv(const std::string& path) {
  last_error_.clear();
  event_ids_.clear();
  photons_.clear();

  std::ifstream input(path);
  if (!input) {
    last_error_ = "Failed to open Phase II photons CSV: " + path;
    return false;
  }
  std::string line;
  if (!std::getline(input, line)) {
    last_error_ = "Empty Phase II photons CSV: " + path;
    return false;
  }
  auto header = SplitCsv(line);
  auto index_of = [&](const std::string& key) -> size_t {
    auto it = std::find(header.begin(), header.end(), key);
    return (it == header.end()) ? header.size() : static_cast<size_t>(it - header.begin());
  };

  const size_t event_idx = index_of("event_id");
  const size_t track_idx = index_of("track_id");
  const size_t parent_idx = index_of("parent_id");
  const size_t x_idx = index_of("x_mm");
  const size_t y_idx = index_of("y_mm");
  const size_t z_idx = index_of("z_mm");
  const size_t t_idx = index_of("t_ns");
  const size_t lambda_idx = index_of("lambda_nm");
  const size_t dx_idx = index_of("dir_x");
  const size_t dy_idx = index_of("dir_y");
  const size_t dz_idx = index_of("dir_z");
  const size_t px_idx = index_of("pol_x");
  const size_t py_idx = index_of("pol_y");
  const size_t pz_idx = index_of("pol_z");
  const size_t weight_idx = index_of("weight");
  if (strict_csv_parsing_) {
    if (event_idx == header.size() || track_idx == header.size() || parent_idx == header.size() ||
        x_idx == header.size() || y_idx == header.size() || z_idx == header.size() ||
        t_idx == header.size() || lambda_idx == header.size() || dx_idx == header.size() ||
        dy_idx == header.size() || dz_idx == header.size() || px_idx == header.size() ||
        py_idx == header.size() || pz_idx == header.size()) {
      last_error_ = "Phase II photons CSV missing required columns.";
      return false;
    }
  }

  int raw_index = 0;
  int row_num = 1;
  while (std::getline(input, line)) {
    ++row_num;
    auto cols = SplitCsv(line);
    PhotonRecord record;
    if (strict_csv_parsing_) {
      if (!csvutil::TryParseInt(cols, event_idx, record.event_id) ||
          !csvutil::TryParseInt(cols, track_idx, record.track_id) ||
          !csvutil::TryParseInt(cols, parent_idx, record.parent_id) ||
          !csvutil::TryParseDouble(cols, x_idx, record.x_mm) ||
          !csvutil::TryParseDouble(cols, y_idx, record.y_mm) ||
          !csvutil::TryParseDouble(cols, z_idx, record.z_mm) ||
          !csvutil::TryParseDouble(cols, t_idx, record.t_ns) ||
          !csvutil::TryParseDouble(cols, lambda_idx, record.lambda_nm) ||
          !csvutil::TryParseDouble(cols, dx_idx, record.dir_x) ||
          !csvutil::TryParseDouble(cols, dy_idx, record.dir_y) ||
          !csvutil::TryParseDouble(cols, dz_idx, record.dir_z) ||
          !csvutil::TryParseDouble(cols, px_idx, record.pol_x) ||
          !csvutil::TryParseDouble(cols, py_idx, record.pol_y) ||
          !csvutil::TryParseDouble(cols, pz_idx, record.pol_z)) {
        last_error_ = "Invalid row in Phase II photons CSV at row " + std::to_string(row_num);
        return false;
      }
      if (!std::isfinite(record.x_mm) || !std::isfinite(record.y_mm) || !std::isfinite(record.z_mm) ||
          !std::isfinite(record.t_ns) || !std::isfinite(record.lambda_nm) ||
          !std::isfinite(record.dir_x) || !std::isfinite(record.dir_y) || !std::isfinite(record.dir_z) ||
          !std::isfinite(record.pol_x) || !std::isfinite(record.pol_y) || !std::isfinite(record.pol_z) ||
          record.lambda_nm <= 0.0) {
        last_error_ = "Non-finite photon state in Phase II photons CSV at row " + std::to_string(row_num);
        return false;
      }
    } else {
      record.event_id = SafeParseInt(cols, event_idx, 0);
      record.track_id = SafeParseInt(cols, track_idx, 0);
      record.parent_id = SafeParseInt(cols, parent_idx, 0);
      record.x_mm = SafeParseDouble(cols, x_idx, 0.0);
      record.y_mm = SafeParseDouble(cols, y_idx, 0.0);
      record.z_mm = SafeParseDouble(cols, z_idx, 0.0);
      record.t_ns = SafeParseDouble(cols, t_idx, 0.0);
      record.lambda_nm = SafeParseDouble(cols, lambda_idx, 400.0);
      record.dir_x = SafeParseDouble(cols, dx_idx, 0.0);
      record.dir_y = SafeParseDouble(cols, dy_idx, 0.0);
      record.dir_z = SafeParseDouble(cols, dz_idx, 1.0);
      record.pol_x = SafeParseDouble(cols, px_idx, 1.0);
      record.pol_y = SafeParseDouble(cols, py_idx, 0.0);
      record.pol_z = SafeParseDouble(cols, pz_idx, 0.0);
    }

    const double dir_norm = std::sqrt(record.dir_x * record.dir_x +
                                      record.dir_y * record.dir_y +
                                      record.dir_z * record.dir_z);
    if (!(dir_norm > 0.0) || !std::isfinite(dir_norm)) {
      if (strict_csv_parsing_) {
        last_error_ = "Invalid photon direction in Phase II photons CSV at row " + std::to_string(row_num);
        return false;
      }
      record.dir_x = 0.0;
      record.dir_y = 0.0;
      record.dir_z = 1.0;
    } else {
      record.dir_x /= dir_norm;
      record.dir_y /= dir_norm;
      record.dir_z /= dir_norm;
    }

    const double pol_norm = std::sqrt(record.pol_x * record.pol_x +
                                      record.pol_y * record.pol_y +
                                      record.pol_z * record.pol_z);
    if (!(pol_norm > 0.0) || !std::isfinite(pol_norm)) {
      if (strict_csv_parsing_) {
        last_error_ = "Invalid photon polarization in Phase II photons CSV at row " + std::to_string(row_num);
        return false;
      }
      record.pol_x = 1.0;
      record.pol_y = 0.0;
      record.pol_z = 0.0;
    } else {
      record.pol_x /= pol_norm;
      record.pol_y /= pol_norm;
      record.pol_z /= pol_norm;
    }

    double weight = SafeParseDouble(cols, weight_idx, 1.0);
    if (!std::isfinite(weight) || weight <= 0.0) {
      if (strict_csv_parsing_) {
        last_error_ = "Invalid photon weight in Phase II photons CSV at row " + std::to_string(row_num);
        return false;
      }
      ++raw_index;
      continue;
    }

    if (weight > static_cast<double>(std::numeric_limits<int>::max())) {
      weight = static_cast<double>(std::numeric_limits<int>::max());
    }

    int copies = static_cast<int>(std::floor(weight));
    const double fractional = std::max(0.0, weight - static_cast<double>(copies));
    if (fractional > 0.0) {
      std::seed_seq seed_seq{
          0x504833u,
          static_cast<uint32_t>(record.event_id),
          static_cast<uint32_t>(record.track_id),
          static_cast<uint32_t>(raw_index)};
      std::mt19937 rng(seed_seq);
      std::uniform_real_distribution<double> uni(0.0, 1.0);
      if (uni(rng) < fractional) {
        ++copies;
      }
    }

    for (int i = 0; i < copies; ++i) {
      photons_[record.event_id].push_back(record);
    }
    ++raw_index;
  }

  event_ids_.clear();
  for (const auto& entry : photons_) {
    event_ids_.push_back(entry.first);
  }
  if (event_ids_.empty()) {
    last_error_ = "No valid photons loaded from CSV: " + path;
    return false;
  }
  return true;
}

int PhotonSource::EventCount() const {
  return static_cast<int>(event_ids_.size());
}

int PhotonSource::TotalPhotons() const {
  int total = 0;
  for (const auto& entry : photons_) {
    total += static_cast<int>(entry.second.size());
  }
  return total;
}

const std::vector<int>& PhotonSource::EventIds() const {
  return event_ids_;
}

const std::vector<PhotonRecord>& PhotonSource::PhotonsForEvent(int event_index) const {
  static const std::vector<PhotonRecord> empty;
  if (event_index < 0 || event_index >= static_cast<int>(event_ids_.size())) {
    return empty;
  }
  auto it = photons_.find(event_ids_[event_index]);
  if (it == photons_.end()) {
    return empty;
  }
  return it->second;
}

const std::string& PhotonSource::LastError() const {
  return last_error_;
}

}  // namespace phase3
