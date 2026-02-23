#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "G4ThreeVector.hh"
#include "G4VUserDetectorConstruction.hh"

#include "PhaseIIIConfig.hh"
#include "PhaseIIIOutput.hh"

class G4LogicalVolume;
class G4Material;
class G4Step;
class G4VPhysicalVolume;
class G4VSolid;

namespace phase3 {

struct DetectorBounds {
  std::string aperture_shape = "box";
  double half_x_mm = 0.0;
  double half_y_mm = 0.0;
  double radius_mm = 0.0;
  double z_min_mm = 0.0;
  double z_max_mm = 0.0;
  bool wall_enabled = false;
  bool wall_absorb = false;
};

class DetectorConstruction : public G4VUserDetectorConstruction {
 public:
  explicit DetectorConstruction(const PhaseIIIConfig& config);

  G4VPhysicalVolume* Construct() override;
  void ConstructSDandField() override;

  void SetOutput(PhaseIIIOutput* output);
  bool IsDetectorVolume(const G4VPhysicalVolume* volume) const;
  bool MakeDetectorHit(const G4Step* step, HitRecord* out) const;
  const G4VPhysicalVolume* WallVolume() const;
  const DetectorBounds& Bounds() const;

 private:
  struct BowingInfo {
    bool enabled = false;
    std::string model = "clamped_plate";
    double pressure_pa = 0.0;
    double youngs_modulus_pa = 0.0;
    double poisson = 0.0;
    double radius_mm = 0.0;
    double override_max_deflection_mm = -1.0;
    int radial_samples = 40;
    int phi_samples = 64;
  };

  struct LayerInfo {
    std::string name;
    std::string material;
    double thickness_mm = 0.0;
    bool is_window = false;
    bool custom_material = false;
    BowingInfo bowing;
  };

  struct GeometryInfo {
    std::string aperture_shape = "box";
    double world_radius_mm = 2000.0;
    double world_half_z_mm = 2000.0;
    double aperture_radius_mm = 500.0;
    double aperture_half_x_mm = 500.0;
    double aperture_half_y_mm = 500.0;
    double stack_center_z_mm = 0.0;
    bool wall_enabled = false;
    double wall_thickness_mm = 0.0;
    std::string wall_material = "G4_Al";
    bool wall_absorb = true;
    std::vector<LayerInfo> layers;
  };

  struct LayerPlacement {
    std::string layer_name;
    std::string material_name;
    G4VPhysicalVolume* volume = nullptr;
    double z_min_mm = 0.0;
    double z_max_mm = 0.0;
  };

  struct DetectorPlacement {
    DetectorConfig config;
    G4LogicalVolume* logical = nullptr;
    G4VPhysicalVolume* volume = nullptr;
  };

  GeometryInfo LoadGeometry() const;
  G4VSolid* BuildDetectorSolid(const DetectorConfig& detector) const;
  G4Material* BuildMaterial(const std::string& name,
                            const MaterialConfig* config,
                            const MaterialConfig* fallback) const;
  G4Material* ResolveMaterial(const std::string& name);
  void ResolveMaterialConfig(const std::string& name,
                             const MaterialConfig*& config,
                             const MaterialConfig*& fallback) const;
  G4Material* ResolveGradientMaterial(const std::string& base_name,
                                      const GradientConfig& gradient,
                                      double layer_thickness_mm,
                                      double slice_offset_mm,
                                      const std::string& material_name);
  MaterialConfig BuildGradientConfig(const MaterialConfig* base_config,
                                     const MaterialConfig* fallback,
                                     const GradientConfig& gradient,
                                     double layer_thickness_mm,
                                     double slice_offset_mm) const;
  void ApplyOpticalProperties(G4Material* material, const MaterialConfig& config) const;
  void ApplySurface(G4LogicalVolume* volume, const SurfaceConfig& surface, bool metal) const;
  void ApplyWindowSurface(G4LogicalVolume* volume, const WindowSurfaceConfig& surface) const;
  void ApplyInterfaceSurface(const std::string& name,
                             const G4VPhysicalVolume* from,
                             const G4VPhysicalVolume* to,
                             const SurfaceConfig& surface) const;
  const GradientConfig* FindGradient(const std::string& layer_name) const;

  PhaseIIIConfig config_;
  mutable std::map<std::string, G4Material*> material_cache_;
  mutable std::map<std::string, G4Material*> gradient_material_cache_;
  mutable std::map<std::string, MaterialConfig> material_table_;
  std::map<std::string, GradientConfig> gradient_table_;
  std::vector<DetectorPlacement> detectors_;
  std::unordered_map<const G4VPhysicalVolume*, int> detector_index_;
  const G4VPhysicalVolume* wall_volume_ = nullptr;
  DetectorBounds bounds_;
  PhaseIIIOutput* output_ = nullptr;
};

}  // namespace phase3
