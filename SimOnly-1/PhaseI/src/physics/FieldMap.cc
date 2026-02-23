#include "FieldMap.hh"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace phase1 {
namespace {

struct FieldPoint {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  G4ThreeVector b;
};

bool IsFinitePoint(const FieldPoint& p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::isfinite(p.b.x()) && std::isfinite(p.b.y()) && std::isfinite(p.b.z());
}

bool FindGridIndex(const std::vector<double>& grid, double value, size_t& index) {
  auto it = std::lower_bound(grid.begin(), grid.end(), value);
  const auto tolerance = [value](double a, double b) {
    const double scale = std::max({1.0, std::abs(a), std::abs(b)});
    return std::abs(a - b) <= (1e-6 * scale);
  };
  if (it != grid.end() && tolerance(*it, value)) {
    index = static_cast<size_t>(it - grid.begin());
    return true;
  }
  if (it != grid.begin()) {
    auto prev = it - 1;
    if (tolerance(*prev, value)) {
      index = static_cast<size_t>(prev - grid.begin());
      return true;
    }
  }
  return false;
}

bool HasUniformSpacing(const std::vector<double>& grid, double spacing) {
  if (grid.size() < 2 || spacing <= 0.0) {
    return false;
  }
  const double tol = std::max(1e-9, 1e-6 * std::abs(spacing));
  for (size_t i = 2; i < grid.size(); ++i) {
    const double local = grid[i] - grid[i - 1];
    if (std::abs(local - spacing) > tol) {
      return false;
    }
  }
  return true;
}

bool NearlyEqual(double a, double b) {
  const double scale = std::max({1.0, std::abs(a), std::abs(b)});
  return std::abs(a - b) <= (1e-9 * scale);
}

}  // namespace

bool FieldMap::LoadFromCsv(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return false;
  }

  std::string line;
  std::vector<FieldPoint> points;

  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::stringstream ss(line);
    std::string token;
    std::vector<double> values;
    while (std::getline(ss, token, ',')) {
      try {
        values.push_back(std::stod(token));
      } catch (const std::exception&) {
        values.clear();
        break;
      }
    }
    if (values.size() < 6) {
      continue;
    }
    FieldPoint p;
    p.x = values[0];
    p.y = values[1];
    p.z = values[2];
    p.b = G4ThreeVector(values[3], values[4], values[5]);
    if (!IsFinitePoint(p)) {
      return false;
    }
    points.push_back(p);
  }

  if (points.empty()) {
    return false;
  }

  xs_.clear();
  ys_.clear();
  zs_.clear();
  for (const auto& p : points) {
    xs_.push_back(p.x);
    ys_.push_back(p.y);
    zs_.push_back(p.z);
  }
  std::sort(xs_.begin(), xs_.end());
  std::sort(ys_.begin(), ys_.end());
  std::sort(zs_.begin(), zs_.end());
  const auto near_equal = [](double a, double b) { return std::abs(a - b) <= 1e-9; };
  xs_.erase(std::unique(xs_.begin(), xs_.end(), near_equal), xs_.end());
  ys_.erase(std::unique(ys_.begin(), ys_.end(), near_equal), ys_.end());
  zs_.erase(std::unique(zs_.begin(), zs_.end(), near_equal), zs_.end());

  if (xs_.size() < 4 || ys_.size() < 4 || zs_.size() < 4) {
    return false;
  }

  dx_ = xs_[1] - xs_[0];
  dy_ = ys_[1] - ys_[0];
  dz_ = zs_[1] - zs_[0];

  if (dx_ <= 0.0 || dy_ <= 0.0 || dz_ <= 0.0) {
    return false;
  }
  if (!HasUniformSpacing(xs_, dx_) || !HasUniformSpacing(ys_, dy_) || !HasUniformSpacing(zs_, dz_)) {
    return false;
  }

  field_.assign(xs_.size() * ys_.size() * zs_.size(), G4ThreeVector(0.0, 0.0, 0.0));
  std::vector<uint8_t> filled(field_.size(), 0);

  for (const auto& p : points) {
    size_t i = 0;
    size_t j = 0;
    size_t k = 0;
    if (!FindGridIndex(xs_, p.x, i) || !FindGridIndex(ys_, p.y, j) || !FindGridIndex(zs_, p.z, k)) {
      return false;
    }
    const size_t idx = Index(i, j, k);
    if (filled[idx]) {
      const auto& existing = field_[idx];
      if (!NearlyEqual(existing.x(), p.b.x()) ||
          !NearlyEqual(existing.y(), p.b.y()) ||
          !NearlyEqual(existing.z(), p.b.z())) {
        return false;
      }
      continue;
    }
    field_[idx] = p.b;
    filled[idx] = 1;
  }

  for (uint8_t present : filled) {
    if (!present) {
      return false;
    }
  }

  return true;
}

bool FieldMap::IsValid() const {
  return !field_.empty();
}

size_t FieldMap::Index(size_t ix, size_t iy, size_t iz) const {
  return (iz * ys_.size() + iy) * xs_.size() + ix;
}

double FieldMap::CubicInterpolate(double p0, double p1, double p2, double p3, double t) const {
  const double a0 = 2.0 * p1;
  const double a1 = -p0 + p2;
  const double a2 = 2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3;
  const double a3 = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
  return 0.5 * (a0 + a1 * t + a2 * t * t + a3 * t * t * t);
}

double FieldMap::Clamp(double value, double min_val, double max_val) const {
  return std::max(min_val, std::min(value, max_val));
}

G4ThreeVector FieldMap::InterpolateTricubic(const G4ThreeVector& pos_mm) const {
  if (!IsValid()) {
    return G4ThreeVector(0.0, 0.0, 0.0);
  }

  const double x = Clamp(pos_mm.x(), xs_.front(), xs_.back());
  const double y = Clamp(pos_mm.y(), ys_.front(), ys_.back());
  const double z = Clamp(pos_mm.z(), zs_.front(), zs_.back());

  const double fx = (x - xs_.front()) / dx_;
  const double fy = (y - ys_.front()) / dy_;
  const double fz = (z - zs_.front()) / dz_;

  int ix = static_cast<int>(std::floor(fx));
  int iy = static_cast<int>(std::floor(fy));
  int iz = static_cast<int>(std::floor(fz));

  ix = std::max(1, std::min(ix, static_cast<int>(xs_.size()) - 3));
  iy = std::max(1, std::min(iy, static_cast<int>(ys_.size()) - 3));
  iz = std::max(1, std::min(iz, static_cast<int>(zs_.size()) - 3));

  const double tx = (x - xs_[ix]) / dx_;
  const double ty = (y - ys_[iy]) / dy_;
  const double tz = (z - zs_[iz]) / dz_;

  double comp[3];
  for (int component = 0; component < 3; ++component) {
    double arr_yz[4][4];
    for (int kz = 0; kz < 4; ++kz) {
      for (int jy = 0; jy < 4; ++jy) {
        const auto& p0 = field_[Index(ix - 1, iy - 1 + jy, iz - 1 + kz)];
        const auto& p1 = field_[Index(ix, iy - 1 + jy, iz - 1 + kz)];
        const auto& p2 = field_[Index(ix + 1, iy - 1 + jy, iz - 1 + kz)];
        const auto& p3 = field_[Index(ix + 2, iy - 1 + jy, iz - 1 + kz)];
        const double v0 = (component == 0) ? p0.x() : (component == 1 ? p0.y() : p0.z());
        const double v1 = (component == 0) ? p1.x() : (component == 1 ? p1.y() : p1.z());
        const double v2 = (component == 0) ? p2.x() : (component == 1 ? p2.y() : p2.z());
        const double v3 = (component == 0) ? p3.x() : (component == 1 ? p3.y() : p3.z());
        arr_yz[jy][kz] = CubicInterpolate(v0, v1, v2, v3, tx);
      }
    }

    double arr_z[4];
    for (int kz = 0; kz < 4; ++kz) {
      arr_z[kz] = CubicInterpolate(arr_yz[0][kz], arr_yz[1][kz], arr_yz[2][kz], arr_yz[3][kz], ty);
    }

    comp[component] = CubicInterpolate(arr_z[0], arr_z[1], arr_z[2], arr_z[3], tz);
  }

  return G4ThreeVector(comp[0], comp[1], comp[2]);
}

}  // namespace phase1
