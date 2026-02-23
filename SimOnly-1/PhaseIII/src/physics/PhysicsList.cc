#include "PhysicsList.hh"

#include "G4EmStandardPhysics.hh"
#include "G4OpticalPhysics.hh"
#include "G4StepLimiterPhysics.hh"

namespace phase3 {

PhysicsList::PhysicsList() {
  RegisterPhysics(new G4EmStandardPhysics());
  RegisterPhysics(new G4StepLimiterPhysics());
  RegisterPhysics(new G4OpticalPhysics());
  SetVerboseLevel(0);
}

}  // namespace phase3
