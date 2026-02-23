#include "PhysicsList.hh"

#include "G4DecayPhysics.hh"
#include "G4EmParameters.hh"
#include "G4EmStandardPhysics_option4.hh"
#include "G4OpticalPhysics.hh"
#include "G4StepLimiterPhysics.hh"
#include "G4SystemOfUnits.hh"

namespace phase1 {

PhysicsList::PhysicsList(const TrackingConfig& tracking_config)
    : tracking_config_(tracking_config) {
  RegisterPhysics(new G4EmStandardPhysics_option4());
  RegisterPhysics(new G4DecayPhysics());
  RegisterPhysics(new G4StepLimiterPhysics());
  RegisterPhysics(new G4OpticalPhysics());
}

void PhysicsList::SetCuts() {
  const auto& delta = tracking_config_.delta_engineering;
  auto* em = G4EmParameters::Instance();
  em->SetMinEnergy(delta.min_energy_mev * MeV);
  em->SetMaxEnergy(delta.max_energy_mev * MeV);
  em->SetLowestElectronEnergy(delta.min_energy_mev * MeV);

  SetCutValue(delta.gamma_cut_mm * mm, "gamma");
  SetCutValue(delta.electron_cut_mm * mm, "e-");
  SetCutValue(delta.positron_cut_mm * mm, "e+");
  SetCutValue(delta.proton_cut_mm * mm, "proton");

  G4VUserPhysicsList::SetCuts();
}

}  // namespace phase1
