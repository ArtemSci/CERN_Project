#include "DetectorConstructionUtils.hh"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "G4MaterialPropertiesTable.hh"
#include "G4PhysicalConstants.hh"
#include "G4SystemOfUnits.hh"
#include "G4TessellatedSolid.hh"
#include "G4ThreeVector.hh"
#include "G4TriangularFacet.hh"

namespace phase3::detector_utils {

std::vector<double> BuildGroupVelocity(const std::vector<double>& lambda_nm,
                                       const std::vector<double>& n_values) {
  std::vector<double> group_vel;
  const size_t n = std::min(lambda_nm.size(), n_values.size());
  group_vel.resize(n, c_light);
  if (n < 2) {
    return group_vel;
  }
  for (size_t i = 0; i < n; ++i) {
    size_t i0 = (i == 0) ? 0 : i - 1;
    size_t i1 = (i + 1 < n) ? i + 1 : n - 1;
    double dl = lambda_nm[i1] - lambda_nm[i0];
    double dn = n_values[i1] - n_values[i0];
    double dn_dlambda = (dl != 0.0) ? (dn / dl) : 0.0;
    double ng = n_values[i] - lambda_nm[i] * dn_dlambda;
    if (ng <= 0.0) {
      ng = n_values[i];
    }
    group_vel[i] = c_light / ng;
  }
  return group_vel;
}

void AddProperty(G4MaterialPropertiesTable* mpt,
                 const char* name,
                 const std::vector<double>& lambda_nm,
                 const std::vector<double>& values,
                 double scale) {
  if (lambda_nm.empty() || values.empty()) {
    return;
  }
  if (lambda_nm.size() != values.size()) {
    throw std::runtime_error(std::string("Optical table size mismatch for property ") + name +
                             ": lambda_nm and values must have equal length.");
  }
  if (lambda_nm.size() < 2) {
    throw std::runtime_error(std::string("Optical table for property ") + name +
                             " must contain at least two points.");
  }
  for (size_t i = 0; i < lambda_nm.size(); ++i) {
    if (!std::isfinite(lambda_nm[i]) || !std::isfinite(values[i])) {
      throw std::runtime_error(std::string("Optical table for property ") + name +
                               " contains non-finite values.");
    }
    if (lambda_nm[i] <= 0.0) {
      throw std::runtime_error(std::string("Optical table for property ") + name +
                               " contains non-positive wavelengths.");
    }
    if (i > 0 && lambda_nm[i] <= lambda_nm[i - 1]) {
      throw std::runtime_error(std::string("Optical table for property ") + name +
                               " wavelengths must be strictly increasing.");
    }
  }
  const size_t n = lambda_nm.size();
  std::vector<G4double> energies;
  std::vector<G4double> props;
  energies.reserve(n);
  props.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const size_t idx = n - 1 - i;
    const double lambda = lambda_nm[idx];
    const double energy = (h_Planck * c_light) / (lambda * nm);
    energies.push_back(energy);
    props.push_back(values[idx] * scale);
  }
  mpt->AddProperty(name, energies.data(), props.data(), static_cast<int>(energies.size()));
}

double ComputeMaxDeflectionMm(double pressure_pa,
                              double radius_mm,
                              double thickness_mm,
                              double youngs_modulus_pa,
                              double poisson) {
  if (pressure_pa <= 0.0 || radius_mm <= 0.0 || thickness_mm <= 0.0 || youngs_modulus_pa <= 0.0) {
    return 0.0;
  }
  const double radius_m = radius_mm * 1e-3;
  const double thickness_m = thickness_mm * 1e-3;
  const double flexural = (youngs_modulus_pa * std::pow(thickness_m, 3.0)) /
                          (12.0 * (1.0 - poisson * poisson));
  if (flexural <= 0.0) {
    return 0.0;
  }
  const double w0_m = (pressure_pa * std::pow(radius_m, 4.0)) / (64.0 * flexural);
  return w0_m * 1e3;
}

G4TessellatedSolid* BuildBowedWindowSolidCylinder(const std::string& name,
                                                  double radius_mm,
                                                  double thickness_mm,
                                                  double w0_mm,
                                                  int radial_samples,
                                                  int phi_samples) {
  constexpr double kPi = 3.141592653589793;
  const int nr = std::max(4, radial_samples);
  const int nphi = std::max(12, phi_samples);
  const double half_thickness = thickness_mm / 2.0;
  const auto w_at_r = [radius_mm, w0_mm](double r_mm) {
    if (radius_mm <= 0.0 || w0_mm == 0.0) {
      return 0.0;
    }
    const double u = std::min(std::abs(r_mm), radius_mm) / radius_mm;
    const double term = 1.0 - u * u;
    return w0_mm * term * term;
  };

  std::vector<G4ThreeVector> top_vertices;
  std::vector<G4ThreeVector> bottom_vertices;
  top_vertices.reserve(1 + nr * nphi);
  bottom_vertices.reserve(1 + nr * nphi);

  top_vertices.emplace_back(0.0, 0.0, (half_thickness + w_at_r(0.0)) * mm);
  bottom_vertices.emplace_back(0.0, 0.0, (-half_thickness + w_at_r(0.0)) * mm);

  for (int ir = 1; ir <= nr; ++ir) {
    const double r = radius_mm * ir / nr;
    const double w = w_at_r(r);
    for (int ip = 0; ip < nphi; ++ip) {
      const double phi = (2.0 * kPi * ip) / nphi;
      const double x = r * std::cos(phi);
      const double y = r * std::sin(phi);
      top_vertices.emplace_back(x * mm, y * mm, (half_thickness + w) * mm);
      bottom_vertices.emplace_back(x * mm, y * mm, (-half_thickness + w) * mm);
    }
  }

  const auto idx = [nphi](int ir, int ip) {
    if (ir == 0) {
      return 0;
    }
    return 1 + (ir - 1) * nphi + (ip % nphi);
  };

  auto* solid = new G4TessellatedSolid(name + "_Solid");

  for (int ip = 0; ip < nphi; ++ip) {
    const int ip_next = (ip + 1) % nphi;
    const auto& v0 = top_vertices[0];
    const auto& v1 = top_vertices[idx(1, ip)];
    const auto& v2 = top_vertices[idx(1, ip_next)];
    solid->AddFacet(new G4TriangularFacet(v0, v1, v2, ABSOLUTE));

    const auto& b0 = bottom_vertices[0];
    const auto& b1 = bottom_vertices[idx(1, ip)];
    const auto& b2 = bottom_vertices[idx(1, ip_next)];
    solid->AddFacet(new G4TriangularFacet(b0, b2, b1, ABSOLUTE));
  }

  for (int ir = 1; ir < nr; ++ir) {
    for (int ip = 0; ip < nphi; ++ip) {
      const int ip_next = (ip + 1) % nphi;
      const auto& t00 = top_vertices[idx(ir, ip)];
      const auto& t10 = top_vertices[idx(ir + 1, ip)];
      const auto& t11 = top_vertices[idx(ir + 1, ip_next)];
      const auto& t01 = top_vertices[idx(ir, ip_next)];
      solid->AddFacet(new G4TriangularFacet(t00, t10, t11, ABSOLUTE));
      solid->AddFacet(new G4TriangularFacet(t00, t11, t01, ABSOLUTE));

      const auto& b00 = bottom_vertices[idx(ir, ip)];
      const auto& b10 = bottom_vertices[idx(ir + 1, ip)];
      const auto& b11 = bottom_vertices[idx(ir + 1, ip_next)];
      const auto& b01 = bottom_vertices[idx(ir, ip_next)];
      solid->AddFacet(new G4TriangularFacet(b00, b11, b10, ABSOLUTE));
      solid->AddFacet(new G4TriangularFacet(b00, b01, b11, ABSOLUTE));
    }
  }

  const int ir = nr;
  for (int ip = 0; ip < nphi; ++ip) {
    const int ip_next = (ip + 1) % nphi;
    const auto& t0 = top_vertices[idx(ir, ip)];
    const auto& t1 = top_vertices[idx(ir, ip_next)];
    const auto& b0 = bottom_vertices[idx(ir, ip)];
    const auto& b1 = bottom_vertices[idx(ir, ip_next)];
    solid->AddFacet(new G4TriangularFacet(t0, b0, b1, ABSOLUTE));
    solid->AddFacet(new G4TriangularFacet(t0, b1, t1, ABSOLUTE));
  }

  solid->SetSolidClosed(true);
  return solid;
}

G4TessellatedSolid* BuildBowedWindowSolidBox(const std::string& name,
                                             double half_x_mm,
                                             double half_y_mm,
                                             double thickness_mm,
                                             double w0_mm,
                                             int samples) {
  const int nx = std::max(4, samples * 2 + 1);
  const int ny = std::max(4, samples * 2 + 1);
  const double half_thickness = thickness_mm / 2.0;

  auto w_at_xy = [half_x_mm, half_y_mm, w0_mm](double x_mm, double y_mm) {
    if (w0_mm == 0.0 || half_x_mm <= 0.0 || half_y_mm <= 0.0) {
      return 0.0;
    }
    const double ux = std::min(std::abs(x_mm), half_x_mm) / half_x_mm;
    const double uy = std::min(std::abs(y_mm), half_y_mm) / half_y_mm;
    const double term_x = 1.0 - ux * ux;
    const double term_y = 1.0 - uy * uy;
    return w0_mm * term_x * term_x * term_y * term_y;
  };

  std::vector<G4ThreeVector> top_vertices(nx * ny);
  std::vector<G4ThreeVector> bottom_vertices(nx * ny);

  for (int iy = 0; iy < ny; ++iy) {
    const double y = -half_y_mm + (2.0 * half_y_mm) * iy / (ny - 1);
    for (int ix = 0; ix < nx; ++ix) {
      const double x = -half_x_mm + (2.0 * half_x_mm) * ix / (nx - 1);
      const double w = w_at_xy(x, y);
      const int idx = iy * nx + ix;
      top_vertices[idx] = G4ThreeVector(x * mm, y * mm, (half_thickness + w) * mm);
      bottom_vertices[idx] = G4ThreeVector(x * mm, y * mm, (-half_thickness + w) * mm);
    }
  }

  auto* solid = new G4TessellatedSolid(name + "_Solid");

  for (int iy = 0; iy < ny - 1; ++iy) {
    for (int ix = 0; ix < nx - 1; ++ix) {
      const int i00 = iy * nx + ix;
      const int i10 = iy * nx + (ix + 1);
      const int i11 = (iy + 1) * nx + (ix + 1);
      const int i01 = (iy + 1) * nx + ix;

      const auto& t00 = top_vertices[i00];
      const auto& t10 = top_vertices[i10];
      const auto& t11 = top_vertices[i11];
      const auto& t01 = top_vertices[i01];
      solid->AddFacet(new G4TriangularFacet(t00, t10, t11, ABSOLUTE));
      solid->AddFacet(new G4TriangularFacet(t00, t11, t01, ABSOLUTE));

      const auto& b00 = bottom_vertices[i00];
      const auto& b10 = bottom_vertices[i10];
      const auto& b11 = bottom_vertices[i11];
      const auto& b01 = bottom_vertices[i01];
      solid->AddFacet(new G4TriangularFacet(b00, b11, b10, ABSOLUTE));
      solid->AddFacet(new G4TriangularFacet(b00, b01, b11, ABSOLUTE));
    }
  }

  for (int ix = 0; ix < nx - 1; ++ix) {
    const int top_idx0 = ix;
    const int top_idx1 = ix + 1;
    const int bottom_idx0 = ix;
    const int bottom_idx1 = ix + 1;
    const auto& t0 = top_vertices[top_idx0];
    const auto& t1 = top_vertices[top_idx1];
    const auto& b0 = bottom_vertices[bottom_idx0];
    const auto& b1 = bottom_vertices[bottom_idx1];
    solid->AddFacet(new G4TriangularFacet(t0, b0, b1, ABSOLUTE));
    solid->AddFacet(new G4TriangularFacet(t0, b1, t1, ABSOLUTE));
  }

  for (int ix = 0; ix < nx - 1; ++ix) {
    const int row = ny - 1;
    const int top_idx0 = row * nx + ix;
    const int top_idx1 = row * nx + ix + 1;
    const int bottom_idx0 = row * nx + ix;
    const int bottom_idx1 = row * nx + ix + 1;
    const auto& t0 = top_vertices[top_idx0];
    const auto& t1 = top_vertices[top_idx1];
    const auto& b0 = bottom_vertices[bottom_idx0];
    const auto& b1 = bottom_vertices[bottom_idx1];
    solid->AddFacet(new G4TriangularFacet(t0, b1, b0, ABSOLUTE));
    solid->AddFacet(new G4TriangularFacet(t0, t1, b1, ABSOLUTE));
  }

  for (int iy = 0; iy < ny - 1; ++iy) {
    const int left_idx0 = iy * nx;
    const int left_idx1 = (iy + 1) * nx;
    const auto& t0 = top_vertices[left_idx0];
    const auto& t1 = top_vertices[left_idx1];
    const auto& b0 = bottom_vertices[left_idx0];
    const auto& b1 = bottom_vertices[left_idx1];
    solid->AddFacet(new G4TriangularFacet(t0, b1, b0, ABSOLUTE));
    solid->AddFacet(new G4TriangularFacet(t0, t1, b1, ABSOLUTE));
  }

  for (int iy = 0; iy < ny - 1; ++iy) {
    const int right_idx0 = iy * nx + (nx - 1);
    const int right_idx1 = (iy + 1) * nx + (nx - 1);
    const auto& t0 = top_vertices[right_idx0];
    const auto& t1 = top_vertices[right_idx1];
    const auto& b0 = bottom_vertices[right_idx0];
    const auto& b1 = bottom_vertices[right_idx1];
    solid->AddFacet(new G4TriangularFacet(t0, b0, b1, ABSOLUTE));
    solid->AddFacet(new G4TriangularFacet(t0, b1, t1, ABSOLUTE));
  }

  solid->SetSolidClosed(true);
  return solid;
}

}  // namespace phase3::detector_utils
