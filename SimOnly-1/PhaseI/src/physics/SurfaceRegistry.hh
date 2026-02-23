#pragma once

#include <map>
#include <string>
#include <vector>

#include "G4ThreeVector.hh"

namespace phase1 {

struct SurfaceInfo {
  int id = 0;
  int pre_layer = -1;
  int post_layer = -1;
  std::string name;
  bool bowed = false;
  double z_plane_mm = 0.0;
  double radius_mm = 0.0;
  double w0_mm = 0.0;
};

struct BowedSurfaceField {
  int nx = 0;
  int ny = 0;
  double xmin = 0.0;
  double xmax = 0.0;
  double ymin = 0.0;
  double ymax = 0.0;
  double dx = 0.0;
  double dy = 0.0;
  std::vector<double> w_mm;

  double SampleW(double x_mm, double y_mm) const;
  void SampleWAndGrad(double x_mm, double y_mm, double& w_mm_out, double& dw_dx, double& dw_dy) const;
  int Index(int ix, int iy) const;
};

class SurfaceRegistry {
 public:
  void AddSurface(const SurfaceInfo& info);
  void AddBowedField(int surface_id, const BowedSurfaceField& field);
  int FindSurfaceId(int pre_layer, int post_layer) const;
  const SurfaceInfo* GetSurface(int surface_id) const;
  const std::vector<SurfaceInfo>& Surfaces() const;

  static double ComputeMaxDeflectionMm(double pressure_pa,
                                       double radius_mm,
                                       double thickness_mm,
                                       double youngs_modulus_pa,
                                       double poisson);
  static double DeflectionMm(double r_mm, double radius_mm, double w0_mm);
  static G4ThreeVector NormalAt(double x_mm, double y_mm, double radius_mm, double w0_mm);
  double DeflectionAt(int surface_id, double x_mm, double y_mm) const;
  G4ThreeVector NormalAt(int surface_id, double x_mm, double y_mm) const;

 private:
  std::vector<SurfaceInfo> surfaces_;
  std::map<std::pair<int, int>, int> lookup_;
  std::map<int, BowedSurfaceField> bowed_fields_;
};

}  // namespace phase1
