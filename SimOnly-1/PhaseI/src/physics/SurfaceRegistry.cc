#include "SurfaceRegistry.hh"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phase1 {

void SurfaceRegistry::AddSurface(const SurfaceInfo& info) {
  surfaces_.push_back(info);
  lookup_[{info.pre_layer, info.post_layer}] = info.id;
  lookup_[{info.post_layer, info.pre_layer}] = info.id;
}

void SurfaceRegistry::AddBowedField(int surface_id, const BowedSurfaceField& field) {
  bowed_fields_[surface_id] = field;
}

int SurfaceRegistry::FindSurfaceId(int pre_layer, int post_layer) const {
  const auto it = lookup_.find({pre_layer, post_layer});
  if (it == lookup_.end()) {
    return -1;
  }
  return it->second;
}

const SurfaceInfo* SurfaceRegistry::GetSurface(int surface_id) const {
  for (const auto& surface : surfaces_) {
    if (surface.id == surface_id) {
      return &surface;
    }
  }
  return nullptr;
}

const std::vector<SurfaceInfo>& SurfaceRegistry::Surfaces() const {
  return surfaces_;
}

double SurfaceRegistry::ComputeMaxDeflectionMm(double pressure_pa,
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

double SurfaceRegistry::DeflectionMm(double r_mm, double radius_mm, double w0_mm) {
  if (radius_mm <= 0.0 || w0_mm == 0.0) {
    return 0.0;
  }
  const double r = std::min(std::abs(r_mm), radius_mm);
  const double u = r / radius_mm;
  const double term = 1.0 - u * u;
  return w0_mm * term * term;
}

G4ThreeVector SurfaceRegistry::NormalAt(double x_mm, double y_mm, double radius_mm, double w0_mm) {
  const double r = std::sqrt(x_mm * x_mm + y_mm * y_mm);
  if (r <= 1e-9 || radius_mm <= 0.0 || w0_mm == 0.0) {
    return G4ThreeVector(0.0, 0.0, 1.0);
  }
  const double u = r / radius_mm;
  const double term = 1.0 - u * u;
  const double dw_dr = (-4.0 * w0_mm * u * term) / radius_mm;
  const double dz_dx = dw_dr * (x_mm / r);
  const double dz_dy = dw_dr * (y_mm / r);
  G4ThreeVector normal(-dz_dx, -dz_dy, 1.0);
  normal = normal.unit();
  return normal;
}

double SurfaceRegistry::DeflectionAt(int surface_id, double x_mm, double y_mm) const {
  const auto it = bowed_fields_.find(surface_id);
  if (it != bowed_fields_.end()) {
    return it->second.SampleW(x_mm, y_mm);
  }
  const auto* surface = GetSurface(surface_id);
  if (!surface || !surface->bowed) {
    return 0.0;
  }
  const double r_mm = std::sqrt(x_mm * x_mm + y_mm * y_mm);
  return DeflectionMm(r_mm, surface->radius_mm, surface->w0_mm);
}

G4ThreeVector SurfaceRegistry::NormalAt(int surface_id, double x_mm, double y_mm) const {
  const auto it = bowed_fields_.find(surface_id);
  if (it != bowed_fields_.end()) {
    double w_mm = 0.0;
    double dw_dx = 0.0;
    double dw_dy = 0.0;
    it->second.SampleWAndGrad(x_mm, y_mm, w_mm, dw_dx, dw_dy);
    G4ThreeVector normal(-dw_dx, -dw_dy, 1.0);
    return normal.unit();
  }
  const auto* surface = GetSurface(surface_id);
  if (!surface || !surface->bowed) {
    return G4ThreeVector(0.0, 0.0, 1.0);
  }
  return NormalAt(x_mm, y_mm, surface->radius_mm, surface->w0_mm);
}

double BowedSurfaceField::SampleW(double x_mm, double y_mm) const {
  double w_mm_out = 0.0;
  double dw_dx = 0.0;
  double dw_dy = 0.0;
  SampleWAndGrad(x_mm, y_mm, w_mm_out, dw_dx, dw_dy);
  return w_mm_out;
}

void BowedSurfaceField::SampleWAndGrad(double x_mm, double y_mm, double& w_mm_out, double& dw_dx, double& dw_dy) const {
  if (nx < 2 || ny < 2 || dx <= 0.0 || dy <= 0.0) {
    w_mm_out = 0.0;
    dw_dx = 0.0;
    dw_dy = 0.0;
    return;
  }

  const double x = std::max(xmin, std::min(x_mm, xmax));
  const double y = std::max(ymin, std::min(y_mm, ymax));

  const double fx = (x - xmin) / dx;
  const double fy = (y - ymin) / dy;
  int ix = static_cast<int>(std::floor(fx));
  int iy = static_cast<int>(std::floor(fy));
  ix = std::max(0, std::min(ix, nx - 2));
  iy = std::max(0, std::min(iy, ny - 2));

  const double tx = (x - (xmin + ix * dx)) / dx;
  const double ty = (y - (ymin + iy * dy)) / dy;

  const double w00 = w_mm[Index(ix, iy)];
  const double w10 = w_mm[Index(ix + 1, iy)];
  const double w01 = w_mm[Index(ix, iy + 1)];
  const double w11 = w_mm[Index(ix + 1, iy + 1)];

  const double w0 = (1.0 - tx) * w00 + tx * w10;
  const double w1 = (1.0 - tx) * w01 + tx * w11;
  w_mm_out = (1.0 - ty) * w0 + ty * w1;

  const double dw_dtx = (w10 - w00) * (1.0 - ty) + (w11 - w01) * ty;
  const double dw_dty = (w01 - w00) * (1.0 - tx) + (w11 - w10) * tx;
  dw_dx = dw_dtx / dx;
  dw_dy = dw_dty / dy;
}

int BowedSurfaceField::Index(int ix, int iy) const {
  return iy * nx + ix;
}

}  // namespace phase1
