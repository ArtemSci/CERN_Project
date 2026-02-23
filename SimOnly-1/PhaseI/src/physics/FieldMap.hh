#pragma once

#include <string>
#include <vector>

#include "G4ThreeVector.hh"

namespace phase1 {

class FieldMap {
 public:
  bool LoadFromCsv(const std::string& path);
  bool IsValid() const;
  G4ThreeVector InterpolateTricubic(const G4ThreeVector& pos_mm) const;

 private:
  double dx_ = 0.0;
  double dy_ = 0.0;
  double dz_ = 0.0;
  std::vector<double> xs_;
  std::vector<double> ys_;
  std::vector<double> zs_;
  std::vector<G4ThreeVector> field_;

  size_t Index(size_t ix, size_t iy, size_t iz) const;
  double CubicInterpolate(double p0, double p1, double p2, double p3, double t) const;
  double Clamp(double value, double min_val, double max_val) const;
};

}  // namespace phase1
