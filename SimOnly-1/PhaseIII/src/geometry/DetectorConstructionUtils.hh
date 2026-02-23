#pragma once

#include <string>
#include <vector>

class G4MaterialPropertiesTable;
class G4TessellatedSolid;

namespace phase3::detector_utils {

std::vector<double> BuildGroupVelocity(const std::vector<double>& lambda_nm,
                                       const std::vector<double>& n_values);

void AddProperty(G4MaterialPropertiesTable* mpt,
                 const char* name,
                 const std::vector<double>& lambda_nm,
                 const std::vector<double>& values,
                 double scale);

double ComputeMaxDeflectionMm(double pressure_pa,
                              double radius_mm,
                              double thickness_mm,
                              double youngs_modulus_pa,
                              double poisson);

G4TessellatedSolid* BuildBowedWindowSolidCylinder(const std::string& name,
                                                  double radius_mm,
                                                  double thickness_mm,
                                                  double w0_mm,
                                                  int radial_samples,
                                                  int phi_samples);

G4TessellatedSolid* BuildBowedWindowSolidBox(const std::string& name,
                                             double half_x_mm,
                                             double half_y_mm,
                                             double thickness_mm,
                                             double w0_mm,
                                             int samples);

}  // namespace phase3::detector_utils
