#pragma once

#include <algorithm>
#include <string>

#include "Randomize.hh"

namespace phase4::digi_utils {

inline double Clamp(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
}

inline double Uniform01() {
  return CLHEP::RandFlat::shoot(0.0, 1.0);
}

inline double Gaussian(double mean, double sigma) {
  if (sigma <= 0.0) {
    return mean;
  }
  return CLHEP::RandGauss::shoot(mean, sigma);
}

inline int Poisson(double mean) {
  if (mean <= 0.0) {
    return 0;
  }
  return static_cast<int>(CLHEP::RandPoisson::shoot(mean));
}

inline std::string ToLower(std::string value) {
  for (auto& ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return value;
}

}  // namespace phase4::digi_utils
