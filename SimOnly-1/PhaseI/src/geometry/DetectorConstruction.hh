#pragma once

#include <string>
#include <vector>

#include "G4VUserDetectorConstruction.hh"

#include "PhaseIConfig.hh"
#include "SurfaceRegistry.hh"

class G4LogicalVolume;

namespace phase1 {

class DetectorConstruction final : public G4VUserDetectorConstruction {
 public:
  explicit DetectorConstruction(const PhaseIConfig& config);
  G4VPhysicalVolume* Construct() override;

  const SurfaceRegistry& GetSurfaceRegistry() const;

 private:
  const PhaseIConfig& config_;
  SurfaceRegistry surfaces_;
  std::vector<std::string> layer_names_;
};

}  // namespace phase1
