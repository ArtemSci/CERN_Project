#pragma once

#include "G4VModularPhysicsList.hh"
#include "PhaseIConfig.hh"

namespace phase1 {

class PhysicsList final : public G4VModularPhysicsList {
 public:
  explicit PhysicsList(const TrackingConfig& tracking_config);
  ~PhysicsList() override = default;
  void SetCuts() override;

 private:
  TrackingConfig tracking_config_;
};

}  // namespace phase1
